/* linker.ld -- where each part of the kernel lives in memory.
 *
 * For now the kernel is linked AND loaded at physical 1 MiB, with paging
 * off, so virtual address == physical address. Lesson 8 changes this to a
 * "higher-half" kernel linked at 0xC0100000 but still loaded at 1 MiB.
 */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* The Multiboot header must be in the first 8 KiB of the file. */
    .text ALIGN(4K) :
    {
        *(.multiboot)
        *(.text .text.*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    kernel_end = .;
}
