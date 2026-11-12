; isr.asm -- the first instructions run for EVERY interrupt and exception.
;
; There are 256 tiny stubs, one per vector. Each makes the stack look the
; same (push an error code if the CPU didn't, then push the vector number)
; and jumps to trap_common, which saves the remaining registers, calls the
; C function trap(struct trapframe *), then restores everything and IRETs.

extern trap

SEG_KDATA equ 0x10

; Vectors for which the CPU itself pushes an error code.
%macro STUB 1
stub%1:
%if %1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17 || %1 == 21 || %1 == 29 || %1 == 30
    ; error code already on the stack
%else
    push dword 0                ; dummy error code, so all frames look alike
%endif
    push dword %1               ; vector number -> trapframe.trapno
    jmp trap_common
%endmacro

section .text

%assign i 0
%rep 256
    STUB i
%assign i i + 1
%endrep

trap_common:
    push ds
    push es
    push fs
    push gs
    pushad

    mov ax, SEG_KDATA           ; we may have come from user mode:
    mov ds, ax                  ; make sure we use kernel data segments
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; argument: pointer to the trapframe
    call trap
    add esp, 4

; Returning from ANY interrupt goes through here. Lesson 10 also jumps here
; to enter user mode for the first time.
global trapret
trapret:
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8                  ; drop trapno and err
    iretd

; Table of stub addresses, used by idt.c to fill the IDT.
section .rodata
global trap_stubs
trap_stubs:
%assign i 0
%rep 256
    dd stub %+ i
%assign i i + 1
%endrep
