OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40080000;
    kernel_begin = .;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :text
    . = ALIGN(4096);
    user_begin = .;
    .usertext : { KEEP(*(.nvuser)) } :text
    . = ALIGN(4096);
    user_end = .;
    kernel_text_end = .;
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(4096);
    kernel_ro_end = .;
    .data : { *(.data .data.*) } :data
    . = ALIGN(4096);
    image_end = .;
    .bss : { *(COMMON) *(.bss .bss.*) *(.bss.stack) } :data
    . = ALIGN(4096);
    kernel_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
