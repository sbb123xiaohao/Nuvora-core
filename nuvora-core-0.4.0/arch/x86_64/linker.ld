OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x00100000;
    kernel_begin = .;
    .multiboot : { KEEP(*(.multiboot)) } :text
    .text ALIGN(4096) : { *(.text.boot) *(.text .text.*) } :text
    . = ALIGN(4096);
    kernel_text_end = .;
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(4096);
    kernel_ro_end = .;
    .data : { *(.data .data.*) } :data
    .bss ALIGN(4096) : { *(COMMON) *(.bss .bss.*) } :data
    . = ALIGN(4096);
    kernel_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
