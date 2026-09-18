CC      := gcc
LD      := ld
NASM    := nasm
CFLAGS  := -m32 -ffreestanding -fno-stack-protector -fno-pic -Wall -Wextra -O2 -nostdlib
LDFLAGS := -m elf_i386 -T linker.ld
ISO     := myos.iso
KERNEL  := iso/boot/myos.bin

C_SRCS  := kernel/kernel.c kernel/gdt.c kernel/idt.c kernel/framebuffer.c kernel/mouse.c kernel/font.c
ASM_SRCS:= boot/boot.asm kernel/isr.asm

OBJS    := $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

all: $(ISO)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(NASM) -f elf32 $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) iso

run: $(ISO)
	qemu-system-i386 -cdrom $(ISO) -m 256

clean:
	rm -f $(OBJS) $(KERNEL) $(ISO)

.PHONY: all run clean