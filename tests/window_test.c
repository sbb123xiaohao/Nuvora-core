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
static void *kmalloc(usize n) { return malloc(n); }
static void kfree(void *p) { free(p); }
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
int main(void) {
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
    window_task_release(2); assert(!allocations && !window_server && !window_find(terminal));
    use(1); assert(io(NV_WINDOW_POLL,&q,sizeof(q))==-NV_ENODEV);
    munmap(user,2*NV_DISPLAY_MAX_COPY);
    puts("window: complete-frame publication, fragmented pages, stale snapshots/configures, allocation rollback, access control, input ordering, inherited terminal stdio and cleanup passed");
    return 0;
}
