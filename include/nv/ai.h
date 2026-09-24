#ifndef NV_AI_H
#define NV_AI_H
#include <nv/types.h>
enum { NV_AI_INFO = 1, NV_AI_GEMM_I8 };
enum { NV_AI_CPU = 0, NV_AI_CAP_GEMM_I8 = 1 };
#define NV_AI_DIM_MAX 128u
struct nv_ai_info {
    u32 version, backend, workers, max_dimension, capabilities;
    u32 pci_candidates, hardware_ready, reserved;
    char name[32];
};
/* Row-major signed INT8 A[m,k], B[k,n], signed INT32 C[m,n], C = A * B.
 * Synchronous; flags/reserved must be zero. Only backend 0 is implemented. */
struct nv_ai_gemm {
    u32 version, backend, m, n, k, flags;
    u64 a, b, c;
};
_Static_assert(sizeof(struct nv_ai_gemm) == 48, "AI request layout");
#endif
