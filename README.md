# TinyOS

[![CI](https://github.com/kill74/TinyOS/actions/workflows/ci.yml/badge.svg)](https://github.com/kill74/TinyOS/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A clean, educational, 32-bit x86 bare-metal kernel written in C and assembly.  
TinyOS demonstrates core operating‑system concepts—bootstrapping, memory management, interrupt handling, process scheduling, syscalls, and a transition to user mode—while remaining small enough to read and modify in a single sitting.

---

## Features

- **Fully freestanding** – no libc, no host runtime.
- **Multiboot‑compatible** entry (`boot.S`) loads via QEMU, Bochs, or real hardware.
- **CPU setup** – GDT (kernel/user segments), IDT (exceptions & IRQs), PIC remap.
- **Memory management** – identity‑mapped kernel, per‑process page directories, two‑level paging, kernel heap (first‑fit with coalescing and canaries).
- **Interrupts** – programmable interval timer (100 Hz) and PS/2 keyboard driver with ring buffer.
- **Process management** – cooperative round‑robin scheduler, process creation, destruction, sleep/wakeup, per‑process stack and page directory.
- **System calls** – `int 0x80` interface with `write`, `exit`, `getpid`, `yield`, `sleep`, `setlog`.
- **User‑mode transition** – `iret`‑based ring‑3 switch, dedicated user memory region, tiny user‑space runtime (`u_printf`, etc.).
- **Developer shell** – interactive prompt with colored prompt, command history (via backspace), process inspection, memory stats, log level control, and test‑task spawning.
- **Build system** – simple Makefile, works with host GCC or a `i686-elf-*` cross‑toolchain.
- **Continuous integration** – GitHub Actions validates build and runs a QEMU smoke test on every push.

---

## Getting Started

### Prerequisites

- GNU Make
- NASM (or any x86 assembler; we use GNU `as` via the Makefile)
- GCC cross‑compiler for i686‑elf **or** host `gcc` with `-m32`
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
# Clone the repo
git clone https://github.com/kill74/TinyOS.git
cd TinyOS

# Build (uses cross‑compiler if present, otherwise host gcc -m32)
make

# Run in QEMU
make run
```

You should see the TinyOS boot banner, a developer shell prompt (`> `), and the user‑mode “Hello from user mode!” program run automatically.

### Common Commands in the Shell
```
help        – show command list
echo <text> – print text
start1/start2 – spawn test tasks
ps          – list processes
clear       – clear screen
log <lvl>   – set log level (none|error|warn|info|debug)
kill <pid>  – kill a process by PID
ticks       – display timer ticks
exit        – yield the shell (returns to scheduler)
```

---

## Project Layout

```
.
├── boot.S           # Multiboot header & entry point
├── helpers.S        # I/O port helpers (outb/inb)
├── isr.S            # ISR stubs & fault handlers
├── process.S        # Context switch, yield, switch_to_user (asm)
├── process.c        # Process management, scheduling, paging setup
├── kernel/
│   ├── kernel.c     # Main kernel entry & subsystem init
│   ├── syscall.c    # Syscall dispatcher & implementations
│   └── switch_to_user.c  # (placeholder for future user‑mode helpers)
├── drivers/
│   ├── gdt.c        # Global Descriptor Table
│   ├── idt.c        # Interrupt Descriptor Table
│   ├── irq.c        # PIC remap & IRQ handling
│   ├── keyboard.c   # PS/2 scancode → ASCII + ring buffer
│   ├── paging.c     # Physical/virtual memory management
│   ├── timer.c      # PIT 8253/8254 at 100 Hz
│   ├── vga.c        # Text‑mode VGA driver (80×25)
│   ├── kmalloc.c    # Kernel heap allocator with canaries
│   └── log.c        # Simple kernel logger (levels, printf)
├── include/         # Public headers (vga.h, paging.h, syscall.h, …)
├── user/
│   ├── lib.h/c      # Tiny user‑space runtime (u_printf, etc.)
│   ├── crt0.S       # Entry point for user programs
│   ├── program.c    # Demo user program
│   └── ld.script    # Linker script for flat binary at 0x0
├── fs/              # (future) filesystem stubs
├── docs/            # Documentation, diagrams, media
├── .github/         # CI workflows, issue/PR templates
├── Makefile
└── linker.ld        # Linker script (places kernel at 1 MiB)
```

---

## Roadmap

| Version | Target |
|---------|--------|
| **v0.2.0** | Solidify user‑mode transition, add basic ELF loader, expand syscall set (`open`, `read`, `close`). |
| **v0.3.0** | Implement a minimal VFS layer (in‑memory `tmpfs`), add `fork`/`exec`‑style process loading. |
| **v0.4.0** | Priority‑based scheduler, preemptive multitasking via timer IRQ, add support for SMP boot (AP init). |
| **v0.5.0** | Dynamic user heap, shared libraries, rudimentary file‑system (FAT12), and a tiny POSIX‑like shell. |

Each version is accompanied by a changelog entry and tag.

---

## Known Limitations

- The scheduler is cooperative; a CPU‑intensive user task will stall the system until it yields (by design, but preemptive tick‑based scheduling is planned).
- Memory protection relies on paging; writes to kernel‑only addresses from user mode trigger a page fault (logged) but do not yet trigger a proper kernel panic or signal.
- Only a single CPU core is supported; secondary cores are left in the halted state.
- Persistent storage and filesystems are stubs; `sys_open`/`sys_read`/`sys_write` currently only handle stdout/stderr and the in‑memory user region.
- The VGA driver does not support hardware scrolling or Unicode; it is a classic 8×16 font text mode.

---

## Contributing

We welcome contributions! Please follow these steps:

1. Fork the repository.
2. Create a feature branch (`git checkout -b feat/awesome-feature`).
3. Make your changes, ensuring you follow the existing code style.
4. Add or update tests (if applicable).
5. Run `make` and `make run` to verify nothing is broken.
6. Commit with a clear message (`git commit -m "feat: add xyz"`).
7. Push to your fork and open a Pull Request.

Please read [`CONTRIBUTING.md`](CONTRIBUTING.md) for detailed guidelines, and use the issue and pull‑request templates provided in `.github/`.

---

## License

TinyOS is licensed under the MIT License – see the [`LICENSE`](LICENSE) file for details.

---

*Happy hacking!*  
— The TinyOS maintainers
