#include "kernel.h"

/* IPv4/Ethernet path shared by physical NICs. Protocol numbers and packet
 * fields are written bytewise, so they do not depend on host endianness. */
static struct nv_net_info adapters[NV_NET_MAX];
static u32 count, active = NV_NET_MAX, wired = NV_NET_MAX, usb_index = NV_NET_MAX;
enum { WIRED_NONE, WIRED_IGC, WIRED_E1000 };
static u32 wired_driver;
static u64 last_dhcp;
static u32 dhcp_xid;
static u8 lease_state; /* 0 idle; 1 discover; 2 request */
static u32 offered, server;
static struct nv_net_udp inbox;
static bool inbox_full, polling;
static u8 frame[1514];
static u8 peer_mac[6];
static u32 peer_ip;
static u64 peer_valid_until;
static struct {
    u32 address, identifier, sequence, ttl, bytes;
    u64 started;
    bool pending, replied;
} echo_probe;
enum { TCP_IDLE, TCP_SYN_SENT, TCP_ESTABLISHED, TCP_ERROR };
static struct {
    u32 owner, index, address, source_ip, next_tx, acked_tx, next_rx;
    u64 last_send, ack_last_send;
    u32 attempts, queued, used;
    u16 local_port, remote_port;
    u8 state, buffer[NV_NET_DATA_MAX], outgoing[NV_NET_DATA_MAX];
    bool eof, ack_pending;
} stream;

/* Traffic and neighbour state belong to the current interface and IPv4
 * configuration. Never return a packet or finish a probe from the old one. */
static void reset_protocol_state(void) {
    lease_state = 0; peer_ip = 0; inbox_full = false;
    echo_probe.pending = false; stream.state = TCP_IDLE; stream.ack_pending = false;
}
static u16 be16(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static void put16(u8 *p, u16 n) { p[0] = (u8)(n >> 8); p[1] = (u8)n; }
static void put32(u8 *p, u32 n) {
    p[0] = (u8)(n >> 24); p[1] = (u8)(n >> 16); p[2] = (u8)(n >> 8); p[3] = (u8)n;
}
static u16 checksum(const u8 *p, u32 size) {
    u32 sum = 0;
    for (u32 i = 0; i < size; i += 2)
        sum += (u16)p[i] << 8 | (i + 1 < size ? p[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)~sum;
}
static bool udp_checksum_valid(const u8 *packet, u32 size, u32 src, u32 dest) {
    if (!be16(packet + 6)) return true; /* valid IPv4 UDP checksum omission */
    u8 pseudo[12] = {0};
    put32(pseudo, src); put32(pseudo + 4, dest);
    pseudo[9] = 17; put16(pseudo + 10, (u16)size);
    u32 sum = (u16)~checksum(pseudo, sizeof(pseudo));
    for (u32 i = 0; i < size; i += 2)
        sum += (u16)packet[i] << 8 | (i + 1 < size ? packet[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)sum == 0xffff;
}
static bool supported(u32 vendor, u32 product) {
    return vendor == 0x8086 && (product == 0x15f2 || product == 0x15f3 ||
                                 product == 0x125b || product == 0x125c);
}
static bool e1000_supported(u32 vendor, u32 product) {
    if (vendor != 0x8086) return false;
    switch (product) {
    case 0x100e: case 0x100f: case 0x1010: case 0x1011:
    case 0x10d3: case 0x10ea: case 0x10eb: case 0x10ef:
    case 0x10f0: case 0x10f5: case 0x10f6: case 0x1503:
        return true;
    default:
        return false;
    }
}
static void discover(u32 address, u32 id, u32 cls) {
    if ((cls >> 16) != 0x0200 && (cls >> 16) != 0x0280) return;
    if (count == NV_NET_MAX) return;
    struct nv_net_info *n = &adapters[count];
    n->index = count++;
    n->vendor = id & 0xffff;
    n->product = id >> 16;
    n->bus = (address >> 16) & 255;
    n->device = (address >> 11) & 31;
    n->function = (address >> 8) & 7;
    n->type = cls >> 16 == 0x0280 ? NV_NET_WIFI : NV_NET_WIRED;
    n->state = NV_NET_UNSUPPORTED;
    if (n->type == NV_NET_WIRED && wired == NV_NET_MAX &&
        supported(n->vendor, n->product)) {
        if (net_igc_start(address, n->mac)) {
            wired = active = n->index;
            wired_driver = WIRED_IGC;
            n->state = NV_NET_DOWN;
        } else n->state = NV_NET_DOWN;
    } else if (n->type == NV_NET_WIRED && wired == NV_NET_MAX &&
               e1000_supported(n->vendor, n->product)) {
        if (net_e1000_start(address, n->mac)) {
            wired = active = n->index;
            wired_driver = WIRED_E1000;
            n->state = NV_NET_DOWN;
        } else n->state = NV_NET_DOWN;
    }
}
void net_init(void) {
    pci_visit(discover);
    kprintf("[net] %u PCI network device(s), physical wired driver %s\n", count,
            wired_driver == WIRED_IGC ? "igc" :
            wired_driver == WIRED_E1000 ? "e1000" : "unavailable");
}
void net_usb_attach(u32 vendor, u32 product, const u8 *mac) {
    if (usb_index < count) return;
    u8 nonzero = 0;
    for (u32 i = 0; i < 6; ++i) nonzero |= mac[i];
    if (!nonzero || (mac[0] & 1)) return;
    u32 index = NV_NET_MAX;
    for (u32 i = 0; i < count; ++i)
        if (adapters[i].type == NV_NET_USB_BRIDGE && adapters[i].state == NV_NET_DOWN) {
            index = i; break;
        }
    if (index == NV_NET_MAX) {
        if (count == NV_NET_MAX) return;
        index = count++;
    }
    struct nv_net_info *n = &adapters[index];
    memset(n, 0, sizeof(*n));
    n->index = index;
    n->type = NV_NET_USB_BRIDGE;
    n->state = NV_NET_DOWN;
    n->vendor = vendor; n->product = product;
    memcpy(n->mac, mac, 6);
    usb_index = index;
    if (active == NV_NET_MAX) active = usb_index;
    kprintf("[net] USB Ethernet interface attached\n");
}
void net_usb_detach(void) {
    if (usb_index >= count) return;
    adapters[usb_index].state = NV_NET_DOWN;
    adapters[usb_index].ip = adapters[usb_index].mask =
        adapters[usb_index].gateway = adapters[usb_index].dns = 0;
    if (active == usb_index) {
        active = wired;
        reset_protocol_state();
    }
    usb_index = NV_NET_MAX;
}
static bool is_active(void) {
    if (active >= count) return false;
    if (active == usb_index) return usb_ecm_link();
    return wired_driver == WIRED_E1000 ? net_e1000_link() : net_igc_link();
}
static int wired_send(const void *data, u32 size) {
    return wired_driver == WIRED_E1000 ? net_e1000_send(data, size) :
                                         net_igc_send(data, size);
}
static void wired_poll(void (*receive)(const void *, u32)) {
    if (wired_driver == WIRED_E1000) net_e1000_poll(receive);
    else net_igc_poll(receive);
}
static int send_frame(const u8 *dest, u16 type, const void *data, u32 length) {
    if (!is_active()) return -NV_ENODEV;
    if (length > 1500) return -NV_E2BIG;
    struct nv_net_info *n = &adapters[active];
    memcpy(frame, dest, 6);
    memcpy(frame + 6, n->mac, 6);
    put16(frame + 12, type);
    memcpy(frame + 14, data, length);
    if (length + 14 < 60) memset(frame + 14 + length, 0, 60 - length - 14);
    int result = active == usb_index ? usb_ecm_send(frame, MAX(60u, length + 14)) :
                                      wired_send(frame, MAX(60u, length + 14));
    if (!result) ++n->tx_packets;
    return result;
}
static const u8 broadcast[6] = {255, 255, 255, 255, 255, 255};
static void arp_request(u32 target) {
    u8 a[28] = {0};
    struct nv_net_info *n = &adapters[active];
    put16(a, 1); put16(a + 2, 0x0800);
    a[4] = 6; a[5] = 4; put16(a + 6, 1);
    memcpy(a + 8, n->mac, 6); put32(a + 14, n->ip);
    put32(a + 24, target);
    send_frame(broadcast, 0x0806, a, sizeof(a));
}
static u32 route(u32 address) {
    struct nv_net_info *n = &adapters[active];
    return (address & n->mask) == (n->ip & n->mask) ? address : n->gateway;
}
static int send_ipv4(const u8 *mac, u32 src, u32 dest, u8 protocol,
                     const u8 *payload, u32 length) {
    if (length + 20 > 1500) return -NV_E2BIG;
    u8 packet[1500] = {0};
    packet[0] = 0x45;
    put16(packet + 2, (u16)(length + 20));
    put16(packet + 6, 0x4000); /* never fragment */
    packet[8] = 64; packet[9] = protocol;
    put32(packet + 12, src); put32(packet + 16, dest);
    put16(packet + 10, checksum(packet, 20));
    memcpy(packet + 20, payload, length);
    return send_frame(mac, 0x0800, packet, length + 20);
}
static int udp_raw(const u8 *mac, u32 src, u32 dest, u16 source_port, u16 dest_port,
                   const u8 *data, u32 length) {
    if (length > 1472) return -NV_E2BIG;
    u8 payload[1480] = {0};
    put16(payload, source_port); put16(payload + 2, dest_port);
    put16(payload + 4, (u16)(length + 8));
    memcpy(payload + 8, data, length);
    /* IPv4 permits checksum 0 for UDP. The software checksum is computed
     * over the pseudoheader so real hosts can validate these packets. */
    u8 pseudo[12] = {0};
    put32(pseudo, src); put32(pseudo + 4, dest);
    pseudo[9] = 17; put16(pseudo + 10, (u16)(length + 8));
    u32 sum = (u16)~checksum(pseudo, 12);
    for (u32 i = 0; i < length + 8; i += 2)
        sum += (u16)payload[i] << 8 | (i + 1 < length + 8 ? payload[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    put16(payload + 6, (u16)~sum ? (u16)~sum : 0xffff);
    return send_ipv4(mac, src, dest, 17, payload, length + 8);
}
static int echo_request(u32 address, u32 identifier, u32 sequence) {
    u32 next = route(address);
    if (!next) return -NV_ENODEV;
    if (peer_ip != next || ticks >= peer_valid_until) {
        arp_request(next);
        return -NV_EAGAIN;
    }
    u8 packet[16] = {8, 0};
    put16(packet + 4, (u16)identifier);
    put16(packet + 6, (u16)sequence);
    put32(packet + 8, ticks);
    put32(packet + 12, address ^ ticks);
    put16(packet + 2, checksum(packet, sizeof(packet)));
    return send_ipv4(peer_mac, adapters[active].ip, address, 1, packet, sizeof(packet));
}
static int tcp_packet(u32 sequence, u8 flags, const u8 *body, u32 length) {
    if (length > NV_NET_DATA_MAX || active != stream.index ||
        adapters[active].ip != stream.source_ip) return -NV_ENODEV;
    u32 next = route(stream.address);
    if (!next) return -NV_ENODEV;
    if (peer_ip != next || ticks >= peer_valid_until) {
        arp_request(next);
        return -NV_EAGAIN;
    }
    u8 packet[20 + NV_NET_DATA_MAX] = {0};
    put16(packet, stream.local_port);
    put16(packet + 2, stream.remote_port);
    put32(packet + 4, sequence);
    put32(packet + 8, stream.next_rx);
    packet[12] = 5 << 4;
    packet[13] = flags;
    put16(packet + 14, stream.eof ? 0 : (u16)(NV_NET_DATA_MAX - stream.used));
    if (length) memcpy(packet + 20, body, length);
    u8 pseudo[12] = {0};
    put32(pseudo, stream.source_ip);
    put32(pseudo + 4, stream.address);
    pseudo[9] = 6;
    put16(pseudo + 10, (u16)(length + 20));
    u32 sum = (u16)~checksum(pseudo, sizeof(pseudo));
    for (u32 i = 0; i < length + 20; i += 2)
        sum += (u16)packet[i] << 8 | (i + 1 < length + 20 ? packet[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    put16(packet + 16, (u16)~sum);
    int r = send_ipv4(peer_mac, stream.source_ip, stream.address, 6, packet, length + 20);
    if (r >= 0 && (flags & 0x10)) stream.ack_pending = false;
    return r;
}
/* Keep only the latest cumulative ACK/window; a busy TX ring or unresolved
 * neighbour must not discard a handshake ACK or a receive-window update. */
static void tcp_ack(void) {
    stream.ack_last_send = ticks;
    stream.ack_pending = tcp_packet(stream.next_tx, 0x10, NULL, 0) == -NV_EAGAIN;
}
static bool tcp_checksum_valid(const u8 *packet, u32 length, u32 source, u32 dest) {
    u8 pseudo[12] = {0};
    put32(pseudo, source);
    put32(pseudo + 4, dest);
    pseudo[9] = 6;
    put16(pseudo + 10, (u16)length);
    u32 sum = (u16)~checksum(pseudo, sizeof(pseudo));
    for (u32 i = 0; i < length; i += 2)
        sum += (u16)packet[i] << 8 | (i + 1 < length ? packet[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return sum == 0xffff;
}
static void tcp_input(const u8 *packet, u32 size, u32 source, u32 dest) {
    if (stream.state != TCP_SYN_SENT && stream.state != TCP_ESTABLISHED) return;
    if (source != stream.address || dest != stream.source_ip ||
        active != stream.index || size < 20 ||
        be16(packet) != stream.remote_port || be16(packet + 2) != stream.local_port ||
        !tcp_checksum_valid(packet, size, source, dest)) return;
    u32 header = (packet[12] >> 4) * 4u;
    if (header < 20 || header > size) return;
    u8 flags = packet[13];
    u32 seq = be32(packet + 4), ack = be32(packet + 8);
    if (stream.state == TCP_SYN_SENT) {
        /* A reset must acknowledge our SYN. Check it before accepting SYN+ACK. */
        if (!(flags & 0x10) || ack != stream.next_tx) return;
        if (flags & 4) { stream.state = TCP_ERROR; stream.ack_pending = false; return; }
        if (flags & 2) {
            stream.next_rx = seq + 1;
            stream.acked_tx = ack;
            stream.state = TCP_ESTABLISHED;
            tcp_ack();
        }
        return;
    }
    if (flags & 4) {
        if (seq == stream.next_rx) { stream.state = TCP_ERROR; stream.ack_pending = false; }
        return;
    }
    if (!(flags & 0x10)) return;
    u32 length = size - header;
    u32 span = length + ((flags & 1) != 0);
    u32 window = stream.eof ? 0 : NV_NET_DATA_MAX - stream.used;
    /* Validate sequence space before an ACK can release queued output. Unsigned
     * offsets handle sequence wrap; FIN also occupies one receive position. */
    bool acceptable = !span ? (window ? seq - stream.next_rx < window :
                                       seq == stream.next_rx) :
        window && (seq - stream.next_rx < window ||
                   seq + span - 1 - stream.next_rx < window);
    if (!acceptable || (flags & 2) || (i32)(ack - stream.next_tx) > 0) {
        tcp_ack();
        return;
    }
    if ((i32)(ack - stream.acked_tx) >= 0 &&
        (i32)(stream.next_tx - ack) >= 0) {
        u32 progress = ack - stream.acked_tx;
        if (progress && progress <= stream.queued) {
            stream.queued -= progress;
            memmove(stream.outgoing, stream.outgoing + progress, stream.queued);
            stream.attempts = stream.queued ? 1 : 0;
            stream.last_send = ticks;
        }
        stream.acked_tx = ack;
    }
    u32 skip = 0;
    if ((i32)(seq - stream.next_rx) < 0)
        skip = MIN(length, stream.next_rx - seq);
    if (seq + skip != stream.next_rx) {
        tcp_ack();
        return;
    }
    u32 accepted = MIN(length - skip, window);
    if (accepted) {
        memcpy(stream.buffer + stream.used, packet + header + skip, accepted);
        stream.used += accepted;
        stream.next_rx += accepted;
    }
    if ((flags & 1) && skip + accepted == length && accepted < window) {
        ++stream.next_rx;
        stream.eof = true;
    }
    if (length || (flags & 1)) tcp_ack();
}
static void tcp_retry(void) {
    if (stream.state != TCP_SYN_SENT && stream.state != TCP_ESTABLISHED) return;
    bool queued = stream.state == TCP_SYN_SENT || stream.acked_tx != stream.next_tx;
    if (queued && ticks - stream.last_send >= 100) {
        if (stream.attempts >= 5) {
            stream.state = TCP_ERROR; stream.ack_pending = false; return;
        }
        u32 seq = stream.state == TCP_SYN_SENT ? stream.next_tx - 1 : stream.acked_tx;
        u32 length = stream.state == TCP_SYN_SENT ? 0 : stream.queued;
        /* A due data retransmission already carries the current ACK/window.
         * Make at most one ACK attempt this poll, even if DMA is still busy. */
        if (stream.state == TCP_ESTABLISHED) stream.ack_last_send = ticks;
        int r = tcp_packet(seq, stream.state == TCP_SYN_SENT ? 2 : 0x18,
                           stream.outgoing, length);
        if (r == -NV_EAGAIN) { stream.last_send = ticks; return; }
        if (r < 0) { stream.state = TCP_ERROR; stream.ack_pending = false; return; }
        ++stream.attempts;
        stream.last_send = ticks;
        return;
    }
    if (stream.state == TCP_ESTABLISHED && stream.ack_pending &&
        ticks != stream.ack_last_send) tcp_ack();
}
static void dhcp_send(bool request) {
    if (!is_active()) return;
    u8 message[300] = {0};
    struct nv_net_info *n = &adapters[active];
    message[0] = 1; message[1] = 1; message[2] = 6;
    put32(message + 4, dhcp_xid);
    put16(message + 10, 0x8000);
    memcpy(message + 28, n->mac, 6);
    put32(message + 236, 0x63825363);
    u32 pos = 240;
    message[pos++] = 53; message[pos++] = 1; message[pos++] = request ? 3 : 1;
    message[pos++] = 61; message[pos++] = 7; message[pos++] = 1;
    memcpy(message + pos, n->mac, 6); pos += 6;
    if (request) {
        message[pos++] = 50; message[pos++] = 4; put32(message + pos, offered); pos += 4;
        message[pos++] = 54; message[pos++] = 4; put32(message + pos, server); pos += 4;
    }
    message[pos++] = 55; message[pos++] = 4;
    message[pos++] = 1; message[pos++] = 3; message[pos++] = 6; message[pos++] = 51;
    message[pos++] = 255;
    udp_raw(broadcast, 0, 0xffffffffu, 68, 67, message, MAX(pos, 300u));
    last_dhcp = ticks;
}
static void dhcp_receive(const u8 *p, u32 size) {
    if (!lease_state || size < 240 || p[0] != 2 || p[1] != 1 || p[2] != 6 ||
        be32(p + 4) != dhcp_xid || be32(p + 236) != 0x63825363 ||
        memcmp(p + 28, adapters[active].mac, 6)) return;
    u32 mask = 0, gateway = 0, dns = 0, from = 0;
    u8 message_type = 0;
    for (u32 pos = 240; pos < size;) {
        u8 kind = p[pos++];
        if (kind == 255) break;
        if (kind == 0) continue;
        if (pos == size || p[pos] > size - pos - 1) return;
        u32 len = p[pos++];
        if (kind == 53 && len == 1) message_type = p[pos];
        if (kind == 1 && len == 4) mask = be32(p + pos);
        if (kind == 3 && len >= 4) gateway = be32(p + pos);
        if (kind == 6 && len >= 4) dns = be32(p + pos);
        if (kind == 54 && len == 4) from = be32(p + pos);
        pos += len;
    }
    if (lease_state == 1 && message_type == 2 && from && be32(p + 16)) {
        offered = be32(p + 16); server = from;
        lease_state = 2; dhcp_send(true);
    } else if (lease_state == 2 && message_type == 5 && from == server &&
               be32(p + 16) == offered && mask) {
        struct nv_net_info *n = &adapters[active];
        n->ip = offered; n->mask = mask; n->gateway = gateway; n->dns = dns;
        n->state = NV_NET_ONLINE; lease_state = 0;
        kprintf("[net] DHCP address %u.%u.%u.%u\n", (offered >> 24) & 255,
                (offered >> 16) & 255, (offered >> 8) & 255, offered & 255);
    } else if (lease_state == 2 && message_type == 6) {
        lease_state = 1; dhcp_send(false);
    }
}
static void input_arp(const u8 *p, u32 size) {
    if (size < 28 || be16(p) != 1 || be16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4)
        return;
    struct nv_net_info *n = &adapters[active];
    u16 operation = be16(p + 6);
    u32 sender = be32(p + 14);
    if (sender && !(p[8] & 1) && (sender == route(n->ip) || sender == n->gateway ||
                                  (sender & n->mask) == (n->ip & n->mask))) {
        peer_ip = sender; memcpy(peer_mac, p + 8, 6);
        peer_valid_until = ticks + 6000;
    }
    if (operation == 1 && n->ip && be32(p + 24) == n->ip) {
        u8 reply[28];
        memcpy(reply, p, sizeof(reply));
        put16(reply + 6, 2);
        memcpy(reply + 18, p + 8, 6);
        put32(reply + 24, sender);
        memcpy(reply + 8, n->mac, 6);
        put32(reply + 14, n->ip);
        send_frame(p + 8, 0x0806, reply, sizeof(reply));
    }
}
static void input_ipv4(const u8 *p, u32 size) {
    if (size < 20 || p[0] >> 4 != 4) return;
    u32 hdr = (p[0] & 15) * 4, total = be16(p + 2);
    if (hdr < 20 || hdr > size || total < hdr || total > size ||
        checksum(p, hdr) || (be16(p + 6) & 0x3fffu)) return;
    struct nv_net_info *n = &adapters[active];
    u32 dest = be32(p + 16), src = be32(p + 12);
    if (dest != 0xffffffffu && dest != n->ip) return;
    const u8 *data = p + hdr; u32 bytes = total - hdr;
    if (p[9] == 17 && bytes >= 8 && be16(data + 4) >= 8 && be16(data + 4) <= bytes &&
        udp_checksum_valid(data, be16(data + 4), src, dest)) {
        u32 length = be16(data + 4) - 8;
        if (be16(data + 2) == 68 && be16(data) == 67)
            dhcp_receive(data + 8, length);
        else if (n->ip && length <= NV_NET_DATA_MAX && !inbox_full) {
            inbox = (struct nv_net_udp){.index = active, .address = src,
                    .port = be16(data), .local_port = be16(data + 2), .length = length};
            memcpy(inbox.data, data + 8, length);
            inbox_full = true;
        }
    } else if (p[9] == 6 && n->ip) {
        tcp_input(data, bytes, src, dest);
    } else if (p[9] == 1 && n->ip && bytes >= 8 && data[0] == 0 &&
               checksum(data, bytes) == 0) {
        if (echo_probe.pending && src == echo_probe.address &&
            be16(data + 4) == (u16)echo_probe.identifier &&
            be16(data + 6) == (u16)echo_probe.sequence) {
            echo_probe.ttl = p[8];
            echo_probe.bytes = bytes - 8;
            echo_probe.replied = true;
        }
    } else if (p[9] == 1 && n->ip && bytes >= 8 && data[0] == 8 &&
               checksum(data, bytes) == 0) {
        u32 next = route(src);
        if (!next || peer_ip != next || ticks >= peer_valid_until) {
            arp_request(next); return;
        }
        u8 echo[1480];
        if (bytes > sizeof(echo)) return;
        memcpy(echo, data, bytes);
        echo[0] = 0; echo[2] = echo[3] = 0;
        put16(echo + 2, checksum(echo, bytes));
        send_ipv4(peer_mac, n->ip, src, 1, echo, bytes);
    }
}
static void receive(const void *buffer, u32 size) {
    if (size < 14 || active >= count) return;
    const u8 *p = buffer;
    struct nv_net_info *n = &adapters[active];
    if (memcmp(p, n->mac, 6) && memcmp(p, broadcast, 6)) return;
    ++n->rx_packets;
    if (be16(p + 12) == 0x0806) input_arp(p + 14, size - 14);
    if (be16(p + 12) == 0x0800) input_ipv4(p + 14, size - 14);
}
void net_usb_receive(const void *buffer, u32 size) {
    if (active == usb_index && usb_index < count) receive(buffer, size);
}
void net_poll(void) {
    if (polling || active >= count) return;
    polling = true;
    struct nv_net_info *n = &adapters[active];
    bool link = is_active();
    if (!link) {
        n->ip = n->mask = n->gateway = n->dns = 0;
        n->state = NV_NET_DOWN;
        reset_protocol_state();
    } else {
        if (n->state == NV_NET_DOWN) n->state = NV_NET_LINK;
        if (active == wired) wired_poll(receive);
        if (lease_state && ticks - last_dhcp >= 400)
            dhcp_send(lease_state == 2);
        tcp_retry();
    }
    polling = false;
}
void net_task_release(u32 pid) {
    if (stream.state != TCP_IDLE && stream.owner == pid) {
        stream.state = TCP_IDLE; stream.ack_pending = false;
    }
}
int net_ioctl(u32 op, uptr pointer) {
    if (op >= NV_NET_TCP_OPEN && op <= NV_NET_TCP_CLOSE) {
        if (!user_range(current->pd, pointer, sizeof(struct nv_net_tcp),
                        op == NV_NET_TCP_OPEN || op == NV_NET_TCP_RECV)) return -NV_EFAULT;
        struct nv_net_tcp io;
        memcpy(&io, (const void *)(uptr)pointer, sizeof(io));
        if (op == NV_NET_TCP_CLOSE) {
            if (stream.state == TCP_IDLE || stream.owner != current->pid) return -NV_EACCESS;
            if (stream.state == TCP_ESTABLISHED)
                tcp_packet(stream.next_tx, stream.acked_tx == stream.next_tx ? 0x11 : 0x14,
                           NULL, 0);
            stream.state = TCP_IDLE; stream.ack_pending = false;
            return 0;
        }
        if (op == NV_NET_TCP_OPEN && stream.state == TCP_IDLE) {
            if (io.index != active || active >= count || !io.address ||
                !io.port || io.port > 65535 ||
                adapters[active].state != NV_NET_ONLINE || !is_active()) return -NV_ENODEV;
            memset(&stream, 0, sizeof(stream));
            stream.owner = current->pid;
            stream.index = active;
            stream.address = io.address;
            stream.source_ip = adapters[active].ip;
            stream.remote_port = (u16)io.port;
            stream.local_port = (u16)(49152u + (ticks % 16000u));
            stream.next_tx = 0x4e560000u ^ ticks ^ io.address ^ ((u32)stream.local_port << 16);
            stream.acked_tx = stream.next_tx;
            ++stream.next_tx; /* SYN consumes one sequence number. */
            stream.last_send = ticks - 100;
            stream.state = TCP_SYN_SENT;
            tcp_retry();
        }
        if (stream.owner != current->pid || io.index != stream.index ||
            io.address != stream.address || io.port != stream.remote_port)
            return -NV_EBUSY;
        if (stream.state == TCP_ERROR) {
            stream.state = TCP_IDLE; stream.ack_pending = false; return -NV_EIO;
        }
        io.local_port = stream.local_port;
        if (op == NV_NET_TCP_OPEN) {
            memcpy((void *)(uptr)pointer, &io, sizeof(io));
            return stream.state == TCP_ESTABLISHED ? 1 : 0;
        }
        if (stream.state != TCP_ESTABLISHED) return -NV_EAGAIN;
        if (op == NV_NET_TCP_RECV) {
            if (!io.length || io.length > sizeof(io.data)) return -NV_EINVAL;
            if (!stream.used) return stream.eof ? 0 : -NV_EAGAIN;
            u32 got = MIN(io.length, stream.used);
            memcpy(io.data, stream.buffer, got);
            stream.used -= got;
            memmove(stream.buffer, stream.buffer + got, stream.used);
            io.length = got;
            memcpy((void *)(uptr)pointer, &io, sizeof(io));
            tcp_ack(); /* reopen the receive window */
            return (int)got;
        }
        if (!io.length || io.length > sizeof(io.data)) return -NV_EINVAL;
        if (stream.acked_tx != stream.next_tx) return -NV_EAGAIN;
        int r = tcp_packet(stream.next_tx, 0x18, io.data, io.length);
        if (r < 0) return r;
        memcpy(stream.outgoing, io.data, io.length);
        stream.queued = io.length;
        stream.next_tx += io.length;
        stream.attempts = 1;
        stream.last_send = ticks;
        return (int)io.length;
    }
    if (op == NV_NET_PING) {
        if (!user_range(current->pd, pointer, sizeof(struct nv_net_ping), true))
            return -NV_EFAULT;
        struct nv_net_ping io;
        memcpy(&io, (const void *)(uptr)pointer, sizeof(io));
        if (io.index != active || active >= count || !io.address ||
            adapters[active].state != NV_NET_ONLINE || !is_active()) return -NV_ENODEV;
        if (echo_probe.pending) {
            if (echo_probe.address != io.address ||
                (u16)echo_probe.identifier != (u16)io.identifier ||
                (u16)echo_probe.sequence != (u16)io.sequence) return -NV_EBUSY;
            if (echo_probe.replied) {
                io.reply_ttl = echo_probe.ttl;
                io.reply_bytes = echo_probe.bytes;
                echo_probe.pending = false;
                memcpy((void *)(uptr)pointer, &io, sizeof(io));
                return 1;
            }
            if (ticks - echo_probe.started >= 200) {
                echo_probe.pending = false;
                return -NV_EIO;
            }
            return 0;
        }
        int r = echo_request(io.address, io.identifier, io.sequence);
        if (r < 0) return r;
        echo_probe.address = io.address;
        echo_probe.identifier = io.identifier;
        echo_probe.sequence = io.sequence;
        echo_probe.started = ticks;
        echo_probe.replied = false;
        echo_probe.pending = true;
        return 0;
    }
    if (op == NV_NET_WIFI_COMMAND || op == NV_NET_WIFI_READ) {
        if (!user_range(current->pd, pointer, sizeof(struct nv_net_wifi_command), true))
            return -NV_EFAULT;
        struct nv_net_wifi_command *user = (void *)(uptr)pointer;
        if (op == NV_NET_WIFI_COMMAND) {
            struct nv_net_wifi_command item;
            memcpy(&item, user, sizeof(item));
            if (item.length > sizeof(item.text)) return -NV_EINVAL;
            int status = usb_wifi_command(item.text, item.length);
            memset(&item, 0, sizeof(item));
            return status;
        }
        struct nv_net_wifi_command item = {0};
        int size = usb_wifi_read(item.text, sizeof(item.text));
        if (size < 0) return size;
        item.length = (u32)size;
        memcpy(user, &item, sizeof(item));
        return size;
    }
    if (op == NV_NET_INFO) {
        if (!user_range(current->pd, pointer, sizeof(struct nv_net_info), true)) return -NV_EFAULT;
        u32 index = ((const struct nv_net_info *)(uptr)pointer)->index;
        if (index >= NV_NET_MAX) return -NV_EINVAL;
        if (index >= count) return 0;
        struct nv_net_info result = adapters[index];
        result.reserved[0] = index == active;
        memcpy((void *)(uptr)pointer, &result, sizeof(result));
        return 1;
    }
    if (op == NV_NET_DHCP || op == NV_NET_STATIC || op == NV_NET_SELECT) {
        if (!user_range(current->pd, pointer, sizeof(struct nv_net_static),
                        op == NV_NET_STATIC)) return -NV_EFAULT;
        struct nv_net_static config;
        memcpy(&config, (void *)(uptr)pointer, sizeof(config));
        if (op == NV_NET_SELECT) {
            if (config.index >= count || (config.index != wired && config.index != usb_index))
                return -NV_ENODEV;
            if (active != config.index) {
                active = config.index;
                reset_protocol_state();
            }
            return 0;
        }
        if (config.index != active || !is_active()) return -NV_ENODEV;
        struct nv_net_info *n = &adapters[active];
        if (op == NV_NET_DHCP) {
            reset_protocol_state();
            n->ip = n->mask = n->gateway = n->dns = 0;
            n->state = NV_NET_CONFIGURING;
            dhcp_xid = 0x4e560000u ^ ticks ^ (u32)n->mac[5] << 8 ^ n->product;
            lease_state = 1; dhcp_send(false);
        } else {
            u32 host = ~config.mask;
            if (!config.ip || !config.mask || (host & (host + 1u)) != 0 ||
                (config.gateway && (config.gateway & config.mask) !=
                                   (config.ip & config.mask))) return -NV_EINVAL;
            reset_protocol_state();
            n->ip = config.ip; n->mask = config.mask;
            n->gateway = config.gateway; n->dns = config.dns;
            n->state = NV_NET_ONLINE; lease_state = 0;
            arp_request(n->ip); /* announce source address */
        }
        return 0;
    }
    if (op == NV_NET_UDP_SEND || op == NV_NET_UDP_RECV) {
        if (!user_range(current->pd, pointer, sizeof(struct nv_net_udp), true)) return -NV_EFAULT;
        struct nv_net_udp *io = (void *)(uptr)pointer;
        if (op == NV_NET_UDP_RECV) {
            u32 filter = io->local_port;
            if (!inbox_full || (filter && inbox.local_port != filter)) return -NV_EAGAIN;
            memcpy(io, &inbox, sizeof(inbox)); inbox_full = false;
            return (int)inbox.length;
        }
        struct nv_net_udp item;
        memcpy(&item, io, sizeof(item));
        if (item.index != active || active >= count ||
            adapters[active].state != NV_NET_ONLINE || !is_active()) return -NV_ENODEV;
        if (!item.port || !item.local_port || item.port > 65535 || item.local_port > 65535 ||
            !item.address || item.length > NV_NET_DATA_MAX) return -NV_EINVAL;
        u32 next = route(item.address);
        if (!next) return -NV_ENODEV;
        if (peer_ip != next || ticks >= peer_valid_until) {
            arp_request(next);
            return -NV_EAGAIN;
        }
        return udp_raw(peer_mac, adapters[active].ip, item.address,
                       (u16)item.local_port, (u16)item.port, item.data, item.length);
    }
    return -NV_EINVAL;
}
