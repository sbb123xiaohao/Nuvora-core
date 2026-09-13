.section .text.start,"ax"
.code32
.global _start
_start:
    xor %ebp,%ebp
    push %ebx
    call user_main
    mov %eax,%ebx
    mov $13,%eax
    int $0x81
1:  jmp 1b
.section .note.GNU-stack,"",@progbits
