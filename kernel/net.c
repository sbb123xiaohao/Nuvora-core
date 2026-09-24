#include "kernel.h"
#include <nv/net.h>
static struct nv_net_info info;
struct neighbor { u32 ip, seen; u8 mac[6]; };
static struct neighbor neighbors[8];
static u32 next_neighbor, arp_pending, arp_tick;
struct endpoint { u32 pid, port, head, count; struct nv_udp *packets[4]; };
static struct endpoint endpoints[NV_TASK_MAX];
static bool unicast(u32 ip) { return (ip >> 24) && (ip >> 24) != 127 && (ip >> 24) < 224; }
static void ethernet(u8 *p, const u8 *destination, u16 type) {
    memcpy(p, destination, 6); memcpy(p + 6, info.mac, 6); nv_net_put16(p + 12, type);
}
static int transmit(const u8 *p, u32 length) {
    int r = virtio_net_send(p, length);
    if (!r) ++info.tx_packets;
    return r;
}
static void arp_request(u32 target) {
    if (arp_pending == target && (u32)(ticks - arp_tick) < 100) return;
    u8 p[60] = {0}; const u8 broadcast[6] = {255,255,255,255,255,255};
    ethernet(p, broadcast, 0x806);
    nv_net_put16(p+14,1); nv_net_put16(p+16,0x800); p[18]=6; p[19]=4;
    nv_net_put16(p+20,1); memcpy(p+22,info.mac,6);
    nv_net_put32(p+28,info.address); nv_net_put32(p+38,target);
    if (!transmit(p,sizeof(p))) { arp_pending=target; arp_tick=ticks; }
}
static void receive_arp(const u8 *p, u32 length) {
    if (length < 42 || nv_net_get16(p+14)!=1 || nv_net_get16(p+16)!=0x800 ||
        p[18]!=6 || p[19]!=4 || memcmp(p+6,p+22,6) || (p[22]&1) ||
        nv_net_get32(p+38)!=info.address) { ++info.dropped; return; }
    u16 op=nv_net_get16(p+20); u32 source=nv_net_get32(p+28);
    if ((op!=1 && op!=2) || !unicast(source) || source==info.address ||
        ((source ^ info.address) & info.mask) ||
        (op==2 && (memcmp(p+32,info.mac,6) || source!=arp_pending))) return;
    u32 slot=ARRAY_LEN(neighbors);
    for (u32 i=0;i<ARRAY_LEN(neighbors);++i) if (neighbors[i].ip==source) {slot=i;break;}
    if (slot==ARRAY_LEN(neighbors)) slot=next_neighbor++ % ARRAY_LEN(neighbors);
    neighbors[slot].ip=source; neighbors[slot].seen=ticks; memcpy(neighbors[slot].mac,p+22,6);
    if (source==arp_pending) arp_pending=0;
    if (op==1) {
        u8 reply[60]={0}; ethernet(reply,p+6,0x806);
        memcpy(reply+14,p+14,28); nv_net_put16(reply+20,2);
        memcpy(reply+22,info.mac,6); nv_net_put32(reply+28,info.address);
        memcpy(reply+32,p+22,6); nv_net_put32(reply+38,source);
        transmit(reply,sizeof(reply));
    }
}
static void receive(const u8 *p, u32 length) {
    ++info.rx_packets;
    static const u8 broadcast[6]={255,255,255,255,255,255};
    if (!info.address || length<14 || (p[6]&1) ||
        (memcmp(p,info.mac,6) && memcmp(p,broadcast,6))) {++info.dropped;return;}
    if (nv_net_get16(p+12)==0x806) { receive_arp(p,length); return; }
    if (nv_net_get16(p+12)!=0x800 || memcmp(p,info.mac,6)) {++info.dropped;return;}
    const u8 *ip=p+14; u32 size=nv_ipv4_validate(ip,length-14);
    if (!size || nv_net_get32(ip+16)!=info.address || !unicast(nv_net_get32(ip+12))) {
        ++info.dropped;return;
    }
    if (ip[9]==1 && size>=28 && ip[20]==8 && !ip[21] && !nv_net_checksum(ip+20,size-20)) {
        u8 reply[1514]={0}; ethernet(reply,p+6,0x800); memcpy(reply+14,ip,size);
        u8 *r=reply+14; nv_net_put32(r+12,info.address); memcpy(r+16,ip+12,4);
        r[8]=64; r[20]=0; r[22]=r[23]=0; nv_net_put16(r+22,nv_net_checksum(r+20,size-20));
        r[10]=r[11]=0; nv_net_put16(r+10,nv_net_checksum(r,20));
        transmit(reply,size+14); return;
    }
    if (!nv_udp_validate(ip,size) || size-28>NV_UDP_MAX) {++info.dropped;return;}
    u32 port=nv_net_get16(ip+22);
    for (u32 i=0;i<NV_TASK_MAX;++i) if (endpoints[i].pid && endpoints[i].port==port) {
        struct endpoint *e=&endpoints[i];
        if (e->count==4) break;
        struct nv_udp *packet=kmalloc(sizeof(*packet));
        if (!packet) break;
        packet->address=nv_net_get32(ip+12); packet->port=nv_net_get16(ip+20);
        packet->length=size-28; memcpy(packet->data,ip+28,size-28);
        e->packets[(e->head+e->count++)%4]=packet; return;
    }
    ++info.dropped;
}
void net_poll(void) {
    uptr flags=irq_save();
    if (info.ready) virtio_net_poll(receive);
    info.ready=virtio_net_ready();
    irq_restore(flags);
}
void net_init(void) {
    info.ready=virtio_net_init(info.mac);
    kprintf(info.ready ? "[ok] virtio-net: Ethernet, ARP/IPv4/ICMP/UDP (static configuration)\n" :
                        "[info] network: no supported virtio legacy Ethernet device\n");
}
void net_task_exit(u32 pid) {
    for (u32 i=0;i<NV_TASK_MAX;++i) if (endpoints[i].pid==pid) {
        struct endpoint *e=&endpoints[i];
        while (e->count) { kfree(e->packets[e->head]);e->head=(e->head+1)%4;--e->count; }
        memset(e,0,sizeof(*e));
    }
}
static int send_udp(struct endpoint *e, const struct nv_udp *packet) {
    if (!info.address) return -NV_ENODEV;
    if (!unicast(packet->address) || !packet->port || packet->port>65535 ||
        packet->length>NV_UDP_MAX || packet->address==info.address) return -NV_EINVAL;
    u32 target=packet->address;
    if ((target ^ info.address) & info.mask) target=info.gateway;
    if (!target || !(target & ~info.mask) || (target & ~info.mask)==~info.mask) return -NV_EINVAL;
    const u8 *mac=NULL;
    for (u32 i=0;i<ARRAY_LEN(neighbors);++i)
        if (neighbors[i].ip==target && (u32)(ticks-neighbors[i].seen)<6000) mac=neighbors[i].mac;
    if (!mac) {arp_request(target);return -NV_EAGAIN;}
    u8 p[1514]={0}; ethernet(p,mac,0x800);
    u8 *ip=p+14; ip[0]=0x45; nv_net_put16(ip+2,(u16)(28+packet->length));
    nv_net_put16(ip+6,0x4000); ip[8]=64;ip[9]=17;
    nv_net_put32(ip+12,info.address);nv_net_put32(ip+16,packet->address);
    nv_net_put16(ip+10,nv_net_checksum(ip,20));
    nv_net_put16(ip+20,(u16)e->port);nv_net_put16(ip+22,(u16)packet->port);
    nv_net_put16(ip+24,(u16)(8+packet->length));
    /* Zero UDP checksum is permitted for IPv4. RX verifies nonzero checksums. */
    memcpy(ip+28,packet->data,packet->length);
    return transmit(p,MAX(60u,42+packet->length));
}
int net_ioctl(u32 op, u32 ptr) {
    u32 size=op==NV_NET_INFO ? sizeof(info) : op==NV_NET_CONFIG ? sizeof(struct nv_net_config) :
             op==NV_NET_BIND ? sizeof(u32) : (op==NV_NET_SEND || op==NV_NET_RECV) ? sizeof(struct nv_udp) : 0;
    if (!size) return -NV_EINVAL;
    if (!user_range(current->pd,ptr,size,op==NV_NET_INFO || op==NV_NET_RECV)) return -NV_EFAULT;
    if (op==NV_NET_INFO) { memcpy((void *)(uptr)ptr,&info,sizeof(info));return 0; }
    if (!info.ready) return -NV_ENODEV;
    if (op==NV_NET_CONFIG) {
        struct nv_net_config c;memcpy(&c,(void *)(uptr)ptr,sizeof(c));
        u32 host=~c.mask;
        if (!unicast(c.address) || !c.mask || host<3 || (host & (host+1)) ||
            !(c.address & host) || (c.address & host)==host ||
            (c.gateway && (!unicast(c.gateway) || ((c.gateway ^ c.address)&c.mask) ||
                           !(c.gateway & host) || (c.gateway & host)==host))) return -NV_EINVAL;
        if (c.address==info.address && c.mask==info.mask && c.gateway==info.gateway) return 0;
        for(u32 i=0;i<NV_TASK_MAX;++i) if(endpoints[i].pid) return -NV_EBUSY;
        info.address=c.address;info.mask=c.mask;info.gateway=c.gateway;
        memset(neighbors,0,sizeof(neighbors));arp_pending=0;return 0;
    }
    struct endpoint *e=NULL,*empty=NULL;
    for (u32 i=0;i<NV_TASK_MAX;++i) {
        if (endpoints[i].pid==current->pid) e=&endpoints[i];
        if (!endpoints[i].pid) empty=&endpoints[i];
    }
    if (op==NV_NET_BIND) {
        u32 port;memcpy(&port,(void *)(uptr)ptr,sizeof(port));
        if (!port) {net_task_exit(current->pid);return 0;}
        if (port>65535) return -NV_EINVAL;
        if (e) return e->port==port ? 0 : -NV_EBUSY;
        for(u32 i=0;i<NV_TASK_MAX;++i) if(endpoints[i].pid && endpoints[i].port==port) return -NV_EBUSY;
        if(!empty) return -NV_ENOSPC;
        empty->pid=current->pid;empty->port=port;return 0;
    }
    if (!e) return -NV_EBADF;
    if (op==NV_NET_SEND) {
        struct nv_udp packet;memcpy(&packet,(void *)(uptr)ptr,sizeof(packet));
        return send_udp(e,&packet);
    }
    if (!e->count) return -NV_EAGAIN;
    struct nv_udp *packet=e->packets[e->head];
    memcpy((void *)(uptr)ptr,packet,sizeof(*packet));kfree(packet);
    e->packets[e->head]=NULL;e->head=(e->head+1)%4;--e->count;return 0;
}
