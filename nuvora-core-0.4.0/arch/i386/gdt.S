.section .text
.code32
.global gdt_load
gdt_load:
    mov 4(%esp),%eax
    lgdt (%eax)
    ljmp $0x08,$1f
1:  mov $0x10,%ax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%fs
    mov %ax,%gs
    mov %ax,%ss
    mov $0x28,%ax
    ltr %ax
    ret
.global arch_resume
arch_resume:
    mov 4(%esp),%esp
    jmp interrupt_return
.section .note.GNU-stack,"",@progbits
