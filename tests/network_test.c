/* End-to-end Ethernet/IPv4/DHCP/UDP frames against the kernel implementation.
 * Only the physical NIC and privileged system-call address check are mocked. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
struct task { u32 pid; void *pd; };
static struct task task_object;
static struct task *current = &task_object;
static u64 ticks;
static bool link_up = true;
static bool bridge_up;
static u8 sent[1514];
static u32 sent_length, transmissions;
static void *user_buffer;
static usize user_size;
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
static bool net_e1000_start(u32 address, u8 mac[6]) { (void)address; (void)mac; return false; }
static bool net_e1000_link(void) { return false; }
static int net_e1000_send(const void *p, u32 size) { (void)p; (void)size; return -NV_ENODEV; }
static void net_e1000_poll(void (*receive)(const void *, u32)) { (void)receive; }
static bool usb_ecm_link(void) { return bridge_up; }
static int usb_ecm_send(const void *p, u32 size) {
    if (!bridge_up) return -NV_ENODEV;
    assert(size <= sizeof(sent));
    memcpy(sent, p, size); sent_length = size; ++transmissions;
    return 0;
}
static int usb_wifi_command(const char *p, u32 n) { (void)p; (void)n; return -NV_ENODEV; }
static int usb_wifi_read(char *p, u32 n) { (void)p; (void)n; return -NV_ENODEV; }
static bool user_range(void *pd, uptr address, usize size, bool write) {
    (void)pd; (void)write;
    return user_buffer && address == (uptr)user_buffer && size == user_size;
}
static void kprintf(const char *format, ...) { (void)format; }
#include "../kernel/net.c"

static int call_net(u32 op, void *buffer, usize size) {
    user_buffer = buffer; user_size = size;
    int result = net_ioctl(op, (uptr)buffer);
    user_buffer = NULL; user_size = 0;
    return result;
}
static void exercise_adapter_selection(void) {
    u32 previous = active;
    const u32 absent[] = {NV_NET_MAX, NV_NET_MAX + 1u, 0xffffffffu, 1u};
    for (u32 i = 0; i < ARRAY_LEN(absent); ++i) {
        struct nv_net_static config = {.index = absent[i]};
        assert(call_net(NV_NET_SELECT, &config, sizeof(config)) == -NV_ENODEV);
        assert(active == previous);
    }
    struct nv_net_static config = {.index = wired};
    assert(!call_net(NV_NET_SELECT, &config, sizeof(config)) && active == previous);
    puts("PASS adapter selection: absent/unsupported indices rejected without changing active NIC");
}
static void exercise_usb_mac(void) {
    const u8 invalid[][6] = {{0}, {1, 2, 3, 4, 5, 6}, {255, 255, 255, 255, 255, 255}};
    u32 previous = active, before = count;
    for (u32 i = 0; i < ARRAY_LEN(invalid); ++i) {
        net_usb_attach(0x303a, 1, invalid[i]);
        assert(count == before && usb_index == NV_NET_MAX && active == previous);
    }
    const u8 mac[6] = {2, 1, 255, 3, 4, 255};
    net_usb_attach(0x303a, 1, mac);
    assert(count == before + 1 && usb_index == before && active == previous);
    struct nv_net_info info = {.index = usb_index};
    assert(call_net(NV_NET_INFO, &info, sizeof(info)) == 1 && !memcmp(info.mac, mac, 6));
    net_usb_detach();
    u32 slot = info.index;
    for (u32 i = 0; i < ARRAY_LEN(invalid); ++i) {
        net_usb_attach(0x303a, 2, invalid[i]);
        assert(count == before + 1 && usb_index == NV_NET_MAX && active == previous);
        info.index = slot;
        assert(call_net(NV_NET_INFO, &info, sizeof(info)) == 1 &&
               info.product == 1 && !memcmp(info.mac, mac, 6));
    }
    const u8 replacement[6] = {2, 0, 0, 0, 0, 1};
    net_usb_attach(0x303a, 2, replacement);
    info.index = slot;
    assert(count == before + 1 && usb_index == slot &&
           call_net(NV_NET_INFO, &info, sizeof(info)) == 1 &&
           info.product == 2 && !memcmp(info.mac, replacement, 6));
    bridge_up = true;
    struct nv_net_static config = {.index = slot, .ip = 0xc0a80165,
        .mask = 0xffffff00, .gateway = 0xc0a80101, .dns = 0x08080808};
    assert(!call_net(NV_NET_SELECT, &config, sizeof(config)));
    assert(!call_net(NV_NET_STATIC, &config, sizeof(config)));
    info.index = slot;
    assert(call_net(NV_NET_INFO, &info, sizeof(info)) == 1 &&
           info.ip == config.ip && info.mask == config.mask &&
           info.gateway == config.gateway && info.dns == config.dns);
    net_usb_detach();
    info.index = slot;
    assert(call_net(NV_NET_INFO, &info, sizeof(info)) == 1 &&
           info.state == NV_NET_DOWN && !info.ip && !info.mask &&
           !info.gateway && !info.dns && !info.reserved[0]);
    assert(active == previous && count == before + 1 && usb_index == NV_NET_MAX);
    puts("PASS USB network MAC: invalid addresses rejected, ff unicast accepted, hotplug slot preserved");
    puts("PASS USB network detach: the disconnected interface exposes no stale IPv4 routes or DNS");
}
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
static void exercise_configuration_reset(void) {
    link_up = true;
    const u32 operations[] = {NV_NET_STATIC, NV_NET_DHCP};
    for (u32 i = 0; i < ARRAY_LEN(operations); ++i) {
        struct nv_net_static config = {.index = active, .ip = 0xc0a80164,
            .mask = 0xffffff00, .gateway = 0xc0a80101, .dns = 0x08080808};
        assert(!call_net(NV_NET_STATIC, &config, sizeof(config)));
        u8 arp[28] = {0};
        put16(arp, 1); put16(arp + 2, 0x0800); arp[4] = 6; arp[5] = 4;
        put16(arp + 6, 2); arp[8] = 2; arp[13] = 1;
        put32(arp + 14, config.gateway);
        input_arp(arp, sizeof(arp));
        const u8 payload[] = {'o', 'l', 'd'};
        deliver(payload, sizeof(payload), config.gateway, 7000, 40000);
        assert(inbox_full && inbox.length == sizeof(payload));
        struct nv_net_ping ping = {.index = active, .address = config.gateway,
            .identifier = 7, .sequence = i + 1};
        assert(!call_net(NV_NET_PING, &ping, sizeof(ping)) && echo_probe.pending);
        struct nv_net_tcp tcp = {.index = active, .address = config.gateway, .port = 80};
        assert(!call_net(NV_NET_TCP_OPEN, &tcp, sizeof(tcp)) && stream.state == TCP_SYN_SENT);

        /* A rejected configuration must preserve existing traffic. */
        config.mask = 0xfffffeff;
        assert(call_net(NV_NET_STATIC, &config, sizeof(config)) == -NV_EINVAL);
        assert(inbox_full && echo_probe.pending && stream.state == TCP_SYN_SENT &&
               peer_ip == config.gateway && adapters[active].ip == config.ip);
        config.mask = 0xffffff00; ++config.ip;
        assert(!call_net(operations[i], &config, sizeof(config)));
        assert(!inbox_full && !echo_probe.pending && !peer_ip && stream.state == TCP_IDLE);
        struct nv_net_udp udp = {.index = active, .local_port = 40000};
        assert(call_net(NV_NET_UDP_RECV, &udp, sizeof(udp)) == -NV_EAGAIN);
        assert(adapters[active].ip == (operations[i] == NV_NET_STATIC ? config.ip : 0));
    }
    puts("PASS IPv4 reconfiguration: stale UDP/ping/ARP/TCP cleared; rejected config preserves traffic");
}
static void deliver_tcp(u32 seq, u32 ack, u8 flags, const u8 *body, u32 size) {
    u8 packet[20 + NV_NET_DATA_MAX] = {0};
    assert(size <= NV_NET_DATA_MAX);
    put16(packet, stream.remote_port); put16(packet + 2, stream.local_port);
    put32(packet + 4, seq); put32(packet + 8, ack);
    packet[12] = 5 << 4; packet[13] = flags;
    put16(packet + 14, NV_NET_DATA_MAX);
    if (size) memcpy(packet + 20, body, size);
    u8 pseudo[12] = {0};
    put32(pseudo, stream.address); put32(pseudo + 4, stream.source_ip);
    pseudo[9] = 6; put16(pseudo + 10, size + 20);
    u32 sum = (u16)~checksum(pseudo, sizeof(pseudo));
    for (u32 i = 0; i < size + 20; i += 2)
        sum += (u16)packet[i] << 8 | (i + 1 < size + 20 ? packet[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    put16(packet + 16, (u16)~sum);
    u8 frame_in[14 + 20 + 20 + NV_NET_DATA_MAX] = {0};
    memcpy(frame_in, adapters[active].mac, 6);
    memcpy(frame_in + 6, peer_mac, 6);
    put16(frame_in + 12, 0x0800);
    u8 *ip = frame_in + 14;
    ip[0] = 0x45; put16(ip + 2, 40 + size);
    ip[8] = 64; ip[9] = 6;
    put32(ip + 12, stream.address); put32(ip + 16, stream.source_ip);
    put16(ip + 10, checksum(ip, 20));
    memcpy(ip + 20, packet, 20 + size);
    receive(frame_in, 54 + size);
}
static struct nv_net_tcp open_tcp_fixture(void) {
    struct nv_net_static config = {.index = active, .ip = 0xc0a80164,
        .mask = 0xffffff00, .gateway = 0xc0a80101};
    assert(!call_net(NV_NET_STATIC, &config, sizeof(config)));
    u8 arp[28] = {0};
    put16(arp, 1); put16(arp + 2, 0x0800); arp[4] = 6; arp[5] = 4;
    put16(arp + 6, 2); arp[8] = 2; arp[13] = 1;
    put32(arp + 14, config.gateway);
    input_arp(arp, sizeof(arp));
    struct nv_net_tcp io = {.index = active, .address = 0xcb007101u, .port = 80};
    assert(!call_net(NV_NET_TCP_OPEN, &io, sizeof(io)));
    assert(sent[47] == 2 && stream.state == TCP_SYN_SENT);
    return io;
}
static struct nv_net_tcp establish_tcp_fixture(u32 peer_sequence) {
    struct nv_net_tcp io = open_tcp_fixture();
    deliver_tcp(peer_sequence, stream.next_tx, 0x12, NULL, 0);
    assert(call_net(NV_NET_TCP_OPEN, &io, sizeof(io)) == 1);
    assert(stream.next_rx == peer_sequence + 1);
    return io;
}
static void exercise_tcp_reset(void) {
    struct nv_net_tcp io = open_tcp_fixture();
    u32 ack = stream.next_tx;
    deliver_tcp(5000, ack, 0x04, NULL, 0); /* no ACK: not a response to our SYN */
    assert(!call_net(NV_NET_TCP_OPEN, &io, sizeof(io)));
    deliver_tcp(5000, ack - 1, 0x14, NULL, 0);
    assert(!call_net(NV_NET_TCP_OPEN, &io, sizeof(io)));
    deliver_tcp(5000, ack + 1, 0x14, NULL, 0);
    assert(!call_net(NV_NET_TCP_OPEN, &io, sizeof(io)));
    deliver_tcp(5000, ack, 0x16, NULL, 0); /* RST takes precedence over SYN */
    assert(call_net(NV_NET_TCP_OPEN, &io, sizeof(io)) == -NV_EIO);
    assert(stream.state == TCP_IDLE);
    io = open_tcp_fixture();
    deliver_tcp(5000, stream.next_tx, 0x14, NULL, 0);
    assert(call_net(NV_NET_TCP_OPEN, &io, sizeof(io)) == -NV_EIO);
    io = establish_tcp_fixture(5000);
    deliver_tcp(stream.next_rx + 1, stream.next_tx, 0x14, NULL, 0);
    assert(call_net(NV_NET_TCP_OPEN, &io, sizeof(io)) == 1);
    deliver_tcp(stream.next_rx, stream.next_tx, 0x14, NULL, 0);
    assert(call_net(NV_NET_TCP_OPEN, &io, sizeof(io)) == -NV_EIO);
    puts("PASS TCP reset: only an ACK of our SYN aborts opening; RST cannot establish a stream");
}
static void exercise_tcp_window_ack(void) {
    struct nv_net_tcp io = establish_tcp_fixture(5000);
    const u8 request[] = "request";
    io.length = sizeof(request) - 1; memcpy(io.data, request, io.length);
    u32 sequence = stream.next_tx;
    assert(call_net(NV_NET_TCP_SEND, &io, sizeof(io)) == (int)io.length);
    deliver_tcp(stream.next_rx + NV_NET_DATA_MAX, stream.next_tx, 0x10, NULL, 0);
    assert(stream.queued == io.length && stream.acked_tx == sequence);
    ticks += 100; net_poll();
    assert(be32(sent + 38) == sequence && be16(sent + 16) == 40 + io.length &&
           !memcmp(sent + 54, request, io.length));
    /* An ACK within the advertised window is valid even with out-of-order data. */
    const u8 data[] = "x";
    deliver_tcp(stream.next_rx + 1, sequence + 2, 0x18, data, 1);
    assert(stream.acked_tx == sequence + 2 && stream.queued == io.length - 2 &&
           !stream.used);
    deliver_tcp(stream.next_rx - 1, stream.next_tx, 0x18, data, 1);
    assert(stream.acked_tx == sequence + 2 && stream.queued == io.length - 2);
    ticks += 100; net_poll();
    assert(be32(sent + 38) == sequence + 2 && be16(sent + 16) == 40 + io.length - 2 &&
           !memcmp(sent + 54, request + 2, io.length - 2));
    deliver_tcp(stream.next_rx, stream.next_tx, 0x10, NULL, 0);
    assert(!stream.queued && stream.acked_tx == stream.next_tx);
    puts("PASS TCP window: invalid sequence numbers cannot cancel outgoing retransmissions");
}
static void exercise_tcp_future_ack(void) {
    struct nv_net_tcp io = establish_tcp_fixture(5000);
    io.length = 3; memcpy(io.data, "GET", 3);
    assert(call_net(NV_NET_TCP_SEND, &io, sizeof(io)) == 3);
    u32 sequence = stream.next_rx, acknowledged = stream.acked_tx;
    const u8 bad[] = "bad";
    deliver_tcp(sequence, stream.next_tx + 1, 0x19, bad, 3);
    assert(!stream.used && !stream.eof && stream.next_rx == sequence &&
           stream.acked_tx == acknowledged && stream.queued == 3);
    deliver_tcp(sequence, stream.next_tx, 0x12, NULL, 0); /* unexpected SYN */
    assert(stream.acked_tx == acknowledged && stream.queued == 3 && !stream.used);
    const u8 good[] = "ok";
    deliver_tcp(sequence, acknowledged - 1, 0x18, good, 2); /* stale ACK, valid data */
    assert(stream.acked_tx == acknowledged && stream.queued == 3);
    io.length = sizeof(io.data);
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == 2 && !memcmp(io.data, good, 2));
    deliver_tcp(stream.next_rx, stream.next_tx, 0x10, NULL, 0);
    assert(!stream.queued);
    puts("PASS TCP ACK bounds: future ACK/SYN cannot deliver data or FIN; stale ACK keeps valid data");
}
static void exercise_tcp_zero_window(void) {
    struct nv_net_tcp io = establish_tcp_fixture(5000);
    u8 data[NV_NET_DATA_MAX]; memset(data, 'a', sizeof(data));
    deliver_tcp(stream.next_rx, stream.next_tx, 0x19, data, sizeof(data));
    assert(stream.used == sizeof(data) && !stream.eof && be16(sent + 48) == 0);
    io.length = 1; io.data[0] = 'q';
    u32 sequence = stream.next_tx;
    assert(call_net(NV_NET_TCP_SEND, &io, sizeof(io)) == 1);
    deliver_tcp(stream.next_rx + 1, stream.next_tx, 0x10, NULL, 0);
    assert(stream.acked_tx == sequence && stream.queued == 1);
    deliver_tcp(stream.next_rx, stream.next_tx, 0x10, NULL, 0);
    assert(!stream.queued); /* a zero-length ACK at RCV.NXT is valid at zero window */
    u32 next = stream.next_rx;
    deliver_tcp(next, stream.next_tx, 0x18, data, 1);
    assert(stream.used == sizeof(data) && stream.next_rx == next);
    deliver_tcp(next, stream.next_tx, 0x11, NULL, 0);
    assert(!stream.eof && stream.next_rx == next);
    io.length = sizeof(io.data) / 2;
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == (int)sizeof(data) / 2);
    assert(be16(sent + 48) == sizeof(data) / 2);
    deliver_tcp(next, stream.next_tx, 0x11, NULL, 0);
    assert(stream.eof && stream.next_rx == next + 1 && be16(sent + 48) == 0);
    io.length = sizeof(io.data);
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == (int)sizeof(data) / 2);
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == 0);
    puts("PASS TCP zero window: ACKs stay usable; FIN waits for receive space and closes it");
}
static void exercise_tcp_eof(void) {
    struct nv_net_tcp io = establish_tcp_fixture(5000);
    const u8 data[] = "a";
    deliver_tcp(stream.next_rx, stream.next_tx, 0x19, data, 1);
    io.length = sizeof(io.data);
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == 1 && io.data[0] == 'a');
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == 0);
    u32 sequence = stream.next_rx;
    deliver_tcp(sequence, stream.next_tx, 0x18, data, 1);
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == 0 && stream.next_rx == sequence);
    deliver_tcp(sequence, stream.next_tx, 0x11, NULL, 0);
    assert(stream.next_rx == sequence && stream.eof);
    puts("PASS TCP EOF: late data/FIN cannot reopen or advance the closed receive stream");
}
static void exercise_tcp_wrap(void) {
    struct nv_net_tcp io = establish_tcp_fixture(0xfffffff7u);
    /* Seed the send boundary so a small real syscall write crosses sequence zero. */
    stream.next_tx = stream.acked_tx = 0xfffffffcu;
    io.length = 8; memcpy(io.data, "abcdefgh", io.length);
    assert(call_net(NV_NET_TCP_SEND, &io, sizeof(io)) == 8 && stream.next_tx == 4);
    deliver_tcp(stream.next_rx + NV_NET_DATA_MAX, 4, 0x10, NULL, 0);
    assert(stream.acked_tx == 0xfffffffcu && stream.queued == 8);
    const u8 data[] = "0123456789ab";
    deliver_tcp(stream.next_rx, 5, 0x18, data, sizeof(data) - 1);
    assert(!stream.used && stream.next_rx == 0xfffffff8u);
    deliver_tcp(stream.next_rx, 0, 0x18, data, sizeof(data) - 1);
    assert(stream.used == sizeof(data) - 1 && stream.next_rx == 4 &&
           stream.acked_tx == 0 && stream.queued == 4);
    const u8 overlap[] = "9abXYZ";
    deliver_tcp(1, 4, 0x19, overlap, sizeof(overlap) - 1);
    assert(!stream.queued && stream.next_rx == 8 && stream.eof);
    io.length = sizeof(io.data);
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == 15 &&
           !memcmp(io.data, "0123456789abXYZ", 15));
    assert(call_net(NV_NET_TCP_RECV, &io, sizeof(io)) == 0);
    puts("PASS TCP sequence wrap: partial ACK, overlapping data and FIN preserve ordering");
}
static void exercise_tcp_icmp(void) {
    u32 dest = 0xcb007101u;
    assert(!echo_request(dest, 0x1234, 7));
    assert(be16(sent + 12) == 0x0800 && sent[14 + 9] == 1 &&
           be32(sent + 14 + 16) == dest && checksum(sent + 14, 20) == 0 &&
           checksum(sent + 14 + 20, 16) == 0);
    u8 echo[16]; memcpy(echo, sent + 14 + 20, sizeof(echo));
    echo[0] = 0; echo[2] = echo[3] = 0;
    put16(echo + 2, checksum(echo, sizeof(echo)));
    echo_probe.pending = true;
    echo_probe.replied = false;
    echo_probe.address = dest;
    echo_probe.identifier = 0x1234;
    echo_probe.sequence = 7;
    u8 ip[36] = {0x45}; put16(ip + 2, sizeof(ip)); ip[8] = 52; ip[9] = 1;
    put32(ip + 12, dest); put32(ip + 16, adapters[active].ip);
    put16(ip + 10, checksum(ip, 20));
    memcpy(ip + 20, echo, sizeof(echo));
    input_ipv4(ip, sizeof(ip));
    assert(echo_probe.replied && echo_probe.ttl == 52 && echo_probe.bytes == 8);

    memset(&stream, 0, sizeof(stream));
    stream.owner = 3; stream.index = active; stream.address = dest;
    stream.source_ip = adapters[active].ip;
    stream.local_port = 49153; stream.remote_port = 80;
    stream.state = TCP_SYN_SENT; stream.next_tx = 901;
    stream.acked_tx = 900; stream.last_send = ticks - 100;
    net_poll();
    assert(be16(sent + 12) == 0x0800 && sent[14 + 9] == 6 &&
           be32(sent + 14 + 20 + 4) == 900 &&
           sent[14 + 20 + 13] == 2 &&
           tcp_checksum_valid(sent + 14 + 20, 20, stream.source_ip, dest));
    deliver_tcp(5000, 901, 0x12, NULL, 0);
    assert(stream.state == TCP_ESTABLISHED && stream.next_rx == 5001 &&
           stream.acked_tx == 901);
    assert(sent[14 + 20 + 13] == 0x10);
    const u8 request[] = "GET / HTTP/1.0\r\n\r\n";
    assert(!tcp_packet(stream.next_tx, 0x18, request, sizeof(request)-1));
    assert(tcp_checksum_valid(sent + 14 + 20, 20 + sizeof(request)-1,
                              stream.source_ip, dest));
    memcpy(stream.outgoing, request, sizeof(request)-1);
    stream.queued = sizeof(request)-1;
    stream.next_tx += stream.queued;
    stream.attempts = 1; stream.last_send = ticks;
    deliver_tcp(5001, stream.next_tx, 0x10, NULL, 0);
    assert(stream.acked_tx == stream.next_tx && !stream.queued);
    const u8 response[] = "HTTP/1.0 200 OK\r\n\r\nhello";
    deliver_tcp(5001, stream.next_tx, 0x18, response, sizeof(response)-1);
    assert(stream.used == sizeof(response)-1 &&
           !memcmp(stream.buffer, response, sizeof(response)-1));
    u32 next_rx = stream.next_rx;
    deliver_tcp(5001, stream.next_tx, 0x18, response, sizeof(response)-1);
    assert(stream.next_rx == next_rx && stream.used == sizeof(response)-1);
    deliver_tcp(next_rx, stream.next_tx, 0x11, NULL, 0);
    assert(stream.eof && stream.next_rx == next_rx + 1);
    net_task_release(3);
    assert(stream.state == TCP_IDLE);
}
int main(int argc, char **argv) {
    net_init();
    assert(count == 2 && active == 0 && adapters[0].type == NV_NET_WIRED);
    assert(adapters[1].type == NV_NET_WIFI && adapters[1].state == NV_NET_UNSUPPORTED);
    net_poll(); assert(adapters[0].state == NV_NET_LINK);
    task_object.pid = 3;
    assert(argc == 1 || argc == 2);
    if (argc == 2) {
        if (!strcmp(argv[1], "select")) exercise_adapter_selection();
        else if (!strcmp(argv[1], "usb-mac")) exercise_usb_mac();
        else if (!strcmp(argv[1], "tcp-reset")) exercise_tcp_reset();
        else if (!strcmp(argv[1], "tcp-window-ack")) exercise_tcp_window_ack();
        else if (!strcmp(argv[1], "tcp-future-ack")) exercise_tcp_future_ack();
        else if (!strcmp(argv[1], "tcp-zero-window")) exercise_tcp_zero_window();
        else if (!strcmp(argv[1], "tcp-eof")) exercise_tcp_eof();
        else if (!strcmp(argv[1], "tcp-wrap")) exercise_tcp_wrap();
        else {
            assert(!strcmp(argv[1], "reconfigure"));
            exercise_configuration_reset();
        }
        return 0;
    }
    exercise_adapter_selection();

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
    exercise_tcp_icmp();
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
    exercise_configuration_reset();
    puts("PASS network: PCI/USB, DHCP, UDP, ICMP and TCP frames, checksum, link loss");
}
