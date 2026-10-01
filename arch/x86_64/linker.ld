OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
    /* Keep the legacy Multiboot load address, but place the UEFI build above
     * low firmware allocations. The EFI stub is linked at 32 MiB. */
    . = DEFINED(UEFI_KERNEL_BASE) ? UEFI_KERNEL_BASE : 0x00100000;
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
    ASSERT(!DEFINED(UEFI_KERNEL_BASE) || kernel_end <= 0x02000000,
           "UEFI kernel/archive overlaps the reserved EFI stub at 32 MiB")
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
