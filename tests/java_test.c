#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../user/java_vm.c"
#include "../user/runtime.c"

struct capture { char text[1024]; u32 length; bool discard, cancel, error; };
static int output(void *context,const char *text,u32 length) {
    struct capture *c=context;
    if (c->error) return -1;
    if (c->discard) return 0;
    if (length>sizeof(c->text)-c->length) return -1;
    memcpy(c->text+c->length,text,length); c->length+=length; return 0;
}
static int poll_host(void *context) { return ((struct capture *)context)->cancel; }
static u8 *read_file(const char *path,u32 *length) {
    FILE *f=fopen(path,"rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long size=ftell(f); assert(size>0 && size<NV_JAVA_CLASS_MAX);
    rewind(f); u8 *bytes=malloc(size); assert(bytes);
    assert(fread(bytes,1,size,f)==(usize)size); assert(!fclose(f));
    *length=size; return bytes;
}
static u32 random_state=0x21c0ffee;
static u32 random_u32(void) { random_state^=random_state<<13; random_state^=random_state>>17; random_state^=random_state<<5; return random_state; }
static struct nv_java_result run(u8 *bytes,u32 length,void *arena,struct capture *capture,const char *arg,u32 budget) {
    struct nv_java_host host={capture,output,poll_host}; struct nv_java_result result;
    const char *args[]={arg};
    int status=nv_java_run(bytes,length,arena,NV_JAVA_ARENA,args,arg?1:0,budget,&host,&result);
    assert(status==(int)result.status); assert(result.heap_bytes<=NV_JAVA_ARENA);
    return result;
}
int main(void) {
    char command[128],*tokens[6];
    char *quoted[]={"", "two words", "a\"b", "C:\\path", "'literal'", "tab\there"};
    assert(!quote_args(command,sizeof(command),quoted,0,6));
    assert(tokenize(command,tokens,6)==6);
    for (u32 i=0;i<6;++i) assert(!strcmp(tokens[i],quoted[i]));
    memset(command,0x5a,sizeof(command));
    assert(quote_args(command,4,quoted,0,6)==-NV_E2BIG && command[4]==0x5a);
    assert(quote_args(command,0,quoted,0,1)==-NV_EINVAL);
    puts("PASS Java launch arguments: empty strings, whitespace, quotes, backslashes and capacity bounds");
    void *mapping=mmap(NULL,NV_JAVA_ARENA+8192,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(mapping!=MAP_FAILED); void *arena=(u8 *)mapping+4096;
    assert(!mprotect(arena,NV_JAVA_ARENA,PROT_READ|PROT_WRITE));
    u32 length,wanted_length,edge_length;
    u8 *bytes=read_file("tests/fixtures/java/NuvoraJavaDemo.class",&length);
    u8 *wanted=read_file("tests/fixtures/java/NuvoraJavaDemo.out",&wanted_length);
    u8 *edges=read_file("tests/fixtures/java/JavaEdges.class",&edge_length);
    struct capture captured={0};
    struct nv_java_result result=run(bytes,length,arena,&captured,NULL,NV_JAVA_STEPS);
    assert(result.status==NV_JAVA_OK && result.major==65 && result.instructions>100000);
    assert(captured.length==wanted_length && !memcmp(captured.text,wanted,wanted_length));
    puts("PASS Java 21 real javac output: classes, objects, arrays, switches, overflow, UTF-16 and invokedynamic concat");
    const char *faults[]={"divide","long-divide","null","bounds","store-bounds","negative","store","cast","char-bounds"};
    for (u32 i=0;i<ARRAY_LEN(faults);++i) {
        captured=(struct capture){.discard=true}; result=run(edges,edge_length,arena,&captured,faults[i],100000);
        assert(result.status==NV_JAVA_EXCEPTION);
    }
    const char *limits[]={"memory","recursive","forever","library"};
    const enum nv_java_status statuses[]={NV_JAVA_MEMORY,NV_JAVA_LIMIT,NV_JAVA_LIMIT,NV_JAVA_UNSUPPORTED};
    for (u32 i=0;i<ARRAY_LEN(limits);++i) {
        result=run(edges,edge_length,arena,&captured,limits[i],10000); assert(result.status==statuses[i]);
    }
    captured=(struct capture){.error=true}; assert(run(bytes,length,arena,&captured,NULL,NV_JAVA_STEPS).status==NV_JAVA_IO);
    captured=(struct capture){.discard=true,.cancel=true}; assert(run(bytes,length,arena,&captured,NULL,NV_JAVA_STEPS).status==NV_JAVA_IO);
    puts("PASS Java errors: divide/null/array/cast/string faults, memory/depth/instruction limits and host cancellation");
    captured=(struct capture){.discard=true};
    for (u32 n=0;n<length;++n) assert(run(bytes,n,arena,&captured,NULL,2000).status!=NV_JAVA_OK);
    /* Guard both the class input and the arena; truncate without accessible
     * bytes after EOF. Parser errors must not touch either guard page. */
    usize mapped=(length+4095)&~4095u;
    u8 *guard=mmap(NULL,mapped+4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); assert(guard!=MAP_FAILED);
    assert(!mprotect(guard+mapped,4096,PROT_NONE));
    u8 *exact=guard+mapped-length; memcpy(exact,bytes,length);
    assert(run(exact,length,arena,&captured,NULL,NV_JAVA_STEPS).status==NV_JAVA_OK);
    u8 *mutated=malloc(length); assert(mutated);
    for (u32 i=0;i<1500;++i) {
        memcpy(mutated,bytes,length);
        for (u32 j=0,n=1+random_u32()%8;j<n;++j) mutated[random_u32()%length]^=1u<<(random_u32()%8);
        result=run(mutated,length,arena,&captured,NULL,2000);
        assert(result.status<=NV_JAVA_IO);
    }
    puts("PASS Java parser: every truncated prefix, 1500 deterministic mutations and input/heap guard pages");
    /* Verify branch boundaries independently of javac's well-formed output. */
    struct vm *v=arena; memset(v,0,sizeof(*v));
    result=(struct nv_java_result){0}; v->result=&result; v->arena=arena; v->capacity=NV_JAVA_ARENA; v->used=(sizeof(*v)+15)&~15u;
    const u8 operand_branch[]={0x10,0x01,0xa7,0xff,0xff,0xb1};
    struct method bad={.code=operand_branch,.length=sizeof(operand_branch)};
    assert(!decode(v,&bad) && result.status==NV_JAVA_VERIFY);
    result=(struct nv_java_result){0};
    const u8 truncated_switch[]={0xaa,0,0,0,0,0,0,0};
    bad=(struct method){.code=truncated_switch,.length=sizeof(truncated_switch)};
    assert(!decode(v,&bad) && result.status==NV_JAVA_FORMAT);
    result=(struct nv_java_result){0}; v->methods[0]=(struct method){.stack=4,.locals=4};
    struct frame *frame=&v->frames[0]; memset(frame,0,sizeof(*frame));
    assert(local_store(v,frame,0,wide(123)) && local_store(v,frame,1,integer(5)));
    assert(!local_load(v,frame,0,T_LONG) && result.status==NV_JAVA_VERIFY);
    result=(struct nv_java_result){0};
    assert(push(v,frame,wide(2)) && !stack_op(v,frame,0x59) && result.status==NV_JAVA_VERIFY);
    result=(struct nv_java_result){0}; frame->used=0; v->methods[0].stack=0;
    assert(local_store(v,frame,0,integer(0xffffffffu)) && increment(v,frame,0,1) && frame->locals[0].bits==0);
    puts("PASS Java bytecode safety: branch-into-operand, truncated switch, wide-local invalidation, stack categories and zero-stack iinc");
    const u8 parent_code[]={0x3b,0xb1}, child_code[]={0x03,0x02,0xac};
    const char *return_types[]={"()B","()C","()S","()Z"};
    const u32 narrowed[]={0xffffffffu,65535,0xffffffffu,1};
    for (u32 i=0;i<4;++i) {
        result=(struct nv_java_result){0};v->pool_count=3;v->limit=20;v->depth=2;
        v->pool[1]=(struct cp){.text=(const u8 *)"()V",.length=3,.tag=1};
        v->pool[2]=(struct cp){.text=(const u8 *)return_types[i],.length=3,.tag=1};
        v->methods[0]=(struct method){.code=parent_code,.length=2,.stack=1,.locals=1,.desc=1};
        v->methods[1]=(struct method){.code=child_code,.length=3,.stack=2,.desc=2};
        memset(v->frames,0,2*sizeof(struct frame));v->frames[1].method=1;
        assert(decode(v,&v->methods[0]) && decode(v,&v->methods[1]) && execute(v));
        assert(v->frames[0].locals[0].bits==narrowed[i]);
    }
    puts("PASS JVMS return semantics: extra operands discarded and byte/char/short/boolean narrowed");
    munmap(guard,mapped+4096); munmap(mapping,NV_JAVA_ARENA+8192);
    free(bytes); free(edges); free(wanted); free(mutated);
    return 0;
}
