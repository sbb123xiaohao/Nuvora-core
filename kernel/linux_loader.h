#ifndef NV_LINUX_LOADER_H
#define NV_LINUX_LOADER_H
/* Linux PIE startup: static rcrt1 or an independently validated PT_INTERP.
 * Relocation and TLS remain the mature runtime linker's responsibility. */
struct linux_elf_info {uptr entry,phdr;u64 bytes;char interpreter[NV_PATH_MAX];};
static int linux_check_elf(const u8 *data,usize len,uptr base,uptr limit,bool linker,struct linux_elf_info *info) {
    memset(info,0,sizeof(*info));
    if(len<sizeof(struct elf_header))return -NV_ENOEXEC;
    const struct elf_header *h=(const void *)data;
    if(memcmp(h->ident,"\177ELF\2\1\1",7) || h->type!=3 || h->machine!=62 || h->version!=1 ||
       h->ehsize!=sizeof(*h) || h->phentsize!=sizeof(struct program_header) || !h->phnum ||
       h->phnum>32 || h->phoff>len || h->phnum>(len-h->phoff)/sizeof(struct program_header))return -NV_ENOEXEC;
    const struct program_header *ph=(const void *)(data+h->phoff);
    bool executable=false,needed=false;
    for(u32 i=0;i<h->phnum;++i) {
        const struct program_header *p=&ph[i];
        if(p->offset>len || p->filesz>len-p->offset || p->filesz>p->memsz ||
           (p->type==ELF_STACK && (p->flags&1)))return -NV_ENOEXEC;
        if(p->type==3) {
            if(linker || *info->interpreter || p->filesz<2 || p->filesz>sizeof(info->interpreter) ||
               data[p->offset]!='/' || data[p->offset+p->filesz-1])return -NV_ENOEXEC;
            for(u64 j=0;j+1<p->filesz;++j)if(!data[p->offset+j])return -NV_ENOEXEC;
            memcpy(info->interpreter,data+p->offset,p->filesz);
        }
        if(p->type==2) {
            if(!p->filesz || p->filesz%sizeof(struct elf_dynamic) || p->filesz>65536)return -NV_ENOEXEC;
            bool end=false;
            for(u64 at=0;at<p->filesz;at+=sizeof(struct elf_dynamic)) {
                const struct elf_dynamic *d=(const void *)(data+p->offset+at);
                if(!d->tag) {end=true;break;}
                if(d->tag==1)needed=true;
                if(d->tag==22 || (d->tag==30 && (d->value&4)))return -NV_ENOEXEC;
            }
            if(!end)return -NV_ENOEXEC;
        }
        if(p->type!=1 || !p->memsz)continue;
        if((p->flags&~7u) || !p->flags || (p->flags&3)==3 || p->vaddr>=limit-base ||
           p->memsz>limit-base-p->vaddr ||
           (p->align>1 && ((p->align&(p->align-1)) || ((p->offset^p->vaddr)&(p->align-1)))))return -NV_ENOEXEC;
        uptr first=p->vaddr&~(uptr)(PAGE-1),end=ALIGN_UP(p->vaddr+p->memsz,PAGE);
        info->bytes+=end-first;if(info->bytes>LINUX_MEMORY_MAX)return -NV_E2BIG;
        for(u32 j=0;j<i;++j)if(ph[j].type==1 && ph[j].memsz &&
            first<ALIGN_UP(ph[j].vaddr+ph[j].memsz,PAGE) && (ph[j].vaddr&~(uptr)(PAGE-1))<end)return -NV_ENOEXEC;
        if((p->flags&1) && h->entry>=p->vaddr && h->entry-p->vaddr<p->memsz)executable=true;
        u64 table_size=(u64)h->phnum*sizeof(*ph);
        if(h->phoff>=p->offset && h->phoff-p->offset<=p->filesz && table_size<=p->filesz-(h->phoff-p->offset))
            info->phdr=base+p->vaddr+(h->phoff-p->offset);
    }
    if(!executable || !info->phdr || (needed && (linker || !*info->interpreter)))return -NV_ENOEXEC;
    info->entry=base+h->entry;return 0;
}
static int linux_map_elf(struct task *t,const u8 *data,uptr base) {
    const struct elf_header *h=(const void *)data;
    const struct program_header *ph=(const void *)(data+h->phoff);
    for(u32 i=0;i<h->phnum;++i) {
        const struct program_header *p=&ph[i];if(p->type!=1 || !p->memsz)continue;
        uptr first=(base+p->vaddr)&~(uptr)(PAGE-1),end=ALIGN_UP(base+p->vaddr+p->memsz,PAGE);
        for(uptr va=first;va<end;va+=PAGE) {
            int r=vm_map(t->pd,va,((p->flags&2)?P_WRITE:0)|((p->flags&1)?P_EXEC:0));
            if(r<0)return r;
        }
        if(copy_to_space(t->pd,base+p->vaddr,data+p->offset,p->filesz)<0)return -NV_ENOEXEC;
    }
    return 0;
}
static int linux_image(struct task *t,const u8 *data,usize len,uptr *entry,uptr *phdr) {
    struct linux_elf_info main,linker;
    *entry=*phdr=0;
    int r=linux_check_elf(data,len,NV_USER_IMAGE,NV_USER_IMAGE_END,false,&main);
    if(r<0)return r;
    const u8 *ld=NULL;u8 *temporary=NULL;usize ld_size=0;
    if(*main.interpreter) {
        r=fs_authorize(t,0,main.interpreter,0);
        if(r>=0)r=fs_blob(main.interpreter,&ld,&ld_size,&temporary);
        if(r>=0)r=linux_check_elf(ld,ld_size,LINUX_INTERP_BASE,LINUX_MAP_BASE,true,&linker);
        if(r>=0 && main.bytes+linker.bytes>LINUX_MEMORY_MAX)r=-NV_E2BIG;
        if(r<0) {kfree(temporary);return r;}
    }
    t->abi=NV_ABI_VERSION;
    r=linux_map_elf(t,data,NV_USER_IMAGE);
    if(r>=0 && ld)r=linux_map_elf(t,ld,LINUX_INTERP_BASE);
    if(r>=0) {
        *entry=main.entry;*phdr=main.phdr;
        t->linux_compat.interpreter=ld?LINUX_INTERP_BASE:0;
        t->linux_compat.interpreter_entry=ld?linker.entry:0;
    }
    kfree(temporary);return r;
}
static int linux_push(struct task *t,uptr *sp,const void *data,u32 size,uptr *address) {
    if (size>*sp-(NV_USER_STACK-LINUX_STACK_PAGES*PAGE)) return -NV_E2BIG;
    *sp-=size; *address=*sp; return copy_to_space(t->pd,*sp,data,size);
}
static int linux_stack(struct task *t,const char *path,const char *args,const u8 *data,
                        uptr entry,uptr phdr,struct frame *f,const struct linux_startup *startup) {
    /* Bounded quoted argv decoder matching the native shell's quote_args.
     * The first argument is always the executable path. */
    char words[NV_ARG_MAX]; strlcpy(words,args,sizeof(words));
    const char *argv[34]={startup?startup->arg0:path}; u32 argc=1; char *in=words,*out=words;
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
    u64 pointers[128]; u32 count=0; uptr sp=NV_USER_STACK,where;
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
    if(startup) {
        for(u32 at=0,n=0;at<startup->environment_bytes;++n) {
            u32 size=strlen(startup->environment+at)+1;
            if(n==32 || size>startup->environment_bytes-at)return -NV_E2BIG;
            if(linux_push(t,&sp,startup->environment+at,size,&where)<0)return -NV_EFAULT;
            pointers[count++]=where;at+=size;
        }
    } else {
        for (u32 i=0;i<ARRAY_LEN(env);++i) {
            if (linux_push(t,&sp,env[i],strlen(env[i])+1,&where)<0) return -NV_EFAULT;
            pointers[count++]=where;
        }
    }
    pointers[count++]=0;
    const struct elf_header *h=(const void *)data;
    if(linux_push(t,&sp,path,strlen(path)+1,&where)<0)return -NV_EFAULT;
    u64 aux[]={3,phdr,4,sizeof(struct program_header),5,h->phnum,6,PAGE,7,t->linux_compat.interpreter,9,entry,
        11,t->uid,12,t->uid,13,t->uid,14,t->uid,23,0,25,random_address,31,where,0,0};
    memcpy(pointers+count,aux,sizeof(aux)); count+=ARRAY_LEN(aux);
    sp=(sp-count*sizeof(u64))&~(uptr)15;
    if (copy_to_space(t->pd,sp,pointers,count*sizeof(u64))<0) return -NV_EFAULT;
    *f=(struct frame){.cs=0x1b,.ss=0x23,.eflags=0x202,.eip=t->linux_compat.interpreter_entry?t->linux_compat.interpreter_entry:entry,.useresp=sp};
    t->linux_compat.brk=NV_USER_HEAP;
    return 0;
}
#endif
