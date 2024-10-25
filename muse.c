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
        fprintf(stderr, "Error: Could not allocate memory for color cache.\n");
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
            fprintf(stderr, "Error: Could not open palette file '%s' or '%s'.\n", filename, filepath);
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

        if (strncmp(p, "FF", 2) == 0) p += 2;
        
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
                fprintf(stderr, "Warning: Maximum palette size of 256 colors reached. Additional colors are ignored.\n");
                break;
            }
        } else {
            fprintf(stderr, "Warning: Invalid color format in palette file: %s", line);
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

void apply_box_blur(float *image_f, int width, int height, int blur_strength) {
    if (blur_strength < 1) return;

    int channels = 3;
    float *temp = malloc(width * height * channels * sizeof(float));
    if (!temp) {
        fprintf(stderr, "Error: Could not allocate memory for blur operation.\n");
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

int main(int argc, char *argv[]) {
    int opt;
    int blur_strength = 0;
    int blur_flag = 0;

    static struct option long_options[] = {
        {"blur", required_argument, 0, 'b'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "b:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b':
                blur_strength = atoi(optarg);
                if (blur_strength < 1) {
                    fprintf(stderr, "Error: Blur strength must be a positive integer.\n");
                    return 1;
                }
                blur_flag = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [options] <input_image> <output_image> <palette_file> [dither_method]\n", argv[0]);
                fprintf(stderr, "Options:\n");
                fprintf(stderr, "  -b, --blur <strength>    Apply blur with specified strength\n");
                fprintf(stderr, "Available dither methods: floyd (default), bayer, ordered, nodither\n");
                return 1;
        }
    }

    int remaining_args = argc - optind;
    if (remaining_args < 3 || remaining_args > 4) {
        fprintf(stderr, "Usage: %s [options] <input_image> <output_image> <palette_file> [dither_method]\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -b, --blur <strength>    Apply blur with specified strength\n");
        fprintf(stderr, "Available dither methods: floyd (default), bayer, ordered, nodither\n");
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
        } else {
            fprintf(stderr, "Unknown dither method: %s\n", argv[optind + 3]);
            fprintf(stderr, "Available methods: floyd (default), bayer, ordered, nodither\n");
            return 1;
        }
    }

    Theme theme = load_palette_file(palette_path);
    if (theme.num_colors == 0) {
        fprintf(stderr, "Error: No valid colors found in palette file '%s'.\n", palette_path);
        return 1;
    }

    printf("Loaded palette '%s' with %d colors\n", theme.name, theme.num_colors);

    initialize_cache(&theme);

    int width, height, channels;
    unsigned char *img = stbi_load(input_path, &width, &height, &channels, 3);
    if (!img) {
        fprintf(stderr, "Error: Could not load image '%s'.\n", input_path);
        free(color_cache);
        return 1;
    }

    float *image_f = malloc(width * height * 3 * sizeof(float));
    if (!image_f) {
        fprintf(stderr, "Error: Could not allocate memory for image processing.\n");
        stbi_image_free(img);
        free(color_cache);
        return 1;
    }

    for (int i = 0; i < width * height * 3; i++) {
        image_f[i] = (float)img[i];
    }

    if (blur_flag) {
        printf("Applying blur with strength %d...\n", blur_strength);
        apply_box_blur(image_f, width, height, blur_strength);
        printf("Blur applied.\n");
    }

    unsigned char *output = malloc(width * height * 3);
    if (!output) {
        fprintf(stderr, "Error: Could not allocate memory for output image.\n");
        free(image_f);
        stbi_image_free(img);
        free(color_cache);
        return 1;
    }

    switch (dither_method) {
        case DITHER_FLOYD_STEINBERG:
            apply_floyd_steinberg_dither(image_f, output, width, height, &theme);
            break;
        case DITHER_ORDERED:
            apply_ordered_dither(image_f, output, width, height, &theme);
            break;
        case DITHER_BAYER:
            apply_bayer_dither(image_f, output, width, height, &theme);
            break;
        case DITHER_NONE:
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
                }
            }
            break;
    }

    int success = stbi_write_png(output_path, width, height, 3, output, width * 3);
    if (!success) {
        fprintf(stderr, "Error: Could not write image '%s'.\n", output_path);
        free(output);
        free(image_f);
        stbi_image_free(img);
        free(color_cache);
        return 1;
    }

    printf("Image converted successfully and saved to '%s'.\n", output_path);

    free(output);
    free(image_f);
    stbi_image_free(img);
    free(color_cache);
    return 0;
}
