#include "runtime.h"
#include "desktop_ui.h"
#define DESKTOP_ITEMS 128u
static struct nv_dirent entries[DESKTOP_ITEMS];
static struct nv_display_info mode;
static char directory[NV_PATH_MAX], message[160], drive[32];
static u32 count, selected, scroll, volumes;
static bool help;
static u32 pointer_x, pointer_y, pointer_buttons;
static bool pointer_visible;
static bool audio_ready;
static bool menu, quit_requested;
static u32 menu_selected;

static void note(const char *s) { strlcpy(message, s, sizeof(message)); }
static void note_error(const char *action, int error) {
    strlcpy(message, action, sizeof(message));
    usize n = strlen(message);
    strlcpy(message + n, ": ", sizeof(message) - n);
    n = strlen(message);
    strlcpy(message + n, error_name(error), sizeof(message) - n);
}
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
                                count, selected, scroll, help, volumes,
                                pointer_visible, pointer_x, pointer_y, audio_ready,
                                menu, menu_selected};
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
static bool wave_name(const char *name) {
    usize len = strlen(name);
    return len >= 4 && !strcmp(name + len - 4, ".wav");
}
static bool mp3_name(const char *name) {
    usize len = strlen(name);
    return len >= 4 && !strcmp(name + len - 4, ".mp3");
}
static bool video_name(const char *name) {
    usize len = strlen(name);
    return (len >= 4 && !strcmp(name + len - 4, ".mpg")) ||
           (len >= 5 && !strcmp(name + len - 5, ".mpeg"));
}
static int launch(const char *app, const char *path) {
    int r = nv_display_release();
    if (r < 0) return r;
    int pid = spawn(app, path);
    if (pid > 0) r = wait_task(pid);
    else r = pid;
    int acquired = nv_display_acquire();
    if (acquired < 0) return acquired;
    pointer_buttons = 0;
    if (r > 0) return -NV_EIO;
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
    bool document = document_name(entries[selected].name);
    bool media = wave_name(entries[selected].name) ||
                 mp3_name(entries[selected].name) || video_name(entries[selected].name);
    if (!document && !media) {
        note("No opener. Supported: .txt, .nvd, .wav, .mp3, .mpg.");
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
    if (media && !video_name(entries[selected].name) && !audio_ready) {
        note("No HDA audio output. Select a supported device."); return 0;
    }
    return launch(document ? "/apps/folio" : "/apps/media", full);
}
static int go_place(u32 index);
static int start_app(u32 index) {
    menu = false;
    menu_selected = index;
    if (index == 0) return launch("/apps/media", "");
    if (index == 1) return go_place(0);
    if (index == 2) return launch("/apps/folio", "");
    if (index == 3) quit_requested = true;
    return 0;
}
static int go_place(u32 index) {
    static const char *const places[] = {"/home", "/drives/D", "/drives/E",
                                        "/drives/F", "/", "/apps", "/tmp"};
    if (index >= ARRAY_LEN(places) || (index < NV_VOLUME_MAX && index >= volumes)) return 0;
    int r = chdir_path(places[index]);
    if (r >= 0) { selected = scroll = 0; r = refresh(); }
    return r;
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
    pointer_x = mode.width / 2;
    pointer_y = mode.height / 2;
    struct nv_audio_info audio;
    audio_ready = nv_audio_info(&audio) == 0 && audio.api_version == NV_AUDIO_API_VERSION &&
                  audio.outputs > 0;
    note("");
    bool dirty = true;
    u32 last_click = 0xffffffffu, last_click_tick = 0;
    for (;;) {
        if (dirty) {
            r = draw(tile, rows);
            if (r < 0) break;
            dirty = false;
        }
        struct nv_pointer_event event;
        int mouse = nv_pointer_poll(&event);
        if (mouse < 0) { r = mouse; break; }
        if (mouse == 1) {
            pointer_visible = true;
            i32 x = (i32)pointer_x + event.dx, y = (i32)pointer_y + event.dy;
            pointer_x = (u32)MAX(0, MIN(x, (i32)mode.width - 1));
            pointer_y = (u32)MAX(0, MIN(y, (i32)mode.height - 1));
            if ((event.buttons & NV_POINTER_LEFT) && !(pointer_buttons & NV_POINTER_LEFT)) {
                struct desktop_view view = {directory, message, drive, entries,
                    count, selected, scroll, help, volumes, true, pointer_x, pointer_y, audio_ready,
                    menu, menu_selected};
                struct desktop_hit hit = desktop_hit(mode.width, mode.height,
                                                      &view, pointer_x, pointer_y);
                if (help) help = false;
                else if (hit.kind == DESKTOP_HIT_START) menu = !menu;
                else if (hit.kind == DESKTOP_HIT_MENU) {
                    r = start_app(hit.index);
                    if (r < 0) note_error("Start", r);
                } else if (hit.kind == DESKTOP_HIT_MEDIA) {
                    menu = false;
                    r = launch("/apps/media", "");
                    if (r < 0) note_error("Media", r);
                } else if (menu) menu = false;
                else if (hit.kind == DESKTOP_HIT_PLACE) {
                    last_click = 0xffffffffu;
                    r = go_place(hit.index);
                    if (r < 0) note_error("Open location", r);
                    else note("");
                } else if (hit.kind == DESKTOP_HIT_FILE) {
                    u32 tick = (u32)call(NV_CLOCK, 0, 0, 0);
                    bool open = last_click == hit.index && tick - last_click_tick <= 40;
                    selected = hit.index;
                    last_click = hit.index;
                    last_click_tick = tick;
                    scroll_to_selection();
                    if (open) {
                        r = open_selected(); last_click = 0xffffffffu;
                        if (r < 0) note_error("Open item", r);
                    } else note("");
                }
            }
            pointer_buttons = event.buttons;
            if (r < 0) r = 0;
            dirty = true;
        }
        if (quit_requested) break;
        int key = key_event();
        if (key == -NV_EAGAIN) { nap(25); continue; }
        if (key < 0) { r = key; break; }
        u32 k = (u32)key & 4095u;
        if (menu) {
            if (k == 27 || k == NV_KEY_F10) menu = false;
            else if (k == NV_KEY_UP && menu_selected) --menu_selected;
            else if (k == NV_KEY_DOWN && menu_selected < 3) ++menu_selected;
            else if (k == '\n') {
                r = start_app(menu_selected);
                if (r < 0) note_error("Start", r);
            }
            dirty = true;
            if (quit_requested) break;
            continue;
        }
        if (k == 27) break;
        if (k == NV_KEY_F10) { menu = true; menu_selected = 0; }
        else if (k == NV_KEY_F1) help = !help;
        else if (k == NV_KEY_UP && selected) --selected;
        else if (k == NV_KEY_DOWN && selected + 1 < count) ++selected;
        else if (k == NV_KEY_PGUP) selected = selected > 8 ? selected - 8 : 0;
        else if (k == NV_KEY_PGDN && count) selected = MIN(count - 1, selected + 8);
        else if (k == '\b') {
            r = chdir_path("..");
            if (r >= 0) { selected = scroll = 0; r = refresh(); }
            if (r < 0) note_error("Parent folder", r);
            else note("");
        } else if (k == '\n') {
            r = open_selected();
            if (r < 0) note_error("Open item", r);
        } else if (k == NV_KEY_F2) {
            r = launch("/apps/folio", "");
            if (r < 0) note_error("New document", r);
        } else if (k == NV_KEY_F3) {
            r = launch("/apps/media", "");
            if (r < 0) note_error("Media", r);
        } else if (k == NV_KEY_F5) {
            r = refresh();
            if (r < 0) note_error("Refresh folder", r);
            else note("");
        }
        else if (k == NV_KEY_F6) {
            r = control(NV_CTL_SYNC, 0);
            if (r >= 0) note("Mounted drives saved.");
            else note_error("Save drives", r);
        } else if (k >= '1' && k <= '7') {
            last_click = 0xffffffffu;
            r = go_place(k - '1');
            if (r < 0) note_error("Open location", r);
            else note("");
        }
        if (r < 0) { if (!*message) note_error("File operation", r); r = 0; }
        scroll_to_selection();
        dirty = true;
    }
    nv_display_release();
    if (r < 0) { report_error("Desktop: rendering", r); return 1; }
    return 0;
}
