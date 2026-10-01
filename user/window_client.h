#ifndef NV_WINDOW_CLIENT_H
#define NV_WINDOW_CLIENT_H
#include "runtime.h"
/* Small client adapter shared by native GUI apps. The desktop owns global
 * shortcuts and the pointer image. Local pointer positions may be outside
 * the content while a button is captured by the window server. */
struct app_window {
    u32 id, serial, width, height;
    bool closed, visible, focused, resized;
    struct nv_window_event input[32];
    u32 head, tail, queued;
};
static inline int app_window_open(struct app_window *w, const char *title, u32 width, u32 height) {
    struct nv_window_info info;
    int r = nv_window_info(&info);
    if (r < 0 || !info.server_pid) return r < 0 ? r : -NV_ENODEV;
    struct nv_window_create io = {.width = width, .height = height};
    strlcpy(io.title, title, sizeof(io.title));
    r = nv_window_create(&io);
    if (r < 0) return r;
    *w = (struct app_window){.id = io.id, .serial = io.serial,
        .width = width, .height = height, .visible = true};
    return 0;
}
static inline int app_window_pump(struct app_window *w) {
    if (!w->id) return 0;
    for (u32 i = 0; i < 32 && w->queued < ARRAY_LEN(w->input); ++i) {
        struct nv_window_event e;
        int r = nv_window_poll(w->id, &e);
        if (r == -NV_ENOENT || r == -NV_ENODEV) { w->closed = true; return 0; }
        if (r <= 0) return r;
        if (e.type == NV_WINDOW_EVENT_CLOSE) w->closed = true;
        else if (e.type == NV_WINDOW_EVENT_CONFIGURE) {
            w->serial = e.serial; w->width = e.width; w->height = e.height;
            w->resized = true;
        } else if (e.type == NV_WINDOW_EVENT_FOCUS) {
            bool visible = !!(e.flags & NV_WINDOW_VISIBLE);
            if (visible && !w->visible) w->resized = true;
            w->visible = visible;
            w->focused = !!(e.flags & NV_WINDOW_FOCUSED);
            w->head = w->tail = w->queued = 0;
        } else {
            w->input[w->head] = e;
            w->head = (w->head + 1) % ARRAY_LEN(w->input); ++w->queued;
        }
    }
    return 0;
}
static inline int app_window_input(struct app_window *w, u32 type, struct nv_window_event *out) {
    int r = app_window_pump(w);
    if (r < 0) return r;
    /* Preserve the other event type for the application's next poll. */
    for (u32 n = 0; n < w->queued; ++n) {
        u32 at = (w->tail + n) % ARRAY_LEN(w->input);
        if (w->input[at].type != type) continue;
        *out = w->input[at];
        for (u32 j = n; j + 1 < w->queued; ++j) {
            u32 from = (w->tail + j + 1) % ARRAY_LEN(w->input);
            w->input[(w->tail + j) % ARRAY_LEN(w->input)] = w->input[from];
        }
        w->head = (w->head + ARRAY_LEN(w->input) - 1) % ARRAY_LEN(w->input);
        --w->queued;
        return 1;
    }
    return 0;
}
static inline int app_window_key(struct app_window *w) {
    struct nv_window_event e;
    int r = app_window_input(w, NV_WINDOW_EVENT_KEY, &e);
    return r > 0 ? (int)e.key : r < 0 ? r : -NV_EAGAIN;
}
static inline int app_window_begin(struct app_window *w) {
    int r = app_window_pump(w);
    if (r < 0) return r;
    if (w->closed) return -NV_ENODEV;
    struct nv_window_frame io = {w->id, w->serial, w->width, w->height};
    return nv_window_begin(&io);
}
static inline int app_window_present(struct app_window *w, const struct nv_display_present *rect) {
    struct nv_window_pixels io = {.id = w->id, .x = rect->x, .y = rect->y,
        .width = rect->width, .height = rect->height, .stride = rect->stride, .pixels = rect->pixels};
    return nv_window_upload(&io);
}
#endif
