/* End-to-end Ethernet/IPv4/DHCP/UDP frames against the kernel implementation.
 * Only the physical NIC and privileged system-call address check are mocked. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
struct task { void *pd; };
static struct task task_object;
static struct task *current = &task_object;
static u32 ticks;
static bool link_up = true;
static bool bridge_up;
static u8 sent[1514];
static u32 sent_length, transmissions;
static void pci_visit(void (*cb)(u32, u32, u32)) {
    cb(0x00002000, (0x15f3u << 16) | 0x8086, 0x02000000);
    cb(0x00002800, (0x2723u << 16) | 0x8086, 0x02800000);
}
static bool net_igc_start(u32 address, u8 mac[6]) {
    assert(address == 0x2000);
    const u8 assigned[6] = {2, 0, 0, 1, 2, 3};
    memcpy(mac, assigned, sizeof(assigned));
    return true;
}
static bool net_igc_link(void) { return link_up; }
static int net_igc_send(const void *p, u32 size) {
    assert(size <= sizeof(sent));
    memcpy(sent, p, size); sent_length = size; ++transmissions;
    return 0;
}
static void net_igc_poll(void (*receive)(const void *, u32)) { (void)receive; }
static bool usb_ecm_link(void) { return bridge_up; }
static int usb_ecm_send(const void *p, u32 size) {
    if (!bridge_up) return -NV_ENODEV;
    assert(size <= sizeof(sent));
    memcpy(sent, p, size); sent_length = size; ++transmissions;
    return 0;
}
static int usb_wifi_command(const char *p, u32 n) { (void)p; (void)n; return -NV_ENODEV; }
static int usb_wifi_read(char *p, u32 n) { (void)p; (void)n; return -NV_ENODEV; }
static bool user_range(void *pd, u32 address, u32 size, bool write) {
    (void)pd; (void)address; (void)size; (void)write; return false;
}
static void kprintf(const char *format, ...) { (void)format; }
#include "../kernel/net.c"

static void fixture_message(u8 *message, u8 type, u32 address) {
    memset(message, 0, 300);
    message[0] = 2; message[1] = 1; message[2] = 6;
    put32(message + 4, dhcp_xid);
    put32(message + 16, address);
    memcpy(message + 28, adapters[active].mac, 6);
    put32(message + 236, 0x63825363);
    message[240] = 53; message[241] = 1; message[242] = type;
    message[243] = 54; message[244] = 4; put32(message + 245, 0xc0a80101);
    message[249] = 1; message[250] = 4; put32(message + 251, 0xffffff00);
    message[255] = 3; message[256] = 4; put32(message + 257, 0xc0a80101);
    message[261] = 6; message[262] = 4; put32(message + 263, 0x08080808);
    message[267] = 255;
}
static void deliver(const u8 *body, u32 length, u32 source, u16 source_port, u16 dest_port) {
    u8 mac[6]; memcpy(mac, adapters[active].mac, sizeof(mac));
    assert(!udp_raw(mac, source, 0xffffffffu, source_port, dest_port, body, length));
    u8 packet[1514]; u32 size = sent_length;
    memcpy(packet, sent, size); receive(packet, size);
}
int main(void) {
    net_init();
    assert(count == 2 && active == 0 && adapters[0].type == NV_NET_WIRED);
    assert(adapters[1].type == NV_NET_WIFI && adapters[1].state == NV_NET_UNSUPPORTED);
    net_poll(); assert(adapters[0].state == NV_NET_LINK);

    lease_state = 1; dhcp_xid = 0x01234567;
    dhcp_send(false);
    assert(be16(sent + 12) == 0x0800 && be16(sent + 14 + 20) == 68 &&
           be16(sent + 14 + 22) == 67 && checksum(sent + 14, 20) == 0);
    u8 offer[300]; fixture_message(offer, 2, 0xc0a80164);
    deliver(offer, sizeof(offer), 0xc0a80101, 67, 68);
    assert(lease_state == 2 && offered == 0xc0a80164 && server == 0xc0a80101);
    u8 ack[300]; fixture_message(ack, 5, 0xc0a80164);
    deliver(ack, sizeof(ack), 0xc0a80101, 67, 68);
    assert(lease_state == 0 && adapters[0].state == NV_NET_ONLINE);
    assert(adapters[0].ip == 0xc0a80164 && adapters[0].mask == 0xffffff00 &&
           adapters[0].gateway == 0xc0a80101 && adapters[0].dns == 0x08080808);

    u8 arp[28] = {0};
    put16(arp, 1); put16(arp + 2, 0x0800); arp[4] = 6; arp[5] = 4;
    put16(arp + 6, 2);
    u8 router[6] = {2, 9, 8, 7, 6, 5};
    memcpy(arp + 8, router, 6); put32(arp + 14, adapters[0].gateway);
    input_arp(arp, sizeof(arp));
    assert(peer_ip == 0xc0a80101 && !memcmp(peer_mac, router, 6));
    u8 payload[] = {'h', 'e', 'l', 'l', 'o'};
    deliver(payload, sizeof(payload), 0xc0a80101, 7000, 40000);
    assert(inbox_full && inbox.length == 5 && inbox.port == 7000 &&
           inbox.local_port == 40000 && !memcmp(inbox.data, "hello", 5));
    inbox_full = false;
    sent[14 + 20 + 8] ^= 1; /* UDP payload changed without updating checksum */
    receive(sent, sent_length); assert(!inbox_full);
    sent[14 + 20 + 8] ^= 1;
    sent[24] ^= 1; /* tamper with IPv4 header checksum */
    receive(sent, sent_length); assert(!inbox_full);
    u32 before = transmissions;
    link_up = false; net_poll();
    assert(adapters[0].state == NV_NET_DOWN && adapters[0].ip == 0 && transmissions == before &&
           !inbox_full);
    u8 bridge[6] = {2, 1, 2, 3, 4, 5};
    net_usb_attach(0x303a, 0x0001, bridge);
    assert(count == 3 && usb_index == 2 && adapters[2].type == NV_NET_USB_BRIDGE);
    active = usb_index;
    bridge_up = true;
    net_poll();
    assert(adapters[2].state == NV_NET_LINK);
    lease_state = 1; dhcp_xid = 0x02468ace;
    dhcp_send(false);
    assert(lease_state == 1 && be16(sent + 12) == 0x0800);
    fixture_message(offer, 2, 0xc0a80165);
    deliver(offer, sizeof(offer), 0xc0a80101, 67, 68);
    fixture_message(ack, 5, 0xc0a80165);
    deliver(ack, sizeof(ack), 0xc0a80101, 67, 68);
    assert(adapters[2].state == NV_NET_ONLINE && adapters[2].ip == 0xc0a80165);
    net_poll();
    assert(adapters[2].state == NV_NET_ONLINE && adapters[2].ip == 0xc0a80165);
    bridge_up = false;
    net_poll();
    assert(adapters[2].state == NV_NET_DOWN && adapters[2].ip == 0);
    net_usb_detach();
    net_usb_attach(0x303a, 0x0001, bridge);
    assert(count == 3 && usb_index == 2); /* hotplug reuses its adapter slot */
    puts("PASS network: physical PCI and USB bridge, DHCP, ARP, UDP, checksum, link loss");
}
