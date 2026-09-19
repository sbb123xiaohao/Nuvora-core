#include <nv/acpi.h>
#include <nv/string.h>

#define ACPI_HEADER_SIZE 36u
#define ACPI_TABLE_MAX (1024u * 1024u)
#define RSDP_MAX 4096u

struct rsdp {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt;
    u32 length;
    u64 xsdt;
    u8 extended_checksum;
    u8 reserved[3];
} PACKED;

struct acpi_header {
    char signature[4];
    u32 length;
    u8 revision, checksum;
    char oem_id[6], oem_table_id[8];
    u32 oem_revision, creator_id, creator_revision;
} PACKED;

struct mcfg_allocation {
    u64 address;
    u16 segment;
    u8 start_bus, end_bus;
    u32 reserved;
} PACKED;

_Static_assert(sizeof(struct rsdp) == 36, "ACPI RSDP layout");
_Static_assert(sizeof(struct acpi_header) == ACPI_HEADER_SIZE, "ACPI table header layout");
_Static_assert(sizeof(struct mcfg_allocation) == 16, "ACPI MCFG allocation layout");

static bool checksum(nv_physical_read read, void *context, u64 address, u32 length) {
    u8 bytes[128], sum = 0;
    while (length) {
        u32 part = MIN(length, (u32)sizeof(bytes));
        if (!read(context, address, bytes, part))
            return false;
        for (u32 i = 0; i < part; ++i)
            sum = (u8)(sum + bytes[i]);
        address += part;
        length -= part;
    }
    return sum == 0;
}

static bool header(nv_physical_read read, void *context, u64 address, const char *signature,
                   struct acpi_header *out) {
    if (!address || !read(context, address, out, sizeof(*out)) ||
        memcmp(out->signature, signature, 4) || out->length < sizeof(*out) ||
        out->length > ACPI_TABLE_MAX)
        return false;
    return checksum(read, context, address, out->length);
}

static void fixed_string(char *out, u32 cap, const char *in, u32 length) {
    u32 n = MIN(length, cap - 1);
    memcpy(out, in, n);
    out[n] = 0;
    while (n && out[n - 1] == ' ')
        out[--n] = 0;
}

static bool overlaps(const struct nv_mcfg_region *a, const struct nv_mcfg_region *b) {
    return a->segment == b->segment && a->start_bus <= b->end_bus &&
           b->start_bus <= a->end_bus;
}

static void add_region(struct nv_acpi_result *out, const struct mcfg_allocation *item) {
    ++out->mcfg_entries;
    u64 buses = (u64)item->end_bus + 1;
    u64 bytes = buses << 20;
    if (!item->address || (item->address & 0xfffffull) || item->start_bus > item->end_bus ||
        item->address >= (1ull << 52) || bytes > (1ull << 52) - item->address) {
        ++out->rejected_entries;
        return;
    }
    struct nv_mcfg_region region = {item->address, item->segment, item->start_bus, item->end_bus};
    for (u32 i = 0; i < out->region_count; ++i)
        if (overlaps(&region, &out->regions[i])) {
            ++out->rejected_entries;
            return;
        }
    if (out->region_count == NV_ACPI_MCFG_MAX) {
        ++out->rejected_entries;
        return;
    }
    out->regions[out->region_count++] = region;
}

static void parse_mcfg(nv_physical_read read, void *context, u64 address,
                       struct nv_acpi_result *out) {
    struct acpi_header h;
    if (!header(read, context, address, "MCFG", &h) || h.length < 44 ||
        (h.length - 44) % sizeof(struct mcfg_allocation)) {
        ++out->rejected_entries;
        return;
    }
    fixed_string(out->oem_table_id, sizeof(out->oem_table_id), h.oem_table_id,
                 sizeof(h.oem_table_id));
    u32 count = (h.length - 44) / sizeof(struct mcfg_allocation);
    for (u32 i = 0; i < count; ++i) {
        struct mcfg_allocation item;
        if (!read(context, address + 44 + (u64)i * sizeof(item), &item, sizeof(item))) {
            out->rejected_entries += count - i;
            return;
        }
        add_region(out, &item);
    }
}

static bool parse_root(nv_physical_read read, void *context, u64 address, bool xsdt,
                       struct nv_acpi_result *out) {
    struct acpi_header root;
    const char *signature = xsdt ? "XSDT" : "RSDT";
    u32 width = xsdt ? 8 : 4;
    if (!header(read, context, address, signature, &root) ||
        (root.length - sizeof(root)) % width)
        return false;
    out->root_kind = xsdt ? NV_ACPI_ROOT_XSDT : NV_ACPI_ROOT_RSDT;
    fixed_string(out->oem_table_id, sizeof(out->oem_table_id), root.oem_table_id,
                 sizeof(root.oem_table_id));
    u32 count = (root.length - sizeof(root)) / width;
    for (u32 i = 0; i < count; ++i) {
        u64 table = 0;
        if (!read(context, address + sizeof(root) + (u64)i * width, &table, width))
            return false;
        struct acpi_header candidate;
        if (table && read(context, table, &candidate, sizeof(candidate)) &&
            !memcmp(candidate.signature, "MCFG", 4))
            parse_mcfg(read, context, table, out);
    }
    return true;
}

static bool parse_rsdp(nv_physical_read read, void *context, u64 address,
                       struct nv_acpi_result *out) {
    struct rsdp r;
    if (!read(context, address, &r, 20) || memcmp(r.signature, "RSD PTR ", 8) ||
        !checksum(read, context, address, 20))
        return false;
    if (r.revision >= 2) {
        if (!read(context, address, &r, sizeof(r)) || r.length < sizeof(r) ||
            r.length > RSDP_MAX || !checksum(read, context, address, r.length))
            return false;
    } else {
        r.length = 20;
        r.xsdt = 0;
    }
    memset(out, 0, sizeof(*out));
    out->rsdp_address = address;
    out->revision = r.revision;
    fixed_string(out->oem_id, sizeof(out->oem_id), r.oem_id, sizeof(r.oem_id));
    if (r.xsdt && parse_root(read, context, r.xsdt, true, out))
        return true;
    /* Firmware with a broken XSDT can still provide a valid ACPI 1.0 RSDT. */
    memset(out->regions, 0, sizeof(out->regions));
    out->root_kind = NV_ACPI_ROOT_NONE;
    out->mcfg_entries = out->rejected_entries = out->region_count = 0;
    return r.rsdt && parse_root(read, context, r.rsdt, false, out);
}

static bool scan(nv_physical_read read, void *context, u64 start, u32 length,
                 struct nv_acpi_result *out) {
    if (length < 20 || start > ~0ull - length)
        return false;
    u64 address = (start + 15) & ~15ull, last = start + length - 20;
    for (; address <= last; address += 16) {
        char signature[8];
        if (!read(context, address, signature, sizeof(signature)))
            return false;
        if (!memcmp(signature, "RSD PTR ", 8) && parse_rsdp(read, context, address, out))
            return true;
    }
    return false;
}

bool nv_acpi_parse_rsdp(nv_physical_read read, void *context, u64 address,
                        struct nv_acpi_result *out) {
    if (!read || !out || !address)
        return false;
    return parse_rsdp(read, context, address, out);
}

bool nv_acpi_discover(nv_physical_read read, void *context, struct nv_acpi_result *out) {
    if (!read || !out)
        return false;
    memset(out, 0, sizeof(*out));
    u16 ebda = 0;
    if (read(context, 0x40e, &ebda, sizeof(ebda))) {
        u32 address = (u32)ebda << 4;
        if (address >= 0x80000 && address <= 0x9fc00 && scan(read, context, address, 1024, out))
            return true;
    }
    return scan(read, context, 0xe0000, 0x20000, out);
}
