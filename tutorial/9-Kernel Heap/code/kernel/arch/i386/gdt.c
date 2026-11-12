/* gdt.c -- our own Global Descriptor Table.
 *
 * We use "flat" segments: every segment starts at 0 and covers 4 GiB, so
 * segmentation does nothing to addresses. Segments still matter for ONE
 * thing: the privilege level. Code running with CS = a DPL-3 segment is in
 * user mode (ring 3); CS = a DPL-0 segment is kernel mode (ring 0).
 */
#include <stdint.h>
#include "arch/i386/gdt.h"

struct gdt_entry {
    uint16_t limit_low;         /* limit bits 0..15                     */
    uint16_t base_low;          /* base bits 0..15                      */
    uint8_t  base_mid;          /* base bits 16..23                     */
    uint8_t  access;            /* present, DPL, type (see table below) */
    uint8_t  limit_high_flags;  /* limit bits 16..19 | flags << 4       */
    uint8_t  base_high;         /* base bits 24..31                     */
} __attribute__((packed));

/* The operand of LGDT: size-1 and linear address of the table. */
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Access byte: P=1 | DPL (2 bits) | S=1 (code/data) | type
 *   0x9A = 1001 1010: present, ring 0, code, readable
 *   0x92 = 1001 0010: present, ring 0, data, writable
 *   0xFA = 1111 1010: present, ring 3, code, readable
 *   0xF2 = 1111 0010: present, ring 3, data, writable */
#define ACCESS_KCODE 0x9A
#define ACCESS_KDATA 0x92
#define ACCESS_UCODE 0xFA
#define ACCESS_UDATA 0xF2

/* Flags nibble: G=1 (limit counts 4 KiB pages), D=1 (32-bit segment) */
#define FLAGS_32BIT_4K 0xC

static struct gdt_entry gdt[5];
static struct gdt_ptr   gdtr;

void gdt_load(struct gdt_ptr *p);  /* gdt_load.asm */

static void set_entry(int i, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags)
{
    gdt[i].limit_low        = limit & 0xFFFF;
    gdt[i].base_low         = base & 0xFFFF;
    gdt[i].base_mid         = (base >> 16) & 0xFF;
    gdt[i].access           = access;
    gdt[i].limit_high_flags = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].base_high        = (base >> 24) & 0xFF;
}

void gdt_init(void)
{
    set_entry(0, 0, 0, 0, 0);                                   /* null */
    set_entry(1, 0, 0xFFFFF, ACCESS_KCODE, FLAGS_32BIT_4K);     /* 0x08 */
    set_entry(2, 0, 0xFFFFF, ACCESS_KDATA, FLAGS_32BIT_4K);     /* 0x10 */
    set_entry(3, 0, 0xFFFFF, ACCESS_UCODE, FLAGS_32BIT_4K);     /* 0x18 */
    set_entry(4, 0, 0xFFFFF, ACCESS_UDATA, FLAGS_32BIT_4K);     /* 0x20 */

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint32_t)gdt;
    gdt_load(&gdtr);
}
