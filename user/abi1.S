/* An actual ABI 1 ELF64 fixture. No native flag or new runtime is used. */
.section .text.start,"ax"
.code64
.global _start
_start:
    xor %ecx,%ecx
    xor %edx,%edx
    mov $17,%eax
    lea info(%rip),%ebx
    int $0x81
    test %eax,%eax
    jne fail
    cmpl $1,info(%rip)
    jne fail
    mov $16,%eax
    mov $1,%ebx
    int $0x81
    cmp $0x50000000,%eax
    jne fail
    movb $0x5a,(%rax)
    mov $16,%eax
    mov $-1,%ebx
    int $0x81
    cmp $0x50001000,%eax
    jne fail
    mov $16,%eax
    mov $0x80000000,%ebx
    int $0x81
    cmp $-1,%eax /* -NV_EINVAL (Nuvora's private error numbering) */
    jne fail
    mov $17,%eax
    movabs $0x140000000,%rbx
    int $0x81
    cmp $-1,%eax
    jne fail
    xor %ebx,%ebx
    jmp done
fail:
    mov $91,%ebx
done:
    mov $13,%eax
    xor %ecx,%ecx
    xor %edx,%edx
    int $0x81
    ud2
.section .bss
.balign 16
info: .skip 44
.section .note.GNU-stack,"",@progbits
