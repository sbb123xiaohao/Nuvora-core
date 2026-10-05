#ifndef NV_FILES_UI_H
#define NV_FILES_UI_H
/* The file canvas uses the same geometry as hit testing and keyboard scrolling. */
static u32 files_line_icon(const struct nv_dirent64 *entry) {
    u32 kind=files_category(entry);
    if (!kind) return UI_ICON_FOLDER;
    if (kind==2) return UI_ICON_MUSIC;
    if (kind==3) return UI_ICON_VIDEO;
    const char *ext=entry->name;
    for (const char *p=entry->name;*p;++p) if (*p=='.') ext=p;
    if (!files_name_compare(ext,".png") || !files_name_compare(ext,".jpg") ||
        !files_name_compare(ext,".jpeg") || !files_name_compare(ext,".bmp") ||
        !files_name_compare(ext,".gif") || !files_name_compare(ext,".webp") ||
        !files_name_compare(ext,".svg")) return UI_ICON_IMAGE;
    if (!files_name_compare(ext,".elf")) return UI_ICON_APP;
    return UI_ICON_DOCUMENT;
}
static void files_badge(struct nv_canvas *c,struct desktop_clip clip,u32 x,u32 y,u32 s,
    const struct nv_dirent64 *entry) {
    ui_line_icon(c,clip,x,y,44*s,files_line_icon(entry),UI_BLUE);
}
static void files_path_title(char title[NV_PATH_MAX],const struct desktop_view *v) {
    const char *path=v->path?v->path:"/";
    const char *last=path;
    for (const char *p=path;*p;++p) if (*p=='/' && p[1]) last=p+1;
    if (v->account && !strcmp(last,v->account->info.name)) last="Home";
    if (!strcmp(path,"/") || !strcmp(path,"C:/")) last=!strcmp(path,"/")?"System":"Home";
    strlcpy(title,last,NV_PATH_MAX);
}
static void files_pill(struct nv_canvas *c,struct desktop_clip clip,u32 x,u32 y,
    u32 width,u32 s,const char *label,bool active) {
    if (!width) return;
    desktop_round(c,clip,x,y,width*s,30*s,7*s,active?UI_BLUE:0xe9edf3);
    desktop_round(c,clip,x+s,y+s,(width-2)*s,28*s,6*s,0xffffff);
    u32 length=strlen(label),text=desktop_text_width(label,length,s);
    desktop_text(c,clip,x+(width*s-MIN(text,(width-12)*s))/2,y+7*s,
        label,length,s,active?UI_BLUE:UI_TEXT);
}
static void desktop_files_render(struct nv_canvas *c,struct desktop_clip clip,u32 x,u32 y,u32 s,
    const struct desktop_view *v) {
    const struct desktop_window *w=&v->windows[DESKTOP_FILES];
    struct files_layout g=desktop_files_layout(w,v->file_view);
    u32 sidebar=g.left-20,main_right=w->w-g.inspector-28;
    desktop_box(c,clip,x+s,y+28*s,(w->w-2)*s,(w->h-29)*s,UI_BG);
    desktop_box(c,clip,x+s,y+28*s,(sidebar-1)*s,(w->h-29)*s,0xffffff);
    desktop_box(c,clip,x+sidebar*s,y+28*s,s,(w->h-29)*s,0xebedf2);
    struct desktop_clip side=clip;
    side.right=MIN(side.right,x+sidebar*s);
    struct desktop_rect control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_SEARCH,0);
    desktop_round(c,side,x+control.x*s,y+control.y*s,control.w*s,control.h*s,8*s,
        v->file_query_focus?UI_BLUE:0xf5f6f9);
    if (v->file_query_focus)
        desktop_round(c,side,x+(control.x+1)*s,y+(control.y+1)*s,
            (control.w-2)*s,(control.h-2)*s,7*s,0xffffff);
    ui_line_icon(c,side,x+(control.x+9)*s,y+(control.y+8)*s,14*s,UI_ICON_SEARCH,UI_MUTED);
    const char *query=v->file_query && *v->file_query?v->file_query:
        sidebar>=150?"Search files":"Search";
    desktop_label(c,side,x+(control.x+27)*s,y+(control.y+8)*s,query,(control.w-35)/6,s,
        v->file_query && *v->file_query?UI_TEXT:UI_MUTED);
    desktop_text(c,side,x+18*s,y+82*s,"Locations",9,s,UI_MUTED);
    u32 spaces=MAX(1u,v->volumes);
    for (u32 i=0;i<spaces+3 && i<7;++i) {
        struct desktop_rect place=desktop_file_place_rect(w,i);
        if (!place.w || !place.h) continue;
        char label[24];u32 icon;
        if (i<spaces) {
            strlcpy(label,i?(sidebar<150?"Vol. ":"Volume "):"Home",sizeof(label));
            if (i) number(label+strlen(label),i+1,10);
            icon=i?UI_ICON_STORAGE:UI_ICON_HOME;
        } else {
            const char *places[]={"System","Applications","Scratch"};
            strlcpy(label,places[i-spaces],sizeof(label));
            if (sidebar<150 && i==spaces+1) strlcpy(label,"Apps",sizeof(label));
            icon=i==spaces?UI_ICON_STORAGE:i==spaces+1?UI_ICON_APP:UI_ICON_FOLDER;
        }
        bool active=i==0 && v->path &&
            (!strncmp(v->path,"/home",5) || !strncmp(v->path,"C:",2));
        if (v->path && i && i<spaces) active=(v->path[0]=='C'+(char)i && v->path[1]==':') ||
            (!strncmp(v->path,"/drives/",8) && v->path[8]=='C'+(char)i);
        if (v->path && i>=spaces) active=i==spaces?!strcmp(v->path,"/"):
            i==spaces+1?!strncmp(v->path,"/apps",5):!strncmp(v->path,"/tmp",4);
        if (active) desktop_round(c,side,x+place.x*s,y+place.y*s,
            place.w*s,place.h*s,8*s,UI_BLUE);
        ui_line_icon(c,side,x+(place.x+10)*s,y+(place.y+7)*s,16*s,icon,
            active?0xffffff:UI_MUTED);
        u32 label_left=sidebar<150?31:35;
        desktop_label(c,side,x+(place.x+label_left)*s,y+(place.y+7)*s,label,
            (place.w-label_left-4)/6,s,
            active?0xffffff:UI_TEXT);
    }
    control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_PARENT,0);
    desktop_round(c,side,x+control.x*s,y+control.y*s,control.w*s,control.h*s,8*s,0xe9edf3);
    desktop_round(c,side,x+(control.x+1)*s,y+(control.y+1)*s,
        (control.w-2)*s,(control.h-2)*s,7*s,0xffffff);
    ui_line_icon(c,side,x+(control.x+10)*s,y+(control.y+7)*s,16*s,UI_ICON_BACK,UI_MUTED);
    desktop_label(c,side,x+(control.x+34)*s,y+(control.y+7)*s,
        sidebar<150?"Up":"Up a level",(control.w-40)/6,s,UI_TEXT);
    struct desktop_clip main=clip;
    main.left=MAX(main.left,x+g.left*s);
    main.right=MIN(main.right,x+main_right*s);
    desktop_bold_text(c,main,x+g.left*s,y+41*s,"Files",5,2*s,UI_TEXT);
    char title[NV_PATH_MAX];files_path_title(title,v);
    const char *path=v->path?v->path:"/";
    desktop_label(c,main,x+g.left*s,y+81*s,path,(main_right-g.left)/6,s,UI_MUTED);
    control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_NEW_FOLDER,0);
    if (control.w) {
        desktop_round(c,main,x+control.x*s,y+control.y*s,control.w*s,control.h*s,8*s,0xffffff);
        ui_line_icon(c,main,x+(control.x+10)*s,y+(control.y+8)*s,14*s,UI_ICON_PLUS,UI_BLUE);
        if (control.w>=70) desktop_text(c,main,x+(control.x+29)*s,y+(control.y+7)*s,"Folder",6,s,UI_TEXT);
    }
    control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_NEW_TEXT,0);
    if (control.w) {
        desktop_round(c,main,x+control.x*s,y+control.y*s,control.w*s,control.h*s,8*s,UI_BLUE);
        ui_line_icon(c,main,x+(control.x+10)*s,y+(control.y+8)*s,14*s,
            control.w>=70?UI_ICON_PLUS:UI_ICON_DOCUMENT,0xffffff);
        if (control.w>=70) desktop_text(c,main,x+(control.x+29)*s,y+(control.y+7)*s,"Note",4,s,0xffffff);
    }
    control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_VIEW,0);
    files_pill(c,main,x+control.x*s,y+control.y*s,control.w,s,"Cards",!v->file_view);
    control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_VIEW,1);
    files_pill(c,main,x+control.x*s,y+control.y*s,control.w,s,"List",v->file_view);
    const char *sorts[]={"Name","Type","Size"};
    char sorting[24]="Sort: ";strlcpy(sorting+6,sorts[MIN(v->file_sort,2u)],18);
    control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_SORT,0);
    files_pill(c,main,x+control.x*s,y+control.y*s,control.w,s,sorting,false);
    control=desktop_file_control_rect(w,DESKTOP_HIT_FILE_HIDDEN,0);
    files_pill(c,main,x+control.x*s,y+control.y*s,control.w,s,
        control.w<80?"Hidden":v->show_hidden?"Hidden on":"Hidden off",v->show_hidden);
    struct desktop_clip items=main;
    items.top=MAX(items.top,y+g.top*s);
    items.bottom=MIN(items.bottom,y+(w->h-34)*s);
    for (u32 i=0;i<g.rows*g.columns && v->scroll+i<v->count;++i) {
        u32 index=v->scroll+i;
        const struct nv_dirent64 *entry=&v->entries[index];
        struct desktop_rect r=desktop_file_rect(g,i);
        u32 left=x+r.x*s,top=y+r.y*s;
        bool selected=index==v->selected;
        desktop_round(c,items,left,top+2*s,r.w*s,r.h*s,12*s,0xe8ecf3);
        desktop_round(c,items,left,top,r.w*s,r.h*s,12*s,selected?UI_BLUE:0xffffff);
        desktop_round(c,items,left+s,top+s,(r.w-2)*s,(r.h-2)*s,11*s,0xffffff);
        if (v->file_view) {
            ui_line_icon(c,items,left+12*s,top+9*s,22*s,files_line_icon(entry),UI_BLUE);
            u32 detail_width=r.w>380?150:r.w>250?92:0;
            desktop_label(c,items,left+45*s,top+10*s,entry->name,
                r.w>58+detail_width?(r.w-58-detail_width)/6:0,s,UI_TEXT);
            if (detail_width) {
                char detail[32];
                if (entry->kind==NV_DIR) strlcpy(detail,"Folder",sizeof(detail));
                else desktop_size(detail,entry->size);
                desktop_label(c,items,left+(r.w-detail_width)*s,top+10*s,detail,
                    (detail_width-12)/6,s,UI_MUTED);
            }
        } else {
            files_badge(c,items,left+14*s,top+15*s,s,entry);
            if (selected) {
                desktop_round(c,items,left+(r.w-31)*s,top+14*s,19*s,19*s,6*s,UI_BLUE);
                ui_line_icon(c,items,left+(r.w-28)*s,top+17*s,13*s,UI_ICON_CHECK,0xffffff);
            }
            desktop_label(c,items,left+14*s,top+73*s,entry->name,(r.w-28)/6,s,UI_TEXT);
            char detail[48];
            if (entry->kind==NV_DIR) strlcpy(detail,"Folder",sizeof(detail));
            else {
                desktop_size(detail,entry->size);u32 at=strlen(detail);
                strlcpy(detail+at,"  /  ",sizeof(detail)-at);at=strlen(detail);
                strlcpy(detail+at,desktop_kind(entry),sizeof(detail)-at);
            }
            desktop_label(c,items,left+14*s,top+94*s,detail,(r.w-28)/6,s,UI_MUTED);
        }
    }
    if (!v->count) {
        desktop_round(c,items,x+g.left*s,y+g.top*s,g.width*s,130*s,12*s,0xffffff);
        ui_line_icon(c,items,x+(g.left+20)*s,y+(g.top+20)*s,32*s,UI_ICON_FOLDER,UI_BLUE);
        desktop_bold_text(c,items,x+(g.left+20)*s,y+(g.top+66)*s,
            v->file_query && *v->file_query?"No matching files":"This folder is empty",
            v->file_query && *v->file_query?17:20,s,UI_TEXT);
        desktop_label(c,items,x+(g.left+20)*s,y+(g.top+91)*s,
            v->file_query && *v->file_query?"Clear the search to see all files.":"Create a folder or a note to get started.",
            (g.width-40)/6,s,UI_MUTED);
    }
    if (g.inspector) {
        u32 dx=x+(w->w-g.inspector-14)*s,panel_width=g.inspector;
        struct desktop_clip detail=clip;
        detail.left=MAX(detail.left,dx);
        detail.right=MIN(detail.right,dx+panel_width*s);
        detail.bottom=MIN(detail.bottom,y+(w->h-20)*s);
        desktop_round(c,detail,dx,y+42*s,panel_width*s,(w->h-62)*s,14*s,0xffffff);
        desktop_bold_text(c,detail,dx+18*s,y+60*s,"Details",7,s,UI_TEXT);
        desktop_box(c,detail,dx+18*s,y+87*s,(panel_width-36)*s,s,0xf0f2f6);
        if (v->count && v->selected<v->count) {
            const struct nv_dirent64 *entry=&v->entries[v->selected];
            desktop_round(c,detail,dx+18*s,y+106*s,(panel_width-36)*s,106*s,11*s,0xf7f9fc);
            files_badge(c,detail,dx+(panel_width/2-22)*s,y+137*s,s,entry);
            desktop_text(c,detail,dx+18*s,y+233*s,"Name",4,s,UI_MUTED);
            desktop_label(c,detail,dx+18*s,y+256*s,entry->name,(panel_width-36)/6,s,UI_TEXT);
            desktop_box(c,detail,dx+18*s,y+282*s,(panel_width-36)*s,s,0xf0f2f6);
            desktop_text(c,detail,dx+18*s,y+297*s,"Type",4,s,UI_MUTED);
            desktop_label(c,detail,dx+18*s,y+320*s,desktop_kind(entry),(panel_width-36)/6,s,UI_TEXT);
            if (w->h>=450) {
                desktop_box(c,detail,dx+18*s,y+346*s,(panel_width-36)*s,s,0xf0f2f6);
                desktop_text(c,detail,dx+18*s,y+361*s,entry->kind==NV_DIR?"Location":"Size",
                    entry->kind==NV_DIR?8:4,s,UI_MUTED);
                char bytes[32];desktop_size(bytes,entry->size);
                desktop_label(c,detail,dx+18*s,y+384*s,entry->kind==NV_DIR?title:bytes,
                    (panel_width-36)/6,s,UI_TEXT);
            }
            if (w->h>=540) {
                desktop_box(c,detail,dx+18*s,y+(w->h-101)*s,(panel_width-36)*s,s,0xf0f2f6);
                desktop_text(c,detail,dx+18*s,y+(w->h-81)*s,"Enter  Open",11,s,UI_MUTED);
                desktop_text(c,detail,dx+18*s,y+(w->h-58)*s,"F2     Rename",13,s,UI_MUTED);
            }
        } else {
            ui_line_icon(c,detail,dx+(panel_width/2-22)*s,y+133*s,44*s,UI_ICON_DOCUMENT,0xc2cbd9);
            desktop_label(c,detail,dx+18*s,y+211*s,"Select a file to see its details.",
                (panel_width-36)/6,s,UI_MUTED);
        }
    }
    char amount[40];number(amount,v->count,10);u32 at=strlen(amount);
    strlcpy(amount+at,v->count==1?" item":" items",sizeof(amount)-at);
    if (v->file_query && *v->file_query) { at=strlen(amount);strlcpy(amount+at," matched",sizeof(amount)-at); }
    desktop_box(c,main,x+g.left*s,y+(w->h-31)*s,(main_right-g.left)*s,30*s,UI_BG);
    desktop_text(c,main,x+g.left*s,y+(w->h-22)*s,amount,strlen(amount),s,UI_MUTED);
    if (main_right-g.left>270)
        desktop_label(c,main,x+(g.left+112)*s,y+(w->h-22)*s,
            v->message && *v->message?v->message:"Enter open / F2 rename / Del remove",
            (main_right-g.left-112)/6,s,UI_MUTED);
}
#endif
