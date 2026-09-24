#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
static uint64_t bss[1024];
static uint64_t initialized = UINT64_C(0x1234567887654321);
int main(int argc, char **argv, char **envp) {
    assert(argc >= 1 && !strcmp(argv[0], "/apps/ctest") && argv[argc] == NULL && !envp[0]);
    if (argc > 1) assert(argc == 4 && !strcmp(argv[1], "alpha") &&
                        !strcmp(argv[2], "two words") && !strcmp(argv[3], ""));
    assert(initialized == UINT64_C(0x1234567887654321));
    for (unsigned i = 0; i < 1024; ++i) assert(!bss[i]);
    for (unsigned i = 1; i <= 100; ++i) {
        size_t n = i * 93;
        unsigned char *p = calloc(n, 1);
        assert(p && !((uintptr_t)p & 15));
        for (size_t j = 0; j < n; ++j) assert(!p[j]);
        memset(p, 0x5a, n);
        unsigned char *q = realloc(p, n + 12000);
        assert(q);
        for (size_t j = 0; j < n; ++j) assert(q[j] == 0x5a);
        free(q);
    }
    assert(!calloc(SIZE_MAX, 2));
    printf("C TEST: argc=%d; ELF64 data/BSS, malloc/calloc/realloc, alignment PASS\n", argc);
    return 0;
}
