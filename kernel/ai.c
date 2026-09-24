#include "kernel.h"
#include <nv/ai.h>
static u32 candidates;
static void discover(u32 address, u32 id, u32 cls) {
    (void)address; (void)id;
    if ((cls >> 24)==3 || (cls >> 24)==0x12) ++candidates;
}
void ai_init(void) {
    pci_visit(discover);
    kprintf("[ok] AI: INT8 GEMM on %u CPU worker(s); %u PCI candidates, no hardware backend\n",
            smp_online(), candidates);
}
struct gemm_job { const signed char *a, *b; i32 *c; u32 m, n, k; };
static void gemm(void *context, u32 worker, u32 count) {
    const struct gemm_job *job=context;
    for (u32 row=worker; row<job->m; row+=count)
        for (u32 col=0; col<job->n; ++col) {
            i32 sum=0;
            for (u32 k=0; k<job->k; ++k)
                sum+=(i32)job->a[row*job->k+k] * (i32)job->b[k*job->n+col];
            job->c[row*job->n+col]=sum;
        }
}
int ai_ioctl(u32 op, u32 ptr) {
    if (op==NV_AI_INFO) {
        if (!user_range(current->pd,ptr,sizeof(struct nv_ai_info),true)) return -NV_EFAULT;
        struct nv_ai_info info={.version=1,.backend=NV_AI_CPU,.workers=smp_online(),
            .max_dimension=NV_AI_DIM_MAX,.capabilities=NV_AI_CAP_GEMM_I8,.pci_candidates=candidates};
        strlcpy(info.name,"CPU INT8 GEMM",sizeof(info.name));
        memcpy((void *)(uptr)ptr,&info,sizeof(info)); return 0;
    }
    if (op!=NV_AI_GEMM_I8) return -NV_EINVAL;
    if (!user_range(current->pd,ptr,sizeof(struct nv_ai_gemm),false)) return -NV_EFAULT;
    struct nv_ai_gemm req; memcpy(&req,(void *)(uptr)ptr,sizeof(req));
    if (req.version!=1 || req.flags || !req.m || !req.n || !req.k ||
        req.m>NV_AI_DIM_MAX || req.n>NV_AI_DIM_MAX || req.k>NV_AI_DIM_MAX) return -NV_EINVAL;
    if (req.backend!=NV_AI_CPU) return -NV_ENODEV;
    if ((req.a|req.b|req.c)>>32) return -NV_EFAULT;
    u32 as=req.m*req.k,bs=req.k*req.n,cs=req.m*req.n*sizeof(i32);
    if (!user_range(current->pd,(u32)req.a,as,false) ||
        !user_range(current->pd,(u32)req.b,bs,false) ||
        !user_range(current->pd,(u32)req.c,cs,true)) return -NV_EFAULT;
    u32 co=ALIGN_UP(as+bs,16);
    u8 *memory=kmalloc(co+cs);
    if (!memory) return -NV_ENOMEM;
    memcpy(memory,(void *)(uptr)req.a,as); memcpy(memory+as,(void *)(uptr)req.b,bs);
    struct gemm_job job={(void *)memory,(void *)(memory+as),(void *)(memory+co),req.m,req.n,req.k};
    smp_parallel(gemm,&job);
    memcpy((void *)(uptr)req.c,job.c,cs); kfree(memory);
    return 0;
}
