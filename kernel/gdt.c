/* gdt.c */
#include "gdt.h"
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];
static struct gdt_ptr   gp;

static void set_gate(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base >> 16) & 0xFF;
    gdt[i].base_high = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].gran      = (limit >> 16) & 0x0F;
    gdt[i].gran     |= gran & 0xF0;
    gdt[i].access    = access;
}

void gdt_init(void) {
    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint32_t)&gdt;

    set_gate(0, 0, 0,          0,    0);        /* null */
    set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);        /* code: ring0, 4K gran, 32-bit */
    set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);        /* data: ring0, 4K gran, 32-bit */

    __asm__ volatile("lgdt (%0)" :: "r"(&gp));
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        ::: "eax", "memory"
    );
}