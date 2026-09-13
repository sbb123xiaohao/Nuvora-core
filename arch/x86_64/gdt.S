.section .text
.code64
.global gdt_load
gdt_load:
    lgdt (%rdi)
    pushq $8
    lea 1f(%rip),%rax
    push %rax
    lretq
1:  mov $0x10,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    xor %eax,%eax
    mov %ax,%fs
    mov %ax,%gs
    mov $0x28,%ax
    ltr %ax
    ret
.global arch_resume
arch_resume:
    mov %rdi,%rsp
    jmp interrupt_return
.section .note.GNU-stack,"",@progbits
