#include "runtime.h"
#include "desktop_ui.h"

#define DESKTOP_ITEMS 512u
#define EDITOR_CAP (128u * 1024u)
static struct nv_dirent64 entries[DESKTOP_ITEMS];
static struct nv_display_info mode;
static struct desktop_window windows[DESKTOP_WINDOW_COUNT];
static u8 order[DESKTOP_WINDOW_COUNT] = {DESKTOP_EDITOR, DESKTOP_TERMINAL, DESKTOP_FILES};
static u32 active = DESKTOP_FILES;
static char directory[NV_PATH_MAX], message[160], drive[32];
static u32 count, selected, scroll, volumes;
static u32 file_mode;
static char file_input[32], file_target[NV_PATH_MAX], file_base[NV_PATH_MAX];
static u32 pointer_x, pointer_y, pointer_buttons, menu_selected;
static bool pointer_visible, audio_ready, menu, quit_requested;
static u32 drag_window, drag_kind;
static char editor_text[EDITOR_CAP+1], editor_scratch[EDITOR_CAP+1];
static char editor_path[NV_PATH_MAX], editor_input[NV_PATH_MAX], editor_next_path[NV_PATH_MAX];
static u32 editor_length, editor_cursor, editor_scroll, editor_mode, editor_next_action;
static bool editor_dirty;
static char terminal_lines[64][128], terminal_input[256], terminal_history[256];
static u32 terminal_first, terminal_count, terminal_column, terminal_length;

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
    for (u32 i=0;i<DESKTOP_ITEMS;++i) {
        r=list_dir64(".",i,&entries[i]);
        if (r<0) return r;
        if (!r) break;
        ++count;
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
    v.path=directory; v.message=message; v.drive=drive; v.entries=entries;
    v.count=count; v.selected=selected; v.scroll=scroll; v.volumes=volumes;
    v.file_mode=file_mode; v.file_input=file_input; v.file_target=file_input;
    v.pointer=pointer_visible; v.pointer_x=pointer_x; v.pointer_y=pointer_y;
    v.audio_ready=audio_ready; v.menu=menu; v.menu_selected=menu_selected;
    v.windows=windows; v.order=order; v.active=active;
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
static int draw(u32 *tile, u32 rows) {
    struct desktop_view v=view();
    for (u32 y=0;y<mode.height;y+=rows) {
        struct nv_canvas canvas={tile,mode.width,y,MIN(rows,mode.height-y),mode.format};
        desktop_render(&canvas,mode.height,&v);
        struct nv_display_present rect={.x=0,.y=y,.width=mode.width,
            .height=canvas.rows,.stride=mode.width*4,.pixels=(u32)(uptr)tile};
        int r=nv_display_present(&rect);
        if (r<0) return r;
    }
    return 0;
}
static void focus_window(u32 id) {
    if (id>=DESKTOP_WINDOW_COUNT) return;
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) if (order[i]==id) {
        for (;i+1<DESKTOP_WINDOW_COUNT;++i) order[i]=order[i+1];
        order[DESKTOP_WINDOW_COUNT-1]=(u8)id;
        break;
    }
    active=id;
    windows[id].open=true;
    windows[id].minimized=false;
}
static void focus_top(void) {
    for (i32 i=DESKTOP_WINDOW_COUNT-1;i>=0;--i) {
        u32 id=order[i];
        if (windows[id].open && !windows[id].minimized) { active=id; return; }
    }
    active=DESKTOP_WINDOW_COUNT;
}
static void maximize_window(u32 id) {
    struct desktop_window *w=&windows[id];
    if (w->maximized) {
        w->x=w->saved_x; w->y=w->saved_y;
        w->w=w->saved_w; w->h=w->saved_h;
        w->maximized=false;
    } else {
        w->saved_x=w->x; w->saved_y=w->y;
        w->saved_w=w->w; w->saved_h=w->h;
        u32 s=desktop_scale(mode.width,mode.height);
        w->x=0; w->y=0; w->w=mode.width/s; w->h=mode.height/s-30;
        w->maximized=true;
    }
}
static int launch(const char *app, const char *arg) {
    int r=nv_display_release();
    if (r<0) return r;
    int pid=spawn(app,arg);
    if (pid>0) r=wait_task(pid);
    else r=pid;
    int acquired=nv_display_acquire();
    if (acquired<0) return acquired;
    pointer_buttons=drag_kind=0;
    if (r>0) return -NV_EIO;
    return r<0?r:refresh();
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
    if (key=='\n') return file_submit();
    if (file_mode==DESKTOP_FILE_DELETE) return 0;
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
    number(temp+end,clock_ticks(),16);
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
        strlcpy(editor_input,"/home/Untitled.txt",sizeof(editor_input));
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
    if (id==DESKTOP_EDITOR && editor_dirty) {
        editor_next_action=2;
        editor_mode=DESKTOP_EDIT_CLOSE;
        focus_window(id);
        return;
    }
    windows[id].open=false;
    windows[id].minimized=false;
    if (id==DESKTOP_FILES) file_mode=DESKTOP_FILE_NORMAL;
    focus_top();
}
static int go_place(u32 index) {
    static const char *const places[]={"/home","/drives/D","/drives/E","/drives/F",
                                       "/","/apps","/tmp"};
    if (index>=ARRAY_LEN(places) || (index<NV_VOLUME_MAX && index>=volumes)) return 0;
    int r=chdir_path(places[index]);
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
    menu=false;
    if (index==0) { focus_window(DESKTOP_FILES); return 0; }
    if (index==1) { focus_window(DESKTOP_EDITOR); return 0; }
    if (index==2) { focus_window(DESKTOP_TERMINAL); return 0; }
    if (index==3) return launch("/apps/media","");
    if (index==4) {
        if (editor_dirty) return editor_request(4,NULL);
        quit_requested=true;
    }
    return 0;
}
static void editor_ensure_visible(void) {
    const struct desktop_window *w=&windows[DESKTOP_EDITOR];
    u32 s=desktop_scale(mode.width,mode.height);
    u32 cols=MAX(1u,desktop_chars(w->w*s-36*s,s));
    u32 row=0,col=0;
    for (u32 i=0;i<editor_cursor;++i) {
        if (editor_text[i]=='\n') { ++row; col=0; }
        else if (++col>=cols) { ++row; col=0; }
    }
    u32 visible=MAX(1u,(w->h-94)/13);
    if (row<editor_scroll) editor_scroll=row;
    if (row>=editor_scroll+visible) editor_scroll=row-visible+1;
}
static u32 editor_cursor_at(u32 rx, u32 ry) {
    const struct desktop_window *w=&windows[DESKTOP_EDITOR];
    u32 s=desktop_scale(mode.width,mode.height);
    u32 cols=MAX(1u,desktop_chars(w->w*s-36*s,s));
    u32 target_row=editor_scroll+(ry>73?(ry-73)/13:0);
    u32 target_col=rx>19?MIN((rx-19)*s/(5*s+1),cols-1):0;
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
                strlcpy(editor_input,*editor_path?editor_path:"/home/Untitled.txt",sizeof(editor_input));
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
    if (!strcmp(cmd,"loom") && n==1) return launch("/apps/loom","--terminal");
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
    u32 s=desktop_scale(mode.width,mode.height), sw=mode.width/s, sh=mode.height/s;
    windows[DESKTOP_FILES]=(struct desktop_window){.x=125,.y=42,
        .w=MIN(520u,sw-145),.h=MIN(350u,sh-83),.open=true};
    windows[DESKTOP_EDITOR]=(struct desktop_window){.x=140,.y=62,
        .w=MIN(485u,sw-155),.h=MIN(310u,sh-100)};
    windows[DESKTOP_TERMINAL]=(struct desktop_window){.x=150,.y=86,
        .w=MIN(475u,sw-160),.h=MIN(250u,sh-126)};
    terminal_new_line();
    terminal_println("Nuvora desktop terminal. Type help for commands.");
    r=refresh();
    if (r<0) { report_error("Desktop: files",r); return 1; }
    r=nv_display_acquire();
    if (r<0) { report_error("Desktop: display",r); return 1; }
    pointer_x=mode.width/2; pointer_y=mode.height/2;
    struct nv_audio_info audio;
    audio_ready=nv_audio_info(&audio)==0 && audio.api_version==NV_AUDIO_API_VERSION &&
                audio.outputs>0;
    bool dirty=true;
    u32 last_file=0xffffffffu,last_file_tick=0;
    u32 last_shortcut=0xffffffffu,last_shortcut_tick=0;
    u32 last_title=0xffffffffu,last_title_tick=0;
    for (;;) {
        if (dirty) { r=draw(tile,rows); if (r<0) break; dirty=false; }
        struct nv_pointer_event event;
        int mouse=nv_pointer_poll(&event);
        if (mouse<0) { r=mouse; break; }
        if (mouse==1) {
            pointer_visible=true;
            bool absolute=(event.buttons&NV_POINTER_ABSOLUTE)!=0;
            u32 oldx=pointer_x/s, oldy=pointer_y/s;
            pointer_x=desktop_pointer_axis(pointer_x,event.dx,mode.width,absolute);
            pointer_y=desktop_pointer_axis(pointer_y,event.dy,mode.height,absolute);
            u32 x=pointer_x/s,y=pointer_y/s;
            if ((event.buttons&NV_POINTER_LEFT) && (pointer_buttons&NV_POINTER_LEFT) && drag_kind) {
                struct desktop_window *w=&windows[drag_window];
                if (drag_kind==DESKTOP_HIT_TITLE && !w->maximized) {
                    w->x=MAX(0,MIN((i32)sw-(i32)w->w,w->x+(i32)x-(i32)oldx));
                    w->y=MAX(0,MIN((i32)sh-58,w->y+(i32)y-(i32)oldy));
                } else if (drag_kind==DESKTOP_HIT_RESIZE) {
                    w->w=MAX(300u,MIN(sw-(u32)w->x,x>(u32)w->x?x-(u32)w->x:300u));
                    w->h=MAX(160u,MIN(sh-30-(u32)w->y,y>(u32)w->y?y-(u32)w->y:160u));
                    if (drag_window==DESKTOP_EDITOR) editor_ensure_visible();
                    if (drag_window==DESKTOP_FILES) scroll_to_selection();
                }
            }
            if ((event.buttons&NV_POINTER_LEFT) && !(pointer_buttons&NV_POINTER_LEFT)) {
                struct desktop_view v=view();
                struct desktop_hit hit=desktop_hit(mode.width,mode.height,&v,pointer_x,pointer_y);
                if (hit.kind==DESKTOP_HIT_START) menu=!menu;
                else if (hit.kind==DESKTOP_HIT_MENU) {
                    r=start_app(hit.index);
                    if (r<0) note_error("Start",r);
                } else if (menu) {
                    menu=false;
                    if (hit.kind==DESKTOP_HIT_TASK) focus_window(hit.index);
                }
                else if (hit.kind==DESKTOP_HIT_TASK) {
                    if (windows[hit.index].open && !windows[hit.index].minimized && active==hit.index) {
                        windows[hit.index].minimized=true; focus_top();
                    } else focus_window(hit.index);
                } else if (hit.kind==DESKTOP_HIT_MEDIA) {
                    r=launch("/apps/media","");
                    if (r<0) note_error("Media",r);
                } else if (hit.kind==DESKTOP_HIT_SHORTCUT) {
                    u32 tick=clock_ticks();
                    if (hit.index==last_shortcut && tick-last_shortcut_tick<45)
                        focus_window(hit.index);
                    last_shortcut=hit.index; last_shortcut_tick=tick;
                } else if (hit.kind==DESKTOP_HIT_MINIMIZE) {
                    windows[hit.window].minimized=true; focus_top();
                } else if (hit.kind==DESKTOP_HIT_MAXIMIZE) {
                    focus_window(hit.window); maximize_window(hit.window);
                } else if (hit.kind==DESKTOP_HIT_CLOSE) close_window(hit.window);
                else if (hit.kind==DESKTOP_HIT_TITLE || hit.kind==DESKTOP_HIT_RESIZE) {
                    focus_window(hit.window);
                    u32 tick=clock_ticks();
                    if (hit.kind==DESKTOP_HIT_TITLE && last_title==hit.window &&
                        tick-last_title_tick<40) maximize_window(hit.window);
                    else { drag_kind=hit.kind; drag_window=hit.window; }
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
                    u32 tick=clock_ticks();
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
                } else if (hit.kind==DESKTOP_HIT_TERMINAL) focus_window(DESKTOP_TERMINAL);
            }
            if (!(event.buttons&NV_POINTER_LEFT)) drag_kind=0;
            pointer_buttons=event.buttons&(NV_POINTER_LEFT|NV_POINTER_RIGHT|NV_POINTER_MIDDLE);
            dirty=true;
        }
        if (quit_requested) break;
        int event_key=key_event();
        if (event_key==-NV_EAGAIN) { nap(25); continue; }
        if (event_key<0) { r=event_key; break; }
        u32 key=(u32)event_key&4095u, flags=(u32)event_key&~4095u;
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
        if (menu) {
            if (key==27 || key==NV_KEY_F10) menu=false;
            else if (key==NV_KEY_UP && menu_selected) --menu_selected;
            else if (key==NV_KEY_DOWN && menu_selected<4) ++menu_selected;
            else if (key=='\n') { r=start_app(menu_selected); if (r<0) note_error("Start",r); }
            dirty=true; continue;
        }
        if (key==NV_KEY_F10) { menu=true; menu_selected=0; }
        else if ((flags&NV_KEY_ALT) && key=='\t') {
            for (u32 i=1;i<=DESKTOP_WINDOW_COUNT;++i) {
                u32 id=(active+i)%DESKTOP_WINDOW_COUNT;
                if (windows[id].open && !windows[id].minimized) { focus_window(id); break; }
            }
        } else if ((flags&NV_KEY_ALT) && key==NV_KEY_F4 && active<DESKTOP_WINDOW_COUNT)
            close_window(active);
        else if (active==DESKTOP_EDITOR) {
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
            else if ((flags&NV_KEY_CTRL) && (key=='t' || key=='T')) {
                r=editor_request(1,NULL); if (r<0) note_error("New document",r);
            }
            else if (key==NV_KEY_F3) { r=launch("/apps/media",""); if (r<0) note_error("Media",r); }
            else if (key==NV_KEY_F5) { r=refresh(); if (r<0) note_error("Refresh",r); }
            else if (key==NV_KEY_F6) { r=control(NV_CTL_SYNC,0);
                if (r<0) note_error("Save drives",r); else note("Mounted drives saved."); }
            else if (key>='1' && key<='7') { r=go_place(key-'1'); if (r<0) note_error("Location",r); }
            scroll_to_selection();
        } else if (key==27) {
            if (editor_dirty) r=editor_request(4,NULL);
            else quit_requested=true;
        }
        if (r<0) r=0;
        dirty=true;
    }
    nv_display_release();
    if (r<0) { report_error("Desktop: rendering",r); return 1; }
    return 0;
}
