#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../kernel/memory.c"

void panic(const char *message) { fprintf(stderr, "PANIC: %s\n", message); abort(); }

static void reset_pages(u64 start, u64 length) {
    memset(bitmap, 0xff, sizeof(bitmap));
    memset(allocated, 0, sizeof(allocated));
    free_count = total_count = search_word = 0;
    available(start, length);
}
static void *fixed_map(uptr address, usize length, int fd) {
    int flags = MAP_FIXED_NOREPLACE | (fd < 0 ? MAP_PRIVATE | MAP_ANONYMOUS : MAP_SHARED);
    void *p = mmap((void *)address, length, PROT_READ | PROT_WRITE, flags, fd, 0);
    assert(p == (void *)address);
    return p;
}
static void boundary_run(void) {
    const uptr physical = USER_BASE - PAGE;
    int fd = memfd_create("nuvora-physical-fixture", 0);
    assert(fd >= 0 && ftruncate(fd, 2 * PAGE) == 0);
    /* Real RAM has a contiguous supervisor alias. The current task owns a
     * different page at USER_BASE, directly after the low identity range. */
    u8 *ram = fixed_map(PHYS_WINDOW + physical, 2 * PAGE, fd);
    void *low = fixed_map(physical, PAGE, fd);
    u8 *user = fixed_map(USER_BASE, PAGE, -1);
    memset(ram, 0xa5, 2 * PAGE);
    memset(user, 0x5a, PAGE);
    reset_pages(physical, 2 * PAGE);
    assert(page_alloc_run(2) == physical && pages_free() == 0);
    for (u32 i = 0; i < PAGE; ++i) {
        if (user[i] != 0x5a || ram[PAGE + i] != 0) {
            fprintf(stderr, "FAIL: contiguous allocation crossed the 1 GiB user mapping; "
                            "user=%02x, physical RAM=%02x\n", user[i], ram[PAGE + i]);
            exit(1);
        }
        assert(ram[i] == 0);
    }
    assert((uptr)phys_ptr(physical) + PAGE == (uptr)phys_ptr(USER_BASE));
    assert(phys_ptr(0) == NULL && ptr_phys(phys_ptr(physical)) == physical);
    page_free(physical); page_free(physical + PAGE);
    assert(pages_free() == 2);
    munmap(ram, 2 * PAGE); munmap(low, PAGE); munmap(user, PAGE); close(fd);
}
static void allocator_edges(void) {
    const uptr physical = 0x6000000u;
    const u32 count = 32;
    int fd = memfd_create("nuvora-page-fixture", 0);
    assert(fd >= 0 && ftruncate(fd, count * PAGE) == 0);
    u8 *ram = fixed_map(PHYS_WINDOW + physical, count * PAGE, fd);
    void *low = fixed_map(physical, count * PAGE, fd);
    reset_pages(physical, count * PAGE);
    memory_reserve(physical + PAGE - 1, 2); /* touches both pages */
    memory_reserve(physical + PAGE - 1, 2); /* overlap must not double-count */
    assert(pages_free() == count - 2);
    assert(!page_alloc_below(physical + 2 * PAGE));
    memset(ram, 0xa5, count * PAGE);
    uptr pages[count];
    for (u32 i = 0; i < count - 2; ++i) {
        pages[i] = page_alloc();
        assert(pages[i] >= physical + 2 * PAGE && pages[i] < physical + count * PAGE);
        for (u32 j = 0; j < PAGE; ++j) assert(((u8 *)phys_ptr(pages[i]))[j] == 0);
        memset(phys_ptr(pages[i]), 0x5a, PAGE);
    }
    assert(!page_alloc() && !page_alloc_run(1));
    for (u32 i = 0; i < count - 2; i += 2) page_free(pages[i]);
    u32 available_before = pages_free();
    assert(available_before && !page_alloc_run(2) && pages_free() == available_before);
    for (u32 i = 1; i < count - 2; i += 2) page_free(pages[i]);
    uptr run = page_alloc_run(count - 2);
    assert(run == physical + 2 * PAGE && !pages_free());
    for (u32 i = 0; i < count - 2; ++i) page_free(run + i * PAGE);
    assert(pages_free() == count - 2);
    munmap(ram, count * PAGE); munmap(low, count * PAGE); close(fd);
}
static u32 random_state = 0x71902u;
static u32 next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static void heap_churn(void) {
    enum { SLOTS = 128, STEPS = 12000 };
    void *items[SLOTS] = {0};
    u32 sizes[SLOTS] = {0};
    heap = aligned_alloc(PAGE, HEAP_SIZE); assert(heap);
    head = (struct block *)heap;
    *head = (struct block){.magic=BLOCK_MAGIC, .size=HEAP_SIZE-sizeof(*head), .free=1};
    used_bytes = 0;
    for (u32 round = 0; round < STEPS + SLOTS; ++round) {
        u32 i = round < STEPS ? next_random() % SLOTS : round - STEPS;
        if (items[i]) {
            for (u32 n = 0; n < sizes[i]; ++n) assert(((u8 *)items[i])[n] == (u8)(i + 1));
            kfree(items[i]); items[i] = NULL;
        } else if (round < STEPS) {
            sizes[i] = 1 + next_random() % 131072;
            items[i] = kmalloc(sizes[i]);
            if (!items[i]) continue;
            assert((uptr)items[i] % 16 == 0);
            for (u32 n = 0; n < sizes[i]; ++n) assert(((u8 *)items[i])[n] == 0);
            memset(items[i], (int)(i + 1), sizes[i]);
        }
    }
    assert(heap_used() == 0 && !kmalloc(0) && !kmalloc((usize)-1));
    void *large = kmalloc(HEAP_SIZE - 64); assert(large);
    assert(!kmalloc(PAGE)); kfree(large);
    assert(heap_used() == 0);
    free(heap); heap = NULL; head = NULL;
}
int main(void) {
    boundary_run(); allocator_edges(); heap_churn();
    puts("PASS memory: 1 GiB boundary isolation; reserved/DMA/fragmented pages; 12000 heap operations");
}
