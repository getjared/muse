<div align="center">

## ｍｕｓｅ
**a minimalist image processor for vintage aesthetics**

[![license: unlicense](https://img.shields.io/badge/license-unlicense-pink.svg)](http://unlicense.org/)
[![made with c](https://img.shields.io/badge/made%20with-c-purple.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

</div>

## overview
muse is a high-performance image manipulation tool, it provides various dithering algorithms, effects, and color grading capabilities while maintaining minimal dependencies and blazing-fast execution.

## before & after
using the catppuccin palette:
| before | after |
|--------|-------|
| ![before](https://i.imgur.com/fkzmPtQ.jpg) | ![after](https://i.imgur.com/tkTjHtR.png) |


## key features
- advanced dithering algorithms (floyd-steinberg, bayer, ordered, jjn, sierra)
- lospec.com palette compatibility
- vintage film emulation:
  - super 8 grain effect
  - super panavision 70 vignetting
- comprehensive color grading
- palette extraction from images
- lightweight implementation in pure c

## installation

### prerequisites
- c compiler (gcc/clang)
- make
- git

### build from source
```bash
git clone https://github.com/getjared/muse.git
cd muse
make
sudo make install
```

## usage

### basic operations
```bash
# standard conversion with floyd-steinberg dithering
muse input.jpg output.png catppuccin.txt

# bayer dithering
muse input.jpg output.png nord.txt bayer

# ordered dithering
muse input.jpg output.png spooky-13.txt ordered

# no dithering
muse input.jpg output.png croma16.txt nodither

# extract palette
muse input.jpg -E palette.txt
```

### effects

#### blur effect
```bash
muse -b 3 input.jpg output.png nord.txt
```

#### film effects
```bash
# super 8 film grain
muse -s 3 input.png output.png croma16.txt

# super panavision 70
muse -p 2 input.png output.png nord.txt

# combined effects
muse -b 3 -s 2 -p 4 input.png output.png spooky-13.txt
```

### color adjustments

| parameter | flag | range | default |
|-----------|------|-------|---------|
| brightness | `-B` | -255.0 to 255.0 | 0.0 |
| contrast | `-C` | 0.0 to 5.0 | 1.0 |
| saturation | `-S` | 0.0 to 5.0 | 1.0 |

```bash
# example with all adjustments
muse -B 10.0 -C 1.2 -S 1.1 input.png output.png nord.txt
```

## dithering algorithms

| algorithm | description | best for |
|-----------|-------------|-----------|
| `floyd` | floyd-steinberg (default) | general purpose, smooth gradients |
| `bayer` | ordered matrix pattern | retro graphics, consistent texture |
| `ordered` | 8x8 threshold matrix | uniform pattern distribution |
| `jjn` | jarvis, judice, and ninke | enhanced detail preservation |
| `sierra` | sierra dithering | balanced error diffusion |
| `nodither` | direct color mapping | sharp color boundaries |

## palette system

### location
palettes are stored in `/usr/local/share/muse/palettes`

### custom palette format
```text
;paint.net palette file
;palette name: custom theme
;colors: 16
ff000000    ; black
ff1a1b26    ; background
ff415166    ; muted blue
ffff9b82    ; coral
[...additional colors...]
```

### palette extraction
```bash
# extract and display
muse input.jpg -E

# extract to file
muse input.jpg -E custom.txt
```

## supported formats

### input
- jpg/jpeg
- png
- bmp
- tga

### output
- png (lossless)
- jpg (lossy, 95% quality)
- bmp (uncompressed)
- tga (lossless)

## acknowledgments
- [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h) - image loading
- [stb_image_write](https://github.com/nothings/stb/blob/master/stb_image_write.h) - image saving

<div align="center">

```ascii
╭──────────────────────╮
│ crafted with care    │
│ by jared             │
╰──────────────────────╯
```

</div>
