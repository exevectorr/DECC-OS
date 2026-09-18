#include "idt.h"
#include <stdint.h>

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

void idt_set_gate(int n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].base_lo = base & 0xFFFF;
    idt[n].base_hi = (base >> 16) & 0xFFFF;
    idt[n].sel     = sel;
    idt[n].always0 = 0;
    idt[n].flags   = flags;
}

extern void isr32(void);   /* IRQ0 timer   */
extern void isr33(void);   /* IRQ1 keyboard */
extern void isr44(void);   /* IRQ12 mouse  */

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0, 0);

    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr33, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)isr44, 0x08, 0x8E);

    /* PIC remap */
    #define PIC1 0x20
    #define PIC2 0xA0
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x11), "Nd"((uint16_t)PIC1));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x11), "Nd"((uint16_t)PIC2));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x20), "Nd"((uint16_t)(PIC1+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x28), "Nd"((uint16_t)(PIC2+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x04), "Nd"((uint16_t)(PIC1+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x02), "Nd"((uint16_t)(PIC2+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x01), "Nd"((uint16_t)(PIC1+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x01), "Nd"((uint16_t)(PIC2+1)));

    /* Masks:
       master: bit0=timer, bit1=keyboard, bit2=cascade
       We want bit1 unmasked (keyboard), bit0 masked (no timer yet),
       bit2 unmasked (cascade so slave IRQs can reach CPU).
       0xFA = 1111_1010  -> mask bits 0,2? No. Let's compute carefully.
       We want ON: bits that are UNMASKED = 0 in the byte.
       bit1 (keyboard) -> 0
       bit2 (cascade)  -> 0  (needed for slave to reach CPU)
       bit0 (timer)    -> 1  (masked)
       bits 3-7        -> 1  (masked)
       Result: 1111_1001 = 0xF9
    */
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xF9), "Nd"((uint16_t)(PIC1+1)));
    /* slave: mask everything for now (mouse not enabled yet) */
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xFF), "Nd"((uint16_t)(PIC2+1)));

    __asm__ volatile("lidt %0" :: "m"(idtp));
    __asm__ volatile("sti");
}