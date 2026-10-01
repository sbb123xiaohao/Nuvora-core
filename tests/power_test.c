#include <assert.h>
#include <stdio.h>
#include <nv/power.h>
#include <nv/string.h>
#include "../common/power.c"
static u8 fadt[196],dsdt[128];
static const u64 fadt_address=0x100003000ull,dsdt_address=0x200004000ull;
static bool read_table(void *ctx,u64 address,void *out,u32 length) {
    (void)ctx;
    const u8 *base=NULL; u64 offset=0; u32 size=0;
    if (address>=fadt_address && address-fadt_address<sizeof(fadt)) {
        base=fadt; offset=address-fadt_address; size=sizeof(fadt);
    } else if (address>=dsdt_address && address-dsdt_address<sizeof(dsdt)) {
        base=dsdt; offset=address-dsdt_address; size=sizeof(dsdt);
    }
    if (!base || length>size-offset) return false;
    memcpy(out,base+(usize)offset,length); return true;
}
static void sum(u8 *table,u32 size) {
    table[9]=0; u8 value=0;
    for (u32 i=0;i<size;++i) value=(u8)(value+table[i]);
    table[9]=(u8)(0u-value);
}
static void reset(void) {
    memset(fadt,0,sizeof(fadt)); memset(dsdt,0,sizeof(dsdt));
    memcpy(fadt,"FACP",4); u32 length=sizeof(fadt); memcpy(fadt+4,&length,4);
    u32 flags=1u<<10; memcpy(fadt+112,&flags,4);
    struct nv_acpi_gas reg={1,8,0,1,0xcf9}; memcpy(fadt+116,&reg,12); fadt[128]=6;
    reg=(struct nv_acpi_gas){0,16,0,2,0x300005000ull}; memcpy(fadt+172,&reg,12);
    memcpy(fadt+140,&dsdt_address,8);
    memcpy(dsdt,"DSDT",4); length=sizeof(dsdt); memcpy(dsdt+4,&length,4);
    const u8 package[]={0x08,'\\','_','S','5','_',0x12,6,2,0x0a,5,0x0a,6};
    memcpy(dsdt+36,package,sizeof(package)); sum(fadt,sizeof(fadt)); sum(dsdt,sizeof(dsdt));
}
int main(void) {
    struct nv_acpi_power p;
    reset(); assert(nv_acpi_power_parse(read_table,NULL,fadt_address,&p));
    assert(p.can_reset && p.can_sleep && p.reset_value==6 && p.sleep_a==5 && p.sleep_b==6);
    assert(p.pm1a.address==0x300005000ull);
    fadt[9]^=1; assert(!nv_acpi_power_parse(read_table,NULL,fadt_address,&p));
    reset(); dsdt[9]^=1;
    assert(nv_acpi_power_parse(read_table,NULL,fadt_address,&p) && p.can_reset && !p.can_sleep);
    reset(); dsdt[47]=8; sum(dsdt,sizeof(dsdt));
    assert(nv_acpi_power_parse(read_table,NULL,fadt_address,&p) && !p.can_sleep);
    reset(); fadt[116]=2; sum(fadt,sizeof(fadt));
    assert(nv_acpi_power_parse(read_table,NULL,fadt_address,&p) && !p.can_reset && p.can_sleep);
    reset(); u32 flags=(1u<<10)|(1u<<20); memcpy(fadt+112,&flags,4); sum(fadt,sizeof(fadt));
    assert(nv_acpi_power_parse(read_table,NULL,fadt_address,&p) && p.can_reset && !p.can_sleep);
    reset(); dsdt[43]=0xff; sum(dsdt,sizeof(dsdt));
    assert(nv_acpi_power_parse(read_table,NULL,fadt_address,&p) && !p.can_sleep);
    puts("PASS ACPI power: high FADT/DSDT/MMIO addresses, static S5, reset GAS, checksums and malformed AML");
}
