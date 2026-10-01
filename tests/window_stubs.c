/* Filesystem fixtures without a desktop keep the ordinary console path. The
 * actual window service has its own boundary and lifecycle fixture. */
#include <nv/abi.h>
struct task;
__attribute__((weak)) struct task *current;
__attribute__((weak)) void account_task_release(u32 pid) { (void)pid; }
__attribute__((weak)) bool account_path_allowed(const struct task *t,const char *path,u32 op) {
    (void)t;(void)path;(void)op;return true;
}
__attribute__((weak)) bool account_task_allowed(const struct task *t) { (void)t;return true; }
__attribute__((weak)) bool account_interactive_allowed(const struct task *t) { (void)t;return true; }
__attribute__((weak)) bool account_manager(const struct task *t) { (void)t;return false; }
__attribute__((weak)) bool account_admin_allowed(const struct task *t) { (void)t;return true; }
__attribute__((weak)) bool account_session_allowed(const struct task *t) { (void)t;return true; }
__attribute__((weak)) void account_tick(void) {}
__attribute__((weak)) void account_input_activity(void) {}
__attribute__((weak)) int window_stdio_read(struct task *t, void *p, u32 n) {
    (void)t; (void)p; (void)n; return -NV_ENODEV;
}
__attribute__((weak)) int window_stdio_write(struct task *t, const void *p, u32 n) {
    (void)t; (void)p; (void)n; return -NV_ENODEV;
}
