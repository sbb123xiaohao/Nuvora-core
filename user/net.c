#include "runtime.h"
#include <nv/net.h>
static void address(u32 value) {
    for(u32 i=0;i<4;++i) {if(i) print("."); print_u32((value>>(24-i*8))&255);}
}
static bool ip_parse(const char *p,u32 *out) {
    u32 ip=0;
    for(u32 i=0;i<4;++i) {
        u32 part=0,digits=0;
        while(*p>='0' && *p<='9') {part=part*10+(u32)(*p++-'0');if(++digits>3 || part>255)return false;}
        if(!digits || (i<3 && *p++!='.')) return false;
        ip=(ip<<8)|part;
    }
    if(*p) return false;
    *out=ip;return true;
}
int user_main(const char *args) {
    if(!strcmp(args,"--help")) {
        println("forge net [info | echo IP PORT TEXT | serve PORT]");
        println("Static QEMU network: 10.0.2.15/24, gateway 10.0.2.2. UDP only.");return 0;
    }
    struct nv_net_info state;
    int r=devctl(NV_SUB_NET,NV_NET_INFO,&state);
    if(r) return 1;
    if(!*args || !strcmp(args,"info")) {
        print("Ethernet: ");println(state.ready ? "virtio legacy ready" : "device unavailable");
        print("IPv4: ");address(state.address);print(" RX=");print_u32(state.rx_packets);
        print(" TX=");print_u32(state.tx_packets);print(" dropped=");print_u32(state.dropped);print("\n");return 0;
    }
    char line[NV_ARG_MAX],*v[8];strlcpy(line,args,sizeof(line));int n=tokenize(line,v,8);
    bool serve=n==2 && !strcmp(v[0],"serve");
    if(!serve && (n!=4 || strcmp(v[0],"echo"))) {println("Use forge net --help");return 1;}
    u32 port=0;
    const char *p=serve ? v[1] : v[2];
    if(!*p) return 1;
    while(*p) {if(*p<'0'||*p>'9')return 1;port=port*10+(u32)(*p++-'0');if(port>65535)return 1;}
    if(!port) return 1;
    struct nv_net_config config={0x0a00020f,0xffffff00,0x0a000202};
    if((r=devctl(NV_SUB_NET,NV_NET_CONFIG,&config))) {report_error("net config",r);return 2;}
    u32 local=serve ? port : 40000;
    if((r=devctl(NV_SUB_NET,NV_NET_BIND,&local))) {report_error("net bind",r);return 3;}
    struct nv_udp packet={0};
    if(serve) {
        print("UDP READY ");print_u32(port);print("\n");
        for(u32 i=0;i<1500;++i) {
            r=devctl(NV_SUB_NET,NV_NET_RECV,&packet);
            if(!r) {
                for(u32 j=0;j<300;++j) {
                    r=devctl(NV_SUB_NET,NV_NET_SEND,&packet);
                    if(r!=-NV_EAGAIN) break;
                    nap(10);
                }
                if(r) return 4;
                println("UDP SERVER PASS");return 0;
            }
            if(r!=-NV_EAGAIN) return 5;
            nap(10);
        }
    } else {
        if(!ip_parse(v[1],&packet.address))return 1;
        packet.port=port;packet.length=(u32)strlen(v[3]);memcpy(packet.data,v[3],packet.length);
        for(u32 i=0;i<300;++i) {
            r=devctl(NV_SUB_NET,NV_NET_SEND,&packet);
            if(r!=-NV_EAGAIN) break;
            nap(10);
        }
        if(r) {report_error("net send",r);return 4;}
        for(u32 i=0;i<300;++i) {
            struct nv_udp reply;
            r=devctl(NV_SUB_NET,NV_NET_RECV,&reply);
            if(!r && reply.address==packet.address && reply.port==port &&
                reply.length==packet.length && !memcmp(reply.data,packet.data,packet.length)) {
                println("UDP ECHO PASS");return 0;
            }
            if(r && r!=-NV_EAGAIN)return 5;
            nap(10);
        }
    }
    println("UDP timeout");return 6;
}
