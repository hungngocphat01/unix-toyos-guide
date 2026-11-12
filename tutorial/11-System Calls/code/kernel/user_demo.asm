; user_demo.asm -- a tiny user program that makes system calls.
;
; It is only data inside the kernel image: kmain copies these bytes to
; 0x00400000 in a new address space and enters user mode there.
; Lesson 12 replaces this with real ELF programs.

USER_CODE equ 0x00400000        ; where kmain maps us (kmain.c)

SYS_exit  equ 1                 ; abi/syscall.h
SYS_write equ 4
EFAULT    equ 14                ; abi/errno.h

; The code is linked inside the kernel but runs at USER_CODE, so an address
; inside it is USER_CODE + (label - user_demo_start).
%define USER_ADDR(label) (USER_CODE + (label - user_demo_start))

section .rodata
global user_demo_start, user_demo_end

user_demo_start:
    ; write(1, hello, hello_len)
    mov eax, SYS_write
    mov ebx, 1
    mov ecx, USER_ADDR(hello)
    mov edx, hello_len
    int 0x80

    ; write(1, <kernel address>, 16) must fail with EFAULT
    mov eax, SYS_write
    mov ebx, 1
    mov ecx, 0xC0100000
    mov edx, 16
    int 0x80
    jnc .failed                 ; carry clear = success?! the kernel let us read it
    cmp eax, EFAULT
    jne .failed

    mov eax, SYS_write
    mov ebx, 1
    mov ecx, USER_ADDR(efault_ok)
    mov edx, efault_ok_len
    int 0x80

    ; exit(0)
    mov eax, SYS_exit
    mov ebx, 0
    int 0x80

.failed:
    mov eax, SYS_exit
    mov ebx, 1
    int 0x80

hello:      db "Hello from user mode!", 10
hello_len   equ $ - hello
efault_ok:  db "writing kernel memory gave EFAULT, good", 10
efault_ok_len equ $ - efault_ok

user_demo_end:
