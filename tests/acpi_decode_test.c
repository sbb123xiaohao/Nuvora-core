#include <nv/acpi.h>
#include <nv/string.h>
extern int printf(const char *, ...);

#define IMAGE_SIZE (2u * 1024u * 1024u)
#define RSDP_ADDRESS 0xe0000u
#define XSDT_ADDRESS 0x10000u
#define RSDT_ADDRESS 0x11000u
#define MCFG_ADDRESS 0x12000u

struct header {
    char signature[4];
    u32 length;
    u8 revision, checksum;
    char oem_id[6], oem_table_id[8];
    u32 oem_revision, creator_id, creator_revision;
} PACKED;
struct rsdp {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt, length;
    u64 xsdt;
    u8 extended_checksum, reserved[3];
} PACKED;
struct allocation {
    u64 address;
    u16 segment;
    u8 start_bus, end_bus;
    u32 reserved;
} PACKED;

static u8 image[IMAGE_SIZE];
static u32 checks, failures;

static void expect(bool value, const char *name) {
    ++checks;
    if (!value) {
        ++failures;
        printf("FAIL %s\n", name);
    }
}

static bool read_image(void *context, u64 address, void *out, u32 length) {
    (void)context;
    if (address > IMAGE_SIZE || length > IMAGE_SIZE - address)
        return false;
    memcpy(out, image + (u32)address, length);
    return true;
}

static void fix_checksum(u32 address, u32 length, u32 field) {
    image[address + field] = 0;
    u8 sum = 0;
    for (u32 i = 0; i < length; ++i)
        sum = (u8)(sum + image[address + i]);
    image[address + field] = (u8)(0u - sum);
}

static struct header *table(u32 address, const char *signature, u32 length) {
    struct header *h = (void *)(image + address);
    memset(h, 0, length);
    memcpy(h->signature, signature, 4);
    h->length = length;
    h->revision = 1;
    memcpy(h->oem_id, "NVTEST", 6);
    memcpy(h->oem_table_id, "NUVORA  ", 8);
    h->oem_revision = 1;
    return h;
}

static void finish_table(u32 address) {
    struct header *h = (void *)(image + address);
    fix_checksum(address, h->length, 9);
}

static void make_mcfg(const struct allocation *items, u32 count) {
    u32 length = 44 + count * sizeof(*items);
    table(MCFG_ADDRESS, "MCFG", length);
    memcpy(image + MCFG_ADDRESS + 44, items, count * sizeof(*items));
    finish_table(MCFG_ADDRESS);
}

static void make_root(u32 address, bool xsdt) {
    u32 width = xsdt ? 8 : 4;
    table(address, xsdt ? "XSDT" : "RSDT", sizeof(struct header) + width);
    if (xsdt) {
        u64 entry = MCFG_ADDRESS;
        memcpy(image + address + sizeof(struct header), &entry, sizeof(entry));
    } else {
        u32 entry = MCFG_ADDRESS;
        memcpy(image + address + sizeof(struct header), &entry, sizeof(entry));
    }
    finish_table(address);
}

static void make_rsdp(u32 address) {
    struct rsdp *r = (void *)(image + address);
    memset(r, 0, sizeof(*r));
    memcpy(r->signature, "RSD PTR ", 8);
    memcpy(r->oem_id, "NVTEST", 6);
    r->revision = 2;
    r->rsdt = RSDT_ADDRESS;
    r->length = sizeof(*r);
    r->xsdt = XSDT_ADDRESS;
    fix_checksum(address, 20, 8);
    fix_checksum(address, sizeof(*r), 32);
}

static void fixture(void) {
    memset(image, 0, sizeof(image));
    const struct allocation item = {0xb0000000ull, 0, 0, 255, 0};
    make_mcfg(&item, 1);
    make_root(XSDT_ADDRESS, true);
    make_root(RSDT_ADDRESS, false);
    make_rsdp(RSDP_ADDRESS);
}

int main(void) {
    struct nv_acpi_result result;
    fixture();
    expect(nv_acpi_discover(read_image, NULL, &result), "valid ACPI 2.0 hierarchy discovered");
    expect(result.root_kind == NV_ACPI_ROOT_XSDT && result.revision == 2 &&
               !strcmp(result.oem_id, "NVTEST") && !strcmp(result.oem_table_id, "NUVORA"),
           "XSDT and fixed-width OEM identifiers decoded");
    expect(result.mcfg_entries == 1 && result.region_count == 1 && !result.rejected_entries &&
               result.regions[0].address == 0xb0000000ull && result.regions[0].start_bus == 0 &&
               result.regions[0].end_bus == 255,
           "MCFG allocation decoded with full bus range");

    fixture();
    image[XSDT_ADDRESS + 9]++;
    expect(nv_acpi_discover(read_image, NULL, &result) &&
               result.root_kind == NV_ACPI_ROOT_RSDT && result.region_count == 1,
           "bad XSDT checksum falls back to valid RSDT");

    fixture();
    image[RSDP_ADDRESS + 32]++;
    expect(!nv_acpi_discover(read_image, NULL, &result), "bad extended RSDP checksum rejected");

    fixture();
    image[MCFG_ADDRESS + 9]++;
    expect(nv_acpi_discover(read_image, NULL, &result) && !result.region_count &&
               result.rejected_entries == 1,
           "bad MCFG checksum isolated without losing ACPI root");

    fixture();
    const struct allocation mixed[] = {
        {0xb0000000ull, 0, 0, 63, 0},
        {0xc0000000ull, 0, 32, 64, 0},
        {0xb0010000ull, 2, 0, 10, 0},
        {0xd0000000ull, 1, 0, 31, 0},
    };
    make_mcfg(mixed, ARRAY_LEN(mixed));
    expect(nv_acpi_discover(read_image, NULL, &result) && result.mcfg_entries == 4 &&
               result.region_count == 2 && result.rejected_entries == 2,
           "overlap and unaligned ECAM entries rejected independently");

    fixture();
    struct allocation many[9];
    for (u32 i = 0; i < ARRAY_LEN(many); ++i)
        many[i] = (struct allocation){0x10000000ull + (u64)i * 0x1000000ull, (u16)i, 0, 0, 0};
    make_mcfg(many, ARRAY_LEN(many));
    expect(nv_acpi_discover(read_image, NULL, &result) && result.region_count == 8 &&
               result.rejected_entries == 1,
           "MCFG region capacity is bounded");

    fixture();
    struct header *mcfg = (void *)(image + MCFG_ADDRESS);
    mcfg->length = 45;
    finish_table(MCFG_ADDRESS);
    expect(nv_acpi_discover(read_image, NULL, &result) && !result.region_count &&
               result.rejected_entries == 1,
           "partial MCFG allocation record rejected");

    fixture();
    memcpy(image + 0x80000, image + RSDP_ADDRESS, sizeof(struct rsdp));
    u16 ebda = 0x8000;
    memcpy(image + 0x40e, &ebda, sizeof(ebda));
    image[RSDP_ADDRESS + 8]++;
    expect(nv_acpi_discover(read_image, NULL, &result) && result.rsdp_address == 0x80000,
           "EBDA RSDP is searched before the high BIOS window");

    fixture();
    struct rsdp *r = (void *)(image + RSDP_ADDRESS);
    r->xsdt = IMAGE_SIZE - 8;
    fix_checksum(RSDP_ADDRESS, 20, 8);
    fix_checksum(RSDP_ADDRESS, sizeof(*r), 32);
    expect(nv_acpi_discover(read_image, NULL, &result) && result.root_kind == NV_ACPI_ROOT_RSDT,
           "unreadable XSDT address safely falls back");

    printf("ACPI DECODER: %u checks, %u failures (synthetic firmware tables)\n", checks,
           failures);
    return failures ? 1 : 0;
}
