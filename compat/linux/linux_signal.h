#ifndef NV_LINUX_SIGNAL_H
#define NV_LINUX_SIGNAL_H
/* Linux x86-64 signal frame ABI. Only self-directed and synchronous signals
 * are provided here; no process/thread-group signalling is implied. */
#define L_SA_SIGINFO 4ull
#define L_SA_RESTORER 0x04000000ull
#define L_SA_ONSTACK 0x08000000ull
#define L_SA_NODEFER 0x40000000ull
#define L_SA_RESETHAND 0x80000000ull
struct linux_context {
    u64 r8,r9,r10,r11,r12,r13,r14,r15,rdi,rsi,rbp,rbx,rdx,rax,rcx,rsp,rip,eflags;
    u16 cs,gs,fs,ss;
    u64 error,trap,mask,address,fpstate,reserved[8];
};
struct linux_ucontext {u64 flags,link;struct linux_sigstack stack;struct linux_context context;u64 mask;};
struct linux_signal_frame {uptr restorer;struct linux_ucontext uc;u8 info[128];};
_Static_assert(sizeof(struct linux_context)==256 && sizeof(struct linux_ucontext)==304,"Linux signal context layout");
static bool linux_on_altstack(uptr sp) {
    const struct linux_sigstack *s=&current->compat_state.signal_stack;
    return s->pointer && !(s->flags&2) && sp>=s->pointer && sp-s->pointer<s->size;
}
static iptr linux_sigaction(u64 signo,uptr input,uptr output,u64 size) {
    if(!signo || signo>64 || signo==9 || signo==19 || size!=8)return -L_EINVAL;
    if((input && !user_range(current->pd,input,sizeof(struct linux_action),false)) ||
       (output && !user_range(current->pd,output,sizeof(struct linux_action),true)))return -L_EFAULT;
    struct linux_action next,old=current->compat_state.actions[signo-1];
    if(input) {
        memcpy(&next,(const void *)input,sizeof(next));
        if(next.flags&~(3ull|L_SA_SIGINFO|L_SA_RESTORER|L_SA_ONSTACK|0x10000000ull|L_SA_NODEFER|L_SA_RESETHAND))return -L_EINVAL;
        if(next.handler>1 && (!nv_user_address(next.handler) || !(next.flags&L_SA_RESTORER) || !nv_user_address(next.restorer)))return -L_EINVAL;
        next.mask&=~((1ull<<8)|(1ull<<18));
    }
    if(output)memcpy((void *)output,&old,sizeof(old));
    if(input)current->compat_state.actions[signo-1]=next;
    return 0;
}
static iptr linux_sigaltstack(uptr input,uptr output,uptr sp) {
    if((input && !user_range(current->pd,input,sizeof(struct linux_sigstack),false)) ||
       (output && !user_range(current->pd,output,sizeof(struct linux_sigstack),true)))return -L_EFAULT;
    bool active=linux_on_altstack(sp);
    struct linux_sigstack next,old=current->compat_state.signal_stack;
    old.flags=active?1:(!old.pointer || (old.flags&2))?2:0;
    if(input) {
        if(active)return -L_EPERM;
        memcpy(&next,(const void *)input,sizeof(next));
        if(next.flags&~2u)return -L_EINVAL;
        if(next.flags&2)next=(struct linux_sigstack){.flags=2};
        else if(next.size<2048)return -L_ENOMEM;
        else if(!nv_user_bounds(next.pointer,next.size))return -L_EFAULT;
    }
    if(output)memcpy((void *)output,&old,sizeof(old));
    if(input)current->compat_state.signal_stack=next;
    return 0;
}
static bool linux_deliver(struct frame *f,u32 signo,i32 code,uptr address) {
    struct compat_state *s=&current->compat_state;struct linux_action action=s->actions[signo-1];
    if(action.handler<=1 || (s->signal_mask&(1ull<<(signo-1))) || s->signal_depth==4)return false;
    uptr top=f->useresp;
    if((action.flags&L_SA_ONSTACK) && !linux_on_altstack(top) && s->signal_stack.pointer && !(s->signal_stack.flags&2))
        top=s->signal_stack.pointer+s->signal_stack.size;
    if(top<128+sizeof(struct fp_state)+sizeof(struct linux_signal_frame)+32)return false;
    uptr fp=(top-128-sizeof(struct fp_state))&~(uptr)63;
    uptr at=((fp-sizeof(struct linux_signal_frame))&~(uptr)15)-8;
    if(!user_range(current->pd,at,top-at,true))return false;
    struct linux_signal_frame out={.restorer=action.restorer};
    out.uc.stack=s->signal_stack;out.uc.stack.flags=linux_on_altstack(f->useresp)?1:out.uc.stack.pointer?0:2;
    out.uc.mask=s->signal_mask;
    out.uc.context=(struct linux_context){f->r8,f->r9,f->r10,f->r11,f->r12,f->r13,f->r14,f->r15,
        f->edi,f->esi,f->ebp,f->ebx,f->edx,f->eax,f->ecx,f->useresp,f->eip,f->eflags,
        (u16)f->cs,0,0,(u16)f->ss,f->error,f->vector,s->signal_mask,address,fp,{0}};
    memcpy(out.info,&signo,4);memcpy(out.info+8,&code,4);memcpy(out.info+16,&address,8);
    if(code<=0) {u32 pid=current->compat_group?current->compat_group->pid:current->pid;memcpy(out.info+16,&pid,4);memcpy(out.info+20,&current->uid,4);}
    cpu_fp_save(&current->fp);memcpy((void *)fp,&current->fp,sizeof(current->fp));memcpy((void *)at,&out,sizeof(out));
    cpu_fp_reset(&current->fp);cpu_fp_restore(&current->fp);
    if(action.flags&L_SA_RESETHAND)s->actions[signo-1]=(struct linux_action){0};
    s->signal_mask|=action.mask;
    if(!(action.flags&L_SA_NODEFER))s->signal_mask|=1ull<<(signo-1);
    ++s->signal_depth;
    f->eip=action.handler;f->useresp=at;f->edi=signo;
    f->esi=at+__builtin_offsetof(struct linux_signal_frame,info);
    f->edx=at+__builtin_offsetof(struct linux_signal_frame,uc);
    f->eax=0;f->eflags&=~(0x100ull|0x400ull);return true;
}
static int linux_sigreturn(struct frame *f) {
    uptr at=f->useresp-8;
    if(!current->compat_state.signal_depth || !user_range(current->pd,at,sizeof(struct linux_signal_frame),false))return -L_EFAULT;
    struct linux_signal_frame in;memcpy(&in,(const void *)at,sizeof(in));
    struct linux_context *c=&in.uc.context;
    if(c->cs!=0x1b || c->ss!=0x23 || !nv_user_address(c->rip) || !nv_user_address(c->rsp) ||
       (c->fpstate && !user_range(current->pd,c->fpstate,sizeof(struct fp_state),false)))return -L_EFAULT;
    if(c->fpstate) {
        struct fp_state fp;memcpy(&fp,(const void *)c->fpstate,sizeof(fp));
        /* Reserved MXCSR bits would fault in supervisor FXRSTOR. SSE2's
         * architectural mask is conservative across the supported CPUs. */
        fp.words[6]&=0xffbfu;memcpy(&current->fp,&fp,sizeof(fp));cpu_fp_restore(&current->fp);
    }
    *f=(struct frame){.r8=c->r8,.r9=c->r9,.r10=c->r10,.r11=c->r11,.r12=c->r12,.r13=c->r13,.r14=c->r14,.r15=c->r15,
        .edi=c->rdi,.esi=c->rsi,.ebp=c->rbp,.ebx=c->rbx,.edx=c->rdx,.eax=c->rax,.ecx=c->rcx,
        .eip=c->rip,.useresp=c->rsp,.cs=0x1b,.ss=0x23,.eflags=(c->eflags&0x210dd5ull)|0x202};
    current->compat_state.signal_mask=in.uc.mask&~((1ull<<8)|(1ull<<18));--current->compat_state.signal_depth;
    return 0;
}
bool linux_fault(struct frame *f,uptr address) {
    u32 signo=f->vector==6?4:f->vector==0 || f->vector==16 || f->vector==19?8:f->vector==1 || f->vector==3?5:11;
    bool caught=linux_deliver(f,signo,signo==11?(f->error&1)?2:1:1,address);
    if(caught)compat_leave(current);
    return caught;
}
static iptr linux_self_signal(u64 pid,u64 signo) {
    return task_linux_signal(current->compat_group?current->compat_group->pid:current->pid,pid,signo);
}
static void linux_sleep_return(struct frame *f) {
    struct compat_state *s=&current->compat_state;
    if(!s->sleep_interrupted)return;
    if(s->sleep_remaining) {
        if(!user_range(current->pd,s->sleep_remaining,16,true))f->eax=(u64)(i64)-L_EFAULT;
        else {
            i64 remaining[2]={(i64)(s->sleep_left/100),(i64)(s->sleep_left%100)*10000000};
            memcpy((void *)s->sleep_remaining,remaining,sizeof(remaining));
        }
    }
    s->sleep_interrupted=false;s->sleep_remaining=0;s->sleep_left=0;
}
static bool linux_pending(struct frame *f) {
    struct compat_state *s=&current->compat_state;
    linux_sleep_return(f);
    for(u32 signo=1;signo<=64;++signo) {
        u64 bit=1ull<<(signo-1);
        if(!(s->pending_signals&bit) || (s->signal_mask&bit))continue;
        s->pending_signals&=~bit;
        if(linux_signal_ignored(s->actions[signo-1],signo))continue;
        linux_interrupt_wait(current);linux_sleep_return(f);
        if(!linux_deliver(f,signo,-6,0)) {task_linux_exit_group(128+signo);return true;}
        break;
    }
    return false;
}
bool linux_resume(struct frame *f) {
    bool stopped=linux_pending(f);if(!stopped)compat_leave(current);return stopped;
}
#endif
