#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../user/java_vm.c"

struct capture {
    char output[2048]; u32 used,queries,reads;
    int fault; const u8 *replacement; u32 replacement_size;
};
static int output(void *context,const char *bytes,u32 length) {
    struct capture *c=context;
    if (length>sizeof(c->output)-c->used) return -1;
    memcpy(c->output+c->used,bytes,length); c->used+=length; return 0;
}
static u8 *fixture(const char *name,u32 *size) {
    char path[256]; assert(!nv_java_class_path(path,sizeof(path),"tests/fixtures/java/multiclass",name));
    FILE *f=fopen(path,"rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>0 && n<NV_JAVA_CLASS_MAX);
    *size=n; rewind(f); u8 *bytes=malloc(n); assert(bytes);
    assert(fread(bytes,1,n,f)==(usize)n); assert(!fclose(f)); return bytes;
}
static int load(void *context,const char *name,void *buffer,u32 capacity,u32 *size) {
    struct capture *c=context;
    assert(valid_class_name((const u8 *)name,strlen(name)));
    if (buffer) c->reads++; else c->queries++;
    if (!strcmp(name,"Missing")) return -1;
    bool block=!strcmp(name,"Block");
    if (block && c->fault==1) return -1;
    if (block && c->fault==2 && !buffer) { *size=NV_JAVA_CLASS_MAX+1; return 0; }
    if (block && c->fault==3 && buffer) { *size=capacity-1; return 0; }
    if (block && c->fault==4 && buffer) return -1;
    if (block && c->replacement) {
        *size=c->replacement_size;
        if (buffer) { assert(capacity==*size); memcpy(buffer,c->replacement,*size); }
        return 0;
    }
    char path[256];
    if (nv_java_class_path(path,sizeof(path),"tests/fixtures/java/multiclass",name)) return -1;
    FILE *file=fopen(path,"rb");
    if (!file) return -1; /* Mutations can reference a well-formed missing name. */
    assert(!fseek(file,0,SEEK_END)); long length=ftell(file);
    assert(length>0 && length<NV_JAVA_CLASS_MAX); *size=length; rewind(file);
    if (buffer) { assert(capacity==*size); assert(fread(buffer,1,capacity,file)==capacity); }
    assert(!fclose(file)); return 0;
}
static struct nv_java_result run(const u8 *bytes,u32 size,void *arena,struct capture *c,
                                 const char *arg,const char *main_name) {
    struct nv_java_host host={.context=c,.write=output,.read_class=load,.main_class=main_name};
    struct nv_java_result result; const char *args[]={arg};
    int status=nv_java_run(bytes,size,arena,NV_JAVA_ARENA,args,arg?1:0,10000,&host,&result);
    assert(status==(int)result.status && result.heap_bytes<=NV_JAVA_ARENA);
    return result;
}
int main(void) {
    char path[64];
    assert(!nv_java_class_path(path,sizeof(path),"/home/root","pkg/Entry"));
    assert(!strcmp(path,"/home/root/pkg/Entry.class"));
    assert(!nv_java_class_path(path,sizeof(path),"/home/root/","pkg/Entry"));
    assert(!strcmp(path,"/home/root/pkg/Entry.class"));
    const char *bad[]={"","../Block","pkg/../Block","pkg//Block","pkg\\Block","/Block","pkg/",
                       "C:Block","[LBlock;","Block.class","a\300\200b","a\355\240\200b","a\nb"};
    for (u32 i=0;i<ARRAY_LEN(bad);++i) assert(nv_java_class_path(path,sizeof(path),"/home",bad[i]));
    memset(path,0x5a,sizeof(path)); assert(nv_java_class_path(path,5,"/home","Block") && path[5]==0x5a);
    puts("PASS Java classpath: package paths, traversal/absolute/NUL/surrogate rejection and output bounds");
    u8 *map=mmap(NULL,NV_JAVA_ARENA+8192,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(map!=MAP_FAILED);
    void *arena=map+4096; assert(!mprotect(arena,NV_JAVA_ARENA,PROT_READ|PROT_WRITE));
    u32 main_size,block_size,cold_size,limit_size,package_size;
    u8 *main_bytes=fixture("MultiMain",&main_size),*block=fixture("Block",&block_size),*cold=fixture("Cold",&cold_size);
    u8 *limit=fixture("LimitMain",&limit_size),*package=fixture("pkg/Entry",&package_size);
    struct capture c={0}; struct nv_java_result result=run(main_bytes,main_size,arena,&c,NULL,"MultiMain");
    assert(result.status==NV_JAVA_OK && result.major==65 && c.queries==7 && c.reads==7);
    FILE *wanted=fopen("tests/fixtures/java/multiclass/MultiMain.out","rb"); assert(wanted);
    char expected[2048]; usize n=fread(expected,1,sizeof(expected),wanted); fclose(wanted);
    assert(n==c.used && !memcmp(expected,c.output,n));
    assert(((struct vm *)arena)->class_count==8);
    puts("PASS Java multi-class: independent pools/fields, calls/long returns, lazy/cyclic initialization and cross-class string interning");
    c=(struct capture){0}; assert(run(package,package_size,arena,&c,"two words","pkg/Entry").status==NV_JAVA_OK);
    const char pkg[]="package=21, arg=two words\nPACKAGE PASS\n";
    assert(c.used==sizeof(pkg)-1 && !memcmp(c.output,pkg,sizeof(pkg)-1));
    c=(struct capture){0}; assert(run(main_bytes,main_size,arena,&c,NULL,"Wrong").status==NV_JAVA_VERIFY && !c.queries);
    c=(struct capture){.replacement=cold,.replacement_size=cold_size};
    result=run(main_bytes,main_size,arena,&c,NULL,NULL);
    assert(result.status==NV_JAVA_VERIFY && strstr(result.message,"name does not match"));
    puts("PASS Java loader identity: packaged entry, main-name mismatch and dependency-name spoof rejected");
    const char *modes[]={"store","cast","missing","init-fail"};
    const enum nv_java_status codes[]={NV_JAVA_EXCEPTION,NV_JAVA_EXCEPTION,NV_JAVA_IO,NV_JAVA_EXCEPTION};
    const char *messages[]={"ArrayStoreException","ClassCastException","Class not found","ArithmeticException"};
    for (u32 i=0;i<ARRAY_LEN(modes);++i) {
        c=(struct capture){0}; result=run(main_bytes,main_size,arena,&c,modes[i],NULL);
        assert(result.status==codes[i] && strstr(result.message,messages[i]));
    }
    for (int fault=1;fault<=4;++fault) {
        c=(struct capture){.fault=fault}; result=run(main_bytes,main_size,arena,&c,NULL,NULL);
        assert(result.status==(fault==2?NV_JAVA_FORMAT:NV_JAVA_IO));
    }
    c=(struct capture){0}; result=run(limit,limit_size,arena,&c,NULL,NULL);
    assert(result.status==NV_JAVA_LIMIT && strstr(result.message,"loaded-class limit"));
    assert(c.queries==15 && c.reads==15);
    puts("PASS Java loading limits: missing/oversized/changing/failed reads, 16-class cap, cast/store faults and failing initializer");
    for (u32 size=0;size<block_size;++size) {
        c=(struct capture){.replacement=block,.replacement_size=size};
        assert(run(main_bytes,main_size,arena,&c,NULL,NULL).status!=NV_JAVA_OK);
    }
    u8 *mutated=malloc(block_size); assert(mutated); u32 seed=0x654321;
    for (u32 i=0;i<1000;++i) {
        memcpy(mutated,block,block_size);
        seed=seed*1664525u+1013904223u; u32 at=seed%block_size;
        seed=seed*1664525u+1013904223u; mutated[at]^=1u<<(seed%8);
        c=(struct capture){.replacement=mutated,.replacement_size=block_size};
        assert(run(main_bytes,main_size,arena,&c,NULL,NULL).status<=NV_JAVA_IO);
    }
    puts("PASS Java dependency parser: every truncated prefix and 1000 mutations with guarded arena");
    munmap(map,NV_JAVA_ARENA+8192); free(main_bytes); free(block); free(cold); free(limit); free(package); free(mutated);
    return 0;
}
