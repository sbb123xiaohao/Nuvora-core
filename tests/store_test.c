#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define NV_FS_TEST_HOST
#include "../kernel/memory.c"
#include "../kernel/fs.c"
#include "../kernel/store.c"
void panic(const char *why) { fputs(why, stderr); abort(); }
void kprintf(const char *fmt, ...) { (void)fmt; }
void console_write(const char *p, usize n) { (void)p; (void)n; }
int console_getc(void) { return -NV_EAGAIN; }
int task_info(u32 i, struct nv_taskinfo *out) { (void)i; (void)out; return 0; }
bool task_cwd_in_use(int node) { (void)node; return false; }
volatile u64 ticks;
enum { SLOT_SECTORS = 12 * 1024 * 1024 / 512 + 1 };
static u8 disk_data[2][2][SLOT_SECTORS * 512];
static bool fail_commit;
static int fail_commit_slot = 1;
static u32 writes;
bool disk_ready(void) { return true; }
u32 disk_volume_count(void) { return 2; }
bool disk_volume_layout(u32 i, struct store_layout *l) {
    assert(i < 2);
    *l = (struct store_layout){{8, 8 + SLOT_SECTORS}, SLOT_SECTORS,
                               (SLOT_SECTORS - 1) * 512, 2, 0, 0};
    return true;
}
static u8 *sector_at(u32 i, u64 lba) {
    assert(i < 2 && lba >= 8 && lba < 8 + 2 * SLOT_SECTORS);
    int slot = lba >= 8 + SLOT_SECTORS;
    u32 sector = (u32)(lba - (slot ? 8 + SLOT_SECTORS : 8));
    return disk_data[i][slot] + sector * 512;
}
int disk_volume_read(u32 i, u64 lba, void *out) {
    memcpy(out, sector_at(i, lba), 512);
    return 0;
}
int disk_volume_write(u32 i, u64 lba, const void *in) {
    if (fail_commit && i == 0 && lba == 8 + (u32)fail_commit_slot * SLOT_SECTORS &&
        !memcmp(in, "NVSS0001", 8)) return -NV_EIO;
    memcpy(sector_at(i, lba), in, 512);
    ++writes;
    return 0;
}
int disk_flush(void) { return 0; }
static struct task task_at(int cwd) {
    struct task t = {.cwd = cwd};
    for (u32 i = 0; i < NV_OPEN_MAX; ++i) t.fd[i].node = -1;
    return t;
}
static void write_file(const char *path, const char *value, u32 bytes) {
    struct task t = task_at(home_node);
    int fd = fs_open(&t, path, NV_WRITE | NV_CREATE | NV_TRUNC);
    assert(fd >= 3 && fs_write(&t, fd, value, bytes) == (int)bytes);
    assert(fs_close(&t, fd) == 0);
}
static void expect_file(const char *path, const char *value, u32 bytes) {
    struct task t = task_at(home_node);
    int fd = fs_open(&t, path, NV_READ);
    assert(fd >= 3);
    char data[1024];
    assert(bytes <= sizeof(data));
    assert(fs_read(&t, fd, data, bytes) == (int)bytes);
    assert(!memcmp(data, value, bytes));
    assert(fs_close(&t, fd) == 0);
}
static void reboot(void) {
    clear_volume(0);
    clear_volume(1);
    store_init();
}
static void expect_sparse_extension(const char *path, u32 original, bool edited) {
    struct task t = task_at(home_node);
    int fd = fs_open(&t, path, NV_READ);
    assert(fd >= 3);
    const u32 end = 4 * PAGE + 3, changed = 2 * PAGE + 17;
    u8 bytes[PAGE];
    for (u32 at = 0; at <= end;) {
        u32 size = MIN((u32)sizeof(bytes), end + 1 - at);
        assert(fs_read(&t, fd, bytes, size) == (int)size);
        for (u32 i = 0; i < size; ++i) {
            u32 offset = at + i;
            u8 expected = offset < original ? 'A' : offset == end ? 'Z' : 0;
            if (edited && offset == changed) expected = '?';
            if (edited && offset == original + 3) expected = '!';
            assert(bytes[i] == expected);
        }
        at += size;
    }
    assert(!fs_read(&t, fd, bytes, 1));
    assert(!fs_close(&t, fd));
}
static void sparse_extension_regression(void) {
    /* Saved legacy files are lazy views into a shared snapshot. Extending
     * one must never expose the metadata or data of the following file. */
    const u32 sizes[] = {0, 513, PAGE - 1, PAGE, PAGE + 13};
    char original[PAGE + 13], neighbour[4 * PAGE];
    memset(original, 'A', sizeof(original));
    memset(neighbour, 'S', sizeof(neighbour));
    for (u32 i = 0; i < ARRAY_LEN(sizes); ++i) {
        write_file("C:/sparse-growth", original, sizes[i]);
        write_file("C:/neighbour", neighbour, sizeof(neighbour));
        assert(!store_sync());
        if (i & 1u) reboot(); /* cover both rebase after save and boot restore */
        struct task t = task_at(home_node);
        int fd = fs_open(&t, "C:/sparse-growth", NV_READ | NV_WRITE);
        assert(fd >= 3 && fs_seek(&t, fd, 4 * PAGE + 3, 0) == 4 * PAGE + 3);
        assert(fs_write(&t, fd, "Z", 1) == 1);
        expect_sparse_extension("C:/sparse-growth", sizes[i], false);
        /* Populating a new RAM page in the gap has the same backing limit. */
        assert(fs_seek(&t, fd, 2 * PAGE + 17, 0) == 2 * PAGE + 17);
        assert(fs_write(&t, fd, "?", 1) == 1);
        /* The page containing the old EOF must copy only its saved prefix. */
        assert(fs_seek(&t, fd, sizes[i] + 3, 0) == (int)sizes[i] + 3);
        assert(fs_write(&t, fd, "!", 1) == 1);
        assert(!fs_close(&t, fd));
        expect_sparse_extension("C:/sparse-growth", sizes[i], true);
        fail_commit_slot = volume[0].active_slot == 0 ? 1 : 0;
        fail_commit = true;
        assert(store_sync() == -NV_EIO);
        fail_commit = false;
        expect_sparse_extension("C:/sparse-growth", sizes[i], true);
        assert(!store_sync());
        reboot();
        expect_sparse_extension("C:/sparse-growth", sizes[i], true);
        fd = fs_open(&t, "C:/neighbour", NV_READ);
        u8 block[PAGE];
        assert(fd >= 3 && fs_read(&t, fd, block, sizeof(block)) == (int)sizeof(block));
        for (u32 j = 0; j < sizeof(block); ++j) assert(block[j] == 'S');
        assert(!fs_close(&t, fd));
    }
    puts("PASS legacy sparse extension: zero holes, adjacent-file isolation, page writes, save and reboot");
}
int main(void) {
    heap = aligned_alloc(PAGE, HEAP_SIZE);
    assert(heap);
    head = (struct block *)heap;
    *head = (struct block){.magic = BLOCK_MAGIC, .size = HEAP_SIZE - sizeof(*head), .free = 1};
    fs_init(); fs_mount_volumes(2); store_init();
    char original[513], updated[513];
    for (u32 i = 0; i < sizeof(original); ++i) {
        original[i] = (char)(i * 17u + 3);
        updated[i] = (char)(i * 31u + 7);
    }
    write_file("C:/record", original, sizeof(original));
    write_file("D:/record", "D-old", 5);
    assert(store_sync() == 0 && store_generation() == 1 &&
           store_volume_generation(1) == 1);
    int n = fs_lookup(0, "C:/record");
    assert(n > 0 && !nodes[n].pages && nodes[n].backing_slot == 0);
    expect_file("C:/record", original, sizeof(original));
    /* A failed final header never publishes the inactive slot. */
    write_file("C:/record", updated, sizeof(updated));
    fail_commit = true;
    assert(store_sync() == -NV_EIO && store_generation() == 1);
    fail_commit = false;
    reboot();
    expect_file("C:/record", original, sizeof(original));
    expect_file("D:/record", "D-old", 5);
    /* Restore without allocating any file data pages, then commit twice. */
    assert(!nodes[fs_lookup(0, "C:/record")].pages);
    write_file("C:/record", updated, sizeof(updated));
    write_file("D:/record", "D-new", 5);
    assert(store_sync() == 0 && store_generation() == 2 &&
           store_volume_generation(1) == 2);
    disk_data[0][1][512 + 17] ^= 1; /* newest C: payload CRC fails */
    reboot();
    assert(store_generation() == 1 && store_volume_generation(1) == 2);
    expect_file("C:/record", original, sizeof(original));
    expect_file("D:/record", "D-new", 5);
    struct task cow = task_at(home_node);
    int changed = fs_open(&cow, "C:/record", NV_READ | NV_WRITE);
    assert(changed >= 3 && fs_seek(&cow, changed, 512, 0) == 512);
    assert(fs_write(&cow, changed, "?", 1) == 1);
    assert(fs_close(&cow, changed) == 0);
    original[512] = '?';
    assert(store_sync() == 0);
    reboot();
    expect_file("C:/record", original, sizeof(original));
    /* A sparse file larger than the old 4 MiB limit is saved and restored
     * without allocating its unwritten 8 MiB of RAM. */
    struct task t = task_at(home_node);
    int fd = fs_open(&t, "C:/archive.bin", NV_READ | NV_WRITE | NV_CREATE);
    assert(fd >= 3 && fs_seek(&t, fd, 8 * 1024 * 1024 - 1, 0) == 8 * 1024 * 1024 - 1);
    assert(fs_write(&t, fd, "!", 1) == 1 && fs_close(&t, fd) == 0);
    assert(heap_used() < 100000);
    assert(store_sync() == 0 && heap_used() < 100000);
    reboot();
    t = task_at(home_node);
    fd = fs_open(&t, "C:/archive.bin", NV_READ);
    char mark[2];
    assert(fd >= 3 && fs_read(&t, fd, mark, 2) == 2 && !mark[0] && !mark[1]);
    assert(fs_seek(&t, fd, 8 * 1024 * 1024 - 1, 0) == 8 * 1024 * 1024 - 1);
    assert(fs_read(&t, fd, mark, 1) == 1 && mark[0] == '!');
    assert(fs_close(&t, fd) == 0);
    /* Neither corrupt nonempty slot may be silently replaced with an empty tree. */
    disk_data[0][0][512 + 2] ^= 1;
    disk_data[0][1][512 + 2] ^= 1;
    reboot();
    assert(volume[0].restore_error == -NV_EIO);
    u32 prior_writes = writes;
    assert(store_sync() == -NV_EIO && writes == prior_writes);
    /* Seed the original on-disk NVSS0001 byte format, then upgrade through
     * the streaming path without a format conversion or a full RAM copy. */
    memset(disk_data[0], 0, sizeof(disk_data[0]));
    u8 *payload = disk_data[0][0] + 512;
    const char oldpath[] = "/home/old.dat";
    const char oldbytes[] = "old snapshot";
    u32 fields[] = {1, NV_FILE, sizeof(oldpath) - 1, sizeof(oldbytes) - 1};
    memcpy(payload, fields, sizeof(fields));
    memcpy(payload + sizeof(fields), oldpath, sizeof(oldpath) - 1);
    memcpy(payload + sizeof(fields) + sizeof(oldpath) - 1, oldbytes, sizeof(oldbytes) - 1);
    struct snapshot_header legacy = {0};
    memcpy(legacy.magic, "NVSS0001", 8);
    legacy.generation = 44;
    legacy.length = sizeof(fields) + sizeof(oldpath) - 1 + sizeof(oldbytes) - 1;
    legacy.checksum = crc32(payload, legacy.length);
    legacy.header_checksum = crc32(&legacy, sizeof(legacy));
    memcpy(disk_data[0][0], &legacy, sizeof(legacy));
    reboot();
    assert(store_generation() == 44);
    expect_file("C:/old.dat", oldbytes, sizeof(oldbytes) - 1);
    assert(store_sync() == 0 && store_generation() == 45);
    assert(writes > 0 && !heap_used());
    sparse_extension_regression();
    assert(!heap_used());
    free(heap);
    puts("PASS streaming snapshots: 8 MiB sparse file, lazy restore, power-loss and CRC fallback");
}
