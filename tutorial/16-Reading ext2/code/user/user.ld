/* user.ld -- how every user program is laid out in memory.
 *
 * Two loadable segments: code (read + execute) and data (read + write).
 * exec() maps the first read-only and the second writable. */
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);      /* R-X */
    data PT_LOAD FLAGS(6);      /* RW- */
}

SECTIONS
{
    . = 0x00400000;             /* USER_BASE in kernel/mm/uvm.h */

    .text   : { *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text

    /* Writable data starts on a new page, so code pages can be read-only.
     * (A separate statement: ".data ALIGN(4K)" would vanish with an empty .data.) */
    . = ALIGN(4K);
    .data   : { *(.data .data.*) } :data
    .bss    : { *(COMMON) *(.bss .bss.*) } :data

    end = .;                    /* V7 name: first address after the program,
                                   where the heap (sbrk) starts */

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
