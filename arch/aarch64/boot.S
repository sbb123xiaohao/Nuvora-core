.section .text.boot,"ax"
.align 3
.global _start
_start:
    b 1f
    .word 0
    .quad 0x80000
    .quad kernel_end - _start
    .quad 0
    .quad 0
    .quad 0
    .quad 0
    .word 0x644d5241
    .word 0
1:
    /* QEMU virt -kernel follows the arm64 Image protocol: x0 is the DTB. */
    mrs x1, mpidr_el1
    and x1, x1, #0xffffff
    cbnz x1, 9f
    ldr x2, =boot_stack_top
    mov sp, x2
    mrs x1, currentel
    cmp x1, #8
    b.ne 2f
    mov x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    mov x1, #(1 << 31)
    msr hcr_el2, x1
    mov x1, #0x3c5
    msr spsr_el2, x1
    adr x1, 2f
    msr elr_el2, x1
    eret
2:
    mrs x1, currentel
    cmp x1, #4
    b.ne 9f
    ldr x1, =boot_stack_top
    mov sp, x1
    msr daifset, #0xf
    /* Image size covers BSS; initialise it even if firmware left RAM dirty. */
    ldr x1, =image_end
    ldr x2, =kernel_end
3:  cmp x1, x2
    b.hs 4f
    str xzr, [x1], #8
    b 3b
4:
    bl arm64_main
9:
    wfi
    b 9b
.section .bss.stack,"aw",%nobits
.align 12
    .skip 16384
boot_stack_top:
