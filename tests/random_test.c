#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../kernel/kernel.h"
static u8 *user;
static bool writable = true, source_ready = true;
static u32 source_calls;
struct task *current;
bool user_range(pte_t *pd, uptr p, usize n, bool write) {
    (void)pd;
    return (!write || writable) && p >= (uptr)user && p - (uptr)user <= PAGE &&
        n <= PAGE - (p - (uptr)user);
}
static bool random_source(void *p, u32 n) {
    ++source_calls;
    /* Model partial output followed by a hardware entropy failure. */
    memset(p, 0x6d, source_ready ? n : n / 2);
    return source_ready;
}
#define NV_CPU_RANDOM_SOURCE random_source
#include "../kernel/cpu.c"
int main(void) {
    struct task actor = {0}; current = &actor;
    user = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(user != MAP_FAILED && (uptr)user > 0xffffffffull);
    struct nv_random *io = (void *)user;
    assert(cpu_ioctl(0, 0) == -NV_ENOSYS);
    assert(cpu_ioctl(NV_CPU_RANDOM, (uptr)user + PAGE - 4) == -NV_EFAULT);
    assert(cpu_ioctl(NV_CPU_RANDOM, 0) == -NV_EFAULT);
    writable = false;
    assert(cpu_ioctl(NV_CPU_RANDOM, (uptr)io) == -NV_EFAULT && !source_calls);
    writable = true;
    const u32 invalid[] = {0, NV_RANDOM_MAX + 1, 0xffffffffu};
    for (u32 i = 0; i < ARRAY_LEN(invalid); ++i) {
        memset(io, 0xb9, sizeof(*io)); io->length = invalid[i];
        assert(cpu_ioctl(NV_CPU_RANDOM, (uptr)io) == -NV_EINVAL && !source_calls);
        for (u32 j = 0; j < sizeof(io->data); ++j) assert(io->data[j] == 0xb9);
    }
    for (u32 n = 1; n <= NV_RANDOM_MAX; ++n) {
        memset(io, 0xb9, sizeof(*io)); io->length = n;
        assert(!cpu_ioctl(NV_CPU_RANDOM, (uptr)io) && io->length == n);
        for (u32 j = 0; j < sizeof(io->data); ++j) assert(io->data[j] == (j < n ? 0x6d : 0));
    }
    source_ready = false;
    for (u32 n = 1; n <= NV_RANDOM_MAX; ++n) {
        memset(io, 0xb9, sizeof(*io)); io->length = n;
        assert(cpu_ioctl(NV_CPU_RANDOM, (uptr)io) == -NV_ENODEV && io->length == n);
        for (u32 j = 0; j < sizeof(io->data); ++j) assert(!io->data[j]);
    }
    assert(source_calls == 2 * NV_RANDOM_MAX);
    puts("PASS random ABI: full writable high-address response, all 1-64 lengths, clean tail, partial-failure clearing and invalid requests without entropy calls");
    munmap(user, PAGE);
}
