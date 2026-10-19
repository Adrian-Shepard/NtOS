[BITS 64]
global syscall_entry
global syscall_fork_ret
global user_rsp_save
extern syscall_dispatch
extern g_current_kstack_top

section .text
syscall_entry:
    mov [rel user_rsp_save], rsp
    mov rsp, [rel g_current_kstack_top]

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    cld
    call syscall_dispatch

    add rsp, 8
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    mov rsp, [rel user_rsp_save]
    db 0x48, 0x0f, 0x07    ; sysretq

syscall_fork_ret:
    add rsp, 8      ; Пропускаем RAX
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    mov rsp, [rel user_rsp_save]
    db 0x48, 0x0f, 0x07    ; sysretq

section .bss
align 16
user_rsp_save:
    resq 1