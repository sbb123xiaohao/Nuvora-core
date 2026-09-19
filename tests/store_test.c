#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
#define SNAP_CAP_MAX (16u * 1024u * 1024u)
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; };
static struct store_layout fixture;
static u8 headers[2][512];
static u32 writes, heap_request, export_size;
static void *phys_ptr(uptr p) { return (void *)p; }
static u32 pages_free(void) { return 64; }
static uptr page_alloc_run(u32 count) { (void)count; return 0; }
static void *kmalloc(usize n) { heap_request = n; return malloc(n); }
static bool disk_ready(void) { return true; }
static bool disk_store_layout(struct store_layout *p) { *p=fixture; return true; }
static int disk_read(u64 lba, void *p) {
    if (lba == fixture.slot_lba[0] || lba == fixture.slot_lba[1]) {
        memcpy(p, headers[lba == fixture.slot_lba[1]], 512); return 0;
    }
    return -NV_EIO;
}
static int disk_write(u64 lba, const void *p) {
    assert(lba >= fixture.slot_lba[0] && lba < fixture.slot_lba[1] + fixture.slot_sectors);
    ++writes;
    if (lba == fixture.slot_lba[0] + 1) {
        const u8 *b=p;
        for (u32 i = export_size; i < 512; ++i) assert(b[i] == 0);
    }
    return 0;
}
static int disk_flush(void) { return 0; }
static int fs_import_home(const u8 *b, u32 n) { (void)b; (void)n; return -NV_EIO; }
static int fs_export_home(u8 *b, u32 cap, u32 *n) {
    assert(export_size <= cap); memset(b, 0xaa, export_size); *n=export_size; return 0;
}
static void kprintf(const char *fmt, ...) { (void)fmt; }
#include "../kernel/store.c"
int main(void) {
    fixture = (struct store_layout){{8,4096},2049,1024*1024};
    struct snapshot_header h = {.magic="NVSS0001", .generation=7, .length=128*1024};
    h.header_checksum = crc32(&h, sizeof(h)); memcpy(headers[0], &h, sizeof(h));
    store_init();
    assert(restore_blocked && store_sync() == -NV_ENOMEM && !writes);
    free(snap_buffer); snap_buffer = NULL; snap_cap = 0; restore_blocked = false;
    fixture = (struct store_layout){{8,16},8,3584}; memset(headers,0,sizeof(headers));
    store_init(); assert(heap_request == 3584 && snap_cap == 3584);
    memset(snap_buffer, 0x55, snap_cap); export_size = 13;
    assert(!store_sync() && writes == 3 && store_generation() == 1);
    free(snap_buffer);
    puts("PASS snapshots: low-memory restore blocks overwrite; small slots bound fallback; sector tails zeroed");
}
