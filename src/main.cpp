#include <Arduino.h>
#include "SD_Card.h"
#include "Display_ST7789.h"

#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#define MAX_SVG_FILES  20
#define SVG_DELAY_MS   10000
#define SVG_PADDING_X  5

static uint16_t linebuf[LCD_WIDTH];
static char svg_names[MAX_SVG_FILES][100];
static uint16_t svg_count = 0;
static uint16_t svg_index = 0;

static void show_svg(const char* path) {
    Serial.printf("Opening %s ...\r\n", path);
    File f = SD_MMC.open(path);
    if (!f) {
        Serial.printf("Cannot open %s\r\n", path);
        return;
    }

    size_t len = f.size();
    Serial.printf("File size: %u bytes\r\n", (unsigned)len);
    char* data = (char*)ps_malloc(len + 1);
    if (!data) {
        Serial.printf("OOM allocating %u bytes for SVG\r\n", (unsigned)(len + 1));
        f.close();
        return;
    }
    f.read((uint8_t*)data, len);
    data[len] = '\0';
    f.close();

    NSVGimage* img = nsvgParse(data, "px", 96);
    free(data);
    if (!img) {
        Serial.printf("SVG parse failed\r\n");
        return;
    }
    Serial.printf("SVG parsed: %.0f x %.0f\r\n", img->width, img->height);

    int rW = LCD_HEIGHT;  // 320 (landscape width = portrait height after rotation)
    int rH = LCD_WIDTH;   // 172 (landscape height = portrait width after rotation)

    // --- Probe pass: rasterize at 1/4 scale to find content bounding box ---
    int probeW = rW / 4;  // 80
    int probeH = rH / 4;  // 43
    // fminf so the whole SVG fits inside the probe buffer without clipping
    float probeScale = (img->width > 0 && img->height > 0)
                     ? fminf((float)probeW / img->width, (float)probeH / img->height)
                     : 1.0f;
    float probeTx = (probeW - img->width  * probeScale) * 0.5f;
    float probeTy = (probeH - img->height * probeScale) * 0.5f;

    unsigned char* probe = (unsigned char*)ps_malloc((size_t)probeW * probeH * 4);
    if (!probe) {
        Serial.printf("OOM for probe buffer\r\n");
        nsvgDelete(img);
        return;
    }
    memset(probe, 0, (size_t)probeW * probeH * 4);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    nsvgRasterize(rast, img, probeTx, probeTy, probeScale, probe, probeW, probeH, probeW * 4);

    int minX = probeW, maxX = -1, minY = probeH, maxY = -1;
    for (int y = 0; y < probeH; y++) {
        for (int x = 0; x < probeW; x++) {
            if (probe[(y * probeW + x) * 4 + 3] > 16) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    free(probe);

    // --- Compute final scale from content bounds ---
    float scale, tx, ty;
    if (maxX >= minX && maxY >= minY) {
        // Map probe-pixel bounds back to SVG coordinate space
        float svgMinX = (minX       - probeTx) / probeScale;
        float svgMinY = (minY       - probeTy) / probeScale;
        float svgMaxX = (maxX + 1.0f - probeTx) / probeScale;
        float svgMaxY = (maxY + 1.0f - probeTy) / probeScale;
        float svgCx   = (svgMinX + svgMaxX) * 0.5f;
        float svgCy   = (svgMinY + svgMaxY) * 0.5f;
        // Fit within padded area on both axes; X drives unless content is too tall
        float sx = (float)(rW - 2 * SVG_PADDING_X) / (svgMaxX - svgMinX);
        float sy = (float)(rH - 2 * SVG_PADDING_X) / (svgMaxY - svgMinY);
        scale = fminf(sx, sy);
        tx = rW * 0.5f - svgCx * scale;
        ty = rH * 0.5f - svgCy * scale;
        Serial.printf("Content SVG bounds: (%.0f,%.0f)-(%.0f,%.0f), scale=%.3f\r\n",
                      svgMinX, svgMinY, svgMaxX, svgMaxY, scale);
    } else {
        // Fallback: no content detected, fill with declared dimensions
        float sx = (float)(rW - 2 * SVG_PADDING_X) / img->width;
        float sy = (float)(rH - 2 * SVG_PADDING_X) / img->height;
        scale = fminf(sx, sy);
        tx = (rW - img->width  * scale) * 0.5f;
        ty = (rH - img->height * scale) * 0.5f;
        Serial.printf("No content detected, using full SVG dimensions\r\n");
    }

    // --- Main rasterize ---
    unsigned char* rgba = (unsigned char*)ps_malloc((size_t)rW * rH * 4);
    if (!rgba) {
        Serial.printf("OOM allocating RGBA buffer\r\n");
        nsvgDeleteRasterizer(rast);
        nsvgDelete(img);
        return;
    }
    memset(rgba, 0, (size_t)rW * rH * 4);

    nsvgRasterize(rast, img, tx, ty, scale, rgba, rW, rH, rW * 4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);

    // 90° CW rotation; ROTATE_180 adds a further 180° flip (= 270° CW / 90° CCW)
    for (int dy = 0; dy < LCD_HEIGHT; dy++) {
        for (int dx = 0; dx < LCD_WIDTH; dx++) {
#ifdef ROTATE_180
            int sx = (rW - 1) - dy;
            int sy = dx;
#else
            int sx = dy;
            int sy = (rH - 1) - dx;
#endif
            const unsigned char* p = rgba + ((size_t)sy * rW + sx) * 4;
            uint8_t a = p[3];
            uint8_t r = (uint8_t)(((uint16_t)p[0] * a + 255u * (255u - a)) / 255u);
            uint8_t g = (uint8_t)(((uint16_t)p[1] * a + 255u * (255u - a)) / 255u);
            uint8_t b = (uint8_t)(((uint16_t)p[2] * a + 255u * (255u - a)) / 255u);
            linebuf[dx] = ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3);
        }
        LCD_addWindow(0, dy, LCD_WIDTH - 1, dy, linebuf);
    }
    free(rgba);
    Serial.printf("SVG displayed.\r\n");
}

static void show_next() {
    char path[110];
    snprintf(path, sizeof(path), "/%s", svg_names[svg_index]);
    show_svg(path);
    svg_index = (svg_index + 1) % svg_count;
}

void setup() {
    Serial.begin(115200);
    Serial.printf("=== SVG Viewer ===\r\n");

    Flash_test();
    SD_Init();
    LCD_Init();
    Set_Backlight(90);

    svg_count = Folder_retrieval("/", ".svg", svg_names, MAX_SVG_FILES);
    if (svg_count == 0) {
        Serial.printf("No SVG files found on SD card\r\n");
        return;
    }
    Serial.printf("Found %d SVG file(s)\r\n", svg_count);
    show_next();
}

void loop() {
    delay(SVG_DELAY_MS);
    if (svg_count > 0)
        show_next();
}
