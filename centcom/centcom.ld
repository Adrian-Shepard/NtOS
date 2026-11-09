OUTPUT_FORMAT(elf64-x86-64)
ENTRY(dualboot_entry)

SECTIONS {
    . = 0x100000;

    .multiboot_header : { KEEP(*(.multiboot_header)) }
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(COMMON) *(.bss .bss.*) }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
    }
}