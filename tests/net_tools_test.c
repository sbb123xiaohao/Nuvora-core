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
    puts("PASS network tools: HTTP URL, response and length bounds");
}
