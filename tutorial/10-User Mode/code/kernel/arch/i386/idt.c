/* idt.c -- the Interrupt Descriptor Table: vector number -> handler address.
 *
 * Every vector points at its stub in isr.asm. All stubs end up in trap(),
 * so the IDT never changes after boot except for the DPL of vector 0x80.
 */
#include "arch/i386/idt.h"
#include "arch/i386/gdt.h"

struct idt_gate {
    uint16_t offset_low;    /* handler address bits 0..15                 */
    uint16_t selector;      /* code segment to run the handler in         */
    uint8_t  zero;
    uint8_t  type_attr;     /* P | DPL (2 bits) | 0 | gate type (4 bits)  */
    uint16_t offset_high;   /* handler address bits 16..31                */
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* P=1, type 0xE = 32-bit interrupt gate (the CPU clears IF on entry) */
#define GATE_INTERRUPT 0x8E

static struct idt_gate idt[256];
static struct idt_ptr  idtr;

extern uint32_t trap_stubs[256];   /* isr.asm */

void idt_set_gate(int vector, uint32_t handler, int dpl)
{
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].selector    = SEG_KCODE;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = GATE_INTERRUPT | (dpl << 5);
    idt[vector].offset_high = handler >> 16;
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, trap_stubs[i], 0);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint32_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
