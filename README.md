# 🖥️ DECC OS

> A small 32-bit x86 operating system kernel written from scratch in C and Assembly.

![version](https://img.shields.io/badge/version-1.0.0-blue)
![platform](https://img.shields.io/badge/platform-x86%20%7C%20QEMU%20%7C%20Real%20HW-lightgrey)
![license](https://img.shields.io/badge/license-OAL%201.0-orange)
![lang](https://img.shields.io/badge/language-C%20%2B%20Assembly-red)
![boot](https://img.shields.io/badge/boot-GRUB%20%2F%20Multiboot-purple)

DECC OS boots on real hardware (and in QEMU) via GRUB, sets up a VESA linear framebuffer, runs a double-buffered compositor at 1024×768×32, and provides a minimal desktop environment with a mouse-driven window manager, movable windows, desktop icons, a taskbar, and a working shell.

It is not a Linux distro. It is not based on any existing kernel. It is roughly 1,500 lines of C and assembly that boot the machine, take over the screen, and run a small GUI.

---

## 📦 Versions

### 🟦 v1.0

The first version that could reasonably be called a GUI. It boots, sets up a framebuffer, and draws a desktop with icons, a taskbar, and movable windows. The mouse works. Windows can be opened, dragged by their titlebar, focused by clicking, and closed via the red button. The shell window opens and displays text, but you **cannot type into it**, input is not wired up yet.

**What works in v1.0:**

- ✅ Boot via GRUB and Multiboot
- ✅ VESA linear framebuffer at 1024×768×32
- ✅ Double-buffered compositor (no tearing)
- ✅ Bitmap 8×16 font, ASCII 32 to 126
- ✅ PS/2 mouse, polled from the main loop
- ✅ Window manager: up to 8 windows, click to focus, drag titlebar, close button
- ✅ Desktop icons for Settings, Shell, and Explorer
- ✅ Taskbar with Start button and static clock
- ✅ Static shell window with hardcoded welcome text
- ✅ Explorer window with fake file grid
- ✅ Settings window with fake sliders

**What does not work in v1.0:**

- ❌ Keyboard input
- ❌ Typing into the shell
- ❌ Any interaction beyond the mouse

### 🟩 v1.1

Adds keyboard input on IRQ1 and turns the shell window into a working terminal. You can now click the Shell window, type commands, see them echoed, edit the current line with backspace, and press Enter to run them. The shell has a scrollback buffer, a blinking cursor, and a handful of commands.

**What is new in v1.1:**

- ⌨️ PS/2 keyboard driver on IRQ1, scancode set 1, US layout
- 🔠 Shift and Ctrl modifiers tracked
- 🔁 128-byte ring buffer between the IRQ handler and the main loop
- ⌫ Line editing with backspace
- ▶️ Command dispatch on Enter
- 📜 Shell scrollback buffer (14 lines)
- 💡 Blinking cursor, only drawn in the focused window
- 🧾 Commands: `help`, `clear`, `about`, `echo <text>`, `date`
- 🎯 Focus system routes typed characters to the focused window only

Everything from v1.0 still works exactly the same in v1.1.

---

## 🤝 What both versions share

Both versions have:

- 🚀 Boot via Multiboot. GRUB loads the ELF kernel, jumps into `_start`
- 🧱 GDT: flat 4 GB segments, ring 0 only
- ⚡ IDT + PIC remap, IRQ0 to 15 remapped to vectors 32 to 47
- 🖼️ VESA framebuffer at 1024×768, 32 bits per pixel, requested via the Multiboot header
- 🎞️ Double buffering: all drawing goes to a 3 MB back buffer, then a single `fb_swap()` blits to VRAM
- 🔤 Bitmap font, embedded 8×16 VGA font, ASCII 32 to 126
- 🖱️ PS/2 mouse, polled from the 0x60/0x64 I/O ports
- 🪟 Window manager: up to 8 windows, z-order via focus, click to focus, drag by titlebar, close button
- 🗂️ Desktop icons for Settings, Shell, Explorer
- 📊 Taskbar with Start button and static clock
- 💬 A shell window with scrollback display

---

## 🚫 What neither version does

- ❌ No filesystem. Explorer shows fake files.
- ❌ No process scheduler. Everything runs in one `while(1)` loop.
- ❌ No user mode. Everything is ring 0.
- ❌ No syscalls.
- ❌ No memory allocator (`kmalloc` / `free`).
- ❌ No timer IRQ, the taskbar clock is hardcoded, the cursor blink in v1.1 is a spin-loop counter.
- ❌ No mouse IRQ, the mouse is polled in both versions, not interrupt-driven.
- ❌ No USB. PS/2 only, which means this won't drive a USB keyboard or mouse on real hardware without a BIOS handoff.
- ❌ No SMP. One CPU.
- ❌ No ACPI, no APIC. Legacy PIC only.

> ⚠️ **DECC OS is a learning project, not a usable OS.**

---

## 📸 Screenshots
![DECC-OS Screenshot](https://raw.githubusercontent.com/exevectorr/DECC-OS/main/assets/screenshot.png)
---

## 🛠️ Building

### 📋 Requirements

You need a Linux environment (native, WSL2, or a VM) with:

- 🧰 `gcc` with 32-bit support (`gcc-multilib` on Debian/Ubuntu)
- 🔧 `nasm`
- 🔗 `ld` (binutils)
- 📀 `grub-mkrescue`, `grub-pc-bin`, `grub-common`
- 💿 `xorriso`
- 📦 `mtools`
- 🖥️ `qemu-system-i386` (to run it)

On Ubuntu or WSL, install everything with:

    sudo apt update
    sudo apt install -y build-essential nasm xorriso qemu-system-x86 grub-pc-bin grub-common mtools gcc-multilib

### 🔨 Build

    make

This produces `deccos.iso`, a bootable CD image.

### ▶️ Run

    make run

or manually:

    qemu-system-i386 -m 256 -cdrom deccos.iso -boot d

### 🐞 Debug

If the kernel triple-faults, run with QEMU halt-on-reset so you can see the last frame instead of a blink:

    qemu-system-i386 -m 256 -cdrom deccos.iso -boot d -no-reboot -no-shutdown -d int,cpu_reset -D qemu.log

Then inspect the log:

    grep -E "check_exception|Triple|v=" qemu.log

The `v=` line tells you the CPU exception vector that fired, and the `pc=` field tells you the exact instruction that faulted.

---

## 📁 Project layout

    .
    ├── Makefile
    ├── linker.ld
    ├── README.md
    ├── boot/
    │   └── boot.asm              # Multiboot header, _start entry point
    ├── kernel/
    │   ├── kernel.c              # kernel_main, main loop, window manager
    │   ├── multiboot.h           # Multiboot info struct layout
    │   ├── gdt.c / gdt.h         # Global Descriptor Table
    │   ├── idt.c / idt.h         # Interrupt Descriptor Table + PIC remap
    │   ├── isr.asm               # IRQ stubs (32, 33, 44)
    │   ├── framebuffer.c / .h    # VESA linear FB, double buffer, text
    │   ├── font.c / font.h       # 8x16 bitmap font, ASCII 32-126
    │   ├── keyboard.c / .h       # PS/2 scancode to ASCII, IRQ1 (v1.1)
    │   └── mouse.c / mouse.h     # PS/2 mouse, polled from main loop
    └── iso/
        └── boot/
            └── grub/
                └── grub.cfg      # GRUB menu entry

---

## 🧾 Shell commands (v1.1)

Type these into the Shell window (click it first to give it focus):

| Command         | What it does                         |
|-----------------|--------------------------------------|
| `help`          | List available commands              |
| `clear`         | Clear the shell scrollback           |
| `about`         | Version and build info               |
| `echo <text>`   | Print text back                      |
| `date`          | Show a fake date (no RTC driver yet) |

---

## 🧠 How it works, briefly

**🚀 Boot.** GRUB reads the Multiboot header at the top of the kernel ELF, sets up a VBE linear framebuffer at the requested mode, loads the kernel at 1 MB, and jumps to `_start`. The header must be within the first 8 KB of the file, and the linker script enforces this by placing `.multiboot` in its own section before `.text`.

**🧱 Memory layout.** The kernel loads at physical address `0x100000`. A 16 KB stack sits in `.bss`. There is no paging yet, so every address is a physical address. The framebuffer is mapped by the firmware somewhere in high memory (`0xFD000000` on QEMU's std VGA), and we write to it directly.

**⚡ Interrupts.** The IDT is populated at init time with gates for IRQ0 (unused), IRQ1 (keyboard), and IRQ12 (mouse). The PIC is remapped so IRQ0 to 7 map to vectors 32 to 39 and IRQ8 to 15 map to vectors 40 to 47. In v1.1, IRQ1 is unmasked and wired to a keyboard handler. When a key is pressed, the keyboard controller raises IRQ1, the CPU vectors to `isr33` in `isr.asm`, which pushes registers, calls `keyboard_handler` in C, and returns via `iret`.

**🎞️ Rendering.** Every frame, the kernel draws to a 3 MB static back buffer. Wallpaper, icons, windows, taskbar, cursor, all into the back buffer. Then `fb_swap()` copies the back buffer to VRAM row by row. Since VRAM is only touched during the swap, there is no tearing.

**🎮 Input.** In v1.0, only the mouse works, polled from the main loop by reading port 0x64 status, then port 0x60 for the 3-byte packet. In v1.1, the keyboard is added on top: IRQ1 feeds a 128-byte ring buffer, and the main loop drains it every frame into whichever window has focus.

---

## 📜 License

**OPEN ATTRIBUTION LICENSE (OAL) 1.0**
This project is released under the Open Attribution License 1.0. You are free to use, modify, and distribute DECC OS, including for commercial purposes, provided that you give clear attribution to the original author and include a copy of the license with any redistribution. Full license text accompanies the source.

---

## 🙏 Credits

Credits to exevectorr for the original DECC OS
discord: dev_0.0
github: https://github.com/exevectorr
