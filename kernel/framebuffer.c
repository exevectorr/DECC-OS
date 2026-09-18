#include "framebuffer.h"
#include "font.h"
#include <stdint.h>

fb_info_t fb;

/* Back buffer — 1024x768x4 bytes = 3 MB. Placed in .bss. */
#define BACKBUF_MAX_PIXELS (1920 * 1080)
static uint8_t backbuf[BACKBUF_MAX_PIXELS * 4];

void fb_init(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp) {
    fb.addr   = (uint8_t *)(uint32_t)addr;
    fb.pitch  = pitch;
    fb.width  = w;
    fb.height = h;
    fb.bpp    = bpp;
}

void fb_pixel(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb.width || (uint32_t)y >= fb.height) return;
    uint32_t off = (y * fb.width + x) * 4;
    backbuf[off + 0] = rgb & 0xFF;
    backbuf[off + 1] = (rgb >> 8) & 0xFF;
    backbuf[off + 2] = (rgb >> 16) & 0xFF;
    backbuf[off + 3] = 0;
}

void fb_fill(int x, int y, int w, int h, uint32_t rgb) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_pixel(x + i, y + j, rgb);
}

void fb_clear(uint32_t rgb) {
    fb_fill(0, 0, fb.width, fb.height, rgb);
}

void fb_rect(int x, int y, int w, int h, uint32_t rgb, int t) {
    for (int i = 0; i < t; i++) {
        for (int xx = x; xx < x + w; xx++) {
            fb_pixel(xx, y + i, rgb);
            fb_pixel(xx, y + h - 1 - i, rgb);
        }
        for (int yy = y; yy < y + h; yy++) {
            fb_pixel(x + i, yy, rgb);
            fb_pixel(x + w - 1 - i, yy, rgb);
        }
    }
}

/* Blast the back buffer to VRAM. This is the only place we touch VRAM. */
void fb_swap(void) {
    uint32_t row_bytes = fb.width * 4;
    for (uint32_t y = 0; y < fb.height; y++) {
        uint8_t *dst = fb.addr + y * fb.pitch;
        uint8_t *src = backbuf + y * row_bytes;
        for (uint32_t i = 0; i < row_bytes; i++)
            dst[i] = src[i];
    }
}

/* ----- text rendering (8x16 font) ----- */
void fb_draw_char(int x, int y, char c, uint32_t color) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font8x16[(int)c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                fb_pixel(x + col, y + row, color);
        }
    }
}

void fb_draw_text(int x, int y, const char *s, uint32_t color) {
    int cx = x;
    while (*s) {
        fb_draw_char(cx, y, *s, color);
        cx += 8;
        s++;
    }
}

int fb_text_width(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n * 8;
}