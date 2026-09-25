/* Feed actual HID boot reports through the input queue used by the desktop. */
#include <assert.h>
#include <stdio.h>
#define NV_KERNEL_H
#include <nv/abi.h>
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#include "../kernel/pointer.c"

int main(void) {
    struct nv_pointer_event event;
    const u8 press[] = {NV_POINTER_LEFT, 0xfe, 0x7f};
    const u8 release[] = {0, 0, 0};
    assert(!pointer_boot_report(NULL, 3, &event));
    assert(!pointer_boot_report(press, 2, &event));
    assert(!pointer_boot_report(press, 3, NULL));
    assert(pointer_boot_report(press, 3, &event));
    assert(event.dx == -2 && event.dy == 127 && event.buttons == NV_POINTER_LEFT);
    pointer_reset();
    assert(pointer_next(&event) == 0);
    pointer_push(event.dx, event.dy, event.buttons);
    assert(pointer_next(&event) == 1);
    assert(event.dx == -2 && event.dy == 127 && event.buttons == NV_POINTER_LEFT);
    pointer_push(0, 0, NV_POINTER_LEFT); /* unchanged state is not a new click */
    assert(pointer_next(&event) == 0);
    assert(pointer_boot_report(release, 3, &event));
    pointer_push(event.dx, event.dy, event.buttons);
    assert(pointer_next(&event) == 1 && event.buttons == 0);
    assert(pointer_next(&event) == 0);
    for (u32 i = 0; i < 100; ++i) pointer_push((i32)i + 1, 0, i & 1);
    assert(pointer_next(&event) == 1 && event.dx == 38); /* 63 newest events */
    for (u32 i = 39; i <= 100; ++i)
        assert(pointer_next(&event) == 1 && event.dx == (i32)i);
    assert(pointer_next(&event) == 0);
    pointer_push(0, 0, 7);
    pointer_reset();
    assert(pointer_next(&event) == 0);
    pointer_push(0, 0, 0);
    assert(pointer_next(&event) == 0);
    puts("PASS pointer: signed HID movement, button transitions, queue wrap and lease reset");
    return 0;
}
