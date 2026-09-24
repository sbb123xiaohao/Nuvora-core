#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/elf.h>
#include <nv/string.h>
static u8 data[8192];
static struct nv_elf_header *h = (void *)data;
static struct nv_elf_segment *p = (void *)(data + 64);
static struct nv_elf_image image;
static void fixture(void) {
    memset(data, 0, sizeof(data));
    memcpy(h->ident, "\177ELF\2\1\1", 7);
    h->type=2; h->machine=62; h->version=1; h->entry=0x400000c0;
    h->phoff=64; h->ehsize=64; h->phentsize=56; h->phnum=2;
    p[0]=(struct nv_elf_segment){1,5,0,0x40000000,0,512,512,4096};
    p[1]=(struct nv_elf_segment){1,6,4096,0x40001000,0,10,8192,4096};
}
static int valid(void) { return nv_elf64_validate(data,sizeof(data),0x40000000,0x41000000,&image); }
int main(void) {
    fixture(); assert(valid()==0 && image.phdr==0x40000040 && image.count==2);
    fixture(); h->ident[4]=1; assert(valid()==-NV_ENOEXEC);
    fixture(); h->machine=183; assert(valid()==-NV_ENOEXEC);
    fixture(); h->type=3; assert(valid()==-NV_ENOEXEC);
    fixture(); h->phoff=~0ull; assert(valid()==-NV_ENOEXEC);
    fixture(); p[1].offset=~0ull; assert(valid()==-NV_ENOEXEC);
    fixture(); p[1].memsz=~0ull; assert(valid()==-NV_ENOEXEC);
    fixture(); p[1].filesz=9000; assert(valid()==-NV_ENOEXEC);
    fixture(); p[1].vaddr=0x40000fff; p[1].align=1; assert(valid()==-NV_ENOEXEC);
    fixture(); p[0].flags=7; assert(valid()==-NV_ENOEXEC);
    fixture(); h->entry=0x40000200; p[0].memsz=1024; assert(valid()==-NV_ENOEXEC);
    fixture(); p[1].align=3; assert(valid()==-NV_ENOEXEC);
    fixture(); p[1].vaddr++; assert(valid()==-NV_ENOEXEC);
    const u32 forbidden[]={2,3,7};
    for(u32 i=0;i<3;++i){ fixture(); p[1].type=forbidden[i]; assert(valid()==-NV_ENOEXEC); }
    fixture(); p[1].type=0x6474e551; p[1].flags=7; assert(valid()==-NV_ENOEXEC);
    fixture(); assert(valid()==0);
    u8 stack[4096] ALIGNED(16); u32 sp,raw,base=0x7ffee000;
    assert(!nv_elf64_stack("/apps/ctest","alpha 'two words' '' a\\ b",&image,stack,base,&sp,&raw));
    u64 words[64]; memcpy(words,stack+sp-base,128);
    assert(!(sp&15) && words[0]==5 && !words[6] && !words[7]);
    const char *expect[]={"/apps/ctest","alpha","two words","","a b"};
    for(u32 i=0;i<5;++i) assert(words[i+1]>=base && words[i+1]<base+4096 &&
                                !strcmp((char *)stack+words[i+1]-base,expect[i]));
    assert(!strcmp((char *)stack+raw-base,"alpha 'two words' '' a\\ b"));
    assert(nv_elf64_stack("a","'",&image,stack,base,&sp,&raw)==-NV_EINVAL);
    assert(nv_elf64_stack("a","\\",&image,stack,base,&sp,&raw)==-NV_EINVAL);
    char many[256]; memset(many,'a',sizeof(many)); many[255]=0;
    for(u32 i=1;i<255;i+=2) many[i]=' ';
    assert(nv_elf64_stack("a",many,&image,stack,base,&sp,&raw)==-NV_E2BIG);
    puts("PASS ELF64: static image bounds, overlap/W^X, entry, dynamic/TLS rejection, argc/argv/auxv");
}
