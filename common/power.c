#include <nv/power.h>
#include <nv/string.h>
#define TABLE_MAX (1024u * 1024u)
static bool power_table(nv_physical_read read, void *ctx, u64 address,
                        const char *signature, u32 *length) {
    u8 h[36], sum=0, bytes[128];
    if (!address || !read(ctx,address,h,sizeof(h)) || memcmp(h,signature,4)) return false;
    memcpy(length,h+4,4);
    if (*length<36 || *length>TABLE_MAX || address>~0ull-*length) return false;
    for (u32 off=0; off<*length;) {
        u32 n=MIN((u32)sizeof(bytes),*length-off);
        if (!read(ctx,address+off,bytes,n)) return false;
        for (u32 i=0;i<n;++i) sum=(u8)(sum+bytes[i]);
        off+=n;
    }
    return !sum;
}
static bool gas_valid(const struct nv_acpi_gas *r) {
    return r->address && r->space<=1 && !r->offset && r->access<=4 &&
        (r->width==8 || r->width==16 || r->width==32) &&
        (!r->access || (8u<<(r->access-1))==r->width) &&
        !(r->address % (r->width/8)) &&
        (r->space!=1 || r->address<=65536u-r->width/8);
}
static bool aml_integer(const u8 *p,u32 n,u32 *used,u32 *value) {
    if (!n) return false;
    if (p[0]<=1) { *used=1; *value=p[0]; return true; }
    u32 bytes=p[0]==0x0a?1:p[0]==0x0b?2:p[0]==0x0c?4:0;
    if (!bytes || n<bytes+1) return false;
    *value=0; memcpy(value,p+1,bytes); *used=bytes+1; return true;
}
/* Recognize only a static Name(_S5_, Package(literal,literal,...)). Dynamic
 * AML methods require an interpreter and are deliberately not executed here. */
static bool static_s5(nv_physical_read read,void *ctx,u64 dsdt,u8 *a,u8 *b) {
    u32 length;
    if (!power_table(read,ctx,dsdt,"DSDT",&length)) return false;
    for (u32 off=36;off+10<=length;++off) {
        u8 bytes[48]; u32 n=MIN((u32)sizeof(bytes),length-off);
        if (!read(ctx,dsdt+off,bytes,n)) return false;
        if (bytes[0]!=0x08) continue;
        u32 p=1;
        if (bytes[p]=='\\') ++p;
        if (p+6>=n || memcmp(bytes+p,"_S5_",4) || bytes[p+4]!=0x12) continue;
        p+=5;
        u32 extra=bytes[p]>>6, encoded=extra+1;
        if (p+encoded>=n) continue;
        u32 size=bytes[p] & (extra?15u:63u);
        for (u32 i=1;i<=extra;++i) size|=(u32)bytes[p+i]<<(4+8*(i-1));
        if (size<encoded+3 || size>length-off-p) continue;
        u32 end=MIN(n,p+size); p+=encoded;
        if (p>=end || bytes[p++]<2) continue;
        u32 used,x,y;
        if (!aml_integer(bytes+p,end-p,&used,&x)) continue;
        p+=used;
        if (!aml_integer(bytes+p,end-p,&used,&y) || x>7 || y>7) continue;
        *a=(u8)x; *b=(u8)y; return true;
    }
    return false;
}
bool nv_acpi_power_parse(nv_physical_read read,void *ctx,u64 address,struct nv_acpi_power *out) {
    if (!read || !out) return false;
    memset(out,0,sizeof(*out)); u32 length;
    if (!power_table(read,ctx,address,"FACP",&length) || length<116) return false;
    u8 f[196]={0};
    if (!read(ctx,address,f,MIN(length,(u32)sizeof(f)))) return false;
    u32 flags; memcpy(&flags,f+112,4);
    if (length>=129 && (flags&(1u<<10))) {
        memcpy(&out->reset,f+116,12); out->reset_value=f[128];
        out->can_reset=gas_valid(&out->reset) && out->reset.width==8;
    }
    if (flags&(1u<<20)) return true; /* Hardware-reduced ACPI has different registers. */
    u32 legacy_a,legacy_b,dsdt32;
    memcpy(&legacy_a,f+64,4); memcpy(&legacy_b,f+68,4); memcpy(&dsdt32,f+40,4);
    out->pm1a=(struct nv_acpi_gas){1,(u8)(f[89]*8),0,0,legacy_a};
    out->pm1b=(struct nv_acpi_gas){1,(u8)(f[89]*8),0,0,legacy_b};
    if (length>=184) { struct nv_acpi_gas x; memcpy(&x,f+172,12); if (x.address) out->pm1a=x; }
    if (length>=196) { struct nv_acpi_gas x; memcpy(&x,f+184,12); if (x.address) out->pm1b=x; }
    memcpy(&out->smi_command,f+48,4); out->acpi_enable=f[52];
    u64 dsdt=dsdt32;
    if (length>=148) { u64 x; memcpy(&x,f+140,8); if (x) dsdt=x; }
    out->can_sleep=gas_valid(&out->pm1a) && out->pm1a.width>=16 &&
        (!out->pm1b.address || (gas_valid(&out->pm1b) && out->pm1b.width>=16)) &&
        static_s5(read,ctx,dsdt,&out->sleep_a,&out->sleep_b);
    return true;
}
