#include "runtime.h"
#include "desktop_ui.h"
#include "account_client.h"

#define DESKTOP_ITEMS 512u
#define EDITOR_CAP (128u * 1024u)
static struct nv_dirent64 entries[DESKTOP_ITEMS];
static struct nv_display_info mode;
static u32 *wallpaper;
static struct desktop_window windows[DESKTOP_WINDOW_COUNT];
static u8 order[DESKTOP_WINDOW_COUNT];
static u32 active = DESKTOP_WINDOW_COUNT;
static u32 shortcut_selected = 0xffffffffu;
static char directory[NV_PATH_MAX], message[160], drive[32];
static u32 count, selected, scroll, volumes;
static bool show_hidden;
static u32 file_mode;
static char file_input[32], file_target[NV_PATH_MAX], file_base[NV_PATH_MAX];
static u32 pointer_x, pointer_y, pointer_buttons;
static bool pointer_visible, audio_ready, menu, volume_open, quit_requested, leaving;
static bool signout_requested;
static struct account_ui account;
static u32 theme,settings_tab,settings_confirm,settings_focus,pending_power,idle_minutes=5,uptime_minutes;
static char system_memory[64];
static struct desktop_launcher launcher;
static struct desktop_switcher switcher;
static bool overview, meta_down, super_armed;
static u32 overview_selected=DESKTOP_WINDOW_COUNT, snap_preview;
static u32 volume_percent = 100;
static u32 drag_window, drag_kind;
static u32 drag_offset_x, drag_offset_y;
static struct desktop_rect drag_restore;
static char editor_text[EDITOR_CAP+1], editor_scratch[EDITOR_CAP+1];
static char editor_path[NV_PATH_MAX], editor_input[NV_PATH_MAX], editor_next_path[NV_PATH_MAX];
static u32 editor_length, editor_cursor, editor_scroll, editor_mode, editor_next_action;
static bool editor_dirty;
static char terminal_lines[64][128], terminal_input[256], terminal_history[256];
static u32 terminal_first, terminal_count, terminal_column, terminal_length;

struct client_cache {
    u32 capacity;
    u32 *front, *back;
    struct nv_surface text;
};
static struct client_cache client_cache[DESKTOP_WINDOW_COUNT];
static u32 child_pids[NV_TASK_MAX];
static u32 hover_kind, hover_window = DESKTOP_WINDOW_COUNT, captured = DESKTOP_WINDOW_COUNT;
static u32 drag_edges;
static struct desktop_clip damage;
static bool damaged;

static void damage_window(const struct desktop_window *w) {
    if (overview || switcher.open) {
        damage=(struct desktop_clip){0,0,mode.width,mode.height}; damaged=true; return;
    }
    u32 s=desktop_scale(mode.width,mode.height);
    struct desktop_clip r={(u32)MAX(0,w->x-6)*s,(u32)MAX(0,w->y-6)*s,
        MIN(mode.width,((u32)w->x+w->w+6)*s),MIN(mode.height,((u32)w->y+w->h+9)*s)};
    if (!damaged) damage=r;
    else {
        damage.left=MIN(damage.left,r.left); damage.top=MIN(damage.top,r.top);
        damage.right=MAX(damage.right,r.right); damage.bottom=MAX(damage.bottom,r.bottom);
    }
    damaged=true;
}
static void sync_clients(void) {
    u32 s=desktop_scale(mode.width,mode.height);
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) if (windows[i].client) {
        struct desktop_window *w=&windows[i];
        struct nv_window_configure io={w->client,(w->w-2)*s,(w->h-29)*s,
            (w->open && !w->minimized?NV_WINDOW_VISIBLE:0u) |
            (active==i && w->open && !w->minimized && !account.gate && !menu && !overview &&
             !switcher.open?NV_WINDOW_FOCUSED:0u)};
        nv_window_call(NV_WINDOW_CONFIGURE,&io);
    }
}
static int client_snapshot(u32 id,const struct nv_window_entry *e) {
    struct desktop_window *w=&windows[id];
    struct client_cache *cache=&client_cache[id];
    if (e->flags&NV_WINDOW_TEXT) {
        struct nv_window_text io={.id=e->id,.generation=e->generation};
        int r=nv_window_call(NV_WINDOW_TEXT_READ,&io);
        if (r<0) return r;
        cache->text=io.surface; w->text=&cache->text;
    } else {
        if (!e->generation || !e->width || !e->height) return 0;
        u32 pixels=e->width*e->height;
        if (cache->capacity<pixels) {
            u32 capacity=1;
            while (capacity<pixels) capacity*=2;
            u32 pages=(capacity*4+NV_PAGE-1)/NV_PAGE;
            u32 *p=grow((i32)(pages*2));
            if ((iptr)p<0) return (int)(iptr)p;
            cache->front=p; cache->back=p+(uptr)pages*NV_PAGE/4;
            cache->capacity=pages*NV_PAGE/4;
        }
        u32 rows=NV_DISPLAY_MAX_COPY/(e->width*4);
        for (u32 y=0;y<e->height;y+=rows) {
            struct nv_window_pixels io={.id=e->id,.generation=e->generation,
                .y=y,.width=e->width,.height=MIN(rows,e->height-y),.stride=e->width*4,
                .pixels=(uptr)(cache->back+(uptr)y*e->width)};
            int r=nv_window_call(NV_WINDOW_READ64,&io);
            if (r<0) return r;
        }
        u32 *old=cache->front; cache->front=cache->back; cache->back=old;
        w->pixels=cache->front; w->pixel_width=e->width; w->pixel_height=e->height;
    }
    w->generation=e->generation;
    damage_window(w);
    return 0;
}
static int client_poll(bool *dirty);
static int reap_children(bool shutdown) {
    for (u32 i=0;i<NV_TASK_MAX;++i) {
        struct nv_taskinfo t;
        int r=task_at(i,&t);
        if (r!=1) continue;
        for (u32 child=0;child<NV_TASK_MAX;++child) if (child_pids[child]==t.pid) {
            if (shutdown && t.state!=NV_ZOMBIE) stop_task((int)t.pid);
            if (shutdown || t.state==NV_ZOMBIE) { wait_task((int)t.pid); child_pids[child]=0; }
            break;
        }
    }
    return 0;
}
static int send_client(u32 id,u32 type,u32 key,const struct nv_pointer_event *pointer) {
    if (id>=DESKTOP_WINDOW_COUNT || !windows[id].client) return 0;
    struct desktop_window *w=&windows[id];
    u32 s=desktop_scale(mode.width,mode.height);
    struct nv_window_event e={.id=w->client,.type=type,.key=key};
    if (pointer) {
        e.x=(i32)pointer_x-(w->x+1)*(i32)s;
        e.y=(i32)pointer_y-(w->y+28)*(i32)s;
        u32 cw=(w->w-2)*s,ch=(w->h-29)*s;
        if (w->pixel_width && w->pixel_height) {
            e.x=(i32)((i64)e.x*w->pixel_width/cw);
            e.y=(i32)((i64)e.y*w->pixel_height/ch);
        }
        e.buttons=pointer->buttons&7; e.wheel=pointer->wheel;
    }
    return nv_window_call(NV_WINDOW_SEND,&e);
}
static void shell_grab(void) {
    if (captured<DESKTOP_WINDOW_COUNT) {
        struct nv_pointer_event release={0};
        send_client(captured,NV_WINDOW_EVENT_POINTER,0,&release);
    }
    captured=DESKTOP_WINDOW_COUNT; drag_kind=snap_preview=0;
}

static void terminal_new_line(void) {
    if (terminal_count < ARRAY_LEN(terminal_lines)) ++terminal_count;
    else terminal_first = (terminal_first + 1) % ARRAY_LEN(terminal_lines);
    u32 index = (terminal_first + terminal_count - 1) % ARRAY_LEN(terminal_lines);
    terminal_lines[index][0] = 0;
    terminal_column = 0;
}
static void terminal_print(const char *value) {
    u32 columns=desktop_chars(windows[DESKTOP_TERMINAL].w*
                             desktop_scale(mode.width,mode.height)-28*
                             desktop_scale(mode.width,mode.height),
                             desktop_scale(mode.width,mode.height));
    columns=MIN(MAX(columns,1u),(u32)sizeof(terminal_lines[0])-1);
    for (; *value; ++value) {
        if (*value == '\r') continue;
        if (*value == '\n') { terminal_new_line(); continue; }
        if (terminal_column >= columns) terminal_new_line();
        u32 index = (terminal_first + terminal_count - 1) % ARRAY_LEN(terminal_lines);
        terminal_lines[index][terminal_column++] = *value == '\t' ? ' ' : *value;
        terminal_lines[index][terminal_column] = 0;
    }
}
static void terminal_println(const char *value) { terminal_print(value); terminal_print("\n"); }
static void terminal_print_u32(u32 n) { char b[32]; number(b,n,10); terminal_print(b); }
static void terminal_print_u64(u64 n) { char b[32]; number64(b,n,10); terminal_print(b); }
static void terminal_print_hex(u32 n) { char b[32]; number(b,n,16); terminal_print(b); }
static void terminal_write_bytes(const void *data, u32 length) {
    const u8 *bytes=data;
    for (u32 i=0;i<length;++i) {
        char ch=(char)bytes[i];
        if (ch=='\n') terminal_new_line();
        else if (ch!='\r') {
            char text[2]={ch>=32 && ch<127?ch:'.',0};
            terminal_print(text);
        }
    }
}
/* The same native network clients used by Loom write to this window's scrollback. */
#define print terminal_print
#define println terminal_println
#define print_u32 terminal_print_u32
#define print_u64 terminal_print_u64
#define print_hex terminal_print_hex
#define NV_NETWORK_OUTPUT(data,length) terminal_write_bytes(data,length)
#include "network.h"
#include "net_tools.h"
#undef NV_NETWORK_OUTPUT
#undef print
#undef println
#undef print_u32
#undef print_u64
#undef print_hex

static void note(const char *value) { strlcpy(message,value,sizeof(message)); }
static void note_error(const char *action, int error) {
    strlcpy(message,action,sizeof(message));
    usize n=strlen(message);
    strlcpy(message+n,": ",sizeof(message)-n);
    n=strlen(message);
    strlcpy(message+n,error_name(error),sizeof(message)-n);
}
static int refresh(void) {
    int r=getcwd_path(directory,sizeof(directory));
    if (r<0) return r;
    count=0;
    for (u32 i=0;count<DESKTOP_ITEMS;++i) {
        struct nv_dirent64 entry;
        r=list_dir64(".",i,&entry);
        if (r<0) return r;
        if (!r) break;
        if (!show_hidden && entry.name[0]=='.') continue;
        entries[count++]=entry;
    }
    if (selected>=count) selected=count?count-1:0;
    if (scroll>selected) scroll=selected;
    volumes=0;
    struct nv_volume_info volume;
    while (volumes<NV_VOLUME_MAX && volume_info(volumes,&volume)==1) ++volumes;
    if (volumes) {
        drive[0]='0'+(char)volumes;
        strlcpy(drive+1,volumes==1?" drive":" drives",sizeof(drive)-1);
    } else strlcpy(drive,"RAM only",sizeof(drive));
    return 0;
}
static void scroll_to_selection(void) {
    u32 visible=desktop_visible(&windows[DESKTOP_FILES]);
    if (selected<scroll) scroll=selected;
    if (selected>=scroll+visible) scroll=selected-visible+1;
}
static struct desktop_view view(void) {
    struct desktop_view v={0};
    v.account=&account;
    v.wallpaper=wallpaper;
    v.theme=theme;v.settings_tab=settings_tab;v.settings_confirm=settings_confirm;
    v.settings_focus=settings_focus;
    v.idle_minutes=idle_minutes;v.uptime_minutes=uptime_minutes;v.system_memory=system_memory;
    v.path=directory; v.message=message; v.drive=drive; v.entries=entries;
    v.count=count; v.selected=selected; v.scroll=scroll; v.volumes=volumes;
    v.file_mode=file_mode; v.file_input=file_input; v.file_target=file_input;
    v.pointer=pointer_visible; v.pointer_x=pointer_x; v.pointer_y=pointer_y;
    v.shortcut_selected=shortcut_selected;
    v.audio_ready=audio_ready; v.menu=menu; v.launcher=&launcher;
    v.switcher=&switcher; v.overview=overview; v.overview_selected=overview_selected;
    v.snap_preview=snap_preview;
    v.volume_open=volume_open; v.volume_percent=volume_percent;
    v.windows=windows; v.order=order; v.active=active;
    v.hover_kind=hover_kind; v.hover_window=hover_window;
    v.editor_path=*editor_path?editor_path:"Untitled.txt";
    v.editor_text=editor_text; v.editor_input=editor_input;
    v.editor_length=editor_length; v.editor_cursor=editor_cursor;
    v.editor_scroll=editor_scroll; v.editor_mode=editor_mode;
    v.editor_dirty=editor_dirty;
    v.terminal_lines=(const char (*)[128])terminal_lines;
    v.terminal_input=terminal_input;
    v.terminal_count=terminal_count; v.terminal_first=terminal_first;
    return v;
}
static int draw_region(u32 *tile, u32 rows, u32 x, u32 y, u32 width, u32 height) {
    if (x>=mode.width || y>=mode.height || !width || !height) return 0;
    width=MIN(width,mode.width-x); height=MIN(height,mode.height-y);
    struct desktop_view v=view();
    struct desktop_clip clip={x,y,x+width,y+height};
    for (u32 row=y;row<y+height;row+=rows) {
        struct nv_canvas canvas={tile,mode.width,row,MIN(rows,y+height-row),mode.format};
        if (x==0 && y==0 && width==mode.width && height==mode.height)
            desktop_render(&canvas,mode.height,&v);
        else desktop_render_clip(&canvas,mode.height,&v,clip);
        struct nv_display_present rect={.x=x,.y=row,.width=width,
            .height=canvas.rows,.stride=mode.width*4,.pixels=(uptr)(tile+x)};
        int r=nv_display_present(&rect);
        if (r<0) return r;
    }
    return 0;
}
static int draw(u32 *tile, u32 rows) {
    return draw_region(tile,rows,0,0,mode.width,mode.height);
}
static void focus_window(u32 id) {
    if (id>=DESKTOP_WINDOW_COUNT) return;
    desktop_raise(order,id);
    active=id;
    windows[id].open=true;
    windows[id].minimized=false;
    sync_clients();
}
static void focus_top(void) {
    for (i32 i=DESKTOP_WINDOW_COUNT-1;i>=0;--i) {
        u32 id=order[i];
        if (windows[id].open && !windows[id].minimized) { active=id; sync_clients(); return; }
    }
    active=DESKTOP_WINDOW_COUNT;
    sync_clients();
}
static void maximize_window(u32 id) {
    struct desktop_window *w=&windows[id];
    u32 s=desktop_scale(mode.width,mode.height);
    desktop_place(w,mode.width/s,mode.height/s-DESKTOP_BAR_HEIGHT,
        w->maximized?DESKTOP_FLOATING:DESKTOP_TILE_MAX);
}
static void set_menu(bool open) {
    shell_grab(); menu=open; overview=false; switcher.open=false; volume_open=false;
    if (open) { launcher.query[0]=0; desktop_search(&launcher); quit_requested=leaving=signout_requested=false;pending_power=0; }
    sync_clients();
}
static void set_overview(bool open) {
    shell_grab(); overview=open; menu=false; switcher.open=false; volume_open=false;
    if (open) {
        u8 ids[DESKTOP_WINDOW_COUNT]; u32 n=desktop_window_list(windows,order,ids);
        overview_selected=active<DESKTOP_WINDOW_COUNT && windows[active].open?active:
            n?ids[0]:DESKTOP_WINDOW_COUNT;
        quit_requested=leaving=false;pending_power=0;
    }
    sync_clients();
}
static void overview_move(i32 offset) {
    u8 ids[DESKTOP_WINDOW_COUNT]; u32 n=desktop_window_list(windows,order,ids),chosen=0;
    for (u32 i=0;i<n;++i) if (ids[i]==overview_selected) chosen=i;
    if (n) overview_selected=ids[(u32)MAX(0,MIN((i32)n-1,(i32)chosen+offset))];
    else overview_selected=DESKTOP_WINDOW_COUNT;
}
static void switch_finish(bool accept) {
    u32 id=switcher.count?switcher.ids[switcher.selected]:DESKTOP_WINDOW_COUNT;
    switcher.open=false;
    if (accept && id<DESKTOP_WINDOW_COUNT && windows[id].open) focus_window(id);
    else sync_clients();
}
static int launch(const char *app, const char *arg) {
    u32 free_slot=NV_TASK_MAX;
    for (u32 i=0;i<NV_TASK_MAX;++i) if (!child_pids[i]) { free_slot=i; break; }
    if (free_slot==NV_TASK_MAX) { reap_children(false); return -NV_ENOSPC; }
    int pid=spawn(app,arg);
    if (pid<0) return pid;
    child_pids[free_slot]=(u32)pid;
    return 0;
}
static int client_poll(bool *dirty) {
    bool seen[DESKTOP_WINDOW_COUNT]={0};
    u32 s=desktop_scale(mode.width,mode.height),sw=mode.width/s,sh=mode.height/s;
    for (u32 index=0;index<NV_WINDOW_MAX;++index) {
        struct nv_window_entry e={.index=index};
        int r=nv_window_call(NV_WINDOW_ENUM,&e);
        if (r<0) return r;
        if (!r) continue;
        u32 id=DESKTOP_WINDOW_COUNT;
        for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) if (windows[i].client==e.id) { id=i; break; }
        if (id==DESKTOP_WINDOW_COUNT) {
            if (!strcmp(e.title,"Terminal") && !windows[DESKTOP_TERMINAL].client) id=DESKTOP_TERMINAL;
            else if (!strcmp(e.title,"Media") && !windows[DESKTOP_MEDIA].client) id=DESKTOP_MEDIA;
            else for (u32 i=DESKTOP_CLIENT_FIRST;i<DESKTOP_WINDOW_COUNT;++i) if (!windows[i].client) { id=i; break; }
            if (id==DESKTOP_WINDOW_COUNT) continue;
            struct desktop_window *w=&windows[id];
            *w=(struct desktop_window){.client=e.id,.pid=e.pid,.open=true,
                .w=MIN(sw-16,(e.requested_width+s-1)/s+2),
                .h=MIN(sh-DESKTOP_BAR_HEIGHT-16,(e.requested_height+s-1)/s+29)};
            w->x=MIN((i32)(sw-w->w), (i32)(sw-w->w)/2+(i32)(id%4)*12);
            w->y=MIN((i32)(sh-DESKTOP_BAR_HEIGHT-w->h),(i32)(sh-DESKTOP_BAR_HEIGHT-w->h)/2+(i32)(id%4)*12);
            strlcpy(w->title,e.title,sizeof(w->title));
            focus_window(id); *dirty=true;
        }
        seen[id]=true;
        if (!windows[id].minimized && e.generation && e.generation!=windows[id].generation) {
            r=client_snapshot(id,&e);
            /* A client can exit between ENUM and a tiled READ. Remove its
             * decoration normally instead of treating that as a display fault. */
            if (r==-NV_ENOENT) { seen[id]=false; continue; }
            if (r<0 && r!=-NV_EAGAIN) {
                note_error("Window frame",r);
                if (r!=-NV_ENOMEM) return r;
                /* Keep the desktop and the previous complete frame usable. */
                windows[id].generation=e.generation; *dirty=true;
            }
        }
    }
    for (u32 id=0;id<DESKTOP_WINDOW_COUNT;++id) if (windows[id].client && !seen[id]) {
        windows[id]=(struct desktop_window){0};
        if (captured==id) captured=DESKTOP_WINDOW_COUNT;
        if (drag_kind && drag_window==id) drag_kind=snap_preview=0;
        if (active==id) focus_top();
        *dirty=true;
    }
    sync_clients();
    return 0;
}
static bool suffix(const char *name, const char *ext) {
    u32 n=strlen(name), m=strlen(ext);
    return n>=m && !strcmp(name+n-m,ext);
}
static int join_file(char *out, const char *folder, const char *name) {
    if (!*name || !strcmp(name,".") || !strcmp(name,"..")) return -NV_EINVAL;
    for (u32 i=0;name[i];++i) if (name[i]=='/' || name[i]=='\\') return -NV_EINVAL;
    u32 n=strlcpy(out,folder,NV_PATH_MAX);
    if (n>=NV_PATH_MAX) return -NV_E2BIG;
    if (n!=1 || folder[0]!='/') {
        if (n+1>=NV_PATH_MAX) return -NV_E2BIG;
        out[n++]='/'; out[n]=0;
    }
    return strlcpy(out+n,name,NV_PATH_MAX-n)>=NV_PATH_MAX-n?-NV_E2BIG:0;
}
static void file_open_dialog(u32 action) {
    if (action!=DESKTOP_FILE_FOLDER && !count) { note("Select an item first."); return; }
    strlcpy(file_base,directory,sizeof(file_base));
    file_input[0]=file_target[0]=0;
    if (action!=DESKTOP_FILE_FOLDER) {
        strlcpy(file_input,entries[selected].name,sizeof(file_input));
        if (join_file(file_target,file_base,entries[selected].name)<0) {
            note("Path is too long."); return;
        }
    }
    file_mode=action;
    focus_window(DESKTOP_FILES);
}
static int file_submit(void) {
    char target[NV_PATH_MAX];
    int r=0;
    if (file_mode==DESKTOP_FILE_FOLDER || file_mode==DESKTOP_FILE_RENAME) {
        r=join_file(target,file_base,file_input);
        if (r<0) return r;
    }
    if (file_mode==DESKTOP_FILE_FOLDER) r=mkdir_path(target);
    else if (file_mode==DESKTOP_FILE_RENAME) {
        if (strcmp(file_target,target)) r=move_path(file_target,target);
        else { file_mode=DESKTOP_FILE_NORMAL; return 0; }
    } else if (file_mode==DESKTOP_FILE_DELETE) r=remove_path(file_target);
    else return 0;
    if (r<0) return r;
    file_mode=DESKTOP_FILE_NORMAL;
    selected=scroll=0;
    r=refresh();
    if (r<0) return r;
    struct nv_info disk;
    if (info(&disk)==0 && disk.disk_present) {
        r=control(NV_CTL_SYNC,0);
        if (r<0) { note("Changed in RAM; disk sync failed."); return r; }
    }
    note("Folder updated.");
    return 0;
}
static int file_key(u32 key) {
    if (key==27) { file_mode=DESKTOP_FILE_NORMAL; return 0; }
    if (file_mode==DESKTOP_FILE_DELETE) {
        if (key=='d' || key=='D') return file_submit();
        return 0;
    }
    if (key=='\n') return file_submit();
    u32 n=strlen(file_input);
    if (key=='\b' && n) file_input[n-1]=0;
    else if (key>=32 && key<127 && n+1<sizeof(file_input)) {
        file_input[n]=(char)key; file_input[n+1]=0;
    }
    return 0;
}
static void editor_reset(void) {
    editor_path[0]=editor_text[0]=0;
    editor_length=editor_cursor=editor_scroll=editor_mode=0;
    editor_dirty=false;
    focus_window(DESKTOP_EDITOR);
}
static int editor_load(const char *path) {
    int fd=open_file(path,NV_READ);
    if (fd<0) return fd;
    int size=seek_file(fd,0,2);
    if (size<0 || (u32)size>EDITOR_CAP) { close_file(fd); return -NV_E2BIG; }
    int r=seek_file(fd,0,0), result=0;
    u32 at=0;
    while (r>=0 && at<(u32)size) {
        r=take(fd,editor_scratch+at,MIN(16384u,(u32)size-at));
        if (r<=0) { result=r<0?r:-NV_EIO; break; }
        at+=(u32)r;
    }
    close_file(fd);
    if (r<0 && !result) result=r;
    if (result<0) return result;
    for (u32 i=0;i<(u32)size;++i)
        if (!editor_scratch[i]) return -NV_EINVAL;
    memcpy(editor_text,editor_scratch,(u32)size);
    editor_text[size]=0;
    editor_length=(u32)size;
    editor_cursor=editor_scroll=editor_mode=0;
    editor_dirty=false;
    strlcpy(editor_path,path,sizeof(editor_path));
    focus_window(DESKTOP_EDITOR);
    return 0;
}
static int editor_atomic_save(const char *path) {
    char temp[NV_PATH_MAX];
    u32 prefix=0;
    for (u32 i=0;path[i];++i) if (path[i]=='/') prefix=i+1;
    if (prefix+38>=sizeof(temp)) return -NV_E2BIG;
    memcpy(temp,path,prefix);
    strlcpy(temp+prefix,".desktop-",sizeof(temp)-prefix);
    u32 end=strlen(temp);
    number64(temp+end,clock_ticks(),16);
    end=strlen(temp);
    temp[end++]='-';
    int fd=-NV_EEXIST;
    for (u32 i=0;i<32 && fd==-NV_EEXIST;++i) {
        number(temp+end,i,10);
        fd=open_file(temp,NV_WRITE|NV_CREATE|NV_EXCL);
    }
    if (fd<0) return fd;
    u32 at=0;
    int r=0;
    while (at<editor_length) {
        int n=emit(fd,editor_text+at,MIN(16384u,editor_length-at));
        if (n<=0) { r=n<0?n:-NV_EIO; break; }
        at+=(u32)n;
    }
    int closed=close_file(fd);
    if (!r && closed<0) r=closed;
    if (!r) r=replace_file(temp,path);
    if (r<0) remove_path(temp);
    return r;
}
static int editor_load_next(void);
static int editor_save_target(const char *target) {
    if (!*target || strlen(target)>=sizeof(editor_path)) return -NV_EINVAL;
    int r=editor_atomic_save(target);
    if (r<0) return r;
    strlcpy(editor_path,target,sizeof(editor_path));
    editor_dirty=false;
    note("Document saved.");
    struct nv_info disk;
    if (info(&disk)==0 && disk.disk_present) {
        r=control(NV_CTL_SYNC,0);
        if (r<0) { note("Saved in RAM; disk sync failed."); return r; }
    }
    editor_mode=DESKTOP_EDIT_NORMAL;
    return editor_load_next();
}
static int editor_save(void) {
    if (!*editor_path) {
        default_home_file(editor_input,"Untitled.txt");
        editor_mode=DESKTOP_EDIT_PATH;
        return 0;
    }
    return editor_save_target(editor_path);
}
static int editor_load_next(void) {
    u32 action=editor_next_action;
    editor_next_action=0;
    editor_mode=DESKTOP_EDIT_NORMAL;
    if (action==1) editor_reset();
    else if (action==2) { windows[DESKTOP_EDITOR].open=false; focus_top(); }
    else if (action==3) return editor_load(editor_next_path);
    else if (action==4) quit_requested=true;
    return 0;
}
static int editor_request(u32 action, const char *path) {
    if (action==3 && strlcpy(editor_next_path,path,sizeof(editor_next_path))>=sizeof(editor_next_path))
        return -NV_E2BIG;
    editor_next_action=action;
    focus_window(DESKTOP_EDITOR);
    if (editor_dirty) { editor_mode=DESKTOP_EDIT_CLOSE; return 0; }
    return editor_load_next();
}
static void close_window(u32 id) {
    if (windows[id].client) {
        focus_window(id);
        send_client(id,NV_WINDOW_EVENT_CLOSE,0,NULL);
        return;
    }
    if (id==DESKTOP_EDITOR && editor_dirty) {
        editor_next_action=2;
        editor_mode=DESKTOP_EDIT_CLOSE;
        focus_window(id);
        return;
    }
    windows[id].open=false;
    windows[id].minimized=false;
    if (id==DESKTOP_FILES) file_mode=DESKTOP_FILE_NORMAL;
    if (id==DESKTOP_ACCOUNTS) {
        account_call(NV_ACCOUNT_CANCEL,NULL);account.busy=false;account_page(&account,ACCOUNT_LIST);
    }
    if (id==DESKTOP_SETTINGS) settings_confirm=0;
    focus_top();
}
static int go_place(u32 index) {
    static const char *const places[]={"/home","/drives/D","/drives/E","/drives/F",
                                       "/","/apps","/tmp"};
    if (index>=ARRAY_LEN(places) || (index<NV_VOLUME_MAX && index>=volumes)) return 0;
    int r=chdir_path(index==0 && *account.info.home?account.info.home:places[index]);
    if (r>=0) { selected=scroll=0; r=refresh(); }
    return r;
}
static int open_selected(void) {
    if (!count) return 0;
    if (entries[selected].kind==NV_DIR) {
        int r=chdir_path(entries[selected].name);
        if (r>=0) { selected=scroll=0; r=refresh(); }
        return r;
    }
    char path[NV_PATH_MAX];
    u32 n=strlcpy(path,directory,sizeof(path));
    if (n>=sizeof(path)) return -NV_E2BIG;
    if (n!=1 || path[0]!='/') {
        if (n+1>=sizeof(path)) return -NV_E2BIG;
        path[n++]='/'; path[n]=0;
    }
    if (strlcpy(path+n,entries[selected].name,sizeof(path)-n)>=sizeof(path)-n)
        return -NV_E2BIG;
    if (suffix(path,".txt") || suffix(path,".md"))
        return editor_request(3,path);
    if (suffix(path,".nvd")) return launch("/apps/folio",path);
    bool media=suffix(path,".mp3") || suffix(path,".mp2") || suffix(path,".flac") ||
               suffix(path,".wav") || suffix(path,".mpg") || suffix(path,".mpeg");
    if (media) {
        if (!audio_ready && !suffix(path,".mpg") && !suffix(path,".mpeg"))
            { note("No HDA audio output detected."); return 0; }
        return launch("/apps/media",path);
    }
    note("No viewer for this file type.");
    return 0;
}
static int start_app(u32 index) {
    set_menu(false);
    if (index==0) { focus_window(DESKTOP_FILES); return 0; }
    if (index==1) { focus_window(DESKTOP_EDITOR); return 0; }
    if (index==2) {
        if (windows[DESKTOP_TERMINAL].client) { focus_window(DESKTOP_TERMINAL); return 0; }
        return launch("/apps/terminal","--terminal");
    }
    if (index==3) {
        if (windows[DESKTOP_MEDIA].client) { focus_window(DESKTOP_MEDIA); return 0; }
        return launch("/apps/media","");
    }
    if (index==4) return launch("/apps/folio", "");
    if (index==5) {
        account_refresh(&account);account_page(&account,ACCOUNT_LIST);
        focus_window(DESKTOP_ACCOUNTS);return 0;
    }
    if (index==6) {
        settings_confirm=settings_focus=0;focus_window(DESKTOP_SETTINGS);
    }
    return 0;
}
static void preference_path(char *out,const char *name) {
    strlcpy(out,account.info.home,NV_PATH_MAX);
    u32 end=strlen(out);strlcpy(out+end,name,NV_PATH_MAX-end);
}
struct desktop_preferences { u32 magic,theme,idle_minutes,reserved; };
static void wallpaper_update(void) {
    if (!wallpaper) return;
    struct nv_canvas c={wallpaper,mode.width,0,mode.height,mode.format};
    struct desktop_clip clip={0,0,mode.width,mode.height};
    desktop_wallpaper(&c,clip,mode.height,theme,false);
}
static int preferences_save(void) {
    struct desktop_preferences p={0x3155494e,theme,idle_minutes,0};
    char path[NV_PATH_MAX],temp[NV_PATH_MAX];
    preference_path(path,"/.desktop");preference_path(temp,"/.desktop-new");
    int fd=open_file(temp,NV_WRITE|NV_CREATE|NV_TRUNC);
    if (fd<0) return fd;
    int r=emit(fd,&p,sizeof(p)),closed=close_file(fd);
    if (r!=(int)sizeof(p) || closed<0) { remove_path(temp);return r<0?r:closed<0?closed:-NV_EIO; }
    return replace_file(temp,path);
}
static void preferences_load(void) {
    theme=0;idle_minutes=5;
    char path[NV_PATH_MAX];preference_path(path,"/.desktop");
    int fd=open_file(path,NV_READ);
    if (fd>=0) {
        struct desktop_preferences p;struct nv_stat64 st;
        if (!stat_file64(fd,&st) && st.size==sizeof(p) && take(fd,&p,sizeof(p))==(int)sizeof(p) &&
            p.magic==0x3155494e && p.theme<=2 && !p.reserved &&
            (p.idle_minutes==1 || p.idle_minutes==5 || p.idle_minutes==15)) {
            theme=p.theme;idle_minutes=p.idle_minutes;
        }
        close_file(fd);
    }
    struct nv_account_policy policy={idle_minutes*60*100,0};
    account_call(NV_ACCOUNT_POLICY_SET,&policy);
    wallpaper_update();
}
static int session_enter(void) {
    int r=account_refresh(&account);
    if (r<0) return r;
    if (!*directory) {
        preferences_load();
        r=chdir_path(account.info.home);
        if (r<0) return r;
        r=refresh();
    }
    meta_down=super_armed=false;sync_clients();
    return r;
}
static int session_lock(void) {
    account_call(NV_ACCOUNT_CANCEL,NULL);account.busy=false;
    int r=account_call(NV_ACCOUNT_LOCK,NULL);
    if (r<0) return r;
    shell_grab();menu=overview=switcher.open=volume_open=false;
    quit_requested=leaving=signout_requested=false;pending_power=settings_confirm=0;
    account.gate=true;account_refresh(&account);account_page(&account,ACCOUNT_LOGIN);
    hover_kind=DESKTOP_HIT_NONE;meta_down=super_armed=false;
    sync_clients();return 0;
}
static int session_request_signout(void) {
    set_menu(false);signout_requested=true;pending_power=0;
    if (editor_dirty) return editor_request(4,NULL);
    quit_requested=true;return 0;
}
static int session_signout(void) {
    int r=account_call(NV_ACCOUNT_LOGOUT,NULL);
    if (r<0) return r;
    shell_grab();menu=overview=switcher.open=volume_open=false;
    quit_requested=leaving=signout_requested=false;
    theme=settings_tab=settings_confirm=pending_power=0;idle_minutes=5;
    wallpaper_update();
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) {
        struct desktop_window *w=&windows[i];
        struct client_cache *cache=&client_cache[i];
        if (cache->front) account_wipe(cache->front,(usize)cache->capacity*4);
        if (cache->back) account_wipe(cache->back,(usize)cache->capacity*4);
        account_wipe(&cache->text,sizeof(cache->text));
        w->open=w->minimized=false;w->client=w->pid=w->generation=0;
        w->pixels=NULL;w->text=NULL;w->title[0]=0;order[i]=(u8)i;
    }
    memset(child_pids,0,sizeof(child_pids));account_wipe(editor_text,sizeof(editor_text));
    account_wipe(editor_scratch,sizeof(editor_scratch));
    account_wipe(editor_path,sizeof(editor_path));account_wipe(editor_input,sizeof(editor_input));
    account_wipe(editor_next_path,sizeof(editor_next_path));
    editor_length=editor_cursor=editor_scroll=editor_mode=editor_next_action=0;editor_dirty=false;
    account_wipe(terminal_lines,sizeof(terminal_lines));
    account_wipe(terminal_input,sizeof(terminal_input));account_wipe(terminal_history,sizeof(terminal_history));
    terminal_first=terminal_count=terminal_column=terminal_length=0;terminal_new_line();
    account_wipe(directory,sizeof(directory));account_wipe(message,sizeof(message));
    account_wipe(entries,sizeof(entries));
    account_wipe(file_input,sizeof(file_input));account_wipe(file_target,sizeof(file_target));account_wipe(file_base,sizeof(file_base));
    count=selected=scroll=file_mode=0;show_hidden=false;active=DESKTOP_WINDOW_COUNT;
    shortcut_selected=0xffffffffu;account_wipe(&account,sizeof(account));account.gate=true;
    account_refresh(&account);account_page(&account,ACCOUNT_LOGIN);sync_clients();
    return 0;
}
static int settings_activate(u32 hit) {
    if (hit<3) { settings_tab=hit;settings_focus=0;return 0; }
    if (hit>=3 && hit<=5 && settings_tab<2) {
        if (!settings_tab) { theme=hit-3;wallpaper_update(); }
        else {
            u32 minutes=hit==3?1u:hit==4?5u:15u;
            struct nv_account_policy policy={minutes*60*100,0};
            int r=account_call(NV_ACCOUNT_POLICY_SET,&policy);
            if (r<0) return r;
            idle_minutes=minutes;
        }
        return preferences_save();
    }
    if (settings_tab==1) {
        if (hit==6) return session_lock();
        if (hit==7) return start_app(5);
    } else if (settings_tab==2) {
        if (hit==6) { int r=control(NV_CTL_SYNC,0);if (!r) note("Mounted drives saved.");return r; }
        if (hit==7 || hit==8) {
            if (account.info.role!=NV_ACCOUNT_ADMIN) return -NV_EACCESS;
            settings_confirm=hit==7?NV_CTL_REBOOT:NV_CTL_POWEROFF;settings_focus=0;return 0;
        }
    }
    if (hit==12) { settings_confirm=0;return 0; }
    if (hit==13 && settings_confirm) {
        pending_power=settings_confirm;settings_confirm=0;signout_requested=false;
        if (editor_dirty) return editor_request(4,NULL);
        quit_requested=true;
    }
    return 0;
}
static void editor_ensure_visible(void) {
    const struct desktop_window *w=&windows[DESKTOP_EDITOR];
    u32 s=desktop_scale(mode.width,mode.height);
    u32 cols=MAX(1u,(w->w*s-36*s)/(DESKTOP_EDITOR_CELL*s));
    u32 row=0,col=0;
    for (u32 i=0;i<editor_cursor;++i) {
        if (editor_text[i]=='\n') { ++row; col=0; }
        else if (++col>=cols) { ++row; col=0; }
    }
    u32 visible=MAX(1u,(w->h-94)/DESKTOP_EDITOR_LINE);
    if (row<editor_scroll) editor_scroll=row;
    if (row>=editor_scroll+visible) editor_scroll=row-visible+1;
}
static u32 editor_cursor_at(u32 rx, u32 ry) {
    const struct desktop_window *w=&windows[DESKTOP_EDITOR];
    u32 s=desktop_scale(mode.width,mode.height);
    u32 cols=MAX(1u,(w->w*s-36*s)/(DESKTOP_EDITOR_CELL*s));
    u32 target_row=editor_scroll+(ry>73?(ry-73)/DESKTOP_EDITOR_LINE:0);
    u32 target_col=rx>19?MIN((rx-19)/DESKTOP_EDITOR_CELL,cols-1):0;
    u32 row=0,col=0;
    for (u32 i=0;i<editor_length;++i) {
        if (row==target_row && col>=target_col) return i;
        if (editor_text[i]=='\n') {
            if (row==target_row) return i;
            ++row; col=0;
        } else if (++col>=cols) { ++row; col=0; }
        if (row>target_row) return i+1;
    }
    return editor_length;
}
static void editor_insert(char ch) {
    if (editor_length>=EDITOR_CAP) { note("Editor memory is full; file storage is unaffected."); return; }
    memmove(editor_text+editor_cursor+1,editor_text+editor_cursor,
            editor_length-editor_cursor+1);
    editor_text[editor_cursor++]=ch;
    ++editor_length;
    editor_dirty=true;
    editor_ensure_visible();
}
static int editor_key(u32 key, u32 flags) {
    if (editor_mode==DESKTOP_EDIT_PATH) {
        if (key==27) { editor_mode=DESKTOP_EDIT_NORMAL; editor_next_action=0; return 0; }
        if (key=='\n') {
            return editor_save_target(editor_input);
        }
        u32 n=strlen(editor_input);
        if (key=='\b' && n) editor_input[n-1]=0;
        else if (key>=32 && key<127 && n+1<sizeof(editor_input)) {
            editor_input[n]=(char)key; editor_input[n+1]=0;
        }
        return 0;
    }
    if (editor_mode==DESKTOP_EDIT_CLOSE) {
        if (key==27) { editor_mode=DESKTOP_EDIT_NORMAL; editor_next_action=0; }
        else if (key=='s' || key=='S') return editor_save();
        else if (key=='d' || key=='D') return editor_load_next();
        return 0;
    }
    if (flags&NV_KEY_CTRL) {
        if (key=='s' || key=='S') {
            if (flags&NV_KEY_SHIFT) {
                if (*editor_path) strlcpy(editor_input,editor_path,sizeof(editor_input));
                else default_home_file(editor_input,"Untitled.txt");
                editor_mode=DESKTOP_EDIT_PATH;
                return 0;
            }
            return editor_save();
        }
        if (key=='n' || key=='N') return editor_request(1,NULL);
    }
    if (key==NV_KEY_LEFT && editor_cursor) --editor_cursor;
    else if (key==NV_KEY_RIGHT && editor_cursor<editor_length) ++editor_cursor;
    else if (key==NV_KEY_HOME) {
        while (editor_cursor && editor_text[editor_cursor-1]!='\n') --editor_cursor;
    } else if (key==NV_KEY_END) {
        while (editor_cursor<editor_length && editor_text[editor_cursor]!='\n') ++editor_cursor;
    } else if (key==NV_KEY_UP || key==NV_KEY_DOWN) {
        u32 start=editor_cursor;
        while (start && editor_text[start-1]!='\n') --start;
        u32 col=editor_cursor-start;
        if (key==NV_KEY_UP && start) {
            u32 previous=start-1;
            while (previous && editor_text[previous-1]!='\n') --previous;
            editor_cursor=MIN(previous+col,start-1);
        } else if (key==NV_KEY_DOWN) {
            u32 next=start;
            while (next<editor_length && editor_text[next]!='\n') ++next;
            if (next<editor_length) {
                u32 end=next+1;
                while (end<editor_length && editor_text[end]!='\n') ++end;
                editor_cursor=MIN(next+1+col,end);
            }
        }
    } else if (key=='\b' && editor_cursor) {
        memmove(editor_text+editor_cursor-1,editor_text+editor_cursor,
                editor_length-editor_cursor+1);
        --editor_cursor; --editor_length; editor_dirty=true;
    } else if (key==NV_KEY_DELETE && editor_cursor<editor_length) {
        memmove(editor_text+editor_cursor,editor_text+editor_cursor+1,
                editor_length-editor_cursor);
        --editor_length; editor_dirty=true;
    } else if (key=='\n') editor_insert('\n');
    else if (!(flags&(NV_KEY_CTRL|NV_KEY_ALT)) && key>=32 && key<127)
        editor_insert((char)key);
    editor_ensure_visible();
    return 0;
}
static void terminal_file(const char *path) {
    int fd=open_file(path,NV_READ);
    if (fd<0) { terminal_println(error_name(fd)); return; }
    char bytes[513]; int n;
    while ((n=take(fd,bytes,512))>0) { bytes[n]=0; terminal_print(bytes); }
    if (n<0) terminal_println(error_name(n));
    terminal_print("\n");
    close_file(fd);
}
static int terminal_command(char *line) {
    char *args[24];
    int n=tokenize(line,args,ARRAY_LEN(args));
    if (n<=0) return n<0?n:0;
    const char *cmd=args[0];
    if (!strcmp(cmd,"help")) {
        terminal_println("Desktop terminal: help, pwd/where, ls/glance [PATH], cd/step PATH,");
        terminal_println("cat/unfold FILE, mkdir/nest PATH, rm/prune PATH, net, ping, wget,");
        terminal_println("anchor, media, folio, loom, exit, clear. Run loom for the full shell.");
        return 0;
    }
    if (!strcmp(cmd,"exit") && n==1) { close_window(DESKTOP_TERMINAL); return 0; }
    if (!strcmp(cmd,"clear")) {
        terminal_first=terminal_count=terminal_column=0; terminal_new_line(); return 0;
    }
    if ((!strcmp(cmd,"pwd") || !strcmp(cmd,"where")) && n==1) {
        terminal_println(directory); return 0;
    }
    if ((!strcmp(cmd,"cd") || !strcmp(cmd,"step")) && n==2) {
        int r=chdir_path(args[1]);
        return r<0?r:refresh();
    }
    if ((!strcmp(cmd,"ls") || !strcmp(cmd,"glance")) && n<=2) {
        for (u32 i=0;i<DESKTOP_ITEMS;++i) {
            struct nv_dirent64 item;
            int r=list_dir64(n==2?args[1]:".",i,&item);
            if (r<=0) return r<0?r:0;
            terminal_print(item.kind==NV_DIR?"[dir]  ":"       ");
            terminal_println(item.name);
        }
        return 0;
    }
    if ((!strcmp(cmd,"cat") || !strcmp(cmd,"unfold")) && n==2) {
        terminal_file(args[1]); return 0;
    }
    if ((!strcmp(cmd,"mkdir") || !strcmp(cmd,"nest")) && n==2) return mkdir_path(args[1]);
    if ((!strcmp(cmd,"rm") || !strcmp(cmd,"prune")) && n==2) return remove_path(args[1]);
    if (!strcmp(cmd,"anchor") && n==1) return control(NV_CTL_SYNC,0);
    if (!strcmp(cmd,"net")) {
        if (n>=3 && !strcmp(args[1],"wifi") && !strcmp(args[2],"join")) {
            terminal_println("Wi-Fi password entry requires the full Loom terminal."); return 0;
        }
        return network_command(n,args);
    }
    if (!strcmp(cmd,"ping")) return ping_command(n,args);
    if (!strcmp(cmd,"wget")) return wget_command(n,args);
    if (!strcmp(cmd,"loom") && n==1) return launch("/apps/terminal","--terminal");
    if (!strcmp(cmd,"folio") && n<=2) return launch("/apps/folio",n==2?args[1]:"");
    if (!strcmp(cmd,"media") && n<=2) return launch("/apps/media",n==2?args[1]:"");
    terminal_println("Unknown here. Type help, or run loom for every command.");
    return 0;
}
static int terminal_run(void) {
    char line[sizeof(terminal_input)];
    strlcpy(line,terminal_input,sizeof(line));
    strlcpy(terminal_history,line,sizeof(terminal_history));
    terminal_print("C:/ :: "); terminal_println(line);
    terminal_input[0]=0; terminal_length=0;
    int r=terminal_command(line);
    if (r<0) { terminal_print("Error: "); terminal_println(error_name(r)); }
    int refreshed=refresh();
    return refreshed<0?refreshed:0;
}
static int terminal_key(u32 key, u32 flags) {
    if (key=='\n') return terminal_run();
    if (key=='\b' && terminal_length) terminal_input[--terminal_length]=0;
    else if (key==NV_KEY_UP) {
        strlcpy(terminal_input,terminal_history,sizeof(terminal_input));
        terminal_length=strlen(terminal_input);
    } else if (!(flags&(NV_KEY_CTRL|NV_KEY_ALT)) && key>=32 && key<127 &&
               terminal_length+1<sizeof(terminal_input)) {
        terminal_input[terminal_length++]=(char)key;
        terminal_input[terminal_length]=0;
    }
    return 0;
}

int user_main(const char *args) {
    if (app_help("desktop",args)) return 0;
    if (*args) { println("Usage: desktop"); return 1; }
    int r=nv_display_info(&mode);
    if (r<0) { println("Desktop requires a UEFI firmware pixel framebuffer."); return 1; }
    if (mode.api_version!=NV_DISPLAY_API_VERSION ||
        (mode.format!=NV_DISPLAY_BGRX8 && mode.format!=NV_DISPLAY_RGBX8) ||
        mode.width<640 || mode.height<480 || mode.width>8192 ||
        (u64)mode.width*4>mode.max_copy_bytes ||
        mode.max_copy_bytes>NV_DISPLAY_MAX_COPY) {
        println("Desktop: unsupported display mode (minimum 640x480)."); return 1;
    }
    u32 rows=mode.max_copy_bytes/(mode.width*4);
    u32 pages=(mode.max_copy_bytes+NV_PAGE-1)/NV_PAGE;
    u32 *tile=grow((i32)pages);
    if ((iptr)tile<0) { report_error("Desktop: memory",(int)(iptr)tile); return 1; }
    wallpaper=grow(((u64)mode.width*mode.height*4+NV_PAGE-1)/NV_PAGE);
    if ((iptr)wallpaper<0) wallpaper=NULL; /* Low-memory fallback stays usable. */
    wallpaper_update();
    u32 s=desktop_scale(mode.width,mode.height), sw=mode.width/s, sh=mode.height/s;
    windows[DESKTOP_FILES]=(struct desktop_window){.x=125,.y=42,
        .w=MIN(720u,sw-145),.h=MIN(500u,sh-83)};
    windows[DESKTOP_EDITOR]=(struct desktop_window){.x=140,.y=62,
        .w=MIN(680u,sw-155),.h=MIN(470u,sh-104)};
    windows[DESKTOP_TERMINAL]=(struct desktop_window){.x=150,.y=86,
        .w=MIN(475u,sw-160),.h=MIN(250u,sh-126)};
    windows[DESKTOP_ACCOUNTS]=(struct desktop_window){.x=(i32)(sw-MIN(560u,sw-40))/2,.y=20,
        .w=MIN(560u,sw-40),.h=MIN(455u,sh-DESKTOP_BAR_HEIGHT-20)};
    windows[DESKTOP_SETTINGS]=windows[DESKTOP_ACCOUNTS];
    struct nv_info64 hardware;
    if (!info64(&hardware)) {
        desktop_size(system_memory,hardware.ram_pages*NV_PAGE);
        strlcpy(system_memory+strlen(system_memory)," RAM / native x86_64",
            sizeof(system_memory)-strlen(system_memory));
    }
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) order[i]=(u8)i;
    desktop_search(&launcher);
    terminal_new_line();
    terminal_println("Nuvora desktop terminal. Type help for commands.");
    r=account_client_open(&account);
    if (r<0) { report_error("Desktop: accounts",r);return 1; }
    r=nv_display_acquire();
    if (r<0) { report_error("Desktop: display",r); return 1; }
    r=nv_window_call(NV_WINDOW_SERVER_ACQUIRE,NULL);
    if (r<0) { nv_display_release(); report_error("Desktop: window server",r); return 1; }
    if (!account.gate && (r=session_enter())<0) {
        nv_window_call(NV_WINDOW_SERVER_RELEASE,NULL);nv_display_release();return 1;
    }
    pointer_x=mode.width/2; pointer_y=mode.height/2;
    struct nv_audio_info audio;
    audio_ready=nv_audio_info(&audio)==0 && audio.api_version==NV_AUDIO_API_VERSION &&
                audio.outputs>0;
    struct nv_audio_volume setting;
    if (nv_audio_get_volume(&setting)==0 && setting.percent<=100)
        volume_percent=setting.percent;
    bool dirty=true;
    u32 last_file=0xffffffffu; u64 last_file_tick=0;
    u32 last_shortcut=0xffffffffu; u64 last_shortcut_tick=0;
    u32 last_title=0xffffffffu; u64 last_title_tick=0,last_reap=0,last_security=0;
    for (;;) {
        u64 now=clock_ticks();
        if (now-last_security>=25) {
            struct nv_account_info status;
            if (!account_call(NV_ACCOUNT_INFO,&status) && (status.flags&NV_AUTH_LOCKED) && !account.gate) {
                r=session_lock();if (r<0) break;dirty=true;
            }
            u32 minutes=(u32)MIN(now/(60*100),(u64)0xffffffffu);
            if (minutes!=uptime_minutes) { uptime_minutes=minutes;dirty=true; }
            last_security=now;
        }
        if (account.busy) {
            u32 before=account.progress.completed;
            int action=account_client_poll(&account);
            if (action==ACCOUNT_OPEN_DESKTOP) { r=session_enter();if (r<0) break;dirty=true; }
            if (!account.busy || before!=account.progress.completed) {
                if (!account.busy) dirty=true;
                else if (account.gate) {
                    struct account_rect box=account_gate_rect(sw,sh,&account);
                    damage=(struct desktop_clip){box.x*s,(box.y+box.h-94)*s,
                        (box.x+box.w)*s,(box.y+box.h-67)*s};damaged=true;
                } else damage_window(&windows[DESKTOP_ACCOUNTS]);
            }
        }
        r=client_poll(&dirty); if (r<0) break;
        if (switcher.open) {
            u32 old_count=switcher.count,old_id=switcher.ids[switcher.selected];
            desktop_switch_prune(&switcher,windows);
            if (!switcher.open || old_count!=switcher.count ||
                old_id!=switcher.ids[switcher.selected]) { dirty=true; sync_clients(); }
        }
        if (overview && (overview_selected>=DESKTOP_WINDOW_COUNT ||
            !windows[overview_selected].open)) {
            u32 old=overview_selected; overview_move(0);
            if (old!=overview_selected) dirty=true;
        }
        struct nv_audio_volume level;
        if (nv_audio_get_volume(&level)==0 && level.percent!=volume_percent) {
            volume_percent=level.percent; dirty=true;
        }
        if (clock_ticks()-last_reap>=50) { reap_children(false); last_reap=clock_ticks(); }
        if (dirty) { r=draw(tile,rows); if (r<0) break; dirty=false; damaged=false; }
        else if (damaged) {
            r=draw_region(tile,rows,damage.left,damage.top,damage.right-damage.left,damage.bottom-damage.top);
            if (r<0) break;
            damaged=false;
        }
        struct nv_pointer_event event;
        int mouse=nv_pointer_poll(&event);
        if (mouse<0) { r=mouse; break; }
        if (mouse==1) {
            u32 previous_x=pointer_x, previous_y=pointer_y;
            bool had_pointer=pointer_visible;
            pointer_visible=true;
            bool absolute=(event.buttons&NV_POINTER_ABSOLUTE)!=0;
            u32 oldx=pointer_x/s, oldy=pointer_y/s;
            pointer_x=desktop_pointer_axis(pointer_x,event.dx,mode.width,absolute);
            pointer_y=desktop_pointer_axis(pointer_y,event.dy,mode.height,absolute);
            u32 x=pointer_x/s,y=pointer_y/s;
            if (account.gate) {
                if ((event.buttons&NV_POINTER_LEFT) && !(pointer_buttons&NV_POINTER_LEFT)) {
                    struct account_rect box=account_gate_rect(sw,sh,&account);
                    if (x>=box.x && x<box.x+box.w && y>=box.y && y<box.y+box.h) {
                        int action=account_activate(&account,account_ui_hit(box.w,box.h,&account,x-box.x,y-box.y));
                        if (action==ACCOUNT_OPEN_DESKTOP) { r=session_enter();if (r<0) break; }
                    }
                }
                pointer_buttons=event.buttons&7;dirty=true;
                goto keyboard_input;
            }
            struct desktop_view hover_view=view();
            struct desktop_hit hover=desktop_hit(mode.width,mode.height,&hover_view,pointer_x,pointer_y);
            if (menu && hover.kind==DESKTOP_HIT_MENU) {
                for (u32 i=0;i<launcher.count;++i) if (launcher.ids[i]==hover.index &&
                    launcher.selected!=i) { launcher.selected=i; dirty=true; }
            } else if (overview && hover.kind==DESKTOP_HIT_OVERVIEW &&
                       overview_selected!=hover.index) { overview_selected=hover.index; dirty=true; }
            else if (switcher.open && hover.kind==DESKTOP_HIT_SWITCH &&
                     switcher.selected!=hover.index) { switcher.selected=hover.index; dirty=true; }
            if (hover_kind!=hover.kind || hover_window!=hover.window) {
                if (hover_kind==DESKTOP_HIT_OVERVIEW_BUTTON || hover.kind==DESKTOP_HIT_OVERVIEW_BUTTON)
                    dirty=true;
                if (hover_kind==DESKTOP_HIT_TASK || hover.kind==DESKTOP_HIT_TASK ||
                    hover_kind==DESKTOP_HIT_MEDIA || hover.kind==DESKTOP_HIT_MEDIA) dirty=true;
                if (hover_window<DESKTOP_WINDOW_COUNT && windows[hover_window].open)
                    damage_window(&windows[hover_window]);
                hover_kind=hover.kind; hover_window=hover.window;
                if (hover_window<DESKTOP_WINDOW_COUNT && windows[hover_window].open)
                    damage_window(&windows[hover_window]);
            }
            if (captured<DESKTOP_WINDOW_COUNT) {
                send_client(captured,NV_WINDOW_EVENT_POINTER,0,&event);
                if (!(event.buttons&NV_POINTER_LEFT)) captured=DESKTOP_WINDOW_COUNT;
            } else if (!(event.buttons&NV_POINTER_LEFT) && hover.kind==DESKTOP_HIT_CLIENT)
                send_client(hover.window,NV_WINDOW_EVENT_POINTER,0,&event);
            if (event.wheel && hover.window<DESKTOP_WINDOW_COUNT &&
                !windows[hover.window].client) {
                if (hover.window==DESKTOP_FILES) {
                    i32 next=(i32)selected-event.wheel*3;
                    selected=count?(u32)MAX(0,MIN(next,(i32)count-1)):0;
                    scroll_to_selection(); dirty=true;
                } else if (hover.window==DESKTOP_EDITOR) {
                    editor_scroll=(u32)MAX(0,(i32)editor_scroll-event.wheel*3); dirty=true;
                }
            }
            if (event.wheel && overview) {
                i32 page=(i32)desktop_overview_page_size(sw);
                overview_move(event.wheel>0?-page:page); dirty=true;
            }
            if ((event.buttons&NV_POINTER_LEFT) && (pointer_buttons&NV_POINTER_LEFT) && drag_kind) {
                if (drag_kind==DESKTOP_HIT_VOLUME_SLIDER) {
                    u32 level=x<=sw-174?0:x>=sw-19?100:(x-(sw-174))*100/155;
                    if (level!=volume_percent) {
                        r=nv_audio_set_volume(level);
                        if (r<0) note_error("Volume",r);
                        else volume_percent=level;
                    }
                } else {
                    struct desktop_window *w=&windows[drag_window];
                    damage_window(w);
                    if (drag_kind==DESKTOP_HIT_TITLE && (x!=oldx || y!=oldy)) {
                        last_title=0xffffffffu;
                        if (w->maximized || w->tiled) {
                            u32 old_width=w->w;
                            desktop_place(w,sw,sh-DESKTOP_BAR_HEIGHT,DESKTOP_FLOATING);
                            w->x=(i32)x-(i32)((u64)drag_offset_x*w->w/old_width);
                            w->y=(i32)y-(i32)MIN(drag_offset_y,27u);
                        } else { w->x+=(i32)x-(i32)oldx; w->y+=(i32)y-(i32)oldy; }
                        w->x=MAX(0,MIN((i32)sw-(i32)w->w,w->x));
                        w->y=MAX(0,MIN((i32)sh-DESKTOP_BAR_HEIGHT-(i32)w->h,w->y));
                        u32 next=desktop_snap_target(sw,x,y);
                        if (next!=snap_preview) { snap_preview=next; dirty=true; }
                    } else if (drag_kind==DESKTOP_HIT_RESIZE) {
                        w->tiled=DESKTOP_FLOATING;
                        i32 dx=(i32)x-(i32)oldx,dy=(i32)y-(i32)oldy;
                        i32 minw=300,minh=160;
                        if (drag_window==DESKTOP_ACCOUNTS) { minw=MIN(500u,sw);minh=MIN(429u,sh-DESKTOP_BAR_HEIGHT); }
                        if (drag_window==DESKTOP_SETTINGS) { minw=MIN(500u,sw);minh=MIN(404u,sh-DESKTOP_BAR_HEIGHT); }
                        if (w->text || !strcmp(w->title,"Folio")) {
                            minw=MAX(minw,(i32)((480+s-1)/s)+2);
                            minh=MAX(minh,(i32)((325+s-1)/s)+29);
                        }
                        if (drag_edges&2) w->w=(u32)MAX(minw,MIN((i32)sw-w->x,(i32)w->w+dx));
                        if (drag_edges&8) w->h=(u32)MAX(minh,MIN((i32)sh-DESKTOP_BAR_HEIGHT-w->y,(i32)w->h+dy));
                        if (drag_edges&1) {
                            i32 right=w->x+(i32)w->w;
                            w->x=MAX(0,MIN(right-minw,w->x+dx)); w->w=(u32)(right-w->x);
                        }
                        if (drag_edges&4) {
                            i32 bottom=w->y+(i32)w->h;
                            w->y=MAX(0,MIN(bottom-minh,w->y+dy)); w->h=(u32)(bottom-w->y);
                        }
                        if (drag_window==DESKTOP_EDITOR) editor_ensure_visible();
                        if (drag_window==DESKTOP_FILES) scroll_to_selection();
                    }
                    damage_window(w); sync_clients();
                }
            }
            if ((event.buttons&NV_POINTER_LEFT) && !(pointer_buttons&NV_POINTER_LEFT)) {
                struct desktop_view v=view();
                struct desktop_hit hit=desktop_hit(mode.width,mode.height,&v,pointer_x,pointer_y);
                bool was_overview=overview;
                if (overview && (hit.kind==DESKTOP_HIT_TASK || hit.kind==DESKTOP_HIT_MEDIA))
                    set_overview(false);
                if (hit.kind==DESKTOP_HIT_SWITCH) {
                    switcher.selected=hit.index; switch_finish(true);
                } else if (switcher.open) { /* Modal switcher owns this click. */ }
                else if (hit.kind==DESKTOP_HIT_OVERVIEW) {
                    set_overview(false); focus_window(hit.index);
                } else if (hit.kind==DESKTOP_HIT_OVERVIEW_PAGE) {
                    i32 page=(i32)desktop_overview_page_size(sw); overview_move(hit.index?page:-page);
                }
                else if (hit.kind==DESKTOP_HIT_OVERVIEW_BUTTON) set_overview(!overview);
                else if (hit.kind==DESKTOP_HIT_VOLUME) {
                    set_menu(false); volume_open=!v.volume_open;
                }
                else if (hit.kind==DESKTOP_HIT_VOLUME_SLIDER && audio_ready) {
                    r=nv_audio_set_volume(hit.index);
                    if (r<0) note_error("Volume",r);
                    else volume_percent=hit.index;
                    drag_kind=DESKTOP_HIT_VOLUME_SLIDER;
                }
                else if (hit.kind==DESKTOP_HIT_START) {
                    set_menu(!menu);
                }
                else if (hit.kind==DESKTOP_HIT_MENU) {
                    r=start_app(hit.index);
                    if (r<0) note_error("Start",r);
                } else if (hit.kind==DESKTOP_HIT_LOCK) {
                    r=session_lock();if (r<0) note_error("Lock",r);
                } else if (hit.kind==DESKTOP_HIT_LOGOUT) {
                    r=session_request_signout();
                } else if (hit.kind==DESKTOP_HIT_SEARCH) { /* Search retains keyboard focus. */ }
                else if (volume_open) volume_open=false;
                else if (menu) {
                    set_menu(false);
                    if (hit.kind==DESKTOP_HIT_TASK) {
                        if (hit.index==DESKTOP_TERMINAL && !windows[hit.index].client)
                            r=launch("/apps/terminal","--terminal");
                        else focus_window(hit.index);
                    }
                }
                else if (hit.kind==DESKTOP_HIT_TASK) {
                    if (!was_overview && windows[hit.index].open &&
                        !windows[hit.index].minimized && active==hit.index) {
                        windows[hit.index].minimized=true; focus_top();
                    } else if (hit.index==DESKTOP_TERMINAL && !windows[hit.index].client) {
                        r=launch("/apps/terminal","--terminal"); if (r<0) note_error("Terminal",r);
                    } else focus_window(hit.index);
                } else if (hit.kind==DESKTOP_HIT_MEDIA) {
                    r=launch("/apps/media","");
                    if (r<0) note_error("Media",r);
                } else if (hit.kind==DESKTOP_HIT_SHORTCUT) {
                    u64 tick = clock_ticks();
                    shortcut_selected=hit.index;
                    if (hit.index==last_shortcut && tick-last_shortcut_tick<45) {
                        if (hit.index==3) {
                            r=launch("/apps/media","");
                            if (r<0) note_error("Media",r);
                        } else if (hit.index==DESKTOP_TERMINAL && !windows[hit.index].client) {
                        r=launch("/apps/terminal","--terminal"); if (r<0) note_error("Terminal",r);
                        } else focus_window(hit.index);
                    }
                    last_shortcut=hit.index; last_shortcut_tick=tick;
                } else if (hit.kind==DESKTOP_HIT_MINIMIZE) {
                    windows[hit.window].minimized=true; focus_top();
                } else if (hit.kind==DESKTOP_HIT_MAXIMIZE) {
                    focus_window(hit.window); maximize_window(hit.window);
                } else if (hit.kind==DESKTOP_HIT_CLOSE) close_window(hit.window);
                else if (hit.kind==DESKTOP_HIT_TITLE || hit.kind==DESKTOP_HIT_RESIZE) {
                    focus_window(hit.window);
                    u64 tick = clock_ticks();
                    if (hit.kind==DESKTOP_HIT_TITLE && last_title==hit.window &&
                        tick-last_title_tick<40) maximize_window(hit.window);
                    else {
                        drag_kind=hit.kind; drag_window=hit.window; drag_edges=hit.index;
                        drag_offset_x=x-(u32)windows[hit.window].x;
                        drag_offset_y=y-(u32)windows[hit.window].y;
                        struct desktop_window *w=&windows[hit.window];
                        drag_restore=(w->maximized || w->tiled)?
                            (struct desktop_rect){(u32)w->saved_x,(u32)w->saved_y,w->saved_w,w->saved_h}:
                            (struct desktop_rect){(u32)w->x,(u32)w->y,w->w,w->h};
                    }
                    last_title=hit.window; last_title_tick=tick;
                } else if (hit.kind==DESKTOP_HIT_PLACE) {
                    focus_window(DESKTOP_FILES);
                    r=go_place(hit.index);
                    if (r<0) note_error("Open location",r);
                } else if (hit.kind==DESKTOP_HIT_FILE_NEW_FOLDER)
                    file_open_dialog(DESKTOP_FILE_FOLDER);
                else if (hit.kind==DESKTOP_HIT_FILE_NEW_TEXT) {
                    r=editor_request(1,NULL);
                    if (r<0) note_error("New document",r);
                } else if (hit.kind==DESKTOP_HIT_FILE_DIALOG) {
                    focus_window(DESKTOP_FILES);
                    if (hit.index==1) { r=file_submit(); if (r<0) note_error("File operation",r); }
                    else if (hit.index==2) file_mode=DESKTOP_FILE_NORMAL;
                } else if (hit.kind==DESKTOP_HIT_FILE) {
                    focus_window(DESKTOP_FILES);
                    u64 tick = clock_ticks();
                    bool open=last_file==hit.index && tick-last_file_tick<=40;
                    selected=hit.index; last_file=hit.index; last_file_tick=tick;
                    scroll_to_selection();
                    if (open) { r=open_selected(); last_file=0xffffffffu;
                        if (r<0) note_error("Open file",r); }
                } else if (hit.kind==DESKTOP_HIT_EDITOR_SAVE) {
                    focus_window(DESKTOP_EDITOR); r=editor_save();
                    if (r<0) note_error("Save document",r);
                } else if (hit.kind==DESKTOP_HIT_EDITOR_NEW) {
                    r=editor_request(1,NULL);
                    if (r<0) note_error("New document",r);
                } else if (hit.kind==DESKTOP_HIT_EDITOR_DIALOG) {
                    focus_window(DESKTOP_EDITOR);
                    if (hit.index==1) r=editor_mode==DESKTOP_EDIT_PATH ?
                        editor_save_target(editor_input):editor_save();
                    else if (hit.index==2 && editor_mode==DESKTOP_EDIT_CLOSE)
                        r=editor_load_next();
                    else if (hit.index==3) {
                        editor_mode=DESKTOP_EDIT_NORMAL; editor_next_action=0;
                    }
                    if (r<0) note_error("Document",r);
                } else if (hit.kind==DESKTOP_HIT_EDITOR_TEXT) {
                    focus_window(DESKTOP_EDITOR);
                    struct desktop_window *w=&windows[DESKTOP_EDITOR];
                    editor_cursor=editor_cursor_at(x-(u32)w->x,y-(u32)w->y);
                } else if (hit.kind==DESKTOP_HIT_CLIENT) {
                    focus_window(hit.window); captured=hit.window;
                    send_client(hit.window,NV_WINDOW_EVENT_POINTER,0,&event);
                } else if (hit.kind==DESKTOP_HIT_ACCOUNT) {
                    focus_window(DESKTOP_ACCOUNTS);account_activate(&account,hit.index);
                } else if (hit.kind==DESKTOP_HIT_SETTINGS) {
                    focus_window(DESKTOP_SETTINGS);r=settings_activate(hit.index);
                    if (r<0) { note_error("Settings",r);r=0; }
                } else if (hit.kind==DESKTOP_HIT_TERMINAL) focus_window(DESKTOP_TERMINAL);
                else if (hit.kind==DESKTOP_HIT_NONE) shortcut_selected=0xffffffffu;
            }
            if (!(event.buttons&NV_POINTER_LEFT)) {
                if (drag_kind==DESKTOP_HIT_TITLE && snap_preview &&
                    windows[drag_window].open) {
                    struct desktop_window *w=&windows[drag_window];
                    desktop_place(w,sw,sh-DESKTOP_BAR_HEIGHT,snap_preview);
                    w->saved_x=(i32)drag_restore.x; w->saved_y=(i32)drag_restore.y;
                    w->saved_w=drag_restore.w; w->saved_h=drag_restore.h;
                    dirty=true;
                }
                if (snap_preview) dirty=true;
                drag_kind=snap_preview=0;
            }
            sync_clients();
            bool changed_buttons=(pointer_buttons^event.buttons)&NV_POINTER_LEFT;
            pointer_buttons=event.buttons&(NV_POINTER_LEFT|NV_POINTER_RIGHT|NV_POINTER_MIDDLE);
            if (changed_buttons || dirty || drag_kind==DESKTOP_HIT_VOLUME_SLIDER) dirty=true;
            else if (!had_pointer || previous_x!=pointer_x || previous_y!=pointer_y) {
                if (had_pointer) {
                    r=draw_region(tile,rows,previous_x,previous_y,8*s,10*s);
                    if (r<0) break;
                }
                r=draw_region(tile,rows,pointer_x,pointer_y,8*s,10*s);
                if (r<0) break;
            }
        }
        if (quit_requested) {
            bool open=false;
            for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) if (windows[i].client) {
                open=true;
                if (!leaving) close_window(i);
            }
            leaving=true;
            if (!open) {
                if (signout_requested) { r=session_signout();if (r<0) break; }
                else if (pending_power) {
                    r=control(NV_CTL_SYNC,0);
                    if (!r) r=control(pending_power,0);
                    if (r<0) { note_error("Power",r);r=0; }
                    pending_power=0;quit_requested=leaving=false;
                } else quit_requested=leaving=false;
                dirty=true;
            } else note(signout_requested?"Save or close app windows to sign out. Start cancels sign-out.":
                "Save or close app windows to continue. Start cancels the power action.");
        }
keyboard_input:;
        int event_key=key_event();
        if (event_key==-NV_EAGAIN) { nap(10); continue; }
        if (event_key<0) { r=event_key; break; }
        u32 key=(u32)event_key&4095u, flags=(u32)event_key&~4095u;
        if (account.gate) {
            int action=account_client_key(&account,key,flags);
            if (action==ACCOUNT_OPEN_DESKTOP) { r=session_enter();if (r<0) break; }
            dirty=true;continue;
        }
        if ((flags&NV_KEY_META) && (key=='l' || key=='L')) {
            super_armed=false;r=session_lock();if (r<0) note_error("Lock",r);dirty=true;continue;
        }
        if (key==NV_KEY_MODIFIERS) {
            bool next_meta=(flags&NV_KEY_META)!=0;
            if (next_meta && !meta_down) super_armed=!(flags&(NV_KEY_ALT|NV_KEY_CTRL|NV_KEY_SHIFT));
            if (flags&(NV_KEY_ALT|NV_KEY_CTRL|NV_KEY_SHIFT)) super_armed=false;
            if (!next_meta && meta_down && super_armed) { set_menu(!menu); dirty=true; }
            if (!next_meta) super_armed=false;
            meta_down=next_meta;
            if (switcher.open && !(flags&NV_KEY_ALT)) { switch_finish(true); dirty=true; }
            continue;
        }
        if (flags&NV_KEY_META) {
            super_armed=false;
            if (key=='\t') set_overview(!overview);
            else if (active<DESKTOP_WINDOW_COUNT &&
                     (key==NV_KEY_LEFT || key==NV_KEY_RIGHT || key==NV_KEY_UP || key==NV_KEY_DOWN)) {
                set_menu(false);
                if (key==NV_KEY_DOWN && !windows[active].maximized && !windows[active].tiled) {
                    windows[active].minimized=true; focus_top();
                } else {
                    desktop_place(&windows[active],sw,sh-DESKTOP_BAR_HEIGHT,key==NV_KEY_LEFT?DESKTOP_TILE_LEFT:
                        key==NV_KEY_RIGHT?DESKTOP_TILE_RIGHT:key==NV_KEY_UP?DESKTOP_TILE_MAX:DESKTOP_FLOATING);
                    sync_clients();
                }
            }
            dirty=true; continue;
        }
        if (key==NV_KEY_F10 && !(flags&(NV_KEY_ALT|NV_KEY_CTRL))) {
            set_menu(!menu); dirty=true; continue;
        }
        if (key==NV_KEY_F12 && !(flags&(NV_KEY_ALT|NV_KEY_CTRL))) {
            set_overview(!overview); dirty=true; continue;
        }
        if ((flags&NV_KEY_ALT) && key=='\t') {
            if (!switcher.open) {
                shell_grab(); menu=overview=volume_open=false;
                desktop_switch_begin(&switcher,windows,order,active,(flags&NV_KEY_SHIFT)?-1:1);
            } else desktop_switch_step(&switcher,(flags&NV_KEY_SHIFT)?-1:1);
            sync_clients(); dirty=true; continue;
        }
        if (switcher.open) {
            if (key==27) switch_finish(false);
            else if (key=='\n') switch_finish(true);
            else if (key==NV_KEY_LEFT || key==NV_KEY_UP) desktop_switch_step(&switcher,-1);
            else if (key==NV_KEY_RIGHT || key==NV_KEY_DOWN) desktop_switch_step(&switcher,1);
            dirty=true; continue;
        }
        if (overview) {
            if (key==27) set_overview(false);
            else if (key=='\n' && overview_selected<DESKTOP_WINDOW_COUNT) {
                u32 id=overview_selected; set_overview(false); focus_window(id);
            } else if (key==NV_KEY_LEFT) overview_move(-1);
            else if (key==NV_KEY_RIGHT) overview_move(1);
            else if (key==NV_KEY_UP) overview_move(-(i32)desktop_overview_columns(sw));
            else if (key==NV_KEY_DOWN) overview_move((i32)desktop_overview_columns(sw));
            else if (key==NV_KEY_PGUP) overview_move(-(i32)desktop_overview_page_size(sw));
            else if (key==NV_KEY_PGDN) overview_move((i32)desktop_overview_page_size(sw));
            else if ((flags&NV_KEY_ALT) && key==NV_KEY_F4 && overview_selected<DESKTOP_WINDOW_COUNT)
                close_window(overview_selected);
            else if (!(flags&(NV_KEY_CTRL|NV_KEY_ALT)) && key>=32 && key<127) {
                set_menu(true); launcher.query[0]=(char)key; launcher.query[1]=0; desktop_search(&launcher);
            }
            dirty=true; continue;
        }
        if (menu) {
            if ((flags&NV_KEY_CTRL) && (key=='l' || key=='L')) {
                r=session_lock();
            } else if ((flags&NV_KEY_CTRL) && (key=='q' || key=='Q')) {
                r=session_request_signout();
            } else if (key==27) {
                if (*launcher.query) { launcher.query[0]=0; desktop_search(&launcher); }
                else set_menu(false);
            } else if (key==NV_KEY_UP && launcher.selected) --launcher.selected;
            else if (key==NV_KEY_DOWN && launcher.selected+1<launcher.count) ++launcher.selected;
            else if (key=='\n' && launcher.count) {
                r=start_app(launcher.ids[launcher.selected]); if (r<0) note_error("Start",r);
            } else if (key=='\b' && *launcher.query) {
                launcher.query[launcher.select_all?0:strlen(launcher.query)-1]=0; desktop_search(&launcher);
            } else if ((flags&NV_KEY_CTRL) && (key=='a' || key=='A')) {
                launcher.select_all=*launcher.query!=0;
            } else if (key==NV_KEY_DELETE && launcher.select_all) {
                launcher.query[0]=0; desktop_search(&launcher);
            } else if (!(flags&(NV_KEY_CTRL|NV_KEY_ALT)) && key>=32 && key<127) {
                u32 n=launcher.select_all?0:strlen(launcher.query);
                if (n+1<sizeof(launcher.query)) {
                    launcher.query[n]=(char)key; launcher.query[n+1]=0; desktop_search(&launcher);
                }
            }
            dirty=true; continue;
        }
        if (active==DESKTOP_EDITOR && editor_mode!=DESKTOP_EDIT_NORMAL) {
            r=editor_key(key,flags);
            if (r<0) { note_error("Document",r); r=0; }
            dirty=true; continue;
        }
        if (active==DESKTOP_FILES && file_mode!=DESKTOP_FILE_NORMAL) {
            r=file_key(key);
            if (r<0) { note_error("File operation",r); r=0; }
            dirty=true; continue;
        }
        if ((flags&NV_KEY_ALT) && key==NV_KEY_F4 && active<DESKTOP_WINDOW_COUNT)
            close_window(active);
        else if ((flags&NV_KEY_ALT) && key==NV_KEY_F9 && active<DESKTOP_WINDOW_COUNT) {
            windows[active].minimized=true; focus_top();
        } else if ((flags&NV_KEY_ALT) && key==NV_KEY_F10 && active<DESKTOP_WINDOW_COUNT) {
            maximize_window(active); sync_clients();
        } else if (active<DESKTOP_WINDOW_COUNT && windows[active].client) {
            r=send_client(active,NV_WINDOW_EVENT_KEY,(u32)event_key,NULL);
            if (r<0 && r!=-NV_EAGAIN) note_error("Window input",r);
        }
        else if (active==DESKTOP_ACCOUNTS) {
            if (key==27 && account.page==ACCOUNT_LIST) close_window(DESKTOP_ACCOUNTS);
            else account_client_key(&account,key,flags);
        } else if (active==DESKTOP_SETTINGS) {
            if (key==27) {
                if (settings_confirm) settings_confirm=0;else close_window(DESKTOP_SETTINGS);
            } else if (key=='\t') {
                u32 choices=settings_confirm?2:settings_tab==0?3:settings_tab==1?5:3;
                settings_focus=(settings_focus+((flags&NV_KEY_SHIFT)?choices-1:1))%choices;
            } else if (key=='\n') {
                u32 hit=settings_confirm?12+settings_focus:settings_tab==0?3+settings_focus:
                    settings_tab==1?(settings_focus<3?3+settings_focus:6+settings_focus-3):6+settings_focus;
                r=settings_activate(hit);if (r<0) note_error("Settings",r);
            } else if (key==NV_KEY_LEFT && settings_tab) { --settings_tab;settings_focus=0; }
            else if (key==NV_KEY_RIGHT && settings_tab<2) { ++settings_tab;settings_focus=0; }
            else if (key>='1' && key<='3' && settings_tab<2) {
                r=settings_activate(3+key-'1');if (r<0) note_error("Settings",r);
            }
        } else if (active==DESKTOP_EDITOR) {
            if (key==27) close_window(DESKTOP_EDITOR);
            else { r=editor_key(key,flags); if (r<0) note_error("Document",r); }
        } else if (active==DESKTOP_TERMINAL) {
            if (key==27) close_window(DESKTOP_TERMINAL);
            else { r=terminal_key(key,flags); if (r<0) note_error("Terminal",r); }
        } else if (active==DESKTOP_FILES) {
            if (key==27) close_window(DESKTOP_FILES);
            else if (key==NV_KEY_UP && selected) --selected;
            else if (key==NV_KEY_DOWN && selected+1<count) ++selected;
            else if (key==NV_KEY_PGUP) selected=selected>8?selected-8:0;
            else if (key==NV_KEY_PGDN && count) selected=MIN(count-1,selected+8);
            else if (key=='\b') {
                r=chdir_path("..");
                if (r>=0) { selected=scroll=0; r=refresh(); }
                if (r<0) note_error("Parent folder",r);
            } else if (key=='\n') { r=open_selected(); if (r<0) note_error("Open file",r); }
            else if (key==NV_KEY_F2) file_open_dialog(DESKTOP_FILE_RENAME);
            else if (key==NV_KEY_DELETE) file_open_dialog(DESKTOP_FILE_DELETE);
            else if ((flags&NV_KEY_CTRL) && (key=='n' || key=='N'))
                file_open_dialog(DESKTOP_FILE_FOLDER);
            else if ((flags&NV_KEY_CTRL) && (key=='h' || key=='H')) {
                show_hidden=!show_hidden;selected=scroll=0;r=refresh();
                if (r<0) note_error("Hidden files",r);else note(show_hidden?"Hidden files shown.":"Hidden files hidden.");
            }
            else if ((flags&NV_KEY_CTRL) && (key=='t' || key=='T')) {
                r=editor_request(1,NULL); if (r<0) note_error("New document",r);
            }
            else if (key==NV_KEY_F3) { r=launch("/apps/media",""); if (r<0) note_error("Media",r); }
            else if (key==NV_KEY_F5) { r=refresh(); if (r<0) note_error("Refresh",r); }
            else if (key==NV_KEY_F6) { r=control(NV_CTL_SYNC,0);
                if (r<0) note_error("Save drives",r); else note("Mounted drives saved."); }
            else if (key>='1' && key<='7') { r=go_place(key-'1'); if (r<0) note_error("Location",r); }
            scroll_to_selection();
        } else if (active==DESKTOP_WINDOW_COUNT &&
                   (key==NV_KEY_UP || key==NV_KEY_DOWN)) {
            if (shortcut_selected>=4) shortcut_selected=0;
            else if (key==NV_KEY_UP && shortcut_selected) --shortcut_selected;
            else if (key==NV_KEY_DOWN && shortcut_selected<3) ++shortcut_selected;
        } else if (active==DESKTOP_WINDOW_COUNT && key=='\n') {
            if (shortcut_selected==3) { r=launch("/apps/media","");
                if (r<0) note_error("Media",r); }
            else if (shortcut_selected==DESKTOP_TERMINAL && !windows[DESKTOP_TERMINAL].client) {
                r=launch("/apps/terminal","--terminal"); if (r<0) note_error("Terminal",r);
            } else focus_window(shortcut_selected<3?shortcut_selected:DESKTOP_FILES);
        } else if (key==27) shortcut_selected=0xffffffffu;
        if (r<0) r=0;
        dirty=true;
    }
    reap_children(true);
    account_call(NV_ACCOUNT_CANCEL,NULL);account_wipe(&account,sizeof(account));
    nv_window_call(NV_WINDOW_SERVER_RELEASE,NULL);
    nv_display_release();
    if (r<0) { report_error("Desktop: rendering",r); return 1; }
    return 0;
}
