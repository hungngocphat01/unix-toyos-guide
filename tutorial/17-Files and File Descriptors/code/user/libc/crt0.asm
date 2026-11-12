; crt0.asm -- the first code of every user program ("C runtime zero").
;
; exec() leaves the stack like this (see kernel/proc/exec.c):
;   [esp]       argc
;   [esp+4]     argv[0] ... argv[argc-1], NULL
;   then        envp[0] ... NULL
; We call main(argc, argv, envp), then exit(main's return value).

global _start
extern main, exit, environ

section .text
_start:
    mov eax, [esp]              ; argc
    lea ebx, [esp + 4]          ; argv
    lea ecx, [ebx + eax*4 + 4]  ; envp = past argv's NULL
    mov [environ], ecx

    push ecx
    push ebx
    push eax
    call main
    add esp, 12

    push eax
    call exit
.hang:
    jmp .hang                   ; exit doesn't return
