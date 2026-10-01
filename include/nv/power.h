#ifndef NV_POWER_H
#define NV_POWER_H
#include <nv/acpi.h>
struct nv_acpi_gas { u8 space, width, offset, access; u64 address; } PACKED;
struct nv_acpi_power {
    struct nv_acpi_gas reset, pm1a, pm1b;
    u32 smi_command;
    u8 reset_value, acpi_enable, sleep_a, sleep_b;
    bool can_reset, can_sleep;
};
bool nv_acpi_power_parse(nv_physical_read, void *, u64, struct nv_acpi_power *);
#endif
