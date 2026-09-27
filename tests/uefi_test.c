#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../arch/x86_64/uefi.c"

static uptr position, file_length, reads, closes, allocations, mapped_length;
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
static bool deny_pages;
static efi_status MSABI mock_pool(u32 type, uptr size, void **out) {
    (void)type;
    *out = malloc(size);
    return *out ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
static efi_status MSABI mock_free(void *p) { free(p); return EFI_SUCCESS; }
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
    assert(kind == EFI_ALLOCATE_ADDRESS && type == EFI_LOADER_CODE);
    ++allocations;
    if (deny_pages) return EFI_NOT_FOUND;
    mapped_length = pages * 4096;
    void *p = mmap((void *)(uptr)*address, mapped_length, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(p == (void *)(uptr)*address);
    return EFI_SUCCESS;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    struct efi_simple_text_output text_output = {.output_string = mock_output};
    early_console = &text_output;
    console_text("loader failure\n");
    assert(print_length == 16 && printed[14] == '\r' && printed[15] == '\n');
    early_console = NULL;
    struct efi_boot_services bs = {.allocate_pool = mock_pool, .free_pool = mock_free,
                                   .allocate_pages = mock_pages};
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
    eh->phoff = ~0ull - 7;
    assert(!load_kernel(&bs, elf, length, 0x2000000, 0x10000, &entry) && !allocations);
    eh->phoff = saved;
    saved = eh->shoff; eh->shoff = ~0ull - 7;
    assert(!load_kernel(&bs, elf, length, 0x2000000, 0x10000, &entry) && !allocations);
    eh->shoff = saved;
    struct elf64_phdr *ph = (void *)(elf + eh->phoff);
    saved = ph[eh->phnum - 1].offset; ph[eh->phnum - 1].offset = ~0ull;
    assert(!load_kernel(&bs, elf, length, 0x2000000, 0x10000, &entry) && !allocations);
    ph[eh->phnum - 1].offset = saved;
    deny_pages = true;
    assert(load_kernel(&bs, elf, length, 0x2000000, 0x10000, &entry) && allocations == 1);
    assert(kernel_first == 0x01000000ull && kernel_limit < 0x02000000ull);
    assert(!kernel_pages_owned && !kernel_destination_ready());
    bi.mem_count = 2;
    bi.mem[0] = (struct boot_mem_entry){0x100000, 0xf00000, 0};
    bi.mem[1] = (struct boot_mem_entry){kernel_first, kernel_limit - kernel_first, 1};
    assert(kernel_destination_ready());
    bi.mem[bi.mem_count++] = (struct boot_mem_entry){kernel_first + 4096, 4096, 0};
    assert(!kernel_destination_ready());
    deny_pages = false;
    assert(load_kernel(&bs, elf, length, 0x2000000, 0x10000, &entry));
    copy_kernel_segments(elf);
    for (u16 i = 0; i < eh->phnum; ++i) if (ph[i].type == 1) {
        assert(!memcmp((void *)(uptr)ph[i].paddr, elf + ph[i].offset, ph[i].filesz));
        for (u64 j = ph[i].filesz; j < ph[i].memsz; ++j)
            assert(!((u8 *)(uptr)ph[i].paddr)[j]);
    }
    assert(!munmap((void *)(uptr)kernel_first, mapped_length));
    free(elf);
    puts("PASS UEFI: partial reads/rewind, memory-map replacement, malformed ELF, page ownership, segment copy/BSS");
    return 0;
}
