#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <nv/string.h>
/* The previous byte loops, under exactly the same freestanding scalar flags. */
static __attribute__((noinline)) void *byte_copy(void *d,const void *s,usize n) {
    u8 *to=d;const u8 *from=s;while (n--) *to++=*from++;return d;
}
static __attribute__((noinline)) void *byte_fill(void *d,int value,usize n) {
    u8 *to=d;while (n--) *to++=(u8)value;return d;
}
static __attribute__((noinline)) void *byte_move(void *d,const void *s,usize n) {
    u8 *to=d;const u8 *from=s;
    if ((uptr)to<(uptr)from) return byte_copy(d,s,n);
    while (n) { --n;to[n]=from[n]; }return d;
}
static u64 nanoseconds(void) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC,&time)) abort();
    return (u64)time.tv_sec*1000000000ull+(u64)time.tv_nsec;
}
static double run(u32 kind,bool baseline,u8 *a,u8 *b,usize n) {
    usize loops=(64u*1024u*1024u)/n;
    void *(*volatile copy)(void *,const void *,usize)=baseline?byte_copy:memcpy;
    void *(*volatile move)(void *,const void *,usize)=baseline?byte_move:memmove;
    void *(*volatile fill)(void *,int,usize)=baseline?byte_fill:memset;
    u64 start=nanoseconds();
    for (usize i=0;i<loops;++i) {
        if (kind==0) copy(a,b,n);
        else if (kind==1) fill(a,0x5a,n);
        else move(a+1,a,n);
    }
    u64 elapsed=nanoseconds()-start;
    return (double)elapsed/(double)loops;
}
static double median(double values[5]) {
    for (u32 i=0;i<5;++i) for (u32 j=i+1;j<5;++j)
        if (values[j]<values[i]) { double t=values[i];values[i]=values[j];values[j]=t; }
    return values[2];
}
int main(void) {
    const usize sizes[]={64,4096,1024*1024};
    const char *names[]={"memcpy","memset","memmove-backward"};
    u8 *a=malloc(sizes[2]+64),*b=malloc(sizes[2]+64);
    if (!a || !b) return 1;
    memset(a,0x37,sizes[2]+64);memset(b,0x91,sizes[2]+64);
    puts("operation,bytes,byte_loop_ns,scalar_word_ns,speedup");
    for (u32 kind=0;kind<3;++kind) for (u32 size=0;size<3;++size) {
        double old[5],now[5];
        for (u32 sample=0;sample<5;++sample) {
            /* Alternate order to reduce systematic cache/clock bias. */
            if (sample&1) {
                now[sample]=run(kind,false,a+3,b+5,sizes[size]);
                old[sample]=run(kind,true,a+3,b+5,sizes[size]);
            } else {
                old[sample]=run(kind,true,a+3,b+5,sizes[size]);
                now[sample]=run(kind,false,a+3,b+5,sizes[size]);
            }
        }
        double before=median(old),after=median(now);
        printf("%s,%zu,%.2f,%.2f,%.2f\n",names[kind],sizes[size],before,after,before/after);
    }
    free(a);free(b);return 0;
}
