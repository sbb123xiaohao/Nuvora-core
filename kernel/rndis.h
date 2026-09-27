#ifndef NV_RNDIS_H
#define NV_RNDIS_H
#include <nv/types.h>
#include <nv/string.h>

/* A bounded USB RNDIS data path. Wire words are little endian on x86-64.
 * One 2 KiB DMA page holds a complete Ethernet packet and its 44-byte header. */
#define RNDIS_FRAME_MAX 1514u
#define RNDIS_PACKET_HEADER 44u
static u32 rndis_word(const u8 *data, u32 offset) {
    return (u32)data[offset] | (u32)data[offset+1]<<8 |
           (u32)data[offset+2]<<16 | (u32)data[offset+3]<<24;
}
static void rndis_put(u8 *data, u32 offset, u32 value) {
    for (u32 i=0;i<4;++i) data[offset+i]=(u8)(value>>(i*8));
}
static bool rndis_reply(const u8 *data, u32 got, u32 type, u32 xid) {
    return got >= 16 && rndis_word(data,0)==(type|0x80000000u) &&
           rndis_word(data,4)>=16 && rndis_word(data,4)<=got &&
           rndis_word(data,8)==xid && rndis_word(data,12)==0;
}
static bool rndis_query_data(const u8 *data, u32 got, u8 mac[6]) {
    if (got<24 || rndis_word(data,4)<24 || rndis_word(data,4)>got ||
        rndis_word(data,16)!=6) return false;
    u32 offset=rndis_word(data,20);
    if (offset<16 || offset>got-14 || offset+14>rndis_word(data,4)) return false;
    memcpy(mac,data+8+offset,6);
    if (mac[0]&1) return false;
    u8 or_all=0;
    for (u32 i=0;i<6;++i) or_all |= mac[i];
    return or_all!=0;
}
static u32 rndis_packet(u8 *out, const void *frame, u32 bytes) {
    if (bytes<14 || bytes>RNDIS_FRAME_MAX) return 0;
    memset(out,0,RNDIS_PACKET_HEADER);
    rndis_put(out,0,1);
    rndis_put(out,4,RNDIS_PACKET_HEADER+bytes);
    rndis_put(out,8,RNDIS_PACKET_HEADER-8);
    rndis_put(out,12,bytes);
    memcpy(out+RNDIS_PACKET_HEADER,frame,bytes);
    return RNDIS_PACKET_HEADER+bytes;
}
static bool rndis_packets(const u8 *data, u32 bytes,
                          void (*receive)(const void *,u32)) {
    while (bytes>=RNDIS_PACKET_HEADER) {
        if (!rndis_word(data,0)) break;
        u32 length=rndis_word(data,4), offset=rndis_word(data,8), frame=rndis_word(data,12);
        if (rndis_word(data,0)!=1 || length<RNDIS_PACKET_HEADER || length>bytes ||
            offset<RNDIS_PACKET_HEADER-8 || offset>length-8 ||
            frame<14 || frame>RNDIS_FRAME_MAX || frame>length-8-offset)
            return false;
        receive(data+8+offset,frame);
        data+=length; bytes-=length;
    }
    /* Some devices round an aggregate to their endpoint packet size. */
    for (u32 i=0;i<bytes;++i) if (data[i]) return false;
    return true;
}
#endif
