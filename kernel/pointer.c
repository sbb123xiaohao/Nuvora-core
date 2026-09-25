#include "kernel.h"
/* One graphical owner exists at a time. Queue whole pointer reports so button
 * transitions survive motion bursts; on overflow, keep the freshest state. */
static struct nv_pointer_event queue[64];
static u32 head, tail, buttons;
void pointer_reset(void) { head = tail = buttons = 0; }
void pointer_push(i32 dx, i32 dy, u32 state) {
    state &= NV_POINTER_LEFT | NV_POINTER_RIGHT | NV_POINTER_MIDDLE;
    if (!dx && !dy && state == buttons) return;
    u32 next = (head + 1) % ARRAY_LEN(queue);
    if (next == tail) tail = (tail + 1) % ARRAY_LEN(queue);
    queue[head] = (struct nv_pointer_event){dx, dy, 0, state};
    head = next;
    buttons = state;
}
int pointer_next(struct nv_pointer_event *event) {
    if (head == tail) return 0;
    *event = queue[tail];
    tail = (tail + 1) % ARRAY_LEN(queue);
    return 1;
}
bool pointer_boot_report(const u8 *bytes, u32 size, struct nv_pointer_event *event) {
    if (!bytes || !event || size != 3) return false;
    *event = (struct nv_pointer_event){(signed char)bytes[1],
        (signed char)bytes[2], 0, bytes[0] & 7u};
    return true;
}
