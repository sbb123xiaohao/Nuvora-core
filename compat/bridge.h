#ifndef NV_COMPAT_BRIDGE_H
#define NV_COMPAT_BRIDGE_H
/* These hooks use native process, VM and filesystem services. No foreign
 * runtime, boot image, network service or package is needed by the base OS. */
int compat_spawn(const char *,const char *,struct task *,u32);
int compat_exec(const char *,const char *,u32);
int compat_exec_start(const char *,const char *,u32,const struct compat_startup *);
#if NV_ENABLE_COMPAT
struct frame *linux_dispatch(struct frame *);
bool linux_fault(struct frame *,uptr);
bool linux_resume(struct frame *);
void compat_enter(struct task *);
void compat_leave(struct task *);
iptr task_linux_clone(struct frame *,u64,uptr,uptr,uptr,uptr);
iptr task_linux_futex(uptr,u64,u64,uptr,uptr,u64);
int task_linux_signal(u64,u64,u64);
void task_linux_exit_group(int);
static inline struct frame *compat_dispatch(struct frame *f) {return linux_dispatch(f);}
static inline bool compat_fault(struct frame *f,uptr address) {
    return current->compat_state.enabled && linux_fault(f,address);
}
static inline bool compat_resume(struct frame *f) {
    return current->compat_state.enabled && linux_resume(f);
}
static inline void compat_exit_group(int status) {
    if(current->compat_state.enabled)task_linux_exit_group(status);else task_exit(status);
}
#else
static inline void compat_enter(struct task *t) {(void)t;}
static inline void compat_leave(struct task *t) {(void)t;}
static inline struct frame *compat_dispatch(struct frame *f) {f->eax=(u64)(i64)-38;return f;}
static inline bool compat_fault(struct frame *f,uptr address) {(void)f;(void)address;return false;}
static inline bool compat_resume(struct frame *f) {(void)f;return false;}
static inline void compat_exit_group(int status) {task_exit(status);}
#endif
#endif
