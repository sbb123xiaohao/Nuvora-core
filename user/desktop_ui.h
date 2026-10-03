#ifndef NV_DESKTOP_UI_H
#define NV_DESKTOP_UI_H
#include <nv/gfx.h>
#include <nv/string.h>
#include "gui_text.h"
#include "text_window.h"
#include "desktop_shell.h"
#include "account_ui.h"
#include "ui_theme.h"
#include "files_model.h"

enum { DESKTOP_EDIT_NORMAL, DESKTOP_EDIT_PATH, DESKTOP_EDIT_CLOSE };
enum { DESKTOP_EDITOR_CELL=8, DESKTOP_EDITOR_LINE=19 };
enum { DESKTOP_FILE_NORMAL, DESKTOP_FILE_FOLDER, DESKTOP_FILE_RENAME,
       DESKTOP_FILE_DELETE };
struct desktop_view {
    const struct account_ui *account;
    const u32 *wallpaper;
    u32 theme,settings_tab,settings_confirm,settings_focus,idle_minutes,uptime_minutes;
    const char *system_memory, *system_free;
    const struct nv_net_info *network;
    bool network_present;
    const char *path, *message, *drive;
    const struct nv_dirent64 *entries;
    u32 count, selected, scroll, volumes, files_total, file_view, file_sort;
    const char *file_query;
    bool file_query_focus,show_hidden;
    u32 file_mode;
    const char *file_input, *file_target;
    bool pointer;
    u32 pointer_x, pointer_y;
    u32 shortcut_selected;
    bool audio_ready, menu, volume_open;
    u32 volume_percent;
    const struct desktop_launcher *launcher;
    const struct desktop_switcher *switcher;
    bool overview;
    u32 overview_selected, snap_preview;
    const struct desktop_window *windows;
    const u8 *order;
    u32 active, hover_kind, hover_window;
    const char *editor_path, *editor_text, *editor_input;
    u32 editor_length, editor_cursor, editor_scroll, editor_mode;
    bool editor_dirty;
    const char (*terminal_lines)[128];
    const char *terminal_input;
    u32 terminal_count, terminal_first;
};
static void desktop_size(char out[32], u64 size) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    u32 unit = 0; u64 divisor = 1;
    while (unit < 6 && size/divisor >= 1024) { divisor *= 1024; ++unit; }
    u64 whole = size/divisor;
    usize n = number64(out, whole, 10);
    if (unit && whole < 10 && size%divisor) {
        out[n++] = '.';
        out[n++] = (char)('0' + (size%divisor)*10/divisor);
        out[n] = 0;
    }
    strlcpy(out+n, units[unit], 32-n);
}
static const char *desktop_kind(const struct nv_dirent64 *entry) {
    if (entry->kind == NV_DIR) return "Folder";
    usize n = strlen(entry->name);
    if (n >= 5 && !files_name_compare(entry->name + n - 5, ".flac")) return "FLAC audio";
    if (n >= 4 && !files_name_compare(entry->name + n - 4, ".mp2")) return "MP2 audio";
    if (n >= 4 && !files_name_compare(entry->name + n - 4, ".wav")) return "WAV audio";
    if (n >= 4 && !files_name_compare(entry->name + n - 4, ".mp3")) return "MP3 audio";
    if (n >= 4 && !files_name_compare(entry->name + n - 4, ".mpg")) return "MPEG video";
    if (n >= 5 && !files_name_compare(entry->name + n - 5, ".mpeg")) return "MPEG video";
    if (n >= 4 && !files_name_compare(entry->name + n - 4, ".txt")) return "Text";
    if (n >= 3 && !files_name_compare(entry->name + n - 3, ".md")) return "Markdown";
    if (n >= 4 && !files_name_compare(entry->name + n - 4, ".nvd")) return "Folio document";
    return "File";
}
static u32 desktop_scale(u32 width, u32 height) {
    return width >= 3840 && height >= 2000 ? 4 :
           width >= 2880 && height >= 1620 ? 3 :
           width >= 1600 && height >= 900 ? 2 : 1;
}
static u32 desktop_pointer_axis(u32 current, i32 value, u32 extent, bool absolute) {
    if (!extent) return 0;
    if (absolute) return (u32)((u64)(u32)MAX(0, MIN(value, 32767)) * (extent - 1) / 32767u);
    i64 next = (i64)current + value;
    return next < 0 ? 0 : next >= extent ? extent - 1 : (u32)next;
}
struct files_layout { u32 left,top,width,height,columns,rows,cell_width,cell_height,inspector; };
static struct files_layout desktop_files_layout(const struct desktop_window *w,u32 list) {
    struct files_layout g={.left=134,.top=166,.inspector=w->w>=820 && w->h>=350?180:0};
    g.width=w->w>g.left+g.inspector+14?w->w-g.left-g.inspector-14:1;
    g.height=w->h>g.top+34?w->h-g.top-34:1;
    g.columns=list?1:MAX(1u,(g.width+10)/166);
    g.cell_height=list?40:108;
    g.rows=MAX(1u,g.height/g.cell_height);
    g.cell_width=(g.width+10)/g.columns-10;
    return g;
}
static struct desktop_rect desktop_file_rect(struct files_layout g,u32 offset) {
    return (struct desktop_rect){g.left+(offset%g.columns)*(g.cell_width+10),
        g.top+(offset/g.columns)*g.cell_height,g.cell_width,g.cell_height-8};
}
enum { DESKTOP_HIT_NONE, DESKTOP_HIT_START, DESKTOP_HIT_MENU,
       DESKTOP_HIT_TASK, DESKTOP_HIT_SHORTCUT, DESKTOP_HIT_TITLE,
       DESKTOP_HIT_MINIMIZE, DESKTOP_HIT_MAXIMIZE, DESKTOP_HIT_CLOSE,
       DESKTOP_HIT_RESIZE, DESKTOP_HIT_PLACE, DESKTOP_HIT_FILE,
       DESKTOP_HIT_FILE_NEW_FOLDER, DESKTOP_HIT_FILE_NEW_TEXT,
       DESKTOP_HIT_FILE_DIALOG,
       DESKTOP_HIT_EDITOR_SAVE, DESKTOP_HIT_EDITOR_NEW, DESKTOP_HIT_EDITOR_TEXT,
       DESKTOP_HIT_EDITOR_DIALOG, DESKTOP_HIT_TERMINAL, DESKTOP_HIT_MEDIA,
       DESKTOP_HIT_VOLUME, DESKTOP_HIT_VOLUME_SLIDER, DESKTOP_HIT_CLIENT,
       DESKTOP_HIT_OVERVIEW_BUTTON, DESKTOP_HIT_OVERVIEW, DESKTOP_HIT_OVERVIEW_PAGE,
       DESKTOP_HIT_SWITCH, DESKTOP_HIT_SEARCH, DESKTOP_HIT_ACCOUNT,
       DESKTOP_HIT_LOCK, DESKTOP_HIT_LOGOUT, DESKTOP_HIT_SETTINGS,
       DESKTOP_HIT_FILE_SEARCH, DESKTOP_HIT_FILE_VIEW, DESKTOP_HIT_FILE_SORT,
       DESKTOP_HIT_FILE_HIDDEN, DESKTOP_HIT_FILE_PARENT, DESKTOP_HIT_SETTINGS_VOLUME };
struct desktop_hit { u32 kind, index, window; };
enum { SETTINGS_APPEARANCE, SETTINGS_SOUND, SETTINGS_NETWORK, SETTINGS_SECURITY,
       SETTINGS_SYSTEM, SETTINGS_TABS, SETTINGS_ACTION=16,
       SETTINGS_CANCEL=48, SETTINGS_CONTINUE=49 };
static bool desktop_inside(struct desktop_rect r,u32 x,u32 y) {
    return x>=r.x && y>=r.y && x-r.x<r.w && y-r.y<r.h;
}
static u32 desktop_settings_choices(u32 tab) {
    return tab==SETTINGS_APPEARANCE?6:tab==SETTINGS_SECURITY?5:3;
}
static struct desktop_rect desktop_settings_rect(u32 w,u32 h,u32 tab,u32 action) {
    u32 aw=w>178?w-178:1,bw=aw/3;
    if (action==SETTINGS_CANCEL || action==SETTINGS_CONTINUE) {
        bw=MIN(160u,(w-40)/2);
        return (struct desktop_rect){24+(action-SETTINGS_CANCEL)*(bw+8),h-62,bw,28};
    }
    if (action<SETTINGS_ACTION) return (struct desktop_rect){12,70+action*44,118,34};
    u32 i=action-SETTINGS_ACTION;
    if (tab==SETTINGS_SYSTEM) return (struct desktop_rect){154,224+i*44,MIN(190u,aw),28};
    if (i<3) return (struct desktop_rect){154+i*bw,tab==SETTINGS_NETWORK?310:142,bw-7,32};
    if (tab==SETTINGS_APPEARANCE && i<5)
        return (struct desktop_rect){154+(i-3)*(aw/2),238,aw/2-7,30};
    return (struct desktop_rect){154,tab==SETTINGS_APPEARANCE?290:224+(i-3)*44,MIN(210u,aw),28};
}
static u32 desktop_tasks(const struct desktop_view *v) {
    u32 count=4;
    for (u32 i=4;i<DESKTOP_WINDOW_COUNT;++i) if (v->windows[i].open) ++count;
    return count;
}
static u32 desktop_task_id(const struct desktop_view *v,u32 index) {
    if (index<4) return index;
    for (u32 i=4;i<DESKTOP_WINDOW_COUNT;++i) if (v->windows[i].open && index--==4) return i;
    return DESKTOP_WINDOW_COUNT;
}
static u32 desktop_task_width(u32 sw,const struct desktop_view *v) {
    return MIN(60u,(sw-140-(sw>=640?70u:0u))/desktop_tasks(v));
}
static u32 desktop_dock_width(u32 sw,const struct desktop_view *v) {
    return 116+desktop_tasks(v)*desktop_task_width(sw,v)+(sw>=640?70:0)+8;
}
static u32 desktop_dock_x(u32 sw,const struct desktop_view *v) {
    return (sw-desktop_dock_width(sw,v))/2;
}
static struct desktop_hit desktop_hit(u32 width, u32 height,
                                      const struct desktop_view *v, u32 px, u32 py) {
    u32 s = desktop_scale(width, height), sh = height/s, sw=width/s;
    u32 x = px/s, y = py/s;
    if (v->switcher && v->switcher->open) {
        struct desktop_rect r=desktop_switch_rect(sw,sh);
        u32 first=v->switcher->selected/6*6, count=MIN(6u,v->switcher->count-first);
        for (u32 i=0;i<count;++i)
            if (x>=r.x+10 && x<r.x+184 && y>=r.y+38+i*30 && y<r.y+66+i*30)
                return (struct desktop_hit){DESKTOP_HIT_SWITCH,first+i,DESKTOP_WINDOW_COUNT};
        return (struct desktop_hit){DESKTOP_HIT_NONE,0,DESKTOP_WINDOW_COUNT};
    }
    if (y >= sh - DESKTOP_BAR_HEIGHT) {
        u32 dx=desktop_dock_x(sw,v),dw=desktop_dock_width(sw,v);
        if (y<sh-48 || y>=sh-8 || x<dx || x>=dx+dw)
            return (struct desktop_hit){DESKTOP_HIT_NONE,0,DESKTOP_WINDOW_COUNT};
        x-=dx;
        if (sw >= 640 && x >= dw-78) return (struct desktop_hit){DESKTOP_HIT_VOLUME,0,0};
        if (x >= 8 && x < 78) return (struct desktop_hit){DESKTOP_HIT_START, 0, 0};
        if (x>=82 && x<110) return (struct desktop_hit){DESKTOP_HIT_OVERVIEW_BUTTON,0,0};
        u32 task_w=desktop_task_width(sw,v);
        for (u32 i=0;i<desktop_tasks(v);++i)
            if (x>=116+i*task_w && x<116+(i+1)*task_w-2) {
                u32 id=desktop_task_id(v,i);
                return (struct desktop_hit){id==DESKTOP_MEDIA && !v->windows[id].open ?
                    DESKTOP_HIT_MEDIA : DESKTOP_HIT_TASK,id,id};
            }
        return (struct desktop_hit){DESKTOP_HIT_NONE, 0, 0};
    }
    if (v->overview) {
        u8 ids[DESKTOP_WINDOW_COUNT];
        u32 total=desktop_window_list(v->windows,v->order,ids),chosen=0;
        for (u32 i=0;i<total;++i) if (ids[i]==v->overview_selected) chosen=i;
        u32 page=desktop_overview_page_size(sw),first=chosen/page*page,count=MIN(page,total-first);
        for (u32 i=0;i<count;++i) {
            struct desktop_rect r=desktop_overview_rect(sw,sh,count,i);
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h)
                return (struct desktop_hit){DESKTOP_HIT_OVERVIEW,ids[first+i],DESKTOP_WINDOW_COUNT};
        }
        if (y>=sh-69 && y<sh-45 && total>page) {
            if (first && x>=sw-142 && x<sw-82) return (struct desktop_hit){DESKTOP_HIT_OVERVIEW_PAGE,0,DESKTOP_WINDOW_COUNT};
            if (first+count<total && x>=sw-78 && x<sw-18) return (struct desktop_hit){DESKTOP_HIT_OVERVIEW_PAGE,1,DESKTOP_WINDOW_COUNT};
        }
        return (struct desktop_hit){DESKTOP_HIT_NONE,0,DESKTOP_WINDOW_COUNT};
    }
    if (v->volume_open && sw >= 640) {
        if (x >= sw-174 && x < sw-18 && y >= sh-89 && y < sh-57)
            return (struct desktop_hit){DESKTOP_HIT_VOLUME_SLIDER,
                MIN(100u,(x-(sw-174))*100/155),0};
        if (x >= sw-193 && y >= sh-127 && y < sh-DESKTOP_BAR_HEIGHT)
            return (struct desktop_hit){DESKTOP_HIT_VOLUME,0,0};
    }
    if (v->menu) {
        struct desktop_rect r=desktop_menu_rect(sw,sh);
        if (x>=r.x+16 && x<r.x+r.w-16 && y>=r.y+48 && y<r.y+84)
            return (struct desktop_hit){DESKTOP_HIT_SEARCH,0,DESKTOP_WINDOW_COUNT};
        if (x>=r.x+8 && x<r.x+r.w-8) {
            for (u32 i=0;i<v->launcher->count;++i)
                if (y>=r.y+96+i*34 && y<r.y+128+i*34)
                    return (struct desktop_hit){DESKTOP_HIT_MENU,v->launcher->ids[i],DESKTOP_WINDOW_COUNT};
        }
        if (y>=r.y+r.h-42 && y<r.y+r.h-14 && v->account) {
            if (x>=r.x+12 && x<r.x+102) return (struct desktop_hit){DESKTOP_HIT_LOCK,0,0};
            if (x>=r.x+110 && x<r.x+210) return (struct desktop_hit){DESKTOP_HIT_LOGOUT,0,0};
        }
        return (struct desktop_hit){DESKTOP_HIT_NONE, 0, DESKTOP_WINDOW_COUNT};
    }
    for (i32 z = DESKTOP_WINDOW_COUNT - 1; z >= 0; --z) {
        u32 id = v->order[z];
        const struct desktop_window *w = &v->windows[id];
        if (!w->open || w->minimized || (i32)x < w->x || (i32)y < w->y ||
            (i32)x >= w->x + (i32)w->w || (i32)y >= w->y + (i32)w->h) continue;
        u32 rx = x - (u32)w->x, ry = y - (u32)w->y;
        if (!w->maximized) {
            u32 edges=(rx<4?1u:rx>=w->w-4?2u:0u) | (ry<4?4u:ry>=w->h-4?8u:0u);
            if (edges) return (struct desktop_hit){DESKTOP_HIT_RESIZE,edges,id};
        }
        if (ry < 28) {
            if (rx >= w->w - 28) return (struct desktop_hit){DESKTOP_HIT_CLOSE, 0, id};
            if (rx >= w->w - 56) return (struct desktop_hit){DESKTOP_HIT_MAXIMIZE, 0, id};
            if (rx >= w->w - 84) return (struct desktop_hit){DESKTOP_HIT_MINIMIZE, 0, id};
            return (struct desktop_hit){DESKTOP_HIT_TITLE, 0, id};
        }
        if (!w->maximized && rx >= w->w - 12 && ry >= w->h - 12)
            return (struct desktop_hit){DESKTOP_HIT_RESIZE, 10, id};
        if (w->client) return (struct desktop_hit){DESKTOP_HIT_CLIENT,0,id};
        if (id==DESKTOP_ACCOUNTS && v->account)
            return (struct desktop_hit){DESKTOP_HIT_ACCOUNT,
                account_ui_hit(w->w-2,w->h-29,v->account,rx-1,ry-28),id};
        if (id==DESKTOP_SETTINGS) {
            u32 cx=rx-1,cy=ry-28;
            if (v->settings_confirm) {
                for (u32 i=SETTINGS_CANCEL;i<=SETTINGS_CONTINUE;++i)
                    if (desktop_inside(desktop_settings_rect(w->w-2,w->h-29,0,i),cx,cy))
                        return (struct desktop_hit){DESKTOP_HIT_SETTINGS,i,id};
            } else {
                for (u32 i=0;i<SETTINGS_TABS;++i)
                    if (desktop_inside(desktop_settings_rect(w->w-2,w->h-29,0,i),cx,cy))
                        return (struct desktop_hit){DESKTOP_HIT_SETTINGS,i,id};
                if (v->settings_tab==SETTINGS_SOUND && v->audio_ready &&
                    cx>=154 && cx<w->w-23 && cy>=210 && cy<244)
                    return (struct desktop_hit){DESKTOP_HIT_SETTINGS_VOLUME,
                        MIN(100u,(cx-154)*100/(w->w-180)),id};
                for (u32 i=0;i<desktop_settings_choices(v->settings_tab);++i)
                    if (desktop_inside(desktop_settings_rect(w->w-2,w->h-29,v->settings_tab,SETTINGS_ACTION+i),cx,cy))
                        return (struct desktop_hit){DESKTOP_HIT_SETTINGS,SETTINGS_ACTION+i,id};
            }
            return (struct desktop_hit){DESKTOP_HIT_NONE,0,id};
        }
        if (id == DESKTOP_FILES) {
            if (v->file_mode != DESKTOP_FILE_NORMAL) {
                u32 top=MAX(40u,w->h/2-48);
                if (ry>=top+64 && ry<top+88) {
                    if (rx>=37 && rx<119)
                        return (struct desktop_hit){DESKTOP_HIT_FILE_DIALOG,1,id};
                    if (rx>=125 && rx<200)
                        return (struct desktop_hit){DESKTOP_HIT_FILE_DIALOG,2,id};
                }
                return (struct desktop_hit){DESKTOP_HIT_FILE_DIALOG,0,id};
            }
            if (ry>=35 && ry<58) {
                if (rx>=w->w-147 && rx<w->w-77)
                    return (struct desktop_hit){DESKTOP_HIT_FILE_NEW_FOLDER,0,id};
                if (rx>=w->w-74 && rx<w->w-7)
                    return (struct desktop_hit){DESKTOP_HIT_FILE_NEW_TEXT,0,id};
            }
            if (ry>=86 && ry<116) {
                if (rx>=134 && rx<w->w-92)
                    return (struct desktop_hit){DESKTOP_HIT_FILE_SEARCH,0,id};
                if (rx>=w->w-82 && rx<w->w-14)
                    return (struct desktop_hit){DESKTOP_HIT_FILE_VIEW,0,id};
            }
            if (ry>=126 && ry<152) {
                if (rx>=134 && rx<226) return (struct desktop_hit){DESKTOP_HIT_FILE_SORT,0,id};
                if (rx>=234 && rx<332) return (struct desktop_hit){DESKTOP_HIT_FILE_HIDDEN,0,id};
            }
            if (rx>=12 && rx<112 && ry>=w->h-48 && ry<w->h-22)
                return (struct desktop_hit){DESKTOP_HIT_FILE_PARENT,0,id};
            u32 spaces=MAX(1u,v->volumes);
            for (u32 i = 0; i < spaces + 3; ++i)
                if (rx >= 10 && rx < 122 && ry >= 94 + i*36 && ry < 124 + i*36 && ry<w->h-58)
                    return (struct desktop_hit){DESKTOP_HIT_PLACE,
                        i < spaces ? i : NV_VOLUME_MAX + i - spaces, id};
            struct files_layout g=desktop_files_layout(w,v->file_view);
            for (u32 i=0;i<g.rows*g.columns && i+v->scroll<v->count;++i)
                if (desktop_inside(desktop_file_rect(g,i),rx,ry) && ry<w->h-34)
                    return (struct desktop_hit){DESKTOP_HIT_FILE,v->scroll+i,id};
        } else if (id == DESKTOP_EDITOR) {
            if (v->editor_mode != DESKTOP_EDIT_NORMAL) {
                u32 top=MAX(66u,w->h/2-38);
                u32 button_y=top+(v->editor_mode==DESKTOP_EDIT_CLOSE?42u:61u);
                if (ry>=button_y && ry<button_y+
                    (v->editor_mode==DESKTOP_EDIT_CLOSE?28u:17u)) {
                    if (rx>=37 && rx<99)
                        return (struct desktop_hit){DESKTOP_HIT_EDITOR_DIALOG,1,id};
                    if (v->editor_mode==DESKTOP_EDIT_CLOSE && rx>=105 && rx<174)
                        return (struct desktop_hit){DESKTOP_HIT_EDITOR_DIALOG,2,id};
                    if (rx>=(v->editor_mode==DESKTOP_EDIT_CLOSE?180u:105u) &&
                        rx<(v->editor_mode==DESKTOP_EDIT_CLOSE?244u:169u))
                        return (struct desktop_hit){DESKTOP_HIT_EDITOR_DIALOG,3,id};
                }
                return (struct desktop_hit){DESKTOP_HIT_EDITOR_DIALOG,0,id};
            }
            if (ry >= 34 && ry < 60) {
                if (rx >= 12 && rx < 72) return (struct desktop_hit){DESKTOP_HIT_EDITOR_SAVE, 0, id};
                if (rx >= 80 && rx < 142) return (struct desktop_hit){DESKTOP_HIT_EDITOR_NEW, 0, id};
            }
            return (struct desktop_hit){DESKTOP_HIT_EDITOR_TEXT, 0, id};
        } else return (struct desktop_hit){DESKTOP_HIT_TERMINAL, 0, id};
        return (struct desktop_hit){DESKTOP_HIT_NONE, 0, id};
    }
    for (u32 i = 0; i < 4; ++i)
        if (x >= 14 && x < 110 && y >= 46 + i*78 && y < 118 + i*78)
            return (struct desktop_hit){DESKTOP_HIT_SHORTCUT, i, i};
    return (struct desktop_hit){DESKTOP_HIT_NONE, 0, 0};
}

static u32 desktop_chars(u32 pixels, u32 s) { return pixels/(6*s); }
static void desktop_ipv4(char out[24],u32 address) {
    if (!address) { strlcpy(out,"Not set",24);return; }
    u32 at=0;
    for (u32 i=0;i<4;++i) {
        if (i) out[at++]='.';
        at+=number(out+at,(address>>(24-i*8))&255u,10);
    }
}
static void desktop_settings_render(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 w,u32 h,u32 s,const struct desktop_view *v) {
    desktop_box(c,clip,x,y,w*s,h*s,0xf6f4ef);
    if (v->settings_confirm) {
        const char *title=v->settings_confirm==NV_CTL_REBOOT?"Restart this computer?":"Shut down this computer?";
        desktop_bold_text(c,clip,x+24*s,y+42*s,"ONE LAST CHECK",14,s,0x987650);
        desktop_bold_text(c,clip,x+24*s,y+87*s,title,strlen(title),s,0x284b47);
        desktop_label(c,clip,x+24*s,y+128*s,"Open apps will ask you to save before continuing.",(w-48)/6,s,0x75877b);
        desktop_label(c,clip,x+24*s,y+158*s,"Mounted data volumes will be saved first.",(w-48)/6,s,0x75877b);
        for (u32 i=0;i<2;++i) {
            struct desktop_rect r=desktop_settings_rect(w,h,0,SETTINGS_CANCEL+i);
            account_button(c,clip,x+r.x*s,y+r.y*s,r.w*s,i?"Continue":"Cancel",s,true,v->settings_focus==i,false);
        }
        return;
    }
    const char *tabs[]={"Appearance","Sound","Network","Security","System"};
    const char *headings[]={"Shape your space","Sound and focus","Connections","Your session","This machine"};
    desktop_box(c,clip,x,y,134*s,h*s,0x17383c);
    desktop_bold_text(c,clip,x+18*s,y+22*s,"NUVORA",6,s,0xd6eee5);
    desktop_text(c,clip,x+18*s,y+45*s,"Control room",12,s,0x8db4ae);
    for (u32 i=0;i<SETTINGS_TABS;++i) {
        struct desktop_rect r=desktop_settings_rect(w,h,0,i);
        if (v->settings_tab==i) desktop_round(c,clip,x+r.x*s,y+r.y*s,r.w*s,r.h*s,9*s,0x315c58);
        desktop_text(c,clip,x+24*s,y+(r.y+8)*s,tabs[i],strlen(tabs[i]),s,
            v->settings_tab==i?0xf0d2a9:0xb3cbc1);
    }
    u32 tab=MIN(v->settings_tab,(u32)SETTINGS_TABS-1),dx=x+154*s,available=(w-178)/6;
    desktop_bold_text(c,clip,dx,y+28*s,headings[tab],strlen(headings[tab]),s,0x284b47);
    const char *sub[]={"A workspace that feels like yours.","Set the level for your output.",
        "Your current network, at a glance.","Keep your workspace private.","Live information and power controls."};
    desktop_label(c,clip,dx,y+62*s,sub[tab],available,s,0x7f8e80);
    bool admin=v->account && v->account->info.role==NV_ACCOUNT_ADMIN;
    const char *buttons[6]={0};bool enabled[6]={true,true,true,true,true,true};
    if (tab==SETTINGS_APPEARANCE || tab==SETTINGS_SECURITY) {
        desktop_text(c,clip,dx,y+111*s,tab==SETTINGS_APPEARANCE?"Wallpaper":"Lock when idle",tab==SETTINGS_APPEARANCE?9:14,s,0x5e7768);
        const char *names[2][3]={{"Aurora","Ocean","Dusk"},{"1 min","5 min","15 min"}};
        for (u32 i=0;i<3;++i) buttons[i]=names[tab==SETTINGS_SECURITY][i];
        if (tab==SETTINGS_APPEARANCE) {
            desktop_text(c,clip,dx,y+202*s,"Files / default view",20,s,0x5e7768);
            buttons[3]="Space cards";buttons[4]="Compact list";
            buttons[5]=v->show_hidden?"Hidden files: on":"Hidden files: off";
            desktop_label(c,clip,dx,y+344*s,"Choices follow your account.",available,s,0x7f8e80);
        } else {
            buttons[3]="Lock now";buttons[4]="Manage accounts";
            desktop_label(c,clip,dx,y+320*s,"A password is required to unlock.",available,s,0x7f8e80);
            desktop_label(c,clip,dx,y+345*s,"System changes require an administrator.",available,s,0x7f8e80);
        }
    } else if (tab==SETTINGS_SOUND) {
        desktop_text(c,clip,dx,y+111*s,v->audio_ready?"Output volume":"No audio output",v->audio_ready?13:15,s,0x5e7768);
        buttons[0]="Quiet -";buttons[1]=v->volume_percent?"Mute":"Unmute";buttons[2]="Louder +";
        enabled[0]=enabled[1]=enabled[2]=v->audio_ready;
        u32 aw=w-178,level=MIN(v->volume_percent,100u);
        desktop_round(c,clip,dx,y+220*s,aw*s,7*s,3*s,0xd6dfd2);
        if (level) desktop_round(c,clip,dx,y+220*s,MAX(1u,aw*level/100)*s,7*s,3*s,0x6eaa92);
        desktop_round(c,clip,dx+((aw-12)*level/100)*s,y+213*s,12*s,21*s,5*s,v->audio_ready?0x2e6861:0xaebdb0);
        char text[24];number(text,level,10);u32 at=strlen(text);strlcpy(text+at,"%",sizeof(text)-at);
        desktop_bold_text(c,clip,dx,y+260*s,text,strlen(text),s,0x345b4f);
        desktop_label(c,clip,dx,y+301*s,v->audio_ready?"Changes apply to playing media immediately.":"Connect a supported HDA output to adjust sound.",available,s,0x7f8e80);
        desktop_label(c,clip,dx,y+334*s,"Volume is saved with your preferences.",available,s,0x7f8e80);
    } else if (tab==SETTINGS_NETWORK) {
        const struct nv_net_info *n=v->network;
        const char *status=!v->network_present?"No network adapter":n->state==NV_NET_ONLINE?"IPv4 connected":
            n->state==NV_NET_CONFIGURING?"Requesting an address":n->state==NV_NET_LINK?"Link ready":
            n->state==NV_NET_DOWN?"Cable / link disconnected":"Driver unavailable";
        desktop_round(c,clip,dx,y+98*s,(w-178)*s,35*s,9*s,0xe5ebde);
        desktop_label(c,clip,dx+12*s,y+107*s,status,available-4,s,0x436552);
        if (v->network_present) {
            const char *labels[]={"Address","Mask","Router","DNS"};
            u32 values[]={n->ip,n->mask,n->gateway,n->dns};
            for (u32 i=0;i<4;++i) {
                char value[24];desktop_ipv4(value,values[i]);
                desktop_text(c,clip,dx,y+(150+i*31)*s,labels[i],strlen(labels[i]),s,0x819181);
                desktop_label(c,clip,dx+88*s,y+(150+i*31)*s,value,available>15?available-15:0,s,0x2d5148);
            }
            char identity[48]="Adapter ";u32 at=8;at+=number(identity+at,n->index+1,10);
            strlcpy(identity+at,n->type==NV_NET_USB_BRIDGE?" / USB":n->type==NV_NET_WIFI?" / Wi-Fi":" / Ethernet",sizeof(identity)-at);
            desktop_label(c,clip,dx,y+280*s,identity,available,s,0x819181);
        } else {
            desktop_label(c,clip,dx,y+157*s,"Connect a supported wired or USB adapter.",available,s,0x819181);
            desktop_label(c,clip,dx,y+200*s,"Status updates automatically.",available,s,0x819181);
        }
        buttons[0]="Next adapter";buttons[1]="Get address";buttons[2]="Refresh";
        enabled[0]=v->network_present;
        enabled[1]=admin && v->network_present && n->state>=NV_NET_LINK;
        if (!admin) desktop_label(c,clip,dx,y+356*s,"Only an administrator can change the connection.",available,s,0x819181);
    } else {
        desktop_label(c,clip,dx,y+104*s,"Nuvora Core " NV_VERSION " / x86_64",available,s,0x5e7768);
        desktop_label(c,clip,dx,y+137*s,v->system_memory?v->system_memory:"Memory information unavailable",available,s,0x5e7768);
        desktop_label(c,clip,dx,y+167*s,v->system_free?v->system_free:"",available,s,0x819181);
        char uptime[40];number(uptime,v->uptime_minutes,10);u32 at=strlen(uptime);
        strlcpy(uptime+at," min up / ",sizeof(uptime)-at);at=strlen(uptime);
        strlcpy(uptime+at,v->drive?v->drive:"RAM only",sizeof(uptime)-at);
        desktop_label(c,clip,dx,y+195*s,uptime,available,s,0x819181);
        buttons[0]="Save volumes";buttons[1]="Restart";buttons[2]="Shut down";
        enabled[1]=enabled[2]=admin;
        if (!admin) desktop_label(c,clip,dx,y+356*s,"Ask an administrator to restart or shut down.",available,s,0x819181);
    }
    for (u32 i=0;i<desktop_settings_choices(tab);++i) {
        struct desktop_rect r=desktop_settings_rect(w,h,tab,SETTINGS_ACTION+i);
        bool selected=tab==SETTINGS_APPEARANCE?(i<3?v->theme==i:i<5?v->file_view==i-3:false):
            tab==SETTINGS_SECURITY && i<3?v->idle_minutes==(i==0?1u:i==1?5u:15u):false;
        account_button(c,clip,x+r.x*s,y+r.y*s,r.w*s,buttons[i],s,enabled[i],selected || v->settings_focus==i,false);
    }
}

#include "files_ui.h"

static void desktop_window_render(struct nv_canvas *c, u32 s,
                                  const struct desktop_view *v, u32 id,
                                  struct desktop_clip repaint) {
    const struct desktop_window *w = &v->windows[id];
    u32 x = (u32)w->x*s, y = (u32)w->y*s, width = w->w*s, height = w->h*s;
    struct desktop_clip screen = repaint;
    struct desktop_clip clip = {MAX(x+s,repaint.left),MAX(y+s,repaint.top),
        MIN(x+width-s,repaint.right),MIN(y+height-s,repaint.bottom)};
    bool focus = v->active == id;
    desktop_shadow(c,screen,(i32)x,(i32)y,width,height,s);
    desktop_round(c,screen,x,y,width,height,9*s,focus ? 0xafc2e5 : 0xb9c5d9);
    desktop_box(c,clip,x+s,y+12*s,width-2*s,height-13*s,0xfaf9f5);
    desktop_round(c,screen,x+s,y+s,width-2*s,27*s,8*s,focus ? 0xf2f5fc : 0xe6ebf5);
    desktop_box(c,clip,x+s,y+12*s,width-2*s,16*s,focus ? 0xf2f5fc : 0xe6ebf5);
    desktop_label(c,clip,x+14*s,y+6*s,desktop_window_name(w,id),desktop_chars(width-102*s,s),s,focus?0x283752:0x748198);
    for (u32 i = 0; i < 3; ++i) {
        u32 bx = x + width - (84 - i*28)*s;
        u32 kind=i==0?DESKTOP_HIT_MINIMIZE:i==1?DESKTOP_HIT_MAXIMIZE:DESKTOP_HIT_CLOSE;
        bool hover=v->hover_window==id && v->hover_kind==kind;
        desktop_round(c,clip,bx+2*s,y+3*s,24*s,22*s,5*s,i==2 && hover ? 0xf8dfe7 :
                    hover?0xdce6fa:focus?0xf2f5fc:0xe6ebf5);
        u32 mark_color=i==2 && hover?0xb44764:0x596982;
        if (i==0) desktop_box(c,clip,bx+9*s,y+17*s,10*s,s,mark_color);
        else if (i==1) {
            if (w->maximized) {
                desktop_box(c,clip,bx+11*s,y+8*s,9*s,s,mark_color);
                desktop_box(c,clip,bx+19*s,y+8*s,s,8*s,mark_color);
                desktop_box(c,clip,bx+8*s,y+11*s,10*s,s,mark_color);
                desktop_box(c,clip,bx+8*s,y+11*s,s,8*s,mark_color);
                desktop_box(c,clip,bx+8*s,y+18*s,10*s,s,mark_color);
            } else {
                desktop_box(c,clip,bx+9*s,y+9*s,10*s,s,mark_color);
                desktop_box(c,clip,bx+9*s,y+9*s,s,9*s,mark_color);
                desktop_box(c,clip,bx+18*s,y+9*s,s,9*s,mark_color);
                desktop_box(c,clip,bx+9*s,y+17*s,10*s,s,mark_color);
            }
        } else for (u32 mark=0;mark<9;++mark) {
            desktop_box(c,clip,bx+(9+mark)*s,y+(9+mark)*s,s,s,mark_color);
            desktop_box(c,clip,bx+(17-mark)*s,y+(9+mark)*s,s,s,mark_color);
        }
    }
    if (w->client) {
        u32 left=x+s,top=y+28*s,cw=width-2*s,ch=height-29*s;
        desktop_box(c,clip,left,top,cw,ch,0x19252b);
        if (w->text) gui_surface_render(c,clip,left,top,cw,ch,w->text);
        else if (w->pixels && w->pixel_width && w->pixel_height) {
            u32 start=MAX(MAX(top,clip.top),c->y0),end=MIN(MIN(top+ch,clip.bottom),c->y0+c->rows);
            u32 begin=MAX(left,clip.left),finish=MIN(left+cw,clip.right);
            for (u32 py=start;py<end;++py) {
                u32 sy=(u32)((u64)(py-top)*w->pixel_height/ch);
                u32 *dst=c->pixels+(py-c->y0)*c->width;
                const u32 *src=w->pixels+(uptr)sy*w->pixel_width;
                if (begin>=finish) continue;
                if (cw==w->pixel_width && c->format==NV_DISPLAY_BGRX8)
                    memcpy(dst+begin,src+begin-left,(finish-begin)*4);
                else for (u32 px=begin;px<finish;++px)
                    dst[px]=nv_display_rgb(c->format,
                        src[(u32)((u64)(px-left)*w->pixel_width/cw)]);
            }
        } else desktop_text(c,clip,left+16*s,top+16*s,"Opening...",10,s,0xd9e2e2);
        if (!w->maximized) for (u32 i=0;i<3;++i) {
            desktop_box(c,clip,x+width-(4+i*3)*s,y+height-3*s,s,s,0x7a939a);
            desktop_box(c,clip,x+width-3*s,y+height-(4+i*3)*s,s,s,0x7a939a);
        }
        return;
    }
    if (id==DESKTOP_ACCOUNTS && v->account) {
        account_ui_render(c,clip,x+s,y+28*s,w->w-2,w->h-29,s,v->account);
    } else if (id==DESKTOP_SETTINGS) {
        desktop_settings_render(c,clip,x+s,y+28*s,w->w-2,w->h-29,s,v);
    } else if (id == DESKTOP_FILES) {
        desktop_files_render(c,clip,x,y,s,v);
        if (v->file_mode != DESKTOP_FILE_NORMAL) {
            u32 dx=x+25*s,dy=y+MAX(40u,w->h/2-48)*s,dw=width-50*s;
            desktop_box(c,clip,dx+3*s,dy+4*s,dw,96*s,0x80929a);
            desktop_box(c,clip,dx,dy,dw,96*s,0xf9f7f2);
            desktop_box(c,clip,dx,dy,dw,4*s,0x31647a);
            const char *title=v->file_mode==DESKTOP_FILE_FOLDER?"New folder":
                v->file_mode==DESKTOP_FILE_RENAME?"Rename item":"Delete item?";
            desktop_text(c,clip,dx+12*s,dy+14*s,title,strlen(title),s,0x263d48);
            if (v->file_mode==DESKTOP_FILE_DELETE)
                desktop_label(c,clip,dx+12*s,dy+39*s,v->file_target,
                              desktop_chars(dw-28*s,s),s,0x3b5360);
            else {
                desktop_box(c,clip,dx+10*s,dy+35*s,dw-20*s,25*s,0xffffff);
                desktop_label(c,clip,dx+15*s,dy+44*s,v->file_input,
                              desktop_chars(dw-30*s,s),s,0x263d48);
            }
            desktop_box(c,clip,dx+12*s,dy+64*s,82*s,24*s,
                        v->file_mode==DESKTOP_FILE_DELETE?0xa14b40:0x31647a);
            desktop_box(c,clip,dx+100*s,dy+64*s,75*s,24*s,0xe4e9e8);
            const char *verb=v->file_mode==DESKTOP_FILE_FOLDER?"Create":
                v->file_mode==DESKTOP_FILE_RENAME?"Rename":"Delete";
            desktop_text(c,clip,dx+21*s,dy+72*s,verb,strlen(verb),s,0xffffff);
            desktop_text(c,clip,dx+110*s,dy+72*s,"Cancel",6,s,0x304953);
            if (v->file_mode==DESKTOP_FILE_DELETE)
                desktop_text(c,clip,dx+12*s,dy+51*s,"Cannot be undone. Press D to delete.",36,s,0x8f493e);
        }
    } else if (id == DESKTOP_EDITOR) {
        desktop_box(c,clip,x+s,y+28*s,width-2*s,36*s,0xe8eded);
        desktop_box(c,clip,x+12*s,y+34*s,60*s,24*s,0x31647a);
        desktop_box(c,clip,x+80*s,y+34*s,62*s,24*s,0xd9e3e6);
        desktop_text(c,clip,x+25*s,y+42*s,"Save",4,s,0xffffff);
        desktop_text(c,clip,x+97*s,y+42*s,"New",3,s,0x27404b);
        desktop_label(c,clip,x+152*s,y+42*s,v->editor_path,
                      desktop_chars(width-170*s,s),s,0x576a71);
        u32 cols = MAX(1u,(width-36*s)/(DESKTOP_EDITOR_CELL*s)), maxrows = (w->h-94)/DESKTOP_EDITOR_LINE;
        u32 row = 0, col = 0, caret_row = 0, caret_col = 0;
        for (u32 i = 0; i <= v->editor_length; ++i) {
            if (i == v->editor_cursor) { caret_row = row; caret_col = col; }
            if (i == v->editor_length) break;
            char ch = v->editor_text[i];
            if (ch == '\n') { ++row; col=0; continue; }
            if (row >= v->editor_scroll && row-v->editor_scroll < maxrows && col < cols &&
                (u8)ch >= 32 && (u8)ch < 127)
                desktop_editor_text(c,clip,x+(19*s)+col*DESKTOP_EDITOR_CELL*s,
                             y+(73+DESKTOP_EDITOR_LINE*(row-v->editor_scroll))*s,&v->editor_text[i],1,s,0x263a43);
            if (++col >= cols) { ++row; col=0; }
        }
        if (focus && v->editor_mode == DESKTOP_EDIT_NORMAL &&
            caret_row >= v->editor_scroll && caret_row-v->editor_scroll < maxrows)
            desktop_box(c,clip,x+19*s+caret_col*DESKTOP_EDITOR_CELL*s,
                        y+(75+DESKTOP_EDITOR_LINE*(caret_row-v->editor_scroll))*s,s,14*s,0x21647e);
        desktop_box(c,clip,x+s,y+height-25*s,width-2*s,24*s,0xf0f2f1);
        desktop_text(c,clip,x+12*s,y+height-18*s,v->editor_dirty ? "Unsaved" : "Saved",
                     v->editor_dirty ? 7 : 5,s,v->editor_dirty ? 0x9a563b : 0x526871);
        desktop_text(c,clip,x+99*s,y+height-18*s,"Ctrl-S Save  Ctrl-N New",23,s,0x5e7077);
        if (v->editor_mode != DESKTOP_EDIT_NORMAL) {
            u32 dx=x+25*s, dy=y+MAX(66u,w->h/2-38)*s, dw=width-50*s;
            desktop_box(c,clip,dx+3*s,dy+4*s,dw,82*s,0x80929a);
            desktop_box(c,clip,dx,dy,dw,82*s,0xf9f7f2);
            desktop_box(c,clip,dx,dy,dw,4*s,0x31647a);
            if (v->editor_mode == DESKTOP_EDIT_PATH) {
                desktop_text(c,clip,dx+12*s,dy+13*s,"Save document as",16,s,0x263d48);
                desktop_box(c,clip,dx+10*s,dy+35*s,dw-20*s,23*s,0xffffff);
                desktop_label(c,clip,dx+15*s,dy+43*s,v->editor_input,
                              desktop_chars(dw-30*s,s),s,0x263d48);
                desktop_box(c,clip,dx+12*s,dy+61*s,62*s,17*s,0x31647a);
                desktop_box(c,clip,dx+80*s,dy+61*s,64*s,17*s,0xe4e9e8);
                desktop_text(c,clip,dx+24*s,dy+66*s,"Save",4,s,0xffffff);
                desktop_text(c,clip,dx+91*s,dy+66*s,"Cancel",6,s,0x304953);
            } else {
                desktop_text(c,clip,dx+12*s,dy+13*s,"Unsaved changes",15,s,0x263d48);
                desktop_text(c,clip,dx+12*s,dy+29*s,"Save before closing?",20,s,0x425b66);
                desktop_box(c,clip,dx+12*s,dy+42*s,62*s,28*s,0x31647a);
                desktop_box(c,clip,dx+80*s,dy+42*s,69*s,28*s,0xe4e9e8);
                desktop_box(c,clip,dx+155*s,dy+42*s,64*s,28*s,0xe4e9e8);
                desktop_text(c,clip,dx+24*s,dy+52*s,"Save",4,s,0xffffff);
                desktop_text(c,clip,dx+91*s,dy+52*s,"Discard",7,s,0x304953);
                desktop_text(c,clip,dx+164*s,dy+52*s,"Cancel",6,s,0x304953);
            }
        }
    } else {
        desktop_box(c,clip,x+s,y+28*s,width-2*s,height-29*s,0x142a35);
        u32 maxrows = (w->h-76)/13, begin = v->terminal_count > maxrows ?
                         v->terminal_count-maxrows : 0;
        for (u32 row = begin; row < v->terminal_count; ++row) {
            const char *line = v->terminal_lines[(v->terminal_first+row)%64];
            desktop_mono_label(c,clip,x+13*s,y+(39+(row-begin)*13)*s,line,
                          desktop_chars(width-28*s,s),s,0xdce7e6);
        }
        desktop_box(c,clip,x+8*s,y+height-34*s,width-16*s,25*s,0x24414e);
        desktop_text(c,clip,x+13*s,y+height-26*s,">",1,s,0x8ac5c3);
        u32 input_cols=desktop_chars(width-42*s,s), input_length=strlen(v->terminal_input);
        u32 input_start=input_length>input_cols?input_length-input_cols:0;
        desktop_mono_label(c,clip,x+27*s,y+height-26*s,v->terminal_input+input_start,
                      input_cols,s,0xffffff);
        if (focus) desktop_box(c,clip,x+27*s+(input_length-input_start)*6*s,
                               y+height-27*s,s,10*s,0x8ac5c3);
    }
    if (!w->maximized) desktop_box(c,clip,x+width-8*s,y+height-8*s,6*s,6*s,0x8ca1a8);
}

/* Sample the existing compositor a scanline at a time. These are real window
 * previews, including built-in apps and terminal surfaces, with no second
 * full-size allocation and no framebuffer readback. */
static u32 desktop_preview_row[8192];
static void desktop_preview(struct nv_canvas *c, struct desktop_clip clip, u32 s,
                             const struct desktop_view *v, u32 id,
                             struct desktop_rect rect) {
    const struct desktop_window *w=&v->windows[id];
    if (!w->w || !w->h || !rect.w || !rect.h) return;
    u32 width=w->w*s,height=w->h*s,tw=rect.w*s,th=rect.h*s;
    if ((u64)tw*height>(u64)th*width) tw=(u32)((u64)th*width/height);
    else th=(u32)((u64)tw*height/width);
    if (!tw || !th || width>ARRAY_LEN(desktop_preview_row)) return;
    u32 x=rect.x*s+(rect.w*s-tw)/2,y=rect.y*s+(rect.h*s-th)/2;
    u32 top=MAX(MAX(y,clip.top),c->y0),bottom=MIN(MIN(y+th,clip.bottom),c->y0+c->rows);
    u32 left=MAX(x,clip.left),right=MIN(x+tw,clip.right);
    if (left>=right) return;
    struct desktop_window copy[DESKTOP_WINDOW_COUNT];
    memcpy(copy,v->windows,sizeof(copy)); copy[id].x=copy[id].y=0;
    struct desktop_view sample=*v; sample.windows=copy;
    sample.hover_kind=DESKTOP_HIT_NONE;
    for (u32 py=top;py<bottom;++py) {
        u32 source_y=(u32)((u64)(py-y)*height/th);
        struct nv_canvas row={desktop_preview_row,width,source_y,1,c->format};
        struct desktop_clip row_clip={0,source_y,width,source_y+1};
        for (u32 px=0;px<width;++px) desktop_preview_row[px]=nv_display_rgb(c->format,0x142b35);
        desktop_window_render(&row,s,&sample,id,row_clip);
        u32 *dst=c->pixels+(py-c->y0)*c->width;
        for (u32 px=left;px<right;++px)
            dst[px]=desktop_preview_row[(u32)((u64)(px-x)*width/tw)];
    }
}
static void desktop_overview_render(struct nv_canvas *c, struct desktop_clip clip,
                                    u32 s, u32 sw, u32 sh, const struct desktop_view *v) {
    u8 ids[DESKTOP_WINDOW_COUNT];
    u32 total=desktop_window_list(v->windows,v->order,ids),chosen=0;
    for (u32 i=0;i<total;++i) if (ids[i]==v->overview_selected) chosen=i;
    u32 page=desktop_overview_page_size(sw),first=chosen/page*page,count=MIN(page,total-first);
    desktop_box(c,clip,0,0,c->width,(sh-DESKTOP_BAR_HEIGHT)*s,0x192744);
    desktop_text(c,clip,24*s,20*s,"Windows",7,s,0xf5f3eb);
    char label[48]; number(label,total,10);
    strlcpy(label+strlen(label),total==1?" open window":" open windows",sizeof(label)-strlen(label));
    desktop_text(c,clip,90*s,20*s,label,strlen(label),s,0xacc5c8);
    if (total>page) {
        strlcpy(label,"Page ",sizeof(label)); number(label+5,first/page+1,10);
        u32 at=strlen(label); label[at++]='/'; number(label+at,(total+page-1)/page,10);
        desktop_text(c,clip,(sw-96)*s,20*s,label,strlen(label),s,0xacc5c8);
    }
    if (!total) {
        desktop_text(c,clip,24*s,80*s,"No open windows",15,s,0xdbe5e1);
        desktop_text(c,clip,24*s,104*s,"Open an app from Start.",23,s,0x9fb9bd);
    }
    for (u32 i=0;i<count;++i) {
        u32 id=ids[first+i]; const struct desktop_window *w=&v->windows[id];
        struct desktop_rect r=desktop_overview_rect(sw,sh,count,i);
        bool selected=id==v->overview_selected;
        desktop_box(c,clip,r.x*s,r.y*s,r.w*s,r.h*s,selected?0x91b2f4:0x5d7098);
        desktop_box(c,clip,(r.x+2)*s,(r.y+2)*s,(r.w-4)*s,(r.h-4)*s,0x243650);
        desktop_label(c,clip,(r.x+10)*s,(r.y+7)*s,desktop_window_name(w,id),(r.w-20)/6,s,0xf4f2eb);
        struct desktop_rect image={r.x+8,r.y+28,r.w-16,r.h-36};
        desktop_box(c,clip,image.x*s,image.y*s,image.w*s,image.h*s,0x142b35);
        desktop_preview(c,clip,s,v,id,image);
        if (w->minimized) {
            desktop_box(c,clip,(r.x+9)*s,(r.y+r.h-23)*s,61*s,14*s,0x244755);
            desktop_text(c,clip,(r.x+11)*s,(r.y+r.h-23)*s,"Minimized",9,s,0xe3ebe6);
        }
    }
    desktop_text(c,clip,24*s,(sh-66)*s,"Enter: switch   Esc: return",27,s,0xb7ccce);
    if (total>page) {
        desktop_box(c,clip,(sw-142)*s,(sh-69)*s,60*s,24*s,first?0x3c5c67:0x294650);
        desktop_box(c,clip,(sw-78)*s,(sh-69)*s,60*s,24*s,first+count<total?0x3c5c67:0x294650);
        desktop_text(c,clip,(sw-134)*s,(sh-64)*s,"Prev",4,s,0xe9efea);
        desktop_text(c,clip,(sw-70)*s,(sh-64)*s,"Next",4,s,0xe9efea);
    }
}
static void desktop_switch_render(struct nv_canvas *c, struct desktop_clip clip,
                                  u32 s, u32 sw, u32 sh, const struct desktop_view *v) {
    const struct desktop_switcher *state=v->switcher;
    struct desktop_rect r=desktop_switch_rect(sw,sh);
    u32 first=state->selected/6*6,count=MIN(6u,state->count-first);
    desktop_box(c,clip,r.x*s,r.y*s,r.w*s,r.h*s,0x9bafd3);
    desktop_box(c,clip,(r.x+2)*s,(r.y+2)*s,(r.w-4)*s,(r.h-4)*s,0xf5f8ff);
    desktop_text(c,clip,(r.x+12)*s,(r.y+12)*s,"Switch window",13,s,0x2e4b57);
    for (u32 i=0;i<count;++i) {
        u32 index=first+i,id=state->ids[index],y=r.y+38+i*30;
        if (index==state->selected) {
            desktop_box(c,clip,(r.x+10)*s,y*s,174*s,28*s,0xe0e9fc);
            desktop_box(c,clip,(r.x+10)*s,y*s,3*s,28*s,0x6e90d9);
        }
        desktop_label(c,clip,(r.x+20)*s,(y+7)*s,desktop_window_name(&v->windows[id],id),26,s,0x294650);
    }
    u32 id=state->ids[state->selected];
    struct desktop_rect image={r.x+198,r.y+42,r.w-212,r.h-84};
    desktop_box(c,clip,image.x*s,image.y*s,image.w*s,image.h*s,0x192744);
    desktop_preview(c,clip,s,v,id,image);
    desktop_text(c,clip,(r.x+12)*s,(r.y+r.h-23)*s,"Release Alt to switch   Esc: cancel",35,s,0x58717a);
}
static void desktop_pointer_render(struct nv_canvas *c,struct desktop_clip screen,u32 s,
                                   const struct desktop_view *v) {
    if (!v->pointer) return;
    for (u32 i=0;i<10;++i) {
        u32 width=(i<7?i+1:4)*s;
        desktop_box(c,screen,v->pointer_x,v->pointer_y+i*s,width,s,0x10242e);
        if (i>1 && i<7)
            desktop_box(c,screen,v->pointer_x+s,v->pointer_y+i*s,(i-1)*s,s,0xffffff);
    }
}
static void desktop_background(struct nv_canvas *c,struct desktop_clip clip,u32 height,
    const struct desktop_view *v,bool dim) {
    if (!v->wallpaper) { desktop_wallpaper(c,clip,height,v->theme,dim);return; }
    u32 left=MIN(clip.left,c->width),right=MIN(clip.right,c->width);
    u32 top=MAX(clip.top,c->y0),bottom=MIN(MIN(clip.bottom,height),c->y0+c->rows);
    if (left>=right) return;
    u32 shade=nv_display_rgb(c->format,0x0b1123);
    for (u32 py=top;py<bottom;++py) {
        u32 *out=c->pixels+(usize)(py-c->y0)*c->width;
        const u32 *src=v->wallpaper+(usize)py*c->width;
        if (!dim) memcpy(out+left,src+left,(right-left)*4);
        else for (u32 px=left;px<right;++px) out[px]=desktop_blend(shade,src[px],75);
    }
}
static void desktop_render_clip(struct nv_canvas *c, u32 height,
                                const struct desktop_view *v,
                                struct desktop_clip repaint) {
    u32 s=desktop_scale(c->width,height), sw=c->width/s, sh=height/s;
    struct desktop_clip screen=repaint;
    if (v->account && v->account->gate) {
        const struct account_ui *a=v->account;
        struct account_rect r=account_gate_rect(sw,sh,a);
        desktop_background(c,screen,height,v,true);
        desktop_bold_text(c,screen,28*s,20*s,"Nuvora",6,s,0xe5edff);
        desktop_text(c,screen,28*s,45*s,"Your personal workspace",23,s,0xa5b7d7);
        desktop_shadow(c,screen,r.x*s,r.y*s,r.w*s,r.h*s,s);
        account_ui_render(c,screen,r.x*s,r.y*s,r.w,r.h,s,a);
        desktop_text(c,screen,24*s,(sh-29)*s,a->info.flags&NV_AUTH_LOCKED?
            "Session locked":"Local sign-in",a->info.flags&NV_AUTH_LOCKED?14:13,s,0x9fb6bf);
        desktop_pointer_render(c,screen,s,v);return;
    }
    desktop_background(c,screen,height,v,false);
    desktop_round_alpha(c,screen,12*s,8*s,(sw-24)*s,30*s,12*s,0x101b32,140);
    desktop_bold_text(c,screen,27*s,13*s,"Nuvora",6,s,0xe7eeff);
    desktop_text(c,screen,102*s,14*s,"Workspace",9,s,0xb5c7e6);
    char uptime[32]="Up ";number(uptime+3,v->uptime_minutes,10);
    strlcpy(uptime+strlen(uptime)," min",sizeof(uptime)-strlen(uptime));
    desktop_label(c,screen,(sw-116)*s,14*s,uptime,16,s,0xb5c7e6);
    if (sw>=800 && v->account) {
        const struct nv_account_info *a=&v->account->info;
        u32 cx=(sw-280)*s,cy=82*s;
        desktop_round_alpha(c,screen,cx,cy,246*s,139*s,16*s,0x15203b,145);
        desktop_label(c,screen,cx+20*s,cy+18*s,a->display_name,34,s,0xe7eeff);
        desktop_text(c,screen,cx+20*s,cy+49*s,a->role==1?"Administrator":"Local account",13,s,0xb3c7e4);
        desktop_text(c,screen,cx+20*s,cy+82*s,"Win + L to lock",15,s,0xb3c7e4);
        desktop_text(c,screen,cx+20*s,cy+107*s,a->flags&NV_AUTH_PERSISTENT?"Files saved to drive":"Temporary session",
            a->flags&NV_AUTH_PERSISTENT?24:17,s,0xb3c7e4);
    }
    const char *shortcuts[]={"Files","Text Editor","Terminal","Media"};
    for (u32 i=0;i<4;++i) {
        u32 top=46+i*78, icon_x=37, icon_y=top+4;
        if (v->shortcut_selected==i) {
            desktop_round_alpha(c,screen,14*s,top*s,96*s,72*s,13*s,0x9cb9ed,65);
        }
        desktop_shadow(c,screen,icon_x*s,icon_y*s,44*s,44*s,s);
        desktop_icon(c,screen,icon_x*s,icon_y*s,40*s,i);
        u32 length=strlen(shortcuts[i]);
        desktop_text(c,screen,57*s-desktop_text_width(shortcuts[i],length,s)/2,(top+50)*s,
                     shortcuts[i],length,s,0xe7eeff);
    }
    for (u32 z=0;z<DESKTOP_WINDOW_COUNT;++z) {
        u32 id=v->order[z];
        if (!v->overview && v->windows[id].open && !v->windows[id].minimized)
            desktop_window_render(c,s,v,id,screen);
    }
    if (v->overview) desktop_overview_render(c,screen,s,sw,sh,v);
    if (v->snap_preview) {
        u32 x=v->snap_preview==DESKTOP_TILE_RIGHT?sw/2:0;
        u32 width=v->snap_preview==DESKTOP_TILE_MAX?sw:v->snap_preview==DESKTOP_TILE_LEFT?sw/2:sw-sw/2;
        desktop_box(c,screen,x*s,0,width*s,2*s,0x91b2f4);
        desktop_box(c,screen,x*s,(sh-DESKTOP_BAR_HEIGHT-2)*s,width*s,2*s,0x91b2f4);
        desktop_box(c,screen,x*s,0,2*s,(sh-DESKTOP_BAR_HEIGHT)*s,0x91b2f4);
        desktop_box(c,screen,(x+width-2)*s,0,2*s,(sh-DESKTOP_BAR_HEIGHT)*s,0x91b2f4);
    }
    if (!v->overview && *v->message && (!v->windows[DESKTOP_FILES].open ||
                        v->windows[DESKTOP_FILES].minimized)) {
        u32 width=MIN(320u,sw-32);
        desktop_box(c,screen,16*s,(sh-75)*s,width*s,28*s,0xe8edeb);
        desktop_box(c,screen,16*s,(sh-75)*s,3*s,28*s,0xb98866);
        desktop_label(c,screen,28*s,(sh-60)*s,v->message,
                      desktop_chars((width-22)*s,s),s,0x354d55);
    }
    u32 dock_x=desktop_dock_x(sw,v)*s,dock_w=desktop_dock_width(sw,v)*s;
    desktop_shadow(c,screen,dock_x,(sh-52)*s,dock_w,48*s,s);
    desktop_round_alpha(c,screen,dock_x,(sh-52)*s,dock_w,48*s,16*s,0xedf3ff,235);
    desktop_round(c,screen,dock_x+8*s,(sh-40)*s,70*s,28*s,9*s,v->menu?0x567ad5:0x3b5ca8);
    desktop_bold_text(c,screen,dock_x+17*s,(sh-35)*s,"Start",5,s,0xf6f9ff);
    desktop_round(c,screen,dock_x+82*s,(sh-40)*s,28*s,28*s,8*s,
        v->overview || v->hover_kind==DESKTOP_HIT_OVERVIEW_BUTTON?0xc5d5f6:0xe3ebfa);
    desktop_round(c,screen,dock_x+88*s,(sh-33)*s,12*s,9*s,2*s,0x5973a5);
    desktop_round(c,screen,dock_x+93*s,(sh-28)*s,12*s,9*s,2*s,0x5973a5);
    desktop_round(c,screen,dock_x+95*s,(sh-26)*s,8*s,5*s,s,0xe3ebfa);
    u32 task_w=desktop_task_width(sw,v);
    for (u32 i=0;i<desktop_tasks(v);++i) {
        u32 id=desktop_task_id(v,i),x=dock_x+(116+i*task_w)*s;
        const struct desktop_window *w=&v->windows[id];
        bool selected=id==v->active && w->open && !w->minimized;
        if (selected || (v->hover_kind==DESKTOP_HIT_TASK && v->hover_window==id))
            desktop_round(c,screen,x,(sh-47)*s,(task_w-2)*s,39*s,10*s,0xd3e0fa);
        if (task_w>=44) desktop_icon(c,screen,x+(task_w-40)*s/2,(sh-48)*s,40*s,
            id<4?id:id==DESKTOP_ACCOUNTS?5:id==DESKTOP_SETTINGS?6:4);
        else desktop_label(c,screen,x+4*s,(sh-33)*s,desktop_window_name(w,id),(task_w-8)/6,s,0x3f5987);
        if (w->open) desktop_round(c,screen,x+(task_w/2-5)*s,(sh-7)*s,10*s,2*s,s,selected?0x446fd3:0x92a5ca);
    }
    if (sw>=640) {
        u32 sx=dock_x+dock_w-70*s;
        desktop_box(c,screen,sx-4*s,(sh-37)*s,s,25*s,0xc8d5eb);
        desktop_round(c,screen,sx+5*s,(sh-32)*s,7*s,10*s,2*s,0x5973a5);
        desktop_box(c,screen,sx+12*s,(sh-35)*s,3*s,16*s,0x5973a5);
        char level[8];number(level,v->volume_percent,10);
        strlcpy(level+strlen(level),"%",sizeof(level)-strlen(level));
        desktop_text(c,screen,sx+22*s,(sh-34)*s,v->audio_ready?level:"--",v->audio_ready?strlen(level):2,s,0x5973a5);
    }
    if (v->menu) {
        const struct desktop_launcher *l=v->launcher;
        struct desktop_rect r=desktop_menu_rect(sw,sh);
        u32 top=r.y*s,x=r.x*s;
        desktop_shadow(c,screen,x,top,r.w*s,r.h*s,s);
        desktop_round(c,screen,x,top,r.w*s,r.h*s,16*s,0xf5f8ff);
        const char *user=v->account?v->account->info.display_name:"Applications";
        desktop_label(c,screen,x+20*s,top+16*s,user,(r.w-40)/6,s,0x2c3c5b);
        desktop_round(c,screen,x+16*s,top+48*s,(r.w-32)*s,36*s,9*s,0xd2ddf2);
        desktop_round(c,screen,x+17*s,top+49*s,(r.w-34)*s,34*s,8*s,0xffffff);
        u32 length=strlen(l->query),budget=(r.w-60)*s;
        const char *query=l->query;
        while (length && desktop_text_width(query,length,s)>budget) { ++query;--length; }
        u32 query_width=desktop_text_width(query,length,s);
        if (l->select_all) desktop_round(c,screen,x+28*s,top+56*s,query_width,20*s,3*s,0xd5e3e4);
        desktop_label(c,screen,x+28*s,top+56*s,*query?query:"Search apps",(r.w-60)/6,s,
            *query?0x334667:0x8090aa);
        if (*query && !l->select_all) desktop_box(c,screen,x+28*s+query_width,top+56*s,s,18*s,0x587dcd);
        static const u32 colors[]={0x568ced,0x72b5ad,0x52647f,0xba8aef,0x79a8c9,0x7b8aaf,0x7b8aaf};
        static const char *letters[]={"F","E",">","M","D","A","S"};
        for (u32 i=0;i<l->count;++i) {
            u32 y=top+(96+i*34)*s,id=l->ids[i];
            if (i==l->selected) desktop_round(c,screen,x+8*s,y,(r.w-16)*s,32*s,8*s,0xe0e9fc);
            desktop_round(c,screen,x+20*s,y+4*s,24*s,24*s,6*s,colors[id]);
            desktop_text(c,screen,x+27*s,y+7*s,letters[id],1,s,0xffffff);
            desktop_text(c,screen,x+56*s,y+7*s,desktop_apps[id].name,strlen(desktop_apps[id].name),s,0x334667);
        }
        if (!l->count) desktop_text(c,screen,x+24*s,top+115*s,"No matching apps",16,s,0x75859e);
        if (l->count) desktop_label(c,screen,x+20*s,top+(r.h-70)*s,
            desktop_apps[l->ids[l->selected]].description,(r.w-40)/6,s,0x75859e);
        if (v->account) {
            account_button(c,screen,x+12*s,top+(r.h-42)*s,90*s,"Lock",s,true,false,false);
            account_button(c,screen,x+110*s,top+(r.h-42)*s,100*s,"Sign out",s,true,false,false);
        }
    }
    if ((v->hover_kind==DESKTOP_HIT_TASK || v->hover_kind==DESKTOP_HIT_MEDIA) &&
        !v->menu && !v->overview && !(v->switcher && v->switcher->open)) {
        const char *name=desktop_window_name(&v->windows[v->hover_window],v->hover_window);
        u32 tw=MIN(sw-32,desktop_text_width(name,strlen(name),1)+24);
        desktop_round(c,screen,(sw-tw)*s/2,(sh-85)*s,tw*s,28*s,8*s,0xf5f8ff);
        desktop_label(c,screen,(sw-tw)*s/2+12*s,(sh-80)*s,name,(tw-24)/6,s,0x334667);
    }
    if (v->volume_open && sw >= 640) {
        u32 x=(sw-193)*s,y=(sh-127)*s;
        desktop_box(c,screen,x+3*s,y+3*s,175*s,87*s,0x1d323b);
        desktop_box(c,screen,x,y,175*s,87*s,0xf6f5f0);
        desktop_box(c,screen,x,y,175*s,3*s,0xb98866);
        desktop_text(c,screen,x+15*s,y+14*s,"Output volume",13,s,0x304a54);
        desktop_box(c,screen,(sw-174)*s,(sh-73)*s,156*s,4*s,0xc4d2d2);
        if (v->audio_ready) {
            desktop_box(c,screen,(sw-174)*s,(sh-73)*s,
                        v->volume_percent*156/100*s,4*s,0x31718a);
            desktop_box(c,screen,(sw-174+v->volume_percent*148/100)*s,
                        (sh-80)*s,8*s,18*s,0x244d60);
        } else desktop_text(c,screen,x+15*s,y+48*s,"No HDA output",13,s,0x786d67);
    }
    if (v->hover_kind==DESKTOP_HIT_OVERVIEW_BUTTON && !v->overview && !v->menu &&
        !(v->switcher && v->switcher->open)) {
        desktop_box(c,screen,82*s,(sh-66)*s,92*s,23*s,0xf6f5f0);
        desktop_text(c,screen,88*s,(sh-62)*s,"Windows (F12)",13,s,0x304a54);
    }
    if (v->switcher && v->switcher->open) desktop_switch_render(c,screen,s,sw,sh,v);
    desktop_pointer_render(c,screen,s,v);
}
static void desktop_render(struct nv_canvas *c, u32 height, const struct desktop_view *v) {
    struct desktop_clip full={0,0,c->width,height};
    desktop_render_clip(c,height,v,full);
}
#endif
