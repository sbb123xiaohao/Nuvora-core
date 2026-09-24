.text
.align 2
.global arm64_neon_dot
arm64_neon_dot:
    ld1 {v0.4s}, [x0]
    ld1 {v1.4s}, [x1]
    mul v0.4s, v0.4s, v1.4s
    addv s0, v0.4s
    fmov w0, s0
    ret
