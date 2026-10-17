#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/linux.c"
struct task tasks[NV_TASK_MAX],*current;
volatile u64 ticks;
u64 clock_realtime_ticks(void){return 170000000000ull+ticks;}
u32 pages_total(void){return 1024;}u32 pages_free(void){return 512;}u32 task_count(void){return 1;}
struct test_page {uptr address;u32 flags;u8 *data;};
struct test_space {struct test_page pages[1024];u32 count;};
static struct test_space primary;
static u32 available=512,allocated,polls,fs_calls;
static int path_error=-NV_EACCESS,read_budget=-1,prepare_budget=-1;
static bool writable=true,random_ok=true,metadata_ok=true;
static bool session_allowed=true,manager;
static int exec_result=-NV_ENOEXEC;
static u32 exec_calls;
static char exec_arguments[NV_ARG_MAX],exec_arg0[NV_PATH_MAX],exec_environment[4096];
static uptr buffer;static usize buffer_size;
static u8 file_data[3*PAGE+13];
static struct test_space *space(pte_t *pd){assert(pd);return (void *)pd;}
static struct test_page *find(pte_t *pd,uptr at) {
    struct test_space *s=space(pd);at&=~(uptr)(PAGE-1);
    for(u32 i=0;i<s->count;i++)if(s->pages[i].address==at)return &s->pages[i];
    return NULL;
}
static bool accepted(uptr at,usize n) {return !n || (at>=buffer && at-buffer<=buffer_size && n<=buffer_size-(at-buffer));}
bool user_range(pte_t *pd,uptr at,usize n,bool write) {
    if(!n)return true;
    if(!nv_user_bounds(at,n))return false;
    if(accepted(at,n))return !write || writable;
    uptr last=(at+n-1)&~(uptr)(PAGE-1);
    for(uptr p=at&~(uptr)(PAGE-1);;p+=PAGE) {
        struct test_page *leaf=find(pd,p);
        if(!leaf || (leaf->flags&P_NOACCESS) || (write && !(leaf->flags&P_WRITE)))return false;
        if(p==last)return true;
    }
}
void *kmalloc(usize n){return metadata_ok?malloc(n):NULL;}
void kfree(void *p){free(p);}
pte_t *vm_create(void){return (void *)calloc(1,sizeof(struct test_space));}
u64 vm_page_count(pte_t *pd){return space(pd)->count;}
int vm_map(pte_t *pd,uptr at,u32 flags) {
    struct test_space *s=space(pd);if(find(pd,at))return -NV_EEXIST;
    if(!available)return -NV_ENOMEM;
    assert(s->count<ARRAY_LEN(s->pages));u8 *data=calloc(1,PAGE);assert(data);
    s->pages[s->count++]=(struct test_page){at,flags,data};--available;++allocated;return 0;
}
void vm_unmap(pte_t *pd,uptr at) {
    struct test_space *s=space(pd);
    for(u32 i=0;i<s->count;i++)if(s->pages[i].address==at) {
        free(s->pages[i].data);s->pages[i]=s->pages[--s->count];++available;--allocated;return;
    }
}
void vm_destroy(pte_t *pd){if(!pd)return;while(space(pd)->count)vm_unmap(pd,space(pd)->pages[0].address);free(pd);}
int vm_protect(pte_t *pd,uptr at,u32 flags){struct test_page *p=find(pd,at);if(!p)return -NV_EFAULT;p->flags=flags;return 0;}
int vm_page_flags(pte_t *pd,uptr at,u32 *flags){struct test_page *p=find(pd,at);if(!p)return -NV_EFAULT;*flags=p->flags;return 0;}
uptr vm_translate(pte_t *pd,uptr at){struct test_page *p=find(pd,at);return p && !(p->flags&P_NOACCESS)?(uptr)p->data-PHYS_WINDOW+(at&(PAGE-1)):0;}
int vm_prepare(pte_t *pd,uptr at){(void)pd;(void)at;if(!prepare_budget)return -NV_ENOMEM;if(prepare_budget>0)--prepare_budget;return 0;}
int vm_move_page(pte_t *dst,pte_t *src,uptr at) {
    struct test_space *s=space(src),*d=space(dst);struct test_page *p=find(src,at);assert(p);
    struct test_page page=*p;u32 index=p-s->pages;s->pages[index]=s->pages[--s->count];
    vm_unmap(dst,at);d->pages[d->count++]=page;return 0;
}
u64 vm_count_range(pte_t *pd,uptr start,uptr end){u64 n=0;struct test_space *s=space(pd);for(u32 i=0;i<s->count;i++)if(s->pages[i].address>=start && s->pages[i].address<end)++n;return n;}
void vm_unmap_range(pte_t *pd,uptr start,uptr end){struct test_space *s=space(pd);for(u32 i=0;i<s->count;)if(s->pages[i].address>=start && s->pages[i].address<end)vm_unmap(pd,s->pages[i].address);else ++i;}
void vm_change_range(pte_t *pd,uptr start,uptr end,u32 flags){struct test_space *s=space(pd);for(u32 i=0;i<s->count;i++)if(s->pages[i].address>=start && s->pages[i].address<end)s->pages[i].flags=flags;}
void vm_clear_range(pte_t *pd,uptr start,uptr end){struct test_space *s=space(pd);for(u32 i=0;i<s->count;i++)if(s->pages[i].address>=start && s->pages[i].address<end)memset(s->pages[i].data,0,PAGE);}
iptr task_grow(i64 change) {
    uptr old=current->heap_end;
    if(change>=0){i64 done=0;for(;done<change;done++)if(vm_map(current->pd,old+done*PAGE,P_WRITE)<0)break;
        if(done!=change){while(done)vm_unmap(current->pd,old+(--done)*PAGE);return -NV_ENOMEM;}}
    else for(i64 i=1;i<=-change;i++)vm_unmap(current->pd,old-i*PAGE);
    current->heap_end=old+change*PAGE;return old;
}
void task_reap(void){}void usb_poll(void){polls++;}void net_poll(void){}
struct frame *schedule(struct frame *f){return f;}void task_exit(int status){current->status=status;}
void task_linux_enter(struct task *t){(void)t;}void task_linux_leave(struct task *t){(void)t;}
iptr task_linux_clone(struct frame *f,u64 a,uptr b,uptr c,uptr d,uptr e){(void)f;(void)a;(void)b;(void)c;(void)d;(void)e;return -L_ENOSYS;}
iptr task_linux_futex(uptr a,u64 b,u64 c,uptr d,uptr e,u64 g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)g;return -L_ENOSYS;}
void task_linux_exit_group(int status){task_exit(status);}
int task_linux_signal(u64 group,u64 tid,u64 signo){if(group!=current->pid || tid!=current->pid)return -3;if(signo>64)return -L_EINVAL;if(signo>=18 && signo<=22)return -L_ENOTSUP;if(signo)current->linux_compat.pending_signals|=1ull<<(signo-1);return 0;}
void arch_set_fs(uptr at){(void)at;}
void cpu_fp_save(struct fp_state *fp){memset(fp,0,sizeof(*fp));}
void cpu_fp_reset(struct fp_state *fp){memset(fp,0,sizeof(*fp));fp->words[6]=0x1f80;}
void cpu_fp_restore(const struct fp_state *fp){assert(!(fp->words[6]&~0xffbfu));}
void panic(const char *text){fprintf(stderr,"%s\n",text);abort();}
void kprintf(const char *text,...){(void)text;}
bool cpu_random(void *out,u32 n){if(!random_ok)return false;memset(out,0x7b,n);return true;}
int user_string(uptr at,char *out,u32 capacity){for(u32 i=0;i<capacity;i++){if(!accepted(at+i,1))return -NV_EFAULT;out[i]=*(const char *)(at+i);if(!out[i])return 0;}return -NV_E2BIG;}
int fs_open(struct task *t,const char *name,u32 flags){(void)t;(void)name;(void)flags;fs_calls++;return -NV_EACCESS;}
int fs_close(struct task *t,int fd){fs_calls++;if(fd<3 || fd>=NV_OPEN_MAX || t->fd[fd].node<0)return -NV_EBADF;t->fd[fd].node=-1;return 0;}
int fs_stat64(struct task *t,int fd,struct nv_stat64 *st){fs_calls++;if(fd==3 && t->fd[fd].node>=0){*st=(struct nv_stat64){.kind=NV_FILE,.size=sizeof(file_data)};return 0;}return -NV_EBADF;}
int fs_pread64(struct task *t,int fd,u64 off,void *out,u32 n){(void)t;assert(fd==3);if(!read_budget)return -NV_EIO;if(read_budget>0)--read_budget;if(off>=sizeof(file_data))return 0;n=MIN(n,sizeof(file_data)-off);memcpy(out,file_data+off,n);return n;}
int fs_stat_path64(struct task *t,const char *name,struct nv_stat64 *st){(void)t;(void)name;fs_calls++;if(path_error)return path_error;*st=(struct nv_stat64){.size=513,.allocated=4096,.kind=NV_FILE};return 0;}
int fs_lookup(int cwd,const char *name){(void)cwd;(void)name;return 3;}
int fs_authorize(const struct task *t,int cwd,const char *name,u32 op){(void)t;(void)cwd;(void)name;(void)op;fs_calls++;return path_error;}
int fs_write(struct task *t,int fd,const void *data,u32 n){(void)t;(void)fd;(void)data;fs_calls++;return n;}
int fs_read(struct task *t,int fd,void *data,u32 n){(void)t;(void)fd;(void)data;fs_calls++;return n;}
int fs_seek64(struct task *t,int fd,struct nv_seek64 *io){(void)t;(void)fd;(void)io;fs_calls++;return -NV_EBADF;}
int fs_path(int node,char *out,usize capacity){(void)node;if(path_error)return path_error;strlcpy(out,"/home/program.elf",capacity);return 0;}
bool account_session_allowed(const struct task *t){(void)t;return session_allowed;}
bool account_manager(const struct task *t){(void)t;return manager;}
int task_exec_linux_start(const char *path,const char *args,u32 options,const struct linux_startup *startup) {
    assert(!strcmp(path,"/home/program.elf"));++exec_calls;
    strlcpy(exec_arguments,args,sizeof(exec_arguments));strlcpy(exec_arg0,startup->arg0,sizeof(exec_arg0));
    memset(exec_environment,0,sizeof(exec_environment));memcpy(exec_environment,startup->environment,startup->environment_bytes);
    if(!exec_result)current->linux_compat=(struct linux_state){.enabled=true,.trace=!!(options&NV_COMPAT_TRACE),.jit=!!(options&NV_COMPAT_JIT)};
    return exec_result;
}
static void reset(void) {
    while(primary.count)vm_unmap((void *)&primary,primary.pages[0].address);
    assert(!allocated);
    memset(tasks,0,sizeof(tasks));current=&tasks[0];current->pid=7;current->abi=2;current->pd=(void *)&primary;
    current->heap_end=NV_USER_HEAP;current->linux_compat.enabled=true;current->linux_compat.brk=NV_USER_HEAP;
    for(u32 i=0;i<NV_OPEN_MAX;i++)current->fd[i].node=-1;
    available=512;buffer=0;buffer_size=0;writable=random_ok=metadata_ok=true;fs_calls=0;read_budget=prepare_budget=-1;
    session_allowed=true;manager=false;exec_result=-NV_ENOEXEC;exec_calls=0;path_error=-NV_EACCESS;
}
static i64 invoke(u64 op,u64 a,u64 b,u64 c){struct frame f={.cs=0x1b,.eax=op,.edi=a,.esi=b,.edx=c};assert(linux_dispatch(&f)==&f);return (i64)f.eax;}
static void mappings(void) {
    for(u32 n=0;n<4;n++){reset();available=n;assert(linux_map(0,4*PAGE,3,0x22,~0ull,0)==-L_ENOMEM && !primary.count && available==n && !allocated);}
    reset();iptr at=linux_map(0,4*PAGE,3,0x22,~0ull,0);assert(at==LINUX_MAP_BASE && primary.count==4);
    memset(find(current->pd,at)->data,0xa6,PAGE);
    assert(!linux_protect(at,PAGE,0) && !user_range(current->pd,at,1,false) && primary.count==4);
    assert(!linux_protect(at,PAGE,3) && find(current->pd,at)->data[0]==0xa6);
    assert(!linux_unmap(at+PAGE,PAGE) && primary.count==3);
    assert(linux_map(at+PAGE,PAGE,3,0x22,~0ull,0)==at+PAGE && primary.count==4);
    assert(!linux_protect(at,4*PAGE,5) && !user_range(current->pd,at,PAGE,true));
    assert(linux_protect(at,4*PAGE,7)==-L_EINVAL && (find(current->pd,at)->flags&P_EXEC));
    assert(linux_protect(0xffff800000000000ull,PAGE,3)==-L_ENOMEM);
    assert(linux_map(at,4*PAGE,3,0x100022,~0ull,0)==-L_EEXIST);
    available=1;assert(linux_map(at,4*PAGE,3,0x32,~0ull,0)==-L_ENOMEM && primary.count==4 && find(current->pd,at)->data[0]==0xa6);
    available=512;prepare_budget=1;assert(linux_map(at,4*PAGE,3,0x32,~0ull,0)==-L_ENOMEM && primary.count==4 && find(current->pd,at)->data[0]==0xa6);
    prepare_budget=-1;assert(linux_map(at,4*PAGE,3,0x32,~0ull,0)==at && primary.count==4 && !find(current->pd,at)->data[0]);
    assert(!linux_unmap(at,4*PAGE) && !primary.count);
    iptr reserved=linux_map(0,LINUX_VIRTUAL_MAX,0,0x22,~0ull,0);assert(reserved==LINUX_MAP_BASE && !primary.count);
    available=1;assert(linux_protect(reserved+PAGE,2*PAGE,3)==-L_ENOMEM && !primary.count);
    available=512;assert(!linux_protect(reserved+PAGE,2*PAGE,3) && primary.count==2);
    memset(find(current->pd,reserved+PAGE)->data,0x6b,PAGE);assert(!linux_advise(reserved+PAGE,PAGE,4) && !find(current->pd,reserved+PAGE)->data[0]);
    assert(!linux_unmap(reserved,LINUX_VIRTUAL_MAX) && !primary.count);
    puts("PASS Linux VMA: retained PROT_NONE data, sparse 16 GiB reservations, commit rollback, fixed replacement rollback, NOREPLACE, RX/W^X and discard");
}
static void files_and_limits(void) {
    reset();current->fd[3]=(struct descriptor){.node=3,.flags=NV_READ,.offset=17};
    for(u32 i=0;i<sizeof(file_data);i++)file_data[i]=(u8)(i*29+(i/PAGE)*13+7);
    iptr at=linux_map(0,3*PAGE,1,2,3,PAGE);assert(at>0 && primary.count==3);
    assert(!memcmp(find(current->pd,at)->data,file_data+PAGE,PAGE));
    assert(find(current->pd,at+2*PAGE)->data[12]==file_data[3*PAGE+12] && !find(current->pd,at+2*PAGE)->data[13]);
    assert(current->fd[3].offset==17 && linux_advise(at,PAGE,4)==-L_ENOTSUP);
    read_budget=1;assert(linux_map(at,3*PAGE,3,0x12,3,0)==-L_EIO && primary.count==3 && find(current->pd,at)->data[0]==file_data[PAGE]);
    reset();for(u32 i=0;i<LINUX_VMAS;i++)assert(linux_map(0,3*PAGE,i%2?1:3,0x22,~0ull,0)>0);
    assert(linux_unmap(LINUX_MAP_BASE+PAGE,PAGE)==-L_ENOMEM && primary.count==3*LINUX_VMAS);
    assert(linux_map(0,PAGE,3,0x22,~0ull,0)==-L_ENOMEM);
    metadata_ok=false;assert(linux_unmap(LINUX_MAP_BASE,PAGE)==-L_ENOMEM);metadata_ok=true;
    reset();assert(linux_brk(0)==NV_USER_HEAP && linux_brk(NV_USER_HEAP+PAGE+7)==NV_USER_HEAP+PAGE+7 && primary.count==2);
    available=0;assert(linux_brk(NV_USER_HEAP+3*PAGE)==NV_USER_HEAP+PAGE+7 && primary.count==2);
    assert(linux_brk(NV_USER_HEAP)==NV_USER_HEAP && !primary.count);
    puts("PASS Linux file mappings: exact offset/EOF zero tail, descriptor position preserved, read-error rollback, metadata exhaustion and byte brk rollback");
}
static void interfaces(void) {
    reset();assert(linux_error(-NV_EACCESS)==-L_EACCES && linux_error(-NV_ENOEXEC)==-8);
    current->linux_compat.enabled=false;assert(invoke(9,0,4096,3)==-L_ENOSYS && !polls);current->linux_compat.enabled=true;
    assert(invoke(999999,0,0,0)==-L_ENOSYS);
    u8 bytes[64];memset(bytes,0x55,sizeof(bytes));buffer=(uptr)bytes;buffer_size=sizeof(bytes);
    assert(invoke(1,0x100000001ull,buffer,1)==-L_EBADF && !fs_calls);
    assert(invoke(1,1,1,16)==-L_EFAULT && !fs_calls);
    assert(invoke(1,1,buffer+63,2)==-L_EFAULT && !fs_calls);
    assert(invoke(1,1,buffer,16)==16 && fs_calls==1);
    writable=false;assert(invoke(0,0,buffer,16)==-L_EFAULT);writable=true;
    assert(invoke(158,0x1002,0xffff800000001000ull,0)==-L_EPERM);
    assert(!invoke(158,0x1002,buffer,0) && !invoke(158,0x1003,buffer,0) && *(uptr *)bytes==buffer);
    random_ok=false;assert(invoke(318,buffer,32,0)==-L_EIO);random_ok=true;
    assert(invoke(318,buffer,32,0)==32 && bytes[31]==0x7b);
    assert(!invoke(3,1,0,0) && invoke(1,1,buffer,1)==-L_EBADF);
    struct {char path[192];struct linux_stat st;char link[32];} io={.path="/home/private"};
    buffer=(uptr)&io;buffer_size=sizeof(io);memset(&io.st,0x55,sizeof(io.st));
    assert(invoke(4,buffer,1,0)==-L_EFAULT);
    assert(invoke(4,buffer,(uptr)&io.st,0)==-L_EACCES && io.st.mode==0x55555555);
    path_error=-NV_ENOENT;assert(invoke(6,buffer,(uptr)&io.st,0)==-L_ENOENT);
    path_error=0;assert(!invoke(4,buffer,(uptr)&io.st,0));
    assert(io.st.mode==(0100000|0600) && io.st.size==513 && io.st.blocks==8 && io.st.inode==4);
    struct frame f={.cs=0x1b,.eax=262,.edi=(u64)-100,.esi=buffer,.edx=(uptr)&io.st,.r10=256};linux_dispatch(&f);assert(!(i64)f.eax);
    f.eax=262;f.r10=4096;linux_dispatch(&f);assert((i64)f.eax==-L_EINVAL);
    assert(invoke(89,buffer,(uptr)io.link,32)==-L_EINVAL);path_error=-NV_EACCESS;
    assert(invoke(89,buffer,(uptr)io.link,32)==-L_EACCES);
    puts("PASS Linux ABI: errno, high descriptors/user pointers, TLS/random/stdio and authorized path metadata/readlink");
}
static void process_interfaces(void) {
    reset();
    struct {char path[64],arg0[32],first[32],empty[1],env[64];uptr argv[4],envp[2];u64 mask,old;} io={
        .path="program.elf",.arg0="custom name",.first="a b\"c\\d",.env="LD_LIBRARY_PATH=/home/jdk/lib"};
    io.argv[0]=(uptr)io.arg0;io.argv[1]=(uptr)io.first;io.argv[2]=(uptr)io.empty;io.envp[0]=(uptr)io.env;
    buffer=(uptr)&io;buffer_size=sizeof(io);
    path_error=0;assert(!invoke(21,(uptr)io.path,0,0));
    assert(invoke(21,(uptr)io.path,8,0)==-L_EINVAL);
    path_error=-NV_EACCESS;assert(invoke(21,(uptr)io.path,2,0)==-L_EACCES);path_error=0;
    strlcpy(io.path,"/proc/self/exe",sizeof(io.path));memset(io.first,'?',sizeof(io.first));
    assert(invoke(89,(uptr)io.path,(uptr)io.first,5)==5 && !memcmp(io.first,"/home?",6));
    strlcpy(io.path,"program.elf",sizeof(io.path));strlcpy(io.first,"a b\"c\\d",sizeof(io.first));
    io.mask=~0ull;
    struct frame f={.cs=0x1b,.eax=14,.edi=2,.esi=(uptr)&io.mask,.edx=(uptr)&io.old,.r10=8};
    linux_dispatch(&f);assert(!(i64)f.eax && !io.old);
    u64 mask=~((1ull<<8)|(1ull<<18));assert(current->linux_compat.signal_mask==mask);
    f.eax=14;f.esi=1;linux_dispatch(&f);assert((i64)f.eax==-L_EFAULT && current->linux_compat.signal_mask==mask);
    current->linux_compat.closed_stdio=2;current->linux_compat.cloexec=(1u<<2)|(1u<<3);current->linux_compat.nonblock=1u<<3;
    current->fd[3]=(struct descriptor){.node=4,.flags=NV_READ};current->linux_compat.jit=true;
    assert(invoke(59,buffer,(uptr)io.argv,(uptr)io.envp)==-8 && exec_calls==1 && current->fd[3].node==4);
    assert(current->linux_compat.cloexec==12 && current->linux_compat.signal_mask==mask);
    assert(!strcmp(exec_arg0,"custom name") && !strcmp(exec_arguments,"\"a b\\\"c\\\\d\" \"\"") && !strcmp(exec_environment,io.env));
    io.argv[1]=1;assert(invoke(59,buffer,(uptr)io.argv,(uptr)io.envp)==-L_EFAULT && exec_calls==1);io.argv[1]=(uptr)io.first;
    session_allowed=false;assert(invoke(59,buffer,(uptr)io.argv,(uptr)io.envp)==-L_EACCES);session_allowed=true;
    manager=true;assert(invoke(59,buffer,(uptr)io.argv,(uptr)io.envp)==-L_EACCES);manager=false;
    exec_result=0;assert(!invoke(59,buffer,(uptr)io.argv,(uptr)io.envp));
    assert(current->linux_compat.closed_stdio==6 && !current->linux_compat.cloexec && !current->linux_compat.nonblock);
    assert(current->fd[3].node==-1 && current->linux_compat.signal_mask==mask && current->linux_compat.jit);
    iptr at=linux_map(0,PAGE,7,0x22,~0ull,0);assert(at==LINUX_MAP_BASE);
    assert(find(current->pd,at)->flags==(P_WRITE|P_EXEC|P_JIT));
    assert(!linux_protect(at,PAGE,5));current->linux_compat.jit=false;assert(linux_protect(at,PAGE,7)==-L_EINVAL);
    puts("PASS Linux process ABI: bounded argv/environment, exec failure preservation, session denial, close-on-exec, signal masks, proc executable and opt-in JIT");
}
static void signal_contexts(void) {
    reset();
    u8 stack[8192] ALIGNED(64);buffer=(uptr)stack;buffer_size=sizeof(stack);
    struct linux_action *a=(void *)stack;
    *a=(struct linux_action){.handler=buffer+64,.restorer=buffer+128,.flags=L_SA_RESTORER|L_SA_SIGINFO};
    assert(!linux_sigaction(4,buffer,0,8));
    assert(linux_sigaction(9,buffer,0,8)==-L_EINVAL && linux_sigaction(4,1,0,8)==-L_EFAULT);
    a->handler=0xffff800000001000ull;assert(linux_sigaction(4,buffer,0,8)==-L_EINVAL);
    assert(current->linux_compat.actions[3].handler==buffer+64);
    struct frame f={.cs=0x1b,.ss=0x23,.eip=buffer+256,.useresp=buffer+sizeof(stack)-16,
                    .eax=0x123456789abcdef0ull,.edi=55,.eflags=0x202,.vector=6};
    assert(linux_fault(&f,0) && f.eip==buffer+64 && f.edi==4 && f.useresp%16==8);
    assert(current->linux_compat.signal_depth==1 && current->linux_compat.signal_mask==8);
    struct linux_signal_frame *saved=(void *)f.useresp;
    assert(saved->uc.context.rax==0x123456789abcdef0ull && saved->uc.context.rdi==55);
    struct fp_state *fp=(void *)saved->uc.context.fpstate;fp->words[6]=~0u;
    saved->uc.context.cs=8;f.useresp+=8;
    assert(linux_sigreturn(&f)==-L_EFAULT && current->linux_compat.signal_depth==1);
    saved->uc.context.cs=0x1b;saved->uc.context.eflags=~0ull;
    assert(!linux_sigreturn(&f) && f.eip==buffer+256 && f.eax==0x123456789abcdef0ull && f.edi==55);
    assert(!(f.eflags&0x3000) && f.cs==0x1b && f.ss==0x23 && !current->linux_compat.signal_mask);
    assert(!current->linux_compat.signal_depth && current->fp.words[6]==0xffbf);
    assert(linux_self_signal(current->pid+1,10)==-3 && linux_self_signal(current->pid,65)==-L_EINVAL);
    assert(linux_self_signal(current->pid,19)==-L_ENOTSUP);
    current->linux_compat.signal_mask=1ull<<9;assert(!linux_self_signal(current->pid,10));
    assert(!linux_pending(&f) && current->linux_compat.pending_signals==(1ull<<9));
    current->linux_compat.signal_mask=0;current->linux_compat.actions[9].handler=1;
    assert(!linux_pending(&f) && !current->linux_compat.pending_signals);
    puts("PASS Linux signals: validated actions/context, retained masked signals, user-only returns, sanitized flags/MXCSR and default/ignored delivery");
}
int main(void){mappings();files_and_limits();interfaces();process_interfaces();signal_contexts();reset();return 0;}
