#ifndef NV_LINUX_THREADS_H
#define NV_LINUX_THREADS_H
#include "linux_errno.h"
#include "linux_wait.h"
/* A serialized kernel shares process metadata at entry/exit. TLS, signal
 * masks/stacks, pending signals, TID and floating-point state stay per task. */
void compat_enter(struct task *t) {
    struct compat_group *g=t->compat_group;if(!g)return;
    memcpy(t->compat_state.maps,g->maps,sizeof(g->maps));memcpy(t->compat_state.actions,g->actions,sizeof(g->actions));
    memcpy(t->fd,g->fd,sizeof(g->fd));t->cwd=g->cwd;t->heap_end=g->heap_end;t->compat_state.brk=g->brk;
    t->compat_state.closed_stdio=g->closed_stdio;t->compat_state.cloexec=g->cloexec;t->compat_state.nonblock=g->nonblock;
}
static void compat_group_update(struct compat_group *g,const struct task *t) {
    memcpy(g->maps,t->compat_state.maps,sizeof(g->maps));memcpy(g->actions,t->compat_state.actions,sizeof(g->actions));
    memcpy(g->fd,t->fd,sizeof(g->fd));g->cwd=t->cwd;g->heap_end=t->heap_end;g->brk=t->compat_state.brk;
    g->closed_stdio=t->compat_state.closed_stdio;g->cloexec=t->compat_state.cloexec;g->nonblock=t->compat_state.nonblock;
}
void compat_leave(struct task *t) {
    struct compat_group *g=t->compat_group;if(!g)return;
    compat_group_update(g,t);
    /* Native filesystem ownership checks inspect each task's descriptors and
     * current directory, so keep those views current even for sleeping tasks. */
    for(u32 i=0;i<NV_TASK_MAX;++i)if(tasks[i].compat_group==g && &tasks[i]!=t) {
        memcpy(tasks[i].fd,g->fd,sizeof(g->fd));tasks[i].cwd=g->cwd;tasks[i].heap_end=g->heap_end;
    }
}
static void linux_space_release(struct compat_group *g,pte_t *pd) {
    if(!g) {vm_destroy(pd);return;}
    if(!g->references)panic("Linux address-space reference underflow");
    if(!--g->references) {vm_destroy(pd);kfree(g);}
}
static u32 linux_futex_wake(pte_t *pd,uptr address,u32 count,u32 bitset) {
    u32 woken=0;
    for(u32 i=0;i<NV_TASK_MAX && woken<count;++i) {
        struct task *t=&tasks[i];
        if(t->pd!=pd || t->state!=NV_SLEEPING || t->compat_state.futex_address!=address ||
           !(t->compat_state.futex_bitset&bitset))continue;
        t->compat_state.futex_address=0;t->state=NV_READY;t->frame->eax=0;++woken;
    }
    return woken;
}
static void linux_clear_tid(struct task *t) {
    uptr at=t->compat_state.clear_tid;if(!at)return;
    if(user_range(t->pd,at,4,true)) {u32 zero=0;copy_to_space(t->pd,at,&zero,4);}
    linux_futex_wake(t->pd,at,1,~0u);t->compat_state.clear_tid=0;
}
iptr task_linux_clone(struct frame *f,u64 flags,uptr stack,uptr parent_tid,uptr child_tid,uptr tls) {
    const u64 required=0x10f00,allowed=required|0x40000|0x80000|0x100000|0x200000|0x400000|0x1000000;
    if((flags&required)!=required || (flags&~allowed))return -L_ENOTSUP;
    if(!stack || !user_range(current->pd,stack,8,true) ||
       ((flags&0x100000) && !user_range(current->pd,parent_tid,4,true)) ||
       ((flags&(0x200000|0x1000000)) && !user_range(current->pd,child_tid,4,true)) ||
       ((flags&0x80000) && !nv_user_address(tls)))return -L_EFAULT;
    struct task *child=NULL;
    for(u32 i=0;i<NV_TASK_MAX;++i)if(!tasks[i].state) {child=&tasks[i];break;}
    if(!child)return -L_EAGAIN;
    void *kstack=vm_stack_alloc((u32)(child-tasks));if(!kstack)return -L_ENOMEM;
    struct compat_group *group=current->compat_group;
    if(!group) {
        group=kmalloc(sizeof(*group));
        if(!group) {vm_stack_free(kstack);return -L_ENOMEM;}
        memset(group,0,sizeof(*group));group->pid=current->pid;group->references=group->alive=1;
        compat_group_update(group,current);
    }
    cpu_fp_save(&current->fp);*child=*current;
    child->compat_group=group;current->compat_group=group;++group->references;++group->alive;
    child->pid=allocate_pid();child->parent=group->pid;child->collected=true;child->cpu_ticks=0;
    child->kstack=kstack;child->frame=(void *)((u8 *)kstack+KSTACK_SIZE-sizeof(struct frame));
    *child->frame=*f;child->frame->eax=0;child->frame->useresp=stack;
    child->compat_state.fs_base=(flags&0x80000)?tls:current->compat_state.fs_base;
    child->compat_state.pending_signals=0;child->compat_state.signal_depth=0;
    child->compat_state.signal_stack=(struct linux_sigstack){.flags=2};
    child->compat_state.clear_tid=(flags&0x200000)?child_tid:0;child->compat_state.futex_address=0;
    child->compat_state.sleep_remaining=0;child->compat_state.sleep_left=0;
    child->compat_state.sleeping=child->compat_state.sleep_interrupted=false;
    child->wait_pid=0;child->wake=0;child->status=0;
    if(flags&0x100000)memcpy((void *)parent_tid,&child->pid,4);
    if(flags&0x1000000)memcpy((void *)child_tid,&child->pid,4);
    child->state=NV_READY;return child->pid;
}
iptr task_linux_futex(uptr address,u64 operation,u64 value,uptr timeout,uptr second,u64 bitset) {
    if(address%4 || !user_range(current->pd,address,4,false))return -L_EFAULT;
    if(operation&~(128ull|256ull|15ull))return -L_ENOSYS;
    u32 op=operation&15,mask=(op==9 || op==10)?(u32)bitset:~0u;
    if((operation&256) && op!=9)return -L_ENOSYS;
    if(!mask || (value>0x7fffffffull && (op==1 || op==10)))return -L_EINVAL;
    if(op==1 || op==10)return linux_futex_wake(current->pd,address,value,mask);
    if(op==3 || op==4) {
        if(value>0x7fffffffull || timeout>0x7fffffffull || second==address)return -L_EINVAL;
        if(second%4 || !user_range(current->pd,second,4,false))return -L_EFAULT;
        if(op==4 && *(const u32 *)address!=(u32)bitset)return -L_EAGAIN;
        u32 count=linux_futex_wake(current->pd,address,value,~0u),moved=0;
        for(u32 i=0;i<NV_TASK_MAX && moved<timeout;++i) {
            struct task *t=&tasks[i];
            if(t->pd==current->pd && t->state==NV_SLEEPING && t->compat_state.futex_address==address) {
                t->compat_state.futex_address=second;++moved;
            }
        }
        return count+moved;
    }
    if(op!=0 && op!=9)return -L_ENOSYS;
    if(*(const u32 *)address!=(u32)value)return -L_EAGAIN;
    u64 wake=~0ull;
    if(timeout) {
        struct {i64 seconds,nanoseconds;} duration;
        if(!user_range(current->pd,timeout,sizeof(duration),false))return -L_EFAULT;
        memcpy(&duration,(const void *)timeout,sizeof(duration));
        if(duration.seconds<0 || duration.nanoseconds<0 || duration.nanoseconds>=1000000000 ||
           (u64)duration.seconds>(~0ull/100)-1)return -L_EINVAL;
        u64 amount=(u64)duration.seconds*100+((u64)duration.nanoseconds+9999999)/10000000;
        if(op==9) {
            u64 now=(operation&256)?clock_realtime_ticks():ticks;
            if(amount<=now)return -L_ETIMEDOUT;
            amount-=now;
        }
        wake=amount>~0ull-ticks?~0ull:ticks+amount;
        if(wake<=ticks)return -L_ETIMEDOUT;
    }
    current->compat_state.futex_address=address;current->compat_state.futex_bitset=mask;
    current->wake=wake;current->state=NV_SLEEPING;return 0;
}
int task_linux_signal(u64 group_pid,u64 tid,u64 signo) {
    u64 own=current->compat_group?current->compat_group->pid:current->pid;
    if(group_pid!=own || tid>0x7fffffffull)return -3;
    struct task *target=by_pid((u32)tid);
    if(!target || !target->compat_state.enabled || target->pd!=current->pd || target->state==NV_ZOMBIE)return -3;
    if(signo>64)return -L_EINVAL;
    if(signo>=18 && signo<=22)return -L_ENOTSUP;
    if(signo) {
        target->compat_state.pending_signals|=1ull<<(signo-1);
        if(!(target->compat_state.signal_mask&(1ull<<(signo-1))) &&
           !linux_signal_ignored(current->compat_state.actions[signo-1],signo))linux_interrupt_wait(target);
    }
    return 0;
}
void task_linux_exit_group(int status) {
    struct compat_group *group=current->compat_group;
    if(!group) {task_exit(status);return;}
    struct task *leader=by_pid(group->pid);if(leader)leader->status=status;
    /* The leader remains waitable until every member has stopped. */
    for(u32 i=0;i<NV_TASK_MAX;++i)if(tasks[i].compat_group==group && tasks[i].state && tasks[i].state!=NV_ZOMBIE)
        finish(&tasks[i],status);
}
#endif
