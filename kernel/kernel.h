#ifndef NV_KERNEL_H
#define NV_KERNEL_H
#ifndef __x86_64__
#error "kernel/ implements x86_64; the ARM64 bring-up lives in arch/aarch64"
#endif
#include <nv/abi.h>
#include <nv/acpi.h>
#include <nv/string.h>
#include <nv/bootinfo.h>
#define PAGE 4096u
/* Managed RAM ceiling. x64 uses 2 MiB mappings above the protected kernel
 * image and uptr-wide physical page addresses. */
#ifdef __x86_64__
#define PHYS_LIMIT (64ull * 1024 * 1024 * 1024) /* 64 GiB */
#else
#define PHYS_LIMIT (192u * 1024u * 1024u)
#endif
#define USER_BASE 0x40000000u
#define USER_IMAGE_END 0x41000000u
#define USER_HEAP 0x50000000u
/* ABI 1 keeps user pointers below 2 GiB; give model buffers 512 MiB of that
 * window while retaining a separate user stack and executable image. */
#define USER_HEAP_END 0x70000000u
#define USER_STACK_TOP 0x7fff0000u
#define USER_STACK_PAGES 8u
#define KHEAP_SIZE (8u * 1024u * 1024u)
#define KSTACK_SIZE 16384u
#define KSTACK_BASE 0x10000000u
#define KSTACK_PAGES (KSTACK_SIZE / PAGE)
#define KSTACK_STRIDE (KSTACK_PAGES + 2u)
#define MMIO_BASE 0x20000000u
/* Firmware framebuffer window: 8 MiB of kernel VA below KSTACK_BASE, backed by
 * four dedicated page tables. The covered physical RAM is left unmapped. */
#define FB_WINDOW 0x0F800000u
#define FB_WINDOW_PAGES (8u * 1024u * 1024u / PAGE)
#define P_PRESENT 1u
#define P_WRITE 2u
#define P_USER 4u
#define P_EXEC 8u
#ifdef __x86_64__
#define NV_ARCH_NAME "x86-64"
typedef u64 pte_t;
#define P_ADDRESS 0x000ffffffffff000ull
#define PHYS_WINDOW (1ull << 39)
#define KHEAP_WINDOW (2ull << 39)
#else
#define NV_ARCH_NAME "i686"
typedef u32 pte_t;
#define P_ADDRESS 0xfffff000u
#endif
/* One continuous supervisor alias for all allocated RAM. Switching aliases
 * at 1 GiB breaks multi-page buffers that cross the user-address boundary.
 * Preserve zero as the allocation-failure sentinel; physical page 0 is reserved. */
static inline void *phys_ptr(uptr p) {
#ifdef __x86_64__
    if (p)
        p += PHYS_WINDOW;
#endif
    return (void *)p;
}
static inline uptr ptr_phys(const void *ptr) {
    uptr p = (uptr)ptr;
#ifdef __x86_64__
    if (p >= PHYS_WINDOW && p - PHYS_WINDOW < PHYS_LIMIT)
        p -= PHYS_WINDOW;
#endif
    return p;
}
#define FS_NODES 128
#define SNAP_CAP_MAX (16u * 1024u * 1024u) /* also limited by the disk slot size */
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
    p = ptr_phys((const void *)p);
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
struct fp_state {
    u32 words[128];
} ALIGNED(16);
struct task {
    u32 pid, parent, state, wake, wait_pid, cpu_ticks, heap_end;
    int status, cwd;
    bool collected;
    pte_t *pd;
    void *kstack;
    struct frame *frame;
    struct descriptor fd[NV_OPEN_MAX];
    char name[32];
    struct fp_state fp;
};
_Static_assert(_Alignof(struct task) >= 16 && sizeof(struct fp_state) == 512,
               "each task requires an aligned FXSAVE area");
extern struct task tasks[NV_TASK_MAX];
extern struct task *current;
extern volatile u32 ticks;
extern pte_t *kernel_pd;
extern bool test_mode;
extern const struct boot_info *boot_info; /* set before console_init() */
extern bool fb_window_mapped; /* vm_kernel_init mapped the firmware framebuffer */
void console_init(const struct boot_info *);
void console_fb_enable(void);
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
void cpu_init(void);
void cpu_get_info(struct nv_cpu_info *);
void cpu_fp_reset(struct fp_state *);
void cpu_fp_save(struct fp_state *);
void cpu_fp_restore(const struct fp_state *);
void acpi_init(bool, u64 rsdp);
void acpi_get_info(struct nv_platform_info *);
u32 pci_read(u32, u32);
void pci_write16(u32, u32, u16);
void pci_visit(void (*)(u32, u32, u32));
u32 pci_ecam_configure(const struct nv_mcfg_region *, u32, u32, u32 *,
                       struct nv_mcfg_region *);
void gpu_init(void);
int gpu_get_info(u32, struct nv_gpu_info *);
int gpu_ioctl(u32, u32);
int cpu_ioctl(u32, u32);
void arch_set_stack(uptr);
void vm_kernel_init(void);
#ifdef __x86_64__
void *vm_heap_create(void);
#endif
void *vm_stack_alloc(u32);
void vm_stack_free(void *);
void *vm_mmio_map(u64, u32);
void *vm_mmio_remap(u64);
NORETURN void arch_guard_probe(uptr);
NORETURN void arch_resume(struct frame *);
struct frame *interrupt_dispatch(struct frame *);
void memory_init(const struct boot_info *);
void memory_reserve(u64, u64);
uptr page_alloc(void);
uptr page_alloc_below(u64 limit); /* never returns pages at or above limit */
uptr page_alloc_run(u32 count);   /* physically contiguous run, zeroed */
void page_free(uptr);
void page_pin(uptr); /* convert an owned page to a permanent kernel reservation */
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
uptr vm_translate(pte_t *, u32);
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
int disk_read(u64, void *);
int disk_write(u64, const void *);
int disk_flush(void);
struct store_layout {
    u32 slot_lba[2], slot_sectors, snap_cap;
};
bool disk_store_layout(struct store_layout *);
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
void kernel_start(const struct boot_info *); /* common entry: Multiboot or UEFI */
NORETURN void machine_poweroff(void);
NORETURN void machine_reboot(void);
#endif
