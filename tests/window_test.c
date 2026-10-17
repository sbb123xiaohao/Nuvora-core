#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE NV_PAGE
struct task { u32 pid, parent, state; void *pd; };
static struct task tasks[NV_TASK_MAX];
static struct task *current;
static u8 *user;
static u32 allocations, fail_after = ~0u;
static bool console_owned(u32 pid) { return pid == 2; }
static bool interactive=true;
static bool account_interactive_allowed(const struct task *t) { (void)t;return interactive; }
static bool account_session_allowed(const struct task *t) { (void)t;return interactive; }
static u32 heap_allocations, heap_fail_after=~0u;
static void *kmalloc(usize n) {
    if (!heap_fail_after) return NULL;
    --heap_fail_after;
    void *p=malloc(n); if (p) ++heap_allocations; return p;
}
static void kfree(void *p) { if (p) { assert(heap_allocations); --heap_allocations; free(p); } }
static uptr page_alloc(void) {
    if (!fail_after) return 0;
    --fail_after;
    void *p = calloc(1, PAGE);
    assert(p); ++allocations; return (uptr)p;
}
static void page_free(uptr p) { assert(p && allocations); --allocations; free((void *)p); }
static void *phys_ptr(uptr p) { return (void *)p; }
static bool user_range(void *pd, uptr p, usize n, bool write) {
    (void)pd; (void)write;
    return p >= (uptr)user && (u64)p+n <= (uptr)user + 2*NV_DISPLAY_MAX_COPY;
}
#include "../kernel/window.c"
static int io(u32 op, const void *p, u32 size) {
    memcpy(user, p, size);
    return window_ioctl(op, (uptr)user);
}
static void use(u32 index) { current = &tasks[index]; }
static u32 create(const char *title, u32 w, u32 h) {
    struct nv_window_create q = {.width = w, .height = h};
    strlcpy(q.title, title, sizeof(q.title));
    assert(io(NV_WINDOW_CREATE,&q,sizeof(q))==0);
    return ((struct nv_window_create *)user)->id;
}
static void frame(u32 id, u32 serial, u32 width, u32 height, u32 value) {
    struct nv_window_frame b={id,serial,width,height};
    assert(io(NV_WINDOW_BEGIN,&b,sizeof(b))==0);
    u32 *data=(void *)(user+4096);
    for (u32 i=0;i<width*height;++i) data[i]=value+i;
    struct nv_window_pixels p={.id=id,.width=width,.height=height,
        .stride=width*4,.pixels=(uptr)data};
    assert(io(NV_WINDOW_UPLOAD64,&p,sizeof(p))==0);
    struct nv_window_id c={id}; assert(io(NV_WINDOW_COMMIT,&c,sizeof(c))==0);
}
static void responsive_terminal(void) {
    use(1); u32 id=create("Responsive Terminal",1278,715);
    struct nv_window_terminal bind={id,0};
    assert(io(NV_WINDOW_BIND_TERMINAL,&bind,sizeof(bind))==-NV_EINVAL);
    bind.scale=2;
    heap_fail_after=0;
    assert(io(NV_WINDOW_BIND_TERMINAL,&bind,sizeof(bind))==-NV_ENOMEM && !heap_allocations);
    heap_fail_after=1;
    assert(io(NV_WINDOW_BIND_TERMINAL,&bind,sizeof(bind))==-NV_ENOMEM && !heap_allocations);
    heap_fail_after=~0u;
    assert(io(NV_WINDOW_BIND_TERMINAL,&bind,sizeof(bind))==0);
    assert(io(NV_WINDOW_BIND_TERMINAL,&bind,sizeof(bind))==-NV_EBUSY);
    struct native_window *w=window_find(id); struct window_terminal *t=w->terminal;
    assert(t->columns==104 && t->rows==28);
    assert(window_stdio_write(&tasks[2],"header\n",7)==7);
    char line[180]; for (u32 i=0;i<sizeof(line);++i) line[i]=(char)('a'+i%26);
    assert(window_stdio_write(&tasks[2],line,sizeof(line))==sizeof(line));
    assert(w->row==2 && w->column==76 && (t->cells[104+90]&255)==line[90]);
    struct nv_window_grid q={.id=id,.generation=w->generation};
    assert(io(NV_WINDOW_GRID_READ,&q,sizeof(q))==-NV_EACCESS);
    use(0); assert(io(NV_WINDOW_GRID_READ,&q,sizeof(q))==0);
    q=*(struct nv_window_grid *)user;
    assert(q.columns==104 && q.rows==28 && q.scale==2 && q.cursor==284);
    q.capacity=NV_TERMINAL_MAX_CELLS; q.cells=(uptr)(user+8192);
    assert(io(NV_WINDOW_GRID_READ,&q,sizeof(q))==0);
    u16 original[104*28]; memcpy(original,user+8192,sizeof(original));
    q.capacity=1; assert(io(NV_WINDOW_GRID_READ,&q,sizeof(q))==-NV_E2BIG);
    q.capacity=NV_TERMINAL_MAX_CELLS; q.cells=1;
    assert(io(NV_WINDOW_GRID_READ,&q,sizeof(q))==-NV_EFAULT);
    q.cells=(uptr)(user+8192); q.reserved=1;
    assert(io(NV_WINDOW_GRID_READ,&q,sizeof(q))==-NV_EINVAL); q.reserved=0;
    struct nv_window_configure cfg={id,638,715,3};
    u16 *previous=t->cells; u32 generation=w->generation, serial=w->serial;
    heap_fail_after=0;
    assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==-NV_ENOMEM);
    assert(w->width==1278 && w->generation==generation && w->serial==serial && t->cells==previous);
    assert(!memcmp(t->cells,original,sizeof(original))); heap_fail_after=~0u;
    assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    assert(t->columns==51 && t->rows==28 && w->row==4 && w->column==27);
    assert((t->cells[51+50]&255)==line[50] && (t->cells[102]&255)==line[51]);
    assert(io(NV_WINDOW_GRID_READ,&q,sizeof(q))==-NV_EAGAIN);
    cfg.width=1278; assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    assert(!memcmp(t->cells,original,sizeof(original)) && w->row==2 && w->column==76);
    struct nv_window_text legacy={.id=id,.generation=w->generation};
    assert(io(NV_WINDOW_TEXT_READ,&legacy,sizeof(legacy))==0);
    assert((((struct nv_window_text *)user)->surface.cells[80+50]&255)==line[50]);
    assert(window_stdio_clear(&tasks[2])==0);
    for (u32 i=0;i<104;++i) text_character(w,'x');
    assert(w->row==0 && t->pending_wrap);
    text_character(w,'\n'); assert(w->row==1 && !w->column); /* No extra blank line. */
    assert(window_stdio_clear(&tasks[2])==0);
    for (u32 i=0;i<105;++i) text_character(w,'a');
    const char *erase="\b \b\b \b";
    window_stdio_write(&tasks[2],erase,6);
    assert(w->row==0 && w->column==103 && (t->cells[103]&255)==' ' && (t->cells[104]&255)==' ');
    text_character(w,'Z'); cfg.width=638; assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    cfg.width=1278; assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    assert((t->cells[103]&255)=='Z' && w->row==0 && t->pending_wrap);
    for (u32 i=0;i<100;++i) window_stdio_write(&tasks[2],"\nrow",4);
    assert(w->row==t->rows-1 && w->column==3 && (t->cells[0]&255)=='r');
    assert(window_stdio_clear(&tasks[2])==0);
    for (u32 i=0;i<TERMINAL_HISTORY+17;++i) text_character(w,'q');
    assert(t->used<=TERMINAL_HISTORY);
    cfg.width=1916; cfg.height=910; assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    assert(t->columns==157 && t->rows==36 && (t->cells[(t->rows-1)*t->columns]&255)=='q');
    assert(window_stdio_clear(&tasks[2])==0);
    cfg.width=1;cfg.height=1;assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    assert(t->columns==1 && t->rows==1 && t->cells[0]==0x0720 && !t->used);
    window_stdio_write(&tasks[2],"xy\b \bZ\n",7);
    assert(w->row<t->rows && w->column<t->columns);
    cfg.width=8192;cfg.height=8192;assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    assert(t->columns*t->rows<=NV_TERMINAL_MAX_CELLS);
    assert(window_stdio_clear(&tasks[2])==0);
    cfg.width=1278;cfg.height=715;assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    for (u32 i=0;i<t->columns*t->rows;++i) assert(t->cells[i]==0x0720);
    struct nv_window_id destroy={id};use(1);assert(io(NV_WINDOW_DESTROY,&destroy,sizeof(destroy))==0);
    assert(!heap_allocations);
}
int main(int argc,char **argv) {
    user=mmap(NULL,2*NV_DISPLAY_MAX_COPY,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(user!=MAP_FAILED && (uptr)user>0xffffffffull);
    tasks[0]=(struct task){.pid=2,.state=NV_READY};
    tasks[1]=(struct task){.pid=3,.parent=2,.state=NV_READY};
    tasks[2]=(struct task){.pid=4,.parent=2,.state=NV_READY};
    use(1);
    assert(window_ioctl(NV_WINDOW_SERVER_ACQUIRE,0)==-NV_EACCESS);
    assert(window_ioctl(NV_WINDOW_INFO,0)==-NV_EFAULT);
    struct nv_window_create absent={.width=32,.height=10};
    assert(io(NV_WINDOW_CREATE,&absent,sizeof(absent))==-NV_ENODEV);
    use(0);interactive=false;assert(window_ioctl(NV_WINDOW_SERVER_ACQUIRE,0)==-NV_EACCESS);
    interactive=true;assert(window_ioctl(NV_WINDOW_SERVER_ACQUIRE,0)==0);
    use(1); u32 id=create("Media",800,4);
    struct nv_window_frame b={id,1,800,4};
    assert(io(NV_WINDOW_BEGIN,&b,sizeof(b))==0);
    u32 *data=(void *)(user+4096);
    for (u32 i=0;i<3200;++i) data[i]=0x120000u+i;
    struct nv_window_pixels p={.id=id,.width=800,.height=2,.stride=3200,.pixels=(uptr)data};
    assert(io(NV_WINDOW_UPLOAD64,&p,sizeof(p))==0);
    struct nv_window_id c={id}; assert(io(NV_WINDOW_COMMIT,&c,sizeof(c))==-NV_EINVAL);
    use(0);
    p.generation=0; assert(io(NV_WINDOW_READ64,&p,sizeof(p))==-NV_EINVAL);
    use(1); p.y=2; p.pixels=(uptr)(data+1600);
    assert(io(NV_WINDOW_UPLOAD64,&p,sizeof(p))==0);
    assert(io(NV_WINDOW_COMMIT,&c,sizeof(c))==0);
    use(2); assert(io(NV_WINDOW_BEGIN,&b,sizeof(b))==-NV_EACCESS);
    assert(io(NV_WINDOW_DESTROY,&c,sizeof(c))==-NV_EACCESS);
    p.generation=1; assert(io(NV_WINDOW_READ64,&p,sizeof(p))==-NV_EACCESS);
    use(0); p.y=0; p.height=4; p.pixels=(uptr)(user+65536);
    assert(io(NV_WINDOW_READ64,&p,sizeof(p))==0);
    u32 *out=(void *)(user+65536);
    for (u32 i=0;i<3200;++i) assert(out[i]==0x120000u+i);
    p.x=0xffffffffu; assert(io(NV_WINDOW_READ64,&p,sizeof(p))==-NV_EINVAL); p.x=0;
    p.stride=0xffffffffu; assert(io(NV_WINDOW_READ64,&p,sizeof(p))==-NV_EFAULT); p.stride=3200;
    p.pixels=0; assert(io(NV_WINDOW_READ64,&p,sizeof(p))==-NV_EFAULT);
    use(1); frame(id,1,800,4,0x440000);
    use(0); p.pixels=(uptr)out;
    assert(io(NV_WINDOW_READ64,&p,sizeof(p))==-NV_EAGAIN);
    struct nv_window_configure cfg={id,800,4,NV_WINDOW_VISIBLE|NV_WINDOW_FOCUSED};
    assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    struct nv_window_event e={.id=id,.type=NV_WINDOW_EVENT_KEY,.key='7'|NV_KEY_DIRECT};
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    use(1); struct nv_window_event q={.id=id};
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1);
    assert(((struct nv_window_event *)user)->type==NV_WINDOW_EVENT_FOCUS);
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1 && ((struct nv_window_event *)user)->key==e.key);
    /* A configure invalidates an in-flight old-size frame without changing front. */
    assert(io(NV_WINDOW_BEGIN,&b,sizeof(b))==0);
    use(0); cfg.width=801;
    assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    use(1); assert(io(NV_WINDOW_COMMIT,&c,sizeof(c))==-NV_EAGAIN);
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1);
    assert(((struct nv_window_event *)user)->serial==2);
    b.serial=2; b.width=801;
    u32 before=allocations; fail_after=1;
    assert(io(NV_WINDOW_BEGIN,&b,sizeof(b))==-NV_ENOMEM && allocations==before);
    fail_after=~0u; frame(id,2,801,4,0x770000);
    /* Input pressure cannot discard close/configure, and motion coalescing
     * preserves press/release transitions. */
    use(0); e=(struct nv_window_event){.id=id,.type=NV_WINDOW_EVENT_POINTER,.x=10};
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0); e.x=20;
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0); e.buttons=1;
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0); e.buttons=0;
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    use(1); assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1 && ((struct nv_window_event *)user)->x==20);
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1 && ((struct nv_window_event *)user)->buttons==1);
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1 && ((struct nv_window_event *)user)->buttons==0);
    use(0); e.type=NV_WINDOW_EVENT_KEY;
    for (u32 i=0;i<32;++i) assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==-NV_EAGAIN);
    e.type=NV_WINDOW_EVENT_CLOSE; assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    use(1); assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1 && ((struct nv_window_event *)user)->type==NV_WINDOW_EVENT_CLOSE);
    window_task_release(3); assert(!allocations);
    u32 terminal=create("Terminal",640,350); c.id=terminal;
    assert(io(NV_WINDOW_BIND_STDIO,&c,sizeof(c))==0);
    tasks[2].parent=3;
    const char *sample="0123456789\nchild output";
    assert(window_stdio_write(&tasks[2],sample,strlen(sample))==(int)strlen(sample));
    struct native_window *tw=window_find(terminal);
    assert((tw->text.cells[9]&255)=='9' && (tw->text.cells[80]&255)=='c');
    assert(window_owned(3) && !window_owned(4));
    assert(window_stdio_clear(&tasks[2])==0 && tw->row==0 && tw->column==0 &&
           tw->text.cells[9]==0x0720 && tw->text.cursor==0);
    use(0); cfg=(struct nv_window_configure){terminal,640,350,3};
    assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    e=(struct nv_window_event){.id=terminal,.type=NV_WINDOW_EVENT_KEY,.key='c'|NV_KEY_CTRL};
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    u8 ch; assert(window_stdio_read(&tasks[2],&ch,1)==1 && ch==3);
    assert(window_stdio_read(&tasks[2],&ch,1)==-NV_EAGAIN);
    e.key=NV_KEY_MODIFIERS|NV_KEY_DIRECT; assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    e.key='x'|NV_KEY_META|NV_KEY_DIRECT; assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    e.key='y'|NV_KEY_DIRECT; assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    assert(window_stdio_read(&tasks[2],&ch,1)==1 && ch=='y');
    cfg.flags=NV_WINDOW_VISIBLE; assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==-NV_EACCESS);
    cfg.flags=NV_WINDOW_VISIBLE|NV_WINDOW_FOCUSED;
    assert(io(NV_WINDOW_CONFIGURE,&cfg,sizeof(cfg))==0);
    e.key='z'|NV_KEY_DIRECT;assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    interactive=false;
    if (argc==1 || !strcmp(argv[1],"locked-send")) {
        u32 queued=tw->queued;
        assert(io(NV_WINDOW_SEND,&e,sizeof(e))==-NV_EACCESS && tw->queued==queued);
    }
    if (argc==1 || !strcmp(argv[1],"locked-stdio")) {
        ch=0xa9;u32 queued=tw->queued;
        assert(window_stdio_read(&tasks[2],&ch,1)==-NV_EACCESS && ch==0xa9 && tw->queued==queued);
        assert(window_stdio_read(&tasks[2],&ch,0)==-NV_EACCESS && tw->queued==queued);
    }
    if (argc==1 || !strcmp(argv[1],"locked-window")) {
        use(1);q.id=terminal;u32 queued=tw->queued;struct nv_window_event before=q;
        assert(io(NV_WINDOW_POLL,&q,sizeof(q))==-NV_EACCESS && tw->queued==queued);
        assert(!memcmp(user,&before,sizeof(before)));
    }
    interactive=true;use(0);
    assert(window_stdio_read(&tasks[2],&ch,1)==1 && ch=='z');
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    e=(struct nv_window_event){.id=terminal,.type=NV_WINDOW_EVENT_POINTER,.x=12,.y=20};
    assert(io(NV_WINDOW_SEND,&e,sizeof(e))==0);
    tw->close_pending=tw->configure_pending=tw->focus_pending=true;
    u32 generation=tw->generation,serial=tw->serial;
    window_session_lock();
    assert(!tw->queued && !tw->head && !tw->tail && tw->generation==generation && tw->serial==serial);
    for (u32 i=0;i<sizeof(tw->events);++i) assert(!((u8 *)tw->events)[i]);
    assert(tw->close_pending && tw->configure_pending && tw->focus_pending);
    use(1);q.id=terminal;assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1);
    assert(((struct nv_window_event *)user)->type==NV_WINDOW_EVENT_CLOSE);
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1 && ((struct nv_window_event *)user)->type==NV_WINDOW_EVENT_CONFIGURE);
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==1 && ((struct nv_window_event *)user)->type==NV_WINDOW_EVENT_FOCUS);
    assert(io(NV_WINDOW_POLL,&q,sizeof(q))==0);
    c.id=terminal;assert(io(NV_WINDOW_DESTROY,&c,sizeof(c))==0);
    responsive_terminal();
    window_task_release(2); assert(!allocations && !window_server && !window_find(terminal));
    assert(!heap_allocations);
    use(1); assert(io(NV_WINDOW_POLL,&q,sizeof(q))==-NV_ENODEV);
    munmap(user,2*NV_DISPLAY_MAX_COPY);
    puts("window: complete frames, fragmented pages, stale snapshots, OOM rollback, access control, locked input, inherited stdio, responsive text reflow/erase/scroll, legacy projection and cleanup passed");
    return 0;
}
