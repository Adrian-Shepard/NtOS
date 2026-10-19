[BITS 64]
global usermode_enter
global user_setjmp
global user_longjmp

section .text

; void usermode_enter(uint64_t entry_rip, uint64_t user_rsp);
;   rdi = entry, rsi = user stack
usermode_enter:
    cli
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x1B
    push rsi
    push qword 0x202
    push qword 0x23
    push rdi
    iretq

; int user_setjmp(jmp_buf_t *b);
;   rdi = buf, возвращает 0
user_setjmp:
    mov [rdi + 0],  rsp
    mov [rdi + 8],  rbp
    mov [rdi + 16], rbx
    mov [rdi + 24], r12
    mov [rdi + 32], r13
    mov [rdi + 40], r14
    mov [rdi + 48], r15
    mov rax, [rsp]
    mov [rdi + 56], rax      ; return address
    xor eax, eax
    ret

; void user_longjmp(jmp_buf_t *b, int val);
;   rdi = buf, rsi = val
user_longjmp:
    mov rsp, [rdi + 0]
    mov rbp, [rdi + 8]
    mov rbx, [rdi + 16]
    mov r12, [rdi + 24]
    mov r13, [rdi + 32]
    mov r14, [rdi + 40]
    mov r15, [rdi + 48]
    mov rax, [rdi + 56]
    mov [rsp], rax           ; восстанавливаем адрес возврата
    mov eax, esi
    ret