; boot.asm -- Multiboot header and kernel entry point.
;
; A Multiboot loader (QEMU's -kernel, or GRUB) finds the header below,
; loads our ELF at the addresses in linker.ld, and jumps to _start with:
;   - the CPU in 32-bit protected mode, paging OFF, interrupts OFF
;   - EAX = 0x2BADB002 (proof we were booted by a Multiboot loader)
;   - EBX = physical address of the Multiboot information structure
;   - ESP = undefined  (we must set up our own stack before calling C)

MB_MAGIC    equ 0x1BADB002
MB_ALIGN    equ 1 << 0              ; page-align boot modules
MB_MEMINFO  equ 1 << 1              ; ask for a memory map
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384                      ; 16 KiB boot stack
stack_top:

section .text
global _start
extern kmain

_start:
    mov esp, stack_top

    ; kmain(uint32_t magic, uint32_t mbi_phys) -- cdecl: push args right to left
    push ebx
    push eax
    call kmain

    ; kmain should never return; if it does, halt forever.
.hang:
    cli
    hlt
    jmp .hang
