# Tiny OS — Minimal x86 Kernel with Logging, Syscalls & Multitasking

A bare-metal x86 kernel written in C and Assembly, built entirely from scratch — no standard library, no OS support layer. It boots, configures the CPU's core protection mechanisms, and now includes a logging system, process management, system calls, and basic round-robin multitasking.

## What it does

When you run the kernel (under QEMU or on real hardware), the following happens in order:

```
Bootloader (GRUB/QEMU)
    └── boot.S          — sets up a 16 KB stack, calls kernel_main()
        └── kernel_main()
            ├── vga_init()        — clears screen, enables cursor
            ├── log_init()        — initializes logging system
            ├── init_gdt()        — loads a flat 5-entry descriptor table
            ├── init_paging()     — identity-maps first 4 MB, enables MMU
            ├── init_idt()        — installs 32 exception + 16 IRQ gates
            ├── init_irq()        — remaps 8259 PIC to vectors 32–47
            ├── init_timer(100)   — PIT fires IRQ0 at 100 Hz
            ├── init_keyboard()   — PS/2 keyboard echoes to screen
            ├── kmalloc_init()    — 2 MB kernel heap
            ├── init_processing() — initializes process management
            ├── Register syscall handler (INT 0x80)
            ├── Create test processes
            └── hlt loop          — CPU sleeps between interrupts
```

After boot you'll see a colored startup log showing each subsystem initialization, followed by two test processes that yield to each other and print messages.

## File map

```
tinyos/
├── boot.S          Multiboot entry point — the very first code to run
├── helpers.S       CPU instruction wrappers (outb, inb, lgdt, lidt, CR0/CR3…)
├── isr.S           48 tiny assembly stubs — one per exception/IRQ
├── process.S       Assembly helpers for process context switching
│
├── include/                # All header files
│   ├── gdt.h
│   ├── idt.h
│   ├── irq.h
│   ├── keyboard.h
│   ├── kmalloc.h
│   ├── log.h
│   ├── paging.h
│   ├── process.h
│   ├── syscall.h
│   ├── timer.h
│   └── vga.h
│
├── kernel/
│   ├── kernel.c        # Entry point — wires everything together
│   └── syscall.c       # System call implementation
│
├── drivers/              # Device drivers and core subsystems
│   ├── gdt.c             # Global Descriptor Table
│   ├── idt.c             # Interrupt Descriptor Table + exception panic handler
│   ├── irq.c             # 8259 PIC remapping + IRQ dispatch table
│   ├── paging.c          # Two-level page table, identity maps 4 MB
│   ├── timer.c           # PIT 8253 driver — 100 Hz tick counter
│   ├── keyboard.c        # PS/2 scancode set 1 → ASCII driver
│   ├── kmalloc.c         # Kernel memory allocator (free-list with canaries)
│   └── vga.c             # VGA text terminal (colors, scroll, printf)
│
├── lib/                  # Library directory (reserved for future use)
├── arch/                 # Architecture-specific code (reserved)
├── fs/                   # Filesystem (reserved for future use)
└── README.md
```

## Concepts covered

### Logging System — `log.h` / `log.c`
The kernel now includes a simple logging system with four levels:
- **ERROR** (red)
- **WARN**  (yellow)
- **INFO**  (cyan)
- **DEBUG** (green)

Logging is initialized early in `kernel_main()` so all subsystems can use it. Example output:
```
[INFO] Logging system initialized
[INFO] Initializing GDT
[INFO] GDT initialized with 5 entries
[INFO] Initializing paging
[INFO] Paging initialized (first 4 MB identity-mapped)
```

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

At 100 Hz, IRQ0 fires every 10 ms and increments a global tick counter. The timer interrupt also triggers the process scheduler.

### PS/2 Keyboard — `keyboard.c`
IRQ1 fires when a key is pressed or released. The scancode byte sits in I/O port `0x60`. Bit 7 distinguishes presses (0) from releases (1). We map the 58 most common scancodes to ASCII using a lookup table and echo each character to the VGA terminal.

### Kernel Memory Allocator — `kmalloc.c`
A free-list allocator with header/footer canaries to detect overflows and double-frees. Features:

- First-fit allocation
- Block splitting and coalescing
- Magic canaries (MAGIC_FREE/MAGIC_USED) for integrity checking
- Allocation statistics and error checking

### Process Management — `process.h` / `process.c` + `process.S`
The kernel now supports basic process management:

- **Process Control Block (PCB)**: Stores CPU state, PID, stacks, and page directory
- **Round-robin scheduler**: Triggered by timer interrupt (IRQ0)
- **Context switching**: Assembly helpers in `process.S` save/restore CPU state
- **Process creation**: `proc_create()` sets up kernel/user stacks and initial context
- **Yielding**: Processes can voluntarily yield with `sys_yield()`

### System Calls — `syscall.h` / `syscall.c`
System calls are invoked via `int $0x80` (vector 0x80 in IDT):

| Syscall  | Number | Description                  |
|----------|--------|------------------------------|
| sys_write| 1      | Write to file descriptor     |
| sys_exit | 2      | Exit process                 |
| sys_getpid| 3     | Get process ID               |
| sys_yield| 4      | Yield processor              |

Currently only `fd=1` (stdout) is supported for `sys_write`, which outputs to the VGA terminal.

### Test Processes
Two simple test processes are created during initialization:
- Process 1: Prints "Process 1 running..." and yields
- Process 2: Prints "Process 2 running..." and yields

They demonstrate context switching and round-robin scheduling.

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
i686-elf-as process.S  -o process.o

# Compile C (no stdlib, freestanding environment)
i686-elf-gcc -m32 -std=gnu99 -ffreestanding -O2 -Wall -c \
    gdt.c idt.c irq.c paging.c timer.c keyboard.c vga.c kmalloc.c \
    kernel/kernel.c kernel/syscall.c drivers/process.c

# Link — linker.ld must place .multiboot at the start
i686-elf-ld -m elf_i386 -T linker.ld -o kernel.bin \
    boot.o helpers.o isr.o process.o \
    gdt.o idt.o irq.o paging.o timer.o keyboard.o vga.o kmalloc.o \
    kernel/kernel.o kernel/syscall.o drivers/process.o
```

### Run

```bash
qemu-system-i386 -kernel kernel.bin
```

Expected output (with colors):

```
      Tiny OS
      Minimal x86 Kernel
  ────────────────────────────────────────
[INFO] Logging system initialized
[INFO] Initializing GDT
[INFO] GDT initialized with 5 entries
[INFO] Initializing paging
[INFO] Paging initialized (first 4 MB identity-mapped)
[INFO] Initializing IDT
[INFO] IDT initialized with 256 entries
[INFO] Initializing IRQ controller
[INFO] IRQ controller initialized
[INFO] Initializing timer at 100 Hz
[INFO] Timer initialized (divisor: 11931)
[INFO] Initializing keyboard
[INFO] Keyboard initialized
[INFO] Initializing heap (start: 0x200000, end: 0x400000)
[INFO] Heap initialized with 2097152 bytes available
[INFO] Process manager initialized
[INFO] Syscall handler registered
[INFO] Creating test processes
[INFO] Process 1 created (state: READY)
[INFO] Process 2 created (state: READY)
  ────────────────────────────────────────

   All subsystems online. Interrupts enabled.
   Type on your keyboard — characters will echo here.
   Timer ticks are running silently in the background.

   > Process 1 running...
   > Process 2 running...
   > Process 1 running...
   > Process 2 running...
   [...]
```

## Design notes

**Why logging first?** Initializing logging early allows us to trace the boot process and debug issues in subsequent subsystems.

**Why process management after memory?** Processes need memory for their stacks and PCB structures, so we initialize the heap first.

**Why syscalls via INT 0x80?** This is the traditional Linux syscall interface and works well with our existing IDT setup.

**Why round-robin scheduling?** It's simple to implement and demonstrates the core concepts of context switching and process management.

**Why test processes?** They provide an immediate visual demonstration of multitasking working.

## Extending the kernel

Some natural next steps:

- **Enhanced syscalls**: Add `sys_fork`, `sys_exec`, `sys_read`, etc.
- **ELF loader**: Load and execute user programs from disk
- **Improved scheduler**: Implement priority-based scheduling or multilevel feedback queues
- **Memory protection**: Give each process its own page directory and implement copy-on-write
- **File system**: Implement a simple RAM disk or FAT12 driver
- **Shell**: Create a command-line interface that runs user programs
- **Networking**: Add a basic network driver (e.g., NE2000 PCI)
- **Graphics**: Support VGA graphics mode or USB input devices

## License

Released for educational use. Fork, read, break things, learn.