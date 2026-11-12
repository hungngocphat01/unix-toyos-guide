/* linker.ld -- a higher-half kernel.
 *
 * Every section has two addresses:
 *   VMA (virtual)  -- where the code expects to run: 0xC0100000 and up
 *   LMA (physical) -- where the loader puts it:      0x00100000 and up
 * AT(...) sets the LMA. The loader uses the LMA; the code uses the VMA.
 *
 * The .boot section is the exception: it runs before paging is on, so it
 * is linked at its physical address.
 */
ENTRY(_start)

KERNBASE = 0xC0000000;      /* must match mm/memlayout.h */

SECTIONS
{
    . = 1M;
    kernel_start = . + KERNBASE;

    .boot :
    {
        *(.multiboot)       /* must be in the first 8 KiB of the file */
        *(.boot)
    }

    . += KERNBASE;

    .text ALIGN(4K) : AT(ADDR(.text) - KERNBASE)
    {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNBASE)
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : AT(ADDR(.data) - KERNBASE)
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNBASE)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    kernel_end = .;
}
