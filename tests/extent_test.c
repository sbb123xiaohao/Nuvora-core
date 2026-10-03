/* Exercise real filesystem/store code over a sparse 32 GiB backing device. */
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
bool task_cwd_in_use(int n) { (void)n; return false; }
volatile u64 ticks;
static FILE *disk;
static u64 disk_base;
static struct store_layout external_layout;
static const u64 disk_blocks = 32ull * 1024 * 1024 * 1024 / PAGE;
static int fail_after = -1, flush_after = -1;
static u64 written_sectors;
static u64 arena_blocks;
bool disk_ready(void) { return true; }
u32 disk_volume_count(void) { return 1; }
bool disk_volume_layout(u32 v, struct store_layout *l) {
    assert(!v);
    if (disk_base) { *l = external_layout; return true; }
    *l = (struct store_layout){.slot_lba={8, 4105}, .slot_sectors=4097,
        .snap_cap=2*1024*1024, .version=3, .data_first=1026,
        .data_end=arena_blocks ? 1026 + arena_blocks : disk_blocks};
    return true;
}
int disk_volume_read(u32 v, u64 lba, void *out) {
    assert(!v && lba < disk_blocks * 8);
    assert(!fseek(disk, (long)((disk_base+lba) * 512), SEEK_SET));
    size_t n = fread(out, 1, 512, disk);
    if (n < 512) { assert(!ferror(disk)); memset((u8 *)out + n, 0, 512 - n); clearerr(disk); }
    return 0;
}
int disk_volume_write(u32 v, u64 lba, const void *data) {
    assert(!v && lba >= 8 && lba < disk_blocks * 8);
    if (!fail_after) return -NV_EIO;
    if (fail_after > 0) --fail_after;
    assert(!fseek(disk, (long)((disk_base+lba) * 512), SEEK_SET));
    assert(fwrite(data, 1, 512, disk) == 512); ++written_sectors; return 0;
}
int disk_flush(void) {
    if (!flush_after) return -NV_EIO;
    if (flush_after > 0) --flush_after;
    assert(!fflush(disk)); return 0;
}
static struct task task;
static void reset(void) {
    for (int i = 0; i < FS_NODES; ++i) release_file(&nodes[i]);
    memset(nodes, 0, sizeof(nodes)); mounted_volumes = 0;
    fs_extent_enable(0, 0, 0);
    fs_init(); store_init();
    memset(&task, 0, sizeof(task)); task.cwd = home_node;
    for (int i = 0; i < NV_OPEN_MAX; ++i) task.fd[i].node = -1;
}
static void seek64(int fd, u64 pos) {
    struct nv_seek64 io = {.offset=(i64)pos};
    assert(!fs_seek64(&task, fd, &io) && io.position == pos);
}
static void expect(int fd, u64 at, const char *s, u32 n) {
    char out[32]; assert(n <= sizeof(out)); seek64(fd, at);
    assert(fs_read(&task, fd, out, n) == (int)n && !memcmp(s, out, n));
}
static void fresh_arena(u64 blocks) {
    if (disk) fclose(disk);
    disk = tmpfile(); assert(disk);
    arena_blocks = blocks; reset();
}
static int write_pattern(const char *name, u8 value, u32 blocks) {
    static u8 bytes[3*PAGE];
    memset(bytes, value, sizeof(bytes));
    int fd = fs_open(&task, name, NV_CREATE|NV_WRITE|NV_READ); assert(fd >= 3);
    assert(fs_write(&task, fd, bytes, blocks*PAGE) == (int)(blocks*PAGE));
    return fd;
}
static void expect_pattern(int fd, u8 value, u32 blocks) {
    u8 bytes[3*PAGE];
    seek64(fd, 0);
    assert(fs_read(&task, fd, bytes, blocks*PAGE) == (int)(blocks*PAGE));
    for (u32 i = 0; i < blocks*PAGE; ++i) assert(bytes[i] == value);
}
static void allocation_regressions(void) {
    /* Deleting a temporary file leaves one free run across the cursor. */
    fresh_arena(4);
    int temporary = write_pattern("temporary", 'T', 2);
    assert(!fs_close(&task, temporary) && !fs_remove(home_node, "temporary"));
    int replacement = write_pattern("replacement", 'R', 3);
    expect_pattern(replacement, 'R', 3);

    /* A failed write also releases its unreferenced allocation. Its retry
     * must overwrite all bytes and retain the original empty file on error. */
    fresh_arena(4);
    int retry = fs_open(&task, "retry", NV_CREATE|NV_WRITE|NV_READ); assert(retry >= 3);
    static u8 bytes[3*PAGE]; memset(bytes, 'F', sizeof(bytes));
    fail_after = 1;
    assert(fs_write(&task, retry, bytes, 2*PAGE) == -NV_EIO);
    fail_after = -1;
    struct nv_stat64 st;
    assert(!fs_stat64(&task, retry, &st) && !st.size && !st.allocated);
    assert(task.fd[retry].offset == 0);
    memset(bytes, 'R', sizeof(bytes));
    assert(fs_write(&task, retry, bytes, sizeof(bytes)) == sizeof(bytes));
    expect_pattern(retry, 'R', 3);

    /* Four free blocks split by live files cannot satisfy one three-block
     * write. Reaching the whole arena must not overwrite either blocker. */
    fresh_arena(6);
    int a = write_pattern("a", 'A', 2), b = write_pattern("b", 'B', 1);
    int c = write_pattern("c", 'C', 2), d = write_pattern("d", 'D', 1);
    assert(!fs_close(&task, a) && !fs_remove(home_node, "a"));
    assert(!fs_close(&task, c) && !fs_remove(home_node, "c"));
    int full = fs_open(&task, "full", NV_CREATE|NV_WRITE|NV_READ); assert(full >= 3);
    u64 writes_before = written_sectors;
    assert(fs_write(&task, full, bytes, sizeof(bytes)) == -NV_ENOSPC);
    assert(written_sectors == writes_before && task.fd[full].offset == 0);
    assert(!fs_stat64(&task, full, &st) && !st.size && !st.allocated);
    expect_pattern(b, 'B', 1); expect_pattern(d, 'D', 1);

    /* Two committed generations pin four blocks. Reclaim the following
     * temporary allocation across the cursor, then fall back to the older
     * root: neither the current file nor that root's bytes may be reused. */
    fresh_arena(8);
    int durable = write_pattern("durable", 'A', 2);
    assert(!store_sync());
    seek64(durable, 0); memset(bytes, 'B', 2*PAGE);
    assert(fs_write(&task, durable, bytes, 2*PAGE) == 2*PAGE);
    assert(!store_sync());
    temporary = write_pattern("temporary", 'T', 2);
    assert(!fs_close(&task, temporary) && !fs_remove(home_node, "temporary"));
    replacement = write_pattern("replacement", 'R', 3);
    expect_pattern(replacement, 'R', 3); expect_pattern(durable, 'B', 2);
    u8 sector[512];
    u64 lba = volume[0].layout.slot_lba[volume[0].active_slot] + 1;
    disk_volume_read(0, lba, sector); sector[0] ^= 0x80;
    assert(!disk_volume_write(0, lba, sector));
    reset();
    durable = fs_open(&task, "durable", NV_READ); assert(durable >= 3);
    assert(store_generation() == 1 && fs_lookup(home_node, "replacement") == -NV_ENOENT);
    expect_pattern(durable, 'A', 2);
    fresh_arena(0); fclose(disk); disk = NULL;
    puts("PASS extent allocation: reclaimed cursor-spanning runs, failed writes, fragmentation and durable root protection");
}
int main(int argc, char **argv) {
    heap = aligned_alloc(PAGE, HEAP_SIZE); assert(heap);
    head = (struct block *)heap;
    *head = (struct block){.magic=BLOCK_MAGIC, .size=HEAP_SIZE-sizeof(*head), .free=1};
    if (argc == 2 && !strcmp(argv[1], "allocation-wrap")) {
        allocation_regressions(); free(heap); return 0;
    }
    if (argc == 2) {
        disk = fopen(argv[1], "r+b"); assert(disk); disk_base = 2048;
        u8 header[512]; disk_volume_read(0, 0, header);
        assert(!memcmp(header, "NVSTORE3", 8));
        memcpy(&external_layout.slot_lba[0], header+16, 4);
        memcpy(&external_layout.slot_lba[1], header+20, 4);
        memcpy(&external_layout.slot_sectors, header+24, 4);
        u64 total; memcpy(&total, header+32, 8);
        external_layout.snap_cap=(external_layout.slot_sectors-1)*512;
        external_layout.version=3;
        external_layout.data_first=(external_layout.slot_lba[1]+external_layout.slot_sectors+7)/8;
        external_layout.data_end=total/8;
        reset();
        int f=fs_open(&task,"huge.bin",NV_READ|NV_WRITE); assert(f>=3);
        expect(f,6ull*1024*1024*1024+4093,"cross-4g-boundary",17);
        seek64(f,10); assert(fs_write(&task,f,"kernel-edited",13)==13);
        assert(!store_sync()); fclose(disk);
        puts("PASS importer/kernel interoperability: host extent metadata mounted, >4 GiB file read and committed by actual kernel code");
        return 0;
    }
    assert(argc==1);
    allocation_regressions();
    disk = tmpfile(); assert(disk); reset();
    int fd = fs_open(&task, "big.bin", NV_CREATE|NV_READ|NV_WRITE); assert(fd >= 3);
    u64 far = 10ull * 1024 * 1024 * 1024 + 4093;
    u32 before = heap_used();
    /* Both logical file offsets AND physical LBAs exceed 4 GiB. */
    extent_vol[0].hint = 6ull * 1024 * 1024 * 1024 / PAGE;
    seek64(fd, far); assert(fs_write(&task, fd, "first-value", 11) == 11);
    struct nv_stat64 st; assert(!fs_stat64(&task, fd, &st));
    assert(st.size == far + 11 && st.allocated == 8192 && heap_used() - before < 4096);
    assert(fs_seek(&task, fd, 0, 2) == -NV_E2BIG);
    struct nv_seek64 bad = {.offset=(-0x7fffffffffffffffll-1), .origin=1};
    assert(fs_seek64(&task, fd, &bad) == -NV_EINVAL);
    expect(fd, far - 3, "\0\0\0first-value", 14);
    struct nv_dirent64 ent; assert(fs_list64(home_node, ".", 0, &ent) == 1 && ent.size == far+11);
    assert(fs_move(home_node, "big.bin", "/tmp/big.bin") == -NV_EINVAL);
    assert(!store_sync()); reset();
    fd = fs_open(&task, "big.bin", NV_READ|NV_WRITE); assert(fd >= 3);
    expect(fd, far, "first-value", 11);
    seek64(fd, far); fail_after = 1;
    assert(fs_write(&task, fd, "second-xxxx", 11) == -NV_EIO); fail_after = -1;
    expect(fd, far, "first-value", 11);
    seek64(fd, far); assert(fs_write(&task, fd, "second-xxxx", 11) == 11);
    assert(!store_sync());
    /* Damage newest metadata, then recover the previous file AND its blocks. */
    int newest = volume[0].active_slot;
    u8 sector[512]; u64 lba = volume[0].layout.slot_lba[newest]+1;
    disk_volume_read(0, lba, sector); sector[0] ^= 0x80; disk_volume_write(0, lba, sector);
    reset(); fd = fs_open(&task, "big.bin", NV_READ|NV_WRITE);
    expect(fd, far, "first-value", 11);
    assert(!fs_close(&task, fd));
    fd = fs_open(&task, "stream.bin", NV_CREATE|NV_WRITE|NV_READ); assert(fd >= 3);
    static u8 data[16384]; memset(data, 0x5a, sizeof(data));
    before = heap_used();
    for (u32 i = 0; i < 160*1024*1024/sizeof(data); ++i)
        assert(fs_write(&task, fd, data, sizeof(data)) == sizeof(data));
    assert(heap_used()-before < 4096); /* file bytes never accumulate in RAM */
    u64 sectors_before = written_sectors;
    assert(!store_sync() && written_sectors-sectors_before < 32); /* metadata only */
    reset(); fd = fs_open(&task, "stream.bin", NV_READ|NV_WRITE);
    assert(!fs_stat64(&task, fd, &st) && st.size == 160*1024*1024);
    expect(fd, st.size-4, "ZZZZ", 4);
    /* Ambiguous final flush freezes disk writes, preserving both possible roots. */
    seek64(fd, 0); assert(fs_write(&task, fd, "new", 3) == 3);
    flush_after = 2; assert(store_sync() == -NV_EIO); flush_after = -1;
    assert(fs_write(&task, fd, "bad", 3) == -NV_EIO);
    u64 frozen_size = nodes[fs_lookup(home_node, "stream.bin")].size;
    assert(fs_open(&task, "stream.bin", NV_WRITE | NV_TRUNC) == -NV_EIO);
    assert(nodes[fs_lookup(home_node, "stream.bin")].size == frozen_size);
    assert(fs_open(&task, "fresh.bin", NV_WRITE | NV_CREATE) == -NV_EIO);
    assert(fs_mkdir(home_node, "newdir") == -NV_EIO);
    assert(fs_move(home_node, "stream.bin", "renamed.bin") == -NV_EIO);
    assert(!fs_close(&task, fd));
    assert(fs_remove(home_node, "stream.bin") == -NV_EIO);
    assert(fs_replace(home_node, "stream.bin", "big.bin") == -NV_EIO);
    assert(fs_lookup(home_node, "stream.bin") > 0 &&
           fs_lookup(home_node, "fresh.bin") == -NV_ENOENT);
    assert(!fs_mkdir(0, "/tmp/working") && !fs_remove(0, "/tmp/working"));
    reset(); fd = fs_open(&task, "stream.bin", NV_READ|NV_WRITE);
    expect(fd, 0, "new", 3);
    /* Exhaustion and allocation failure do not modify visible file content. */
    u64 end = extent_vol[0].end, first = extent_vol[0].first;
    extent_vol[0].first = extent_vol[0].end = extent_vol[0].hint = end;
    seek64(fd, 0); assert(fs_write(&task, fd, "bad", 3) == -NV_ENOSPC);
    expect(fd, 0, "new", 3);
    extent_vol[0].first=first; extent_vol[0].end=end; extent_vol[0].hint=first;
    assert(!fs_close(&task,fd));
    fd=fs_open(&task,"random.bin",NV_CREATE|NV_READ|NV_WRITE); assert(fd>=3);
    static u8 expected[65536], actual[65536]; u32 seed=12345;
    for (u32 i=0;i<180;++i) {
        seed=seed*1664525u+1013904223u;
        u32 at=seed%60000, len=(seed>>16)%4000+1;
        memset(data,(int)(i&255),len);seek64(fd,at);
        assert(fs_write(&task,fd,data,len)==(int)len);
        memcpy(expected+at,data,len);
        if (i%31==0) assert(!store_sync());
    }
    assert(!fs_stat64(&task,fd,&st)); seek64(fd,0);
    assert(fs_read(&task,fd,actual,(u32)st.size)==(int)st.size && !memcmp(actual,expected,(u32)st.size));
    void *held[256];u32 held_count=0;
    while (held_count<256 && (held[held_count]=kmalloc(65536))) ++held_count;
    void *small[4096];u32 small_count=0;
    while (small_count<4096 && (small[small_count]=kmalloc(32))) ++small_count;
    seek64(fd,0);assert(fs_write(&task,fd,"bad",3)==-NV_ENOMEM);
    for (u32 i=0;i<small_count;++i) kfree(small[i]);
    for (u32 i=0;i<held_count;++i) kfree(held[i]);
    seek64(fd,0);assert(fs_read(&task,fd,actual,(u32)st.size)==(int)st.size && !memcmp(actual,expected,(u32)st.size));
    assert(!store_sync()); reset(); fd=fs_open(&task,"random.bin",NV_READ);
    assert(fs_read(&task,fd,actual,(u32)st.size)==(int)st.size && !memcmp(actual,expected,(u32)st.size));
    reset(); fclose(disk); free(heap);
    puts("PASS extent store: 10 GiB sparse file, >4 GiB physical LBA, 160 MiB stream with bounded RAM, metadata-only commits, torn write/CRC fallback, full disk and ambiguous flush");
}
