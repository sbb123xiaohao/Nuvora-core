#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../kernel/kernel.h"
static u8 *user;
static u32 pixels[16*16+2];
static bool session_locked;
static u32 clears;
static int terminal_clear=-NV_ENODEV;
struct task *current;
bool test_mode;
volatile u64 ticks;
const struct boot_info *boot_info;
bool account_interactive_allowed(const struct task *t) {
    return t && (t->pid==2 || (!session_locked && t->uid!=NV_UID_NONE));
}
bool account_manager(const struct task *t) { return t && t->pid==2; }
void account_input_activity(void) {}
bool user_range(pte_t *pd,uptr p,usize n,bool write) {
    (void)pd;(void)write;
    return p>=(uptr)user && p-(uptr)user<=PAGE && n<=PAGE-(p-(uptr)user);
}
int window_stdio_clear(struct task *t) { (void)t;++clears;return terminal_clear; }
u32 usb_pointer_count(void) { return 1; }
static char serial_log[256];
static u32 serial_length;
static void fixture_outb(u16 p,u8 v) {
    if(p==0x3f8 && serial_length+1<sizeof(serial_log))serial_log[serial_length++]=(char)v;
}
static u8 fixture_inb(u16 p) { (void)p;return 0x20; }
#define outb fixture_outb
#define inb fixture_inb
#undef FB_WINDOW
#define FB_WINDOW ((uptr)(pixels+1))
#include "../kernel/pointer.c"
#include "../kernel/console.c"
static void reset(void) {
    memset(pixels,0,sizeof(pixels));pixels[0]=0x17abcd31;pixels[ARRAY_LEN(pixels)-1]=0x93ef8214;
    fb_mode=fb_ready=true;vga_present=false;fb_width=fb_height=16;fb_pitch=16*4;fb_format=NV_FB_RGBX8;
    fb_scale=1;fb_cellw=8;fb_cellh=16;fb_cursor_cell=2000;
    screen_owner=current->pid;graphics_owner=true;input_head=input_tail=0;
    session_locked=false;clears=0;terminal_clear=-NV_ENODEV;pointer_reset();
}
static void guards(void) {
    assert(pixels[0]==0x17abcd31 && pixels[ARRAY_LEN(pixels)-1]==0x93ef8214);
}
int main(int argc,char **argv) {
    struct task actor={.pid=3,.uid=1000};current=&actor;
    user=mmap(NULL,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(user!=MAP_FAILED);reset();
    const char *mode=argc>1?argv[1]:"all";
    if (!strcmp(mode,"format") || argc==1) {
        serial_ok=true;fb_quiet=true;serial_length=0;
        kprintf("%lld %lld %lld %d",(i64)0x1000000000ull,
                (i64)0x8000000000000000ull,(i64)0x7fffffffffffffffull,(i32)-2147483647-1);
        serial_log[serial_length]=0;
        assert(!strcmp(serial_log,"68719476736 -9223372036854775808 9223372036854775807 -2147483648"));
        serial_ok=false;fb_quiet=false;
    }
    if (!strcmp(mode,"raw-key") || argc==1) {
        queue_key('q'|NV_KEY_DIRECT);session_locked=true;u32 tail=input_tail;
        assert(console_key(3)==-NV_EACCESS && input_tail==tail);
        session_locked=false;assert(console_key(3)==('q'|NV_KEY_DIRECT));
    }
    if (!strcmp(mode,"pointer") || argc==1) {
        reset();pointer_push(3,4,NV_POINTER_LEFT);session_locked=true;
        memset(user,0xa9,sizeof(struct nv_pointer_event));
        assert(input_ioctl(NV_INPUT_POINTER_POLL,(uptr)user)==-NV_EACCESS);
        for (u32 i=0;i<sizeof(struct nv_pointer_event);++i) assert(user[i]==0xa9);
        session_locked=false;assert(input_ioctl(NV_INPUT_POINTER_POLL,(uptr)user)==1);
        struct nv_pointer_event *e=(void *)user;assert(e->dx==3 && e->dy==4 && e->buttons==NV_POINTER_LEFT);
    }
    if (!strcmp(mode,"display") || argc==1) {
        reset();u32 source[4]={0xabc001,0xabc002,0xabc003,0xabc004};memcpy(user+128,source,sizeof(source));
        struct nv_display_present r={.width=2,.height=2,.stride=8,.pixels=(uptr)(user+128)};
        session_locked=true;u32 before[ARRAY_LEN(pixels)];memcpy(before,pixels,sizeof(pixels));
        assert(console_display_acquire(3)==-NV_EACCESS && screen_owner==3);
        assert(console_display_present(3,&r)==-NV_EACCESS && !memcmp(before,pixels,sizeof(pixels)));
        session_locked=false;assert(console_display_present(3,&r)==0);
        assert(pixels[1]==source[0] && pixels[2]==source[1] && pixels[17]==source[2]);guards();
    }
    if (!strcmp(mode,"surface") || argc==1) {
        reset();graphics_owner=false;struct nv_surface *s=(void *)user;
        for (u32 i=0;i<2000;++i) s->cells[i]=0x0741;
        s->cursor=0;
        session_locked=true;u32 before[ARRAY_LEN(pixels)];memcpy(before,pixels,sizeof(pixels));
        assert(console_surface(3,NV_SCREEN_PRESENT,s)==-NV_EACCESS && !memcmp(before,pixels,sizeof(pixels)));
        session_locked=false;assert(console_surface(3,NV_SCREEN_PRESENT,s)==0);guards();
    }
    if (!strcmp(mode,"clear") || argc==1) {
        reset();screen_owner=0;session_locked=true;
        screen[0]=0x0741;u32 before[ARRAY_LEN(pixels)];memcpy(before,pixels,sizeof(pixels));
        assert(console_clear_for(current)==-NV_EACCESS && !clears && screen[0]==0x0741);
        terminal_clear=0;
        assert(console_clear_for(current)==-NV_EACCESS && !clears && !memcmp(before,pixels,sizeof(pixels)));
        session_locked=false;assert(console_clear_for(current)==0 && clears==1);
        terminal_clear=-NV_ENODEV;screen_owner=3;
        assert(console_clear_for(current)==-NV_EBUSY && screen[0]==0x0741);
        screen_owner=0;assert(console_clear_for(current)==0 && screen[0]==0x0720);guards();
    }
    if (!strcmp(mode,"revoke") || argc==1) {
        for (u32 graphics=0;graphics<2;++graphics) {
            reset();graphics_owner=graphics;queue_key('x'|NV_KEY_DIRECT);pointer_push(8,9,NV_POINTER_LEFT);
            session_locked=true;console_session_lock(2);
            assert(!screen_owner && !graphics_owner && !input_head && !input_tail);
            for (u32 i=0;i<ARRAY_LEN(input);++i) assert(!input[i]);
            struct nv_pointer_event e;assert(pointer_next(&e)==0);
            assert(console_key(3)==-NV_EACCESS && console_display_acquire(3)==-NV_EACCESS);
            assert(console_surface(3,NV_SCREEN_ACQUIRE,NULL)==-NV_EACCESS);
            /* Recovery login must be able to receive fresh input after revocation. */
            queue_key('p'|NV_KEY_DIRECT);actor.pid=2;
            assert(console_getc()=='p');guards();actor.pid=3;
        }
        reset();screen_owner=2;queue_key('x'|NV_KEY_DIRECT);pointer_push(8,9,NV_POINTER_LEFT);
        session_locked=true;console_session_lock(2);
        assert(screen_owner==2 && graphics_owner && !input_head && !input_tail);
        actor.pid=2;queue_key('n'|NV_KEY_DIRECT);assert(console_key(2)==('n'|NV_KEY_DIRECT));
        pointer_push(2,3,0);assert(input_ioctl(NV_INPUT_POINTER_POLL,(uptr)user)==1);
        console_release(2);assert(!screen_owner);actor.pid=3;guards();
        reset();graphics_owner=false;session_locked=true;
        assert(console_surface(3,NV_SCREEN_RELEASE,NULL)==0 && !screen_owner);guards();
    }
    munmap(user,PAGE);
    puts("PASS console security: locked reads/presents/clear denied without mutation; application ownership revoked, old input flushed, trusted graphical/recovery login and release preserved");
}
