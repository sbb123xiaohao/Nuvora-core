#include "kernel.h"
/* One graphical owner exists at a time. Queue whole pointer reports so button
 * transitions survive motion bursts; on overflow, keep the freshest state. */
static struct nv_pointer_event queue[64];
static u32 head, tail, buttons;
static i32 absolute_x, absolute_y;
static bool can_merge;
void pointer_reset(void) {
    head = tail = buttons = 0;
    absolute_x = absolute_y = 0;
    can_merge = false;
}
static i32 add_motion(i32 a, i32 b) {
    i64 sum = (i64)a + b;
    return sum > 0x7fffffff ? 0x7fffffff :
           sum < (-0x7fffffff - 1) ? (-0x7fffffff - 1) : (i32)sum;
}
void pointer_push(i32 dx, i32 dy, u32 state) {
    state &= NV_POINTER_LEFT | NV_POINTER_RIGHT | NV_POINTER_MIDDLE | NV_POINTER_ABSOLUTE;
    bool absolute = (state & NV_POINTER_ABSOLUTE) != 0;
    if (absolute) {
        if (dx < 0 || dx > 32767 || dy < 0 || dy > 32767) return;
        if (state == buttons && dx == absolute_x && dy == absolute_y) return;
    } else if (!dx && !dy && state == buttons) return;
    bool same_buttons = state == buttons;
    /* Compress a run of motion, but retain the first event after a button
     * transition so a press still occurs at its original coordinates. */
    if (same_buttons && can_merge && head != tail) {
        u32 last = (head + ARRAY_LEN(queue) - 1) % ARRAY_LEN(queue);
        if (absolute) {
            queue[last].dx = dx;
            queue[last].dy = dy;
        } else {
            queue[last].dx = add_motion(queue[last].dx, dx);
            queue[last].dy = add_motion(queue[last].dy, dy);
        }
        absolute_x = dx;
        absolute_y = dy;
        return;
    }
    u32 next = (head + 1) % ARRAY_LEN(queue);
    if (next == tail) tail = (tail + 1) % ARRAY_LEN(queue);
    queue[head] = (struct nv_pointer_event){dx, dy, 0, state};
    head = next;
    buttons = state;
    absolute_x = dx;
    absolute_y = dy;
    can_merge = same_buttons;
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
/* QEMU's HID tablet supplies buttons, two LE absolute axes and a wheel.
 * Read its complete six-byte report; wheel handling is not implemented yet. */
bool pointer_tablet_report(const u8 *bytes, u32 size, struct nv_pointer_event *event) {
    if (!bytes || !event || size != 6) return false;
    u32 x = bytes[1] | ((u32)bytes[2] << 8);
    u32 y = bytes[3] | ((u32)bytes[4] << 8);
    if (x > 32767 || y > 32767) return false;
    *event = (struct nv_pointer_event){(i32)x, (i32)y, 0,
        (bytes[0] & 7u) | NV_POINTER_ABSOLUTE};
    return true;
}
