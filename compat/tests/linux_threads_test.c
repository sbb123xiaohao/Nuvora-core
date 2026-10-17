#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/kernel.h"
#include "../../kernel/task.c"
volatile u64 ticks;
static u8 area[16384] ALIGNED(16);
static struct frame parent_frame,shell_frame;
static bool stack_ok=true,heap_ok=true;
static u32 stacks,groups,destroyed,closed;
bool user_range(pte_t *pd,uptr at,usize size,bool write) {
    (void)write;return pd==(pte_t *)1 && at>=(uptr)area && at-(uptr)area<=sizeof(area) && size<=sizeof(area)-(at-(uptr)area);
}
int copy_to_space(pte_t *pd,uptr at,const void *source,usize size){assert(user_range(pd,at,size,true));memcpy((void *)at,source,size);return 0;}
void *vm_stack_alloc(u32 slot){(void)slot;if(!stack_ok)return NULL;++stacks;return aligned_alloc(16,KSTACK_SIZE);}
void vm_stack_free(void *p){if(p){assert(stacks);--stacks;free(p);}}
void vm_destroy(pte_t *pd){if(pd){assert(pd==(pte_t *)1);++destroyed;}}
void *kmalloc(usize size){if(!heap_ok)return NULL;++groups;return malloc(size);}
void kfree(void *p){if(p){assert(groups);--groups;free(p);}}
void panic(const char *message){fprintf(stderr,"%s\n",message);abort();}
void kprintf(const char *format,...){(void)format;}
void cpu_fp_save(struct fp_state *fp){memset(fp,0x33,sizeof(*fp));}
void console_release(u32 pid){(void)pid;}void window_task_release(u32 pid){(void)pid;}void net_task_release(u32 pid){(void)pid;}
int fs_close(struct task *t,int fd){assert(fd>=3 && t->fd[fd].node>=0);++closed;t->fd[fd].node=-1;return 0;}
u64 clock_realtime_ticks(void){return 170000000000ull+ticks;}
static void setup(void) {
    assert(!stacks && !groups);memset(tasks,0,sizeof(tasks));memset(area,0,sizeof(area));
    tasks[0]=(struct task){.pid=1,.state=NV_READY,.frame=&shell_frame};
    current=&tasks[1];*current=(struct task){.pid=7,.parent=1,.state=NV_READY,.abi=2,.uid=1000,.pd=(pte_t *)1,
        .heap_end=NV_USER_HEAP,.frame=&parent_frame,.compat_state={.enabled=true,.brk=NV_USER_HEAP}};
    for(u32 i=0;i<NV_OPEN_MAX;++i)current->fd[i].node=-1;
    current->fd[3]=(struct descriptor){.node=11,.flags=NV_READ};
    current->kstack=vm_stack_alloc(1);next_pid=20;destroyed=closed=0;stack_ok=heap_ok=true;ticks=1000;
    parent_frame=(struct frame){.eip=0x100001000ull,.cs=0x1b,.ss=0x23,.eflags=0x202};
}
static struct task *clone_worker(void) {
    iptr pid=task_linux_clone(current->frame,0x7d0f00,(uptr)area+sizeof(area)-8,(uptr)area,(uptr)area+8,(uptr)area+128);
    assert(pid>0);struct task *child=by_pid(pid);assert(child && child!=current);
    assert(child->pd==current->pd && child->compat_group==current->compat_group && child->frame->eax==0);
    assert(child->frame->eip==parent_frame.eip && child->compat_state.fs_base==(uptr)area+128);
    assert(*(u32 *)area==(u32)pid && child->compat_state.clear_tid==(uptr)area+8);
    assert(!child->compat_state.sleeping && !child->compat_state.sleep_interrupted && !child->compat_state.sleep_remaining);
    return child;
}
static void sleep_signals(void) {
    setup();struct task *parent=current,*child=clone_worker();
    parent->state=NV_SLEEPING;parent->wake=ticks+1000;
    parent->compat_state.sleeping=true;parent->compat_state.sleep_remaining=(uptr)area;
    memset(area,0xa5,16);current=child;
    assert(!task_linux_signal(7,7,17) && parent->state==NV_SLEEPING);
    parent->compat_state.signal_mask=1ull<<9;
    assert(!task_linux_signal(7,7,10) && parent->state==NV_SLEEPING);
    parent->compat_state.signal_mask=0;ticks+=23;
    assert(!task_linux_signal(7,7,10) && parent->state==NV_READY && (i64)parent_frame.eax==-L_EINTR);
    assert(parent->compat_state.sleep_interrupted && !parent->compat_state.sleeping && parent->compat_state.sleep_left==977);
    for(u32 i=0;i<16;++i)assert(area[i]==0xa5); /* Output belongs to the target's address space. */
    ticks+=10;assert(parent->compat_state.sleep_left==977);
    parent->compat_state.sleep_interrupted=false;parent->compat_state.pending_signals=0;
    parent->compat_state.sleeping=true;parent->state=NV_SLEEPING;parent->wake=ticks+5;
    ticks+=5;task_tick();
    assert(parent->state==NV_READY && !parent->compat_state.sleeping && !parent->compat_state.sleep_remaining);
    parent->compat_state.sleeping=true;parent->state=NV_SLEEPING;parent->wake=ticks+1000;
    assert(!task_linux_signal(7,7,9) && parent->state==NV_READY && (parent->compat_state.pending_signals&(1ull<<8)));
    task_linux_exit_group(0);current=&tasks[0];task_reap();
    assert(!stacks && !groups && destroyed==1);
    puts("PASS Linux sleep lifecycle: ignored/blocked signals retain waits, caught signals and SIGKILL wake promptly, remainder captures interrupt time, normal deadline clears wait state");
}
int main(void) {
    sleep_signals();
    setup();
    assert(task_linux_clone(&parent_frame,0,(uptr)area,0,0,0)==-L_ENOTSUP);
    assert(task_linux_clone(&parent_frame,0x7d0f00,1,(uptr)area,(uptr)area+8,(uptr)area+128)==-L_EFAULT);
    stack_ok=false;assert(task_linux_clone(&parent_frame,0x7d0f00,(uptr)area,(uptr)area,(uptr)area+8,(uptr)area+128)==-L_ENOMEM);stack_ok=true;
    heap_ok=false;assert(task_linux_clone(&parent_frame,0x7d0f00,(uptr)area,(uptr)area,(uptr)area+8,(uptr)area+128)==-L_ENOMEM);heap_ok=true;
    assert(stacks==1 && !groups && !current->compat_group);
    struct task *parent=current,*child=clone_worker();struct compat_group *group=parent->compat_group;
    assert(stacks==2 && groups==1 && group->references==2 && group->alive==2);
    current=child;compat_enter(current);current->fd[3].offset=99;current->cwd=12;
    current->compat_state.maps[0]=(struct linux_vma){LINUX_MAP_BASE,LINUX_MAP_BASE+PAGE,3,32};compat_leave(current);
    current=parent;compat_enter(current);assert(current->fd[3].offset==99 && current->cwd==12 && current->compat_state.maps[0].start==LINUX_MAP_BASE);
    uptr address=(uptr)area+32;*(u32 *)address=1;
    assert(task_linux_futex(address,128,2,0,0,0)==-L_EAGAIN);
    assert(!task_linux_futex(address,128,1,0,0,0) && parent->state==NV_SLEEPING);
    current=child;assert(task_linux_futex(address,129,1,0,0,0)==1 && parent->state==NV_READY);
    current=parent;assert(!task_linux_futex(address,128,1,0,0,0));current=child;
    uptr other=address+4;
    assert(task_linux_futex(address,132,0,1,other,2)==-L_EAGAIN && parent->compat_state.futex_address==address);
    assert(task_linux_futex(address,132,0,1,1,1)==-L_EFAULT && parent->compat_state.futex_address==address);
    assert(task_linux_futex(address,132,0,1,other,1)==1 && parent->compat_state.futex_address==other);
    assert(task_linux_futex(address,129,1,0,0,0)==0 && task_linux_futex(other,129,1,0,0,0)==1);
    current=parent;struct {i64 seconds,nanoseconds;} *timeout=(void *)(area+64);*timeout=(__typeof__(*timeout)){0,20000000};
    assert(!task_linux_futex(address,128,1,(uptr)timeout,0,0));ticks+=2;task_tick();assert(parent->state==NV_READY && (i64)parent_frame.eax==-L_ETIMEDOUT);
    assert(!task_linux_futex(address,128,1,0,0,0));current=child;
    assert(!task_linux_signal(7,7,17) && parent->state==NV_SLEEPING && parent->compat_state.futex_address==address);
    parent->compat_state.pending_signals=0;
    assert(!task_linux_signal(7,7,10) && parent->state==NV_READY && (i64)parent_frame.eax==-L_EINTR);
    assert(task_linux_signal(1,1,10)==-3 && task_linux_signal(7,7,65)==-L_EINVAL);
    *(u32 *)(area+8)=child->pid;current=parent;assert(!task_linux_futex((uptr)area+8,0,child->pid,0,0,0));
    current=child;task_exit(0);assert(!*(u32 *)(area+8) && parent->state==NV_READY && group->alive==1);
    current=parent;task_reap();assert(group->references==1 && !destroyed && stacks==1);
    parent->compat_state.pending_signals=0;child=clone_worker();
    tasks[0].state=NV_WAITING;tasks[0].wait_pid=7;
    current=child;task_linux_exit_group(0);
    assert(!group->alive && tasks[0].state==NV_READY && !shell_frame.eax && parent->collected);
    current=&tasks[0];task_reap();assert(!stacks && !groups && destroyed==1 && closed==1);
    setup();parent=current;child=clone_worker();(void)child;
    current=&tasks[0];assert(!task_stop(7));task_reap();assert(destroyed==1 && !stacks && !groups);
    assert(task_wait(7)==143);task_reap();assert(!parent->state);
    setup();parent=current;child=clone_worker();task_exit(0);
    assert(parent->state==NV_ZOMBIE && parent->compat_group->alive==1);
    current=&tasks[0];task_reap();assert(parent->pd==(pte_t *)1 && !destroyed);
    assert(task_wait(7)==-4096);current=child;task_linux_exit_group(17);
    assert(tasks[0].state==NV_READY && shell_frame.eax==17);
    current=&tasks[0];task_reap();assert(!stacks && !groups && destroyed==1);
    puts("PASS Linux threads: clone validation/OOM rollback, separate kernel stacks/TLS, shared metadata, futex wake/timeout/EINTR, clear-TID join, whole-group stop/exit and single final VM release");
}
