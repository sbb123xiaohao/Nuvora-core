#ifndef NV_COMPAT_PROCESS_STATE_H
#define NV_COMPAT_PROCESS_STATE_H
/* Build-time boundary. Native-only kernels have no Linux VMA, signal table or
 * thread-group records in each task, and do not link the Linux implementation. */
#ifndef NV_ENABLE_COMPAT
#define NV_ENABLE_COMPAT 1
#endif
#define NV_COMPAT_ARG_BYTES 32768u
#define NV_COMPAT_ARG_COUNT 128u
#define NV_COMPAT_ENV_BYTES 8192u
#define NV_COMPAT_ENV_COUNT 64u
struct compat_startup {
    const char *arg0,*environment;
    u32 environment_bytes;
    const char *arguments;
    u32 argument_bytes,argument_count;
};
#if NV_ENABLE_COMPAT
#include "linux/state.h"
#else
struct compat_state {bool enabled,trace,jit;uptr fs_base;};
struct compat_group;
#endif
#endif
