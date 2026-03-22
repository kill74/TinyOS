# TinyOS

[![CI](https://github.com/kill74/TinyOS/actions/workflows/ci.yml/badge.svg)](https://github.com/kill74/TinyOS/actions/workflows/ci.yml)

A bare-metal 32-bit x86 kernel written in C and Assembly.

TinyOS boots with no host OS support, initializes core CPU structures (GDT/IDT/paging), handles hardware interrupts, exposes a small syscall interface, and runs cooperative test processes with round-robin scheduling.

## Why this project stands out

- Freestanding kernel code (no libc, no host runtime)
- End-to-end boot chain from multiboot entry to multitasking demo
- Clear subsystem boundaries (memory, interrupts, drivers, scheduler)
- CI build + QEMU smoke boot for reproducibility

## Demo and proof

Add these assets to make your repository visually strong:

- Boot demo GIF: `docs/media/boot-demo.gif`
- Architecture diagram PNG: `docs/media/architecture.png`

Recommended capture command (Linux):

```bash
qemu-system-i386 -kernel kernel.bin
```

Then record 10-20 seconds showing:

1. Kernel startup log
2. Keyboard echo
3. Process 1 / Process 2 yielding output

## Current capabilities

| Area            | Status  | Notes                                                       |
| --------------- | ------- | ----------------------------------------------------------- |
| Boot            | Done    | Multiboot-compliant entry in `boot.S`                       |
| CPU setup       | Done    | GDT + IDT + interrupt stubs                                 |
| Memory          | Done    | Paging + kernel heap allocator                              |
| IRQs            | Done    | PIC remap + IRQ dispatch                                    |
| Timer           | Done    | PIT at configurable frequency                               |
| Keyboard        | Done    | PS/2 scancode handling                                      |
| Syscalls        | Done    | `int 0x80` with `write`, `exit`, `getpid`, `yield`, `sleep` |
| Multitasking    | Done    | Round-robin + timer-driven sleep/wakeup                     |
| Filesystem      | Planned | `fs/` reserved                                              |
| Userland loader | Planned | ELF/process image loading                                   |

## Architecture at a glance

```text
bootloader -> boot.S -> kernel_main()
					 -> init_gdt()
					 -> init_paging()
					 -> init_idt()
					 -> init_irq()
					 -> init_timer(100)
					 -> init_keyboard()
					 -> kmalloc_init()
					 -> init_processing()
					 -> register int 0x80 syscall handler
					 -> create test processes
					 -> idle loop (hlt)
```

## Project layout

```text
.
├── boot.S
├── helpers.S
├── isr.S
├── process.S
├── process.c
├── linker.ld
├── Makefile
├── drivers/
├── include/
├── kernel/
├── arch/
├── fs/
├── docs/
└── .github/
```

## Quick start (60 seconds)

### Option A: host compiler (Linux)

Install:

```bash
sudo apt-get update
sudo apt-get install -y make gcc-multilib binutils qemu-system-x86
```

Build and run:

```bash
make CROSS=
make CROSS= run
```

### Option B: cross compiler (`i686-elf-*`)

If your toolchain is prefixed (`i686-elf-gcc`, `i686-elf-ld`, etc.):

```bash
make
make run
```

## Build details

- Assembles: `boot.S`, `helpers.S`, `isr.S`, `process.S`
- Compiles: drivers + kernel + `process.c`
- Links with: `linker.ld` into `kernel.bin`

## CI

GitHub Actions workflow:

1. Installs toolchain dependencies
2. Builds kernel using `make CROSS=`
3. Verifies `_start` and `kernel_main` symbols exist
4. Runs a short QEMU smoke boot

See `.github/workflows/ci.yml`.

## Roadmap

### v0.2.0

- User mode transition (ring 3)
- Safer context switching and scheduler fixes

### v0.3.0

- Minimal VFS abstraction
- Initramfs read support
- `sys_read`/`sys_open` syscall groundwork

### v0.4.0

- ELF loader for user programs
- Tiny userspace shell

### v0.5.0

- Improved process states (sleep/block/wakeup) (partially implemented: sleep/wakeup)
- Priority scheduling experiments

## Known limitations

- Scheduler context switch path is intentionally minimal and needs hardening
- No user/kernel memory isolation yet
- No persistent filesystem or userspace binary loading
- Single-core assumptions throughout interrupt and process paths

## New in Unreleased: sleep/wakeup scheduling

TinyOS now supports timer-driven process sleeping:

- `sys_sleep(ticks)` blocks the current process for a tick interval.
- Timer IRQ (`IRQ0`) calls into process management each tick.
- Sleeping processes automatically transition back to `READY` when their wake tick is reached.

This is a stepping stone toward richer process states (`blocked`, `wakeup`, priorities) in future releases.

## Contributing

Contributions are welcome.

- Read `CONTRIBUTING.md`
- Use issue templates in `.github/ISSUE_TEMPLATE/`
- Follow PR checklist in `.github/pull_request_template.md`

## Release and changelog

- Changelog: `CHANGELOG.md`
- Release process: `docs/RELEASE_PROCESS.md`

## Suggested next showcase upgrades

To separate this project from other kernel repos on GitHub:

1. Add a short boot GIF at the top of this README.
2. Add architecture and memory layout diagrams.
3. Add one standout feature branch (ELF loader or user mode isolation) with a technical write-up in `docs/`.
