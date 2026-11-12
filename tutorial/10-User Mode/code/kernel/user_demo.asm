; user_demo.asm -- a tiny program that runs in USER mode (ring 3).
;
; It is only data inside the kernel image: kmain copies these bytes into a
; user page at 0x00400000 and jumps there. It uses only relative jumps, so
; it works at any address. Lesson 12 replaces this with real ELF programs.

section .rodata
global user_demo_start, user_demo_end

user_demo_start:
    mov eax, 0x1234             ; something recognizable in the register dump
    mov ecx, 100000000
.spin:                          ; burn time: timer interrupts arrive meanwhile
    dec ecx
    jnz .spin
    cli                         ; privileged instruction: #GP in ring 3
user_demo_end:
