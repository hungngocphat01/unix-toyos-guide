; sigtramp.asm -- where the kernel sends a process to run a signal handler
; (V7: the "tvect" entries in sys/signal.s).
;
; The kernel interrupted the program, pushed on its stack
;
;     eip, eflags, sig        (sig on top)
;
; and jumped here. We call the C function the program gave signal() for
; sig, then return to eip with every register as it was: the program
; continues as if nothing had happened.

global _sigtramp
extern _sigfunc

section .text
_sigtramp:
    push eax                    ; the registers a C function may change
    push ecx                    ; (it preserves ebx esi edi ebp itself)
    push edx
    sub esp, 108                ; and the x87 registers: the program may have
    fnsave [esp]                ; been interrupted in the middle of a computation
    cld                         ; C code expects the direction flag clear

    mov eax, [esp + 108 + 12]   ; sig
    push eax
    call [_sigfunc + eax * 4]   ; _sigfunc[sig](sig)
    add esp, 4

    frstor [esp]
    add esp, 108
    pop edx
    pop ecx
    pop eax
    add esp, 4                  ; drop sig
    popfd                       ; flags (CF of an interrupted system call...)
    ret                         ; to the interrupted eip
