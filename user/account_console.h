#ifndef NV_ACCOUNT_CONSOLE_H
#define NV_ACCOUNT_CONSOLE_H
#include "runtime.h"
static void console_secret_clear(void *p,usize n) { volatile u8 *b=p;while (n--) *b++=0; }
static int console_password(char *out,u32 capacity) {
    u32 n=0;out[0]=0;
    for (;;) {
        char character;int event=take(0,&character,1);
        if (!event || event==-NV_EAGAIN) { nap(10);continue; }
        if (event<0) { console_secret_clear(out,capacity);return event; }
        u32 key=(u8)character;
        if (key=='\n') { print("\n");return (int)n; }
        if (key=='\b' && n) { out[--n]=0;print("\b \b"); }
        else if (key>=32 && key<127 && n+1<capacity) {
            out[n++]=(char)key;out[n]=0;print("*");
        }
    }
}
/* The init shell must authenticate even after a desktop crash, or in BIOS
 * text mode. Native terminal clients inherit an authenticated identity. */
static int console_account_gate(void) {
    struct nv_account_info state;
    int r=account_call(NV_ACCOUNT_INFO,&state);
    if (r==-NV_ENODEV) return 0; /* Explicit nv.test=1 regression boot. */
    if (r<0) return r;
    r=account_call(NV_ACCOUNT_CLAIM,NULL);
    if (r<0) return r;
    for (;;) {
        r=account_call(NV_ACCOUNT_INFO,&state);
        if (r<0) return r;
        if (state.flags&NV_AUTH_ERROR) { println("Account database unavailable. Restart to reload it.");return -NV_EIO; }
        if ((state.flags&NV_AUTH_SIGNED_IN) && !(state.flags&NV_AUTH_LOCKED)) return 0;
        struct nv_account_request request={0};
        bool setup=(state.flags&NV_AUTH_SETUP)!=0;
        println(setup?"Nuvora first setup: create the administrator account.":"Nuvora local sign-in");
        if (setup && !(state.flags&NV_AUTH_PERSISTENT)) println("No data disk: this account lasts until shutdown.");
        if (state.flags&NV_AUTH_LOCKED) strlcpy(request.name,state.name,sizeof(request.name));
        else {
            print("Username: ");
            r=read_line(request.name,sizeof(request.name));
            if (r<0) { console_secret_clear(&request,sizeof(request));continue; }
        }
        if (setup) { print("Display name: ");r=read_line(request.display_name,sizeof(request.display_name));
            if (r<0) { console_secret_clear(&request,sizeof(request));continue; }
            if (!*request.display_name) strlcpy(request.display_name,request.name,sizeof(request.display_name));
            println("Password: 15-128 characters. Spaces are allowed.");
        }
        print("Password: ");r=console_password(request.password,sizeof(request.password));
        if (r>=0 && setup) {
            char repeated[NV_PASSWORD_MAX+1];print("Repeat password: ");
            r=console_password(repeated,sizeof(repeated));
            if (r>=0 && strcmp(repeated,request.password)) r=-NV_EINVAL;
            console_secret_clear(repeated,sizeof(repeated));
        }
        if (r>=0) r=account_call(setup?NV_ACCOUNT_SETUP:NV_ACCOUNT_LOGIN,&request);
        console_secret_clear(&request,sizeof(request));
        if (r<0) { report_error("Sign-in",r);if (r==-NV_EAGAIN) nap(2000);continue; }
        println("Checking account...");
        struct nv_account_progress progress;
        do { r=account_call(NV_ACCOUNT_POLL,&progress);if (!r) nap(1); } while (!r);
        if (r<0 || progress.result<0) report_error("Sign-in",r<0?r:progress.result);
    }
}
#endif
