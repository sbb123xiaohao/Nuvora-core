/* Bounded DNS, ICMP and HTTP clients on the native IPv4 drivers. */
static struct nv_net_udp dns_io;
static struct nv_net_tcp tcp_io;
static char http_headers[4096];

static int active_network(struct nv_net_info *out) {
    u32 index = 0;
    int r = net_current(&index);
    if (r < 0) return r;
    *out = (struct nv_net_info){.index = index};
    r = devctl(NV_SUB_NET, NV_NET_INFO, out);
    if (r <= 0) return -NV_ENODEV;
    if (out->state == NV_NET_ONLINE) return 0;
    if (out->state == NV_NET_DOWN || out->state == NV_NET_LINK ||
        out->state == NV_NET_CONFIGURING) {
        u32 start = clock_ticks();
        bool requested = out->state == NV_NET_CONFIGURING;
        while (clock_ticks() - start < 1000) {
            if (!requested && out->state == NV_NET_LINK) {
                struct nv_net_static config = {.index = index};
                r = devctl(NV_SUB_NET, NV_NET_DHCP, &config);
                if (r < 0) return r;
                requested = true;
            }
            nap(50);
            *out = (struct nv_net_info){.index = index};
            r = devctl(NV_SUB_NET, NV_NET_INFO, out);
            if (r <= 0) return -NV_ENODEV;
            if (out->state == NV_NET_ONLINE) return 0;
            if (out->state == NV_NET_DOWN && requested) break;
        }
    }
    println("Network has no IPv4 address. Run net to check the link and DHCP.");
    return -NV_ENODEV;
}
static u16 dns_u16(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static u32 dns_u32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 dns_skip(const u8 *p, u32 n, u32 at) {
    for (u32 labels = 0; at < n && labels < 128; ++labels) {
        u8 length = p[at++];
        if (!length) return at;
        if ((length & 0xc0) == 0xc0) return at < n ? at + 1 : 0;
        if ((length & 0xc0) || length > n - at) return 0;
        at += length;
    }
    return 0;
}
static int resolve_host(const char *host, const struct nv_net_info *network, u32 *out) {
    if (read_ipv4(host, out) == 0) return 0;
    if (!network->dns) return -NV_ENODEV;
    u32 len = strlen(host);
    if (!len || len > 253) return -NV_EINVAL;
    memset(&dns_io, 0, sizeof(dns_io));
    dns_io.index = network->index;
    dns_io.address = network->dns;
    dns_io.local_port = 49152 + (clock_ticks() % 14000);
    dns_io.port = 53;
    u16 id = (u16)(clock_ticks() ^ len ^ 0x4e56);
    dns_io.data[0] = (u8)(id >> 8); dns_io.data[1] = (u8)id;
    dns_io.data[2] = 1; dns_io.data[5] = 1;
    u32 pos = 12, start = 0;
    for (u32 i = 0; i <= len; ++i) {
        if (host[i] != '.' && host[i]) {
            if ((u8)host[i] <= 32 || (u8)host[i] >= 127) return -NV_EINVAL;
            continue;
        }
        u32 part = i - start;
        if (!part || part > 63 || pos + part + 6 >= sizeof(dns_io.data)) return -NV_EINVAL;
        dns_io.data[pos++] = (u8)part;
        memcpy(dns_io.data + pos, host + start, part);
        pos += part;
        start = i + 1;
    }
    dns_io.data[pos++] = 0; dns_io.data[pos++] = 0; dns_io.data[pos++] = 1;
    dns_io.data[pos++] = 0; dns_io.data[pos++] = 1;
    dns_io.length = pos;
    for (u32 attempt = 0; attempt < 3; ++attempt) {
        u32 started = clock_ticks();
        int sent;
        do {
            sent = devctl(NV_SUB_NET, NV_NET_UDP_SEND, &dns_io);
            if (sent != -NV_EAGAIN) break;
            nap(50);
        } while (clock_ticks() - started < 150);
        if (sent < 0 && sent != -NV_EAGAIN) return sent;
        if (sent == -NV_EAGAIN) continue;
        while (clock_ticks() - started < 300) {
            struct nv_net_udp response = {.index = network->index, .local_port = dns_io.local_port};
            int got = devctl(NV_SUB_NET, NV_NET_UDP_RECV, &response);
            if (got == -NV_EAGAIN) { nap(30); continue; }
            if (got < 0) return got;
            if (response.address != network->dns || response.port != 53 || got < 12 ||
                dns_u16(response.data) != id || !(response.data[2] & 0x80) ||
                (response.data[3] & 15)) continue;
            u32 at = 12;
            for (u32 q = 0; q < dns_u16(response.data + 4); ++q) {
                at = dns_skip(response.data, (u32)got, at);
                if (!at || at + 4 > (u32)got) return -NV_EIO;
                at += 4;
            }
            for (u32 a = 0; a < dns_u16(response.data + 6); ++a) {
                at = dns_skip(response.data, (u32)got, at);
                if (!at || at + 10 > (u32)got) return -NV_EIO;
                u16 kind = dns_u16(response.data + at), family = dns_u16(response.data + at + 2);
                u16 size = dns_u16(response.data + at + 8);
                at += 10;
                if (size > (u32)got - at) return -NV_EIO;
                if (kind == 1 && family == 1 && size == 4) {
                    *out = dns_u32(response.data + at);
                    return *out ? 0 : -NV_EIO;
                }
                at += size;
            }
            return -NV_ENOENT;
        }
    }
    return -NV_EIO;
}
static int ping_command(int n, char **v) {
    if (n != 2 && n != 3) return -NV_EINVAL;
    u32 count = 4;
    if (n == 3 && (parse_u32(v[2], &count) < 0 || !count)) return -NV_EINVAL;
    struct nv_net_info network;
    int r = active_network(&network);
    if (r < 0) return r;
    u32 address;
    r = resolve_host(v[1], &network, &address);
    if (r < 0) return r;
    print("PING "); print(v[1]); print(" ("); show_ipv4(address); println(")");
    u32 received = 0;
    for (u32 i = 0; i < count; ++i) {
        struct nv_net_ping probe = {.index = network.index, .address = address,
                                    .identifier = 0x4e56, .sequence = i + 1};
        u32 start = clock_ticks();
        for (;;) {
            r = devctl(NV_SUB_NET, NV_NET_PING, &probe);
            if (r == 1) {
                print_u32(probe.reply_bytes); print(" bytes from "); show_ipv4(address);
                print("  seq="); print_u32(i + 1); print("  ttl="); print_u32(probe.reply_ttl);
                print("  time="); print_u32((clock_ticks() - start) * 10); println(" ms");
                ++received;
                break;
            }
            if (r < 0 && r != -NV_EAGAIN) break;
            if (clock_ticks() - start >= 300) break;
            nap(50);
        }
        if (r != 1) { print("Timeout for seq="); print_u32(i + 1); print("\n"); }
        if (i + 1 < count) nap(500);
    }
    print_u32(count); print(" sent, "); print_u32(received); println(" received");
    return received ? 0 : -NV_EIO;
}

static bool ascii_prefix(const char *a, const char *b) {
    while (*b) {
        if (!*a) return false;
        char left = *a++, right = *b++;
        if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
        if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
        if (left != right) return false;
    }
    return true;
}
static int http_url(const char *url, char host[254], char path[768], u32 *port) {
    if (ascii_prefix(url, "https://")) {
        println("HTTPS needs TLS, which is not implemented. Use an HTTP URL.");
        return -NV_ENOSYS;
    }
    if (!ascii_prefix(url, "http://")) return -NV_EINVAL;
    const char *begin = url + 7, *p = begin;
    while (*p && *p != '/' && *p != '?' && *p != '#') {
        if (*p == '@' || *p == ' ' || (u8)*p < 33) return -NV_EINVAL;
        ++p;
    }
    if (p == begin || (u32)(p - begin) >= 254) return -NV_EINVAL;
    u32 count = p - begin;
    memcpy(host, begin, count); host[count] = 0;
    *port = 80;
    for (u32 i = 0; i < count; ++i) if (host[i] == ':') {
        host[i] = 0;
        if (parse_u32(host + i + 1, port) < 0 || !*port || *port > 65535)
            return -NV_EINVAL;
        break;
    }
    if (!*host) return -NV_EINVAL;
    const char *fragment = p;
    while (*fragment && *fragment != '#') {
        if ((u8)*fragment < 33 || (u8)*fragment >= 127) return -NV_EINVAL;
        ++fragment;
    }
    u32 length = fragment - p;
    if (length + 2 > 768) return -NV_E2BIG;
    if (*p == '/') {
        memcpy(path, p, length);
        path[length] = 0;
    } else {
        path[0] = '/';
        memcpy(path + 1, p, length);
        path[length + 1] = 0;
    }
    return 0;
}
static int http_response(u32 size, bool *known, u64 *length, bool *chunked) {
    if (size < 12 || strncmp(http_headers, "HTTP/1.", 7) ||
        http_headers[8] != ' ' || http_headers[9] != '2' ||
        http_headers[10] != '0' || http_headers[11] != '0') {
        println("HTTP server did not return 200 OK.");
        return -NV_EIO;
    }
    *known = *chunked = false;
    for (u32 i = 0; i + 1 < size;) {
        u32 end = i;
        while (end + 1 < size && !(http_headers[end] == '\r' && http_headers[end + 1] == '\n')) ++end;
        if (end + 1 >= size) return -NV_EIO;
        http_headers[end] = 0;
        const char *line = http_headers + i;
        if (ascii_prefix(line, "Content-Length:")) {
            const char *digits = line + 15;
            while (*digits == ' ' || *digits == '\t') ++digits;
            u64 value = 0;
            if (!*digits) return -NV_EIO;
            while (*digits >= '0' && *digits <= '9') {
                if (value > (~0ull - 9u) / 10u) return -NV_E2BIG;
                value = value * 10 + (u32)(*digits++ - '0');
            }
            if (*digits) return -NV_EIO;
            *length = value; *known = true;
        } else if (ascii_prefix(line, "Transfer-Encoding:") &&
                   ascii_prefix(line + 18, " chunked")) *chunked = true;
        else if (ascii_prefix(line, "Content-Encoding:") &&
                 !ascii_prefix(line + 17, " identity")) {
            println("Compressed HTTP transfer is not supported.");
            return -NV_ENOSYS;
        }
        i = end + 2;
        if (!http_headers[i]) break;
    }
    if (*chunked) {
        println("Chunked HTTP transfer is not supported by this downloader.");
        return -NV_ENOSYS;
    }
    return 0;
}
static int http_write(int fd, const u8 *data, u32 count) {
    while (count) {
        int done = emit(fd, data, count);
        if (done <= 0) return done < 0 ? done : -NV_EIO;
        data += done;
        count -= (u32)done;
    }
    return 0;
}
static int wget_command(int n, char **v) {
    if (n != 2 && n != 4) return -NV_EINVAL;
    if (n == 4 && strcmp(v[1], "-O")) return -NV_EINVAL;
    const char *url = n == 4 ? v[3] : v[1];
    char host[254], path[768];
    u32 port;
    int r = http_url(url, host, path, &port);
    if (r < 0) return r;
    struct nv_net_info network;
    r = active_network(&network);
    if (r < 0) return r;
    u32 address;
    r = resolve_host(host, &network, &address);
    if (r < 0) return r;
    const char *output = NULL;
    if (n == 4) output = v[2];
    else {
        output = "index.html";
        for (const char *p = path; *p && *p != '?'; ++p)
            if (*p == '/' && p[1] && p[1] != '?') output = p + 1;
    }
    char target[NV_PATH_MAX];
    if (!*output || strlcpy(target, output, sizeof(target)) >= sizeof(target)) return -NV_E2BIG;
    char *query = target;
    while (*query && *query != '?') ++query;
    if (n == 2) *query = 0;
    if (!*target) return -NV_EINVAL;
    memset(&tcp_io, 0, sizeof(tcp_io));
    tcp_io.index = network.index; tcp_io.address = address; tcp_io.port = port;
    u32 started = clock_ticks();
    do {
        r = devctl(NV_SUB_NET, NV_NET_TCP_OPEN, &tcp_io);
        if (r < 0) return r;
        if (r == 1) break;
        nap(50);
    } while (clock_ticks() - started < 800);
    if (r != 1) { devctl(NV_SUB_NET, NV_NET_TCP_CLOSE, &tcp_io); return -NV_EIO; }
    u32 size = 0;
    char port_text[8] = {0};
    if (port != 80) number(port_text, port, 10);
    const char *parts[] = {"GET ", path, " HTTP/1.0\r\nHost: ", host,
                            port == 80 ? "" : ":", port_text,
                            "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"};
    for (u32 i = 0; i < ARRAY_LEN(parts); ++i) {
        u32 length = strlen(parts[i]);
        if (size + length > sizeof(tcp_io.data)) { r = -NV_E2BIG; goto done; }
        memcpy(tcp_io.data + size, parts[i], length); size += length;
    }
    tcp_io.length = size;
    started = clock_ticks();
    do {
        r = devctl(NV_SUB_NET, NV_NET_TCP_SEND, &tcp_io);
        if (r != -NV_EAGAIN) break;
        nap(30);
    } while (clock_ticks() - started < 500);
    if (r != (int)size) { if (r >= 0) r = -NV_EIO; goto done; }
    char temp[NV_PATH_MAX];
    u32 prefix = 0;
    for (u32 i = 0; target[i]; ++i) if (target[i] == '/') prefix = i + 1;
    if (prefix + 32 >= sizeof(temp)) { r = -NV_E2BIG; goto done; }
    memcpy(temp, target, prefix);
    strlcpy(temp + prefix, ".download-", sizeof(temp) - prefix);
    u32 end = strlen(temp);
    number(temp + end, clock_ticks(), 16);
    end = strlen(temp);
    temp[end++] = '-';
    int fd = -NV_EEXIST;
    for (u32 i = 0; i < 32 && fd == -NV_EEXIST; ++i) {
        number(temp + end, i, 10);
        fd = open_file(temp, NV_WRITE | NV_CREATE | NV_EXCL);
    }
    if (fd < 0) { r = fd; goto done; }
    u32 header_size = 0, idle_at = clock_ticks();
    bool headers_done = false, known = false, chunked = false;
    u64 expected = 0, total = 0;
    for (;;) {
        tcp_io.length = sizeof(tcp_io.data);
        int got = devctl(NV_SUB_NET, NV_NET_TCP_RECV, &tcp_io);
        if (got == -NV_EAGAIN) {
            if (clock_ticks() - idle_at >= 1000) { r = -NV_EIO; break; }
            nap(20); continue;
        }
        if (got < 0) { r = got; break; }
        if (!got) { r = known && total != expected ? -NV_EIO : 0; break; }
        idle_at = clock_ticks();
        u32 at = 0;
        if (!headers_done) {
            for (; at < (u32)got; ++at) {
                if (header_size + 1 >= sizeof(http_headers)) { r = -NV_E2BIG; break; }
                http_headers[header_size++] = (char)tcp_io.data[at];
                if (header_size >= 4 && !memcmp(http_headers + header_size - 4, "\r\n\r\n", 4)) {
                    http_headers[header_size] = 0;
                    r = http_response(header_size, &known, &expected, &chunked);
                    headers_done = true;
                    ++at;
                    break;
                }
            }
            if (r < 0) break;
            if (!headers_done) continue;
        }
        if ((u32)got > at) {
            u32 body = (u32)got - at;
            if (known && (u64)body > expected - MIN(total, expected)) { r = -NV_EIO; break; }
            r = http_write(fd, tcp_io.data + at, body);
            if (r < 0) break;
            total += body;
        }
    }
    if (!headers_done && !r) r = -NV_EIO;
    close_file(fd);
    if (!r) r = move_path(temp, target);
    if (r < 0) remove_path(temp);
    if (!r) {
        print("Downloaded "); print_u64(total); print(" bytes to "); println(target);
        struct nv_info status;
        if (info(&status) == 0 && status.disk_present) {
            int saved = control(NV_CTL_SYNC, 0);
            if (saved < 0) { println("Download is in RAM; anchor failed."); r = saved; }
        }
    }
done:
    devctl(NV_SUB_NET, NV_NET_TCP_CLOSE, &tcp_io);
    return r;
}
