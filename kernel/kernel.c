#include "multiboot.h"
#include "gdt.h"
#include "idt.h"
#include "framebuffer.h"
#include "mouse.h"
#include "keyboard.h"
#include <stdint.h>

/* ---------- I/O ---------- */
static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}

/* ---------- constants ---------- */
#define TASKBAR_H      36
#define MAX_WINDOWS    8
#define ICON_COUNT     3
#define SHELL_COLS     56
#define SHELL_ROWS     14
#define SHELL_LINE_MAX 80

/* ---------- cursor (mouse) ---------- */
static int cur_x = 512;
static int cur_y = 384;
static int prev_left = 0;
static uint8_t mouse_cycle = 0;
static int8_t  mouse_packet[3];

static const uint8_t cursor_bmp[8] = {
    0b10000000, 0b11000000, 0b11100000, 0b11110000,
    0b11111000, 0b11100000, 0b10100000, 0b00000000
};

/* ---------- shell state per window ---------- */
typedef struct {
    char out[SHELL_ROWS][SHELL_LINE_MAX];  /* scrollback */
    int  out_count;                         /* how many lines used */
    char line[SHELL_LINE_MAX];              /* current input line */
    int  line_len;
    int  cursor_blink;                      /* blink phase */
} shell_t;

/* ---------- windows ---------- */
typedef struct {
    int  active;
    int  x, y, w, h;
    char title[32];
    int  app_id;         /* 0=settings, 1=shell, 2=explorer */
    int  dragging;
    int  drag_dx, drag_dy;
    shell_t shell;       /* only used when app_id == 1 */
} window_t;

static window_t windows[MAX_WINDOWS];
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

/* ---------- shell helpers ---------- */
static void shell_push_line(shell_t *sh, const char *s) {
    if (sh->out_count < SHELL_ROWS) {
        int i = 0;
        while (s[i] && i < SHELL_LINE_MAX - 1) { sh->out[sh->out_count][i] = s[i]; i++; }
        sh->out[sh->out_count][i] = 0;
        sh->out_count++;
    } else {
        /* scroll up */
        for (int r = 1; r < SHELL_ROWS; r++) {
            for (int i = 0; i < SHELL_LINE_MAX; i++)
                sh->out[r-1][i] = sh->out[r][i];
        }
        int i = 0;
        while (s[i] && i < SHELL_LINE_MAX - 1) { sh->out[SHELL_ROWS-1][i] = s[i]; i++; }
        sh->out[SHELL_ROWS-1][i] = 0;
    }
}

static int str_starts_with(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void shell_run(shell_t *sh) {
    /* echo the input line as a prompt line in the scrollback */
    char promptline[SHELL_LINE_MAX];
    int i = 0;
    const char *p = "myos> ";
    while (*p && i < SHELL_LINE_MAX - 1) promptline[i++] = *p++;
    int j = 0;
    while (sh->line[j] && i < SHELL_LINE_MAX - 1) promptline[i++] = sh->line[j++];
    promptline[i] = 0;
    shell_push_line(sh, promptline);

    /* dispatch */
    if (sh->line_len == 0) {
        /* nothing */
    } else if (str_eq(sh->line, "help")) {
        shell_push_line(sh, "commands:");
        shell_push_line(sh, "  help        - this list");
        shell_push_line(sh, "  clear       - clear screen");
        shell_push_line(sh, "  about       - about DECC OS");
        shell_push_line(sh, "  echo <txt>  - print text");
        shell_push_line(sh, "  date        - fake date");
    } else if (str_eq(sh->line, "clear")) {
        sh->out_count = 0;
    } else if (str_eq(sh->line, "about")) {
        shell_push_line(sh, "DECC OS v1.1 - bare metal x86 kernel");
        shell_push_line(sh, "C + ASSEMBLY");
        shell_push_line(sh, "double buffered, mouse + kbd IRQs");
    } else if (str_starts_with(sh->line, "echo ")) {
        shell_push_line(sh, sh->line + 5);
    } else if (str_eq(sh->line, "date")) {
        shell_push_line(sh, "Thu Sep 18 2026 12:34:56");
    } else {
        shell_push_line(sh, "unknown command: ");
        /* append what was typed for context */
        char tmp[SHELL_LINE_MAX];
        int k = 0;
        const char *u = "  ";
        while (*u) tmp[k++] = *u++;
        int m = 0;
        while (sh->line[m] && k < SHELL_LINE_MAX - 1) tmp[k++] = sh->line[m++];
        tmp[k] = 0;
        shell_push_line(sh, tmp);
    }

    /* reset input line */
    sh->line_len = 0;
    sh->line[0] = 0;
}

/* Feed a typed character to the focused shell window. */
static void shell_input_char(shell_t *sh, char c) {
    if (c == '\n') {
        shell_run(sh);
    } else if (c == '\b') {
        if (sh->line_len > 0) {
            sh->line_len--;
            sh->line[sh->line_len] = 0;
        }
    } else if (c >= ' ' && c <= '~') {
        if (sh->line_len < SHELL_LINE_MAX - 1) {
            sh->line[sh->line_len++] = c;
            sh->line[sh->line_len] = 0;
        }
    }
}

/* ---------- mouse polling ---------- */
static void mouse_poll(void) {
    while (inb(0x64) & 1) {
        uint8_t status = inb(0x64);
        int8_t data = inb(0x60);
        if (!(status & 0x20)) continue;   /* keyboard data — leave it for IRQ1 */

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

/* ---------- open window ---------- */
static void open_window(int app_id) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            window_t *w = &windows[i];
            w->active = 1;
            w->x = 250 + i * 30;
            w->y = 120 + i * 30;
            w->w = 560;
            w->h = 360;
            w->app_id = app_id;
            w->dragging = 0;

            const char *t = "Window";
            if (app_id == 0) t = "Settings";
            if (app_id == 1) t = "Shell";
            if (app_id == 2) t = "Explorer";
            int j = 0;
            while (t[j] && j < 31) { w->title[j] = t[j]; j++; }
            w->title[j] = 0;

            /* if shell, initialize buffer and print welcome */
            if (app_id == 1) {
                w->shell.out_count = 0;
                w->shell.line_len = 0;
                w->shell.line[0] = 0;
                w->shell.cursor_blink = 0;
                shell_push_line(&w->shell, "DECC OS shell v0.4");
                shell_push_line(&w->shell, "type 'help' for commands");
            }

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

static void draw_app_icon(int x, int y, uint32_t color, char letter) {
    fb_fill(x, y, 48, 48, color);
    fb_rect(x, y, 48, 48, 0xFFFFFF, 1);
    char s[2] = { letter, 0 };
    fb_draw_text(x + 20, y + 16, s, 0x101020);
}

static void draw_icons(void) {
    for (int i = 0; i < ICON_COUNT; i++) {
        icon_t *ic = &icons[i];
        char letter = ic->label[0];
        draw_app_icon(ic->x, ic->y, ic->color, letter);
        int tw = fb_text_width(ic->label);
        int tx = ic->x + 24 - tw / 2;
        int ty = ic->y + 54;
        fb_draw_text(tx + 1, ty + 1, ic->label, 0x000000);
        fb_draw_text(tx, ty, ic->label, 0xFFFFFF);
    }
}

static void draw_taskbar(void) {
    int y = fb.height - TASKBAR_H;
    fb_fill(0, y, fb.width, TASKBAR_H, 0x181828);
    fb_fill(0, y, fb.width, 1, 0x404060);

    fb_fill(8, y + 5, 110, TASKBAR_H - 10, 0x2A4A90);
    fb_rect(8, y + 5, 110, TASKBAR_H - 10, 0x5FD0FF, 1);
    fb_draw_text(20, y + 10, "Start", 0xFFFFFF);

    fb_draw_text(fb.width - 80, y + 10, "12:34", 0xFFFFFF);
}

static void draw_shell_content(window_t *w) {
    int px = w->x + 8;
    int py = w->y + 36;
    int pw = w->w - 16;
    int ph = w->h - 44;

    /* terminal bg */
    fb_fill(px, py, pw, ph, 0x000000);

    /* scrollback lines */
    for (int r = 0; r < w->shell.out_count; r++) {
        fb_draw_text(px + 6, py + 6 + r * 16, w->shell.out[r], 0xC0E0C0);
    }

    /* current input line, prefixed with prompt */
    int line_y = py + 6 + w->shell.out_count * 16;
    fb_draw_text(px + 6, line_y, "DECC> ", 0x5FFFA0);
    fb_draw_text(px + 6 + 6 * 8, line_y, w->shell.line, 0xFFFFFF);

    /* cursor — only if this is the focused window */
    if (w == &windows[focused]) {
        int cur_col = 6 + 6 + w->shell.line_len;
        int cx = px + 6 + cur_col * 8;
        if (w->shell.cursor_blink) {
            fb_fill(cx, line_y, 8, 16, 0xFFFFFF);
            fb_draw_text(cx, line_y, " ", 0x000000);
        } else {
            fb_fill(cx, line_y + 14, 8, 2, 0xFFFFFF);
        }
    }
}

static void draw_window(window_t *w) {
    if (!w->active) return;

    fb_fill(w->x + 6, w->y + 6, w->w, w->h, 0x101020);
    fb_fill(w->x, w->y, w->w, w->h, 0x202840);

    uint32_t tb_color = (w == &windows[focused]) ? 0x3A6AC0 : 0x2A3A70;
    fb_fill(w->x, w->y, w->w, 28, tb_color);
    fb_rect(w->x, w->y, w->w, w->h, 0xFFFFFF, 1);

    fb_draw_text(w->x + 8, w->y + 6, w->title, 0xFFFFFF);

    fb_fill(w->x + w->w - 24, w->y + 6, 16, 16, 0xC02020);
    fb_rect(w->x + w->w - 24, w->y + 6, 16, 16, 0xFFFFFF, 1);

    if (w->app_id == 0) {
        fb_draw_text(w->x + 16, w->y + 44, "Settings", 0xFFFFFF);
        fb_fill(w->x + 16, w->y + 70, 200, 20, 0x404A80);
        fb_fill(w->x + 16, w->y + 70, 140, 20, 0x5FD0FF);
        fb_draw_text(w->x + 16, w->y + 100, "Volume", 0xC0C0C0);
        fb_fill(w->x + 16, w->y + 120, 200, 20, 0x404A80);
        fb_fill(w->x + 16, w->y + 120, 80, 20, 0x5FFFA0);
        fb_draw_text(w->x + 16, w->y + 150, "Brightness", 0xC0C0C0);
    } else if (w->app_id == 1) {
        draw_shell_content(w);
    } else if (w->app_id == 2) {
        fb_draw_text(w->x + 16, w->y + 40, "Home", 0xFFFFFF);
        for (int i = 0; i < 6; i++) {
            int gx = w->x + 20 + (i % 3) * 90;
            int gy = w->y + 70 + (i / 3) * 90;
            uint32_t col = (i == 0) ? 0xFFD05F : (i == 1) ? 0x5FD0FF :
                           (i == 2) ? 0xFF5FA2 : (i == 3) ? 0xB45FFF :
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

/* ---------- click handling ---------- */
static void handle_click(int mx, int my) {
    /* close buttons */
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
    /* titlebars */
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
    /* desktop icons */
    for (int i = 0; i < ICON_COUNT; i++) {
        if (hit_icon(&icons[i], mx, my)) {
            open_window(icons[i].app_id);
            return;
        }
    }
    /* clicking inside any window body focuses it */
    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (!w->active) continue;
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h) {
            focused = i;
            return;
        }
    }
}

/* ---------- interrupt dispatch ---------- */
extern void keyboard_handler(void);
void isr_handler(uint32_t irq) {
    if (irq == 33) keyboard_handler();      /* keyboard IRQ1 */
    /* (mouse is polled, not IRQ-driven, for now) */
}

/* ---------- main ---------- */
void kernel_main(uint32_t magic, uint32_t mbi_addr) {
    (void)magic;
    multiboot_info_t *mbi = (multiboot_info_t *)mbi_addr;

    gdt_init();
    idt_init();           /* installs IRQ gates, unmasks IRQ1, sti */

    fb_init(mbi->framebuffer_addr,
            mbi->framebuffer_pitch,
            mbi->framebuffer_width,
            mbi->framebuffer_height,
            mbi->framebuffer_bpp);

    mouse_init();
    keyboard_init();

    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].active = 0;

    /* Open a Shell by default so you can start typing right away */
    open_window(1);

    while (1) {
        /* ---- keyboard: drain any queued chars into focused shell ---- */
        while (keyboard_haschar()) {
            char c = keyboard_getchar();
            if (focused >= 0 && windows[focused].active &&
                windows[focused].app_id == 1) {
                shell_input_char(&windows[focused].shell, c);
            }
        }

        /* ---- mouse ---- */
        mouse_poll();
        int left = mouse_packet[0] & 1;
        if (left && !prev_left) handle_click(cur_x, cur_y);
        if (!left && prev_left)
            for (int i = 0; i < MAX_WINDOWS; i++) windows[i].dragging = 0;
        if (left) {
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i].active && windows[i].dragging) {
                    windows[i].x = cur_x - windows[i].drag_dx;
                    windows[i].y = cur_y - windows[i].drag_dy;
                }
            }
        }
        prev_left = left;

        /* ---- blink cursor ---- */
        static int blink_counter = 0;
        if (++blink_counter > 2000) {          /* rough cadence */
            blink_counter = 0;
            for (int i = 0; i < MAX_WINDOWS; i++)
                if (windows[i].active) windows[i].shell.cursor_blink ^= 1;
        }

        /* ---- render ---- */
        draw_wallpaper();
        draw_icons();
        for (int i = 0; i < MAX_WINDOWS; i++)
            if (windows[i].active && i != focused) draw_window(&windows[i]);
        if (focused >= 0 && windows[focused].active) draw_window(&windows[focused]);
        draw_taskbar();
        draw_cursor();
        fb_swap();

        for (volatile uint32_t i = 0; i < 20000; i++) {}
    }
}