.section .nvuser,"ax"
.align 12
.global arm64_user_start
arm64_user_start:
    /* The mappings and stack belong to EL0; no kernel address is required. */
    ldr x0, =0x20000000
    sub sp, sp, #16
    str x0, [sp]
    ldr x0, [sp]
    add sp, sp, #16
    ld1 {v0.4s}, [x0]
    add x1, x0, #16
    ld1 {v1.4s}, [x1]
    mul v0.4s, v0.4s, v1.4s
    addv s0, v0.4s
    fmov w1, s0
    svc #0                     /* submit buffer and computed dot product */
    ldr x0, =0x40080000
    ldr w2, [x0]               /* expected EL0 data abort: kernel text */
    svc #1                     /* report isolation and exit via PSCI */
1:  b 1b
.ltorg
