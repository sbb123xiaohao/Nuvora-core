#include <assert.h>
#include <stdio.h>
#include "../kernel/rndis.h"

static u32 calls, seen[3];
static void receive(const void *data, u32 size) {
    assert(calls<3 && ((const u8 *)data)[0]==(u8)(calls+1));
    seen[calls++]=size;
}
int main(void) {
    u8 bytes[2048]={0}, frame[60]={1};
    assert(rndis_packet(bytes,frame,sizeof(frame))==104);
    assert(rndis_word(bytes,0)==1 && rndis_word(bytes,4)==104 &&
           rndis_word(bytes,8)==36 && rndis_word(bytes,12)==60);
    assert(rndis_packets(bytes,104,receive) && calls==1 && seen[0]==60);
    frame[0]=2;
    assert(rndis_packet(bytes+104,frame,sizeof(frame))==104);
    calls=0; frame[0]=1;
    assert(rndis_packet(bytes,frame,sizeof(frame))==104);
    assert(rndis_packets(bytes,208,receive) && calls==2);
    memset(bytes+208,0,64);
    calls=0;
    assert(rndis_packets(bytes,272,receive) && calls==2);
    rndis_put(bytes+104,8,0xffffffffu);
    calls=0;
    /* The first packet is delivered before a later malformed message. */
    assert(!rndis_packets(bytes,208,receive) && calls==1);
    rndis_put(bytes,4,2049);
    calls=0;
    assert(!rndis_packets(bytes,104,receive) && !calls);
    assert(!rndis_packet(bytes,frame,13));
    assert(!rndis_packet(bytes,frame,1515));

    memset(bytes,0,128);
    rndis_put(bytes,0,0x80000004);
    rndis_put(bytes,4,30);
    rndis_put(bytes,8,9);
    rndis_put(bytes,16,6);
    rndis_put(bytes,20,16);
    u8 mac[6]={2,1,2,3,4,5};
    memcpy(bytes+24,mac,6);
    assert(rndis_reply(bytes,30,4,9) && rndis_query_data(bytes,30,mac));
    assert(!rndis_reply(bytes,30,4,10));
    assert(!rndis_reply(bytes,29,4,9));
    rndis_put(bytes,20,0xfffffff0u);
    assert(!rndis_query_data(bytes,30,mac));
    puts("PASS RNDIS: bounded replies, MAC query, packets, aggregation and malformed offsets");
}
