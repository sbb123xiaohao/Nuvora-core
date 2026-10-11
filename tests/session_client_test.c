/* Run the production window adapter and line reader with controlled syscalls. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_SDK_H
static bool session_locked;
static int poll_error, info_error;
static u32 sleeps, begins, source_at, event_at, event_count;
static bool lock_on_read;
static const char *source;
static struct nv_window_event events[4];
static iptr nv_syscall(u32 op,uptr a,uptr b,uptr c) {
    if (op==NV_DEVCTL && a==NV_SUB_ACCOUNT && b==NV_ACCOUNT_INFO) {
        if (info_error) return info_error;
        *(struct nv_account_info *)c=(struct nv_account_info){.flags=session_locked?NV_AUTH_LOCKED:NV_AUTH_SIGNED_IN};
        return 0;
    }
    if (op==NV_TAKE) {
        assert(a==0 && c==1);
        if (lock_on_read && source_at==2) { lock_on_read=false;session_locked=true; }
        if (session_locked) return -NV_EACCESS;
        if (poll_error) return poll_error;
        *(char *)b=source[source_at++];return 1;
    }
    if (op==NV_SLEEP) { ++sleeps;session_locked=false;return 0; }
    if (op==NV_EMIT) return c;
    assert(!"unexpected syscall");return -NV_ENOSYS;
}
static int nv_window_info(struct nv_window_info *out) { out->server_pid=2;return 0; }
static int nv_window_create(struct nv_window_create *io) { io->id=3;io->serial=1;return 0; }
static int nv_window_poll(u32 id,struct nv_window_event *out) {
    assert(id==3);
    if (session_locked) return -NV_EACCESS;
    if (poll_error) return poll_error;
    if (event_at==event_count) return 0;
    *out=events[event_at++];return 1;
}
static int nv_window_begin(struct nv_window_frame *io) { assert(io->id==3);++begins;return 0; }
static int nv_window_upload(struct nv_window_pixels *io) { (void)io;return 0; }
#include "../user/runtime.c"
#include "../user/window_client.h"

static void window_test(void) {
    struct app_window w;
    assert(!app_window_open(&w,"test",640,400));
    events[0]=(struct nv_window_event){.type=NV_WINDOW_EVENT_KEY,.key='x'};
    event_count=1;assert(!app_window_pump(&w) && w.queued==1);
    session_locked=true;
    assert(!app_window_pump(&w) && !w.closed && !w.queued);
    assert(app_window_key(&w)==-NV_EAGAIN);
    assert(app_window_begin(&w)==-NV_EAGAIN && !begins);
    /* A full client queue must not skip the locked-session check. */
    w.queued=ARRAY_LEN(w.input);w.head=w.tail=0;
    for (u32 i=0;i<w.queued;++i) w.input[i]=events[0];
    assert(app_window_key(&w)==-NV_EAGAIN && !w.queued);
    struct nv_window_event out={.key=0xdeadbeef};
    assert(!app_window_input(&w,NV_WINDOW_EVENT_POINTER,&out) && out.key==0xdeadbeef);
    session_locked=false;
    events[1]=(struct nv_window_event){.type=NV_WINDOW_EVENT_CONFIGURE,.serial=2,.width=800,.height=500};
    events[2]=(struct nv_window_event){.type=NV_WINDOW_EVENT_KEY,.key='y'};event_count=3;
    assert(app_window_key(&w)=='y' && w.serial==2 && w.width==800 && w.height==500);
    assert(!app_window_begin(&w) && begins==1);
    poll_error=-NV_EACCESS;assert(app_window_pump(&w)==-NV_EACCESS);
    info_error=-NV_EIO;assert(app_window_pump(&w)==-NV_EACCESS);
    info_error=0;poll_error=-NV_EIO;assert(app_window_pump(&w)==-NV_EIO);
    poll_error=-NV_ENOENT;assert(!app_window_pump(&w) && w.closed);
    puts("PASS session window client: lock pauses input/rendering, clears full/partial queues, resumes configure/key events, preserves real errors");
}
static void line_test(void) {
    char line[32];source="hello\n";source_at=0;poll_error=0;lock_on_read=true;
    assert(read_line(line,sizeof(line))==5 && !strcmp(line,"hello") && sleeps==1);
    poll_error=-NV_EACCESS;assert(read_line(line,sizeof(line))==-NV_EACCESS && sleeps==1);
    session_locked=true;info_error=-NV_EIO;
    assert(read_line(line,sizeof(line))==-NV_EACCESS && sleeps==1);
    info_error=0;session_locked=false;poll_error=-NV_EIO;
    assert(read_line(line,sizeof(line))==-NV_EIO);
    puts("PASS session line reader: sleeps across lock, preserves partial input, resumes after unlock, preserves unrelated access/I/O errors");
}
int main(int argc,char **argv) {
    if (argc==1 || !strcmp(argv[1],"window")) window_test();
    if (argc==1 || !strcmp(argv[1],"line")) line_test();
    return 0;
}
