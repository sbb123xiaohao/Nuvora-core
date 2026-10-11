/* Host adapter for differential and sanitizer tests of the guest's VM core. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../user/java_vm.h"
static int output(void *ctx, const char *s, u32 n) {
    (void)ctx; return fwrite(s,1,n,stdout)==n?0:-1;
}
static int load(void *context,const char *name,void *buffer,u32 capacity,u32 *length) {
    char path[1024];
    if (nv_java_class_path(path,sizeof(path),context,name)) return -1;
    FILE *file=fopen(path,"rb");
    if (!file) return -1;
    if (fseek(file,0,SEEK_END)) { fclose(file); return -1; }
    long size=ftell(file);
    if (size<0 || size>NV_JAVA_CLASS_MAX) { fclose(file); return -1; }
    *length=size; rewind(file);
    int status=buffer && (capacity!=*length || fread(buffer,1,capacity,file)!=capacity);
    fclose(file); return status;
}
int main(int argc,char **argv) {
    if (argc<2) return 99;
    FILE *file=fopen(argv[1],"rb");
    if (!file) return 98;
    u8 *bytes=malloc(NV_JAVA_CLASS_MAX+1); void *arena=malloc(NV_JAVA_ARENA);
    if (!bytes || !arena) return 97;
    usize n=fread(bytes,1,NV_JAVA_CLASS_MAX+1,file); fclose(file);
    char root[1024];
    const char *selected=getenv("JAVA_TEST_CLASSPATH");
    if (selected) { if (strlen(selected)>=sizeof(root)) return 96; strcpy(root,selected); }
    else {
        if (strlen(argv[1])>=sizeof(root)) return 96;
        strcpy(root,argv[1]); char *slash=strrchr(root,'/');
        if (slash) slash[1]=0; else strcpy(root,".");
    }
    struct nv_java_host host={.context=root,.write=output,.read_class=load}; struct nv_java_result result;
    int status=nv_java_run(bytes,n,arena,NV_JAVA_ARENA,(const char *const *)(argv+2),argc-2,NV_JAVA_STEPS,&host,&result);
    fprintf(stderr,"java status=%d: %s; major=%u steps=%u heap=%u method=%u pc=%u opcode=%02x\n",
            status,result.message,result.major,result.instructions,result.heap_bytes,result.method,result.pc,result.opcode);
    free(bytes); free(arena); return status;
}
