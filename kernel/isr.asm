bits 32

section .text
extern isr_handler
global isr32
global isr44

isr32:              ; IRQ0 — timer (unused for now)
    pusha
    push 32
    call isr_handler
    add esp, 4
    popa
    iret

isr44:              ; IRQ12 — mouse
    pusha
    push 44
    call isr_handler
    add esp, 4
    popa
    iret