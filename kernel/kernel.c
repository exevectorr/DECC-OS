#include "multiboot.h"
#include "gdt.h"
#include "idt.h"
#include "framebuffer.h"
#include "mouse.h"
#include <stdint.h>

/* ---------- I/O ---------- */
static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}

/* ---------- constants ---------- */
#define TASKBAR_H      36
#define MAX_WINDOWS    8
#define ICON_COUNT     3

/* ---------- cursor ---------- */
static int cur_x = 512;
static int cur_y = 384;
static int prev_left = 0;
static uint8_t mouse_cycle = 0;
static int8_t  mouse_packet[3];

static const uint8_t cursor_bmp[8] = {
    0b10000000, 0b11000000, 0b11100000, 0b11110000,
    0b11111000, 0b11100000, 0b10100000, 0b00000000
};

/* ---------- windows ---------- */
typedef struct {
    int  active;
    int  x, y, w, h;
    char title[32];
    int  app_id;         /* 0=settings, 1=shell, 2=explorer */
    int  dragging;
    int  drag_dx, drag_dy;
} window_t;

static window_t windows[MAX_WINDOWS];
static int window_count = 0;   /* how many slots used; slots may be freed */
static int focused = -1;

/* ---------- icons ---------- */
typedef struct {
    int         x, y;
    const char *label;
    int         app_id;
    uint32_t    color;
} icon_t;

static icon_t icons[ICON_COUNT] = {
    { 40,  40, "Settings", 0, 0x5FD0FF },
    { 40, 160, "Shell",    1, 0xFF5FA2 },
    { 40, 280, "Explorer", 2, 0x5FFFA0 },
};

/* ---------- mouse polling ---------- */
static void mouse_poll(void) {
    while (inb(0x64) & 1) {
        uint8_t status = inb(0x64);
        int8_t data = inb(0x60);
        if (!(status & 0x20)) continue;

        switch (mouse_cycle) {
            case 0:
                mouse_packet[0] = data;
                if (!(data & 0x08)) continue;
                mouse_cycle = 1;
                break;
            case 1:
                mouse_packet[1] = data;
                mouse_cycle = 2;
                break;
            case 2:
                mouse_packet[2] = data;
                mouse_cycle = 0;
                int dx = mouse_packet[1];
                int dy = mouse_packet[2];
                if (mouse_packet[0] & 0x10) dx |= 0xFFFFFF00;
                if (mouse_packet[0] & 0x20) dy |= 0xFFFFFF00;
                cur_x += dx;
                cur_y -= dy;
                if (cur_x < 0) cur_x = 0;
                if (cur_y < 0) cur_y = 0;
                if ((uint32_t)cur_x >= fb.width)  cur_x = fb.width  - 1;
                if ((uint32_t)cur_y >= fb.height) cur_y = fb.height - 1;
                break;
        }
    }
}

/* ---------- open a new window for an app ---------- */
static void open_window(int app_id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            windows[i].active = 1;
            windows[i].x = 250 + i * 30;
            windows[i].y = 120 + i * 30;
            windows[i].w = 480;
            windows[i].h = 320;
            windows[i].app_id = app_id;
            windows[i].dragging = 0;

            const char *t = "Window";
            if (app_id == 0) t = "Settings";
            if (app_id == 1) t = "Shell";
            if (app_id == 2) t = "Explorer";

            int j = 0;
            while (t[j] && j < 31) { windows[i].title[j] = t[j]; j++; }
            windows[i].title[j] = 0;

            focused = i;
            return;
        }
    }
}

/* ---------- drawing ---------- */
static void draw_wallpaper(void) {
    for (uint32_t y = 0; y < fb.height; y++) {
        uint8_t r = 0x20 + (y * 0x20) / fb.height;
        uint8_t g = 0x10 + (y * 0x10) / fb.height;
        uint8_t b = 0x50 + (y * 0x30) / fb.height;
        uint32_t c = (r << 16) | (g << 8) | b;
        fb_fill(0, y, fb.width, 1, c);
    }
}

/* simple 48x48 icon: a colored rounded square with a letter */
static void draw_app_icon(int x, int y, uint32_t color, char letter) {
    fb_fill(x, y, 48, 48, color);
    fb_rect(x, y, 48, 48, 0xFFFFFF, 1);

    /* letter drawn big-ish — 8x16 at offset */
    char s[2] = { letter, 0 };
    fb_draw_text(x + 20, y + 16, s, 0x101020);
}

static void draw_icons(void) {
    for (int i = 0; i < ICON_COUNT; i++) {
        icon_t *ic = &icons[i];
        char letter = ic->label[0];
        draw_app_icon(ic->x, ic->y, ic->color, letter);

        /* label under icon, white with shadow for readability */
        int tw = fb_text_width(ic->label);
        int tx = ic->x + 24 - tw / 2;
        int ty = ic->y + 54;
        fb_draw_text(tx + 1, ty + 1, ic->label, 0x000000);
        fb_draw_text(tx, ty, ic->label, 0xFFFFFF);
    }
}

static void draw_taskbar(void) {
    int y = fb.height - TASKBAR_H;
    /* base bar */
    fb_fill(0, y, fb.width, TASKBAR_H, 0x181828);
    /* top highlight line */
    fb_fill(0, y, fb.width, 1, 0x404060);

    /* start button */
    fb_fill(8, y + 5, 110, TASKBAR_H - 10, 0x2A4A90);
    fb_rect(8, y + 5, 110, TASKBAR_H - 10, 0x5FD0FF, 1);
    fb_draw_text(20, y + 10, "Start", 0xFFFFFF);

    /* clock (static for now) */
    fb_draw_text(fb.width - 80, y + 10, "12:34", 0xFFFFFF);
}

static void draw_window(window_t *w) {
    if (!w->active) return;

    /* shadow */
    fb_fill(w->x + 6, w->y + 6, w->w, w->h, 0x101020);

    /* body */
    fb_fill(w->x, w->y, w->w, w->h, 0x202840);

    /* titlebar */
    uint32_t tb_color = (w == &windows[focused]) ? 0x3A6AC0 : 0x2A3A70;
    fb_fill(w->x, w->y, w->w, 28, tb_color);

    /* border */
    fb_rect(w->x, w->y, w->w, w->h, 0xFFFFFF, 1);

    /* title text */
    fb_draw_text(w->x + 8, w->y + 6, w->title, 0xFFFFFF);

    /* close button */
    fb_fill(w->x + w->w - 24, w->y + 6, 16, 16, 0xC02020);
    fb_rect(w->x + w->w - 24, w->y + 6, 16, 16, 0xFFFFFF, 1);

    /* app-specific content */
    if (w->app_id == 0) {
        /* Settings: some colored bars */
        fb_draw_text(w->x + 16, w->y + 44, "Settings", 0xFFFFFF);
        fb_fill(w->x + 16, w->y + 70, 200, 20, 0x404A80);
        fb_fill(w->x + 16, w->y + 70, 140, 20, 0x5FD0FF);
        fb_draw_text(w->x + 16, w->y + 100, "Volume", 0xC0C0C0);
        fb_fill(w->x + 16, w->y + 120, 200, 20, 0x404A80);
        fb_fill(w->x + 16, w->y + 120, 80, 20, 0x5FFFA0);
        fb_draw_text(w->x + 16, w->y + 150, "Brightness", 0xC0C0C0);
    } else if (w->app_id == 1) {
        /* Shell: fake terminal */
        fb_fill(w->x + 8, w->y + 36, w->w - 16, w->h - 44, 0x000000);
        fb_draw_text(w->x + 16, w->y + 44,  "myOS shell v0.1", 0x5FFFA0);
        fb_draw_text(w->x + 16, w->y + 64,  "type 'help' for commands", 0xC0C0C0);
        fb_draw_text(w->x + 16, w->y + 96,  "myos> _", 0x5FD0FF);
    } else if (w->app_id == 2) {
        /* Explorer: fake file grid */
        fb_draw_text(w->x + 16, w->y + 40, "Home", 0xFFFFFF);
        for (int i = 0; i < 6; i++) {
            int gx = w->x + 20 + (i % 3) * 90;
            int gy = w->y + 70 + (i / 3) * 90;
            uint32_t col = (i == 0) ? 0xFFD05F :
                           (i == 1) ? 0x5FD0FF :
                           (i == 2) ? 0xFF5FA2 :
                           (i == 3) ? 0xB45FFF :
                           (i == 4) ? 0x5FFFA0 : 0xFFA05F;
            fb_fill(gx, gy, 60, 50, col);
            fb_rect(gx, gy, 60, 50, 0xFFFFFF, 1);
        }
    }
}

static void draw_cursor(void) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (cursor_bmp[y] & (0x80 >> x))
                fb_pixel(cur_x + x, cur_y + y, 0xFFFFFF);
}

/* ---------- hit testing ---------- */
static int hit_close(window_t *w, int mx, int my) {
    int bx = w->x + w->w - 24;
    int by = w->y + 6;
    return (mx >= bx && mx < bx + 16 && my >= by && my < by + 16);
}

static int hit_titlebar(window_t *w, int mx, int my) {
    return (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + 28);
}

static int hit_icon(icon_t *ic, int mx, int my) {
    return (mx >= ic->x && mx < ic->x + 48 &&
            my >= ic->y && my < ic->y + 48 + 20);
}

/* ---------- input handling ---------- */
static void handle_click(int mx, int my) {
    /* 1. check window close buttons (topmost first: iterate reverse focus) */
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (!w->active) continue;
        if (hit_close(w, mx, my)) {
            w->active = 0;
            if (focused == i) {
                focused = -1;
                for (int j = 0; j < MAX_WINDOWS; j++)
                    if (windows[j].active) focused = j;
            }
            return;
        }
    }

    /* 2. check titlebars for drag + focus */
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (!w->active) continue;
        if (hit_titlebar(w, mx, my)) {
            focused = i;
            w->dragging = 1;
            w->drag_dx = mx - w->x;
            w->drag_dy = my - w->y;
            return;
        }
    }

    /* 3. check desktop icons */
    for (int i = 0; i < ICON_COUNT; i++) {
        if (hit_icon(&icons[i], mx, my)) {
            open_window(icons[i].app_id);
            return;
        }
    }
}

/* ---------- main ---------- */
void isr_handler(uint32_t irq) { (void)irq; }

void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    (void)magic;
    multiboot_info_t *mbi = (multiboot_info_t *)mbi_addr;

    gdt_init();
    idt_init();

    fb_init(mbi->framebuffer_addr,
            mbi->framebuffer_pitch,
            mbi->framebuffer_width,
            mbi->framebuffer_height,
            mbi->framebuffer_bpp);

    mouse_init();

    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].active = 0;

    while (1) {
        mouse_poll();

        /* ---- input ---- */
        int left = mouse_packet[0] & 1;
        if (left && !prev_left) {
            handle_click(cur_x, cur_y);
        }
        if (!left && prev_left) {
            for (int i = 0; i < MAX_WINDOWS; i++)
                windows[i].dragging = 0;
        }
        if (left) {
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i].active && windows[i].dragging) {
                    windows[i].x = cur_x - windows[i].drag_dx;
                    windows[i].y = cur_y - windows[i].drag_dy;
                }
            }
        }
        prev_left = left;

        /* ---- render ---- */
        draw_wallpaper();
        draw_icons();

        /* draw unfocused windows first, then focused, then taskbar, then cursor */
        for (int i = 0; i < MAX_WINDOWS; i++)
            if (windows[i].active && i != focused) draw_window(&windows[i]);
        if (focused >= 0 && windows[focused].active) draw_window(&windows[focused]);

        draw_taskbar();
        draw_cursor();

        /* ---- present ---- */
        fb_swap();

        /* light frame limiter */
        for (volatile uint32_t i = 0; i < 50000; i++) {}
    }
}