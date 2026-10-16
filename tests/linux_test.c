#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/linux.c"
struct task tasks[NV_TASK_MAX],*current;
volatile u64 ticks;
static struct {uptr address;u32 flags;} pages[256];
static u32 count,available=256,polls,fs_calls;
static uptr buffer;static usize buffer_size;static bool writable=true,random_ok=true;
static bool accepted(uptr address,usize size) {
    if (!size) return true;
    return address>=buffer && address-buffer<=buffer_size && size<=buffer_size-(address-buffer);
}
bool user_range(pte_t *pd,uptr address,usize size,bool write) {
    (void)pd;if(!size)return true;if(!nv_user_bounds(address,size))return false;
    if(accepted(address,size))return !write||writable;
    if(size>LINUX_MEMORY_MAX)return false;
    uptr last=(address+size-1)&~(uptr)(PAGE-1);
    for(uptr at=address&~(uptr)(PAGE-1);;at+=PAGE) {
        u32 i;for(i=0;i<count;i++)if(pages[i].address==at && (!write || (pages[i].flags&P_WRITE)))break;
        if(i==count)return false;
        if(at==last)return true;
    }
}
u64 vm_page_count(pte_t *pd) {(void)pd;return count;}
int vm_map(pte_t *pd,uptr address,u32 flags) {
    (void)pd;for(u32 i=0;i<count;i++)if(pages[i].address==address)return -NV_EEXIST;
    if(!available)return -NV_ENOMEM;
    assert(count<ARRAY_LEN(pages));available--;
    pages[count].address=address;pages[count++].flags=flags;return 0;
}
void vm_unmap(pte_t *pd,uptr address) {
    (void)pd;for(u32 i=0;i<count;i++)if(pages[i].address==address) {pages[i]=pages[--count];available++;return;}
}
int vm_protect(pte_t *pd,uptr address,u32 flags) {
    (void)pd;for(u32 i=0;i<count;i++)if(pages[i].address==address) {pages[i].flags=flags;return 0;}return -NV_EFAULT;
}
iptr task_grow(i64 change) {
    uptr old=current->heap_end;
    if(change>=0) {
        i64 done=0;for(;done<change;done++)if(vm_map(current->pd,old+done*PAGE,P_WRITE)<0)break;
        if(done!=change) {while(done)vm_unmap(current->pd,old+(--done)*PAGE);return -NV_ENOMEM;}
    } else for(i64 i=1;i<=-change;i++)vm_unmap(current->pd,old-i*PAGE);
    current->heap_end=old+change*PAGE;return old;
}
void task_reap(void){}void usb_poll(void){polls++;}void net_poll(void){}
struct frame *schedule(struct frame *f){return f;}
void task_exit(int status){current->status=status;}
void arch_set_fs(uptr address){(void)address;}
void panic(const char *text){fprintf(stderr,"%s\n",text);abort();}
bool cpu_random(void *out,u32 n){if(!random_ok)return false;memset(out,0x7b,n);return true;}
int user_string(uptr address,char *out,u32 capacity) {
    for(usize i=0;i<capacity;i++){if(!accepted(address+i,1))return -NV_EFAULT;out[i]=*(const char *)(address+i);if(!out[i])return 0;}
    return -NV_E2BIG;
}
int fs_open(struct task *t,const char *name,u32 flags){(void)t;(void)name;(void)flags;fs_calls++;return -NV_EACCESS;}
int fs_close(struct task *t,int fd){(void)t;(void)fd;fs_calls++;return -NV_EBADF;}
int fs_stat64(struct task *t,int fd,struct nv_stat64 *st){(void)t;(void)fd;(void)st;fs_calls++;return -NV_EBADF;}
int fs_write(struct task *t,int fd,const void *data,u32 n){(void)t;(void)fd;(void)data;fs_calls++;return n;}
int fs_read(struct task *t,int fd,void *data,u32 n){(void)t;(void)fd;(void)data;fs_calls++;return n;}
int fs_seek64(struct task *t,int fd,struct nv_seek64 *io){(void)t;(void)fd;(void)io;fs_calls++;return -NV_EBADF;}
int fs_path(int node,char *out,usize capacity){(void)node;(void)out;(void)capacity;return -NV_ENOENT;}
static void reset(void) {
    memset(tasks,0,sizeof(tasks));current=&tasks[0];current->pid=7;current->abi=2;
    current->heap_end=NV_USER_HEAP;current->linux_compat.enabled=true;current->linux_compat.brk=NV_USER_HEAP;
    for(u32 i=0;i<NV_OPEN_MAX;i++)current->fd[i].node=-1;
    count=0;available=256;buffer=0;buffer_size=0;writable=true;random_ok=true;fs_calls=0;
}
static i64 invoke(u64 op,u64 a,u64 b,u64 c) {
    struct frame f={.cs=0x1b,.eax=op,.edi=a,.esi=b,.edx=c};assert(linux_dispatch(&f)==&f);return (i64)f.eax;
}
int main(void) {
    reset();assert(linux_error(-NV_EACCESS)==-L_EACCES && linux_error(-NV_ENOEXEC)==-8 && linux_error(-NV_EFAULT)==-L_EFAULT);
    current->linux_compat.enabled=false;assert(invoke(9,0,4096,3)==-L_ENOSYS && !polls);current->linux_compat.enabled=true;
    assert(invoke(999999,0,0,0)==-L_ENOSYS);
    for(u32 n=0;n<4;n++) {
        reset();available=n;assert(linux_map(0,4*PAGE,3,0x22,~0ull,0)==-L_ENOMEM && !count && available==n);
        for(u32 i=0;i<LINUX_VMAS;i++)assert(!current->linux_compat.maps[i].start);
    }
    reset();iptr at=linux_map(0,4*PAGE,3,0x22,~0ull,0);assert(at==LINUX_MAP_BASE && count==4);
    assert(!linux_unmap(at+PAGE,PAGE));assert(count==3 && user_range(NULL,at,1,true) && user_range(NULL,at+2*PAGE,1,true));
    assert(linux_map(at+PAGE,PAGE,3,0x22,~0ull,0)==at+PAGE && count==4);
    assert(!linux_protect(at,4*PAGE,1));assert(!user_range(NULL,at,PAGE,true));
    assert(!linux_protect(at,4*PAGE,5) && (pages[0].flags&P_EXEC));
    assert(linux_protect(at,4*PAGE,7)==-L_EINVAL && (pages[0].flags&P_EXEC));
    assert(linux_protect(0xffff800000000000ull,PAGE,3)==-L_ENOMEM);
    assert(!linux_unmap(at,4*PAGE) && !count);
    puts("PASS Linux VMA: allocation rollback, split/reuse, R/RW/RX permissions, W+X denial and supervisor rejection");
    reset();for(u32 i=0;i<LINUX_VMAS;i++)assert(linux_map(0,3*PAGE,3,0x22,~0ull,0)>0);
    assert(linux_unmap(LINUX_MAP_BASE+PAGE,PAGE)==-L_ENOMEM && count==3*LINUX_VMAS);
    assert(linux_map(0,PAGE,3,0x22,~0ull,0)==-L_ENOMEM);
    assert(linux_unmap(0,PAGE)==-L_EINVAL);
    assert(linux_map(0,0,3,0x22,~0ull,0)==-L_EINVAL);
    assert(linux_map(0,PAGE,3,0x32,~0ull,0)==-L_EINVAL);
    assert(linux_map(0,LINUX_MEMORY_MAX+1ull,3,0x22,~0ull,0)==-L_EINVAL);
    reset();assert(linux_brk(0)==NV_USER_HEAP);assert(linux_brk(NV_USER_HEAP+PAGE+7)==NV_USER_HEAP+PAGE+7 && count==2);
    available=0;assert(linux_brk(NV_USER_HEAP+3*PAGE)==NV_USER_HEAP+PAGE+7 && count==2);
    assert(linux_brk(NV_USER_HEAP)==NV_USER_HEAP && !count);
    puts("PASS Linux quotas: 32 mappings, split failure is atomic, fixed mappings rejected and byte brk rollback");
    reset();u8 bytes[64];memset(bytes,0x55,sizeof(bytes));buffer=(uptr)bytes;buffer_size=sizeof(bytes);
    assert(invoke(1,0x100000001ull,buffer,1)==-L_EBADF && !fs_calls);
    assert(invoke(1,1,1,16)==-L_EFAULT && !fs_calls);
    assert(invoke(1,1,buffer+63,2)==-L_EFAULT && !fs_calls);
    assert(invoke(1,1,buffer,16)==16 && fs_calls==1);
    writable=false;assert(invoke(0,0,buffer,16)==-L_EFAULT);writable=true;
    assert(invoke(158,0x1002,0xffff800000001000ull,0)==-L_EPERM && !current->linux_compat.fs_base);
    assert(!invoke(158,0x1002,buffer,0));assert(!invoke(158,0x1003,buffer,0));assert(*(uptr *)bytes==buffer);
    assert(invoke(318,buffer,32,4)==-L_EINVAL);random_ok=false;
    memset(bytes,0x55,sizeof(bytes));assert(invoke(318,buffer,32,0)==-L_EIO && bytes[0]==0x55);
    random_ok=true;assert(invoke(318,buffer,32,0)==32 && bytes[31]==0x7b && bytes[32]==0x55);
    assert(!invoke(3,1,0,0) && invoke(1,1,buffer,1)==-L_EBADF && invoke(3,1,0,0)==-L_EBADF);
    puts("PASS Linux ABI: native process gate, errno translation, wide descriptor/pointer checks, FS-base isolation, random errors and closed stdio");
    return 0;
}
