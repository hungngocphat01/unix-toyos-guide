; setjmp.asm -- non-local goto (V7: gen/setjmp.s, rewritten for i386).
;
; setjmp(buf) saves what the caller needs to resume right after the call:
; the callee-saved registers, the stack pointer and the return address.
; longjmp(buf, val) restores them and "returns from setjmp" again, with val.
;
;   buf[0] ebx   buf[1] esi   buf[2] edi   buf[3] ebp
;   buf[4] esp (as it is after setjmp returns)   buf[5] eip (return address)

global setjmp, longjmp

section .text
setjmp:
    mov eax, [esp + 4]          ; buf
    mov [eax], ebx
    mov [eax + 4], esi
    mov [eax + 8], edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]          ; esp after our "ret"
    mov [eax + 16], ecx
    mov ecx, [esp]              ; return address
    mov [eax + 20], ecx
    xor eax, eax                ; the first time, setjmp returns 0
    ret

longjmp:
    mov edx, [esp + 4]          ; buf
    mov eax, [esp + 8]          ; val
    test eax, eax
    jnz .nonzero
    inc eax                     ; longjmp(buf, 0) makes setjmp return 1
.nonzero:
    mov ebx, [edx]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    jmp [edx + 20]              ; "return" from setjmp again
