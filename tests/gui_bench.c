#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../user/gui_text.h"

/* Build with the same scalar, freestanding optimization flags as native apps.
 * Measure the public renderer, without a second implementation in the benchmark. */
static u64 nanoseconds(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) abort();
    return (u64)t.tv_sec*1000000000ull+(u64)t.tv_nsec;
}
static __attribute__((noinline)) void scene(struct nv_canvas *c,u32 kind) {
    struct desktop_clip clip=kind==2?(struct desktop_clip){613,317,621,327}:
        (struct desktop_clip){0,0,1024,768};
    desktop_round_alpha(c,clip,12,8,1000,750,kind==3?0:14,0x315a87,
        kind==1?220:255);
}
static u64 median(u64 values[7]) {
    for (u32 i=0;i<7;++i) for (u32 j=i+1;j<7;++j)
        if (values[j]<values[i]) { u64 t=values[i];values[i]=values[j];values[j]=t; }
    return values[3];
}
int main(void) {
    const char *names[]={"opaque-card","translucent-card","cursor-clip","square-card"};
    u32 *pixels=malloc(1024*768*sizeof(u32));
    if (!pixels) return 1;
    struct nv_canvas c={pixels,1024,0,768,NV_DISPLAY_BGRX8};
    puts("scene,ns_per_draw,checksum");
    for (u32 kind=0;kind<4;++kind) {
        u32 loops=kind==2?100000:64;
        u64 samples[7];
        for (u32 sample=0;sample<7;++sample) {
            for (u32 i=0;i<1024*768;++i) pixels[i]=0x243951;
            u64 start=nanoseconds();
            for (u32 i=0;i<loops;++i) scene(&c,kind);
            samples[sample]=(nanoseconds()-start)/loops;
        }
        u64 checksum=0;
        for (u32 i=0;i<1024*768;++i) checksum=checksum*33+pixels[i];
        printf("%s,%llu,%016llx\n",names[kind],(unsigned long long)median(samples),(unsigned long long)checksum);
    }
    free(pixels);return 0;
}
