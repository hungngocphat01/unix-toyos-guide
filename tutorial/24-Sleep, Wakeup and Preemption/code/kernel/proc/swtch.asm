; swtch.asm -- switch from one kernel stack to another.
;
; void swtch(struct context **old, struct context *new);
;
; Save the current kernel context on the current stack and store a pointer
; to it in *old; then load the context `new` and continue there. A context
; is what a C function must preserve across a call (the callee-saved
; registers) plus where to continue (eip). So from the caller's point of
; view, swtch() is a function call that returns... later, when someone
; switches back to it.
;
;   struct context (on the stack, lowest address first):
;     edi, esi, ebx, ebp, eip       <- eip = swtch's return address

global swtch

section .text
swtch:
    mov eax, [esp + 4]          ; old
    mov edx, [esp + 8]          ; new

    ; Build struct context on the current stack: the "call swtch" already
    ; pushed eip. Push the callee-saved registers below it.
    push ebp
    push ebx
    push esi
    push edi

    mov [eax], esp              ; *old = the context we just built
    mov esp, edx                ; switch stacks: we're on new's stack now

    pop edi                     ; restore the context saved there earlier
    pop esi
    pop ebx
    pop ebp
    ret                         ; pops new's eip
