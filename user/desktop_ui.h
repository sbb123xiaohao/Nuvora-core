#ifndef NV_DESKTOP_UI_H
#define NV_DESKTOP_UI_H
#include <nv/gfx.h>
#include <nv/string.h>
#include "desktop_font.h"

enum { DESKTOP_FILES, DESKTOP_EDITOR, DESKTOP_TERMINAL, DESKTOP_WINDOW_COUNT };
enum { DESKTOP_EDIT_NORMAL, DESKTOP_EDIT_PATH, DESKTOP_EDIT_CLOSE };
enum { DESKTOP_FILE_NORMAL, DESKTOP_FILE_FOLDER, DESKTOP_FILE_RENAME,
       DESKTOP_FILE_DELETE };
struct desktop_window {
    i32 x, y;
    u32 w, h;
    i32 saved_x, saved_y;
    u32 saved_w, saved_h;
    bool open, minimized, maximized;
};
struct desktop_view {
    const char *path, *message, *drive;
    const struct nv_dirent64 *entries;
    u32 count, selected, scroll, volumes;
    u32 file_mode;
    const char *file_input, *file_target;
    bool pointer;
    u32 pointer_x, pointer_y;
    u32 shortcut_selected;
    bool audio_ready, menu;
    u32 menu_selected;
    const struct desktop_window *windows;
    const u8 *order;
    u32 active;
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
    if (n >= 5 && !strcmp(entry->name + n - 5, ".flac")) return "FLAC audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".mp2")) return "MP2 audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".wav")) return "WAV audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".mp3")) return "MP3 audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".mpg")) return "MPEG video";
    if (n >= 5 && !strcmp(entry->name + n - 5, ".mpeg")) return "MPEG video";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".txt")) return "Text";
    if (n >= 3 && !strcmp(entry->name + n - 3, ".md")) return "Markdown";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".nvd")) return "Folio document";
    return "File";
}
static u32 desktop_scale(u32 width, u32 height) {
    return width >= 3840 && height >= 2000 ? 4 :
           width >= 2880 && height >= 1620 ? 3 :
           width >= 1000 && height >= 700 ? 2 : 1;
}
static u32 desktop_pointer_axis(u32 current, i32 value, u32 extent, bool absolute) {
    if (!extent) return 0;
    if (absolute) return (u32)((u64)(u32)MAX(0, MIN(value, 32767)) * (extent - 1) / 32767u);
    i64 next = (i64)current + value;
    return next < 0 ? 0 : next >= extent ? extent - 1 : (u32)next;
}
static u32 desktop_visible(const struct desktop_window *w) {
    return w->h > 112 ? MAX(1u, (w->h - 112) / 18) : 1;
}
enum { DESKTOP_HIT_NONE, DESKTOP_HIT_START, DESKTOP_HIT_MENU,
       DESKTOP_HIT_TASK, DESKTOP_HIT_SHORTCUT, DESKTOP_HIT_TITLE,
       DESKTOP_HIT_MINIMIZE, DESKTOP_HIT_MAXIMIZE, DESKTOP_HIT_CLOSE,
       DESKTOP_HIT_RESIZE, DESKTOP_HIT_PLACE, DESKTOP_HIT_FILE,
       DESKTOP_HIT_FILE_NEW_FOLDER, DESKTOP_HIT_FILE_NEW_TEXT,
       DESKTOP_HIT_FILE_DIALOG,
       DESKTOP_HIT_EDITOR_SAVE, DESKTOP_HIT_EDITOR_NEW, DESKTOP_HIT_EDITOR_TEXT,
       DESKTOP_HIT_EDITOR_DIALOG, DESKTOP_HIT_TERMINAL, DESKTOP_HIT_MEDIA };
struct desktop_hit { u32 kind, index, window; };
static struct desktop_hit desktop_hit(u32 width, u32 height,
                                      const struct desktop_view *v, u32 px, u32 py) {
    u32 s = desktop_scale(width, height), sh = height/s, sw=width/s;
    u32 x = px/s, y = py/s;
    if (y >= sh - 30) {
        if (x >= 8 && x < 78) return (struct desktop_hit){DESKTOP_HIT_START, 0, 0};
        for (u32 i = 0; i < DESKTOP_WINDOW_COUNT; ++i)
            if (x >= 88 + i*98 && x < 184 + i*98)
                return (struct desktop_hit){DESKTOP_HIT_TASK, i, i};
        if (sw>=580 && x >= 88 + 3*98 && x < 184 + 3*98)
            return (struct desktop_hit){DESKTOP_HIT_MEDIA, 0, 0};
        return (struct desktop_hit){DESKTOP_HIT_NONE, 0, 0};
    }
    if (v->menu) {
        u32 top = sh - 30 - 190;
        if (x >= 8 && x < 205 && y >= top && y < sh - 30) {
            for (u32 i = 0; i < 5; ++i)
                if (y >= top + 50 + i*25 && y < top + 73 + i*25)
                    return (struct desktop_hit){DESKTOP_HIT_MENU, i, 0};
        }
        return (struct desktop_hit){DESKTOP_HIT_NONE, 0, 0};
    }
    for (i32 z = DESKTOP_WINDOW_COUNT - 1; z >= 0; --z) {
        u32 id = v->order[z];
        const struct desktop_window *w = &v->windows[id];
        if (!w->open || w->minimized || (i32)x < w->x || (i32)y < w->y ||
            (i32)x >= w->x + (i32)w->w || (i32)y >= w->y + (i32)w->h) continue;
        u32 rx = x - (u32)w->x, ry = y - (u32)w->y;
        if (ry < 28) {
            if (rx >= w->w - 28) return (struct desktop_hit){DESKTOP_HIT_CLOSE, 0, id};
            if (rx >= w->w - 56) return (struct desktop_hit){DESKTOP_HIT_MAXIMIZE, 0, id};
            if (rx >= w->w - 84) return (struct desktop_hit){DESKTOP_HIT_MINIMIZE, 0, id};
            return (struct desktop_hit){DESKTOP_HIT_TITLE, 0, id};
        }
        if (!w->maximized && rx >= w->w - 12 && ry >= w->h - 12)
            return (struct desktop_hit){DESKTOP_HIT_RESIZE, 0, id};
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
            for (u32 i = 0; i < v->volumes + 3; ++i)
                if (rx >= 10 && rx < 112 && ry >= 84 + i*22 && ry < 104 + i*22)
                    return (struct desktop_hit){DESKTOP_HIT_PLACE,
                        i < v->volumes ? i : NV_VOLUME_MAX + i - v->volumes, id};
            if (rx >= 124 && ry >= 86 && ry < w->h - 27) {
                u32 row = (ry - 86)/18, index = v->scroll + row;
                if (row < desktop_visible(w) && index < v->count)
                    return (struct desktop_hit){DESKTOP_HIT_FILE, index, id};
            }
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
        if (x >= 14 && x < 110 && y >= 28 + i*70 && y < 92 + i*70)
            return (struct desktop_hit){DESKTOP_HIT_SHORTCUT, i, i};
    return (struct desktop_hit){DESKTOP_HIT_NONE, 0, 0};
}

struct desktop_clip { u32 left, top, right, bottom; };
static void desktop_box(struct nv_canvas *c, struct desktop_clip clip,
                        u32 x, u32 y, u32 w, u32 h, u32 rgb) {
    if (!w || !h || x >= clip.right || y >= clip.bottom) return;
    u32 left = MAX(x, clip.left), top = MAX(y, clip.top);
    u32 right = MIN(x + w, clip.right), bottom = MIN(y + h, clip.bottom);
    if (right > left && bottom > top) nv_gfx_fill(c, left, top, right - left, bottom - top, rgb);
}
static void desktop_text(struct nv_canvas *c, struct desktop_clip clip,
                         u32 x, u32 y, const char *value, u32 count, u32 s, u32 rgb) {
    u32 color = nv_display_rgb(c->format, rgb), advance = 6*s;
    u32 glyph_height = s==1?13:s==2?24:s==3?36:48;
    for (u32 ch = 0; ch < count && value[ch]; ++ch) {
        u32 gx = x + ch*advance;
        if (gx >= clip.right) break;
        u8 glyph = (u8)value[ch];
        if (glyph < 32 || glyph > 126) glyph = '?';
        const u8 *mask = s==1?nv_desktop_font_1[glyph-32]:
                         s==2?nv_desktop_font_2[glyph-32]:
                         s==3?nv_desktop_font_3[glyph-32]:nv_desktop_font_4[glyph-32];
        for (u32 row = 0; row < glyph_height; ++row) {
            u32 py = y + row;
            if (py < clip.top || py >= clip.bottom || py < c->y0 ||
                py >= c->y0 + c->rows) continue;
            for (u32 col = 0; col < advance; ++col) {
                u32 px = gx + col;
                if (px < clip.left || px >= clip.right || px >= c->width) continue;
                u32 at=row*advance+col;
                u32 alpha=(at&1)?mask[at/2]&15:mask[at/2]>>4;
                if (!alpha) continue;
                u32 *pixel=&c->pixels[(py-c->y0)*c->width+px];
                if (alpha==15) { *pixel=color; continue; }
                u32 background=*pixel, remainder=15-alpha;
                u32 b=((color&255)*alpha+(background&255)*remainder+7)/15;
                u32 g=(((color>>8)&255)*alpha+((background>>8)&255)*remainder+7)/15;
                u32 r=(((color>>16)&255)*alpha+((background>>16)&255)*remainder+7)/15;
                *pixel=(r<<16)|(g<<8)|b;
            }
        }
    }
}
static void desktop_label(struct nv_canvas *c, struct desktop_clip clip,
                          u32 x, u32 y, const char *value, u32 max, u32 s, u32 rgb) {
    u32 n = strnlen(value, max + 1);
    if (n <= max) desktop_text(c, clip, x, y, value, n, s, rgb);
    else if (max >= 3) {
        desktop_text(c, clip, x, y, value, max - 3, s, rgb);
        desktop_text(c, clip, x + (max - 3)*6*s, y, "...", 3, s, rgb);
    }
}
static u32 desktop_chars(u32 pixels, u32 s) { return pixels/(6*s); }

static void desktop_window_render(struct nv_canvas *c, u32 s, u32 sh,
                                  const struct desktop_view *v, u32 id) {
    const struct desktop_window *w = &v->windows[id];
    u32 x = (u32)w->x*s, y = (u32)w->y*s, width = w->w*s, height = w->h*s;
    struct desktop_clip screen = {0,0,c->width,sh*s};
    struct desktop_clip clip = {x+s,y+s,MIN(x+width-s,c->width),MIN(y+height-s,sh*s)};
    bool focus = v->active == id;
    desktop_box(c,screen,x+3*s,y+4*s,width,height,0x203741);
    desktop_box(c,screen,x,y,width,height,focus ? 0x214d5d : 0x80949d);
    desktop_box(c,clip,x+s,y+s,width-2*s,height-2*s,0xfaf9f5);
    desktop_box(c,clip,x+s,y+s,width-2*s,27*s,focus ? 0x244755 : 0x526873);
    static const char *const titles[] = {"Files", "Text Editor", "Terminal"};
    desktop_label(c,clip,x+13*s,y+10*s,titles[id],desktop_chars(width-102*s,s),s,0xf7f6f1);
    for (u32 i = 0; i < 3; ++i) {
        u32 bx = x + width - (84 - i*28)*s;
        desktop_box(c,clip,bx,y+s,28*s,26*s,i == 2 ? 0xa14b40 :
                    focus ? 0x345967 : 0x607681);
        if (i==0) desktop_box(c,clip,bx+9*s,y+17*s,10*s,s,0xffffff);
        else if (i==1) {
            if (w->maximized) {
                desktop_box(c,clip,bx+11*s,y+8*s,9*s,s,0xffffff);
                desktop_box(c,clip,bx+19*s,y+8*s,s,8*s,0xffffff);
                desktop_box(c,clip,bx+8*s,y+11*s,10*s,s,0xffffff);
                desktop_box(c,clip,bx+8*s,y+11*s,s,8*s,0xffffff);
                desktop_box(c,clip,bx+8*s,y+18*s,10*s,s,0xffffff);
            } else {
                desktop_box(c,clip,bx+9*s,y+9*s,10*s,s,0xffffff);
                desktop_box(c,clip,bx+9*s,y+9*s,s,9*s,0xffffff);
                desktop_box(c,clip,bx+18*s,y+9*s,s,9*s,0xffffff);
                desktop_box(c,clip,bx+9*s,y+17*s,10*s,s,0xffffff);
            }
        } else for (u32 mark=0;mark<9;++mark) {
            desktop_box(c,clip,bx+(9+mark)*s,y+(9+mark)*s,s,s,0xffffff);
            desktop_box(c,clip,bx+(17-mark)*s,y+(9+mark)*s,s,s,0xffffff);
        }
    }
    if (id == DESKTOP_FILES) {
        desktop_box(c,clip,x+s,y+28*s,width-2*s,36*s,0xe6ecee);
        desktop_box(c,clip,x+13*s,y+35*s,width-26*s,22*s,0xffffff);
        desktop_label(c,clip,x+19*s,y+42*s,v->path,
                      desktop_chars(width-175*s,s),s,0x253944);
        desktop_box(c,clip,x+width-147*s,y+35*s,70*s,22*s,0xd5e5e7);
        desktop_box(c,clip,x+width-74*s,y+35*s,67*s,22*s,0x31647a);
        desktop_text(c,clip,x+width-137*s,y+42*s,"+ Folder",8,s,0x294651);
        desktop_text(c,clip,x+width-67*s,y+42*s,"+ Text",6,s,0xffffff);
        desktop_box(c,clip,x+s,y+64*s,116*s,height-65*s,0xe9eef0);
        desktop_text(c,clip,x+14*s,y+71*s,"PLACES",6,s,0x596d75);
        for (u32 i = 0; i < v->volumes + 3 && i < 7; ++i) {
            u32 line = y + (88+i*22)*s;
            if (line + 12*s >= y + height - 27*s) break;
            char label[20];
            if (i < v->volumes) {
                strlcpy(label,i ? "D: Drive" : "C: Home",sizeof(label));
                if (i) label[0] = 'C' + (char)i;
            } else {
                static const char *const places[] = {"Root /", "Apps", "Temp"};
                strlcpy(label,places[i-v->volumes],sizeof(label));
            }
            if (i == 0 && !strncmp(v->path,"/home",5))
                desktop_box(c,clip,x+5*s,line-4*s,108*s,19*s,0xc9dfe5);
            desktop_label(c,clip,x+13*s,line,label,15,s,0x263d48);
        }
        desktop_box(c,clip,x+117*s,y+64*s,width-118*s,22*s,0xf0f2f2);
        desktop_text(c,clip,x+130*s,y+71*s,"NAME",4,s,0x51646d);
        if (width >= 380*s) desktop_text(c,clip,x+width-99*s,y+71*s,"SIZE",4,s,0x51646d);
        u32 visible = desktop_visible(w);
        for (u32 row = 0; row < visible && row + v->scroll < v->count; ++row) {
            u32 index = row + v->scroll, line = y + (90+row*18)*s;
            if (line + 11*s >= y + height - 27*s) break;
            if (index == v->selected) {
                desktop_box(c,clip,x+121*s,line-4*s,width-127*s,17*s,0xd4e7ed);
                desktop_box(c,clip,x+121*s,line-4*s,2*s,17*s,0x286984);
            }
            desktop_label(c,clip,x+130*s,line,v->entries[index].name,
                          desktop_chars(width-(width>=380*s ? 245*s : 150*s),s),s,0x243941);
            if (width >= 380*s && v->entries[index].kind == NV_FILE) {
                char bytes[32]; desktop_size(bytes,v->entries[index].size);
                desktop_label(c,clip,x+width-99*s,line,bytes,9,s,0x576c75);
            }
        }
        if (!v->count) desktop_text(c,clip,x+130*s,y+104*s,"This folder is empty",20,s,0x6a7b81);
        desktop_box(c,clip,x+s,y+height-27*s,width-2*s,26*s,0xf0f2f1);
        char amount[24]; number(amount,v->count,10);
        strlcpy(amount+strlen(amount),v->count==1 ? " item" : " items",sizeof(amount)-strlen(amount));
        desktop_text(c,clip,x+12*s,y+height-19*s,amount,strlen(amount),s,0x526871);
        if (v->count && v->selected<v->count)
            desktop_label(c,clip,x+74*s,y+height-19*s,
                          desktop_kind(&v->entries[v->selected]),14,s,0x526871);
        desktop_label(c,clip,x+174*s,y+height-19*s,*v->message?v->message:
                      "F2 Rename  Del Delete",
                      desktop_chars(width-187*s,s),s,0x92513a);
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
        u32 cols = MAX(1u,desktop_chars(width-36*s,s)), maxrows = (w->h-94)/13;
        u32 row = 0, col = 0, caret_row = 0, caret_col = 0;
        for (u32 i = 0; i <= v->editor_length; ++i) {
            if (i == v->editor_cursor) { caret_row = row; caret_col = col; }
            if (i == v->editor_length) break;
            char ch = v->editor_text[i];
            if (ch == '\n') { ++row; col=0; continue; }
            if (row >= v->editor_scroll && row-v->editor_scroll < maxrows && col < cols &&
                (u8)ch >= 32 && (u8)ch < 127)
                desktop_text(c,clip,x+(19*s)+col*6*s,
                             y+(73+13*(row-v->editor_scroll))*s,&v->editor_text[i],1,s,0x263a43);
            if (++col >= cols) { ++row; col=0; }
        }
        if (focus && v->editor_mode == DESKTOP_EDIT_NORMAL &&
            caret_row >= v->editor_scroll && caret_row-v->editor_scroll < maxrows)
            desktop_box(c,clip,x+19*s+caret_col*6*s,
                        y+(72+13*(caret_row-v->editor_scroll))*s,s,9*s,0x21647e);
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
            desktop_label(c,clip,x+13*s,y+(39+(row-begin)*13)*s,line,
                          desktop_chars(width-28*s,s),s,0xdce7e6);
        }
        desktop_box(c,clip,x+8*s,y+height-34*s,width-16*s,25*s,0x24414e);
        desktop_text(c,clip,x+13*s,y+height-26*s,">",1,s,0x8ac5c3);
        u32 input_cols=desktop_chars(width-42*s,s), input_length=strlen(v->terminal_input);
        u32 input_start=input_length>input_cols?input_length-input_cols:0;
        desktop_label(c,clip,x+27*s,y+height-26*s,v->terminal_input+input_start,
                      input_cols,s,0xffffff);
        if (focus) desktop_box(c,clip,x+27*s+(input_length-input_start)*6*s,
                               y+height-27*s,s,10*s,0x8ac5c3);
    }
    if (!w->maximized) desktop_box(c,clip,x+width-8*s,y+height-8*s,6*s,6*s,0x8ca1a8);
}

static void desktop_render(struct nv_canvas *c, u32 height, const struct desktop_view *v) {
    u32 s=desktop_scale(c->width,height), sw=c->width/s, sh=height/s;
    struct desktop_clip screen={0,0,c->width,height};
    desktop_box(c,screen,0,0,c->width,height,0x263f4a);
    desktop_box(c,screen,(sw*59/100)*s,0,c->width-(sw*59/100)*s,height-30*s,0x2b4954);
    desktop_box(c,screen,(sw*59/100)*s,0,2*s,height-30*s,0x42636c);
    /* A quiet geometric N keeps the workspace recognizable behind windows. */
    u32 mark_x=sw-184, mark_y=sh-226;
    desktop_box(c,screen,mark_x*s,mark_y*s,24*s,144*s,0x385964);
    desktop_box(c,screen,(mark_x+132)*s,mark_y*s,24*s,144*s,0x385964);
    for (u32 row=0;row<144;++row)
        desktop_box(c,screen,(mark_x+19+row*113/144)*s,(mark_y+row)*s,
                    26*s,s,0x385964);
    desktop_box(c,screen,(sw-170)*s,(sh-59)*s,24*s,3*s,0xb98866);
    desktop_text(c,screen,(sw-139)*s,(sh-64)*s,"NUVORA",6,s,0xb3c9ca);
    const char *shortcuts[]={"Files","Text Editor","Terminal","Media"};
    for (u32 i=0;i<4;++i) {
        u32 top=28+i*70, icon_x=29, icon_y=top+5;
        if (v->shortcut_selected==i) {
            desktop_box(c,screen,14*s,top*s,96*s,64*s,0x3b626d);
            desktop_box(c,screen,14*s,(top+63)*s,96*s,s,0xa5c5c8);
        }
        if (i==0) {
            desktop_box(c,screen,icon_x*s,(icon_y+5)*s,39*s,27*s,0xe1d2b4);
            desktop_box(c,screen,(icon_x+2)*s,(icon_y+2)*s,17*s,7*s,0xe1d2b4);
            desktop_box(c,screen,(icon_x+3)*s,(icon_y+11)*s,33*s,2*s,0xb8a585);
        } else if (i==1) {
            desktop_box(c,screen,(icon_x+5)*s,icon_y*s,30*s,36*s,0xe7eae3);
            for (u32 line=0;line<3;++line)
                desktop_box(c,screen,(icon_x+10)*s,(icon_y+10+line*7)*s,19*s,2*s,0x76969b);
        } else if (i==2) {
            desktop_box(c,screen,icon_x*s,(icon_y+3)*s,39*s,29*s,0x142e3b);
            desktop_box(c,screen,(icon_x+3)*s,(icon_y+6)*s,33*s,22*s,0x1c4553);
            desktop_text(c,screen,(icon_x+8)*s,(icon_y+9)*s,">_",2,s,0xc0e0dc);
        } else {
            desktop_box(c,screen,(icon_x+2)*s,(icon_y+4)*s,35*s,30*s,0x9c7764);
            for (u32 line=0;line<19;++line) {
                u32 side=line<10?line/2:(18-line)/2;
                desktop_box(c,screen,(icon_x+13)*s,(icon_y+9+line)*s,
                            (side+2)*s,s,0xf6eee4);
            }
        }
        u32 length=strlen(shortcuts[i]);
        desktop_text(c,screen,(62-length*3)*s,(top+44)*s,
                     shortcuts[i],length,s,0xedf2ef);
    }
    for (u32 z=0;z<DESKTOP_WINDOW_COUNT;++z) {
        u32 id=v->order[z];
        if (v->windows[id].open && !v->windows[id].minimized)
            desktop_window_render(c,s,sh,v,id);
    }
    if (*v->message && (!v->windows[DESKTOP_FILES].open ||
                        v->windows[DESKTOP_FILES].minimized)) {
        u32 width=MIN(320u,sw-32);
        desktop_box(c,screen,16*s,(sh-65)*s,width*s,28*s,0xe8edeb);
        desktop_box(c,screen,16*s,(sh-65)*s,3*s,28*s,0xb98866);
        desktop_label(c,screen,28*s,(sh-60)*s,v->message,
                      desktop_chars((width-22)*s,s),s,0x354d55);
    }
    desktop_box(c,screen,0,(sh-30)*s,c->width,30*s,0xe3e9e8);
    desktop_box(c,screen,0,(sh-30)*s,c->width,s,0xa9bbbf);
    desktop_box(c,screen,8*s,(sh-26)*s,70*s,22*s,0x234451);
    desktop_box(c,screen,8*s,(sh-26)*s,22*s,22*s,0xb98866);
    desktop_text(c,screen,15*s,(sh-21)*s,"N",1,s,0xffffff);
    desktop_text(c,screen,39*s,(sh-21)*s,"Start",5,s,0xf4f5f1);
    const char *tasks[]={"Files","Editor","Terminal","Media"};
    for (u32 i=0;i<(sw>=580?4u:3u);++i) {
        u32 x=(88+i*98)*s;
        if (i==v->active && i<3 && v->windows[i].open && !v->windows[i].minimized)
            desktop_box(c,screen,x,(sh-26)*s,96*s,22*s,0xc5d9dc);
        desktop_label(c,screen,x+11*s,(sh-21)*s,tasks[i],12,s,0x29434f);
        if (i<3 && v->windows[i].open)
            desktop_box(c,screen,x,(sh-5)*s,96*s,2*s,0x2d7188);
    }
    if (sw>=640) {
        desktop_box(c,screen,(sw-140)*s,(sh-25)*s,s,20*s,0xb5c3c4);
        desktop_label(c,screen,(sw-129)*s,(sh-21)*s,v->drive,10,s,0x38525b);
        desktop_text(c,screen,(sw-62)*s,(sh-21)*s,
                     v->audio_ready?"Audio":"No audio",v->audio_ready?5:8,s,0x38525b);
    } else {
        desktop_box(c,screen,(sw-86)*s,(sh-25)*s,s,20*s,0xb5c3c4);
        desktop_label(c,screen,(sw-76)*s,(sh-21)*s,v->drive,10,s,0x38525b);
    }
    if (v->menu) {
        u32 top=(sh-220)*s, x=8*s;
        desktop_box(c,screen,x+3*s,top+4*s,197*s,190*s,0x1d323b);
        desktop_box(c,screen,x,top,197*s,190*s,0xf8f7f2);
        desktop_box(c,screen,x,top,197*s,38*s,0x244755);
        desktop_text(c,screen,x+13*s,top+13*s,"Nuvora",6,s,0xf4f3ee);
        desktop_text(c,screen,x+13*s,top+39*s,"APPLICATIONS",12,s,0x617780);
        const char *apps[]={"Files","Text Editor","Terminal","Media","Return to Loom"};
        for (u32 i=0;i<5;++i) {
            u32 y=top+(50+i*25)*s;
            if (i==v->menu_selected) {
                desktop_box(c,screen,x+8*s,y-2*s,181*s,23*s,0xdbe9ea);
                desktop_box(c,screen,x+8*s,y-2*s,3*s,23*s,0xb67c52);
            }
            desktop_text(c,screen,x+19*s,y+5*s,apps[i],strlen(apps[i]),s,0x2c4651);
        }
    }
    if (v->pointer) {
        for (u32 i=0;i<10;++i) {
            u32 width=(i<7?i+1:4)*s;
            desktop_box(c,screen,v->pointer_x,v->pointer_y+i*s,width,s,0x10242e);
            if (i>1 && i<7)
                desktop_box(c,screen,v->pointer_x+s,v->pointer_y+i*s,(i-1)*s,s,0xffffff);
        }
    }
}
#endif
