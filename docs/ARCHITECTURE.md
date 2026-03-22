# TinyOS Architecture

## Boot flow

```text
+-----------------+
| Bootloader      |
+-----------------+
         |
         v
+-----------------+      +---------------------+
| boot.S          | ---> | kernel_main()       |
+-----------------+      +---------------------+
                                |
                                v
+-----------------+      +---------------------+
| GDT             | ---> | Segmentation setup  |
+-----------------+      +---------------------+
                                |
                                v
+-----------------+      +---------------------+
| Paging          | ---> | MMU enabled         |
+-----------------+      +---------------------+
                                |
                                v
+-----------------+      +---------------------+
| IDT + ISR stubs | ---> | Exception/IRQ entry |
+-----------------+      +---------------------+
                                |
                                v
+-----------------+      +---------------------+
| PIC + PIT + KBD | ---> | Hardware interrupts |
+-----------------+      +---------------------+
                                |
                                v
+-----------------+      +---------------------+
| Process manager | ---> | Round-robin tasks   |
+-----------------+      +---------------------+
```

## Runtime layers

1. Hardware and CPU control (`helpers.S`, `boot.S`)
2. Core architecture setup (`drivers/gdt.c`, `drivers/idt.c`, `drivers/paging.c`)
3. Interrupt and device handling (`drivers/irq.c`, `drivers/timer.c`, `drivers/keyboard.c`)
4. Kernel runtime (`drivers/kmalloc.c`, `drivers/log.c`, `kernel/syscall.c`, `process.c`)
5. Entry/orchestration (`kernel/kernel.c`)

## Notes

- The current scheduler is intentionally simple and should be treated as a stepping stone.
- User mode and process isolation are planned roadmap items.
