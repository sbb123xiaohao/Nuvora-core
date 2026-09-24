#include <nv/net.h>
u16 nv_net_get16(const u8 *p) { return (u16)((u16)p[0] << 8 | p[1]); }
u32 nv_net_get32(const u8 *p) { return (u32)nv_net_get16(p) << 16 | nv_net_get16(p + 2); }
void nv_net_put16(u8 *p, u16 n) { p[0] = (u8)(n >> 8); p[1] = (u8)n; }
void nv_net_put32(u8 *p, u32 n) { nv_net_put16(p, n >> 16); nv_net_put16(p + 2, (u16)n); }
static u32 sum(const u8 *p, u32 len) {
    u32 s = 0;
    while (len > 1) { s += nv_net_get16(p); p += 2; len -= 2; }
    if (len) s += (u32)*p << 8;
    return s;
}
static u16 fold(u32 s) {
    while (s >> 16) s = (s & 65535u) + (s >> 16);
    return (u16)~s;
}
u16 nv_net_checksum(const void *p, u32 length) { return fold(sum(p, length)); }
u32 nv_ipv4_validate(const u8 *ip, u32 length) {
    if (length < 20 || ip[0] != 0x45) return 0;
    u32 size = nv_net_get16(ip + 2);
    if (size < 20 || size > length || size > NV_NET_MTU ||
        (nv_net_get16(ip + 6) & ~0x4000u) || !ip[8] || nv_net_checksum(ip, 20)) return 0;
    return size;
}
bool nv_udp_validate(const u8 *ip, u32 length) {
    u32 size = nv_ipv4_validate(ip, length);
    if (size < 28 || ip[9] != 17 || nv_net_get16(ip + 24) != size - 20) return false;
    if (!nv_net_get16(ip + 26)) return true;
    return !fold(sum(ip + 12, 8) + 17 + (size - 20) + sum(ip + 20, size - 20));
}
