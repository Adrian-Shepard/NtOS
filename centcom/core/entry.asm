[BITS 64]
global _start
global stack_top
global gdt_flush
global idt_flush
extern centcom_boot

section .text
_start:
    cli
    mov rsp, stack_top
    and rsp, -16
    call centcom_boot
.hang:
    hlt
    jmp .hang

gdt_flush:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .reload]
    push rax
    retfq
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov ax, 0x28
    ltr ax
    ret

idt_flush:
    lidt [rdi]
    ret

section .bss
align 16
stack_bottom: resb 32768
stack_top: