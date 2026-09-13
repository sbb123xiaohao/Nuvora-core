OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x40000000;
    .text : { *(.text.start) *(.text .text.*) } :text
    .rodata ALIGN(4096) : { *(.rodata .rodata.*) } :rodata
    .data ALIGN(4096) : { *(.data .data.*) } :data
    .bss ALIGN(4096) : { *(COMMON) *(.bss .bss.*) } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
