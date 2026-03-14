/* idt.c — Interrupt Descriptor Table (IDT)
 *
 * The IDT maps each of the 256 possible interrupt vectors to a handler address.
 * When the CPU takes interrupt N it:
 *   1. Saves EIP, CS, EFLAGS (and maybe an error code) onto the stack.
 *   2. Loads the handler address from IDT entry N.
 *   3. Jumps to that address.
 *
 * The actual handler addresses point to tiny stubs in isr.S, which push a
 * uniform register frame and call the C functions below.
 */

#include "../include/idt.h"
#include "../include/log.h"
#include "../include/vga.h"
#include <stdint.h>

/* ── IDT structures ───────────────────────────────────────────────────────── */
struct idt_entry {
    uint16_t base_low;   /* Handler address bits 15:0  */
    uint16_t sel;        /* Kernel code segment selector (0x08) */
    uint8_t  always0;    /* Must be 0 */
    uint8_t  flags;      /* Gate type + DPL + Present bit */
    uint16_t base_high;  /* Handler address bits 31:16 */
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

/* ── ISR stub declarations (defined in isr.S) ────────────────────────────── */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

extern void idt_load(uint32_t addr);

/* ── Handler dispatch table ───────────────────────────────────────────────── */
/* Any C function can register itself here to handle a specific interrupt. */
static isr_t isr_handlers[256];

void isr_register_handler(uint8_t num, isr_t handler) {
    isr_handlers[num] = handler;
}

/* ── Human-readable exception names ──────────────────────────────────────── */
static const char *exception_names[] = {
    "Division Error",          "Debug",
    "Non-Maskable Interrupt",  "Breakpoint",
    "Overflow",                "Bound Range Exceeded",
    "Invalid Opcode",          "Device Not Available",
    "Double Fault",            "Coprocessor Segment Overrun",
    "Invalid TSS",             "Segment Not Present",
    "Stack Fault",             "General Protection Fault",
    "Page Fault",              "Reserved",
    "x87 FPU Error",           "Alignment Check",
    "Machine Check",           "SIMD Exception",
};
#define N_NAMED_EXCEPTIONS 20

/* ── C-level exception dispatcher ────────────────────────────────────────── */
/* Called by isr_common in isr.S with a pointer to the saved register frame. */
void isr_handler(registers_t *regs) {
    /* Defensive bounds check before indexing the handler table. */
    if (regs->int_no >= 256) {
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_printf("\n  *** BOGUS INTERRUPT %u — HALTING ***\n", regs->int_no);
        __asm__ __volatile__("cli; hlt");
        return;
    }

    if (isr_handlers[regs->int_no]) {
        /* A driver registered a specific handler — call it. */
        isr_handlers[regs->int_no](regs);
        return;
    }

    /* Default: print a panic message and halt. */
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_puts("\n\n  *** KERNEL EXCEPTION ***\n");

    const char *name = (regs->int_no < N_NAMED_EXCEPTIONS)
                       ? exception_names[regs->int_no]
                       : "Unknown";

    vga_printf("  Exception %u: %s\n", regs->int_no, name);
    vga_printf("  Error code : 0x%x\n", regs->err_code);
    vga_printf("  EIP=0x%x  CS=0x%x  EFLAGS=0x%x\n",
               regs->eip, regs->cs, regs->eflags);
    vga_printf("  EAX=0x%x  EBX=0x%x  ECX=0x%x  EDX=0x%x\n",
               regs->eax, regs->ebx, regs->ecx, regs->edx);

    /* Halt — there's no safe way to continue after an unhandled exception. */
    __asm__ __volatile__("cli; hlt");
}

/* ── Internal: set one IDT gate ───────────────────────────────────────────── */
static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = (uint16_t)(base & 0xFFFF);
    idt[num].base_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].sel       = sel;
    idt[num].always0   = 0;
    /* OR with 0x60 to allow ring-3 software to trigger this via `int N`.
     * For pure kernel gates keep flags as-is (e.g., 0x8E). */
    idt[num].flags     = flags;
}

/* ── Public: initialise the IDT ──────────────────────────────────────────── */
void init_idt(void) {
    LOG_INFO("Initializing IDT");
    idtp.limit = (uint16_t)(sizeof(struct idt_entry) * 256 - 1);
    idtp.base  = (uint32_t)&idt;

    /* Zero the entire table first */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* Install CPU exception handlers (vectors 0–31)
     * flags = 0x8E → Present=1, DPL=0, 32-bit interrupt gate */
    idt_set_gate( 0, (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate( 1, (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate( 2, (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate( 3, (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate( 4, (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate( 5, (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate( 6, (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate( 7, (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate( 8, (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate( 9, (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    /* Hardware IRQ stubs (vectors 32–47) — installed by init_irq() in irq.c */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    idt_load((uint32_t)&idtp);
    LOG_INFO("IDT initialized with 256 entries");
}
