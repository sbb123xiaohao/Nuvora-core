#include "runtime.h"
#include "desktop_ui.h"
#define DESKTOP_ITEMS 128u
static struct nv_dirent entries[DESKTOP_ITEMS];
static struct nv_display_info mode;
static char directory[NV_PATH_MAX], message[160], drive[32];
static u32 count, selected, scroll, volumes;
static bool help;

static void note(const char *s) { strlcpy(message, s, sizeof(message)); }
static int refresh(void) {
    int r = getcwd_path(directory, sizeof(directory));
    if (r < 0) return r;
    count = 0;
    for (u32 i = 0; i < DESKTOP_ITEMS; ++i) {
        r = list_dir(".", i, &entries[i]);
        if (r < 0) return r;
        if (!r) break;
        ++count;
    }
    if (selected >= count) selected = count ? count - 1 : 0;
    if (scroll > selected) scroll = selected;
    volumes = 0;
    struct nv_volume_info volume;
    while (volumes < NV_VOLUME_MAX && volume_info(volumes, &volume) == 1) ++volumes;
    if (volumes) {
        drive[0] = (char)('0' + volumes);
        strlcpy(drive + 1, volumes == 1 ? " drive" : " drives", sizeof(drive) - 1);
    } else strlcpy(drive, "RAM only", sizeof(drive));
    return 0;
}
static void scroll_to_selection(void) {
    u32 visible = desktop_visible(mode.height, desktop_scale(mode.width, mode.height));
    if (selected < scroll) scroll = selected;
    if (selected >= scroll + visible) scroll = selected - visible + 1;
}
static int draw(u32 *tile, u32 rows) {
    struct desktop_view view = {directory, message, drive, entries,
                                count, selected, scroll, help, volumes};
    for (u32 y = 0; y < mode.height; y += rows) {
        struct nv_canvas canvas = {tile, mode.width, y, MIN(rows, mode.height - y), mode.format};
        desktop_render(&canvas, mode.height, &view);
        struct nv_display_present rect = {
            .x = 0, .y = y, .width = mode.width, .height = canvas.rows,
            .stride = mode.width * 4, .pixels = (u32)(uptr)tile};
        int r = nv_display_present(&rect);
        if (r < 0) return r;
    }
    return 0;
}
static bool document_name(const char *name) {
    usize len = strlen(name);
    return (len >= 4 && !strcmp(name + len - 4, ".txt")) ||
           (len >= 4 && !strcmp(name + len - 4, ".nvd"));
}
static int edit(const char *path) {
    int r = nv_display_release();
    if (r < 0) return r;
    int pid = spawn("/apps/folio", path);
    if (pid > 0) r = wait_task(pid);
    else r = pid;
    int acquired = nv_display_acquire();
    if (acquired < 0) return acquired;
    if (r < 0) return r;
    return refresh();
}
static int open_selected(void) {
    if (!count) return 0;
    if (entries[selected].kind == NV_DIR) {
        int r = chdir_path(entries[selected].name);
        if (r < 0) return r;
        selected = scroll = 0;
        return refresh();
    }
    if (!document_name(entries[selected].name)) {
        note("No viewer for this file. Open .txt or .nvd documents.");
        return 0;
    }
    char full[NV_PATH_MAX];
    u32 len = (u32)strlcpy(full, directory, sizeof(full));
    if (len >= sizeof(full)) return -NV_E2BIG;
    if (len != 1 || full[0] != '/') {
        if (len + 1 >= sizeof(full)) return -NV_E2BIG;
        full[len++] = '/'; full[len] = 0;
    }
    if (strlcpy(full + len, entries[selected].name, sizeof(full) - len) >= sizeof(full) - len)
        return -NV_E2BIG;
    return edit(full);
}
int user_main(const char *args) {
    if (app_help("desktop", args)) return 0;
    if (*args) { println("Usage: desktop"); return 1; }
    int r = nv_display_info(&mode);
    if (r < 0) { println("Desktop requires a UEFI firmware pixel framebuffer."); return 1; }
    if (mode.api_version != NV_DISPLAY_API_VERSION ||
        (mode.format != NV_DISPLAY_BGRX8 && mode.format != NV_DISPLAY_RGBX8) ||
        mode.width < 640 || mode.height < 480 || mode.width > 8192 ||
        (u64)mode.width * 4 > mode.max_copy_bytes ||
        mode.max_copy_bytes > NV_DISPLAY_MAX_COPY) {
        println("Desktop: unsupported display mode (minimum 640x480).");
        return 1;
    }
    u32 rows = mode.max_copy_bytes / (mode.width * 4);
    u32 pages = (mode.max_copy_bytes + NV_PAGE - 1) / NV_PAGE;
    u32 *tile = grow((i32)pages);
    if ((iptr)tile < 0) { report_error("Desktop: memory", (int)(iptr)tile); return 1; }
    r = refresh();
    if (r < 0) { report_error("Desktop: files", r); return 1; }
    r = nv_display_acquire();
    if (r < 0) { report_error("Desktop: display", r); return 1; }
    note("Select a folder or document. F1 shows keyboard controls.");
    bool dirty = true;
    for (;;) {
        if (dirty) {
            r = draw(tile, rows);
            if (r < 0) break;
            dirty = false;
        }
        int key = key_event();
        if (key == -NV_EAGAIN) { nap(25); continue; }
        if (key < 0) { r = key; break; }
        u32 k = (u32)key & 4095u;
        if (k == 27) break;
        if (k == NV_KEY_F1) help = !help;
        else if (k == NV_KEY_UP && selected) --selected;
        else if (k == NV_KEY_DOWN && selected + 1 < count) ++selected;
        else if (k == NV_KEY_PGUP) selected = selected > 8 ? selected - 8 : 0;
        else if (k == NV_KEY_PGDN && count) selected = MIN(count - 1, selected + 8);
        else if (k == '\b') {
            r = chdir_path("..");
            if (r >= 0) { selected = scroll = 0; r = refresh(); }
        } else if (k == '\n') r = open_selected();
        else if (k == NV_KEY_F2) r = edit("");
        else if (k == NV_KEY_F5) r = refresh();
        else if (k == NV_KEY_F6) {
            r = control(NV_CTL_SYNC, 0);
            if (r >= 0) note("Changes saved to all mounted data drives.");
        } else if (k >= '1' && k <= '7') {
            static const char *const places[] = {"/home", "/drives/D", "/drives/E",
                                                "/drives/F", "/", "/apps", "/tmp"};
            if (k <= '4' && (u32)(k - '1') >= volumes) continue;
            r = chdir_path(places[k - '1']);
            if (r >= 0) { selected = scroll = 0; r = refresh(); }
        }
        if (r < 0) { note(error_name(r)); r = 0; }
        scroll_to_selection();
        dirty = true;
    }
    nv_display_release();
    if (r < 0) { report_error("Desktop: rendering", r); return 1; }
    return 0;
}
