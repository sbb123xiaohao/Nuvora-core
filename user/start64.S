.section .text.start,"ax"
.code64
.global _start
_start:
    xor %ebp,%ebp
    mov %rbx,%rdi
    and $-16,%rsp
    call user_main
    mov %eax,%ebx
    mov $13,%eax
    int $0x81
1:  jmp 1b
.section .note.GNU-stack,"",@progbits
