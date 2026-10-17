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
static u32 live, budget = POOL_PAGES, page_allocations;
bool cpu_random(void *out,u32 n) { memset(out,0x7b,n);return true; }
volatile u64 ticks;
uptr page_alloc(void) {
    if (!budget) return 0;
    for (u32 i=0; i<POOL_PAGES; ++i) if (!owned[i]) {
        owned[i]=true; ++live; --budget; ++page_allocations;
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
static u8 image[PAGE],linker_image[PAGE];
static bool linker_allowed=true;
int fs_authorize(const struct task *t,int cwd,const char *path,u32 operation) {
    (void)t;(void)cwd;(void)path;(void)operation;return linker_allowed?0:-NV_EACCESS;
}
int fs_blob(const char *path,const u8 **data,usize *length,u8 **temporary) {
    *data=!strcmp(path,"/home/ldso")?linker_image:image; *length=sizeof(image); *temporary=NULL; return 0;
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
    assert(!vm_protect(pd,NV_USER_HEAP,P_EXEC) && !user_range(pd,NV_USER_HEAP,1,true));
    assert(!(*leaf(pd,NV_USER_HEAP,false)&NX));
    assert(vm_protect(pd,NV_USER_HEAP,P_EXEC|P_WRITE)==-NV_EINVAL);
    assert(!vm_protect(pd,NV_USER_HEAP,P_WRITE) && user_range(pd,NV_USER_HEAP,1,true));
    assert(*leaf(pd,NV_USER_HEAP,false)&NX);
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
static void loader_pie(void) {
    reset(); executable(0);
    ((struct elf_header *)(void *)image)->type=3;
    struct task task={0}; struct frame frame;
    assert(prepare_image(&task,"/apps/pie","PIE arguments",&frame)==0);
    assert(task.abi==NV_ABI_VERSION && frame.eip==NV_USER_IMAGE);
    vm_destroy(task.pd); assert(!live);
}
static void pie_image(void) {
    executable(0);
    struct elf_header *h=(void *)image;h->type=3;h->phnum=5;
    struct program_header *p=(void *)(image+h->phoff);
    p[0]=(struct program_header){.type=1,.flags=5,.offset=512,.filesz=16,.memsz=PAGE,.align=1};
    p[1]=(struct program_header){.type=1,.flags=4,.offset=768,.vaddr=0x1000,.filesz=16,.memsz=PAGE,.align=1};
    p[2]=(struct program_header){.type=1,.flags=6,.offset=1024,.vaddr=0x2000,.filesz=512,.memsz=PAGE+8,.align=1};
    p[3]=(struct program_header){.type=2,.flags=6,.offset=1088,.vaddr=0x2040,.filesz=80,.memsz=80,.align=8};
    p[4]=(struct program_header){.type=ELF_RELRO,.vaddr=0x2000,.memsz=PAGE};
    struct elf_dynamic *d=(void *)(image+1088);
    d[0]=(struct elf_dynamic){7,0x2100};d[1]=(struct elf_dynamic){8,24};
    d[2]=(struct elf_dynamic){9,24};d[3]=(struct elf_dynamic){0x6ffffffb,0x08000001};
    d[4]=(struct elf_dynamic){0,0};
    *(struct elf_rela *)(void *)(image+1280)=(struct elf_rela){0x2000,8,0x1000};
    image[512]=0xc3;image[768]=0x5a;
}
static u64 mapped_word(pte_t *pd,uptr address) {
    u64 result=0;
    for (u32 i=0;i<8;++i) result|=(u64)*(u8 *)phys_ptr(vm_translate(pd,address+i))<<(8*i);
    return result;
}
static void pie_relocations(void) {
    for (u32 cross=0;cross<2;++cross) {
        reset();pie_image();
        struct elf_rela *r=(void *)(image+1280);
        if (cross) r->offset=0x2ffd;
        struct task task={0};struct frame frame;
        assert(!prepare_image(&task,"/apps/relative","argument",&frame));
        assert(task.abi==NV_ABI_VERSION && frame.eip==NV_USER_IMAGE);
        assert(mapped_word(task.pd,NV_USER_IMAGE+r->offset)==NV_USER_IMAGE+0x1000);
        assert(!user_range(task.pd,NV_USER_IMAGE+0x2000,1,true));
        assert(user_range(task.pd,NV_USER_IMAGE+0x3000,8,true));
        assert((*leaf(task.pd,NV_USER_IMAGE+0x2000,false)&NX) &&
               !(*leaf(task.pd,NV_USER_IMAGE,false)&NX));
        assert(*(u8 *)phys_ptr(vm_translate(task.pd,NV_USER_IMAGE+0x2800))==0);
        assert(!strcmp(phys_ptr(vm_translate(task.pd,frame.ebx)),"argument"));
        u32 before=live;
        assert(vm_readonly(task.pd,NV_USER_IMAGE+1)==-NV_EINVAL);
        assert(vm_readonly(task.pd,NV_USER_IMAGE+0x4000)==-NV_EFAULT);
        assert(!vm_readonly(task.pd,NV_USER_IMAGE+0x2000) && live==before);
        vm_destroy(task.pd);assert(!live);
    }
    for (u32 which=0;which<28;++which) {
        reset();pie_image();
        struct elf_header *h=(void *)image;
        struct program_header *p=(void *)(image+h->phoff);
        struct elf_dynamic *d=(void *)(image+1088);
        struct elf_rela *r=(void *)(image+1280);
        switch (which) {
        case 0: p[3].type=3;break; /* Dynamic interpreter. */
        case 1: p[3].type=7;break; /* TLS. */
        case 2: d[3].tag=1;break; /* DT_NEEDED. */
        case 3: d[3].tag=22;break; /* DT_TEXTREL. */
        case 4: d[3].tag=17;break; /* DT_REL. */
        case 5: d[3].tag=23;break; /* DT_JMPREL. */
        case 6: d[3].tag=12;break; /* Constructors need another startup ABI. */
        case 7: d[3]=d[0];break; /* Duplicate relocation tag. */
        case 8: d[4].tag=5;break; /* Missing DT_NULL. */
        case 9: d[1].value=23;break;
        case 10: d[2].value=16;break;
        case 11: d[0].value=0x21f8;break; /* Table extends into zero-fill. */
        case 12: r->info=1;break; /* Unsupported relocation kind. */
        case 13: r->info=(1ull<<32)|8;break; /* Nonzero symbol. */
        case 14: r->offset=0;break; /* Text modification. */
        case 15: r->offset=0x1000;break; /* Read-only data modification. */
        case 16: r->offset=0x3001;break; /* Eight bytes do not fit. */
        case 17: r->addend=-1;break;
        case 18: r->addend=(i64)(NV_USER_IMAGE_END-NV_USER_IMAGE);break;
        case 19: p[3].offset++;break; /* Dynamic file/virtual address mismatch. */
        case 20: p[3].filesz--;break;
        case 21: p[4]=p[3];break; /* Duplicate PT_DYNAMIC. */
        case 22: p[4].vaddr=0x1000;break; /* RELRO not in writable LOAD. */
        case 23: p[4].memsz=~0ull;break;
        case 24: p[2].vaddr=~0ull-7;break;
        case 25: p[4].type=ELF_STACK;p[4].flags=7;break;
        case 26: d[3]=(struct elf_dynamic){0x6ffffff9,2};break;
        case 27: d[3]=(struct elf_dynamic){30,4};break; /* DF_TEXTREL. */
        }
        struct task task={.pd=vm_create()};assert(task.pd);
        u32 before=live,allocations=page_allocations;uptr entry=0;
        assert(load_elf(&task,image,sizeof(image),&entry)==-NV_ENOEXEC);
        assert(live==before && page_allocations==allocations && !task.abi && !entry);
        vm_destroy(task.pd);assert(!live);
    }
    /* Every allocation failure in a valid PIE leaves a reclaimable image. */
    for (u32 limit=0;limit<24;++limit) {
        reset();pie_image();budget=limit;
        struct task task={0};struct frame frame;
        int result=prepare_image(&task,"/apps/relative","",&frame);
        assert(result==0 || result==-NV_ENOMEM);
        vm_destroy(task.pd);assert(!live && budget==limit);
    }
    puts("PASS static PIE: relative/cross-page fixups, BSS, arguments, NX/RELRO, 28 malformed images rejected before mapping and 24 OOM budgets");
}
static void loader_validation_budget(void) {
    for (u32 which=0;which<6;++which) {
        reset(); executable(NV_USER_IMAGE);
        struct elf_header *h=(void *)image;
        struct program_header *ph=(void *)(image+h->phoff);
        ph[0].memsz=(u64)POOL_PAGES*PAGE;
        h->phnum=2;
        ph[1]=(struct program_header){.type=1,.flags=6,.offset=256,
            .vaddr=NV_USER_IMAGE+ph[0].memsz,.filesz=1,.memsz=PAGE,.align=1};
        if (which==0) ph[1].type=3; /* A late unsupported PT_INTERP. */
        if (which==1) ph[1].offset=sizeof(image); /* Data extends beyond EOF. */
        if (which==2) ph[1].flags=7; /* Writable executable segment. */
        if (which==3) h->entry=NV_USER_HEAP; /* Entry outside executable data. */
        if (which==4) ph[1].vaddr=USER_BASE; /* Mixed legacy/native layout. */
        if (which==5) {
            ph[0].memsz=(u64)(POOL_PAGES-1)*PAGE+1;
            ph[1].vaddr=NV_USER_IMAGE+ph[0].memsz; /* Adjacent bytes, shared page. */
        }
        struct task task={.pd=vm_create()}; assert(task.pd);
        u32 before=live, allocations_before=page_allocations;
        uptr entry=0;
        assert(load_elf(&task,image,sizeof(image),&entry)==-NV_ENOEXEC);
        assert(live==before && page_allocations==allocations_before && !entry && !task.abi);
        vm_destroy(task.pd); assert(!live);
    }
    /* A valid BSS larger than the remaining physical budget still reports
     * OOM; the caller can reclaim its complete partially mapped image. */
    reset(); executable(NV_USER_IMAGE);
    struct elf_header *h=(void *)image;
    struct program_header *ph=(void *)(image+h->phoff);
    ph[0].memsz=(u64)POOL_PAGES*PAGE;
    struct task task={0}; struct frame frame;
    assert(prepare_image(&task,"/apps/large-bss","",&frame)==-NV_ENOMEM);
    assert(task.abi==NV_ABI_VERSION && live>2);
    vm_destroy(task.pd); assert(!live && budget==POOL_PAGES);
}
static void linux_fixture(void) {
    memset(image,0,sizeof(image));
    struct elf_header *h=(void *)image;memcpy(h->ident,"\177ELF\2\1\1",7);
    h->type=3;h->machine=62;h->version=1;h->ehsize=sizeof(*h);
    h->phoff=sizeof(*h);h->phnum=4;h->phentsize=sizeof(struct program_header);h->entry=0x1000;
    struct program_header *p=(void *)(image+h->phoff);
    p[0]=(struct program_header){.type=1,.flags=4,.filesz=512,.memsz=PAGE,.align=1};
    p[1]=(struct program_header){.type=1,.flags=5,.offset=1024,.vaddr=0x1000,.filesz=16,.memsz=PAGE,.align=1};
    p[2]=(struct program_header){.type=1,.flags=6,.offset=2048,.vaddr=0x2000,.filesz=8,.memsz=PAGE,.align=1};
    p[3]=(struct program_header){.type=7,.flags=4,.offset=2048,.vaddr=0x2000,.filesz=8,.memsz=16,.align=8};
}
static void mapped_text(pte_t *pd,uptr at,const char *expected) {
    for(u32 i=0;i<=strlen(expected);i++)assert(*(u8 *)phys_ptr(vm_translate(pd,at+i))==(u8)expected[i]);
}
static void linux_initial_stack(void) {
    reset();linux_fixture();struct task t={.uid=42,.linux_compat={.enabled=true}};struct frame frame;
    const char *args="\"\" \"two words\" \"a\\\"b\" \"C:\\\\path\"";
    assert(!prepare_image(&t,"/home/program.elf",args,&frame));
    assert(t.abi==2 && frame.eip==NV_USER_IMAGE+0x1000 && !(frame.useresp&15));
    assert(mapped_word(t.pd,frame.useresp)==5 && t.linux_compat.brk==NV_USER_HEAP);
    const char *wanted[]={"/home/program.elf","","two words","a\"b","C:\\path"};
    for(u32 i=0;i<ARRAY_LEN(wanted);i++)mapped_text(t.pd,mapped_word(t.pd,frame.useresp+(i+1)*8),wanted[i]);
    uptr at=frame.useresp+6*8;assert(!mapped_word(t.pd,at));at+=8;
    while(mapped_word(t.pd,at))at+=8;
    at+=8;u32 seen=0;
    for(;;at+=16) {
        u64 key=mapped_word(t.pd,at),value=mapped_word(t.pd,at+8);if(!key)break;
        if(key==3) {assert(value==NV_USER_IMAGE+64);seen|=1;}
        if(key==6) {assert(value==PAGE);seen|=2;}
        if(key==25) {assert(mapped_word(t.pd,value)==0x7b7b7b7b7b7b7b7bull);seen|=4;}
        if(key==11) {assert(value==42);seen|=8;}
    }
    assert(seen==15);vm_destroy(t.pd);assert(!live);
    for(u32 which=0;which<14;which++) {
        reset();linux_fixture();struct elf_header *h=(void *)image;
        struct program_header *p=(void *)(image+h->phoff);
        switch(which) {
        case 0:h->type=2;break;case 1:h->phoff=~0ull;break;case 2:h->phnum=33;break;
        case 3:p[2].offset=sizeof(image);break;case 4:p[2].filesz=PAGE+1;break;
        case 5:p[2].vaddr=~0ull;break;case 6:p[1].flags=7;break;case 7:p[3].type=3;break;
        case 8:h->entry=0;break;case 9:p[0].filesz=1;break;case 10:p[2].vaddr=0x1000;break;
        case 11:p[3].type=2;p[3].filesz=1;break;case 12:p[2].memsz=LINUX_MEMORY_MAX+PAGE;break;
        case 13:p[2].align=3;break;
        }
        struct task bad={.pd=vm_create(),.linux_compat={.enabled=true}};assert(bad.pd);uptr entry=0,phdr=0;
        u32 before=live;assert(linux_image(&bad,image,sizeof(image),&entry,&phdr)<0);
        assert(live==before && !vm_page_count(bad.pd));vm_destroy(bad.pd);assert(!live);
    }
    puts("PASS Linux ELF: static PIE/TLS template, quoted argv/env/auxv, random seed, aligned stack and malformed-image rejection before mapping");
}
static void linux_dynamic_entry(void) {
    reset();linux_fixture();memcpy(linker_image,image,sizeof(image));
    struct elf_header *h=(void *)image;struct program_header *p=(void *)(image+h->phoff);
    h->phnum=5;memcpy(image+3000,"/home/ldso",11);
    p[4]=(struct program_header){.type=3,.offset=3000,.filesz=11,.memsz=11,.align=1};
    struct task t={.uid=42,.linux_compat={.enabled=true}};struct frame f;
    assert(!prepare_image(&t,"/home/dynamic","arg",&f));
    assert(f.eip==LINUX_INTERP_BASE+0x1000 && t.linux_compat.interpreter==LINUX_INTERP_BASE);
    assert(user_range(t.pd,NV_USER_IMAGE+0x1000,1,false) && user_range(t.pd,LINUX_INTERP_BASE+0x1000,1,false));
    uptr at=f.useresp+4*8;while(mapped_word(t.pd,at))at+=8;at+=8;u32 seen=0;
    for(;;at+=16) {
        u64 key=mapped_word(t.pd,at),value=mapped_word(t.pd,at+8);if(!key)break;
        if(key==7) {assert(value==LINUX_INTERP_BASE);seen|=1;}
        if(key==9) {assert(value==NV_USER_IMAGE+0x1000);seen|=2;}
    }
    assert(seen==3);vm_destroy(t.pd);assert(!live);
    linker_allowed=false;t=(struct task){.linux_compat={.enabled=true}};
    assert(prepare_image(&t,"/home/dynamic","",&f)==-NV_EACCESS);
    assert(!vm_page_count(t.pd));vm_destroy(t.pd);linker_allowed=true;assert(!live);
    ((struct elf_header *)linker_image)->phnum=5;
    ((struct program_header *)(linker_image+sizeof(struct elf_header)))[4]=p[4];
    memcpy(linker_image+3000,"/home/ldso",11);
    t=(struct task){.linux_compat={.enabled=true}};
    assert(prepare_image(&t,"/home/dynamic","",&f)==-NV_ENOEXEC);
    assert(!vm_page_count(t.pd));vm_destroy(t.pd);assert(!live);
    puts("PASS dynamic ELF: independently validated interpreter, entry handoff, AT_BASE/AT_ENTRY, permission denial and recursive interpreter rejection");
}
static void guarded_pages(void) {
    reset();pte_t *pd=vm_create(),*staged=vm_create();assert(pd && staged);
    uptr at=LINUX_MAP_BASE;u32 flags;
    assert(!vm_map(pd,at,P_WRITE));uptr physical=vm_translate(pd,at);memset(phys_ptr(physical),0xa6,PAGE);
    assert(!vm_protect(pd,at,P_NOACCESS) && !user_range(pd,at,1,false) && !vm_translate(pd,at));
    assert(vm_page_count(pd)==1 && vm_count_range(pd,at,at+LINUX_VIRTUAL_MAX)==1);
    assert(!vm_page_flags(pd,at,&flags) && flags==P_NOACCESS);
    assert(vm_protect(pd,at,P_WRITE|P_EXEC)==-NV_EINVAL);
    assert(!vm_protect(pd,at,P_WRITE|P_EXEC|P_JIT));
    assert(!vm_page_flags(pd,at,&flags) && flags==(P_WRITE|P_EXEC));
    vm_change_range(pd,at,at+PAGE,P_WRITE);
    assert(user_range(pd,at,PAGE,true) && *(u8 *)phys_ptr(vm_translate(pd,at))==0xa6);
    assert(!vm_map(staged,at,P_WRITE));memset(phys_ptr(vm_translate(staged,at)),0x3b,PAGE);
    assert(!vm_prepare(pd,at) && !vm_move_page(pd,staged,at));
    assert(!vm_page_count(staged) && *(u8 *)phys_ptr(vm_translate(pd,at))==0x3b);
    vm_clear_range(pd,at,at+LINUX_VIRTUAL_MAX);assert(!*(u8 *)phys_ptr(vm_translate(pd,at)));
    assert(!vm_protect(pd,at,P_NOACCESS));vm_unmap_range(pd,at,at+LINUX_VIRTUAL_MAX);
    assert(!vm_page_count(pd));vm_destroy(staged);vm_destroy(pd);assert(!live);
    puts("PASS VM retained guards, physical ownership transfer, zeroing and sparse multi-gigabyte unmap without leaks");
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
    mapping_boundaries(); loader_and_heap(); loader_validation_budget(); loader_pie(); pie_relocations(); linux_initial_stack(); linux_dynamic_entry(); guarded_pages(); framebuffer_masks();
    assert(!munmap(ram,bytes));
    puts("PASS native64: high physical backing, cross-4GiB copies, multi-PML4 walks, ELF/stack/heap, full validation before allocation, OOM rollback, legacy ELF and GOP masks");
}
