.section .text
.code64
.extern interrupt_dispatch
.global arch_guard_probe
arch_guard_probe:
    cli
    mov %rdi,%rsp
    pushq $0
    ud2
.global isr_0
isr_0:
    pushq $0
    pushq $0
    jmp interrupt_common
.global isr_1
isr_1:
    pushq $0
    pushq $1
    jmp interrupt_common
.global isr_2
isr_2:
    pushq $0
    pushq $2
    jmp interrupt_common
.global isr_3
isr_3:
    pushq $0
    pushq $3
    jmp interrupt_common
.global isr_4
isr_4:
    pushq $0
    pushq $4
    jmp interrupt_common
.global isr_5
isr_5:
    pushq $0
    pushq $5
    jmp interrupt_common
.global isr_6
isr_6:
    pushq $0
    pushq $6
    jmp interrupt_common
.global isr_7
isr_7:
    pushq $0
    pushq $7
    jmp interrupt_common
.global isr_8
isr_8:
    pushq $8
    jmp interrupt_common
.global isr_9
isr_9:
    pushq $0
    pushq $9
    jmp interrupt_common
.global isr_10
isr_10:
    pushq $10
    jmp interrupt_common
.global isr_11
isr_11:
    pushq $11
    jmp interrupt_common
.global isr_12
isr_12:
    pushq $12
    jmp interrupt_common
.global isr_13
isr_13:
    pushq $13
    jmp interrupt_common
.global isr_14
isr_14:
    pushq $14
    jmp interrupt_common
.global isr_15
isr_15:
    pushq $0
    pushq $15
    jmp interrupt_common
.global isr_16
isr_16:
    pushq $0
    pushq $16
    jmp interrupt_common
.global isr_17
isr_17:
    pushq $17
    jmp interrupt_common
.global isr_18
isr_18:
    pushq $0
    pushq $18
    jmp interrupt_common
.global isr_19
isr_19:
    pushq $0
    pushq $19
    jmp interrupt_common
.global isr_20
isr_20:
    pushq $0
    pushq $20
    jmp interrupt_common
.global isr_21
isr_21:
    pushq $21
    jmp interrupt_common
.global isr_22
isr_22:
    pushq $0
    pushq $22
    jmp interrupt_common
.global isr_23
isr_23:
    pushq $0
    pushq $23
    jmp interrupt_common
.global isr_24
isr_24:
    pushq $0
    pushq $24
    jmp interrupt_common
.global isr_25
isr_25:
    pushq $0
    pushq $25
    jmp interrupt_common
.global isr_26
isr_26:
    pushq $0
    pushq $26
    jmp interrupt_common
.global isr_27
isr_27:
    pushq $0
    pushq $27
    jmp interrupt_common
.global isr_28
isr_28:
    pushq $0
    pushq $28
    jmp interrupt_common
.global isr_29
isr_29:
    pushq $29
    jmp interrupt_common
.global isr_30
isr_30:
    pushq $30
    jmp interrupt_common
.global isr_31
isr_31:
    pushq $0
    pushq $31
    jmp interrupt_common
.global isr_32
isr_32:
    pushq $0
    pushq $32
    jmp interrupt_common
.global isr_33
isr_33:
    pushq $0
    pushq $33
    jmp interrupt_common
.global isr_34
isr_34:
    pushq $0
    pushq $34
    jmp interrupt_common
.global isr_35
isr_35:
    pushq $0
    pushq $35
    jmp interrupt_common
.global isr_36
isr_36:
    pushq $0
    pushq $36
    jmp interrupt_common
.global isr_37
isr_37:
    pushq $0
    pushq $37
    jmp interrupt_common
.global isr_38
isr_38:
    pushq $0
    pushq $38
    jmp interrupt_common
.global isr_39
isr_39:
    pushq $0
    pushq $39
    jmp interrupt_common
.global isr_40
isr_40:
    pushq $0
    pushq $40
    jmp interrupt_common
.global isr_41
isr_41:
    pushq $0
    pushq $41
    jmp interrupt_common
.global isr_42
isr_42:
    pushq $0
    pushq $42
    jmp interrupt_common
.global isr_43
isr_43:
    pushq $0
    pushq $43
    jmp interrupt_common
.global isr_44
isr_44:
    pushq $0
    pushq $44
    jmp interrupt_common
.global isr_45
isr_45:
    pushq $0
    pushq $45
    jmp interrupt_common
.global isr_46
isr_46:
    pushq $0
    pushq $46
    jmp interrupt_common
.global isr_47
isr_47:
    pushq $0
    pushq $47
    jmp interrupt_common
.global isr_129
isr_129:
    pushq $0
    pushq $129
    jmp interrupt_common
isr_240:
    pushq $0
    pushq $240
    jmp interrupt_common
isr_255:
    pushq $0
    pushq $255
    jmp interrupt_common
interrupt_common:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    cld
    mov %rsp,%rdi
    and $-16,%rsp
    call interrupt_dispatch
    mov %rax,%rsp
.global interrupt_return
interrupt_return:
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16,%rsp
    iretq
.section .rodata
.global isr_table
isr_table:
    .quad isr_0
    .quad isr_1
    .quad isr_2
    .quad isr_3
    .quad isr_4
    .quad isr_5
    .quad isr_6
    .quad isr_7
    .quad isr_8
    .quad isr_9
    .quad isr_10
    .quad isr_11
    .quad isr_12
    .quad isr_13
    .quad isr_14
    .quad isr_15
    .quad isr_16
    .quad isr_17
    .quad isr_18
    .quad isr_19
    .quad isr_20
    .quad isr_21
    .quad isr_22
    .quad isr_23
    .quad isr_24
    .quad isr_25
    .quad isr_26
    .quad isr_27
    .quad isr_28
    .quad isr_29
    .quad isr_30
    .quad isr_31
    .quad isr_32
    .quad isr_33
    .quad isr_34
    .quad isr_35
    .quad isr_36
    .quad isr_37
    .quad isr_38
    .quad isr_39
    .quad isr_40
    .quad isr_41
    .quad isr_42
    .quad isr_43
    .quad isr_44
    .quad isr_45
    .quad isr_46
    .quad isr_47
    .quad isr_129
    .quad isr_240
    .quad isr_255
.section .note.GNU-stack,"",@progbits
