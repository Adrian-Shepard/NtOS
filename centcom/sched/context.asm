[BITS 64]
global context_switch
global context_switch_full

section .text

; void context_switch(uint64_t *old_rsp, uint64_t new_rsp)
;   rdi = &prev->rsp, rsi = next->rsp
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov [rdi], rsp
    mov rsp, rsi

    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; void context_switch_full(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3)
;   rdi = &prev->rsp, rsi = next->rsp, rdx = next_cr3
context_switch_full:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    mov [rdi], rsp
    mov rsp, rsi
    mov cr3, rdx

    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret