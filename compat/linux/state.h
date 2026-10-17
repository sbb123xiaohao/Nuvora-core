#ifndef NV_COMPAT_LINUX_STATE_H
#define NV_COMPAT_LINUX_STATE_H
#define LINUX_VMAS 256u
#define LINUX_STACK_PAGES 64u
#define LINUX_MAP_BASE 0x2000000000ull
#define LINUX_MAP_END  0x400000000000ull
#define LINUX_INTERP_BASE 0x1800000000ull
#define LINUX_VIRTUAL_MAX (16ull * 1024u * 1024u * 1024u)
/* A ceiling, not a reservation: page allocation still checks physical RAM and
 * every failed mapping rolls back. Larger game/JVM heaps need this budget. */
#define LINUX_MEMORY_MAX (1024ull * 1024u * 1024u)
struct linux_vma { uptr start, end; u32 protection, flags; };
struct linux_action {uptr handler;u64 flags;uptr restorer;u64 mask;};
struct linux_sigstack {uptr pointer;u32 flags,pad;u64 size;};
struct compat_state {
    bool enabled, trace, jit;
    u16 closed_stdio;
    u64 cloexec, nonblock;
    uptr fs_base, brk, interpreter, interpreter_entry;
    u64 signal_mask;
    u64 pending_signals;
    u32 signal_depth;
    struct linux_sigstack signal_stack;
    struct linux_action actions[64];
    struct linux_vma maps[LINUX_VMAS];
    uptr clear_tid,futex_address;
    u32 futex_bitset;
    uptr sleep_remaining;
    u64 sleep_left;
    bool sleeping,sleep_interrupted;
};
struct compat_group {
    u32 pid,references,alive;
    u64 retired_ticks;
    struct linux_vma maps[LINUX_VMAS];
    struct linux_action actions[64];
    struct descriptor fd[NV_OPEN_MAX];
    uptr brk,heap_end;
    int cwd;
    u16 closed_stdio;
    u64 cloexec,nonblock;
};
#endif
