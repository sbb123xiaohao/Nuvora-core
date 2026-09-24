/* Reserved low-memory SIPI bootstrap to x64, not a 32-bit kernel target.
 * Code at 0x7000; separate NX parameters at 0x8000: CR3, slot, RSP, entry. */
.section .rodata.ap,"a"
.balign 16
.global ap_trampoline, ap_trampoline_end
ap_trampoline:
.code16
    cli
    cld
    xor %ax,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    lgdt 0x7000+(ap_gdtr-ap_trampoline)
    mov %cr0,%eax
    or $1,%eax
    mov %eax,%cr0
    ljmpl $8,$0x7000+(ap_protected-ap_trampoline)
.code32
ap_protected:
    mov $16,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    mov $0x620,%eax
    mov %eax,%cr4
    mov 0x8000,%eax
    mov %eax,%cr3
    mov $0xc0000080,%ecx
    rdmsr
    or $0x900,%eax
    wrmsr
    mov %cr0,%eax
    and $0xfffffff3,%eax
    or $0x80010023,%eax
    mov %eax,%cr0
    ljmp $24,$0x7000+(ap_long-ap_trampoline)
.code64
ap_long:
    mov $16,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    xor %eax,%eax
    mov %ax,%fs
    mov %ax,%gs
    mov 0x8008,%rsp
    xor %ebp,%ebp
    mov 0x8004,%edi
    mov 0x8010,%rax
    call *%rax
1:  cli
    hlt
    jmp 1b
.balign 8
ap_gdt:
    .quad 0,0x00cf9a000000ffff,0x00cf92000000ffff,0x00af9a000000ffff
ap_gdtr:
    .word 31
    .long 0x7000+(ap_gdt-ap_trampoline)
ap_trampoline_end:
.section .note.GNU-stack,"",@progbits
