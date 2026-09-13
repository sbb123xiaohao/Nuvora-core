.section .multiboot,"a"
.balign 4
.long 0x1badb002, 3, -(0x1badb002+3)
.section .bss
.balign 4096
boot_pml4: .skip 4096
boot_pdpt: .skip 4096
boot_pd: .skip 4096
.balign 16
boot_stack: .skip 32768
boot_stack_top:
.section .rodata
.balign 8
boot_gdt:
.quad 0
.quad 0x00af9a000000ffff
.quad 0x00cf92000000ffff
boot_gdtr:
.word 23
.long boot_gdt
.section .text.boot,"ax"
.code32
.global _start
_start:
    cli
    cld
    mov $boot_stack_top,%esp
    mov %eax,%esi
    mov %ebx,%edi
    mov $0x80000000,%eax
    cpuid
    cmp $0x80000001,%eax
    jb no_long_mode
    mov $0x80000001,%eax
    cpuid
    and $0x20100000,%edx
    cmp $0x20100000,%edx
    jne no_long_mode
    mov $boot_pdpt,%eax
    or $3,%eax
    mov %eax,boot_pml4
    mov $boot_pd,%eax
    or $3,%eax
    mov %eax,boot_pdpt
    xor %ecx,%ecx
1:  mov %ecx,%eax
    shl $21,%eax
    or $0x83,%eax
    mov %eax,boot_pd(,%ecx,8)
    inc %ecx
    cmp $512,%ecx
    jne 1b
    mov $0x20,%eax
    mov %eax,%cr4
    mov $boot_pml4,%eax
    mov %eax,%cr3
    mov $0xc0000080,%ecx
    rdmsr
    or $0x900,%eax
    wrmsr
    lgdt boot_gdtr
    mov %cr0,%eax
    or $0x80010000,%eax
    mov %eax,%cr0
    ljmp $0x08,$long_start
no_long_mode:
    movl $0x4f364f58,0xb8000
    movl $0x4f214f34,0xb8004
2:  hlt
    jmp 2b
.code64
long_start:
    mov $0x10,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    mov %ax,%fs
    mov %ax,%gs
    mov $boot_stack_top,%rsp
    xor %rbp,%rbp
    mov %edi,%edx
    mov %esi,%edi
    mov %edx,%esi
    call kernel_main
3:  cli
    hlt
    jmp 3b
.section .note.GNU-stack,"",@progbits
