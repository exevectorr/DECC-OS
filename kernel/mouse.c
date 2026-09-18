#include "mouse.h"
#include "framebuffer.h"
#include <stdint.h>

static int mx = 512, my = 384;
static int mleft = 0;
static uint8_t cycle = 0;
static int8_t  packet[3];

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}

static void mouse_wait(uint8_t type) {
    uint32_t t = 100000;
    if (type == 0) { while (t-- && (inb(0x64) & 1)) {} }
    else           { while (t-- && !(inb(0x64) & 1)) {} }
}

static void mouse_write(uint8_t v) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, v);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_init(void) {
    /* Enable auxiliary device */
    mouse_wait(1); outb(0x64, 0xA8);

    /* Enable IRQ12 in the controller command byte — but since PIC
       masks IRQ12 for now, this just prepares the device. */
    mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = inb(0x60) | 2;
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, status);

    mouse_write(0xF6); mouse_read();   /* set defaults */
    mouse_write(0xF4); mouse_read();   /* enable data reporting */

    mx = fb.width  / 2;
    my = fb.height / 2;
}

void mouse_handler(void) {
    uint8_t st = inb(0x64);
    if (!(st & 0x20)) return;

    int8_t data = inb(0x60);

    switch (cycle) {
        case 0:
            packet[0] = data;
            if (!(data & 0x08)) return;
            cycle = 1;
            break;
        case 1:
            packet[1] = data;
            cycle = 2;
            break;
        case 2:
            packet[2] = data;
            cycle = 0;

            mleft = packet[0] & 1;
            int dx = packet[1];
            int dy = packet[2];
            if (packet[0] & 0x10) dx |= 0xFFFFFF00;
            if (packet[0] & 0x20) dy |= 0xFFFFFF00;

            mx += dx;
            my -= dy;

            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if ((uint32_t)mx >= fb.width)  mx = fb.width  - 1;
            if ((uint32_t)my >= fb.height) my = fb.height - 1;
            break;
    }
    /* EOI is sent by isr_handler() in kernel.c, not here. */
}

int mouse_x(void)    { return mx; }
int mouse_y(void)    { return my; }
int mouse_left(void) { return mleft; }

static const uint8_t cursor[8] = {
    0b10000000, 0b11000000, 0b11100000, 0b11110000,
    0b11111000, 0b11100000, 0b10100000, 0b00000000
};

void mouse_draw_cursor(void) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (cursor[y] & (0x80 >> x))
                fb_pixel(mx + x, my + y, 0xFFFFFF);
}