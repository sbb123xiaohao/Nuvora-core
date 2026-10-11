#ifndef NV_JAVA_VM_H
#define NV_JAVA_VM_H
#include <nv/types.h>

/* Experimental, bounded interpreter for a subset of Java 21 class files.
 * It is not a Java SE implementation. See docs/JAVA.md for the exact contract.
 * Class bytes, arena and callbacks remain owned by the caller. */
#define NV_JAVA_CLASS_MAX (256u * 1024u)
#define NV_JAVA_ARENA (2u * 1024u * 1024u)
#define NV_JAVA_STEPS 20000000u
enum nv_java_status {
    NV_JAVA_OK, NV_JAVA_FORMAT, NV_JAVA_UNSUPPORTED, NV_JAVA_MEMORY,
    NV_JAVA_VERIFY, NV_JAVA_EXCEPTION, NV_JAVA_LIMIT, NV_JAVA_IO
};
struct nv_java_host {
    void *context;
    int (*write)(void *context, const char *bytes, u32 count);
    int (*poll)(void *context); /* optional; nonzero cancels execution */
};
struct nv_java_result {
    enum nv_java_status status;
    const char *message;
    u32 instructions, heap_bytes, pc;
    u16 major, method;
    u8 opcode;
};
int nv_java_run(const void *bytes, u32 length, void *arena, u32 arena_size,
                const char *const *args, u32 argc, u32 instruction_limit,
                const struct nv_java_host *host, struct nv_java_result *result);
#endif
