#ifndef NV_DESKTOP_SHELL_H
#define NV_DESKTOP_SHELL_H
#include <nv/abi.h>
#include <nv/string.h>
#include <nv/terminal.h>

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
    u32 app; /* Registered launcher identity + 1; zero for unknown clients. */
    const u32 *pixels;
    const struct nv_surface *text;
    const struct nv_terminal_view *grid;
    char title[64];
};
struct desktop_rect { u32 x, y, w, h; };
static inline u32 desktop_child_app(u32 pid,const u32 *pids,const u8 *apps) {
    if (pid) for (u32 i=0;i<NV_TASK_MAX;++i) if (pids[i]==pid) return apps[i];
    return 0;
}
/* Launcher identities come from a registered launch, never client text. */
static inline u32 desktop_client_slot(u32 app) {
    if (app==DESKTOP_TERMINAL+1) return DESKTOP_TERMINAL;
    if (app==DESKTOP_MEDIA+1) return DESKTOP_MEDIA;
    return DESKTOP_WINDOW_COUNT;
}
struct desktop_app { const char *name, *description, *keywords; };
static const struct desktop_app desktop_apps[] = {
    {"Files", "Spaces and collections", "file folder space disk drive home search"},
    {"Text Editor", "Plain text", "text edit note txt markdown"},
    {"Terminal", "Commands and downloads", "terminal shell command console wget ping"},
    {"Music", "Music and video", "media music audio video mp3 mp2 flac wav mpeg player"},
    {"Folio", "Documents", "folio document write nvd"},
    {"Accounts", "Users, passwords and sign-in", "account user password login"},
    {"Settings", "Appearance, security and power", "settings appearance wallpaper security lock power restart shutdown"},
};
struct desktop_launcher {
    char query[64];
    bool select_all;
    u8 ids[ARRAY_LEN(desktop_apps)];
    u32 count, selected, pinned;
};
struct desktop_app_press { bool active, moved; u32 app, x, y; };
static inline void desktop_app_press_begin(struct desktop_app_press *p,u32 app,u32 x,u32 y) {
    *p=(struct desktop_app_press){.active=true,.app=app,.x=x,.y=y};
}
static inline void desktop_app_press_move(struct desktop_app_press *p,u32 x,u32 y) {
    if (p->active && ((i64)x-p->x>5 || (i64)p->x-x>5 ||
                      (i64)y-p->y>5 || (i64)p->y-y>5)) p->moved=true;
}
static inline bool desktop_app_press_end(struct desktop_app_press *p,u32 app,u32 x,u32 y) {
    desktop_app_press_move(p,x,y);
    bool click=p->active && !p->moved && p->app==app;
    *p=(struct desktop_app_press){0};
    return click;
}
struct desktop_pins { u32 magic, mask; };
static inline bool desktop_pins_valid(const struct desktop_pins *p) {
    return p->magic==0x3150414e && !(p->mask&~((1u<<ARRAY_LEN(desktop_apps))-1));
}
static inline u32 desktop_registered_app(const struct desktop_window *w,u32 id) {
    if (w->client) return w->app && w->app<=ARRAY_LEN(desktop_apps)?w->app-1:ARRAY_LEN(desktop_apps);
    return id<DESKTOP_BUILTINS?id:id==DESKTOP_ACCOUNTS?5:id==DESKTOP_SETTINGS?6:ARRAY_LEN(desktop_apps);
}
static inline void desktop_restore_geometry(struct desktop_window *w,const struct desktop_window *old) {
    w->x=old->x;w->y=old->y;w->w=old->w;w->h=old->h;
    w->saved_x=old->saved_x;w->saved_y=old->saved_y;
    w->saved_w=old->saved_w;w->saved_h=old->saved_h;
    w->maximized=old->maximized;w->tiled=old->tiled;
}
static inline void desktop_fit(struct desktop_window *w,u32 width,u32 height) {
    w->w=MIN(w->w,width);w->h=MIN(w->h,height);
    w->x=MAX(0,MIN(w->x,(i32)(width-w->w)));
    w->y=MAX(0,MIN(w->y,(i32)(height-w->h)));
}
static inline void desktop_resize(struct desktop_window *w,u32 edges,u32 sw,u32 workh,
                                  i32 minw,i32 minh,i32 dx,i32 dy) {
    w->tiled=DESKTOP_FLOATING;
    /* A tiled frame can be smaller than an app's preferred minimum. Available
     * workspace takes priority; resizing must not grow it past the screen. */
    if (edges&2) w->w=(u32)MIN((i32)sw-w->x,MAX(minw,(i32)w->w+dx));
    if (edges&8) w->h=(u32)MIN((i32)workh-w->y,MAX(minh,(i32)w->h+dy));
    if (edges&1) {
        i32 right=w->x+(i32)w->w;
        w->x=MAX(0,MIN(right-minw,w->x+dx));w->w=(u32)(right-w->x);
    }
    if (edges&4) {
        i32 bottom=w->y+(i32)w->h;
        w->y=MAX(0,MIN(bottom-minh,w->y+dy));w->h=(u32)(bottom-w->y);
    }
}
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
    for (u32 pass=0;pass<2;++pass) for (u32 app=0;app<ARRAY_LEN(desktop_apps);++app) {
        if (!!(l->pinned&(1u<<app))!=(pass==0)) continue;
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
    u32 w=MIN(560u,sw-32),h=MIN(450u,sh-DESKTOP_BAR_HEIGHT-48);
    return (struct desktop_rect){(sw-w)/2,44+(sh-DESKTOP_BAR_HEIGHT-44-h)/2,w,h};
}
static inline u32 desktop_menu_columns(u32 sw) { return sw>=580?3:2; }
static inline struct desktop_rect desktop_menu_app_rect(u32 sw,u32 sh,u32 index) {
    struct desktop_rect r=desktop_menu_rect(sw,sh);
    u32 columns=desktop_menu_columns(sw),rows=(ARRAY_LEN(desktop_apps)+columns-1)/columns;
    u32 cell=(r.w-40-(columns-1)*12)/columns,row=MIN(88u,(r.h-154)/rows);
    return (struct desktop_rect){r.x+20+(index%columns)*(cell+12),
        r.y+98+(index/columns)*row,cell,row-4};
}
static inline void desktop_launcher_move(struct desktop_launcher *l,i32 step) {
    if (l->count) l->selected=(u32)MAX(0,MIN((i32)l->selected+step,(i32)l->count-1));
}
static inline struct desktop_rect desktop_apps_button(u32 sw) {
    return (struct desktop_rect){sw-244,11,114,24};
}
static inline struct desktop_rect desktop_home_rect(u32 sw,u32 sh,u32 index) {
    u32 left=sw>=900?64:32,width=MIN(174u,(sw-2*left-24)/3);
    u32 top=(u32)MAX(76,((i32)sh-360)/2)+(sh>=600?154:116);
    return (struct desktop_rect){left+index*(width+12),top,width,78};
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
