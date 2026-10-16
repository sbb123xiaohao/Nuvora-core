#ifndef NV_LINUX_LOADER_H
#define NV_LINUX_LOADER_H
/* Static PIE only. The program's ordinary rcrt1 performs its own relocation
 * and TLS initialization. Native Nuvora ELF loading remains a separate path. */
static int linux_image(struct task *t,const u8 *data,usize len,uptr *entry,uptr *phdr) {
    if (len<sizeof(struct elf_header)) return -NV_ENOEXEC;
    const struct elf_header *h=(const void *)data;
    if (memcmp(h->ident,"\177ELF\2\1\1",7) || h->type!=3 || h->machine!=62 || h->version!=1 ||
        h->ehsize!=sizeof(*h) || h->phentsize!=sizeof(struct program_header) || !h->phnum ||
        h->phnum>32 || h->phoff>len || h->phnum>(len-h->phoff)/sizeof(struct program_header)) return -NV_ENOEXEC;
    const struct program_header *ph=(const void *)(data+h->phoff);
    *phdr=0; bool executable=false; u64 total=0;
    for (u32 i=0;i<h->phnum;++i) {
        const struct program_header *p=&ph[i];
        if (p->offset>len || p->filesz>len-p->offset || p->filesz>p->memsz ||
            p->type==3 || (p->type==ELF_STACK && (p->flags&1))) return -NV_ENOEXEC;
        if (p->type==2) {
            if (!p->filesz || p->filesz%sizeof(struct elf_dynamic) || p->filesz>65536) return -NV_ENOEXEC;
            bool end=false;
            for (u64 at=0;at<p->filesz;at+=sizeof(struct elf_dynamic)) {
                const struct elf_dynamic *d=(const void *)(data+p->offset+at);
                if (!d->tag) { end=true; break; }
                if (d->tag==1 || d->tag==22 || (d->tag==30 && (d->value&4))) return -NV_ENOEXEC;
            }
            if (!end) return -NV_ENOEXEC;
        }
        if (p->type!=1 || !p->memsz) continue;
        if ((p->flags&~7u) || (p->flags&3)==3 || p->vaddr>=NV_USER_IMAGE_END-NV_USER_IMAGE ||
            p->memsz>NV_USER_IMAGE_END-NV_USER_IMAGE-p->vaddr ||
            (p->align>1 && ((p->align&(p->align-1)) || ((p->offset^p->vaddr)&(p->align-1))))) return -NV_ENOEXEC;
        uptr first=p->vaddr&~(uptr)(PAGE-1),end=ALIGN_UP(p->vaddr+p->memsz,PAGE);
        total+=end-first; if (total>LINUX_MEMORY_MAX) return -NV_E2BIG;
        for (u32 j=0;j<i;++j) if (ph[j].type==1 && ph[j].memsz &&
            first<ALIGN_UP(ph[j].vaddr+ph[j].memsz,PAGE) && (ph[j].vaddr&~(uptr)(PAGE-1))<end) return -NV_ENOEXEC;
        if ((p->flags&1) && h->entry>=p->vaddr && h->entry-p->vaddr<p->memsz) executable=true;
        u64 table_size=(u64)h->phnum*sizeof(*ph);
        if (h->phoff>=p->offset && h->phoff-p->offset<=p->filesz && table_size<=p->filesz-(h->phoff-p->offset))
            *phdr=NV_USER_IMAGE+p->vaddr+(h->phoff-p->offset);
    }
    if (!executable || !*phdr) return -NV_ENOEXEC;
    t->abi=NV_ABI_VERSION;
    for (u32 i=0;i<h->phnum;++i) {
        const struct program_header *p=&ph[i];
        if (p->type!=1 || !p->memsz) continue;
        uptr first=(NV_USER_IMAGE+p->vaddr)&~(uptr)(PAGE-1),end=ALIGN_UP(NV_USER_IMAGE+p->vaddr+p->memsz,PAGE);
        for (uptr va=first;va<end;va+=PAGE) {
            int r=vm_map(t->pd,va,((p->flags&2)?P_WRITE:0)|((p->flags&1)?P_EXEC:0));
            if (r<0) return r;
        }
        if (copy_to_space(t->pd,NV_USER_IMAGE+p->vaddr,data+p->offset,p->filesz)<0) return -NV_ENOEXEC;
    }
    *entry=NV_USER_IMAGE+h->entry;
    return 0;
}
static int linux_push(struct task *t,uptr *sp,const void *data,u32 size,uptr *address) {
    if (size>*sp-(NV_USER_STACK-LINUX_STACK_PAGES*PAGE)) return -NV_E2BIG;
    *sp-=size; *address=*sp; return copy_to_space(t->pd,*sp,data,size);
}
static int linux_stack(struct task *t,const char *path,const char *args,const u8 *data,
                        uptr entry,uptr phdr,struct frame *f) {
    /* Bounded quoted argv decoder matching the native shell's quote_args.
     * The first argument is always the executable path. */
    char words[NV_ARG_MAX]; strlcpy(words,args,sizeof(words));
    const char *argv[34]={path}; u32 argc=1; char *in=words,*out=words;
    while (*in) {
        while (*in==' ' || *in=='\t' || *in=='\n' || *in=='\r') ++in;
        if (!*in) break;
        if (argc==ARRAY_LEN(argv)) return -NV_E2BIG;
        argv[argc++]=out; char quote=0;
        while (*in) {
            char c=*in++;
            if (c=='\\' && quote!='\'') { if (!*in) return -NV_EINVAL; *out++=*in++; }
            else if (c=='\'' || c=='"') {
                if (!quote) quote=c; else if (quote==c) quote=0; else *out++=c;
            } else if (!quote && (c==' ' || c=='\t' || c=='\n' || c=='\r')) break;
            else *out++=c;
        }
        if (quote) return -NV_EINVAL;
        *out++=0;
    }
    u64 pointers[96]; u32 count=0; uptr sp=NV_USER_STACK,where;
    u8 random[16]; if (!cpu_random(random,sizeof(random))) return -NV_EIO;
    if (linux_push(t,&sp,random,sizeof(random),&where)<0) return -NV_EFAULT;
    uptr random_address=where;
    pointers[count++]=argc;
    for (u32 i=0;i<argc;++i) {
        if (linux_push(t,&sp,argv[i],strlen(argv[i])+1,&where)<0) return -NV_EFAULT;
        pointers[count++]=where;
    }
    pointers[count++]=0;
    const char *env[]={"LANG=C","PATH=/apps:/home","NUVORA_COMPAT=linux-x86_64-v0"};
    for (u32 i=0;i<ARRAY_LEN(env);++i) {
        if (linux_push(t,&sp,env[i],strlen(env[i])+1,&where)<0) return -NV_EFAULT;
        pointers[count++]=where;
    }
    pointers[count++]=0;
    const struct elf_header *h=(const void *)data;
    u64 aux[]={3,phdr,4,sizeof(struct program_header),5,h->phnum,6,PAGE,7,0,9,entry,
        11,t->uid,12,t->uid,13,t->uid,14,t->uid,23,0,25,random_address,31,pointers[1],0,0};
    memcpy(pointers+count,aux,sizeof(aux)); count+=ARRAY_LEN(aux);
    sp=(sp-count*sizeof(u64))&~(uptr)15;
    if (copy_to_space(t->pd,sp,pointers,count*sizeof(u64))<0) return -NV_EFAULT;
    *f=(struct frame){.cs=0x1b,.ss=0x23,.eflags=0x202,.eip=entry,.useresp=sp};
    t->linux_compat.brk=NV_USER_HEAP;
    return 0;
}
#endif
