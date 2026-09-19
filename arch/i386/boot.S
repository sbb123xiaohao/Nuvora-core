.set MB_MAGIC, 0x1badb002
.set MB_FLAGS, 3
.section .multiboot,"a"
.balign 4
.long MB_MAGIC, MB_FLAGS, -(MB_MAGIC + MB_FLAGS)
.section .bss
.balign 16
boot_stack_bottom:
.skip 32768
boot_stack_top:
.section .text
.code32
.global _start
.type _start,@function
_start:
    cli
    cld
    mov $boot_stack_top, %esp
    xor %ebp, %ebp
    call boot_cpu_check
    push %ebx
    push %eax
    call kernel_main
1:  cli
    hlt
    jmp 1b
.size _start,.-_start
#include "../cpu_boot.inc"
.section .note.GNU-stack,"",@progbits
