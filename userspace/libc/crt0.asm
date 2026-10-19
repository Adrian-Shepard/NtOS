[BITS 64]
global _start
extern main
extern _exit

section .text
_start:
    xor rbp, rbp
    mov rdi, [rsp]        ; argc
    lea rsi, [rsp + 8]    ; argv
    and rsp, -16
    call main
    mov rdi, rax
    call _exit
.hang:
    hlt
    jmp .hang