#ifndef NV_ACPI_H
#define NV_ACPI_H
#include <nv/types.h>

#define NV_ACPI_MCFG_MAX 8u
#define NV_CPU_MAX 32u
struct nv_madt_cpu { u32 apic_id, uid; };
struct nv_madt_ioapic { u32 address, gsi; u8 id; };
struct nv_madt_iso { u32 gsi; u16 flags; u8 irq; };
struct nv_madt {
    u64 lapic;
    u32 valid, flags, cpu_count, omitted, io_count, iso_count;
    struct nv_madt_cpu cpus[NV_CPU_MAX];
    struct nv_madt_ioapic io[8];
    struct nv_madt_iso iso[16];
};
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
    struct nv_madt madt;
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
