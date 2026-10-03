#ifndef NV_FILES_UI_H
#define NV_FILES_UI_H
/* The Spaces canvas shares layout with hit testing and keyboard scrolling. */
static void files_badge(struct nv_canvas *c,struct desktop_clip clip,u32 x,u32 y,u32 s,
    const struct nv_dirent64 *entry) {
    static const u32 tones[]={0x6ba99a,0xc99160,0x8b84b7,0x7294aa,0x7d9697};
    static const char *tags[]={"DIR","TXT","SND","VID","OBJ"};
    u32 kind=files_category(entry);
    desktop_round(c,clip,x,y,48*s,34*s,10*s,tones[kind]);
    desktop_bold_text(c,clip,x+7*s,y+9*s,tags[kind],3,s,0xffffff);
}
static void files_path_title(char title[NV_PATH_MAX],const struct desktop_view *v) {
    const char *path=v->path?v->path:"/";
    const char *last=path;
    for (const char *p=path;*p;++p) if (*p=='/' && p[1]) last=p+1;
    if (v->account && !strcmp(last,v->account->info.name)) last="Home";
    if (!strcmp(path,"/") || !strcmp(path,"C:/")) last=!strcmp(path,"/")?"System":"Home";
    strlcpy(title,last,NV_PATH_MAX);
}
static void desktop_files_render(struct nv_canvas *c,struct desktop_clip clip,u32 x,u32 y,u32 s,
    const struct desktop_view *v) {
    const struct desktop_window *w=&v->windows[DESKTOP_FILES];
    struct files_layout g=desktop_files_layout(w,v->file_view);
    desktop_box(c,clip,x+s,y+28*s,(w->w-2)*s,(w->h-29)*s,0xf6f4ef);
    desktop_box(c,clip,x+s,y+28*s,121*s,(w->h-29)*s,0x17383c);
    desktop_bold_text(c,clip,x+15*s,y+46*s,"SPACES",6,s,0xd6eee5);
    desktop_text(c,clip,x+15*s,y+70*s,"Find your flow",14,s,0x8db4ae);
    u32 spaces=MAX(1u,v->volumes);
    for (u32 i=0;i<spaces+3 && i<7;++i) {
        u32 top=94+i*36;
        if (top+30>=w->h-58) break;
        char label[24];
        if (i<spaces) {
            strlcpy(label,i?"Volume ":"Home",sizeof(label));
            if (i) number(label+7,i+1,10);
        } else {
            const char *places[]={"System","Applications","Scratch"};
            strlcpy(label,places[i-spaces],sizeof(label));
        }
        bool active=i==0 && v->path &&
            (!strncmp(v->path,"/home",5) || !strncmp(v->path,"C:",2));
        if (v->path && i && i<spaces) active=(v->path[0]=='C'+(char)i && v->path[1]==':') ||
            (!strncmp(v->path,"/drives/",8) && v->path[8]=='C'+(char)i);
        if (v->path && i>=spaces) active=i==spaces?!strcmp(v->path,"/"):
            i==spaces+1?!strncmp(v->path,"/apps",5):!strncmp(v->path,"/tmp",4);
        if (active) desktop_round(c,clip,x+9*s,y+top*s,105*s,30*s,9*s,0x2d5555);
        desktop_round(c,clip,x+17*s,y+(top+12)*s,6*s,6*s,3*s,active?0xe0ae7c:0x75a59d);
        desktop_label(c,clip,x+31*s,y+(top+8)*s,label,12,s,active?0xf4ead9:0xafc9c1);
    }
    desktop_round(c,clip,x+12*s,y+(w->h-48)*s,100*s,26*s,8*s,0x2d5555);
    desktop_text(c,clip,x+27*s,y+(w->h-41)*s,"Up a level",10,s,0xd6eee5);
    char title[NV_PATH_MAX];files_path_title(title,v);
    desktop_label(c,clip,x+134*s,y+41*s,title,w->w>299?(w->w-299)/6:0,s,0x274b49);
    char location[NV_PATH_MAX];const char *path=v->path?v->path:"/";
    if (path[0] && path[1]==':' && path[0]>='C' && path[0]<='F') {
        strlcpy(location,path[0]=='C'?"Home / ":"Volume / ",sizeof(location));
        const char *tail=path+2;if (*tail=='/') ++tail;
        strlcpy(location+strlen(location),tail,sizeof(location)-strlen(location));
    } else strlcpy(location,path,sizeof(location));
    desktop_label(c,clip,x+134*s,y+66*s,location,(w->w-148)/6,s,0x728380);
    desktop_round(c,clip,x+(w->w-147)*s,y+35*s,70*s,25*s,7*s,0xe4e7de);
    desktop_round(c,clip,x+(w->w-74)*s,y+35*s,67*s,25*s,7*s,0x2d6862);
    desktop_text(c,clip,x+(w->w-139)*s,y+42*s,"+ Space",7,s,0x365b55);
    desktop_text(c,clip,x+(w->w-66)*s,y+42*s,"+ Note",6,s,0xffffff);
    u32 query_width=w->w-226;
    desktop_round(c,clip,x+134*s,y+86*s,query_width*s,30*s,9*s,
        v->file_query_focus?0xc6dfd3:0xe5e8df);
    desktop_round(c,clip,x+136*s,y+88*s,(query_width-4)*s,26*s,7*s,0xfcfcf7);
    const char *query=v->file_query && *v->file_query?v->file_query:"Find in this space  /  Ctrl-F";
    desktop_label(c,clip,x+146*s,y+94*s,query,(query_width-24)/7,s,
        v->file_query && *v->file_query?0x294c49:0x82918a);
    desktop_round(c,clip,x+(w->w-82)*s,y+86*s,68*s,30*s,9*s,0xe5e8df);
    desktop_text(c,clip,x+(w->w-72)*s,y+94*s,v->file_view?"Cards":"List",v->file_view?5:4,s,0x48685e);
    const char *sorts[]={"Name","Type","Size"};
    char sorting[24]="Sort: ";strlcpy(sorting+6,sorts[MIN(v->file_sort,2u)],18);
    desktop_round(c,clip,x+134*s,y+126*s,92*s,26*s,8*s,0xe8e9e0);
    desktop_text(c,clip,x+142*s,y+133*s,sorting,strlen(sorting),s,0x4b695f);
    desktop_round(c,clip,x+234*s,y+126*s,98*s,26*s,8*s,0xe8e9e0);
    desktop_text(c,clip,x+242*s,y+133*s,v->show_hidden?"Hidden on":"Hidden off",v->show_hidden?9:10,s,0x4b695f);
    for (u32 i=0;i<g.rows*g.columns && v->scroll+i<v->count;++i) {
        u32 index=v->scroll+i;
        struct desktop_rect r=desktop_file_rect(g,i);
        u32 left=x+r.x*s,top=y+r.y*s;
        bool selected=index==v->selected;
        desktop_round(c,clip,left,top,r.w*s,r.h*s,12*s,selected?0x9fc4b4:0xe1e5d9);
        desktop_round(c,clip,left+s,top+s,(r.w-2)*s,(r.h-2)*s,11*s,selected?0xe9f0df:0xfcfcf7);
        if (v->file_view) {
            const char *kind=desktop_kind(&v->entries[index]);
            desktop_round(c,clip,left+10*s,top+12*s,5*s,12*s,2*s,
                files_category(&v->entries[index])==0?0x6ba99a:0xc99160);
            desktop_label(c,clip,left+24*s,top+12*s,v->entries[index].name,
                r.w>220?(r.w-200)/7:(r.w-36)/7,s,0x2c4942);
            if (r.w>220) desktop_label(c,clip,left+(r.w-166)*s,top+12*s,kind,18,s,0x819088);
        } else {
            files_badge(c,clip,left+12*s,top+10*s,s,&v->entries[index]);
            desktop_label(c,clip,left+12*s,top+55*s,v->entries[index].name,(r.w-24)/7,s,0x2c4942);
            char detail[32];
            if (v->entries[index].kind==NV_DIR) strlcpy(detail,"Folder / Enter to open",sizeof(detail));
            else { desktop_size(detail,v->entries[index].size);u32 at=strlen(detail);
                strlcpy(detail+at," / ",sizeof(detail)-at);at=strlen(detail);
                strlcpy(detail+at,desktop_kind(&v->entries[index]),sizeof(detail)-at); }
            desktop_label(c,clip,left+12*s,top+78*s,detail,(r.w-24)/7,s,0x809083);
        }
    }
    if (!v->count) {
        desktop_label(c,clip,x+g.left*s,y+(g.top+12)*s,
            v->file_query && *v->file_query?"No matching items":"A quiet space",g.width/7,s,0x4b695f);
        desktop_label(c,clip,x+g.left*s,y+(g.top+42)*s,
            v->file_query && *v->file_query?"Clear the search to see everything.":"Create a space or write your first note.",
            g.width/7,s,0x86958a);
    }
    if (g.inspector) {
        u32 dx=x+(w->w-180)*s;
        desktop_round(c,clip,dx,y+166*s,164*s,(w->h-210)*s,13*s,0xe8ece1);
        desktop_bold_text(c,clip,dx+14*s,y+184*s,"IN FOCUS",8,s,0x526e62);
        if (v->count && v->selected<v->count) {
            const struct nv_dirent64 *item=&v->entries[v->selected];
            files_badge(c,clip,dx+14*s,y+218*s,s,item);
            desktop_label(c,clip,dx+14*s,y+272*s,item->name,19,s,0x274b49);
            desktop_label(c,clip,dx+14*s,y+301*s,desktop_kind(item),19,s,0x7a8c80);
            char bytes[32];desktop_size(bytes,item->size);
            desktop_label(c,clip,dx+14*s,y+329*s,item->kind==NV_DIR?"Open to explore":bytes,19,s,0x7a8c80);
            if (w->h>430) {
                desktop_text(c,clip,dx+14*s,y+373*s,"Enter  Open",11,s,0x577365);
                desktop_text(c,clip,dx+14*s,y+397*s,"F2     Rename",13,s,0x577365);
            }
        } else desktop_text(c,clip,dx+14*s,y+225*s,"Select an item",14,s,0x7a8c80);
    }
    char amount[40];number(amount,v->count,10);u32 at=strlen(amount);
    strlcpy(amount+at,v->count==1?" item":" items",sizeof(amount)-at);
    if (v->file_query && *v->file_query) { at=strlen(amount);strlcpy(amount+at," matched",sizeof(amount)-at); }
    desktop_box(c,clip,x+123*s,y+(w->h-31)*s,(w->w-124)*s,30*s,0xf6f4ef);
    desktop_text(c,clip,x+134*s,y+(w->h-21)*s,amount,strlen(amount),s,0x748a7e);
    desktop_label(c,clip,x+260*s,y+(w->h-21)*s,*v->message?v->message:"Enter open / F2 rename / Del remove",
        w->w>274?(w->w-274)/7:0,s,0x967651);
}
#endif
