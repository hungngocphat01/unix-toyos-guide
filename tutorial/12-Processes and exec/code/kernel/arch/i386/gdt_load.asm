; gdt_load.asm -- load a new GDT and make every segment register use it.
;
; The CPU caches the descriptor of each segment register when it is loaded.
; LGDT alone changes nothing visible: we must reload all six registers.

global gdt_load

SEG_KCODE equ 0x08
SEG_KDATA equ 0x10

; void gdt_load(struct gdt_ptr *p)
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, SEG_KDATA           ; data segments: a plain MOV works
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp SEG_KCODE:.reload_cs    ; CS can only be changed by a far jump/call/ret
.reload_cs:
    ret
