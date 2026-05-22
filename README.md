# ESP32-S3 1.47" SVG Image Display

SVG slideshow viewer for the **Waveshare ESP32-S3-LCD-1.47B**. Loads `.svg` files from a micro SD card and cycles through them on the built-in 172×320 ST7789 display.

## Hardware

| | |
|---|---|
| **Board** | Waveshare ESP32-S3-LCD-1.47B |
| **MCU** | ESP32-S3R8 (240 MHz, 8 MB OPI PSRAM) |
| **Flash** | 16 MB |
| **Display** | 172×320 ST7789, landscape orientation |
| **Storage** | Micro SD via SD_MMC 4-bit |

### Pin Mapping

**Display (SPI / FSPI)**

| Signal | GPIO |
|--------|------|
| MOSI   | 45   |
| SCLK   | 40   |
| CS     | 42   |
| DC     | 41   |
| RST    | 39   |
| Backlight | 46 |

**SD Card (SD_MMC 4-bit)**

| Signal | GPIO |
|--------|------|
| CLK    | 14   |
| CMD    | 15   |
| D0     | 16   |
| D1     | 18   |
| D2     | 17   |
| D3     | 21   |

## Features

- Renders SVG files using [NanoSVG](https://github.com/memononen/nanosvg) entirely on-device
- **Content-aware scaling** — detects the actual logo bounds via a fast probe rasterization and trims SVG whitespace automatically
- Scales to fit the display width; falls back to height if content would be clipped vertically
- Configurable padding, slideshow delay, and rotation orientation
- Cycles through all `.svg` files found in the SD card root with a configurable delay

## SD Card Setup

Place any number of `.svg` files in the **root directory** of the SD card:

```
/logo1.svg
/logo2.svg
/logo3.svg
...
```

Files are displayed in the order returned by the filesystem. Up to `MAX_SVG_FILES` (default 20) are loaded.

## Build & Flash

The project uses [PlatformIO](https://platformio.org/). Two build environments are provided:

| Environment | Description |
|-------------|-------------|
| `normal`    | Default orientation |
| `rotated180`| Display rotated 180° (for reversed board mounting) |

```bash
# Flash default orientation
pio run -e normal --target upload

# Flash 180° rotated
pio run -e rotated180 --target upload

# Monitor serial output
pio device monitor
```

## Configuration

All tuneable constants are at the top of [`src/main.cpp`](src/main.cpp):

| Constant | Default | Description |
|----------|---------|-------------|
| `SVG_DELAY_MS` | `10000` | Milliseconds between slides |
| `SVG_PADDING_X` | `5` | Padding (px) on each side — applied to all four sides when height-constrained |
| `MAX_SVG_FILES` | `20` | Maximum number of SVG files to load from SD |

## How It Works

1. **Probe pass** — rasterizes the SVG at 1/4 scale into a small temporary buffer (80×43 px) to find the bounding box of non-transparent pixels
2. **Scale computation** — maps the detected content bounds back to SVG coordinates, then computes a scale that fits the content within the padded display area (`fminf` of X and Y constrained scales)
3. **Main rasterize** — renders the SVG at full quality into a 320×172 landscape RGBA buffer in PSRAM (~220 KB)
4. **Display** — composites RGBA over white, converts to RGB565, and streams to the ST7789 one row at a time via SPI

## Dependencies

All dependencies are vendored in `src/`:

- [`nanosvg.h`](src/nanosvg.h) — SVG parser
- [`nanosvgrast.h`](src/nanosvgrast.h) — SVG rasterizer

No additional PlatformIO libraries required.

## Branch Structure

| Branch | Purpose |
|--------|---------|
| `main` | Stable, tested firmware |
| `dev`  | Active development |
