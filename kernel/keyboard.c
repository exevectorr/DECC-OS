#include "keyboard.h"
#include <stdint.h>

#define KBD_BUF_SIZE 128

static char     kbd_buf[KBD_BUF_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;
static int shift = 0;
static int ctrl  = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(port));
}

/* Scancode set 1 — US layout, no shift */
static const char scancode_lower[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',
    0,   '*', 0,  ' ',
    /* extended / function keys ignored */
};

/* With shift held */
static const char scancode_upper[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',
    0,   '*', 0,  ' ',
};

void keyboard_init(void) {
    kbd_head = 0;
    kbd_tail = 0;
    shift = 0;
    ctrl  = 0;
}

int keyboard_haschar(void) {
    return kbd_head != kbd_tail;
}

char keyboard_getchar(void) {
    while (kbd_head == kbd_tail) {
        __asm__ volatile("hlt");
    }
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

int keyboard_shift(void) { return shift; }
int keyboard_ctrl(void)  { return ctrl;  }

/* Called from IRQ1 */
void keyboard_handler(void) {
    uint8_t sc = inb(0x60);

    /* ---- modifier key tracking (both press and release) ---- */
    if (sc == 0x2A || sc == 0x36) { shift = 1; goto eoi; }   /* left/right shift press */
    if (sc == 0xAA || sc == 0xB6) { shift = 0; goto eoi; }   /* shift release */
    if (sc == 0x1D) { ctrl = 1; goto eoi; }                  /* ctrl press */
    if (sc == 0x9D) { ctrl = 0; goto eoi; }                  /* ctrl release */

    /* ---- ignore key release events for normal keys ---- */
    if (sc & 0x80) goto eoi;

    /* ---- translate ---- */
    {
        char c = shift ? scancode_upper[sc] : scancode_lower[sc];
        if (c) {
            int next = (kbd_head + 1) % KBD_BUF_SIZE;
            if (next != kbd_tail) {
                kbd_buf[kbd_head] = c;
                kbd_head = next;
            }
        }
    }

eoi:
    outb(0x20, 0x20);   /* EOI to master PIC */
}