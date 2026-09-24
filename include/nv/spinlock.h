#ifndef NV_SPINLOCK_H
#define NV_SPINLOCK_H
#include <nv/types.h>
struct nv_spinlock { u32 held; };
static inline void nv_cpu_relax(void) {
#if defined(__x86_64__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#else
#error "Nuvora supports x64 and ARM64 only"
#endif
}
static inline void nv_spin_lock(struct nv_spinlock *lock) {
    for (;;) {
        if (!__atomic_exchange_n(&lock->held, 1, __ATOMIC_ACQUIRE)) return;
        while (__atomic_load_n(&lock->held, __ATOMIC_RELAXED)) nv_cpu_relax();
    }
}
static inline void nv_spin_unlock(struct nv_spinlock *lock) {
    __atomic_store_n(&lock->held, 0, __ATOMIC_RELEASE);
}
#endif
