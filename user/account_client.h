#ifndef NV_ACCOUNT_CLIENT_H
#define NV_ACCOUNT_CLIENT_H
#include "runtime.h"
#include "account_ui.h"
static inline void account_message(struct account_ui *a,const char *text,bool error) {
    strlcpy(a->message,text,sizeof(a->message));a->error=error;
}
static inline void account_failure(struct account_ui *a,int r) {
    const char *text=r==-NV_EACCESS?(a->page==ACCOUNT_LOGIN?"Incorrect username or password.":"Wrong password or permission denied."):
        r==-NV_EINVAL?"Check the username and password requirements.":
        r==-NV_EEXIST?"That username or home folder is already in use.":
        r==-NV_EAGAIN?"Too many attempts. Wait before trying again.":
        r==-NV_ENODEV?"A hardware random generator is required to save passwords.":
        r==-NV_EBUSY?"The current account cannot be removed or disabled.":
        r==-NV_ENOSPC?"There is no space for another account.":
        "Save failed. Restart to reload accounts.";
    account_message(a,text,true);
}
static inline int account_refresh(struct account_ui *a) {
    int r=account_call(NV_ACCOUNT_INFO,&a->info);
    if (r<0) return r;
    u32 chosen=a->count?a->profiles[a->selected].uid:NV_UID_NONE;a->count=0;
    for (u32 i=0;i<a->info.count && i<NV_ACCOUNT_MAX;++i) {
        struct nv_account_profile p={.index=i};
        r=account_call(NV_ACCOUNT_LIST,&p);
        if (r<0) return r;
        if (r && (a->gate || a->info.role==1 || p.uid==a->info.uid)) a->profiles[a->count++]=p;
    }
    a->selected=0;
    for (u32 i=0;i<a->count;++i) if (a->profiles[i].uid==chosen) a->selected=i;
    return 0;
}
static inline void account_page(struct account_ui *a,u32 page) {
    account_wipe(a->fields,sizeof(a->fields));memset(a->positions,0,sizeof(a->positions));
    a->page=page;a->focus=0;a->select_all=a->show_password=false;a->message[0]=0;
    if (page==ACCOUNT_LOGIN && *a->info.name) {
        strlcpy(a->fields[0],a->info.name,sizeof(a->fields[0]));a->positions[0]=strlen(a->fields[0]);a->focus=1;
    } else if (page==ACCOUNT_LOGIN && a->count) {
        strlcpy(a->fields[0],a->profiles[0].name,sizeof(a->fields[0]));a->positions[0]=strlen(a->fields[0]);a->focus=1;
    } else if (page==ACCOUNT_PROFILE && a->count) {
        strlcpy(a->fields[0],a->profiles[a->selected].display_name,sizeof(a->fields[0]));
        a->positions[0]=strlen(a->fields[0]);a->role=a->profiles[a->selected].role;a->flags=a->profiles[a->selected].flags;
    } else if (page==ACCOUNT_ADD) { a->role=0;a->flags=0; }
}
static inline int account_client_open(struct account_ui *a) {
    *a=(struct account_ui){.gate=true};
    int r=account_call(NV_ACCOUNT_CLAIM,NULL);
    if (r<0) return r;
    r=account_refresh(a);
    if (r<0) return r;
    a->gate=(a->info.flags&NV_AUTH_SIGNED_IN)==0 || (a->info.flags&NV_AUTH_LOCKED)!=0;
    account_page(a,a->info.flags&NV_AUTH_ERROR?ACCOUNT_FATAL:
        a->info.flags&NV_AUTH_SETUP?ACCOUNT_WELCOME:a->gate?ACCOUNT_LOGIN:ACCOUNT_LIST);
    return 0;
}
static inline int account_submit(struct account_ui *a) {
    if (a->busy) return 0;
    if (a->page==ACCOUNT_WELCOME) { account_page(a,ACCOUNT_CREATE);return 0; }
    if (a->page==ACCOUNT_READY) { a->gate=false;account_page(a,ACCOUNT_LIST);return ACCOUNT_OPEN_DESKTOP; }
    if (a->page==ACCOUNT_FATAL || a->page==ACCOUNT_LIST) return 0;
    struct nv_account_request request={0};u32 op=0;
    if (a->page==ACCOUNT_CREATE || a->page==ACCOUNT_ADD) {
        if (strcmp(a->fields[2],a->fields[3])) { account_message(a,"The passwords do not match.",true);return 0; }
        strlcpy(request.name,a->fields[0],sizeof(request.name));
        strlcpy(request.display_name,*a->fields[1]?a->fields[1]:a->fields[0],sizeof(request.display_name));
        strlcpy(request.password,a->fields[2],sizeof(request.password));request.role=a->role;
        op=a->page==ACCOUNT_CREATE?NV_ACCOUNT_SETUP:NV_ACCOUNT_ADD;
    } else if (a->page==ACCOUNT_LOGIN) {
        strlcpy(request.name,a->fields[0],sizeof(request.name));
        strlcpy(request.password,a->fields[1],sizeof(request.password));op=NV_ACCOUNT_LOGIN;
    } else if (a->page==ACCOUNT_PASSWORD) {
        if (strcmp(a->fields[1],a->fields[2])) { account_message(a,"The passwords do not match.",true);return 0; }
        request.uid=a->profiles[a->selected].uid;
        strlcpy(request.old_password,a->fields[0],sizeof(request.old_password));
        strlcpy(request.password,a->fields[1],sizeof(request.password));op=NV_ACCOUNT_PASSWORD;
    } else {
        request.uid=a->profiles[a->selected].uid;
        if (a->page==ACCOUNT_PROFILE) {
            request.role=a->role;request.flags=a->flags;
            strlcpy(request.display_name,a->fields[0],sizeof(request.display_name));op=NV_ACCOUNT_UPDATE;
        } else if (a->page==ACCOUNT_REMOVE && a->focus==1) op=NV_ACCOUNT_REMOVE;
        else return 0;
    }
    if ((op==NV_ACCOUNT_SETUP || op==NV_ACCOUNT_ADD || op==NV_ACCOUNT_PASSWORD) && strlen(request.password)<NV_PASSWORD_MIN) {
        account_wipe(&request,sizeof(request));account_message(a,"Use a password with at least 15 characters.",true);return 0;
    }
    int r=account_call(op,&request);account_wipe(&request,sizeof(request));
    if (r<0) { account_failure(a,r);return 0; }
    if (op==NV_ACCOUNT_UPDATE || op==NV_ACCOUNT_REMOVE) {
        account_refresh(a);account_page(a,ACCOUNT_LIST);account_message(a,"Account saved.",false);return 0;
    }
    a->busy=true;a->job=op;a->progress=(struct nv_account_progress){0};a->message[0]=0;
    for (u32 i=0;i<4;++i) if (account_password_field(a->page,i)) {
        account_wipe(a->fields[i],sizeof(a->fields[i]));a->positions[i]=0;
    }
    a->show_password=a->select_all=false;
    return 0;
}
static inline int account_client_poll(struct account_ui *a) {
    if (!a->busy) return 0;
    int r=account_call(NV_ACCOUNT_POLL,&a->progress);
    if (!r) return 0;
    a->busy=false;
    if (r<0 || a->progress.result<0) {
        account_refresh(a);
        if (a->info.flags&NV_AUTH_ERROR) account_page(a,ACCOUNT_FATAL);
        account_failure(a,r<0?r:a->progress.result);
        return 0;
    }
    account_refresh(a);
    if (a->job==NV_ACCOUNT_SETUP) account_page(a,ACCOUNT_READY);
    else if (a->job==NV_ACCOUNT_LOGIN) {
        a->gate=false;account_page(a,ACCOUNT_LIST);return ACCOUNT_OPEN_DESKTOP;
    } else { account_page(a,ACCOUNT_LIST);account_message(a,"Account saved.",false); }
    return 0;
}
static inline int account_activate(struct account_ui *a,u32 hit) {
    if (a->busy) return 0;
    if (hit>=ACCOUNT_HIT_SELECT && hit<ACCOUNT_HIT_SELECT+a->count) {
        a->selected=hit-ACCOUNT_HIT_SELECT;a->focus=0;return 0;
    }
    if (hit>=ACCOUNT_HIT_FIELD && hit<ACCOUNT_HIT_FIELD+account_fields(a->page)) {
        u32 focus=hit-ACCOUNT_HIT_FIELD;
        if (a->page==ACCOUNT_LOGIN && !focus && (a->info.flags&NV_AUTH_LOCKED)) return 0;
        a->focus=focus;a->select_all=false;return 0;
    }
    if (hit==ACCOUNT_HIT_SUBMIT) { if (a->page==ACCOUNT_REMOVE) a->focus=1;return account_submit(a); }
    if (hit==ACCOUNT_HIT_BACK) {
        if (a->page==ACCOUNT_CREATE) account_page(a,ACCOUNT_WELCOME);
        else if (!a->gate) account_page(a,ACCOUNT_LIST);
    } else if (hit==ACCOUNT_HIT_SHOW) a->show_password=!a->show_password;
    else if (hit==ACCOUNT_HIT_ADD && a->info.role==1) account_page(a,ACCOUNT_ADD);
    else if (hit==ACCOUNT_HIT_PASSWORD && a->count) account_page(a,ACCOUNT_PASSWORD);
    else if (hit==ACCOUNT_HIT_PROFILE && a->count) account_page(a,ACCOUNT_PROFILE);
    else if (hit==ACCOUNT_HIT_REMOVE && a->info.role==1 && a->count && a->profiles[a->selected].uid!=a->info.uid)
        account_page(a,ACCOUNT_REMOVE);
    else if (hit==ACCOUNT_HIT_ROLE) a->role=!a->role;
    else if (hit==ACCOUNT_HIT_DISABLE && a->page==ACCOUNT_PROFILE) a->flags=!a->flags;
    return 0;
}
static inline int account_client_key(struct account_ui *a,u32 key,u32 flags) {
    if (a->busy) return 0;
    if (key==27) return account_activate(a,ACCOUNT_HIT_BACK);
    if ((flags&NV_KEY_ALT) && (key=='v' || key=='V')) return account_activate(a,ACCOUNT_HIT_SHOW);
    if ((flags&NV_KEY_ALT) && (key=='r' || key=='R') &&
        (a->page==ACCOUNT_ADD || (a->page==ACCOUNT_PROFILE && a->info.role==1))) return account_activate(a,ACCOUNT_HIT_ROLE);
    if ((flags&NV_KEY_ALT) && (key=='d' || key=='D') && a->page==ACCOUNT_PROFILE && a->info.role==1)
        return account_activate(a,ACCOUNT_HIT_DISABLE);
    if (a->page==ACCOUNT_REMOVE) {
        if (key=='\t') a->focus=!a->focus;
        else if (key=='\n') return a->focus?account_submit(a):account_activate(a,ACCOUNT_HIT_BACK);
        return 0;
    }
    if (a->page==ACCOUNT_LIST) {
        if (key=='\t') a->focus=(a->focus+((flags&NV_KEY_SHIFT)?4:1))%5;
        else if (key==NV_KEY_UP && a->selected) --a->selected;
        else if (key==NV_KEY_DOWN && a->selected+1<a->count) ++a->selected;
        else if (key=='\n') {
            const u32 hits[]={ACCOUNT_HIT_PASSWORD,ACCOUNT_HIT_ADD,ACCOUNT_HIT_PASSWORD,ACCOUNT_HIT_PROFILE,ACCOUNT_HIT_REMOVE};
            return account_activate(a,hits[a->focus]);
        }
        return 0;
    }
    u32 fields=account_fields(a->page);
    if (key=='\n' && (!fields || a->focus+1>=fields)) return account_submit(a);
    if ((key=='\t' || key=='\n') && fields) {
        a->focus=(a->focus+((flags&NV_KEY_SHIFT)?fields-1:1))%fields;
        if (a->page==ACCOUNT_LOGIN && (a->info.flags&NV_AUTH_LOCKED)) a->focus=1;
        a->select_all=false;return 0;
    }
    if (!fields || a->focus>=fields) return 0;
    if (a->page==ACCOUNT_LOGIN && !a->focus && (a->info.flags&NV_AUTH_LOCKED)) return 0;
    char *text=a->fields[a->focus];u32 n=strlen(text),*cursor=&a->positions[a->focus];
    if ((flags&NV_KEY_CTRL) && (key=='a' || key=='A')) { a->select_all=true;return 0; }
    if ((key=='\b' || key==NV_KEY_DELETE || (key>=32 && key<127 && !(flags&(NV_KEY_CTRL|NV_KEY_ALT|NV_KEY_META)))) && a->select_all) {
        account_wipe(text,NV_PASSWORD_MAX+1);*cursor=n=0;a->select_all=false;
    }
    if (key==NV_KEY_LEFT && *cursor) --*cursor;
    else if (key==NV_KEY_RIGHT && *cursor<n) ++*cursor;
    else if (key==NV_KEY_HOME) *cursor=0;
    else if (key==NV_KEY_END) *cursor=n;
    else if (key=='\b' && *cursor) { memmove(text+*cursor-1,text+*cursor,n-*cursor+1);--*cursor; }
    else if (key==NV_KEY_DELETE && *cursor<n) memmove(text+*cursor,text+*cursor+1,n-*cursor);
    else if (key>=32 && key<127 && !(flags&(NV_KEY_CTRL|NV_KEY_ALT|NV_KEY_META))) {
        u32 limit=NV_PASSWORD_MAX;
        if ((a->page==ACCOUNT_CREATE || a->page==ACCOUNT_ADD || a->page==ACCOUNT_LOGIN) && !a->focus) limit=31;
        else if ((a->page==ACCOUNT_CREATE || a->page==ACCOUNT_ADD) && a->focus==1) limit=63;
        else if (a->page==ACCOUNT_PROFILE) limit=63;
        if (n<limit) { memmove(text+*cursor+1,text+*cursor,n-*cursor+1);text[(*cursor)++]=(char)key; }
    }
    if (key==NV_KEY_LEFT || key==NV_KEY_RIGHT || key==NV_KEY_HOME || key==NV_KEY_END) a->select_all=false;
    return 0;
}
#endif
