#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../kernel/kernel.h"
#include <nv/pixel.h>
/* Every physical backing page is above 4 GiB, including page tables. */
#define POOL_PAGES 128u
static const uptr physical_base = 0x100200000ull;
static bool owned[POOL_PAGES];
static u32 live, budget = POOL_PAGES;
volatile u64 ticks;
uptr page_alloc(void) {
    if (!budget) return 0;
    for (u32 i=0; i<POOL_PAGES; ++i) if (!owned[i]) {
        owned[i]=true; ++live; --budget;
        uptr p=physical_base+(uptr)i*PAGE;
        memset(phys_ptr(p),0,PAGE); return p;
    }
    return 0;
}
void page_free(uptr p) {
    assert(p>=physical_base && !(p%PAGE));
    u32 i=(u32)((p-physical_base)/PAGE);
    assert(i<POOL_PAGES && owned[i]);
    owned[i]=false; --live; ++budget;
}
u32 pages_free(void) { return budget; }
void panic(const char *message) { fprintf(stderr,"PANIC: %s\n",message); abort(); }
static void fixture_load_cr3(uptr p) { (void)p; }
#define load_cr3 fixture_load_cr3
#include "../kernel/memory64.c"
#include "../kernel/task.c"
#include "../kernel/usercopy.c"
static u8 image[PAGE];
int fs_blob(const char *path,const u8 **data,usize *length,u8 **temporary) {
    (void)path; *data=image; *length=sizeof(image); *temporary=NULL; return 0;
}
void kfree(void *p) { assert(!p); }
static void reset(void) {
    assert(!live); memset(owned,0,sizeof(owned)); budget=POOL_PAGES;
}
static void mapping_boundaries(void) {
    reset(); pte_t *pd=vm_create(); assert(pd);
    uptr addresses[]={0xfffff000ull,0x100000000ull,0x100001000ull,
        NV_USER_HEAP,0x20000000000ull,NV_USER_STACK-PAGE,NV_USER_END-PAGE};
    for (u32 i=0;i<ARRAY_LEN(addresses);++i) {
        assert(vm_map(pd,addresses[i],P_WRITE)==0);
        assert(vm_translate(pd,addresses[i])>=0x100000000ull);
        assert(user_range(pd,addresses[i]+7,PAGE-7,true));
    }
    assert(user_range(pd,0xfffffff0ull,32,true));
    assert(user_range(pd,0x100000ff0ull,32,true));
    assert(!user_range(pd,NV_USER_END-1,2,false));
    assert(!user_range(pd,NV_USER_END,1,false));
    assert(!user_range(pd,~(uptr)0-4,16,false));
    assert(!user_range(pd,NV_PHYS_WINDOW+physical_base,1,false));
    assert(vm_map(pd,NV_USER_END,P_WRITE)==-NV_EINVAL);
    assert(vm_map(pd,0xffff800000001000ull,P_WRITE)==-NV_EINVAL);
    assert(vm_map(pd,NV_USER_HEAP+PAGE,P_WRITE|P_EXEC)==-NV_EINVAL);
    assert(vm_map(pd,NV_USER_HEAP,P_WRITE)==-NV_EEXIST);
    u8 bytes[32]; memset(bytes,0xa7,sizeof(bytes));
    assert(copy_to_space(pd,0xfffffff0ull,bytes,sizeof(bytes))==0);
    assert(*(u8 *)phys_ptr(vm_translate(pd,0x100000000ull))==0xa7);
    assert(vm_page_count(pd)==ARRAY_LEN(addresses));
    vm_destroy(pd); assert(!live);
    for (u32 n=0;n<4;++n) {
        reset(); pd=vm_create(); assert(pd); u32 before=live;
        budget=n;
        assert(vm_map(pd,0x20000000000ull,P_WRITE)==-NV_ENOMEM);
        assert(live==before && !vm_page_count(pd));
        budget=POOL_PAGES-live; vm_destroy(pd); assert(!live);
    }
}
static void executable(uptr address) {
    memset(image,0,sizeof(image));
    struct elf_header *h=(void *)image;
    memcpy(h->ident,"\177ELF\2\1\1",7);
    h->type=2; h->machine=62; h->version=1;
    h->ehsize=sizeof(*h); h->phoff=sizeof(*h); h->phnum=1;
    h->phentsize=sizeof(struct program_header); h->entry=address;
    struct program_header *ph=(void *)(image+h->phoff);
    *ph=(struct program_header){.type=1,.flags=5,.offset=256,
        .vaddr=address,.filesz=1,.memsz=PAGE,.align=1};
    image[256]=0xc3;
}
static void loader_and_heap(void) {
    reset(); struct task task={0}; struct frame frame;
    executable(NV_USER_IMAGE);
    assert(prepare_image(&task,"/apps/high","native args",&frame)==0);
    assert(task.abi==2 && frame.eip==NV_USER_IMAGE && frame.useresp>0xffffffffull);
    assert(!user_range(task.pd,NV_USER_IMAGE,1,true));
    assert(user_range(task.pd,frame.ebx,12,true));
    assert(!strcmp(phys_ptr(vm_translate(task.pd,frame.ebx)),"native args"));
    current=&task; task.heap_end=NV_USER_HEAP;
    u32 before=live;
    assert(task_grow(2)==(iptr)NV_USER_HEAP && task.heap_end==NV_USER_HEAP+2*PAGE);
    assert(user_range(task.pd,NV_USER_HEAP,2*PAGE,true));
    assert(task_grow(-2)==(iptr)(NV_USER_HEAP+2*PAGE) && live==before);
    assert(task_grow((i64)1<<32)==-NV_ENOMEM && task.heap_end==NV_USER_HEAP);
    assert(task_grow((-9223372036854775807ll-1))==-NV_EINVAL);
    budget=0;
    assert(task_grow(1)==-NV_ENOMEM && task.heap_end==NV_USER_HEAP && live==before);
    budget=POOL_PAGES-live; vm_destroy(task.pd); assert(!live);
    task=(struct task){0}; executable(USER_BASE);
    assert(prepare_image(&task,"/apps/legacy","old args",&frame)==0);
    assert(task.abi==1 && frame.eip==USER_BASE && frame.useresp<0x80000000ull);
    vm_destroy(task.pd); assert(!live);
    task=(struct task){0}; executable(NV_USER_IMAGE);
    ((struct elf_header *)(void *)image)->phoff=0x100000040ull;
    task.pd=vm_create(); uptr entry;
    assert(load_elf(&task,image,sizeof(image),&entry)==-NV_ENOEXEC);
    vm_destroy(task.pd); assert(!live);
}
static void framebuffer_masks(void) {
    u32 rgb[]={0xff0000,0xff00,0xff,0};
    u32 bgr[]={0xff,0xff00,0xff0000,0};
    u32 deep[]={0x3ff00000,0xffc00,0x3ff,0xc0000000};
    assert(nv_pixel_masks_valid(rgb) && nv_pixel_masks_valid(bgr) && nv_pixel_masks_valid(deep));
    assert(nv_pixel_pack(0x123456,rgb)==0x123456);
    assert(nv_pixel_pack(0x123456,bgr)==0x563412);
    assert(nv_pixel_pack(0xffffff,deep)==0x3fffffff);
    deep[0]=deep[1]; assert(!nv_pixel_masks_valid(deep));
}
int main(void) {
    usize bytes=POOL_PAGES*PAGE;
    void *ram=mmap(phys_ptr(physical_base),bytes,PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(ram==phys_ptr(physical_base));
    mapping_boundaries(); loader_and_heap(); framebuffer_masks();
    assert(!munmap(ram,bytes));
    puts("PASS native64: high physical backing, cross-4GiB copies, multi-PML4 walks, ELF/stack/heap, rollback, legacy ELF and GOP masks");
}
