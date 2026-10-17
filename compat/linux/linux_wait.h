#ifndef NV_LINUX_WAIT_H
#define NV_LINUX_WAIT_H
/* Shared by syscall delivery and the task lifecycle. Never dereference another
 * thread's user pointer here: its address space may not be active. */
static bool linux_signal_ignored(struct linux_action action,u32 signo) {
    return action.handler==1 || (!action.handler && (signo==17 || signo==23 || signo==28));
}
static void linux_interrupt_wait(struct task *t) {
    struct compat_state *s=&t->compat_state;
    if(t->state!=NV_SLEEPING || (!s->futex_address && !s->sleeping))return;
    if(s->sleeping) {
        s->sleep_left=t->wake>ticks?t->wake-ticks:0;
        s->sleeping=false;s->sleep_interrupted=true;
    }
    s->futex_address=0;t->state=NV_READY;t->frame->eax=(u64)(i64)-L_EINTR;
}
#endif
