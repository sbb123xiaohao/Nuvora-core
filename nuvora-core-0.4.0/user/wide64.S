.section .text.wide_register_test,"ax"
.code64
.global wide_register_test
wide_register_test:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    movabs $0x123456789abc0008,%r8
    movabs $0x123456789abc0009,%r9
    movabs $0x123456789abc000a,%r10
    movabs $0x123456789abc000b,%r11
    movabs $0x123456789abc000c,%r12
    movabs $0x123456789abc000d,%r13
    movabs $0x123456789abc000e,%r14
    movabs $0x123456789abc000f,%r15
    movabs $0xfedcba9876543210,%rbp
    movabs $0x8877665544332211,%rsi
    movabs $0x1122334455667788,%rdi
    mov $14,%eax
    mov $30,%ebx
    xor %ecx,%ecx
    xor %edx,%edx
    int $0x81
.macro CHECK reg, value
    movabs $\value,%rax
    cmp %rax,%\reg
    jne 1f
.endm
    CHECK r8, 0x123456789abc0008
    CHECK r9, 0x123456789abc0009
    CHECK r10, 0x123456789abc000a
    CHECK r11, 0x123456789abc000b
    CHECK r12, 0x123456789abc000c
    CHECK r13, 0x123456789abc000d
    CHECK r14, 0x123456789abc000e
    CHECK r15, 0x123456789abc000f
    CHECK rbp, 0xfedcba9876543210
    CHECK rsi, 0x8877665544332211
    CHECK rdi, 0x1122334455667788
    mov $1,%eax
    jmp 2f
1:  xor %eax,%eax
2:  pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
.section .note.GNU-stack,"",@progbits
