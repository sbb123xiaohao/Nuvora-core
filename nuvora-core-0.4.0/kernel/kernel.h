#ifndef NV_KERNEL_H
#define NV_KERNEL_H
#include <nv/abi.h>
#include <nv/string.h>
#define PAGE 4096u
#define PHYS_LIMIT (128u * 1024u * 1024u)
#define USER_BASE 0x40000000u
#define USER_IMAGE_END 0x41000000u
#define USER_HEAP 0x50000000u
#define USER_HEAP_END 0x50400000u
#define USER_STACK_TOP 0x7fff0000u
#define USER_STACK_PAGES 8u
#define KSTACK_SIZE 16384u
#define KSTACK_BASE 0x10000000u
#define KSTACK_PAGES (KSTACK_SIZE / PAGE)
#define KSTACK_STRIDE (KSTACK_PAGES + 2u)
#define MMIO_BASE 0x20000000u
#define P_PRESENT 1u
#define P_WRITE 2u
#define P_USER 4u
#define P_EXEC 8u
#ifdef __x86_64__
#define NV_ARCH_NAME "x86-64"
typedef u64 pte_t;
#else
#define NV_ARCH_NAME "i686"
typedef u32 pte_t;
#endif
#define FS_NODES 128
#define SNAP_CAP (1024u * 1024u)
static inline void outb(u16 p, u8 v) {
    __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p));
}
static inline u8 inb(u16 p) {
    u8 v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void outw(u16 p, u16 v) {
    __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(p));
}
static inline u16 inw(u16 p) {
    u16 v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void outl(u16 p, u32 v) {
    __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p));
}
static inline u32 inl(u16 p) {
    u32 v;
    __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static inline void irq_disable(void) {
    __asm__ volatile("cli" ::: "memory");
}
static inline void irq_enable(void) {
    __asm__ volatile("sti" ::: "memory");
}
static inline void idle_once(void) {
    __asm__ volatile("sti; hlt; cli" ::: "memory");
}
static inline uptr irq_save(void) {
    uptr f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f)::"memory");
    return f;
}
static inline void irq_restore(uptr f) {
    __asm__ volatile("push %0; popf" ::"r"(f) : "memory", "cc");
}
static inline void load_cr3(uptr p) {
    __asm__ volatile("mov %0,%%cr3" ::"r"(p) : "memory");
}
#ifdef __x86_64__
struct frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, edi, esi, ebp, edx, ecx, ebx, eax;
    u64 vector, error, eip, cs, eflags, useresp, ss;
};
_Static_assert(sizeof(struct frame) == 176, "64-bit interrupt frame");
#else
struct frame {
    u32 gs, fs, es, ds, edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 vector, error, eip, cs, eflags, useresp, ss;
};
_Static_assert(sizeof(struct frame) == 76, "interrupt frame");
#endif
struct multiboot {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4], mmap_length, mmap_addr;
} PACKED;
struct mmap_entry {
    u32 size;
    u64 base, len;
    u32 type;
} PACKED;
struct descriptor {
    int node;
    u32 offset, flags;
};
struct task {
    u32 pid, parent, state, wake, wait_pid, cpu_ticks, heap_end;
    int status, cwd;
    bool collected;
    pte_t *pd;
    void *kstack;
    struct frame *frame;
    struct descriptor fd[NV_OPEN_MAX];
    char name[32];
};
extern struct task tasks[NV_TASK_MAX];
extern struct task *current;
extern volatile u32 ticks;
extern pte_t *kernel_pd;
extern bool test_mode;
void console_init(void);
void console_clear(void);
void console_putc(char);
void console_write(const char *, usize);
void kprintf(const char *, ...);
NORETURN void panic(const char *);
int console_getc(void);
int console_key(u32);
int console_surface(u32, u32, const struct nv_surface *);
bool console_owned(u32);
void console_release(u32);
void keyboard_irq(void);
void console_usb_key(u8, u8);
void usb_init(void);
void usb_poll(void);
int usb_rescan(void);
int usb_controller_info(u32, struct nv_usb_controller *);
int usb_device_info(u32, struct nv_usb_device *);
void arch_init(void);
void arch_set_stack(uptr);
void vm_kernel_init(void);
void *vm_stack_alloc(u32);
void vm_stack_free(void *);
void *vm_mmio_map(u64, u32);
NORETURN void arch_guard_probe(uptr);
NORETURN void arch_resume(struct frame *);
struct frame *interrupt_dispatch(struct frame *);
void memory_init(const struct multiboot *);
u32 page_alloc(void);
void page_free(u32);
u32 pages_free(void);
u32 pages_total(void);
void *kmalloc(usize);
void kfree(void *);
u32 heap_used(void);
u32 heap_total(void);
void memory_selftest(void);
pte_t *vm_create(void);
void vm_destroy(pte_t *);
int vm_map(pte_t *, u32, u32);
u32 vm_translate(pte_t *, u32);
void vm_unmap(pte_t *, u32);
bool user_range(pte_t *, u32, u32, bool);
int user_string(u32, char *, u32);
int copy_to_space(pte_t *, u32, const void *, u32);
u32 vm_page_count(pte_t *);
void fs_init(void);
void fs_unpack(const u8 *, usize);
int fs_lookup(int, const char *);
int fs_kind(int);
int fs_open(struct task *, const char *, u32);
int fs_close(struct task *, int);
int fs_read(struct task *, int, void *, u32);
int fs_write(struct task *, int, const void *, u32);
int fs_seek(struct task *, int, i32, u32);
int fs_list(int, const char *, u32, struct nv_dirent *);
int fs_mkdir(int, const char *);
int fs_remove(int, const char *);
int fs_move(int, const char *, const char *);
int fs_replace(int, const char *, const char *);
int fs_path(int, char *, usize);
int fs_blob(const char *, const u8 **, u32 *);
u32 fs_node_count(void);
int fs_export_home(u8 *, u32, u32 *);
int fs_import_home(const u8 *, u32);
bool disk_init(void);
bool disk_ready(void);
int disk_read(u32, void *);
int disk_write(u32, const void *);
int disk_flush(void);
void store_init(void);
int store_sync(void);
u32 store_generation(void);
void task_init(void);
int task_spawn(const char *, const char *, struct task *);
int task_exec(const char *, const char *);
void task_reap(void);
NORETURN void task_start(int);
struct frame *schedule(struct frame *);
void task_tick(void);
void task_exit(int);
int task_wait(u32);
int task_stop(u32);
int task_grow(i32);
u32 task_count(void);
int task_info(u32, struct nv_taskinfo *);
bool task_cwd_in_use(int);
struct frame *syscall_dispatch(struct frame *);
void fill_info(struct nv_info *);
NORETURN void machine_poweroff(void);
NORETURN void machine_reboot(void);
#endif
