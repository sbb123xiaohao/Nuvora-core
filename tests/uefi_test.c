#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <nv/bootinfo.h>
#define NV_UEFI_TEST_HOST
static u8 nv_uefi_host_inb(u16 port) { (void)port; return 0x20; }
static void nv_uefi_host_outb(u16 port, u8 value) { (void)port; (void)value; }
#include "../arch/x86_64/uefi.c"

static uptr position, file_length, reads, closes, allocations, mapped_length;
static uptr staging_allocations, staging_frees, overlap_length;
static u8 *overlap_pool;
static u8 *file_bytes;
static char16 printed[256];
static u32 print_length;
static efi_status MSABI mock_output(struct efi_simple_text_output *out, const char16 *line) {
    (void)out;
    while (*line) {
        assert(print_length < ARRAY_LEN(printed) - 1);
        printed[print_length++] = *line++;
    }
    return EFI_SUCCESS;
}
static struct efi_file root_file, input_file;
static bool deny_pages, deny_staging, bad_staging;
static efi_status target_status;
static efi_status MSABI mock_pool(u32 type, uptr size, void **out) {
    (void)type;
    *out = malloc(size);
    return *out ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
static efi_status MSABI mock_free(void *p) {
    if (p == overlap_pool) {
        assert(!munmap(p, overlap_length));
        overlap_pool = NULL;
    } else free(p);
    return EFI_SUCCESS;
}
static efi_status MSABI mock_close(struct efi_file *f) {
    (void)f; ++closes; return EFI_SUCCESS;
}
static efi_status MSABI mock_volume(struct efi_sfs *sfs, void **out) {
    (void)sfs; *out = &root_file; return EFI_SUCCESS;
}
static efi_status MSABI mock_open(struct efi_file *f, void **out, const char16 *path,
                                 u64 mode, u64 flags) {
    (void)f; (void)path; (void)mode; (void)flags;
    position = 0; *out = &input_file; return EFI_SUCCESS;
}
static efi_status MSABI mock_seek(struct efi_file *f, u64 p) {
    (void)f; position = p == ~0ull ? file_length : p; return EFI_SUCCESS;
}
static efi_status MSABI mock_tell(struct efi_file *f, u64 *out) {
    (void)f; *out = position; return EFI_SUCCESS;
}
static efi_status MSABI mock_read(struct efi_file *f, uptr *n, void *out) {
    (void)f; ++reads;
    *n = MIN(*n, MIN(file_length - position, 7u));
    memcpy(out, file_bytes + position, *n); position += *n;
    return EFI_SUCCESS;
}
static efi_status MSABI mock_pages(u32 kind, u32 type, uptr pages, u64 *address) {
    if (kind == EFI_ALLOCATE_MAX_ADDRESS) {
        assert(type == EFI_LOADER_DATA);
        ++staging_allocations;
        if (deny_staging) return EFI_DEVICE_ERROR;
        *address = bad_staging ? 0x2000000 : ((*address + 1) & ~4095ull) - pages * 4096;
        void *p = mmap((void *)(uptr)*address, pages * 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        assert(p == (void *)(uptr)*address);
        return EFI_SUCCESS;
    }
    assert(kind == EFI_ALLOCATE_ADDRESS && type == EFI_LOADER_CODE);
    ++allocations;
    if (target_status) return target_status;
    if (deny_pages) return EFI_NOT_FOUND;
    mapped_length = pages * 4096;
    void *p = mmap((void *)(uptr)*address, mapped_length, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(p == (void *)(uptr)*address);
    return EFI_SUCCESS;
}
static efi_status MSABI mock_free_pages(u64 address, uptr pages) {
    ++staging_frees;
    assert(!munmap((void *)(uptr)address, pages * 4096));
    return EFI_SUCCESS;
}
static struct efi_gop_mode_info gop_modes[3];
static u32 queries, sets, rejected_mode = 3;
static efi_status MSABI mock_query(struct efi_gop *g,u32 index,uptr *n,
                                  struct efi_gop_mode_info **out) {
    (void)g; assert(index<3); ++queries; *n=sizeof(**out);
    *out=malloc(*n); assert(*out); **out=gop_modes[index]; return EFI_SUCCESS;
}
static efi_status MSABI mock_set(struct efi_gop *g,u32 index) {
    ++sets;
    if (index==rejected_mode) return EFI_DEVICE_ERROR;
    g->mode->mode=index; g->mode->info=&gop_modes[index]; return EFI_SUCCESS;
}
static void check_gop(struct efi_boot_services *bs) {
    gop_modes[0]=(struct efi_gop_mode_info){.horizontal=800,.vertical=600,
        .pixels_per_scanline=800,.pixel_format=1};
    gop_modes[1]=(struct efi_gop_mode_info){.horizontal=640,.vertical=480,
        .pixels_per_scanline=640,.pixel_format=2,
        .pixel_information={0x3ff00000u,0x000ffc00u,0x000003ffu,0xc0000000u}};
    gop_modes[2]=(struct efi_gop_mode_info){.horizontal=640,.vertical=480,
        .pixels_per_scanline=640,.pixel_format=3};
    struct efi_gop_mode mode={.max_mode=3,.mode=2,.info=&gop_modes[2],
        .framebuffer_base=0x200000003ull,.framebuffer_size=800*600*4};
    struct efi_gop gop={.mode=&mode,.query_mode=mock_query,.set_mode=mock_set};
    select_framebuffer(bs,&gop);
    assert(queries==3 && sets==1 && mode.mode==1 && bi.fb.format==NV_FB_BITMASK);
    assert(bi.fb.address==0x200000003ull && bi.fb.masks[0]==0x3ff00000u);
    select_framebuffer(bs,&gop); assert(queries==3 && sets==1); /* Keep a usable active mode. */
    mode.info=&gop_modes[2]; rejected_mode=1; sets=0;
    select_framebuffer(bs,&gop); assert(sets==2 && mode.mode==0 && bi.fb.width==800);
    gop_modes[1].pixel_information[1]=gop_modes[1].pixel_information[0];
    mode.info=&gop_modes[1]; fill_framebuffer(&gop); assert(!bi.fb.format);
    mode.info=&gop_modes[0]; mode.framebuffer_base=(1ull<<52)-16;
    fill_framebuffer(&gop); assert(!bi.fb.format);
    fill_framebuffer(NULL); assert(!bi.fb.format);
}

static void check_acpi_tables(void) {
    /* Literal UEFI specification GUIDs catch an incorrect discovery constant. */
    struct efi_config_table tables[] = {
        {{0xeb9d2d30,0x2d88,0x11d3,{0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}},
         (void *)(uptr)0x12340000ull},
        {{0x8868e871,0xe4f1,0x11d3,{0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}},
         (void *)(uptr)0x100002000ull}
    };
    struct efi_system_table st={.number_of_table_entries=1,.configuration_table=tables};
    assert(find_rsdp(&st)==0x12340000ull);
    st.number_of_table_entries=2;
    assert(find_rsdp(&st)==0x100002000ull); /* Prefer ACPI 2 even after ACPI 1. */
    tables[1].vendor_table=NULL;
    assert(find_rsdp(&st)==0x12340000ull);
    tables[0].vendor_table=NULL;
    assert(!find_rsdp(&st));
}

/* Exercise the complete loader with explicit firmware allocation/error
 * callbacks. Memory services remain valid after a failed exit, and the ledger
 * rejects double frees and attempts to release firmware-owned destinations. */
enum flow_failure { FLOW_MAP_ERROR, FLOW_BAD_STRIDE, FLOW_POOL_ERROR,
    FLOW_REPLACEMENT_ERROR, FLOW_BAD_MAP, FLOW_RESERVED, FLOW_EXIT_ERROR,
    FLOW_EXIT_EXHAUSTED, FLOW_MAP_AFTER_EXIT_ERROR, FLOW_MAP_AFTER_EXIT_GROW };
static struct { void *address; uptr size; bool pages, mapped; } flow_blocks[8];
static u32 flow_live, flow_pools, flow_maps, flow_exits, flow_target_frees;
static enum flow_failure flow_failure;
static bool flow_relocate, flow_unowned;
static struct efi_file flow_root, flow_file;
static struct efi_sfs flow_sfs;
static struct efi_loaded_image flow_image = {.device_handle=(void *)2,
    .image_base=(void *)0x2000000, .image_size=0x20000};
static void flow_track(void *address, uptr size, bool pages, bool mapped) {
    for (u32 i=0;i<ARRAY_LEN(flow_blocks);++i) if (!flow_blocks[i].address) {
        flow_blocks[i]=(__typeof__(flow_blocks[0])){address,size,pages,mapped};
        ++flow_live;return;
    }
    assert(false);
}
static void flow_free(void *address, uptr size, bool pages) {
    for (u32 i=0;i<ARRAY_LEN(flow_blocks);++i) if (flow_blocks[i].address==address) {
        assert(flow_blocks[i].pages==pages && (!pages || flow_blocks[i].size==size));
        if (flow_blocks[i].mapped) assert(!munmap(address,flow_blocks[i].size));
        else free(address);
        flow_blocks[i]=(__typeof__(flow_blocks[0])){0};--flow_live;return;
    }
    assert(false);
}
static efi_status MSABI flow_pool(u32 type, uptr size, void **out) {
    assert(type==EFI_LOADER_POOL);++flow_pools;
    if ((flow_failure==FLOW_POOL_ERROR && flow_pools==2) ||
        (flow_failure==FLOW_REPLACEMENT_ERROR && flow_pools==3)) {
        /* Failed AllocatePool leaves no owned buffer, regardless of output. */
        assert(!*out);*out=(void *)0xdeadbeef;return EFI_DEVICE_ERROR;
    }
    bool mapped=flow_relocate && flow_pools==1;
    uptr allocated=mapped?ALIGN_UP(size,4096):size;
    *out=mapped?mmap((void *)0x1b00000,allocated,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0):malloc(size);
    assert(*out && *out!=MAP_FAILED && (!mapped || *out==(void *)0x1b00000));
    flow_track(*out,allocated,false,mapped);return EFI_SUCCESS;
}
static efi_status MSABI flow_free_pool(void *address) {
    flow_free(address,0,false);return EFI_SUCCESS;
}
static efi_status MSABI flow_pages(u32 kind, u32 type, uptr pages, u64 *address) {
    if (kind==EFI_ALLOCATE_ADDRESS) {
        assert(type==EFI_LOADER_CODE);
        if (flow_unowned) return EFI_NOT_FOUND;
    } else {
        assert(kind==EFI_ALLOCATE_MAX_ADDRESS && type==EFI_LOADER_DATA);
        *address=((*address+1)&~4095ull)-pages*4096;
    }
    void *p=mmap((void *)(uptr)*address,pages*4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(p==(void *)(uptr)*address);
    flow_track(p,pages*4096,true,true);return EFI_SUCCESS;
}
static efi_status MSABI flow_free_pages(u64 address, uptr pages) {
    if (address==kernel_first) ++flow_target_frees;
    flow_free((void *)(uptr)address,pages*4096,true);return EFI_SUCCESS;
}
static efi_status MSABI flow_open(struct efi_file *root, void **out,
        const char16 *path, u64 mode, u64 flags) {
    (void)root;(void)mode;(void)flags;
    if (path[12]=='C') return EFI_NOT_FOUND; /* No optional command line. */
    position=0;*out=&flow_file;return EFI_SUCCESS;
}
static efi_status MSABI flow_volume(struct efi_sfs *sfs, void **out) {
    (void)sfs;*out=&flow_root;return EFI_SUCCESS;
}
static efi_status MSABI flow_read(struct efi_file *file, uptr *n, void *out) {
    (void)file;*n=MIN(*n,file_length-position);
    memcpy(out,file_bytes+position,*n);position+=*n;return EFI_SUCCESS;
}
static efi_status MSABI flow_handle(efi_handle handle,const struct efi_guid *guid,void **out) {
    if (handle==(void *)1 && !memcmp(guid,&loaded_image_guid,sizeof(*guid))) {
        *out=&flow_image;return EFI_SUCCESS;
    }
    if (handle==(void *)2 && !memcmp(guid,&sfs_guid,sizeof(*guid))) {
        *out=&flow_sfs;return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}
static efi_status MSABI flow_locate(const struct efi_guid *guid,void *registration,void **out) {
    (void)guid;(void)registration;(void)out;return EFI_NOT_FOUND;
}
static efi_status MSABI flow_watchdog(uptr timeout,u64 code,uptr size,const char16 *text) {
    (void)timeout;(void)code;(void)size;(void)text;return EFI_SUCCESS;
}
static efi_status MSABI flow_map(uptr *size,struct efi_memory_descriptor *map,
        uptr *key,uptr *stride,u32 *version) {
    ++flow_maps;
    if (flow_failure==FLOW_MAP_ERROR ||
        (flow_failure==FLOW_MAP_AFTER_EXIT_ERROR && flow_exits)) return EFI_DEVICE_ERROR;
    *stride=flow_failure==FLOW_BAD_STRIDE?8:sizeof(*map);*version=1;*key=flow_maps;
    if (!map) { *size=sizeof(*map);return EFI_BUFFER_TOO_SMALL; }
    if (flow_failure==FLOW_REPLACEMENT_ERROR ||
        (flow_failure==FLOW_MAP_AFTER_EXIT_GROW && flow_exits)) {
        *size+=16*sizeof(*map);return EFI_BUFFER_TOO_SMALL;
    }
    *map=(struct efi_memory_descriptor){.type=flow_failure==FLOW_RESERVED?EFI_RESERVED:
        flow_unowned?EFI_BOOT_SERVICES_DATA:EFI_LOADER_CODE,
        .physical_start=kernel_first,.number_of_pages=(kernel_limit-kernel_first)/4096};
    *size=flow_failure==FLOW_BAD_MAP?sizeof(*map)-1:sizeof(*map);
    return EFI_SUCCESS;
}
static efi_status MSABI flow_exit(efi_handle image,uptr key) {
    (void)image;(void)key;++flow_exits;
    return flow_failure==FLOW_EXIT_ERROR?EFI_DEVICE_ERROR:EFI_INVALID_PARAMETER;
}
static void check_failure_cleanup(u8 *elf,uptr length) {
    file_bytes=elf;file_length=length;
    flow_root=(struct efi_file){.open=flow_open,.close=mock_close};
    flow_file=(struct efi_file){.close=mock_close,.read=flow_read,
        .set_position=mock_seek,.get_position=mock_tell};
    flow_sfs.open_volume=flow_volume;
    struct efi_boot_services bs={.allocate_pool=flow_pool,.free_pool=flow_free_pool,
        .allocate_pages=flow_pages,.free_pages=flow_free_pages,.handle_protocol=flow_handle,
        .locate_protocol=flow_locate,.set_watchdog_timer=flow_watchdog,
        .get_memory_map=flow_map,.exit_boot_services=flow_exit};
    struct efi_system_table st={.boot_services=&bs};
    for (u32 failure=FLOW_MAP_ERROR;failure<=FLOW_MAP_AFTER_EXIT_GROW;++failure) {
        for (u32 relocate=0;relocate<2;++relocate) {
            for (u32 retry=0;retry<2;++retry) {
                flow_failure=(enum flow_failure)failure;flow_relocate=relocate;
                flow_unowned=failure==FLOW_RESERVED || (failure==FLOW_EXIT_ERROR && relocate);
                flow_pools=flow_maps=flow_exits=flow_target_frees=0;
                efi_status result=efi_main((void *)1,&st);
                efi_status expected=failure==FLOW_RESERVED?EFI_LOAD_ERROR:
                    failure==FLOW_MAP_AFTER_EXIT_GROW?EFI_BUFFER_TOO_SMALL:EFI_DEVICE_ERROR;
                assert(result==expected && !flow_live && !kernel_file_pages && !kernel_pages_owned);
                assert(!pending_kernel && !pending_entry);
                assert(flow_target_frees==(flow_unowned?0u:1u));
                if (failure==FLOW_EXIT_EXHAUSTED) assert(flow_exits>1);
            }
        }
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    struct efi_simple_text_output text_output = {.output_string = mock_output};
    early_console = &text_output;
    console_text("loader failure\n");
    assert(print_length == 16 && printed[14] == '\r' && printed[15] == '\n');
    early_console = NULL;
    struct efi_boot_services bs = {.allocate_pool = mock_pool, .free_pool = mock_free,
                                   .allocate_pages = mock_pages, .free_pages = mock_free_pages};
    check_gop(&bs);
    check_acpi_tables();
    struct efi_sfs volume = {.open_volume = mock_volume};
    root_file = (struct efi_file){.open = mock_open, .close = mock_close};
    input_file = (struct efi_file){.close = mock_close, .read = mock_read,
                                  .set_position = mock_seek, .get_position = mock_tell};
    u8 payload[] = "Nuvora file reading must rewind from EOF and accept partial reads";
    file_bytes = payload; file_length = sizeof(payload);
    u8 *out = NULL; uptr size = 0;
    assert(read_whole_file(&bs, &volume, L"test", &out, &size) == EFI_SUCCESS);
    assert(size == sizeof(payload) && !memcmp(out, payload, size) && reads > 1 && closes == 2);
    free(out);

    struct efi_memory_descriptor map[2] = {
        {.type = EFI_CONVENTIONAL, .physical_start = 0x100000000ull, .number_of_pages = 3},
        {.type = EFI_BOOT_SERVICES_DATA, .physical_start = 0x100003000ull, .number_of_pages = 4}
    };
    assert(convert_memory_map(map, sizeof(map), sizeof(map[0])));
    assert(bi.mem_count == 1 && bi.mem[0].length == 7 * 4096);
    map[0].type = EFI_RUNTIME_SERVICES_DATA;
    assert(convert_memory_map(map, sizeof(map), sizeof(map[0])));
    assert(bi.mem_count == 2 && bi.mem[0].type == 0); /* retry replaced the old map */
    struct efi_memory_descriptor fragmented[768] = {0};
    for (u32 i = 0; i < ARRAY_LEN(fragmented); ++i)
        fragmented[i] = (struct efi_memory_descriptor){
            .type = i & 1 ? EFI_RUNTIME_SERVICES_DATA : EFI_CONVENTIONAL,
            .physical_start = 0x20000000ull + (u64)i * 4096,
            .number_of_pages = 1};
    assert(convert_memory_map(fragmented, sizeof(fragmented), sizeof(fragmented[0])));
    assert(bi.mem_count == ARRAY_LEN(fragmented) && bi.mem[767].type == 0);
    for (u32 i = 0; i < ARRAY_LEN(fragmented); ++i)
        fragmented[i].type = EFI_RESERVED;
    assert(convert_memory_map(fragmented, sizeof(fragmented), sizeof(fragmented[0])));
    assert(bi.mem_count == 1 && bi.mem[0].type == 0 &&
           bi.mem[0].length == ARRAY_LEN(fragmented) * 4096);
    assert(!convert_memory_map(map, sizeof(map), 8));
    map[0].physical_start = ~0ull - 4095;
    assert(!convert_memory_map(map, sizeof(map), sizeof(map[0])));

    FILE *f = fopen(argv[1], "rb"); assert(f);
    assert(!fseek(f, 0, SEEK_END)); long length = ftell(f); assert(length > 0);
    rewind(f); u8 *elf = malloc((usize)length); assert(elf);
    assert(fread(elf, 1, (usize)length, f) == (usize)length); fclose(f);
    struct elf64_ehdr *eh = (void *)elf;
    u64 entry = 0, saved = eh->phoff;
    assert(!load_kernel(&bs, &elf, ~(uptr)0, 0x2000000, 0x10000, &entry) && !allocations);
    u8 *wrapped = (void *)(uptr)(~(uptr)0 - 31);
    assert(!load_kernel(&bs, &wrapped, 64, 0x2000000, 0x10000, &entry) && !allocations);
    eh->phoff = ~0ull - 7;
    assert(!load_kernel(&bs, &elf, length, 0x2000000, 0x10000, &entry) && !allocations);
    eh->phoff = saved;
    saved = eh->shoff; eh->shoff = ~0ull - 7;
    assert(!load_kernel(&bs, &elf, length, 0x2000000, 0x10000, &entry) && !allocations);
    eh->shoff = saved;
    struct elf64_phdr *ph = (void *)(elf + eh->phoff);
    saved = ph[eh->phnum - 1].offset; ph[eh->phnum - 1].offset = ~0ull;
    assert(!load_kernel(&bs, &elf, length, 0x2000000, 0x10000, &entry) && !allocations);
    ph[eh->phnum - 1].offset = saved;
    deny_pages = true;
    assert(load_kernel(&bs, &elf, length, 0x2000000, 0x10000, &entry) && allocations == 1);
    assert(kernel_first == 0x01000000ull && kernel_limit < 0x02000000ull);
    assert(!kernel_pages_owned && !kernel_destination_ready());
    bi.mem_count = 2;
    bi.mem[0] = (struct boot_mem_entry){0x100000, 0xf00000, 0};
    bi.mem[1] = (struct boot_mem_entry){kernel_first, kernel_limit - kernel_first, 1};
    assert(kernel_destination_ready());
    bi.mem[bi.mem_count++] = (struct boot_mem_entry){kernel_first + 4096, 4096, 0};
    assert(!kernel_destination_ready());
    deny_pages = false;
    assert(load_kernel(&bs, &elf, length, 0x2000000, 0x10000, &entry));
    copy_kernel_segments(elf);
    for (u16 i = 0; i < eh->phnum; ++i) if (ph[i].type == 1) {
        assert(!memcmp((void *)(uptr)ph[i].paddr, elf + ph[i].offset, ph[i].filesz));
        for (u64 j = ph[i].filesz; j < ph[i].memsz; ++j)
            assert(!((u8 *)(uptr)ph[i].paddr)[j]);
    }
    assert(!munmap((void *)(uptr)kernel_first, mapped_length));
    /* Model the real 64 MiB firmware: AllocatePool returned staging pages
     * inside the 16..30 MiB kernel destination. Validate before relocating. */
    overlap_length = ALIGN_UP((uptr)length, 4096);
    overlap_pool = mmap((void *)0x01b00000, overlap_length, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(overlap_pool == (void *)0x01b00000);
    memcpy(overlap_pool, elf, (usize)length);
    u8 *staged = overlap_pool;
    struct elf64_ehdr *staged_eh = (void *)staged;
    struct elf64_phdr *staged_ph = (void *)(staged + staged_eh->phoff);
    saved = staged_ph[staged_eh->phnum - 1].offset;
    staged_ph[staged_eh->phnum - 1].offset = ~0ull;
    uptr previous = allocations;
    assert(!load_kernel(&bs, &staged, length, 0x2000000, 0x10000, &entry));
    assert(staged == overlap_pool && !staging_allocations && allocations == previous);
    staged_ph[staged_eh->phnum - 1].offset = saved;
    deny_staging = true;
    assert(!load_kernel(&bs, &staged, length, 0x2000000, 0x10000, &entry));
    assert(staged == overlap_pool && staging_allocations == 1 && allocations == previous);
    assert(kernel_load_status == EFI_DEVICE_ERROR && !memcmp(staged, elf, (usize)length));
    deny_staging = false;
    bad_staging = true;
    assert(!load_kernel(&bs, &staged, length, 0x2000000, 0x10000, &entry));
    assert(staged == overlap_pool && staging_allocations == 2 && allocations == previous);
    assert(staging_frees == 1 && kernel_load_status == EFI_DEVICE_ERROR);
    bad_staging = false;
    target_status = EFI_DEVICE_ERROR;
    assert(!load_kernel(&bs, &staged, length, 0x2000000, 0x10000, &entry));
    assert(!overlap_pool && staging_allocations == 3 && kernel_file_pages);
    free_kernel_file(&bs, staged);
    assert(staging_frees == 2 && !kernel_file_pages);
    target_status = EFI_SUCCESS;
    overlap_pool = mmap((void *)0x01b00000, overlap_length, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(overlap_pool == (void *)0x01b00000);
    memcpy(overlap_pool, elf, (usize)length);
    staged = overlap_pool;
    assert(load_kernel(&bs, &staged, length, 0x2000000, 0x10000, &entry));
    assert(!overlap_pool && staging_allocations == 4 && kernel_file_pages);
    assert((u64)(uptr)staged + (u64)kernel_file_pages * 4096 <= kernel_first);
    assert(!memcmp(staged, elf, (usize)length));
    copy_kernel_segments(staged);
    for (u16 i = 0; i < eh->phnum; ++i) if (ph[i].type == 1) {
        assert(!memcmp((void *)(uptr)ph[i].paddr, elf + ph[i].offset, ph[i].filesz));
        for (u64 j = ph[i].filesz; j < ph[i].memsz; ++j)
            assert(!((u8 *)(uptr)ph[i].paddr)[j]);
    }
    free_kernel_file(&bs, staged);
    assert(staging_frees == 3 && !kernel_file_pages);
    assert(!munmap((void *)(uptr)kernel_first, mapped_length));
    check_failure_cleanup(elf,(uptr)length);
    free(elf);
    puts("PASS UEFI: ACPI GUID/preference/high address, partial reads/rewind, memory-map replacement, malformed ELF, page ownership, overlapping ELF relocation/OOM, failed handover cleanup/retry, segment copy/BSS");
    return 0;
}
