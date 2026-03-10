# Tiny OS — Minimal x86 Kernel

A bare-metal x86 kernel written in C and Assembly, built entirely from scratch — no standard library, no OS support layer. It boots, configures the CPU's core protection mechanisms, and then waits for hardware interrupts while echoing keyboard input to the screen.

This project is designed to be **read like a book**: every file is short, every decision has a comment explaining the *why*, and the boot sequence follows a strict dependency order that mirrors how real kernels start.

---

## What it does

When you run the kernel (under QEMU or on real hardware), the following happens in order:

```
Bootloader (GRUB/QEMU)
    └── boot.S          — sets up a 16 KB stack, calls kernel_main()
        └── kernel_main()
            ├── vga_init()        — clears screen, enables cursor
            ├── init_gdt()        — loads a flat 5-entry descriptor table
            ├── init_paging()     — identity-maps first 4 MB, enables MMU
            ├── init_idt()        — installs 32 exception + 16 IRQ gates
            ├── init_irq()        — remaps 8259 PIC to vectors 32–47
            ├── init_timer(100)   — PIT fires IRQ0 at 100 Hz
            ├── init_keyboard()   — PS/2 keyboard echoes to screen
            ├── kmalloc_init()    — 2 MB bump heap from 0x200000
            ├── sti               — hardware interrupts go live
            └── hlt loop          — CPU sleeps between interrupts
```

After boot you'll see a startup log and a `>` prompt. Typing on the keyboard echoes characters to the screen in real time.

---

## File map

```
tinyos/
├── boot.S          Multiboot entry point — the very first code to run
├── helpers.S       CPU instruction wrappers (outb, inb, lgdt, lidt, CR0/CR3…)
├── isr.S           48 tiny assembly stubs — one per exception/IRQ
│
├── gdt.h / gdt.c   Global Descriptor Table (5 segments, flat model)
├── idt.h / idt.c   Interrupt Descriptor Table + exception panic handler
├── irq.h / irq.c   8259 PIC remapping + IRQ dispatch table
│
├── paging.h / paging.c   Two-level page table, identity maps 4 MB
├── timer.h / timer.c     PIT 8253 driver — 100 Hz tick counter
├── keyboard.h / keyboard.c  PS/2 scancode set 1 → ASCII driver
├── vga.h / vga.c         VGA text terminal (colors, scroll, printf)
├── kmalloc.h / kmalloc.c  Bump allocator — 2 MB kernel heap
│
└── kernel.c        Entry point — wires everything together
```

---

## Concepts covered

### Global Descriptor Table (GDT) — `gdt.c`

In 32-bit protected mode the CPU won't let you just use raw addresses. Every memory access goes through a *segment descriptor* that says: "this region is readable / executable / user-level or kernel-only." Our GDT has five entries:

| # | Selector | Description        | Ring |
|---|----------|--------------------|------|
| 0 | —        | Null (required)    | —    |
| 1 | `0x08`   | Kernel Code        | 0    |
| 2 | `0x10`   | Kernel Data        | 0    |
| 3 | `0x18`   | User Code          | 3    |
| 4 | `0x20`   | User Data          | 3    |

We use a *flat model* — all segments span the full 4 GB address space and protection comes from paging, not segmentation.

### Interrupt Descriptor Table (IDT) — `idt.c` + `isr.S`

When the CPU takes an interrupt it looks up vector N in the IDT to find the handler address. Our table has 48 live entries:

- Vectors **0–31**: CPU exceptions (Divide Error, Page Fault, General Protection Fault, …)
- Vectors **32–47**: Hardware IRQs (timer, keyboard, ATA disk, …)

Because each handler needs a uniform stack frame before calling C, `isr.S` provides 48 tiny assembly stubs. Each stub pushes the interrupt number and (if the CPU didn't push one) a dummy error code, then jumps to a shared C dispatcher.

The C dispatcher either calls a registered handler function or, for unhandled exceptions, prints a full register dump in red and halts.

### 8259 PIC remapping — `irq.c`

Out of the box the two 8259 PICs fire hardware IRQs on vectors 8–15 and `0x70–0x77` — the same range as CPU exceptions. We reprogram them through a 4-step ICW handshake to use vectors 32–47 instead.

After remapping, every IRQ handler must send an *End Of Interrupt* byte back to the PIC or it will never fire again.

### Virtual Memory / Paging — `paging.c`

x86 paging uses a two-level table. The CPU splits every 32-bit virtual address into three fields:

```
 31        22 21        12 11          0
 ┌──────────┬──────────┬─────────────┐
 │  Dir[10] │  Tbl[10] │  Offset[12] │
 └──────────┴──────────┴─────────────┘
```

`CR3` points to the Page Directory. Entry `Dir` in the directory points to a Page Table. Entry `Tbl` in that table gives the physical page. The offset is added directly.

We identity-map the first 4 MB (`virtual 0x0 == physical 0x0`) so the kernel can run without translating any of its own addresses.

### VGA Terminal — `vga.c`

The VGA controller exposes an 80×25 character grid at physical address `0xB8000`. Each cell is two bytes: the ASCII code and a colour attribute byte. We also write to I/O ports `0x3D4/0x3D5` to move the blinking hardware cursor. The driver supports:

- 16 foreground and background colours
- Newline, tab, backspace
- Automatic scrolling when the last row is reached
- A minimal `vga_printf` that handles `%s`, `%c`, `%d`, `%u`, `%x`

### Programmable Interval Timer — `timer.c`

The PIT has a fixed 1,193,182 Hz oscillator. We write a 16-bit divisor to I/O port `0x40` to set the interrupt frequency:

```
divisor = 1,193,182 / desired_hz
```

At 100 Hz, IRQ0 fires every 10 ms and increments a global tick counter.

### PS/2 Keyboard — `keyboard.c`

IRQ1 fires when a key is pressed or released. The scancode byte sits in I/O port `0x60`. Bit 7 distinguishes presses (0) from releases (1). We map the 58 most common scancodes to ASCII using a lookup table and echo each character to the VGA terminal.

### Bump Allocator — `kmalloc.c`

The simplest allocator possible: a pointer into a reserved memory region that only moves forward. Allocation is O(1); deallocation doesn't exist. All returned pointers are 8-byte aligned. The kernel heap lives in physical addresses 2 MB – 4 MB.

---

## Build & run

### Prerequisites

| Tool | Purpose |
|------|---------|
| `i686-elf-gcc` | Cross-compiler (no host OS headers leak in) |
| `i686-elf-ld` | Cross-linker |
| `nasm` or GNU `as` | Assembler |
| `qemu-system-i386` | Virtual machine for testing |

### Compile

```bash
# Assemble
i686-elf-as boot.S     -o boot.o
i686-elf-as helpers.S  -o helpers.o
i686-elf-as isr.S      -o isr.o

# Compile C (no stdlib, freestanding environment)
i686-elf-gcc -m32 -std=gnu99 -ffreestanding -O2 -Wall -c \
    gdt.c idt.c irq.c paging.c timer.c keyboard.c vga.c kmalloc.c kernel.c

# Link — linker.ld must place .multiboot at the start
i686-elf-ld -m elf_i386 -T linker.ld -o kernel.bin \
    boot.o helpers.o isr.o \
    gdt.o idt.o irq.o paging.o timer.o keyboard.o vga.o kmalloc.o kernel.o
```

### Run

```bash
qemu-system-i386 -kernel kernel.bin
```

Expected output:

```
      Tiny OS
      Minimal x86 Kernel
  ────────────────────────────────────────
  [ OK ]  GDT  initialised (5 descriptors)
  [ OK ]  MMU  enabled (first 4 MB identity-mapped)
  [ OK ]  IDT  loaded (32 exception gates + 16 IRQ gates)
  [ OK ]  PIC  remapped (IRQ0–15 → vectors 32–47)
  [ OK ]  PIT  running at 100 Hz
  [ OK ]  KB   PS/2 keyboard ready
  [ OK ]  HEAP bump allocator: 2 MB pool at 0x00200000
  ────────────────────────────────────────

  All subsystems online. Interrupts enabled.
  > _
```

---

## Design notes

**Why no `libc`?** At boot time there is no operating system to call into. Every function we use must be written by us. That's also what makes this educational — you see exactly where `printf` would come from.

**Why identity mapping?** After enabling paging, every pointer in the kernel still holds a physical address. If we didn't identity-map, the very next instruction would fault. A real OS would map the kernel to high virtual memory (`0xC0000000+`) and keep low memory for user space.

**Why a bump allocator?** It's a stepping stone. Once the kernel has parsed the Multiboot memory map it knows exactly which physical pages are free, and a proper free-list or buddy allocator can be built on top.

---

## Extending the kernel

Some natural next steps, roughly in order of difficulty:

- **More ISRs** — register a handler in `idt.c` for `#PF` (page fault) to print the faulting address from `CR2`.
- **Kernel shell** — use `keyboard_last_char()` to build a command line buffer and react to `\n`.
- **Free memory map** — read the Multiboot `mmap` tag passed in `EBX` from `boot.S` to discover how much RAM is available.
- **Physical page allocator** — replace the bump allocator with a free-list over 4 KB frames.
- **User mode** — set up a TSS, switch to ring 3 via `iret`, handle system calls on `int 0x80`.
- **VFS / disk** — implement the ATA PIO driver (IRQ14/15) and a simple FAT or ext2 reader.

---

## License

Released for educational use. Fork, read, break things, learn.
