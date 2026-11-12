/* cpu.h -- access to control registers and a few special instructions. */
#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdint.h>

#define CR0_WP 0x00010000       /* kernel writes respect read-only pages */
#define CR0_PG 0x80000000       /* paging enabled */

#define EFLAGS_CF 0x00000001    /* carry: syscall failed (abi/syscall.h) */
#define EFLAGS_IF 0x00000200    /* interrupts enabled */

static inline uint32_t read_cr0(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline void write_cr0(uint32_t v)
{
    __asm__ volatile ("mov %0, %%cr0" : : "r"(v) : "memory");
}

/* CR2 = the address that caused the last page fault */
static inline uint32_t read_cr2(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

/* CR3 = PHYSICAL address of the current page directory */
static inline uint32_t read_cr3(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint32_t pa)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pa) : "memory");
}

/* Drop the TLB's cached translation for one virtual address. */
static inline void invlpg(uint32_t va)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}

static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }
static inline void hlt(void) { __asm__ volatile ("hlt"); }

#endif
