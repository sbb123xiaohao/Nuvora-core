OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)
/* The stub prefers a fixed load address well above the kernel's 1 MiB load
 * range; scripts/mkuefi.py turns this ELF into a PE32+ with the same base.
 * Sections start one page in so PE headers and code never share an RVA. */
SECTIONS {
    . = 0x02001000;
    .text : { *(.text .text.*) }
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    .data : { *(.data .data.*) }
    . = ALIGN(4096);
    .bss : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
