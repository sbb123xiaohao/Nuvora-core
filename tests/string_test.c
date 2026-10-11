#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <nv/string.h>
static u32 state=0x7154a123;
static u32 random_word(void) {
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
static void reference_move(u8 *d,const u8 *s,usize n) {
    u8 *temporary=malloc(n?n:1);assert(temporary);
    for (usize i=0;i<n;++i) temporary[i]=s[i];
    for (usize i=0;i<n;++i) d[i]=temporary[i];
    free(temporary);
}
static void randomized(void) {
    u8 actual[2048],expected[2048];
    for (u32 trial=0;trial<12000;++trial) {
        for (u32 i=0;i<sizeof(actual);++i) actual[i]=expected[i]=(u8)random_word();
        u32 n=random_word()%1025,from=random_word()%(sizeof(actual)-n+1);
        u32 to=random_word()%(sizeof(actual)-n+1);
        assert(memmove(actual+to,actual+from,n)==actual+to);
        reference_move(expected+to,expected+from,n);
        assert(!memcmp(actual,expected,sizeof(actual)));
        int value=(int)random_word();
        assert(memset(actual+to,value,n)==actual+to);
        for (u32 i=0;i<n;++i) expected[to+i]=(u8)value;
        assert(!memcmp(actual,expected,sizeof(actual)));
    }
}
static u8 *guarded(void) {
    u8 *p=mmap(NULL,3*4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(p!=MAP_FAILED && !mprotect(p+4096,4096,PROT_READ|PROT_WRITE));return p;
}
static void boundaries(void) {
    u8 *a=guarded(),*b=guarded(),*source=a+4096,*dest=b+4096;
    for (u32 i=0;i<4096;++i) source[i]=(u8)(i*37);
    for (u32 n=0;n<=4096;++n) {
        u32 offsets[]={0,4096-n};
        for (u32 i=0;i<2;++i) for (u32 j=0;j<2;++j) {
            u8 *d=dest+offsets[i];const u8 *s=source+offsets[j];
            assert(memcpy(d,s,n)==d);
            for (u32 k=0;k<n;++k) assert(d[k]==s[k]);
            assert(memset(d,0xa5,n)==d);
            for (u32 k=0;k<n;++k) assert(d[k]==0xa5);
        }
    }
    /* Adjacent overlapping ranges end at the unreadable page; each shift
       exercises both directions and all scalar-word alignments. */
    u8 expected[4096];
    for (u32 shift=1;shift<=33;++shift) {
        for (u32 i=0;i<4096;++i) dest[i]=expected[i]=(u8)(i*13);
        reference_move(expected+shift,expected,4096-shift);
        memmove(dest+shift,dest,4096-shift);assert(!memcmp(dest,expected,4096));
        reference_move(expected,expected+shift,4096-shift);
        memmove(dest,dest+shift,4096-shift);assert(!memcmp(dest,expected,4096));
    }
    assert(!munmap(a,3*4096) && !munmap(b,3*4096));
}
int main(void) {
    randomized();boundaries();
    puts("PASS scalar memory: 12000 random overlaps, every 0..4096 length at guard pages, unaligned copy/fill and 1..33 byte overlapping shifts");
    return 0;
}
