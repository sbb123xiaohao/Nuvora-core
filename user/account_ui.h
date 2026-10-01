#ifndef NV_ACCOUNT_UI_H
#define NV_ACCOUNT_UI_H
#include "gui_text.h"
#include <nv/abi.h>
enum { ACCOUNT_WELCOME,ACCOUNT_CREATE,ACCOUNT_READY,ACCOUNT_LOGIN,ACCOUNT_LIST,
       ACCOUNT_ADD,ACCOUNT_PASSWORD,ACCOUNT_PROFILE,ACCOUNT_REMOVE,ACCOUNT_FATAL };
enum { ACCOUNT_HIT_NONE,ACCOUNT_HIT_FIELD=1,ACCOUNT_HIT_SUBMIT=16,ACCOUNT_HIT_BACK,
       ACCOUNT_HIT_SHOW,ACCOUNT_HIT_ADD,ACCOUNT_HIT_PASSWORD,ACCOUNT_HIT_PROFILE,
       ACCOUNT_HIT_REMOVE,ACCOUNT_HIT_ROLE,ACCOUNT_HIT_DISABLE,ACCOUNT_HIT_SELECT=64 };
enum { ACCOUNT_OPEN_DESKTOP=1000 };
struct account_ui {
    struct nv_account_info info;
    struct nv_account_profile profiles[NV_ACCOUNT_MAX];
    struct nv_account_progress progress;
    u32 page,focus,selected,count,job,role,flags,positions[4];
    bool gate,busy,select_all,show_password,error;
    char fields[4][NV_PASSWORD_MAX+1],message[160];
};
struct account_rect { u32 x,y,w,h; };
static inline void account_wipe(void *p,usize n) { volatile u8 *b=p;while (n--) *b++=0; }
static inline u32 account_fields(u32 page) {
    return page==ACCOUNT_CREATE || page==ACCOUNT_ADD?4:page==ACCOUNT_LOGIN?2:
        page==ACCOUNT_PASSWORD?3:page==ACCOUNT_PROFILE?1:0;
}
static inline bool account_password_field(u32 page,u32 field) {
    return ((page==ACCOUNT_CREATE || page==ACCOUNT_ADD) && field>=2) ||
        (page==ACCOUNT_LOGIN && field==1) || page==ACCOUNT_PASSWORD;
}
static inline struct account_rect account_gate_rect(u32 sw,u32 sh,const struct account_ui *a) {
    u32 w=MIN(a->page==ACCOUNT_LOGIN?460u:560u,sw-32);
    u32 h=MIN(a->page==ACCOUNT_LOGIN?292u:398u,sh-48);
    return (struct account_rect){(sw-w)/2,(sh-h)/2,w,h};
}
static inline const char *account_page_title(u32 page) {
    static const char *const titles[]={"Set up Nuvora","Create your account","Account ready","Sign in",
        "Accounts","Add account","Change password","Account details","Remove account","Accounts unavailable"};
    return titles[MIN(page,(u32)ARRAY_LEN(titles)-1)];
}
static inline struct account_rect account_field_rect(u32 w,u32 field) {
    return (struct account_rect){22,64+field*48+17,w-44,28};
}
static inline void account_button(struct nv_canvas *c,struct desktop_clip clip,u32 x,u32 y,
    u32 w,const char *label,u32 scale,bool enabled,bool focus,bool danger) {
    u32 color=!enabled?0xe1e6e7:danger?0x9c4439:focus?0x28617a:0xdfe7e9;
    desktop_round(c,clip,x,y,w,28*scale,7*scale,color);
    u32 length=(u32)strlen(label),text=desktop_text_width(label,length,scale);
    desktop_text(c,clip,x+(w>text?(w-text)/2:3*scale),y+6*scale,label,length,scale,
        !enabled?0x88989f:focus || danger?0xffffff:0x27434e);
}
static inline void account_ui_render(struct nv_canvas *c,struct desktop_clip clip,u32 x,u32 y,
    u32 w,u32 h,u32 scale,const struct account_ui *a) {
    u32 width=w*scale,height=h*scale;
    if (a->gate) desktop_round(c,clip,x,y,width,height,14*scale,0xf6f8f9);
    else desktop_box(c,clip,x,y,width,height,0xf6f8f9);
    clip.left=MAX(clip.left,x);clip.top=MAX(clip.top,y);
    clip.right=MIN(clip.right,x+width);clip.bottom=MIN(clip.bottom,y+height);
    const char *title=account_page_title(a->page);char named[96];
    if (a->count && (a->page==ACCOUNT_PASSWORD || a->page==ACCOUNT_PROFILE || a->page==ACCOUNT_REMOVE)) {
        u32 n=strlcpy(named,title,sizeof(named));strlcpy(named+n,": ",sizeof(named)-n);n+=2;
        strlcpy(named+n,a->profiles[a->selected].name,sizeof(named)-n);title=named;
    }
    desktop_bold_text(c,clip,x+22*scale,y+17*scale,title,strlen(title),scale,0x243e4b);
    const char *subtitle=a->page==ACCOUNT_LOGIN?(a->info.flags&NV_AUTH_LOCKED?"Unlock your session":"Local account"):
        a->page==ACCOUNT_CREATE || a->page==ACCOUNT_ADD?"Password: 15-128 characters. Spaces are allowed.":
        a->page==ACCOUNT_PASSWORD?"Use your current password to authorize this change.":
        a->page==ACCOUNT_LIST?(a->info.role==1?"Manage local users on this computer":"Your local account"):
        a->page==ACCOUNT_WELCOME?"Create an account before opening the desktop.":"";
    desktop_label(c,clip,x+22*scale,y+37*scale,subtitle,(w-44)/6,scale,0x647984);
    if (a->page==ACCOUNT_WELCOME) {
        desktop_bold_text(c,clip,x+22*scale,y+89*scale,"Desktop",7,scale,0x2b4858);
        desktop_label(c,clip,x+22*scale,y+113*scale,"Windows, files, a terminal and media playback.",(w-44)/6,scale,0x506b78);
        desktop_bold_text(c,clip,x+22*scale,y+157*scale,"Local account",13,scale,0x2b4858);
        desktop_label(c,clip,x+22*scale,y+181*scale,"The first account manages other users.",(w-44)/6,scale,0x506b78);
        desktop_label(c,clip,x+22*scale,y+228*scale,a->info.flags&NV_AUTH_PERSISTENT?
            "Accounts and files will be saved to the data disk.":
            "No data disk: accounts last until shutdown.",(w-44)/6,scale,0x506b78);
    } else if (a->page==ACCOUNT_READY) {
        desktop_bold_text(c,clip,x+22*scale,y+89*scale,a->info.display_name,strlen(a->info.display_name),scale,0x2b4858);
        desktop_label(c,clip,x+22*scale,y+121*scale,a->info.home,(w-44)/6,scale,0x506b78);
        desktop_label(c,clip,x+22*scale,y+174*scale,"Start opens apps. Alt-Tab switches windows.",(w-44)/6,scale,0x506b78);
        desktop_label(c,clip,x+22*scale,y+199*scale,"Win-L locks the session.",(w-44)/6,scale,0x506b78);
    } else if (a->page==ACCOUNT_LIST) {
        u32 shown=MIN(a->count,(h-120)/42),first=a->selected>=shown?a->selected-shown+1:0;
        u32 listw=MIN(180u,w/3);
        desktop_box(c,clip,x+listw*scale,y+64*scale,scale,(h-124)*scale,0xd6e0e4);
        for (u32 i=first;i<first+shown && i<a->count;++i) {
            u32 top=66+(i-first)*42;
            if (i==a->selected) desktop_box(c,clip,x+10*scale,y+top*scale,(listw-20)*scale,38*scale,0xdbe9ed);
            desktop_label(c,clip,x+20*scale,y+(top+4)*scale,a->profiles[i].display_name,(listw-38)/6,scale,0x274857);
            desktop_label(c,clip,x+20*scale,y+(top+21)*scale,a->profiles[i].name,(listw-38)/6,scale,0x617983);
        }
        if (a->count) {
            const struct nv_account_profile *p=&a->profiles[a->selected];u32 dx=x+(listw+20)*scale;
            desktop_label(c,clip,dx,y+77*scale,p->display_name,(w-listw-40)/6,scale,0x243f4e);
            desktop_label(c,clip,dx,y+101*scale,p->name,(w-listw-40)/6,scale,0x657984);
            const char *status=p->flags&NV_ACCOUNT_DISABLED?"Disabled":p->role==1?"Administrator":"Standard account";
            desktop_text(c,clip,dx,y+128*scale,status,strlen(status),scale,0x506b78);
            account_button(c,clip,dx,y+169*scale,MIN(170u,w-listw-40)*scale,"Change password",scale,!a->busy,a->focus==2,false);
            account_button(c,clip,dx,y+207*scale,MIN(170u,w-listw-40)*scale,"Account details",scale,!a->busy,a->focus==3,false);
            account_button(c,clip,dx,y+245*scale,MIN(170u,w-listw-40)*scale,"Remove account",scale,
                !a->busy && a->info.role==1 && p->uid!=a->info.uid,a->focus==4,false);
        }
    } else if (a->page==ACCOUNT_REMOVE) {
        const struct nv_account_profile *p=&a->profiles[a->selected];
        desktop_label(c,clip,x+22*scale,y+92*scale,p->name,(w-44)/6,scale,0x9c4439);
        desktop_label(c,clip,x+22*scale,y+133*scale,"This account will no longer be able to sign in.",(w-44)/6,scale,0x506b78);
        desktop_label(c,clip,x+22*scale,y+163*scale,"Its home files will be kept. Removal cannot be undone.",(w-44)/6,scale,0x506b78);
    } else if (a->page==ACCOUNT_FATAL) {
        desktop_label(c,clip,x+22*scale,y+95*scale,"The account database could not be loaded.",(w-44)/6,scale,0x9c4439);
        desktop_label(c,clip,x+22*scale,y+131*scale,"Restart to reload it. Existing files have not been erased.",(w-44)/6,scale,0x506b78);
    } else {
        const char *create_labels[]={"Username (lowercase first; a-z, 0-9, - or _)","Display name","Password","Repeat password"};
        const char *login_labels[]={"Username","Password"};
        const char *password_labels[]={"Your current password","New password","Repeat new password"};
        u32 fields=account_fields(a->page);
        for (u32 i=0;i<fields;++i) {
            struct account_rect r=account_field_rect(w,i);
            const char *label=a->page==ACCOUNT_LOGIN?login_labels[i]:a->page==ACCOUNT_PASSWORD?
                password_labels[i]:a->page==ACCOUNT_PROFILE?"Display name":create_labels[i];
            desktop_text(c,clip,x+r.x*scale,y+(r.y-17)*scale,label,strlen(label),scale,0x506b78);
            bool focus=a->focus==i && !a->busy;
            desktop_round(c,clip,x+r.x*scale,y+r.y*scale,r.w*scale,r.h*scale,7*scale,focus?0x6686d7:0xc8d3e7);
            desktop_round(c,clip,x+(r.x+1)*scale,y+(r.y+1)*scale,(r.w-2)*scale,(r.h-2)*scale,6*scale,0xffffff);
            char masked[NV_PASSWORD_MAX+1];const char *value=a->fields[i];u32 n=strlen(value);
            if (account_password_field(a->page,i) && !a->show_password) {
                for (u32 j=0;j<n;++j) masked[j]='*';
                masked[n]=0;value=masked;
            }
            u32 first=0,budget=(r.w-14)*scale,caret=MIN(a->positions[i],n);
            while (first<caret && desktop_text_width(value+first,caret-first,scale)>=budget) ++first;
            u32 length=n-first;
            while (length && desktop_text_width(value+first,length,scale)>budget) --length;
            u32 text=desktop_text_width(value+first,length,scale),caret_x=desktop_text_width(value+first,caret-first,scale);
            if (focus && a->select_all) desktop_box(c,clip,x+(r.x+6)*scale,y+(r.y+5)*scale,
                MIN(text,budget),18*scale,0xd4e5ef);
            desktop_text(c,clip,x+(r.x+6)*scale,y+(r.y+5)*scale,value+first,length,scale,0x2b4656);
            if (focus && !a->select_all) desktop_box(c,clip,x+(r.x+7)*scale+caret_x,y+(r.y+5)*scale,scale,17*scale,0x357694);
            account_wipe(masked,sizeof(masked));
        }
        if (a->page==ACCOUNT_LOGIN || a->page==ACCOUNT_PASSWORD) {
            u32 top=64+fields*48+5;
            desktop_box(c,clip,x+22*scale,y+top*scale,12*scale,12*scale,a->show_password?0x357694:0xc5d0d6);
            desktop_text(c,clip,x+42*scale,y+(top-2)*scale,"Show password",13,scale,0x647984);
        }
        if (a->page==ACCOUNT_ADD || (a->page==ACCOUNT_PROFILE && a->info.role==1)) {
            u32 top=64+fields*48+4;
            const char *role=a->role==1?"Administrator   Alt-R: change":"Standard account   Alt-R: change";
            desktop_text(c,clip,x+22*scale,y+top*scale,role,strlen(role),scale,0x28617a);
            if (a->page==ACCOUNT_PROFILE)
                desktop_text(c,clip,x+22*scale,y+(top+35)*scale,a->flags?"Disabled   Alt-D: change":"Enabled   Alt-D: change",
                    a->flags?23:22,scale,0x28617a);
        }
    }
    if (a->busy) {
        const char *text=a->job==NV_ACCOUNT_LOGIN?"Checking password...":"Saving account...";
        desktop_label(c,clip,x+22*scale,y+(h-93)*scale,text,(w-44)/6,scale,0x28617a);
        desktop_box(c,clip,x+22*scale,y+(h-72)*scale,(w-44)*scale,3*scale,0xd1dce1);
        if (a->progress.total) desktop_box(c,clip,x+22*scale,y+(h-72)*scale,
            (u32)((u64)(w-44)*scale*a->progress.completed/a->progress.total),3*scale,0x357694);
    } else if (*a->message) desktop_label(c,clip,x+22*scale,y+(h-84)*scale,a->message,(w-44)/6,scale,a->error?0x9c4439:0x417361);
    if (a->page==ACCOUNT_LIST) {
        account_button(c,clip,x+22*scale,y+(h-45)*scale,114*scale,"Add account",scale,a->info.role==1 && !a->busy,a->focus==1,false);
    } else {
        const char *primary=a->page==ACCOUNT_WELCOME?"Continue":a->page==ACCOUNT_CREATE?"Create account":
            a->page==ACCOUNT_READY?"Open desktop":a->page==ACCOUNT_LOGIN?"Unlock":"Save changes";
        if (a->page==ACCOUNT_LOGIN && !(a->info.flags&NV_AUTH_LOCKED)) primary="Sign in";
        if (a->page==ACCOUNT_ADD) primary="Add account";
        if (a->page==ACCOUNT_REMOVE) primary="Remove account";
        if (a->page!=ACCOUNT_FATAL) account_button(c,clip,x+(w-166)*scale,y+(h-45)*scale,
            144*scale,primary,scale,!a->busy,a->page!=ACCOUNT_REMOVE || a->focus==1,a->page==ACCOUNT_REMOVE);
        if (a->page!=ACCOUNT_WELCOME && a->page!=ACCOUNT_READY && a->page!=ACCOUNT_FATAL && a->page!=ACCOUNT_LOGIN)
            account_button(c,clip,x+22*scale,y+(h-45)*scale,94*scale,a->page==ACCOUNT_CREATE?"Back":"Cancel",scale,!a->busy,
                a->page==ACCOUNT_REMOVE && !a->focus,false);
    }
}
static inline u32 account_ui_hit(u32 w,u32 h,const struct account_ui *a,u32 x,u32 y) {
    if (a->busy) return ACCOUNT_HIT_NONE;
    u32 fields=account_fields(a->page);
    for (u32 i=0;i<fields;++i) {
        struct account_rect r=account_field_rect(w,i);
        if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) return ACCOUNT_HIT_FIELD+i;
    }
    if ((a->page==ACCOUNT_LOGIN || a->page==ACCOUNT_PASSWORD) && x>=22 && x<185 &&
        y>=64+fields*48 && y<64+fields*48+23) return ACCOUNT_HIT_SHOW;
    if (a->page==ACCOUNT_ADD || (a->page==ACCOUNT_PROFILE && a->info.role==1)) {
        if (x>=22 && x<w-22 && y>=64+fields*48 && y<64+fields*48+24) return ACCOUNT_HIT_ROLE;
        if (a->page==ACCOUNT_PROFILE && x>=22 && x<w-22 && y>=64+fields*48+30 && y<64+fields*48+59) return ACCOUNT_HIT_DISABLE;
    }
    if (a->page==ACCOUNT_LIST) {
        u32 listw=MIN(180u,w/3),shown=MIN(a->count,(h-120)/42),first=a->selected>=shown?a->selected-shown+1:0;
        if (x>=10 && x<listw-10 && y>=66 && y<66+shown*42) return ACCOUNT_HIT_SELECT+first+(y-66)/42;
        if (a->count && x>=listw+20 && x<w-20) {
            if (y>=169 && y<197) return ACCOUNT_HIT_PASSWORD;
            if (y>=207 && y<235) return ACCOUNT_HIT_PROFILE;
            if (y>=245 && y<273 && a->info.role==1 && a->profiles[a->selected].uid!=a->info.uid) return ACCOUNT_HIT_REMOVE;
        }
        if (x>=22 && x<136 && y>=h-45 && y<h-17 && a->info.role==1) return ACCOUNT_HIT_ADD;
    } else if (y>=h-45 && y<h-17) {
        if (x>=w-166 && x<w-22 && a->page!=ACCOUNT_FATAL) return ACCOUNT_HIT_SUBMIT;
        if (x>=22 && x<116 && a->page!=ACCOUNT_LOGIN) return ACCOUNT_HIT_BACK;
    }
    return ACCOUNT_HIT_NONE;
}
#endif
