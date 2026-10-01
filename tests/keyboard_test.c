/* Exercise the real input decoder, with only the port read substituted. */
#include <assert.h>
#include <stdio.h>
#include "../kernel/kernel.h"
static u8 scancode;
static u8 fixture_inb(u16 port) { assert(port==0x60); return scancode; }
#define inb fixture_inb
#include "../kernel/console.c"

static void ps2(u8 code) { scancode=code; keyboard_irq(); }
static u32 next(void) { int r=raw_key(); assert(r>=0); return (u32)r; }
static void modifiers(u32 flags) { assert(next()==(NV_KEY_MODIFIERS|NV_KEY_DIRECT|flags)); }
static void keypad_operators(void) {
    const u8 scancodes[]={0x4a,0x4e,0x37,0x35};
    const u8 usages[]={0x56,0x57,0x55,0x54};
    const char expected[]="-+*/";
    for (u32 locked=0;locked<2;++locked) {
        if (numlock!=(locked!=0)) ps2(0x45);
        assert(raw_key()==-NV_EAGAIN);
        for (u32 shift=0;shift<2;++shift) {
            u32 flags=shift?NV_KEY_SHIFT:0;
            if (shift) { ps2(0x2a); modifiers(flags); }
            for (u32 i=0;i<ARRAY_LEN(scancodes);++i) {
                /* Keypad divide has an E0 prefix; the other operators do not. */
                if (i==3) ps2(0xe0);
                ps2(scancodes[i]);
                assert(next()==((u32)expected[i]|NV_KEY_DIRECT|flags));
                if (i==3) ps2(0xe0);
                ps2(scancodes[i]|0x80);
                assert(raw_key()==-NV_EAGAIN); /* Releases must not type twice. */
                console_usb_key(usages[i],0);
                assert(next()==((u32)expected[i]|NV_KEY_DIRECT|flags));
            }
            ps2(0x4f); /* Num Lock and Shift still select digits or navigation. */
            assert(next()==(((locked!=shift)?'1':NV_KEY_END)|NV_KEY_DIRECT|flags));
            ps2(0xcf);
            if (shift) { ps2(0xaa); modifiers(0); }
        }
    }
}
int main(void) {
    keypad_operators();
    ps2(0xe0); ps2(0x5b); modifiers(NV_KEY_META);
    ps2(0xe0); ps2(0x4b); assert(next()==(NV_KEY_LEFT|NV_KEY_META|NV_KEY_DIRECT));
    ps2(0xe0); ps2(0xdb); modifiers(0);
    ps2(0x38); modifiers(NV_KEY_ALT);
    ps2(0x0f); assert(next()==('\t'|NV_KEY_ALT|NV_KEY_DIRECT));
    ps2(0x2a); modifiers(NV_KEY_ALT|NV_KEY_SHIFT);
    ps2(0x0f); assert(next()==('\t'|NV_KEY_ALT|NV_KEY_SHIFT|NV_KEY_DIRECT));
    ps2(0xaa); modifiers(NV_KEY_ALT); ps2(0xb8); modifiers(0);
    console_usb_modifiers(0,0x08); modifiers(NV_KEY_META);
    console_usb_key(0x50,0x08); assert(next()==(NV_KEY_LEFT|NV_KEY_META|NV_KEY_DIRECT));
    console_usb_modifiers(0,0x80); assert(raw_key()==-NV_EAGAIN); /* Second keyboard. */
    console_usb_modifiers(0x08,0); assert(raw_key()==-NV_EAGAIN);
    console_usb_modifiers(0x80,0); modifiers(0);
    console_usb_modifiers(0,0x04); modifiers(NV_KEY_ALT);
    ps2(0x38); assert(raw_key()==-NV_EAGAIN);
    console_usb_modifiers(0x04,0); assert(raw_key()==-NV_EAGAIN);
    ps2(0xb8); modifiers(0);
    console_usb_modifiers(0,0x02); modifiers(NV_KEY_SHIFT);
    ps2(0x1e); assert(next()==('A'|NV_KEY_SHIFT|NV_KEY_DIRECT));
    console_usb_modifiers(0x02,0); modifiers(0);
    for (u32 i=0;i<255;++i) queue_key('x');
    console_usb_modifiers(0,0x40);
    console_usb_modifiers(0x40,0);
    u32 last=0; while (input_head!=input_tail) last=next();
    assert(last==(NV_KEY_MODIFIERS|NV_KEY_DIRECT)); /* Release survives overflow. */
    queue_key(NV_KEY_MODIFIERS|NV_KEY_DIRECT); queue_key('x'|NV_KEY_META|NV_KEY_DIRECT);
    queue_key('y'|NV_KEY_DIRECT); assert(console_getc()=='y');
    puts("PASS keyboard: PS/2 and USB keypad operators, Num Lock/Shift, Super, modifier release, multiple devices and overflow recovery");
    return 0;
}
