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

extern void isr32(void);   /* IRQ0 timer */
extern void isr44(void);   /* IRQ12 mouse */

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;

    /* Clear all gates */
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0, 0);

    /* Install the two IRQ handlers we have stubs for.
       Flags = 0x8E = present, ring 0, 32-bit interrupt gate. */
    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)isr44, 0x08, 0x8E);

    /* Remap PIC: IRQ0..15 -> IDT 32..47 */
    #define PIC1 0x20
    #define PIC2 0xA0
    #define ICW1 0x11
    #define ICW4 0x01

    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)ICW1), "Nd"((uint16_t)PIC1));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)ICW1), "Nd"((uint16_t)PIC2));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x20), "Nd"((uint16_t)(PIC1+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x28), "Nd"((uint16_t)(PIC2+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x04), "Nd"((uint16_t)(PIC1+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0x02), "Nd"((uint16_t)(PIC2+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)ICW4), "Nd"((uint16_t)(PIC1+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)ICW4), "Nd"((uint16_t)(PIC2+1)));

    /* Mask ALL IRQs at the PIC. 0xFF = everything masked.
       We'll unmask them individually as we add handlers. */
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xFF), "Nd"((uint16_t)(PIC1+1)));
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)0xFF), "Nd"((uint16_t)(PIC2+1)));

    __asm__ volatile("lidt %0" :: "m"(idtp));

    /* NOTE: deliberately NOT calling sti. Interrupts stay disabled
       until we have handlers for everything we might receive.
       Re-enable later with: __asm__ volatile("sti"); */
}