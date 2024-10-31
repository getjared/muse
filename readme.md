<div align="center">

```ascii



█████████████████████████████████████████████████████████████████████
█░░░░░░██████████░░░░░░█░░░░░░██░░░░░░█░░░░░░░░░░░░░░█░░░░░░░░░░░░░░█
█░░▄▀░░░░░░░░░░░░░░▄▀░░█░░▄▀░░██░░▄▀░░█░░▄▀▄▀▄▀▄▀▄▀░░█░░▄▀▄▀▄▀▄▀▄▀░░█
█░░▄▀▄▀▄▀▄▀▄▀▄▀▄▀▄▀▄▀░░█░░▄▀░░██░░▄▀░░█░░▄▀░░░░░░░░░░█░░▄▀░░░░░░░░░░█
█░░▄▀░░░░░░▄▀░░░░░░▄▀░░█░░▄▀░░██░░▄▀░░█░░▄▀░░█████████░░▄▀░░█████████
█░░▄▀░░██░░▄▀░░██░░▄▀░░█░░▄▀░░██░░▄▀░░█░░▄▀░░░░░░░░░░█░░▄▀░░░░░░░░░░█
█░░▄▀░░██░░▄▀░░██░░▄▀░░█░░▄▀░░██░░▄▀░░█░░▄▀▄▀▄▀▄▀▄▀░░█░░▄▀▄▀▄▀▄▀▄▀░░█
█░░▄▀░░██░░░░░░██░░▄▀░░█░░▄▀░░██░░▄▀░░█░░░░░░░░░░▄▀░░█░░▄▀░░░░░░░░░░█
█░░▄▀░░██████████░░▄▀░░█░░▄▀░░██░░▄▀░░█████████░░▄▀░░█░░▄▀░░█████████
█░░▄▀░░██████████░░▄▀░░█░░▄▀░░░░░░▄▀░░█░░░░░░░░░░▄▀░░█░░▄▀░░░░░░░░░░█
█░░▄▀░░██████████░░▄▀░░█░░▄▀▄▀▄▀▄▀▄▀░░█░░▄▀▄▀▄▀▄▀▄▀░░█░░▄▀▄▀▄▀▄▀▄▀░░█
█░░░░░░██████████░░░░░░█░░░░░░░░░░░░░░█░░░░░░░░░░░░░░█░░░░░░░░░░░░░░█
█████████████████████████████████████████████████████████████████████

```

**[ a simple image color manipulator with vintage aesthetics ]**

[![License: MIT](https://img.shields.io/badge/License-MIT-pink.svg)](https://opensource.org/licenses/MIT)
[![Made with C](https://img.shields.io/badge/Made%20with-C-purple.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

</div>

## ✧ features

- 🎨 multiple dithering algorithms for that perfect retro look
- 🌈 external palette support (compatible with PAINT.NET)
- 🚀 lightweight and blazingly fast
- 📦 easy installation

## ✧ before & after

using the catppuccin palette:

| before | after |
|--------|-------|
| ![before](https://i.imgur.com/fkzmPtQ.jpg) | ![after](https://i.imgur.com/tkTjHtR.png) |

## ✧ installation

```bash
git clone https://github.com/getjared/muse.git
cd muse
make
sudo make install
```

## ✧ dependencies

- 📝 c compiler (gcc or clang)
- 🔧 make
- 📚 stb_image.h (included)
- 📚 stb_image_write.h (included)

## ✧ quick start guide

### basic usage

```bash
# convert with floyd-steinberg (default)
./muse input.jpg output.png catppuccin.txt

# convert using bayer dithering
./muse input.jpg output.png nord.txt bayer

# convert using ordered dithering
./muse input.jpg output.png spooky-13.txt ordered

# convert without dithering
./muse input.jpg output.png croma16.txt nodither
```

### special effects

#### 🌫️ blur effect
```bash
./muse -b 3 input.jpg output.png nord.txt bayer
```

#### 📽️ super8 effect (8mm camera)
```bash
./muse -s 3 input.png output.png croma16.txt nodither
```

#### 🎬 super panavision 70
```bash
./muse -p 2 input.png output.png nord.txt
```

#### 🎨 combined effects
```bash
./muse -b 3 -s 2 -p 4 input.png output.png spooky-13.txt
```

### image adjustments

you can directly modify image properties using these flags:

| flag | description | example |
|------|-------------|---------|
| `-B` | brightness | `-B 10.0` |
| `-C` | contrast | `-C 1.2` |
| `-S` | saturation | `-S 1.1` |

#### example with all modifications
```bash
./muse -b 3 -s 2 -p 4 -B 10.0 -C 1.2 -S 1.1 input.png output.png nord.txt floyd
```

## ✧ dithering methods

| method | description |
|--------|-------------|
| `floyd` | floyd-steinberg dithering (default) |
| `bayer` | bayer matrix dithering |
| `ordered` | ordered dithering |
| `nodither` | no dithering |

## ✧ palettes

palettes are stored in `/usr/local/share/muse/palettes` after installation. you can add custom palettes by copying `.txt` files to this directory. the palette format is compatible with lospec palette files (PAINT.NET txt file).

## ✧ credits

- [stb_image.h](https://github.com/nothings/stb/blob/master/stb_image.h)
- [stb_image_write.h](https://github.com/nothings/stb/blob/master/stb_image_write.h)

<div align="center">

```ascii
                    ╭─────────────────────────╮
                    │    made with ♥ by jared  │
                    ╰─────────────────────────╯
```

</div>
