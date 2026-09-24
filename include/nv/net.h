#ifndef NV_NET_H
#define NV_NET_H
#include <nv/types.h>
#define NV_NET_MTU 1500u
#define NV_UDP_MAX 1400u
enum { NV_NET_INFO = 1, NV_NET_CONFIG, NV_NET_BIND, NV_NET_SEND, NV_NET_RECV };
struct nv_net_info {
    u32 ready, address, mask, gateway, rx_packets, tx_packets, dropped;
    u8 mac[6]; u16 reserved;
};
/* IPv4 integer: 0x0a00020f = 10.0.2.15; ports are host-endian. */
struct nv_net_config { u32 address, mask, gateway; };
struct nv_udp { u32 address, port, length; u8 data[NV_UDP_MAX]; };
u16 nv_net_checksum(const void *, u32);
u16 nv_net_get16(const u8 *);
u32 nv_net_get32(const u8 *);
void nv_net_put16(u8 *, u16);
void nv_net_put32(u8 *, u32);
u32 nv_ipv4_validate(const u8 *, u32);
bool nv_udp_validate(const u8 *, u32);
#endif
