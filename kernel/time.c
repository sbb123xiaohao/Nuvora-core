#include "kernel.h"
/* The PC RTC is UTC. Snapshot twice around update-in-progress; firmware with
 * no valid RTC starts at the Unix epoch and still advances monotonically. */
static bool rtc_initialized;
static u64 rtc_origin,rtc_start;
struct rtc_sample {u8 second,minute,hour,day,month,year,century,mode;};
static u8 rtc_read(u8 index) {outb(0x70,index);return inb(0x71);}
static bool rtc_sample(struct rtc_sample *s) {
    u32 budget=10000;while((rtc_read(0x0a)&0x80) && --budget) {}
    if(!budget)return false;
    *s=(struct rtc_sample){rtc_read(0),rtc_read(2),rtc_read(4),rtc_read(7),rtc_read(8),rtc_read(9),rtc_read(0x32),rtc_read(0x0b)};
    return !(rtc_read(0x0a)&0x80);
}
static u32 rtc_number(u8 value,bool binary) {
    if(binary)return value;
    return (value&15)>9 || (value>>4)>9?~0u:((u32)value>>4)*10u+(value&15u);
}
static bool leap(u32 year) {return !(year%4) && (year%100 || !(year%400));}
static bool rtc_epoch(struct rtc_sample s,u64 *seconds) {
    bool binary=s.mode&4,pm=s.hour&0x80;
    u32 sec=rtc_number(s.second,binary),min=rtc_number(s.minute,binary),hour=rtc_number(s.hour&0x7f,binary);
    u32 day=rtc_number(s.day,binary),month=rtc_number(s.month,binary),year=rtc_number(s.year,binary),century=rtc_number(s.century,binary);
    if(!(s.mode&2)) {if(hour<1 || hour>12)return false;hour=hour%12+(pm?12:0);}
    else if(pm)return false;
    if(year>99)return false;
    year+=(century>=19 && century<=99)?century*100:year>=70?1900:2000;
    static const u8 days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(year<1970 || sec>59 || min>59 || hour>23 || !month || month>12 ||
       !day || day>(u32)days[month-1]+(month==2 && leap(year)))return false;
    u64 count=0;for(u32 y=1970;y<year;++y)count+=365+leap(y);
    for(u32 m=1;m<month;++m)count+=days[m-1]+(m==2 && leap(year));
    *seconds=((count+day-1)*24+hour)*3600+min*60+sec;return true;
}
u64 clock_realtime_ticks(void) {
    if(!rtc_initialized) {
        struct rtc_sample a,b;u64 seconds=0;
        if(rtc_read(0x0d)&0x80)for(u32 attempt=0;attempt<4;++attempt)
            if(rtc_sample(&a) && rtc_sample(&b) && !memcmp(&a,&b,sizeof(a)) && rtc_epoch(a,&seconds))break;
        rtc_origin=seconds*100;rtc_start=ticks;rtc_initialized=true;
    }
    return rtc_origin+(ticks-rtc_start);
}
