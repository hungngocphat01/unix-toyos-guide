/* elf.h -- the parts of the ELF format we need to load a program.
 * Spec: "System V ABI", or `man 5 elf` on Linux. */
#ifndef PROC_ELF_H
#define PROC_ELF_H

#include <stdint.h>

#define ELF_MAGIC 0x464C457F        /* "\x7FELF" read as a little-endian word */

/* File header: at offset 0 */
struct elf_header {
    uint32_t magic;
    uint8_t  class;                 /* 1 = 32-bit           */
    uint8_t  data;                  /* 1 = little-endian    */
    uint8_t  ident_rest[10];
    uint16_t type;                  /* 2 = executable       */
    uint16_t machine;               /* 3 = i386             */
    uint32_t version;
    uint32_t entry;                 /* address of _start    */
    uint32_t phoff;                 /* program headers: file offset ... */
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;             /* ... size of each ...  */
    uint16_t phnum;                 /* ... and how many     */
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

#define ELF_CLASS32   1
#define ELF_EXEC      2
#define ELF_MACH_386  3

/* Program header: "put this part of the file at this address" */
struct elf_phdr {
    uint32_t type;                  /* 1 = PT_LOAD          */
    uint32_t offset;                /* where in the file    */
    uint32_t vaddr;                 /* where in memory      */
    uint32_t paddr;
    uint32_t filesz;                /* bytes to copy        */
    uint32_t memsz;                 /* bytes in memory (the rest is zero: .bss) */
    uint32_t flags;                 /* PF_X, PF_W, PF_R     */
    uint32_t align;
};

#define PT_LOAD 1
#define PF_W    2

#endif
