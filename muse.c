#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>
#include <time.h>
#include "stb_image.h"
#include "stb_image_write.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

typedef struct {
    char name[64];
    int num_colors;
    Color palette[256];
} Theme;

typedef enum {
    DITHER_FLOYD_STEINBERG,
    DITHER_ORDERED,
    DITHER_BAYER,
    DITHER_JJN,
    DITHER_SIERRA,
    DITHER_NONE
} DitherMethod;

const float bayer8x8[8][8] = {
    {0.0/64, 32.0/64, 8.0/64, 40.0/64, 2.0/64, 34.0/64, 10.0/64, 42.0/64},
    {48.0/64, 16.0/64, 56.0/64, 24.0/64, 50.0/64, 18.0/64, 58.0/64, 26.0/64},
    {12.0/64, 44.0/64, 4.0/64, 36.0/64, 14.0/64, 46.0/64, 6.0/64, 38.0/64},
    {60.0/64, 28.0/64, 52.0/64, 20.0/64, 62.0/64, 30.0/64, 54.0/64, 22.0/64},
    {3.0/64, 35.0/64, 11.0/64, 43.0/64, 1.0/64, 33.0/64, 9.0/64, 41.0/64},
    {51.0/64, 19.0/64, 59.0/64, 27.0/64, 49.0/64, 17.0/64, 57.0/64, 25.0/64},
    {15.0/64, 47.0/64, 7.0/64, 39.0/64, 13.0/64, 45.0/64, 5.0/64, 37.0/64},
    {63.0/64, 31.0/64, 55.0/64, 23.0/64, 61.0/64, 29.0/64, 53.0/64, 21.0/64}
};

static inline uint8_t clamp_float(float value) {
    if (value < 0.0f) return 0;
    if (value > 255.0f) return 255;
    return (uint8_t)(roundf(value));
}

#define CACHE_SIZE 65536
Color *color_cache = NULL;

int color_distance_sq_custom(Color a, Color b) {
    int dr = (int)a.r - (int)b.r;
    int dg = (int)a.g - (int)b.g;
    int db = (int)a.b - (int)b.b;
    return (int)(0.299f * dr * dr + 0.587f * dg * dg + 0.114f * db * db);
}

void initialize_cache(const Theme *theme) {
    color_cache = malloc(CACHE_SIZE * sizeof(Color));
    if (!color_cache) {
        fprintf(stderr, "error: could not allocate memory for color cache.\n");
        exit(1);
    }
    for (int r = 0; r < 32; r++) {
        for (int g = 0; g < 64; g++) {
            for (int b = 0; b < 32; b++) {
                int key = (r << 11) | (g << 5) | b;
                Color pixel = { (uint8_t)(r << 3), (uint8_t)(g << 2), (uint8_t)(b << 3) };
                Color closest = theme->palette[0];
                int min_dist = color_distance_sq_custom(pixel, closest);
                for (int j = 1; j < theme->num_colors; j++) {
                    int dist = color_distance_sq_custom(pixel, theme->palette[j]);
                    if (dist < min_dist) {
                        min_dist = dist;
                        closest = theme->palette[j];
                        if (dist == 0) break;
                    }
                }
                color_cache[key] = closest;
            }
        }
    }
}

Color find_closest_color_cached(Color pixel) {
    int r = pixel.r >> 3;
    int g = pixel.g >> 2;
    int b = pixel.b >> 3;
    int key = (r << 11) | (g << 5) | b;
    return color_cache[key];
}

Theme load_palette_file(const char *filename) {
    Theme theme;
    memset(&theme, 0, sizeof(Theme));
    char filepath[512];
    FILE *file;

    file = fopen(filename, "r");
    if (!file) {
        snprintf(filepath, sizeof(filepath), "/usr/local/share/muse/palettes/%s", filename);
        file = fopen(filepath, "r");
        if (!file) {
            fprintf(stderr, "error: could not open palette file '%s' or '%s'.\n", filename, filepath);
            exit(1);
        }
    }

    char *base_name = strrchr(filename, '/');
    base_name = base_name ? base_name + 1 : (char*)filename;
    
    char *dot = strrchr(base_name, '.');
    if (dot) {
        int len = dot - base_name;
        len = len > 63 ? 63 : len;
        strncpy(theme.name, base_name, len);
        theme.name[len] = '\0';
    } else {
        strncpy(theme.name, base_name, 63);
        theme.name[63] = '\0';
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == ';' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        char *p = line;
        while (*p && isspace(*p)) p++;
        if (!*p) continue;

        if (strncmp(p, "ff", 2) == 0 || strncmp(p, "FF", 2) == 0) p += 2;
        
        unsigned int r, g, b;
        char hex[7];
        strncpy(hex, p, 6);
        hex[6] = '\0';

        if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
            if (theme.num_colors < 256) {
                theme.palette[theme.num_colors].r = (uint8_t)r;
                theme.palette[theme.num_colors].g = (uint8_t)g;
                theme.palette[theme.num_colors].b = (uint8_t)b;
                theme.num_colors++;
            } else {
                fprintf(stderr, "warning: maximum palette size of 256 colors reached. additional colors are ignored.\n");
                break;
            }
        } else {
            fprintf(stderr, "warning: invalid color format in palette file: %s", line);
        }
    }

    fclose(file);
    return theme;
}

void apply_ordered_dither(float *image_f, unsigned char *output, int width, int height, const Theme *theme) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            Color old_pixel = {
                clamp_float(image_f[idx]),
                clamp_float(image_f[idx + 1]),
                clamp_float(image_f[idx + 2])
            };
            float pattern = bayer8x8[y % 8][x % 8];
            Color adjusted_pixel = {
                clamp_float(old_pixel.r + (pattern - 0.5f) * 32),
                clamp_float(old_pixel.g + (pattern - 0.5f) * 32),
                clamp_float(old_pixel.b + (pattern - 0.5f) * 32)
            };
            Color new_pixel = find_closest_color_cached(adjusted_pixel);
            output[idx] = new_pixel.r;
            output[idx + 1] = new_pixel.g;
            output[idx + 2] = new_pixel.b;
        }
    }
}

void apply_bayer_dither(float *image_f, unsigned char *output, int width, int height, const Theme *theme) {
    const int matrix[4][4] = {
        { 0, 8, 2, 10},
        {12, 4, 14, 6},
        { 3, 11, 1, 9},
        {15, 7, 13, 5}
    };

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            Color old_pixel = {
                clamp_float(image_f[idx]),
                clamp_float(image_f[idx + 1]),
                clamp_float(image_f[idx + 2])
            };
            float factor = (matrix[y % 4][x % 4] / 16.0f - 0.5f) * 32;
            Color adjusted_pixel = {
                clamp_float(old_pixel.r + factor),
                clamp_float(old_pixel.g + factor),
                clamp_float(old_pixel.b + factor)
            };
            Color new_pixel = find_closest_color_cached(adjusted_pixel);
            output[idx] = new_pixel.r;
            output[idx + 1] = new_pixel.g;
            output[idx + 2] = new_pixel.b;
        }
    }
}

void apply_floyd_steinberg_dither(float *image_f, unsigned char *output, int width, int height, const Theme *theme) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            Color old_pixel = {
                clamp_float(image_f[idx]),
                clamp_float(image_f[idx + 1]),
                clamp_float(image_f[idx + 2])
            };
            Color new_pixel = find_closest_color_cached(old_pixel);
            output[idx] = new_pixel.r;
            output[idx + 1] = new_pixel.g;
            output[idx + 2] = new_pixel.b;

            float err_r = (float)old_pixel.r - (float)new_pixel.r;
            float err_g = (float)old_pixel.g - (float)new_pixel.g;
            float err_b = (float)old_pixel.b - (float)new_pixel.b;

            if (x + 1 < width) {
                int right = idx + 3;
                image_f[right]     += err_r * 7.0f / 16.0f;
                image_f[right + 1] += err_g * 7.0f / 16.0f;
                image_f[right + 2] += err_b * 7.0f / 16.0f;
            }
            if (y + 1 < height) {
                if (x > 0) {
                    int bottom_left = idx + width * 3 - 3;
                    image_f[bottom_left]     += err_r * 3.0f / 16.0f;
                    image_f[bottom_left + 1] += err_g * 3.0f / 16.0f;
                    image_f[bottom_left + 2] += err_b * 3.0f / 16.0f;
                }
                int bottom = idx + width * 3;
                image_f[bottom]     += err_r * 5.0f / 16.0f;
                image_f[bottom + 1] += err_g * 5.0f / 16.0f;
                image_f[bottom + 2] += err_b * 5.0f / 16.0f;
                
                if (x + 1 < width) {
                    int bottom_right = idx + width * 3 + 3;
                    image_f[bottom_right]     += err_r * 1.0f / 16.0f;
                    image_f[bottom_right + 1] += err_g * 1.0f / 16.0f;
                    image_f[bottom_right + 2] += err_b * 1.0f / 16.0f;
                }
            }
        }
    }
}

void apply_jjn_dither(float *image_f, unsigned char *output, int width, int height, const Theme *theme) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            Color old_pixel = {
                clamp_float(image_f[idx]),
                clamp_float(image_f[idx + 1]),
                clamp_float(image_f[idx + 2])
            };
            Color new_pixel = find_closest_color_cached(old_pixel);
            output[idx] = new_pixel.r;
            output[idx + 1] = new_pixel.g;
            output[idx + 2] = new_pixel.b;

            float err_r = (float)old_pixel.r - (float)new_pixel.r;
            float err_g = (float)old_pixel.g - (float)new_pixel.g;
            float err_b = (float)old_pixel.b - (float)new_pixel.b;

            if (x + 1 < width) {
                int right = idx + 3;
                image_f[right]     += err_r * 7.0f / 48.0f;
                image_f[right + 1] += err_g * 7.0f / 48.0f;
                image_f[right + 2] += err_b * 7.0f / 48.0f;
            }
            if (x + 2 < width) {
                int right2 = idx + 6;
                image_f[right2]     += err_r * 5.0f / 48.0f;
                image_f[right2 + 1] += err_g * 5.0f / 48.0f;
                image_f[right2 + 2] += err_b * 5.0f / 48.0f;
            }
            if (y + 1 < height) {
                if (x > 0) {
                    int bottom_left = idx + width * 3 - 3;
                    image_f[bottom_left]     += err_r * 3.0f / 48.0f;
                    image_f[bottom_left + 1] += err_g * 3.0f / 48.0f;
                    image_f[bottom_left + 2] += err_b * 3.0f / 48.0f;
                }
                int bottom = idx + width * 3;
                image_f[bottom]     += err_r * 5.0f / 48.0f;
                image_f[bottom + 1] += err_g * 5.0f / 48.0f;
                image_f[bottom + 2] += err_b * 5.0f / 48.0f;
                
                if (x + 1 < width) {
                    int bottom_right = idx + width * 3 + 3;
                    image_f[bottom_right]     += err_r * 7.0f / 48.0f;
                    image_f[bottom_right + 1] += err_g * 7.0f / 48.0f;
                    image_f[bottom_right + 2] += err_b * 7.0f / 48.0f;
                }
                if (x + 2 < width) {
                    int bottom_right2 = idx + width * 3 + 6;
                    image_f[bottom_right2]     += err_r * 5.0f / 48.0f;
                    image_f[bottom_right2 + 1] += err_g * 5.0f / 48.0f;
                    image_f[bottom_right2 + 2] += err_b * 5.0f / 48.0f;
                }
            }
        }
    }
}

void apply_sierra_dither(float *image_f, unsigned char *output, int width, int height, const Theme *theme) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            Color old_pixel = {
                clamp_float(image_f[idx]),
                clamp_float(image_f[idx + 1]),
                clamp_float(image_f[idx + 2])
            };
            Color new_pixel = find_closest_color_cached(old_pixel);
            output[idx] = new_pixel.r;
            output[idx + 1] = new_pixel.g;
            output[idx + 2] = new_pixel.b;

            float err_r = (float)old_pixel.r - (float)new_pixel.r;
            float err_g = (float)old_pixel.g - (float)new_pixel.g;
            float err_b = (float)old_pixel.b - (float)new_pixel.b;

            if (x + 1 < width) {
                int right = idx + 3;
                image_f[right]     += err_r * 5.0f / 32.0f;
                image_f[right + 1] += err_g * 5.0f / 32.0f;
                image_f[right + 2] += err_b * 5.0f / 32.0f;
            }
            if (x + 2 < width) {
                int right2 = idx + 6;
                image_f[right2]     += err_r * 3.0f / 32.0f;
                image_f[right2 + 1] += err_g * 3.0f / 32.0f;
                image_f[right2 + 2] += err_b * 3.0f / 32.0f;
            }
            if (y + 1 < height) {
                if (x > 0) {
                    int bottom_left = idx + width * 3 - 3;
                    image_f[bottom_left]     += err_r * 2.0f / 32.0f;
                    image_f[bottom_left + 1] += err_g * 2.0f / 32.0f;
                    image_f[bottom_left + 2] += err_b * 2.0f / 32.0f;
                }
                int bottom = idx + width * 3;
                image_f[bottom]     += err_r * 4.0f / 32.0f;
                image_f[bottom + 1] += err_g * 4.0f / 32.0f;
                image_f[bottom + 2] += err_b * 4.0f / 32.0f;
                
                if (x + 1 < width) {
                    int bottom_right = idx + width * 3 + 3;
                    image_f[bottom_right]     += err_r * 5.0f / 32.0f;
                    image_f[bottom_right + 1] += err_g * 5.0f / 32.0f;
                    image_f[bottom_right + 2] += err_b * 5.0f / 32.0f;
                }
                if (x + 2 < width) {
                    int bottom_right2 = idx + width * 3 + 6;
                    image_f[bottom_right2]     += err_r * 3.0f / 32.0f;
                    image_f[bottom_right2 + 1] += err_g * 3.0f / 32.0f;
                    image_f[bottom_right2 + 2] += err_b * 3.0f / 32.0f;
                }
            }
        }
    }
}

void apply_box_blur(float *image_f, int width, int height, int blur_strength) {
    if (blur_strength < 1) return;

    int channels = 3;
    float *temp = malloc(width * height * channels * sizeof(float));
    if (!temp) {
        fprintf(stderr, "error: could not allocate memory for blur operation.\n");
        exit(1);
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;
            int count = 0;
            for (int i = -blur_strength; i <= blur_strength; i++) {
                int nx = x + i;
                if (nx < 0 || nx >= width) continue;
                int n_idx = (y * width + nx) * channels;
                sum_r += image_f[n_idx];
                sum_g += image_f[n_idx + 1];
                sum_b += image_f[n_idx + 2];
                count++;
            }
            int idx = (y * width + x) * channels;
            temp[idx]     = sum_r / count;
            temp[idx + 1] = sum_g / count;
            temp[idx + 2] = sum_b / count;
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;
            int count = 0;
            for (int i = -blur_strength; i <= blur_strength; i++) {
                int ny = y + i;
                if (ny < 0 || ny >= height) continue;
                int n_idx = (ny * width + x) * channels;
                sum_r += temp[n_idx];
                sum_g += temp[n_idx + 1];
                sum_b += temp[n_idx + 2];
                count++;
            }
            int idx = (y * width + x) * channels;
            image_f[idx]     = sum_r / count;
            image_f[idx + 1] = sum_g / count;
            image_f[idx + 2] = sum_b / count;
        }
    }

    free(temp);
}

void apply_super8_effect(float *image_f, int width, int height, int strength) {
    for (int i = 0; i < width * height * 3; i++) {
        float noise = ((rand() / (float)RAND_MAX) - 0.5f) * 2.0f * strength;
        image_f[i] += noise;
        if (image_f[i] < 0.0f) image_f[i] = 0.0f;
        if (image_f[i] > 255.0f) image_f[i] = 255.0f;
    }
}

void apply_super_panavision70_effect(float *image_f, int width, int height, int strength) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            float distance = sqrtf(powf(x - width / 2.0f, 2) + powf(y - height / 2.0f, 2));
            float max_distance = sqrtf(powf(width / 2.0f, 2) + powf(height / 2.0f, 2));
            float vignette = 1.0f - (distance / max_distance) * 0.5f * strength;
            image_f[idx] *= vignette;
            image_f[idx + 1] *= vignette;
            image_f[idx + 2] *= vignette;
            float grain = ((rand() / (float)RAND_MAX) - 0.5f) * 2.0f * strength;
            image_f[idx] += grain;
            image_f[idx + 1] += grain;
            image_f[idx + 2] += grain;
            if (image_f[idx] < 0.0f) image_f[idx] = 0.0f;
            if (image_f[idx] > 255.0f) image_f[idx] = 255.0f;
            if (image_f[idx + 1] < 0.0f) image_f[idx + 1] = 0.0f;
            if (image_f[idx + 1] > 255.0f) image_f[idx + 1] = 255.0f;
            if (image_f[idx + 2] < 0.0f) image_f[idx + 2] = 0.0f;
            if (image_f[idx + 2] > 255.0f) image_f[idx + 2] = 255.0f;
        }
    }
}

void apply_color_grading(float *image_f, int width, int height, float brightness, float contrast, float saturation) {
    for (int i = 0; i < width * height * 3; i += 3) {
        float r = image_f[i];
        float g = image_f[i + 1];
        float b = image_f[i + 2];

        r = (r - 128.0f) * contrast + 128.0f + brightness;
        g = (g - 128.0f) * contrast + 128.0f + brightness;
        b = (b - 128.0f) * contrast + 128.0f + brightness;

        float gray = 0.299f * r + 0.587f * g + 0.114f * b;
        r = gray + (r - gray) * saturation;
        g = gray + (g - gray) * saturation;
        b = gray + (b - gray) * saturation;

        image_f[i]     = r > 255.0f ? 255.0f : (r < 0.0f ? 0.0f : r);
        image_f[i + 1] = g > 255.0f ? 255.0f : (g < 0.0f ? 0.0f : g);
        image_f[i + 2] = b > 255.0f ? 255.0f : (b < 0.0f ? 0.0f : b);
    }
}

void display_palette(const Theme *theme) {
    printf("palette '%s' with %d colors:\n", theme->name, theme->num_colors);
    for(int i = 0; i < theme->num_colors; i++) {
        printf("\033[48;2;%d;%d;%dm  \033[0m ", theme->palette[i].r, theme->palette[i].g, theme->palette[i].b);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (theme->num_colors % 16 != 0)
        printf("\n");
}

int export_palette(const Theme *theme, const char *filename) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "error: could not open file '%s' for writing.\n", filename);
        return 1;
    }
    fprintf(file, "palette '%s' with %d colors:\n", theme->name, theme->num_colors);
    for(int i = 0; i < theme->num_colors; i++) {
        fprintf(file, "%d %d %d\n", theme->palette[i].r, theme->palette[i].g, theme->palette[i].b);
    }
    fclose(file);
    return 0;
}

int extract_palette_from_image(unsigned char *img, int width, int height, Theme *theme) {
    // Quantize colors to 12 bits (4 bits per channel)
    unsigned int color_counts[4096] = {0};
    for(int i = 0; i < width * height * 3; i +=3 ) {
        unsigned int r = img[i] >> 4;
        unsigned int g = img[i+1] >> 4;
        unsigned int b = img[i+2] >> 4;
        unsigned int key = (r << 8) | (g << 4) | b;
        color_counts[key]++;
    }

    // Create an array of color keys and their counts
    typedef struct {
        unsigned int key;
        unsigned int count;
    } ColorCount;

    ColorCount *cc_array = malloc(4096 * sizeof(ColorCount));
    if (!cc_array) {
        fprintf(stderr, "error: could not allocate memory for palette extraction.\n");
        return 1;
    }

    int cc_size =0;
    for(int key =0; key <4096; key++) {
        if(color_counts[key] >0) {
            cc_array[cc_size].key = key;
            cc_array[cc_size].count = color_counts[key];
            cc_size++;
        }
    }

    // Sort the colors by count in descending order
    for(int i =0; i < cc_size -1; i++) {
        for(int j = i+1; j < cc_size; j++) {
            if(cc_array[j].count > cc_array[i].count) {
                ColorCount temp = cc_array[i];
                cc_array[i] = cc_array[j];
                cc_array[j] = temp;
            }
        }
    }

    // Take top 256 colors
    theme->num_colors = cc_size <256 ? cc_size : 256;
    for(int i =0; i < theme->num_colors; i++) {
        unsigned int key = cc_array[i].key;
        theme->palette[i].r = (key >> 8) & 0xF0;
        theme->palette[i].g = (key >> 4) & 0xF0;
        theme->palette[i].b = key & 0xF0;
    }

    strncpy(theme->name, "extracted_palette", sizeof(theme->name)-1);
    theme->name[sizeof(theme->name)-1] = '\0';

    free(cc_array);
    return 0;
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "usage: %s [options] <input_image> <output_image> <palette_file> [dither_method]\n", prog_name);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -b, --blur <strength>          apply blur with specified strength\n");
    fprintf(stderr, "  -s, --super8 <strength>        apply super8 effect with specified strength\n");
    fprintf(stderr, "  -p, --panavision <strength>    apply super panavision 70 effect with specified strength\n");
    fprintf(stderr, "  -B, --brightness <value>       adjust brightness (float)\n");
    fprintf(stderr, "  -C, --contrast <value>         adjust contrast (float)\n");
    fprintf(stderr, "  -S, --saturation <value>       adjust saturation (float)\n");
    fprintf(stderr, "  -E, --export-palette [file]    export the color palette to a .txt file\n");
    fprintf(stderr, "  -h, --help                     display this help message\n");
    fprintf(stderr, "available dither methods: floyd (default), bayer, ordered, jjn, sierra, nodither\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int blur_strength = 0;
    int blur_flag = 0;
    int super8_strength = 0;
    int super8_flag = 0;
    int panavision_strength = 0;
    int panavision_flag = 0;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    int grading_flag = 0;
    int export_flag = 0;
    char export_palette_file[256] = {0};

    static struct option long_options[] = {
        {"blur", required_argument, 0, 'b'},
        {"super8", required_argument, 0, 's'},
        {"panavision", required_argument, 0, 'p'},
        {"brightness", required_argument, 0, 'B'},
        {"contrast", required_argument, 0, 'C'},
        {"saturation", required_argument, 0, 'S'},
        {"export-palette", optional_argument, 0, 'E'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    srand((unsigned int)time(NULL));

    while ((opt = getopt_long(argc, argv, "b:s:p:B:C:S:E::h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b':
                blur_strength = atoi(optarg);
                if (blur_strength < 1) {
                    fprintf(stderr, "error: blur strength must be at least 1.\n");
                    return 1;
                }
                blur_flag = 1;
                break;
            case 's':
                super8_strength = atoi(optarg);
                if (super8_strength < 1) {
                    fprintf(stderr, "error: super8 strength must be at least 1.\n");
                    return 1;
                }
                super8_flag = 1;
                break;
            case 'p':
                panavision_strength = atoi(optarg);
                if (panavision_strength < 1) {
                    fprintf(stderr, "error: panavision strength must be at least 1.\n");
                    return 1;
                }
                panavision_flag = 1;
                break;
            case 'B':
                brightness = atof(optarg);
                grading_flag = 1;
                break;
            case 'C':
                contrast = atof(optarg);
                grading_flag = 1;
                break;
            case 'S':
                saturation = atof(optarg);
                grading_flag = 1;
                break;
            case 'E':
                export_flag = 1;
                if (optarg) {
                    strncpy(export_palette_file, optarg, 255);
                    export_palette_file[255] = '\0';
                } else {
                    strncpy(export_palette_file, "exported_palette.txt", 255);
                    export_palette_file[255] = '\0';
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    int remaining_args = argc - optind;
    if (export_flag && remaining_args == 1) {
        // Extraction mode: ./muse input.jpg -E [export_file]
        const char *input_path = argv[optind];
        Theme extracted_theme;

        // Load image
        int width, height, channels;
        unsigned char *img = stbi_load(input_path, &width, &height, &channels, 3);
        if (!img) {
            fprintf(stderr, "error: could not load input image '%s'.\n", input_path);
            return 1;
        }

        // Extract palette
        if (extract_palette_from_image(img, width, height, &extracted_theme) != 0) {
            stbi_image_free(img);
            return 1;
        }

        // Display palette
        display_palette(&extracted_theme);

        // Export palette
        if (export_flag) {
            if (export_palette(&extracted_theme, export_palette_file) == 0) {
                printf("palette exported to '%s'.\n", export_palette_file);
            } else {
                printf("failed to export palette to '%s'.\n", export_palette_file);
            }
        }

        // Cleanup
        stbi_image_free(img);
        return 0;
    } else {
        // Conversion mode: ./muse [options] <input_image> <output_image> <palette_file> [dither_method]
        if (remaining_args < 3 || remaining_args > 4) {
            print_usage(argv[0]);
            return 1;
        }

        const char *input_path = argv[optind];
        const char *output_path = argv[optind + 1];
        const char *palette_path = argv[optind + 2];
        DitherMethod dither_method = DITHER_FLOYD_STEINBERG;

        if (remaining_args == 4) {
            if (strcmp(argv[optind + 3], "nodither") == 0) {
                dither_method = DITHER_NONE;
            } else if (strcmp(argv[optind + 3], "bayer") == 0) {
                dither_method = DITHER_BAYER;
            } else if (strcmp(argv[optind + 3], "ordered") == 0) {
                dither_method = DITHER_ORDERED;
            } else if (strcmp(argv[optind + 3], "floyd") == 0) {
                dither_method = DITHER_FLOYD_STEINBERG;
            } else if (strcmp(argv[optind + 3], "jjn") == 0) {
                dither_method = DITHER_JJN;
            } else if (strcmp(argv[optind + 3], "sierra") == 0) {
                dither_method = DITHER_SIERRA;
            } else {
                fprintf(stderr, "error: unknown dither method '%s'.\n", argv[optind + 3]);
                print_usage(argv[0]);
                return 1;
            }
        }

        Theme theme = load_palette_file(palette_path);
        if (theme.num_colors == 0) {
            fprintf(stderr, "error: palette file '%s' contains no colors.\n", palette_path);
            return 1;
        }

        initialize_cache(&theme);

        // Load input image
        int width_img, height_img, channels_img;
        unsigned char *img = stbi_load(input_path, &width_img, &height_img, &channels_img, 3);
        if (!img) {
            fprintf(stderr, "error: could not load input image '%s'.\n", input_path);
            free(color_cache);
            return 1;
        }

        // Convert image to float
        float *image_f = malloc(width_img * height_img * 3 * sizeof(float));
        if (!image_f) {
            fprintf(stderr, "error: could not allocate memory for image processing.\n");
            stbi_image_free(img);
            free(color_cache);
            return 1;
        }

        for (int i = 0; i < width_img * height_img * 3; i++) {
            image_f[i] = (float)img[i];
        }

        // Apply effects
        if (blur_flag) {
            apply_box_blur(image_f, width_img, height_img, blur_strength);
        }

        if (super8_flag) {
            apply_super8_effect(image_f, width_img, height_img, super8_strength);
        }

        if (panavision_flag) {
            apply_super_panavision70_effect(image_f, width_img, height_img, panavision_strength);
        }

        if (grading_flag) {
            apply_color_grading(image_f, width_img, height_img, brightness, contrast, saturation);
        }

        // Allocate output image buffer
        unsigned char *output = malloc(width_img * height_img * 3);
        if (!output) {
            fprintf(stderr, "error: could not allocate memory for output image.\n");
            free(image_f);
            stbi_image_free(img);
            free(color_cache);
            return 1;
        }

        // Apply dithering
        switch (dither_method) {
            case DITHER_FLOYD_STEINBERG:
                apply_floyd_steinberg_dither(image_f, output, width_img, height_img, &theme);
                break;
            case DITHER_ORDERED:
                apply_ordered_dither(image_f, output, width_img, height_img, &theme);
                break;
            case DITHER_BAYER:
                apply_bayer_dither(image_f, output, width_img, height_img, &theme);
                break;
            case DITHER_JJN:
                apply_jjn_dither(image_f, output, width_img, height_img, &theme);
                break;
            case DITHER_SIERRA:
                apply_sierra_dither(image_f, output, width_img, height_img, &theme);
                break;
            case DITHER_NONE:
                for (int y = 0; y < height_img; y++) {
                    for (int x = 0; x < width_img; x++) {
                        int idx = (y * width_img + x) * 3;
                        Color old_pixel = {
                            clamp_float(image_f[idx]),
                            clamp_float(image_f[idx + 1]),
                            clamp_float(image_f[idx + 2])
                        };
                        Color new_pixel = find_closest_color_cached(old_pixel);
                        output[idx] = new_pixel.r;
                        output[idx + 1] = new_pixel.g;
                        output[idx + 2] = new_pixel.b;
                    }
                }
                break;
        }

        // Write output image
        int success = stbi_write_png(output_path, width_img, height_img, 3, output, width_img * 3);
        if (!success) {
            fprintf(stderr, "error: could not write output image to '%s'.\n", output_path);
            free(output);
            free(image_f);
            stbi_image_free(img);
            free(color_cache);
            return 1;
        }

        // Display palette
        display_palette(&theme);

        // Export palette if requested
        if (export_flag && ! (remaining_args ==1)) { // Ensure not in extraction mode
            if (export_palette(&theme, export_palette_file) == 0) {
                printf("palette exported to '%s'.\n", export_palette_file);
            } else {
                printf("failed to export palette to '%s'.\n", export_palette_file);
            }
        }

        // Output information
        printf("image converted successfully and saved to '%s'.\n", output_path);
        printf("settings used:\n");
        printf("  input image: %s\n", input_path);
        printf("  output image: %s\n", output_path);
        printf("  palette file: %s\n", palette_path);
        printf("  dither method: ");
        switch (dither_method) {
            case DITHER_FLOYD_STEINBERG:
                printf("floyd-steinberg\n");
                break;
            case DITHER_ORDERED:
                printf("ordered\n");
                break;
            case DITHER_BAYER:
                printf("bayer\n");
                break;
            case DITHER_JJN:
                printf("jarvis, judice, and ninke\n");
                break;
            case DITHER_SIERRA:
                printf("sierra\n");
                break;
            case DITHER_NONE:
                printf("no dither\n");
                break;
        }
        if (blur_flag) {
            printf("  blur strength: %d\n", blur_strength);
        }
        if (super8_flag) {
            printf("  super8 strength: %d\n", super8_strength);
        }
        if (panavision_flag) {
            printf("  panavision strength: %d\n", panavision_strength);
        }
        if (grading_flag) {
            printf("  brightness: %.2f\n", brightness);
            printf("  contrast: %.2f\n", contrast);
            printf("  saturation: %.2f\n", saturation);
        }

        // Cleanup
        free(output);
        free(image_f);
        stbi_image_free(img);
        free(color_cache);
        return 0;
    }
}
