.section .text.start,"ax"
.code64
.global _start
_start:
    xor %ebp,%ebp
    mov (%rsp),%rdi
    lea 8(%rsp),%rsi
    lea 16(%rsp,%rdi,8),%rdx
    and $-16,%rsp
    call main
    mov %eax,%ebx
    mov $13,%eax
    int $0x81
1:  ud2
    jmp 1b
.section .note.GNU-stack,"",@progbits
