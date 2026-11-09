[BITS 64]
global gdt_flush
global idt_flush

section .text

; void gdt_flush(struct gdt_ptr* ptr);
; В 64-битном ABI первый аргумент передается в rdi
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Дальний прыжок для перезагрузки CS
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq
.reload_cs:
    ret

; void idt_flush(struct idt_ptr* ptr);
idt_flush:
    lidt [rdi]
    ret