# Contributing to TinyOS

Thanks for your interest in contributing.

## Development setup

1. Install prerequisites:
   - `make`
   - `gcc` with 32-bit support (`gcc-multilib` on Debian/Ubuntu)
   - `binutils`
   - `qemu-system-i386`
2. Build the kernel:
   - `make CROSS=`
3. Run in QEMU:
   - `make CROSS= run`

If you use a cross toolchain (`i686-elf-*`), run with default `make`.

## Commit style

Use focused commits with clear messages:

- `feat(timer): add periodic sleep queue`
- `fix(irq): acknowledge slave PIC before master`
- `docs(readme): refresh roadmap`

## Pull requests

1. Open an issue first for larger changes.
2. Keep PRs small and reviewable.
3. Include test or demo steps in the PR description.
4. Update docs and `CHANGELOG.md` when behavior changes.

## Coding guidelines

- Prefer small, explicit functions in low-level code.
- Keep side effects obvious in interrupt and scheduler paths.
- Avoid introducing host OS dependencies.
- Comment only where logic is non-obvious.

## Reporting bugs

Use the bug template and include:

- exact build/run commands
- expected vs actual behavior
- environment details (toolchain + QEMU)
