static void show_ipv4(u32 address) {
    for (u32 i = 0; i < 4; ++i) {
        if (i) print(".");
        print_u32((address >> (24 - i * 8)) & 255);
    }
}
static int read_ipv4(const char *s, u32 *out) {
    u32 result = 0;
    for (u32 i = 0; i < 4; ++i) {
        u32 octet = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (++digits > 3) return -NV_EINVAL;
            octet = octet * 10 + (u32)(*s++ - '0');
        }
        if (!digits || octet > 255) return -NV_EINVAL;
        result = (result << 8) | octet;
        if (i < 3 && *s++ != '.') return -NV_EINVAL;
    }
    if (*s) return -NV_EINVAL;
    *out = result;
    return 0;
}
static int net_index(const char *text, u32 *index) {
    if (parse_u32(text, index) < 0 || *index >= NV_NET_MAX) return -NV_EINVAL;
    return 0;
}
static int net_current(u32 *index) {
    for (u32 i = 0; i < NV_NET_MAX; ++i) {
        struct nv_net_info n = {.index = i};
        int r = devctl(NV_SUB_NET, NV_NET_INFO, &n);
        if (r < 0) return r;
        if (r && n.reserved[0]) { *index = i; return 0; }
    }
    return -NV_ENODEV;
}
static int net_list(void) {
    u32 devices = 0;
    for (u32 i = 0; i < NV_NET_MAX; ++i) {
        struct nv_net_info n = {.index = i};
        int r = devctl(NV_SUB_NET, NV_NET_INFO, &n);
        if (r < 0) return r;
        if (!r) continue;
        ++devices;
        print(n.reserved[0] ? "* " : "  ");
        print_u32(i);
        print(n.type == NV_NET_WIFI ? " Wi-Fi PCI " :
              n.type == NV_NET_USB_BRIDGE && n.vendor == 0x303a ? " USB Wi-Fi bridge " :
              n.type == NV_NET_USB_BRIDGE ? " USB Ethernet bridge " : " wired PCI ");
        print_hex(n.vendor); print(":"); print_hex(n.product);
        print("  ");
        println(n.state == NV_NET_UNSUPPORTED ? "no driver" :
                n.state == NV_NET_DOWN ? "link down" :
                n.state == NV_NET_LINK ? "link ready" :
                n.state == NV_NET_CONFIGURING ? "DHCP waiting" : "IPv4 configured");
        if (n.state != NV_NET_UNSUPPORTED) {
            print("    MAC ");
            for (u32 j = 0; j < 6; ++j) {
                if (j) print(":");
                char pair[3] = {"0123456789abcdef"[n.mac[j] >> 4],
                                "0123456789abcdef"[n.mac[j] & 15], 0};
                print(pair);
            }
            print("  IP "); show_ipv4(n.ip);
            print("  gateway "); show_ipv4(n.gateway);
            print("  RX "); print_u32(n.rx_packets);
            print(" TX "); print_u32(n.tx_packets);
            print("\n");
        }
    }
    if (!devices) println("No PCI network device or configured USB CDC-ECM adapter found.");
    println("* active interface. Supported PCI wired: Intel I225-V/LM, I226-V/LM.");
    println("Supported ESP USB Wi-Fi Dongle: net wifi scan, net wifi join SSID.");
    println("Built-in PCI Wi-Fi has no driver; detection does not mean connection.");
    return 0;
}
static int read_wifi_password(char *password, u32 capacity) {
    u32 size = 0;
    print("Wi-Fi password (hidden): ");
    for (;;) {
        char c;
        int r = take(0, &c, 1);
        if (r == -NV_EAGAIN) { nap(10); continue; }
        if (r < 0) return r;
        if (!r) continue;
        if (c == '\r') continue;
        if (c == '\n') {
            print("\n"); password[size] = 0;
            return size >= 8 && size <= 63 ? 0 : -NV_EINVAL;
        }
        if (c == '\b' || c == 127) {
            if (size) --size;
        } else if (c >= 33 && c <= 126 && size + 1 < capacity)
            password[size++] = c;
    }
}
static bool has_text(const char *buffer, u32 length, const char *needle) {
    u32 size = strlen(needle);
    for (u32 pos = 0; pos + size <= length; ++pos)
        if (!memcmp(buffer + pos, needle, size)) return true;
    return false;
}
static int wifi_send(const char *message, u32 length) {
    struct nv_net_wifi_command io = {0};
    if (length > sizeof(io.text)) return -NV_E2BIG;
    memcpy(io.text, message, length);
    io.length = length;
    int result = devctl(NV_SUB_NET, NV_NET_WIFI_COMMAND, &io);
    memset(&io, 0, sizeof(io));
    return result;
}
static int wifi_read_response(u32 duration, bool print_response, bool *connected) {
    char response[1024] = {0};
    u32 size = 0;
    for (u32 i = 0; i < duration; ++i) {
        struct nv_net_wifi_command io = {0};
        int got = devctl(NV_SUB_NET, NV_NET_WIFI_READ, &io);
        if (got < 0) return got;
        if (got) {
            if (print_response) emit(1, io.text, io.length);
            u32 copy = MIN(io.length, sizeof(response) - size);
            memcpy(response + size, io.text, copy); size += copy;
            if (connected && (has_text(response, size, "connect success") ||
                              has_text(response, size, "Wi-Fi STA connected"))) {
                *connected = true;
                return 0;
            }
            if (connected && (has_text(response, size, "connect fail") ||
                              has_text(response, size, "ERROR"))) return -NV_EIO;
            if (size > sizeof(response) - 252) {
                memmove(response, response + size - 252, 252);
                size = 252;
            }
        }
        nap(100);
    }
    return 0;
}
static int wifi_command(int n, char **v) {
    if (n == 3 && !strcmp(v[2], "scan")) {
        int r = wifi_send("scan\n", 5);
        return r < 0 ? r : wifi_read_response(50, true, NULL);
    }
    if (n == 3 && !strcmp(v[2], "status")) {
        int r = wifi_send("sta\n", 4);
        return r < 0 ? r : wifi_read_response(15, true, NULL);
    }
    if ((n == 4 || n == 5) && !strcmp(v[2], "join")) {
        u32 ssid = strlen(v[3]);
        if (!ssid || ssid > 32) return -NV_EINVAL;
        for (u32 j = 0; j < ssid; ++j)
            if (v[3][j] < 33 || v[3][j] > 126) return -NV_EINVAL;
        bool open = n == 5 && !strcmp(v[4], "--open");
        if (n == 5 && !open) return -NV_EINVAL;
        char secret[65] = {0};
        int r = 0;
        if (!open && (r = read_wifi_password(secret, sizeof(secret))) < 0) {
            memset(secret, 0, sizeof(secret)); return r;
        }
        char command[128];
        u32 pos = 0;
        memcpy(command + pos, "sta -s ", 7); pos += 7;
        memcpy(command + pos, v[3], ssid); pos += ssid;
        if (!open) {
            memcpy(command + pos, " -p ", 4); pos += 4;
            u32 length = strlen(secret);
            memcpy(command + pos, secret, length); pos += length;
        }
        command[pos++] = '\n';
        r = wifi_send(command, pos);
        memset(secret, 0, sizeof(secret)); memset(command, 0, sizeof(command));
        if (r < 0) return r;
        println("Connecting to Wi-Fi...");
        bool connected = false;
        r = wifi_read_response(150, false, &connected);
        if (r < 0) return r;
        if (!connected) { println("Connection still pending; run net wifi status."); return -NV_EAGAIN; }
        for (u32 i = 0; i < NV_NET_MAX; ++i) {
            struct nv_net_info device = {.index = i};
            if (devctl(NV_SUB_NET, NV_NET_INFO, &device) <= 0) continue;
            if (device.type != NV_NET_USB_BRIDGE || device.vendor != 0x303a) continue;
            struct nv_net_static cfg = {.index = i};
            r = devctl(NV_SUB_NET, NV_NET_SELECT, &cfg);
            if (r < 0) return r;
            r = devctl(NV_SUB_NET, NV_NET_DHCP, &cfg);
            if (!r) println("Wi-Fi associated; DHCP started. Run net to see the IP address.");
            return r;
        }
        return -NV_ENODEV;
    }
    return -NV_EINVAL;
}
static int network_command(int n, char **v) {
    if (n == 1) return net_list();
    if (!strcmp(v[1], "wifi")) return wifi_command(n, v);
    if (!strcmp(v[1], "use") && n == 3) {
        struct nv_net_static cfg = {0};
        if (net_index(v[2], &cfg.index) < 0) return -NV_EINVAL;
        return devctl(NV_SUB_NET, NV_NET_SELECT, &cfg);
    }
    if (!strcmp(v[1], "dhcp") && (n == 2 || n == 3)) {
        struct nv_net_static cfg = {0};
        int r = n == 3 ? net_index(v[2], &cfg.index) : net_current(&cfg.index);
        if (r < 0) return r;
        if (n == 3 && (r = devctl(NV_SUB_NET, NV_NET_SELECT, &cfg)) < 0) return r;
        r = devctl(NV_SUB_NET, NV_NET_DHCP, &cfg);
        if (!r) println("DHCP started; run net again to check the address.");
        return r;
    }
    if (!strcmp(v[1], "static") && (n == 6 || n == 7)) {
        struct nv_net_static cfg = {0};
        if (net_index(v[2], &cfg.index) < 0 || read_ipv4(v[3], &cfg.ip) < 0 ||
            read_ipv4(v[4], &cfg.mask) < 0 || read_ipv4(v[5], &cfg.gateway) < 0 ||
            (n == 7 && read_ipv4(v[6], &cfg.dns) < 0)) return -NV_EINVAL;
        int r = devctl(NV_SUB_NET, NV_NET_SELECT, &cfg);
        return r < 0 ? r : devctl(NV_SUB_NET, NV_NET_STATIC, &cfg);
    }
    if (!strcmp(v[1], "send") && n >= 5) {
        struct nv_net_udp packet = {.local_port = 40000};
        if (net_current(&packet.index) < 0 || read_ipv4(v[2], &packet.address) < 0 ||
            parse_u32(v[3], &packet.port) < 0 || !packet.port || packet.port > 65535)
            return -NV_EINVAL;
        char data[NV_NET_DATA_MAX];
        if (join_args(data, sizeof(data), v, 4, n) < 0) return -NV_E2BIG;
        packet.length = strlen(data);
        memcpy(packet.data, data, packet.length);
        for (u32 attempt = 0; attempt < 20; ++attempt) {
            int r = devctl(NV_SUB_NET, NV_NET_UDP_SEND, &packet);
            if (r != -NV_EAGAIN) return r;
            nap(100);
        }
        return -NV_EAGAIN;
    }
    if (!strcmp(v[1], "recv") && n == 3) {
        struct nv_net_udp packet = {0};
        if (parse_u32(v[2], &packet.local_port) < 0 || !packet.local_port ||
            packet.local_port > 65535) return -NV_EINVAL;
        for (u32 attempt = 0; attempt < 50; ++attempt) {
            int r = devctl(NV_SUB_NET, NV_NET_UDP_RECV, &packet);
            if (r == -NV_EAGAIN) { nap(100); continue; }
            if (r < 0) return r;
            print("from "); show_ipv4(packet.address); print(":");
            print_u32(packet.port); print("  ");
            emit(1, packet.data, packet.length); print("\n");
            return 0;
        }
        return -NV_EAGAIN;
    }
    return -NV_EINVAL;
}
