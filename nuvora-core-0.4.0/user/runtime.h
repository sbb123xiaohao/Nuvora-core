#ifndef NV_RUNTIME_H
#define NV_RUNTIME_H
#include <nv/abi.h>
#include <nv/string.h>
static inline int call(u32 op, u32 a, u32 b, u32 c) {
    int r;
    __asm__ volatile("int $0x81" : "=a"(r) : "0"(op), "b"(a), "c"(b), "d"(c) : "memory", "cc");
    return r;
}
static inline int emit(int fd, const void *p, u32 n) {
    return call(NV_EMIT, (u32)fd, (uptr)p, n);
}
static inline int take(int fd, void *p, u32 n) {
    return call(NV_TAKE, (u32)fd, (uptr)p, n);
}
static inline int open_file(const char *p, u32 flags) {
    return call(NV_OPEN, (uptr)p, flags, 0);
}
static inline int close_file(int fd) {
    return call(NV_CLOSE, (u32)fd, 0, 0);
}
static inline int seek_file(int fd, i32 off, u32 origin) {
    return call(NV_SEEK, (u32)fd, (u32)off, origin);
}
static inline int surface(u32 op, const struct nv_surface *screen) {
    return call(NV_SURFACE, op, (uptr)screen, 0);
}
static inline int key_event(void) {
    return call(NV_KEY, 0, 0, 0);
}
static inline int usb_controller(u32 index, struct nv_usb_controller *out) {
    return call(NV_USB, NV_USB_CONTROLLERS, index, (uptr)out);
}
static inline int usb_device(u32 index, struct nv_usb_device *out) {
    return call(NV_USB, NV_USB_DEVICES, index, (uptr)out);
}
static inline int usb_scan(void) {
    return call(NV_USB, NV_USB_RESCAN, 0, 0);
}
static inline int replace_file(const char *from, const char *to) {
    return call(NV_REPLACE, (uptr)from, (uptr)to, 0);
}
static inline int list_dir(const char *p, u32 i, struct nv_dirent *e) {
    return call(NV_LIST, (uptr)p, i, (uptr)e);
}
static inline int mkdir_path(const char *p) {
    return call(NV_MKDIR, (uptr)p, 0, 0);
}
static inline int remove_path(const char *p) {
    return call(NV_REMOVE, (uptr)p, 0, 0);
}
static inline int move_path(const char *p, const char *q) {
    return call(NV_MOVE, (uptr)p, (uptr)q, 0);
}
static inline int chdir_path(const char *p) {
    return call(NV_CHDIR, (uptr)p, 0, 0);
}
static inline int getcwd_path(char *p, u32 n) {
    return call(NV_GETCWD, (uptr)p, n, 0);
}
static inline int spawn(const char *p, const char *args) {
    return call(NV_SPAWN, (uptr)p, (uptr)args, 0);
}
/* Success enters the new program and never returns to the caller. */
static inline int exec_program(const char *p, const char *args) {
    return call(NV_EXEC, (uptr)p, (uptr)args, 0);
}
static inline int wait_task(int pid) {
    return call(NV_WAIT, (uptr)pid, 0, 0);
}
static inline int stop_task(int pid) {
    return call(NV_STOP, (uptr)pid, 0, 0);
}
static inline int nap(u32 ms) {
    return call(NV_SLEEP, ms, 0, 0);
}
static inline int yield(void) {
    return call(NV_YIELD, 0, 0, 0);
}
static inline void *grow(i32 pages) {
    return (void *)(iptr)call(NV_GROW, (u32)pages, 0, 0);
}
static inline int info(struct nv_info *p) {
    return call(NV_INFO, (uptr)p, 0, 0);
}
static inline int task_at(u32 i, struct nv_taskinfo *p) {
    return call(NV_TASK, i, (uptr)p, 0);
}
static inline u32 clock_ticks(void) {
    return (u32)call(NV_CLOCK, 0, 0, 0);
}
static inline int control(u32 op, u32 arg) {
    return call(NV_CONTROL, op, arg, 0);
}
static inline NORETURN void finish(int code) {
    call(NV_EXIT, (u32)code, 0, 0);
    for (;;) {
    }
}
void print(const char *);
bool app_help(const char *, const char *);
void println(const char *);
void print_u32(u32);
void print_hex(u32);
void report_error(const char *, int);
const char *error_name(int);
int read_line(char *, u32);
int tokenize(char *, char **, u32);
int join_args(char *, u32, char **, int, int);
int copy_file(const char *, const char *);
#endif
