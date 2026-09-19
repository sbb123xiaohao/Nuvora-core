#ifndef NV_ACPI_H
#define NV_ACPI_H
#include <nv/types.h>

#define NV_ACPI_MCFG_MAX 8u
enum { NV_ACPI_ROOT_NONE, NV_ACPI_ROOT_RSDT, NV_ACPI_ROOT_XSDT };

struct nv_mcfg_region {
    u64 address;
    u16 segment;
    u8 start_bus, end_bus;
};

struct nv_acpi_result {
    u64 rsdp_address;
    u32 revision, root_kind, mcfg_entries, rejected_entries, region_count;
    char oem_id[8], oem_table_id[9];
    struct nv_mcfg_region regions[NV_ACPI_MCFG_MAX];
};

typedef bool (*nv_physical_read)(void *context, u64 address, void *out, u32 length);

/* Scan the PC firmware locations and parse only the tables needed for early
 * platform discovery. Every signature, length and checksum is validated. */
bool nv_acpi_discover(nv_physical_read read, void *context, struct nv_acpi_result *out);

/* Parse a known RSDP address, e.g. handed over by UEFI via its configuration
 * table. Same validation as the discovery scan. */
bool nv_acpi_parse_rsdp(nv_physical_read read, void *context, u64 address,
                        struct nv_acpi_result *out);

#endif
