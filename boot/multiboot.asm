[BITS 32]

section .multiboot_header
align 8
header_start:
    dd 0xE85250D6                ; Magic number (Multiboot 2)
    dd 0                         ; Architecture 0 (protected mode i386)
    dd header_end - header_start ; Header length
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start)) ; Checksum

    ; Framebuffer tag (тип 5, обязательный, 1280x800x32)
    align 8
    dw 5    ; Tag type: 5 = Framebuffer request
    dw 0    ; Flags: 0 = Required (обязательный)
    dd 20   ; Size
    dd 1280 ; Width
    dd 800  ; Height
    dd 32   ; Depth (bpp)

    ; End tag
    align 8
    dw 0
    dw 0
    dd 8
header_end:

section .text
global dualboot_entry
extern dualboot_main
extern stack_top

dualboot_entry:
    cli
    mov esp, stack_top

    ; Сохраняем указатели, переданные GRUB (magic в eax, info в ebx)
    push ebx
    push eax

    ; 1. Включаем PAE (Physical Address Extension)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; 2. Настраиваем таблицы страниц для identity mapping (первые 4 ГБ)
    ; ВАЖНО: используем адрес 0x200000, так как 0x100000 занят ядром!
    mov edi, 0x200000
    mov cr3, edi
    xor eax, eax
    mov ecx, 6144 ; Очищаем 6 страниц (24 КБ: 1 PML4 + 1 PDPT + 4 PD)
    rep stosd
    mov edi, cr3

    ; PML4[0] -> PDPT (0x201000)
    mov dword [edi], 0x201003

    ; PDPT[0..3] -> PD0..3
    mov dword [edi + 4096 + 0],  0x202003
    mov dword [edi + 4096 + 8],  0x203003
    mov dword [edi + 4096 + 16], 0x204003
    mov dword [edi + 4096 + 24], 0x205003

    ; Заполняем 4 PD (по 2 МБ страницы, всего 4 ГБ)
    mov edi, 0x202000
    mov ebx, 0x00000083 ; Present + RW + Huge Page
    mov ecx, 512 * 4    ; 2048 записей
.fill_pd:
    mov [edi], ebx
    add ebx, 0x200000
    add edi, 8
    loop .fill_pd

    ; 3. Включаем Long Mode в EFER
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8 ; Устанавливаем LME
    wrmsr

    ; 4. Включаем пейджинг
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Загружаем 64-битную GDT
    lgdt [gdt64.pointer]

    ; Дальний прыжок в 64-битный код
    jmp gdt64.code:long_mode_start

[BITS 64]
long_mode_start:
    ; Восстанавливаем аргументы GRUB (стек 32-битный, по 4 байта)
    mov eax, [rsp]     ; magic
    mov ebx, [rsp + 4] ; mb_info
    add rsp, 8         ; Очищаем стек от 2 значений
    
    ; Передаем управление в C-код DualBoot
    mov rdi, rbx ; Multiboot info ptr
    mov rsi, rax ; Magic
    call dualboot_main
    
    ; Если вернулось управление (не должно), зависаем
    cli
.hang:
    hlt
    jmp .hang

section .rodata
gdt64:
    dq 0
.code equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; 64-bit code segment
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .bss
align 16
stack_bottom:
    resb 32768
stack_top: