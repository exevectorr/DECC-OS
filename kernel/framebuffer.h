#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include <stdint.h>

typedef struct {
    uint8_t  *addr;
    uint32_t  pitch;
    uint32_t  width;
    uint32_t  height;
    uint8_t   bpp;
} fb_info_t;

extern fb_info_t fb;

void fb_init(uint64_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint8_t bpp);
void fb_clear(uint32_t rgb);
void fb_pixel(int x, int y, uint32_t rgb);
void fb_fill(int x, int y, int w, int h, uint32_t rgb);
void fb_rect(int x, int y, int w, int h, uint32_t rgb, int thickness);

/* ----- double buffering ----- */
void fb_swap(void);                              /* copy back buffer to VRAM */
void fb_draw_char(int x, int y, char c, uint32_t color);
void fb_draw_text(int x, int y, const char *s, uint32_t color);
int  fb_text_width(const char *s);               /* 8 px per char */

#endif