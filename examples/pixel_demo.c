#include <nv/sdk.h>
/* A tiny external application: query first, paint one rectangle, wait for Esc. */
static u32 pixels[64 * 64];
int user_main(const char *args) {
    (void)args;
    struct nv_display_info info;
    if (nv_display_info(&info) < 0 || info.api_version != NV_DISPLAY_API_VERSION ||
        info.width < 64 || info.height < 64) return 1;
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x)
            pixels[y * 64 + x] = nv_display_rgb(info.format,
                (x / 8 + y / 8) % 2 ? 0x276078 : 0xe9eff3);
    if (nv_display_acquire() < 0) return 2;
    struct nv_display_present rect = {
        .x = (info.width - 64) / 2, .y = (info.height - 64) / 2,
        .width = 64, .height = 64, .stride = 64 * 4, .pixels = (u32)(uptr)pixels};
    int result = nv_display_present(&rect);
    if (result >= 0) {
        for (;;) {
            int key = nv_syscall(NV_KEY, 0, 0, 0);
            if (key >= 0 && (key & 4095) == 27) break;
            if (key < 0 && key != -NV_EAGAIN) break;
            nv_syscall(NV_SLEEP, 25, 0, 0);
        }
    }
    nv_display_release();
    return result < 0 ? 3 : 0;
}
