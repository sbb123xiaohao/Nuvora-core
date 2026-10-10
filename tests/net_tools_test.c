/* Parse real HTTP response shapes before a downloader creates a target file. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <nv/abi.h>
#define NV_SDK_H
iptr nv_syscall(u32,uptr,uptr,uptr);
#include "../user/runtime.h"
#include "../user/network.h"
#include "../user/net_tools.h"

void println(const char *s) { (void)s; }
static struct nv_net_udp query;
static u32 dns_received,dns_sent,dns_attack,entropy_calls;
static bool entropy_ready=true;
static u64 fake_ticks;
static void dns_put16(u8 *p,u16 value) { p[0]=(u8)(value>>8);p[1]=(u8)value; }
static u32 dns_fixture(u8 *p) {
    memset(p,0,NV_NET_DATA_MAX);
    memcpy(p,query.data,query.length);p[2]=0x81;p[3]=0x80;dns_put16(p+6,1);
    u32 at=query.length;
    p[at++]=0xc0;p[at++]=12;
    dns_put16(p+at,1);dns_put16(p+at+2,1);dns_put16(p+at+8,4);at+=10;
    p[at++]=192;p[at++]=0;p[at++]=2;p[at++]=10;
    return at;
}
iptr nv_syscall(u32 op,uptr a,uptr b,uptr c) {
    if (op==NV_CLOCK64) return (iptr)fake_ticks++;
    if (op==NV_SLEEP) { fake_ticks+=(a+9)/10;return 0; }
    assert(op==NV_DEVCTL);
    if (a==NV_SUB_CPU) {
        assert(b==NV_CPU_RANDOM);
        struct nv_random *r=(void *)c;++entropy_calls;
        if (!entropy_ready) return -NV_ENODEV;
        assert(r->length==4);
        for (u32 i=0;i<4;++i) r->data[i]=(u8)(entropy_calls*37+i*61);
        return 0;
    }
    assert(a==NV_SUB_NET);
    struct nv_net_udp *io=(void *)c;
    if (b==NV_NET_UDP_SEND) { query=*io;++dns_sent;return 0; }
    assert(b==NV_NET_UDP_RECV);
    assert(io->local_port==query.local_port);
    io->address=query.address;io->port=53;io->length=dns_fixture(io->data);
    if (!dns_received++) {
        u32 at=query.length;
        if (dns_attack==1) io->data[13]='z'; /* Wrong question, matching transaction ID. */
        if (dns_attack==2) io->data[at+1]=20; /* Answer owner is only net, not example.net. */
        if (dns_attack==3) io->data[2]|=2; /* Truncated reply. */
        if (dns_attack==4) io->data[2]|=8; /* Wrong opcode. */
        if (dns_attack==5) dns_put16(io->data+4,0);
        if (dns_attack==6) dns_put16(io->data+at-4,28);
        if (dns_attack==7) dns_put16(io->data+at-2,3);
        if (dns_attack) { io->data[io->length-4]=11;io->data[io->length-3]=173; }
    }
    return io->length;
}
static void dns_clients(void) {
    struct nv_net_info network={.index=2,.dns=0xc0000235};
    for (dns_attack=1;dns_attack<=7;++dns_attack) {
        dns_received=dns_sent=0;u32 address=0;
        assert(!resolve_host("example.net",&network,&address));
        assert(address==0xc000020au && dns_received==2 && dns_sent==1);
        assert(query.local_port>=49152 && query.local_port<=65535);
    }
    u32 before=dns_sent;entropy_ready=false;
    assert(resolve_host("example.net",&network,&before)==-NV_ENODEV && dns_sent==before);
    entropy_ready=true;
    puts("PASS DNS client: randomized query, wrong question/owner/opcode/TC/count/type/class ignored; entropy failure sends nothing");
}
static u32 dns_wire_name(u8 *p,u32 at,const char *name) {
    while (*name) {
        const char *end=name;while (*end && *end!='.') ++end;
        p[at++]=(u8)(end-name);memcpy(p+at,name,(usize)(end-name));at+=(u32)(end-name);
        name=*end?end+1:end;
    }
    p[at++]=0;return at;
}
static void dns_parsers(void) {
    memset(&query,0,sizeof(query));query.data[0]=0x76;query.data[1]=0x41;query.data[5]=1;
    u32 end=dns_wire_name(query.data,12,"example.net");
    dns_put16(query.data+end,1);dns_put16(query.data+end+2,1);query.length=end+4;
    u8 packet[NV_NET_DATA_MAX];u32 size=dns_fixture(packet),address=0;
    assert(!dns_answer(packet,size,"EXAMPLE.NET",0x7641,&address) && address==0xc000020au);
    packet[13]='E';assert(!dns_answer(packet,size,"example.net",0x7641,&address));
    packet[13]='e';packet[query.length+1]=(u8)query.length;
    assert(dns_answer(packet,size,"example.net",0x7641,&address)==-NV_EIO); /* Self pointer. */
    size=dns_fixture(packet);packet[query.length+1]=(u8)(query.length+2);
    assert(dns_answer(packet,size,"example.net",0x7641,&address)==-NV_EIO); /* Forward pointer. */
    memset(packet,0,sizeof(packet));packet[12]=11;memcpy(packet+13,"example.net",11);
    char name[254];u32 next=0;
    assert(!dns_name(packet,25,12,name,&next)); /* One label containing a literal dot. */
    size=dns_fixture(packet);dns_put16(packet+query.length+2,5);
    dns_put16(packet+query.length+10,2);packet[query.length+12]=0xc0;packet[query.length+13]=12;
    assert(dns_answer(packet,size-2,"example.net",0x7641,&address)==-NV_EIO); /* CNAME to itself. */
    for (u32 reverse=0;reverse<2;++reverse) {
        dns_fixture(packet);dns_put16(packet+6,2);u32 at=query.length;
        for (u32 record=0;record<2;++record) {
            bool cname=record==reverse;
            if (cname) { packet[at++]=0xc0;packet[at++]=12; }
            else at=dns_wire_name(packet,at,"cdn.example.net");
            dns_put16(packet+at,cname?5:1);dns_put16(packet+at+2,1);
            memset(packet+at+4,0,4);u32 header=at;at+=10;
            if (cname) at=dns_wire_name(packet,at,"cdn.example.net");
            else { packet[at++]=192;packet[at++]=0;packet[at++]=2;packet[at++]=10; }
            dns_put16(packet+header+8,(u16)(at-header-10));
        }
        assert(!dns_answer(packet,at,"example.net",0x7641,&address) && address==0xc000020au);
    }
    size=dns_fixture(packet);
    u8 *guard=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(guard!=MAP_FAILED && !mprotect(guard+4096,4096,PROT_NONE));
    for (u32 n=0;n<size;++n) {
        u8 *at=guard+4096-n;memcpy(at,packet,n);address=0xfeedbeef;
        assert(dns_answer(at,n,"example.net",0x7641,&address)<0 && address==0xfeedbeef);
    }
    u32 seed=0x35ac2197;
    for (u32 run=0;run<10000;++run) {
        u32 n=run%NV_NET_DATA_MAX+1;u8 *at=guard+4096-n;
        for (u32 i=0;i<n;++i) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;at[i]=(u8)seed; }
        if (n>=12) { memcpy(at,query.data,12);at[2]=0x81;at[3]=0x80;dns_put16(at+6,1); }
        if (!(run%2) && n>=query.length) memcpy(at+12,query.data+12,query.length-12);
        if (!(run%4) && n>=query.length+2) { at[query.length]=0xc0;at[query.length+1]=12; }
        (void)dns_answer(at,n,"example.net",0x7641,&address);
    }
    munmap(guard,8192);
    puts("PASS DNS parser: case-insensitive names, compressed owners, both CNAME orders, invalid pointers, every truncation and 10000 guarded malformed packets");
}

static int headers(const char *text, bool *known, u64 *length, bool *chunked) {
    u32 n = strlen(text);
    assert(n + 1 < sizeof(http_headers));
    memcpy(http_headers, text, n + 1);
    return http_response(n, known, length, chunked);
}
struct response_case {
    const char *name, *text;
    int result;
    bool known;
    u64 length;
};
static const struct response_case response_cases[] = {
    {"chunked-tab", "HTTP/1.1 200 OK\r\nTransfer-Encoding:\tchunked\r\n\r\n", -NV_ENOSYS, false, 0},
    {"chunked-nospace", "HTTP/1.1 200 OK\r\nTransfer-Encoding:chunked\r\n\r\n", -NV_ENOSYS, false, 0},
    {"chunked-spaces", "HTTP/1.1 200 OK\r\nTransfer-Encoding:  ChUnKeD \t\r\n\r\n", -NV_ENOSYS, false, 0},
    {"transfer-list", "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n", -NV_ENOSYS, false, 0},
    {"transfer-gzip", "HTTP/1.0 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n", -NV_ENOSYS, false, 0},
    {"encoding-prefix", "HTTP/1.0 200 OK\r\nContent-Encoding: identity-gzip\r\n\r\n", -NV_ENOSYS, false, 0},
    {"encoding-identity", "HTTP/1.0 200 OK\r\nContent-Encoding:\tIDENTITY \t\r\n\r\n", 0, false, 0},
    {"length-spaces", "HTTP/1.0 200 OK\r\nContent-Length:\t11 \t\r\n\r\n", 0, true, 11},
    {"length-conflict", "HTTP/1.0 200 OK\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\n", -NV_EIO, false, 0},
    {"length-repeat", "HTTP/1.0 200 OK\r\nContent-Length: 3\r\nContent-Length: 3 \t\r\n\r\n", 0, true, 3},
    {"length-max", "HTTP/1.0 200 OK\r\nContent-Length: 18446744073709551615\r\n\r\n", 0, true, ~0ull},
    {"length-overflow", "HTTP/1.0 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n", -NV_E2BIG, false, 0},
    {"length-nondigit", "HTTP/1.0 200 OK\r\nContent-Length: +3\r\n\r\n", -NV_EIO, false, 0},
    {"status-prefix", "HTTP/1.0 2000 Bad\r\n\r\n", -NV_EIO, false, 0},
    {"version-unknown", "HTTP/1.9 200 OK\r\n\r\n", -NV_EIO, false, 0},
    {"incomplete", "HTTP/1.0 200 OK\r\nContent-Length: 3\r\n", -NV_EIO, false, 0},
    {"field-space", "HTTP/1.1 200 OK\r\nTransfer-Encoding : chunked\r\n\r\n", -NV_EIO, false, 0},
    {"field-colon", "HTTP/1.1 200 OK\r\nTransfer-Encoding chunked\r\n\r\n", -NV_EIO, false, 0},
    {"field-fold", "HTTP/1.1 200 OK\r\n Transfer-Encoding: chunked\r\n\r\n", -NV_EIO, false, 0},
    {"value-newline", "HTTP/1.1 200 OK\r\nX-Test: a\nb\r\n\r\n", -NV_EIO, false, 0},
    {"value-control", "HTTP/1.1 200 OK\r\nX-Test: a\177b\r\n\r\n", -NV_EIO, false, 0},
    {"unknown-field", "HTTP/1.1 200 OK\r\nX-Test_~!: opaque\r\n\r\n", 0, false, 0},
};
static void response_regression(const struct response_case *test) {
    bool known = false, chunked = false;
    u64 length = 0;
    int result = headers(test->text, &known, &length, &chunked);
    if (result != test->result)
        fprintf(stderr, "%s: expected %d, got %d\n", test->name, test->result, result);
    assert(result == test->result);
    if (!result) assert(known == test->known && length == test->length);
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    if (argc == 2) {
        if (!strcmp(argv[1],"dns-client")) { dns_clients();return 0; }
        if (!strcmp(argv[1],"dns-parser")) { dns_parsers();return 0; }
        for (u32 i = 0; i < ARRAY_LEN(response_cases); ++i)
            if (!strcmp(argv[1], response_cases[i].name)) {
                response_regression(&response_cases[i]); return 0;
            }
        assert(!"unknown HTTP regression case");
    }
    char host[254], path[768];
    u32 port;
    assert(http_url("http://example.net:8000/dir/a.txt?q=1#part", host, path, &port) == 0);
    assert(!strcmp(host, "example.net") && port == 8000 &&
           !strcmp(path, "/dir/a.txt?q=1"));
    assert(http_url("HTTP://10.0.2.2", host, path, &port) == 0);
    assert(!strcmp(path, "/") && port == 80);
    assert(http_url("https://example.net/", host, path, &port) == -NV_ENOSYS);
    assert(http_url("http://example.net:0/", host, path, &port) == -NV_EINVAL);
    assert(http_url("h", host, path, &port) == -NV_EINVAL);
    bool known = false, chunked = false;
    u64 length = 0;
    assert(headers("HTTP/1.0 200 OK\r\nContent-Length: 12345678901\r\n\r\n",
                   &known, &length, &chunked) == 0);
    assert(known && length == 12345678901ull && !chunked);
    assert(headers("HTTP/1.1 301 Moved\r\nLocation: /elsewhere\r\n\r\n",
                   &known, &length, &chunked) == -NV_EIO);
    assert(headers("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",
                   &known, &length, &chunked) == -NV_ENOSYS);
    assert(headers("HTTP/1.0 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n",
                   &known, &length, &chunked) == -NV_E2BIG);
    for (u32 i = 0; i < ARRAY_LEN(response_cases); ++i) response_regression(&response_cases[i]);
    const char embedded_nul[] = "HTTP/1.0 200 OK\r\nX-Test: a\0b\r\n\r\n";
    memcpy(http_headers, embedded_nul, sizeof(embedded_nul));
    assert(http_response(sizeof(embedded_nul) - 1, &known, &length, &chunked) == -NV_EIO);
    memset(http_headers, 'A', sizeof(http_headers));
    assert(http_response(sizeof(http_headers), &known, &length, &chunked) == -NV_E2BIG);
    dns_clients();dns_parsers();
    puts("PASS network tools: HTTP URL, response and length bounds");
}
