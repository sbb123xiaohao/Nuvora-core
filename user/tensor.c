#include "runtime.h"
#include <nv/ai.h>
static signed char a[128*128], b[128*128];
static i32 c[128*128];
int user_main(const char *args) {
    if (!strcmp(args,"--help")) {println("forge tensor: verify CPU INT8 matrix multiplication.");return 0;}
    struct nv_ai_info caps;
    if (devctl(NV_SUB_AI,NV_AI_INFO,&caps)<0 || caps.hardware_ready || caps.backend!=NV_AI_CPU) return 1;
    u32 cases[][3]={{1,1,1},{7,13,9},{128,128,128}};
    struct nv_info before,after;info(&before);
    for(u32 t=0;t<ARRAY_LEN(cases);++t) {
        u32 m=cases[t][0],n=cases[t][1],k=cases[t][2];
        for(u32 i=0;i<m*k;++i) a[i]=(signed char)((i*17u)%256);
        for(u32 i=0;i<k*n;++i) b[i]=(signed char)((i*31u+7)%256);
        struct nv_ai_gemm req={1,NV_AI_CPU,m,n,k,0,(uptr)a,(uptr)b,(uptr)c};
        if(devctl(NV_SUB_AI,NV_AI_GEMM_I8,&req)) return 2;
        for(u32 row=0;row<m;++row) for(u32 col=0;col<n;++col) {
            i32 sum=0;
            for(u32 z=0;z<k;++z) sum+=(i32)a[row*k+z]*b[z*n+col];
            if(c[row*n+col]!=sum) return 3;
        }
        req.m=129;
        if(devctl(NV_SUB_AI,NV_AI_GEMM_I8,&req)!=-NV_EINVAL) return 4;
        req.m=m;req.a=1ull<<32;
        if(devctl(NV_SUB_AI,NV_AI_GEMM_I8,&req)!=-NV_EFAULT) return 5;
        req.a=(uptr)a;req.c=(uptr)user_main;
        if(devctl(NV_SUB_AI,NV_AI_GEMM_I8,&req)!=-NV_EFAULT) return 6;
        req.c=(uptr)c;req.backend=1;
        if(devctl(NV_SUB_AI,NV_AI_GEMM_I8,&req)!=-NV_ENODEV) return 7;
    }
    info(&after);
    if(before.free_pages!=after.free_pages || before.heap_used!=after.heap_used) return 8;
    print("TENSOR PASS: CPU workers=");print_u32(caps.workers);
    println("; signed INT8 GEMM, bounds, buffer protection and reclamation");return 0;
}
