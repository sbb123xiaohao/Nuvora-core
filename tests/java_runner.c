/* Host adapter for differential and sanitizer tests of the guest's VM core. */
#include <stdio.h>
#include <stdlib.h>
#include "../user/java_vm.h"
static int output(void *ctx, const char *s, u32 n) {
    (void)ctx; return fwrite(s,1,n,stdout)==n?0:-1;
}
int main(int argc,char **argv) {
    if (argc<2) return 99;
    FILE *file=fopen(argv[1],"rb");
    if (!file) return 98;
    u8 *bytes=malloc(NV_JAVA_CLASS_MAX+1); void *arena=malloc(NV_JAVA_ARENA);
    if (!bytes || !arena) return 97;
    usize n=fread(bytes,1,NV_JAVA_CLASS_MAX+1,file); fclose(file);
    struct nv_java_host host={.write=output}; struct nv_java_result result;
    int status=nv_java_run(bytes,n,arena,NV_JAVA_ARENA,(const char *const *)(argv+2),argc-2,NV_JAVA_STEPS,&host,&result);
    fprintf(stderr,"java status=%d: %s; major=%u steps=%u heap=%u method=%u pc=%u opcode=%02x\n",
            status,result.message,result.major,result.instructions,result.heap_bytes,result.method,result.pc,result.opcode);
    free(bytes); free(arena); return status;
}
