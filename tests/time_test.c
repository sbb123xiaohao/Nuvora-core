#include <assert.h>
#include <stdio.h>
#include "../kernel/kernel.h"
volatile u64 ticks;
static u8 registers[128],selected;
static void fixture_outb(u16 port,u8 value){assert(port==0x70);selected=value;}
static u8 fixture_inb(u16 port){assert(port==0x71);return registers[selected];}
#define outb fixture_outb
#define inb fixture_inb
#include "../kernel/time.c"
int main(void) {
    u64 seconds;
    assert(rtc_epoch((struct rtc_sample){.day=1,.month=1,.year=0x70,.century=0x19,.mode=2},&seconds) && !seconds);
    assert(rtc_epoch((struct rtc_sample){.day=0x29,.month=2,.year=0,.century=0x20,.mode=2},&seconds) && seconds==951782400ull);
    assert(!rtc_epoch((struct rtc_sample){.day=0x29,.month=2,.year=0,.century=0x21,.mode=2},&seconds));
    assert(rtc_epoch((struct rtc_sample){.day=1,.month=1,.year=0x70,.century=0x19,.hour=0x92},&seconds) && seconds==43200);
    assert(rtc_epoch((struct rtc_sample){.day=1,.month=1,.year=0x70,.century=0x19,.hour=0x12},&seconds) && !seconds);
    assert(!rtc_epoch((struct rtc_sample){.day=1,.month=0x1a,.year=0x26,.century=0x20,.mode=2},&seconds));
    assert(rtc_epoch((struct rtc_sample){.second=59,.minute=58,.hour=23,.day=31,.month=12,.year=99,.century=20,.mode=6},&seconds) && seconds==4102444739ull);
    registers[0x0d]=0x80;registers[7]=1;registers[8]=1;registers[9]=0x70;registers[0x32]=0x19;registers[0x0b]=2;
    ticks=250;assert(!clock_realtime_ticks());ticks=499;assert(clock_realtime_ticks()==249);
    rtc_initialized=false;registers[0x0a]=0x80;ticks=500;assert(!clock_realtime_ticks());
    ticks=503;assert(clock_realtime_ticks()==3);
    puts("PASS RTC clocks: BCD/binary, 12/24-hour, Gregorian leap/century, malformed dates, update timeout and monotonic epoch fallback");
}
