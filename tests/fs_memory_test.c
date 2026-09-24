#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
/* Exercise the real ramfs and kernel heap together. Only console/process
 * services are stubs; no substitute allocator hides the memory budget. */
#include "../kernel/memory.c"
#include "../kernel/fs.c"

void panic(const char *message) { fprintf(stderr, "PANIC: %s\n", message); abort(); }
void console_write(const char *p, usize n) { (void)p; (void)n; }
bool task_cwd_in_use(int node) { (void)node; return false; }

static void restored_growth(u32 size, u32 budget, bool should_fail) {
    const char path[] = "/home/growth";
    u32 pathlen = sizeof(path) - 1, length = 16 + pathlen + size;
    u8 *snapshot = malloc(length);
    assert(snapshot);
    u32 header[] = {1, NV_FILE, pathlen, size};
    memcpy(snapshot, header, sizeof(header));
    memcpy(snapshot + sizeof(header), path, pathlen);
    memset(snapshot + sizeof(header) + pathlen, 0x5a, size);
    assert(fs_import_home(snapshot, length) == 0);
    free(snapshot);
    int node = fs_lookup(0, path);
    assert(node > 0 && nodes[node].capacity == size);
    struct task task = {.cwd = home_node};
    for (u32 i = 0; i < NV_OPEN_MAX; ++i) task.fd[i].node = -1;
    int fd = fs_open(&task, path, NV_READ | NV_WRITE);
    assert(fd >= 3 && fs_seek(&task, fd, NV_FILE_MAX - 1, 0) == NV_FILE_MAX - 1);

    void *filler = NULL;
    if (budget) {
        assert(head->next && head->next->free && !head->next->next);
        filler = kmalloc(head->next->size - budget - sizeof(struct block));
        assert(filler);
    }
    u8 *old_data = nodes[node].data;
    u32 before = heap_used();
    int result = fs_write(&task, fd, "!", 1);
    if (should_fail) {
        assert(result == -NV_ENOMEM && nodes[node].data == old_data);
        assert(nodes[node].size == size && nodes[node].capacity == size);
        assert(task.fd[fd].offset == NV_FILE_MAX - 1 && heap_used() == before);
    } else {
        if (result != 1 || nodes[node].capacity > NV_FILE_MAX) {
            fprintf(stderr, "FAIL restored file growth: size=%u budget=%u result=%d "
                            "capacity=%u (maximum %u)\n",
                    size, budget, result, nodes[node].capacity, NV_FILE_MAX);
            exit(1);
        }
        assert(nodes[node].size == NV_FILE_MAX && task.fd[fd].offset == NV_FILE_MAX);
        assert(fs_write(&task, fd, "?", 1) == -NV_ENOSPC);
        assert(nodes[node].data[NV_FILE_MAX - 1] == '!');
        for (u32 i = size; i < NV_FILE_MAX - 1; ++i) assert(nodes[node].data[i] == 0);
    }
    for (u32 i = 0; i < size; ++i) assert(nodes[node].data[i] == 0x5a);
    assert(fs_close(&task, fd) == 0 && fs_remove(0, path) == 0);
    kfree(filler);
    assert(!heap_used() && head->free && !head->next && head->size == HEAP_SIZE - sizeof(*head));
}

int main(void) {
    heap = aligned_alloc(PAGE, HEAP_SIZE);
    assert(heap);
    head = (struct block *)heap;
    *head = (struct block){.magic = BLOCK_MAGIC, .size = HEAP_SIZE - sizeof(*head), .free = 1};
    fs_init();
    const u32 sizes[] = {NV_FILE_MAX - 1, 257, 65535, 65537};
    for (u32 i = 0; i < ARRAY_LEN(sizes); ++i) {
        restored_growth(sizes[i], NV_FILE_MAX, false);
        restored_growth(sizes[i], 0, false);
        restored_growth(sizes[i], NV_FILE_MAX - 16, true);
    }
    free(heap);
    puts("PASS ramfs memory: restored sizes, bounded growth under pressure, sparse zeros, OOM rollback and full reclamation");
}
