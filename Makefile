CROSS ?= i686-elf-

AS = $(CROSS)as
CC = $(CROSS)gcc
LD = $(CROSS)ld

BUILD_DIR = build
KERNEL_BIN = kernel.bin

ASM_SOURCES = \
	boot.S \
	helpers.S \
	isr.S \
	process_asm.S

C_SOURCES = \
	drivers/gdt.c \
	drivers/idt.c \
	drivers/irq.c \
	drivers/keyboard.c \
	drivers/kmalloc.c \
	drivers/log.c \
	drivers/paging.c \
	drivers/timer.c \
	drivers/tss.c \
	drivers/vga.c \
	drivers/elf.c \
	drivers/panic.c \
	drivers/rtc.c \
	gui/graphics.c \
	gui/mouse.c \
	gui/font.c \
	gui/gui.c \
	fs/fs.c \
	libc/string.c \
	libc/stdlib.c \
	libc/stdio.c \
	tests/tests.c \
	kernel/kernel.c \
	kernel/syscall.c \
	kernel/switch_to_user.c \
	net/packet.c \
	net/rtl8139.c \
	net/ethernet.c \
	net/arp.c \
	net/ip.c \
	net/tcp.c \
	net/udp.c \
	net/socket.c \
	shell.c \
	process.c \
	userprog_run.c \
	userprog_blob.c

ASM_OBJECTS = $(ASM_SOURCES:%.S=$(BUILD_DIR)/%.o)
C_OBJECTS = $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

CFLAGS = -m32 -std=gnu99 -ffreestanding -fno-pic -fno-stack-protector -Wall -Wextra -Iinclude
ASFLAGS = --32
LDFLAGS = -m elf_i386 -T linker.ld

.PHONY: all clean run check-tools

all: check-tools $(KERNEL_BIN)

check-tools:
	@command -v $(AS) >/dev/null 2>&1 || (echo "[ERROR] Missing assembler: $(AS)" && exit 1)
	@command -v $(CC) >/dev/null 2>&1 || (echo "[ERROR] Missing compiler: $(CC)" && exit 1)
	@command -v $(LD) >/dev/null 2>&1 || (echo "[ERROR] Missing linker: $(LD)" && exit 1)

$(KERNEL_BIN): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	qemu-system-i386 -kernel $(KERNEL_BIN)

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_BIN)
