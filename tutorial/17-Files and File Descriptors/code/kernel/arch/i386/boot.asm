; boot.asm -- Multiboot header, early paging, and the jump to the higher half.
;
; The kernel is LINKED at 0xC0100000 but LOADED at physical 0x00100000.
; Until paging is on, every address we use must be physical, so the code in
; section .boot (linked low, see linker.ld) subtracts KERNBASE by hand.

KERNBASE    equ 0xC0000000          ; must match mm/memlayout.h

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ (1 << 0) | (1 << 1) ; page-align modules, give a memory map
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

CR0_PG      equ 0x80000000
CR4_PSE     equ 0x00000010          ; allow 4 MiB pages

PDE_4M      equ 0x83                ; present | writable | 4 MiB page

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

; The boot page directory maps the first 16 MiB of physical memory twice,
; with four 4 MiB pages each:
;   virtual 0x00000000.. -> physical 0..16 MiB   (identity: the code below keeps running)
;   virtual 0xC0000000.. -> physical 0..16 MiB   (where the kernel is linked)
; vmm_init() replaces it with the real page tables.
section .data
align 4096
boot_pd:
    dd 0x00000000 | PDE_4M, 0x00400000 | PDE_4M, 0x00800000 | PDE_4M, 0x00C00000 | PDE_4M
    times (KERNBASE >> 22) - 4 dd 0
    dd 0x00000000 | PDE_4M, 0x00400000 | PDE_4M, 0x00800000 | PDE_4M, 0x00C00000 | PDE_4M
    times 1024 - (KERNBASE >> 22) - 4 dd 0

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

; ---------------------------------------------------------------------------
; Runs at its physical address, paging off. Don't touch EAX/EBX: they hold
; the Multiboot magic and info pointer.
section .boot
global _start
_start:
    mov ecx, cr4
    or ecx, CR4_PSE
    mov cr4, ecx

    mov ecx, boot_pd - KERNBASE     ; CR3 takes a PHYSICAL address
    mov cr3, ecx

    mov ecx, cr0
    or ecx, CR0_PG
    mov cr0, ecx                    ; paging on. Next fetch goes through boot_pd
                                    ; (identity part, so we keep running here)

    mov ecx, higher_half            ; an absolute address: 0xC01xxxxx
    jmp ecx

; ---------------------------------------------------------------------------
; Runs at 0xC01xxxxx, as linked.
section .text
extern kmain
higher_half:
    mov esp, stack_top

    push ebx                        ; kmain(magic, mbi_phys)
    push eax
    call kmain

.hang:
    cli
    hlt
    jmp .hang
