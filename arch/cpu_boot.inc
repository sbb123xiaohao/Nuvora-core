/* Included in the 32-bit entry path of both kernels. No C runs before this gate.
 * Preserve the Multiboot arguments, including on CPUs without CPUID. */
.code32
boot_cpu_check:
    pushal
    pushfl
    pop %eax
    mov %eax,%ecx
    xor $0x200000,%eax
    push %eax
    popfl
    pushfl
    pop %eax
    push %ecx
    popfl
    xor %ecx,%eax
    test $0x200000,%eax
    jz boot_cpu_fail
    xor %eax,%eax
    cpuid
    cmp $1,%eax
    jb boot_cpu_fail
    mov $1,%eax
    cpuid
#ifdef __x86_64__
    /* FPU, MSR, PAE, CMOV, FXSR, SSE, SSE2. */
    and $0x07008061,%edx
    cmp $0x07008061,%edx
    jne boot_cpu_fail
    mov $0x80000000,%eax
    cpuid
    cmp $0x80000001,%eax
    jb boot_cpu_fail
    mov $0x80000001,%eax
    cpuid
    and $0x20100000,%edx /* long mode and NX */
    cmp $0x20100000,%edx
#else
    and $0x8001,%edx /* i686 compiler baseline: FPU and CMOV */
    cmp $0x8001,%edx
#endif
    jne boot_cpu_fail
    popal
    ret
boot_cpu_fail:
    /* Diagnose on VGA and COM1 without relying on IDT, paging or a C ABI. */
    mov $0x3f9,%dx
    xor %al,%al
    out %al,%dx
    mov $0x3fb,%dx
    mov $0x80,%al
    out %al,%dx
    mov $0x3f8,%dx
    mov $1,%al
    out %al,%dx
    mov $0x3f9,%dx
    xor %al,%al
    out %al,%dx
    mov $0x3fb,%dx
    mov $3,%al
    out %al,%dx
    mov $boot_cpu_message,%esi
    mov $0xb8000,%edi
1:  lodsb
    test %al,%al
    jz 4f
    mov %al,%bl
    mov $0x4f,%ah
    stosw
    mov $0x3fd,%dx
    mov $65536,%ecx
2:  in %dx,%al
    test $0x20,%al
    jnz 3f
    loop 2b
    jmp 1b
3:  mov $0x3f8,%dx
    mov %bl,%al
    out %al,%dx
    jmp 1b
4:  cli
    hlt
    jmp 4b
.section .rodata
boot_cpu_message:
#ifdef __x86_64__
    .asciz "CPU UNSUPPORTED: need x64 FPU CMOV MSR PAE FXSR SSE2 NX.\r\n"
#else
    .asciz "CPU UNSUPPORTED: need i686 CPUID FPU CMOV.\r\n"
#endif
.section .text
