#ifndef NV_DESKTOP_SHELL_H
#define NV_DESKTOP_SHELL_H
#include <nv/abi.h>
#include <nv/string.h>

enum { DESKTOP_FILES, DESKTOP_EDITOR, DESKTOP_TERMINAL, DESKTOP_MEDIA,
       DESKTOP_ACCOUNTS, DESKTOP_SETTINGS, DESKTOP_CLIENT_FIRST,
       DESKTOP_BUILTINS = 4, DESKTOP_WINDOW_COUNT = DESKTOP_CLIENT_FIRST + NV_WINDOW_MAX };
enum { DESKTOP_FLOATING, DESKTOP_TILE_LEFT, DESKTOP_TILE_RIGHT, DESKTOP_TILE_MAX };
enum { DESKTOP_BAR_HEIGHT=56 };
struct desktop_window {
    i32 x, y;
    u32 w, h;
    i32 saved_x, saved_y;
    u32 saved_w, saved_h;
    bool open, minimized, maximized;
    u32 tiled;
    u32 client, pid, generation, pixel_width, pixel_height;
    const u32 *pixels;
    const struct nv_surface *text;
    char title[64];
};
struct desktop_rect { u32 x, y, w, h; };
struct desktop_app { const char *name, *description, *keywords; };
static const struct desktop_app desktop_apps[] = {
    {"Files", "Folders and drives", "file folder disk drive home"},
    {"Text Editor", "Plain text", "text edit note txt markdown"},
    {"Terminal", "Commands and downloads", "terminal shell command console wget ping"},
    {"Media", "Music and video", "media music audio video mp3 mp2 flac wav mpeg player"},
    {"Folio", "Documents", "folio document write nvd"},
    {"Accounts", "Users, passwords and sign-in", "account user password login"},
    {"Settings", "Appearance, security and power", "settings appearance wallpaper security lock power restart shutdown"},
};
struct desktop_launcher {
    char query[64];
    bool select_all;
    u8 ids[ARRAY_LEN(desktop_apps)];
    u32 count, selected;
};
struct desktop_switcher {
    bool open;
    u8 ids[DESKTOP_WINDOW_COUNT];
    u32 count, selected;
};
static inline char desktop_lower(char c) { return c>='A' && c<='Z'?c+('a'-'A'):c; }
static inline bool desktop_contains(const char *text, const char *term, u32 length) {
    for (;*text;++text) {
        u32 i=0;
        while (i<length && text[i] && desktop_lower(text[i])==desktop_lower(term[i])) ++i;
        if (i==length) return true;
    }
    return !length;
}
/* Each word must match the name or vocabulary of the same application. */
static inline void desktop_search(struct desktop_launcher *l) {
    l->count=l->selected=0;
    l->select_all=false;
    for (u32 app=0;app<ARRAY_LEN(desktop_apps);++app) {
        bool match=true;
        const char *q=l->query;
        while (*q) {
            while (*q==' ') ++q;
            const char *term=q;
            while (*q && *q!=' ') ++q;
            u32 n=(u32)(q-term);
            if (n && !desktop_contains(desktop_apps[app].name,term,n) &&
                !desktop_contains(desktop_apps[app].keywords,term,n)) { match=false; break; }
        }
        if (match) l->ids[l->count++]=(u8)app;
    }
}
static inline struct desktop_rect desktop_menu_rect(u32 sw, u32 sh) {
    (void)sw;
    u32 w=MIN(348u,sw-32),h=MIN(404u,sh-DESKTOP_BAR_HEIGHT-16);
    return (struct desktop_rect){(sw-w)/2,sh-DESKTOP_BAR_HEIGHT-h-8,w,h};
}
static inline void desktop_raise(u8 *order, u32 id) {
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) if (order[i]==id) {
        for (;i+1<DESKTOP_WINDOW_COUNT;++i) order[i]=order[i+1];
        order[DESKTOP_WINDOW_COUNT-1]=(u8)id;
        break;
    }
}
/* Stacking order also records most recent use. Include minimized windows so
 * they can be restored from either the switcher or the overview. */
static inline u32 desktop_window_list(const struct desktop_window *windows,
                                      const u8 *order, u8 *ids) {
    u32 count=0;
    for (i32 z=DESKTOP_WINDOW_COUNT-1;z>=0;--z)
        if (windows[order[z]].open) ids[count++]=order[z];
    return count;
}
static inline void desktop_switch_step(struct desktop_switcher *s, i32 direction) {
    if (s->count) s->selected=(u32)((i32)s->selected+(i32)s->count+direction) % s->count;
}
static inline void desktop_switch_begin(struct desktop_switcher *s,
                                        const struct desktop_window *windows,
                                        const u8 *order, u32 active, i32 direction) {
    s->count=desktop_window_list(windows,order,s->ids);
    s->selected=0; s->open=s->count>0;
    for (u32 i=0;i<s->count;++i) if (s->ids[i]==active) { s->selected=i; break; }
    if (active<DESKTOP_WINDOW_COUNT) desktop_switch_step(s,direction);
}
/* The captured order remains stable while Alt is held, even when a client
 * exits. Removing a window preserves the selected identity when possible. */
static inline void desktop_switch_prune(struct desktop_switcher *s,
                                        const struct desktop_window *windows) {
    if (!s->open) return;
    u32 chosen=s->ids[s->selected], count=0, selected=s->selected;
    for (u32 i=0;i<s->count;++i) if (windows[s->ids[i]].open) {
        if (s->ids[i]==chosen) selected=count;
        s->ids[count++]=s->ids[i];
    }
    s->count=count; s->open=count>0;
    s->selected=count?MIN(selected,count-1):0;
}
static inline const char *desktop_window_name(const struct desktop_window *w, u32 id) {
    if (id==DESKTOP_ACCOUNTS) return "Accounts";
    if (id==DESKTOP_SETTINGS) return "Settings";
    return w->client?w->title:id<ARRAY_LEN(desktop_apps)?desktop_apps[id].name:"App";
}
static inline void desktop_place(struct desktop_window *w, u32 sw, u32 workh, u32 place) {
    if (place==DESKTOP_FLOATING) {
        if (!w->maximized && !w->tiled) return;
        w->w=MIN(w->saved_w,sw); w->h=MIN(w->saved_h,workh);
        w->x=MAX(0,MIN(w->saved_x,(i32)(sw-w->w)));
        w->y=MAX(0,MIN(w->saved_y,(i32)(workh-w->h)));
        w->maximized=false; w->tiled=DESKTOP_FLOATING;
        return;
    }
    if (!w->maximized && !w->tiled) {
        w->saved_x=w->x; w->saved_y=w->y;
        w->saved_w=w->w; w->saved_h=w->h;
    }
    w->x=place==DESKTOP_TILE_RIGHT?(i32)(sw/2):0; w->y=0;
    w->w=place==DESKTOP_TILE_MAX?sw:place==DESKTOP_TILE_LEFT?sw/2:sw-sw/2;
    w->h=workh;
    w->maximized=place==DESKTOP_TILE_MAX;
    w->tiled=w->maximized?DESKTOP_FLOATING:place;
}
static inline u32 desktop_snap_target(u32 sw, u32 x, u32 y) {
    return y<=7?DESKTOP_TILE_MAX:x<=7?DESKTOP_TILE_LEFT:
           x+8>=sw?DESKTOP_TILE_RIGHT:DESKTOP_FLOATING;
}
static inline u32 desktop_overview_columns(u32 sw) { return sw>=580?3:2; }
static inline u32 desktop_overview_page_size(u32 sw) { return desktop_overview_columns(sw)*2; }
/* At most two rows retain usable previews on the smallest supported mode. */
static inline struct desktop_rect desktop_overview_rect(u32 sw, u32 sh,
                                                        u32 count, u32 index) {
    if (!count) return (struct desktop_rect){0};
    u32 columns=MIN(desktop_overview_columns(sw),count), rows=(count+columns-1)/columns;
    u32 width=(sw-48-(columns-1)*12)/columns;
    u32 height=MIN((sh-136-(rows-1)*12)/rows,width*3/4+36);
    u32 top=58+(sh-136-rows*height-(rows-1)*12)/2;
    return (struct desktop_rect){24+(index%columns)*(width+12),
        top+(index/columns)*(height+12),width,height};
}
static inline struct desktop_rect desktop_switch_rect(u32 sw, u32 sh) {
    u32 width=MIN(510u,sw-32),height=MIN(270u,sh-62);
    return (struct desktop_rect){(sw-width)/2,(sh-DESKTOP_BAR_HEIGHT-height)/2,width,height};
}
#endif
