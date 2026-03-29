# TinyOS

[![CI](https://github.com/kill74/TinyOS/actions/workflows/ci.yml/badge.svg)](https://github.com/kill74/TinyOS/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A clean, educational, 32-bit x86 bare-metal kernel written in C and assembly.
TinyOS demonstrates core operating-system concepts — bootstrapping, memory
management, interrupt handling, process scheduling, syscalls, and user-mode
execution — while remaining small enough to read and modify in a single sitting.

---

## Features

### Boot & CPU Setup
- **Multiboot-compatible** entry (`boot.S`) — loads via GRUB, QEMU, or real hardware.
- **GDT** — flat model with 6 descriptors: null, kernel code/data, user code/data, TSS.
- **TSS** — Task State Segment for ring-3 → ring-0 stack switching on interrupts.
- **IDT** — 32 CPU exception gates, 16 IRQ gates, syscall trap (vector `0x80`, DPL=3).
- **PIC** — 8259 remap (IRQ0–15 → vectors 32–47).

### Memory Management
- **Two-level paging** — identity-mapped kernel, per-process page directories.
- **`map_page()`** — dynamic single-page mapping with TLB flush.
- **Kernel heap** — first-fit free-list allocator with coalescing and header/footer canaries (`0xDEADBEEF` / `0xCAFEBABE`).

### Interrupts & Drivers
- **PIT timer** — 8253/8254 at 100 Hz with tick counter.
- **PS/2 keyboard** — scancode set 1 → ASCII, ring buffer, backspace handling.
- **VGA text mode** — 80×25, hardware cursor, scrolling, minimal printf (`%s %d %u %x %%`).
- **PS/2 mouse** — movement + button events (used by GUI).

### Process Management
- **Preemptive round-robin scheduler** — timer-interrupt-based time-slicing (10 ms quantum).
- **Process states** — UNUSED, RUNNING, READY, BLOCKED, SLEEPING, ZOMBIE, WAITING.
- **Operations** — `proc_create`, `proc_kill`, `proc_fork`, `proc_exec`, `proc_sbrk`, `proc_wait`, `proc_sleep_ticks`.
- **Per-process isolation** — separate kernel stack, page directory, user heap.

### System Calls (`int 0x80`)
| # | Syscall | Description |
|---|---------|-------------|
| 1 | `write` | Write to stdout |
| 2 | `exit` | Terminate process |
| 3 | `getpid` | Get process ID |
| 4 | `yield` | Yield CPU |
| 5 | `sleep` | Sleep for N ticks |
| 6 | `read` | (defined, pending) |
| 7 | `setlog` | Set kernel log level |
| 8 | `fork` | Fork current process |
| 9 | `exec` | Replace process image |
| 10 | `sbrk` | Adjust user heap |
| 11 | `wait` | Wait for child |
| 12 | `execelf` | Load & run ELF binary |

### ELF Loader
- Validates 32-bit x86 ELF headers (magic, class, encoding, machine).
- Loads `PT_LOAD` segments, allocates physical pages, maps them via `map_page()`.
- Zero-fills BSS (`.bss` where `memsz > filesz`).
- Integrated with `sys_execelf` syscall.

### User Mode
- **Ring-3 transition** via crafted IRET frame (`enter_usermode` in assembly).
- **Per-process page directories** with kernel-only and user-accessible regions.
- **User runtime** — `u_printf`, `u_putchar`, `u_puts` (uses `int 0x80`).
- **CRT0 entry** — calls `main()` then `sys_exit()`.

### Network Stack
- **RTL8139 NIC** driver with DMA.
- **Ethernet** frame handling, **ARP** table with aging.
- **IPv4** with checksum, **TCP** full state machine (handshake, data, teardown), **UDP**.
- **BSD socket API** — `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`.

### Filesystem
- **TinyFS** — in-memory inode-based filesystem (64 inodes, 512 blocks).
- Operations: create, read, write, delete, truncate, seek, directory listing.
- Shell commands: `ls`, `touch`, `rm`, `cat`, `write`, `fsinfo`.

### Graphical Desktop (VGA Mode 13h)
- 320×200, 256 colours with mouse support.
- **Window manager** — draggable windows, close buttons, taskbar.
- **Apps** — Terminal (embedded shell), Clock, Paint, About dialog.

### Developer Shell
- Interactive command prompt with coloured output.
- Commands: `help`, `echo`, `ps`, `kill`, `ticks`, `clear`, `log`, `kmem`, `kmem_check`, `start1`, `start2`, `fork`, `sbrk`, `net`, `arp`, `tcp`, `listen`, `connect`, filesystem commands, `exit`.

### Kernel Panic
- **Full diagnostic screen** on unhandled exceptions or explicit `kernel_panic()`.
- Shows: exception name, error code, register dump, page-fault details (CR2, cause), stack trace (up to 16 frames), current process info.
- Distinctive red-on-white screen, halts the CPU.

### Quality
- **Freestanding C library** — `memset`, `memcpy`, `strcmp`, `strlen`, `sprintf`, `atoi`, etc.
- **Test suite** — string functions, stdlib, printf, filesystem, packet buffers, memory stress, TCP.
- **GitHub Actions CI** — build verification + QEMU 3-second smoke test.
- **`.clang-format`** — consistent code style across the project.

---

## Getting Started

### Prerequisites

- GNU Make
- GCC cross-compiler for `i686-elf` **or** host `gcc` with `-m32`
- QEMU (any recent version)

#### On Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y make gcc nasm binutils qemu-system-x86
```

#### On macOS (with Homebrew)
```bash
brew install make nasm binutils qemu
```

### Build & Run

```bash
git clone https://github.com/kill74/TinyOS.git
cd TinyOS
make
make run
```

You should see the TinyOS boot banner, initialization messages, and a
developer shell prompt (`> `).

### Shell Commands

```
help              show all commands
echo <text>       print text
ps                list processes
kill <pid>        kill process by PID
clear             clear screen
log <level>       set log level (none|error|warn|info|debug)
ticks             show timer tick count
kmem              heap usage summary
kmem_check        verify heap canaries
start1/start2     spawn test tasks
fork              test fork() syscall
sbrk              test sbrk() user heap
net               NIC status
arp               ARP table
tcp               TCP connections
ls/touch/rm/cat/write/fsinfo  filesystem commands
exit              yield shell back to scheduler
```

---

## Project Layout

```
.
├── boot.S              # Multiboot header & entry point
├── helpers.S           # I/O port helpers (outb/inb/ltg/lgdt/load_cr3)
├── isr.S               # ISR stubs & fault handlers (32 exceptions + 16 IRQs + syscall)
├── process_asm.S       # Context switch, enter_usermode
├── process.c           # Process management, preemptive scheduler, fork/exec/wait
├── shell.c             # Interactive developer shell
├── userprog_run.c      # In-kernel bytecode interpreter
├── userprog_blob.c     # Demo bytecode program
├── linker.ld           # Linker script (kernel at 1 MiB)
├── Makefile            # Build system
│
├── kernel/
│   ├── kernel.c        # Main entry, subsystem init order, boot banner
│   ├── syscall.c       # int 0x80 dispatcher & implementations
│   └── switch_to_user.c # Ring-3 transition (enter_usermode wrapper)
│
├── drivers/
│   ├── gdt.c           # Global Descriptor Table (6 entries + TSS)
│   ├── idt.c           # Interrupt Descriptor Table + exception dispatch
│   ├── irq.c           # 8259 PIC remap & IRQ handling
│   ├── keyboard.c      # PS/2 scancode → ASCII + ring buffer
│   ├── paging.c        # Two-level page tables, map_page(), TLB flush
│   ├── timer.c         # PIT 8253/8254 at 100 Hz, preemption hook
│   ├── tss.c           # Task State Segment (ring-3 → ring-0 stack)
│   ├── vga.c           # Text-mode VGA driver (80×25)
│   ├── kmalloc.c       # Free-list heap allocator with canaries
│   ├── log.c           # Kernel logger (levels, colours, printf)
│   ├── panic.c         # Kernel panic handler (register dump, stack trace)
│   └── elf.c           # ELF binary loader (32-bit x86, PT_LOAD)
│
├── include/            # Public headers
│   ├── gdt.h, idt.h, irq.h, keyboard.h, kmalloc.h
│   ├── log.h, paging.h, panic.h, process.h, syscall.h
│   ├── timer.h, tss.h, vga.h, elf.h, userprog.h
│
├── net/                # Network stack
│   ├── rtl8139.c/h     # NIC driver (Realtek 8139)
│   ├── ethernet.c/h    # Ethernet frame handling
│   ├── arp.c/h         # ARP protocol + table aging
│   ├── ip.c/h          # IPv4 packet handling
│   ├── tcp.c/h         # TCP state machine
│   ├── udp.c/h         # UDP protocol
│   ├── socket.c/h      # BSD socket API
│   └── packet.c/h      # Packet buffer pool
│
├── gui/                # Graphical interface (VGA Mode 13h)
│   ├── graphics.c/h    # 320×200, 256-colour framebuffer
│   ├── mouse.c/h       # PS/2 mouse driver
│   ├── font.c/h        # 8×16 bitmap font
│   └── gui.c/h         # Window manager, taskbar, demo apps
│
├── fs/                 # Filesystem
│   └── fs.c/h          # TinyFS (in-memory, inode-based)
│
├── libc/               # Freestanding C library
│   ├── string.c/h      # memset, memcpy, strcmp, strlen, …
│   ├── stdlib.c/h      # atoi, itoa, malloc, free, …
│   └── stdio.c/h       # sprintf, snprintf, printf
│
├── user/               # User-space programs
│   ├── crt0.S          # Entry point (calls main, then sys_exit)
│   ├── lib.c/h         # User runtime (u_printf, sys_* wrappers)
│   ├── program.c       # Demo "Hello from user mode!" program
│   └── ld.script       # User program linker script
│
├── tests/              # Test suite
│   └── tests.c/h       # libc, FS, packet, memory, TCP tests
│
├── docs/               # Documentation
├── .github/            # CI workflows, issue/PR templates
├── .clang-format       # Code style config
├── CHANGELOG.md
├── CONTRIBUTING.md
└── README.md
```

---

## Architecture

### Boot Sequence

```
GRUB/QEMU → boot.S → kernel_main()
  ├─ VGA init
  ├─ Log init
  ├─ GDT (6 descriptors) + TSS
  ├─ Paging (identity-map first 4 MB)
  ├─ IDT (32 exceptions + 16 IRQs + syscall)
  ├─ PIC remap
  ├─ PIT timer @ 100 Hz
  ├─ PS/2 keyboard
  ├─ Heap allocator
  ├─ Process manager
  ├─ Network stack
  ├─ Filesystem
  ├─ Test suite
  ├─ Create processes (shell, test tasks, userprog)
  ├─ Enable interrupts
  ├─ GUI init (VGA Mode 13h)
  └─ gui_run() (desktop loop)
```

### Preemptive Scheduling

```
Timer IRQ (100 Hz)
  → irq_handler()
    → timer_callback()
      → proc_tick()       # wake sleeping processes
      → preempt()
        → schedule()       # round-robin context switch
          → context_switch()  # save/restore ESP, cli/popfl
```

### User-Mode Execution

```
proc_create(entry, pid, PROC_MODE_USER)
  → allocates user stack + page directory
  → builds IRET frame: CS=0x1B, SS=0x2B, EIP=entry

context_switch → iret → user code runs at ring 3
  → int $0x80 (or hardware IRQ)
    → CPU switches to TSS.ESP0 (kernel stack)
    → ISR stub saves frame, calls C handler
    → handler runs, returns
    → iret → back to ring 3
```

### ELF Loading

```
sys_execelf(data, size)
  → elf_load(data, size, &entry)
    → validates ELF header
    → for each PT_LOAD segment:
        → kmalloc physical pages
        → map_page(vaddr, phys, flags)
        → memcpy file data, zero-fill BSS
  → proc_exec(entry)
```

---

## Roadmap

| Version | Target | Status |
|---------|--------|--------|
| **v0.1.0** | Basic kernel: GDT, IDT, paging, timer, keyboard, VGA, heap, process, shell | Done |
| **v0.2.0** | User-mode transition, ELF loader, preemptive scheduler, TSS | Done |
| **v0.3.0** | VFS layer, `sys_read`/`sys_open`/`sys_close`, file descriptors | Planned |
| **v0.4.0** | Priority scheduler, signals, pipes, shared memory | Planned |
| **v0.5.0** | FAT12 support, DHCP/DNS, shell scripting, SMP boot | Planned |

---

## Known Limitations

- Only a single CPU core is supported; secondary cores are halted.
- `sys_read` is defined but not yet wired to the keyboard buffer.
- No `sys_open`/`sys_close` — stdout/stderr only.
- The filesystem is RAM-backed (no block device).
- The VGA text driver does not support Unicode.
- Network stack lacks DHCP, DNS, and higher-level protocols.

---

## Contributing

1. Fork the repository.
2. Create a feature branch (`git checkout -b feat/awesome-feature`).
3. Follow the existing code style (enforced by `.clang-format`).
4. Add or update tests if applicable.
5. Run `make` and `make run` to verify.
6. Commit with a clear message (`git commit -m "feat: add xyz"`).
7. Push and open a Pull Request.

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for details.

---

## License

MIT License — see [`LICENSE`](LICENSE) for details.

---

*Happy hacking!*
