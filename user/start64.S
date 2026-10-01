.section .text.start,"ax"
.code64
.global _start
_start:
    xor %ebp,%ebp
    mov %rbx,%rdi
    and $-16,%rsp
    call user_main
    mov %eax,%ebx
    movabs $0x10000000d,%rax /* NV_CALL_NATIVE | NV_EXIT */
    xor %ecx,%ecx
    xor %edx,%edx
    int $0x81
1:  jmp 1b
.section .note.GNU-stack,"",@progbits
