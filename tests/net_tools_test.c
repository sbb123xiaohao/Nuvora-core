/* Parse real HTTP response shapes before a downloader creates a target file. */
#include <assert.h>
#include <stdio.h>
#include "../user/runtime.h"
#include "../user/network.h"
#include "../user/net_tools.h"

void println(const char *s) { (void)s; }

static int headers(const char *text, bool *known, u64 *length, bool *chunked) {
    u32 n = strlen(text);
    assert(n + 1 < sizeof(http_headers));
    memcpy(http_headers, text, n + 1);
    return http_response(n, known, length, chunked);
}
int main(void) {
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
    puts("PASS network tools: HTTP URL, response and length bounds");
}
