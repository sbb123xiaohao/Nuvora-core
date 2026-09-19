#include <assert.h>
#include <stdio.h>
#include "../kernel/kernel.h"
_Static_assert(ALIGN_UP(0x100000001ull, PAGE) == 0x100001000ull, "alignment beyond 4 GiB");
_Static_assert(ALIGN_UP(64ull << 30, PAGE) == (64ull << 30), "64 GiB endpoint");
_Static_assert(((0x8000000100003003ull & P_ADDRESS)) == 0x100003000ull, "NX is not an address");
int main(void) {
    uptr pages[] = {0x1000, 0x40000000, 0x100001000ull, PHYS_LIMIT - PAGE};
    for (u32 i = 0; i < ARRAY_LEN(pages); ++i) {
        assert(ptr_phys(phys_ptr(pages[i])) == pages[i]);
        if (pages[i] >= USER_BASE) assert((uptr)phys_ptr(pages[i]) >= PHYS_WINDOW);
    }
    puts("PASS addresses: 64-bit alignment, NX mask, physical/kernel-pointer round trip");
}
