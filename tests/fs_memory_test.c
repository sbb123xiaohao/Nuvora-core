#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
/* Real heap plus real filesystem; host page allocations use the heap so
 * out-of-memory rollback and ownership can be checked by the same fixture. */
#define NV_FS_TEST_HOST
#include "../kernel/memory.c"
#include "../kernel/fs.c"
void panic(const char *message) { fprintf(stderr, "PANIC: %s\n", message); abort(); }
void console_write(const char *p, usize n) { (void)p; (void)n; }
int console_getc(void) { return -NV_EAGAIN; }
int task_info(u32 index, struct nv_taskinfo *out) { (void)index; (void)out; return 0; }
volatile u64 ticks;
bool task_cwd_in_use(int node) { (void)node; return false; }
static bool entropy_available;
static u32 entropy_calls;
bool cpu_random(void *out,u32 bytes) {
    ++entropy_calls;
    if(!entropy_available) {if(bytes)((u8 *)out)[0]=0;return false;}
    for(u32 i=0;i<bytes;++i)((u8 *)out)[i]=(u8)(i+entropy_calls);
    return true;
}
int store_read_bytes(u32 v, int slot, u32 off, void *dst, u32 len) {
    (void)v; (void)slot; (void)off; (void)dst; (void)len; return -NV_EIO;
}
int disk_volume_read(u32 v, u64 lba, void *out) { (void)v; (void)lba; (void)out; return -NV_EIO; }
int disk_volume_write(u32 v, u64 lba, const void *out) { (void)v; (void)lba; (void)out; return -NV_EIO; }
int store_write_error(u32 v) { (void)v; return 0; }
static struct task task_at(int cwd) {
    struct task t = {.cwd = cwd};
    for (u32 i = 0; i < NV_OPEN_MAX; ++i) t.fd[i].node = -1;
    return t;
}
static void entropy_devices(void) {
    struct task t=task_at(home_node);
    int fd=fs_open(&t,"/dev/urandom",NV_READ);assert(fd>=3);
    u8 bytes[300];memset(bytes,0xa5,sizeof(bytes));
    assert(fs_read(&t,fd,bytes,300)==-NV_EIO);
    for(u32 i=0;i<sizeof(bytes);++i)assert(bytes[i]==0xa5);
    u32 calls=entropy_calls;assert(!fs_read(&t,fd,bytes,0) && entropy_calls==calls);
    entropy_available=true;
    assert(fs_read(&t,fd,bytes,300)==256 && bytes[256]==0xa5);
    u8 first=bytes[0];assert(fs_read(&t,fd,bytes,1)==1 && bytes[0]!=first);
    struct nv_stat64 st;assert(!fs_stat64(&t,fd,&st) && st.kind==NV_DEVICE);
    assert(!fs_close(&t,fd));fd=fs_open(&t,"/dev/random",NV_READ);assert(fd>=3);
    assert(fs_read(&t,fd,bytes,32)==32 && !fs_close(&t,fd));
    assert(fs_open(&t,"/dev/random",NV_WRITE)==-NV_EACCESS);
    assert(fs_open(&t,"/dev/urandom",NV_WRITE)==-NV_EACCESS);
    puts("PASS random devices: bounded hardware entropy, stream reads, zero-length reads, read-only access and unchanged output on failure");
}
static void shared_descriptions(void) {
    struct task t=task_at(home_node);
    int fd=fs_open(&t,"/home/shared",NV_READ|NV_WRITE|NV_CREATE);assert(fd>=3);
    assert(fs_write(&t,fd,"abcdef",6)==6 && fs_seek(&t,fd,0,0)==0);
    int other=fs_duplicate(&t,fd,31);assert(other==31 && t.fd[fd].sharing);
    char bytes[3]={0};assert(fs_read(&t,other,bytes,2)==2 && !strcmp(bytes,"ab"));
    assert(t.fd[fd].offset==2 && fs_read(&t,fd,bytes,2)==2 && !strcmp(bytes,"cd"));
    assert(t.fd[other].offset==4 && fs_seek(&t,other,1,0)==1 && t.fd[fd].offset==1);
    assert(fs_write(&t,fd,"X",1)==1 && t.fd[other].offset==2);
    assert(fs_pread64(&t,other,0,bytes,2)==2 && !strcmp(bytes,"aX") && t.fd[fd].offset==2);
    t.fd[other].flags|=NV_APPEND;fs_descriptor_sync(&t,other);
    assert((t.fd[fd].flags&NV_APPEND) && fs_write(&t,fd,"!",1)==1 && t.fd[other].offset==7);
    assert(fs_duplicate(&t,fd,NV_OPEN_MAX)==-NV_EINVAL);
    assert(fs_close(&t,fd)==0 && fs_remove(0,"/home/shared")==-NV_EBUSY);
    assert(fs_seek(&t,other,6,0)==6 && fs_read(&t,other,bytes,1)==1 && bytes[0]=='!');
    assert(fs_close(&t,other)==0 && fs_remove(0,"/home/shared")==0);
    for(u32 i=0;i<ARRAY_LEN(descriptor_references);++i)assert(!descriptor_references[i]);
    assert(!fs_mkdir(0,"/home/directory"));
    fd=fs_open_directory(&t,"/home/directory");assert(fd>=3);
    assert(fs_read(&t,fd,bytes,1)==-NV_EISDIR);
    assert(fs_remove(0,"/home/directory")==-NV_EBUSY);
    other=fs_duplicate(&t,fd,63);assert(other==63);
    assert(fs_close(&t,fd)==0 && fs_remove(0,"/home/directory")==-NV_EBUSY);
    assert(fs_close(&t,other)==0 && fs_remove(0,"/home/directory")==0);
    assert(fs_open_directory(&t,"/dev/null")==-NV_ENOTDIR);
    assert(!heap_used());
    puts("PASS shared descriptions: dup retains offsets/status across read/write/seek/close; positioned reads stay independent; directory references prevent removal");
}
int main(void) {
    heap = aligned_alloc(PAGE, HEAP_SIZE);
    assert(heap);
    head = (struct block *)heap;
    *head = (struct block){.magic = BLOCK_MAGIC, .size = HEAP_SIZE - sizeof(*head), .free = 1};
    fs_init();
    entropy_devices();
    shared_descriptions();
    /* The old inline snapshot remains readable and writable. */
    const char path[] = "/home/legacy";
    u32 pathlen = sizeof(path) - 1, size = 128 * 1024 - 1;
    u8 *snapshot = malloc(16 + pathlen + size);
    assert(snapshot);
    u32 header[] = {1, NV_FILE, pathlen, size};
    memcpy(snapshot, header, sizeof(header));
    memcpy(snapshot + sizeof(header), path, pathlen);
    memset(snapshot + sizeof(header) + pathlen, 0x5a, size);
    assert(fs_import_home(snapshot, 16 + pathlen + size) == 0);
    free(snapshot);
    struct task t = task_at(home_node);
    int fd = fs_open(&t, path, NV_READ | NV_WRITE);
    assert(fd >= 3 && fs_seek(&t, fd, size, 0) == (int)size);
    assert(fs_write(&t, fd, "!", 1) == 1);
    assert(fs_seek(&t, fd, size - 1, 0) == (int)size - 1);
    char pair[2];
    assert(fs_read(&t, fd, pair, 2) == 2 && pair[0] == 0x5a && pair[1] == '!');
    assert(fs_close(&t, fd) == 0 && fs_remove(0, path) == 0);

    /* Sparse far seek allocates one data page plus its pointer index. */
    fd = fs_open(&t, "/home/sparse", NV_READ | NV_WRITE | NV_CREATE);
    assert(fd >= 3);
    u32 before = heap_used();
    void *filler = kmalloc(head->size - 8192 - sizeof(struct block));
    assert(filler);
    assert(fs_seek(&t, fd, NV_FILE_MAX - 1, 0) == (int)NV_FILE_MAX - 1);
    assert(fs_write(&t, fd, "x", 1) == -NV_ENOMEM);
    int n = fs_lookup(0, "/home/sparse");
    assert(nodes[n].size == 0 && nodes[n].pages == NULL);
    kfree(filler);
    assert(heap_used() == before);
    assert(fs_write(&t, fd, "x", 1) == 1 && nodes[n].size == NV_FILE_MAX);
    assert(nodes[n].page_count == NV_FILE_MAX / PAGE);
    assert(fs_write(&t, fd, "y", 1) == -NV_ENOSPC);
    assert(fs_seek(&t, fd, 0, 0) == 0 && fs_read(&t, fd, pair, 2) == 2);
    assert(!pair[0] && !pair[1]);
    assert(fs_seek(&t, fd, NV_FILE_MAX - 1, 0) == (int)NV_FILE_MAX - 1);
    assert(fs_read(&t, fd, pair, 1) == 1 && pair[0] == 'x');
    assert(fs_close(&t, fd) == 0 && fs_remove(0, "/home/sparse") == 0);
    assert(heap_used() == 0);

    fd = fs_open(&t, "/home/rollback", NV_READ | NV_WRITE | NV_CREATE);
    assert(fd >= 3 && fs_write(&t, fd, "A", 1) == 1);
    n = fs_lookup(0, "/home/rollback");
    assert(nodes[n].page_count >= 3);
    struct block *free_block = head;
    while (free_block->next) free_block = free_block->next;
    assert(free_block->free);
    filler = kmalloc(free_block->size - PAGE - 128 - sizeof(struct block));
    assert(filler);
    before = heap_used();
    assert(fs_seek(&t, fd, PAGE, 0) == PAGE);
    u8 two_pages[PAGE * 2]; memset(two_pages, 0xb4, sizeof(two_pages));
    assert(fs_write(&t, fd, two_pages, sizeof(two_pages)) == -NV_ENOMEM);
    assert(nodes[n].size == 1 && !nodes[n].pages[1] && !nodes[n].pages[2]);
    assert(heap_used() == before && t.fd[fd].offset == PAGE);
    kfree(filler);
    assert(fs_write(&t, fd, two_pages, sizeof(two_pages)) == (int)sizeof(two_pages));
    assert(fs_close(&t, fd) == 0 && fs_remove(0, "/home/rollback") == 0);
    assert(heap_used() == 0);

    fd = fs_open(&t, "/home/large", NV_READ | NV_WRITE | NV_CREATE);
    u8 block[PAGE]; memset(block, 0xa3, sizeof(block));
    for (u32 i = 0; i < 1024 * 1024; i += PAGE)
        assert(fs_write(&t, fd, block, PAGE) == PAGE);
    assert(fs_seek(&t, fd, 500000, 0) == 500000);
    assert(fs_read(&t, fd, pair, 2) == 2 && (u8)pair[0] == 0xa3);
    u8 positioned[49154];memset(positioned,0,sizeof(positioned));
    assert(fs_pread64(&t,fd,12345,positioned,sizeof(positioned))==sizeof(positioned));
    for(u32 i=0;i<sizeof(positioned);++i)assert(positioned[i]==0xa3);
    assert(t.fd[fd].offset==500002 && !fs_pread64(&t,fd,0x100000000ull,positioned,1));
    assert(fs_pread64(&t,2,0,positioned,1)==-NV_EBADF);
    /* Loading a paged executable borrows no file pages and rolls back its
     * temporary materialization on both allocation and backing-read errors. */
    const u8 *image; u8 *temporary; usize image_length;
    before = heap_used();
    assert(fs_blob("/home/large", &image, &image_length, &temporary) == 0);
    assert(image == temporary && image_length == 1024 * 1024);
    for (u32 i = 0; i < image_length; ++i) assert(image[i] == 0xa3);
    kfree(temporary); assert(heap_used() == before);
    free_block = head;
    while (free_block->next) free_block = free_block->next;
    filler = kmalloc(free_block->size - 256 - sizeof(struct block));
    assert(filler);
    u32 pressure = heap_used();
    assert(fs_blob("/home/large", &image, &image_length, &temporary) == -NV_ENOMEM);
    assert(!image && !image_length && !temporary && heap_used() == pressure);
    kfree(filler);
    n = fs_lookup(0, "/home/large");
    u8 *saved_page = nodes[n].pages[0];
    nodes[n].pages[0] = NULL; nodes[n].backing_slot = 0;
    nodes[n].backing_size = (u32)nodes[n].size;
    assert(fs_blob("/home/large", &image, &image_length, &temporary) == -NV_EIO);
    assert(!image && !image_length && !temporary && heap_used() == before);
    nodes[n].pages[0] = saved_page; nodes[n].backing_slot = -1; nodes[n].backing_size = 0;
    assert(fs_close(&t, fd) == 0 && fs_remove(0, "/home/large") == 0);

    fs_mount_volumes(2);
    int droot = fs_lookup(0, "D:/");
    assert(droot > 0 && fs_lookup(0, "C:/") == home_node);
    assert(fs_mkdir(0, "C:/notes") == 0 && fs_mkdir(0, "D:/notes") == 0);
    assert(fs_move(0, "C:/notes", "D:/stolen") == -NV_EINVAL);
    struct task d = task_at(droot);
    fd = fs_open(&d, "D:/notes/file", NV_WRITE | NV_CREATE);
    assert(fd >= 3 && fs_write(&d, fd, "second drive", 12) == 12);
    assert(fs_close(&d, fd) == 0);
    u8 packed[4096];
    u32 length;
    assert(fs_export_volume(1, packed, sizeof(packed), &length) == 0 && length > 4);
    assert(fs_import_volume(0, packed, length) == -NV_EIO);
    assert(fs_remove(0, "D:/notes/file") == 0 && fs_remove(0, "D:/notes") == 0);
    assert(fs_import_volume(1, packed, length) == 0);
    assert(fs_lookup(0, "D:/notes/file") > 0 && fs_lookup(0, "C:/notes") > 0);
    free(heap);
    puts("PASS paged files: sparse 64 MiB seek, low-RAM rollback, legacy data and volumes");
}
