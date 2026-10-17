.section .text
.global compat_syscall_entry
compat_syscall_entry:
    mov %rsp,arch_syscall_user_stack(%rip)
    mov arch_syscall_stack(%rip),%rsp
    pushq $0x23
    pushq arch_syscall_user_stack(%rip)
    push %r11
    pushq $0x1b
    push %rcx
    pushq $0
    pushq $0x82
    jmp interrupt_common
.section .note.GNU-stack,"",@progbits
