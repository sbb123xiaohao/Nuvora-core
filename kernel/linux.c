#include "kernel.h"
/* Linux x86-64 ABI numbers/layouts are public interface definitions. This is a
 * bounded static-process subset, not a claim to emulate all Linux behavior. */
#define L_EPERM 1
#define L_ENOENT 2
#define L_EIO 5
#define L_E2BIG 7
#define L_EBADF 9
#define L_EAGAIN 11
#define L_ENOMEM 12
#define L_EACCES 13
#define L_EFAULT 14
#define L_EEXIST 17
#define L_ENODEV 19
#define L_ENOTDIR 20
#define L_EISDIR 21
#define L_EINVAL 22
#define L_ENOTTY 25
#define L_ENOSPC 28
#define L_ENOSYS 38
#define L_ENOTEMPTY 39
#define L_ENOTSUP 95
static iptr linux_error(iptr n) {
    static const u8 errors[]={0,L_EINVAL,L_ENOENT,L_ENOMEM,L_EFAULT,L_EACCES,L_EEXIST,
        L_ENOTDIR,L_EISDIR,L_ENOSPC,16,L_EBADF,L_ENOTEMPTY,8,10,L_ENOSYS,L_EIO,L_ENODEV,L_E2BIG,L_EAGAIN};
    if (n>=0) return n;
    u64 index=0ull-(u64)n;
    return index<ARRAY_LEN(errors)?-(iptr)errors[index]:-L_EIO;
}
static bool linux_length(uptr address,u64 n) { return n && n<=LINUX_MEMORY_MAX && !(address%PAGE) && n<=~(uptr)0-address-PAGE+1; }
static u64 mapped_bytes(void) { return vm_page_count(current->pd)*PAGE; }
static iptr linux_map(uptr hint,u64 length,u64 prot,u64 flags,u64 fd,u64 offset) {
    if (!length || length>LINUX_MEMORY_MAX || (prot!=1 && prot!=3 && prot!=5) ||
        (flags&~(0x22ull|0x20000ull)) || (flags&0x22)!=0x22 || fd!=~0ull || offset) return -L_EINVAL;
    uptr size=ALIGN_UP(length,PAGE);
    if (mapped_bytes()>LINUX_MEMORY_MAX-size) return -L_ENOMEM;
    struct linux_vma *maps=current->linux_compat.maps; u32 slot=LINUX_VMAS;
    for (u32 i=0;i<LINUX_VMAS;++i) if (!maps[i].start) { slot=i; break; }
    if (slot==LINUX_VMAS) return -L_ENOMEM;
    uptr address=hint>=LINUX_MAP_BASE && hint<LINUX_MAP_END && !(hint%PAGE)?hint:LINUX_MAP_BASE;
    for (u32 pass=0;pass<=LINUX_VMAS;++pass) {
        if (size>LINUX_MAP_END-address) return -L_ENOMEM;
        bool overlap=false;
        for (u32 i=0;i<LINUX_VMAS;++i) if (maps[i].start && address<maps[i].end && maps[i].start<address+size) {
            address=maps[i].end; overlap=true; break;
        }
        if (!overlap) break;
        if (pass==LINUX_VMAS) return -L_ENOMEM;
    }
    u32 permissions=((prot&2)?P_WRITE:0)|((prot&4)?P_EXEC:0); uptr done=0;
    for (;done<size;done+=PAGE) {
        int r=vm_map(current->pd,address+done,permissions);
        if (r<0) {
            while (done) { done-=PAGE; vm_unmap(current->pd,address+done); }
            return linux_error(r);
        }
    }
    maps[slot]=(struct linux_vma){address,address+size,(u32)prot};
    return address;
}
static iptr linux_unmap(uptr start,u64 length) {
    if (!linux_length(start,length) || start<LINUX_MAP_BASE || start>=LINUX_MAP_END) return -L_EINVAL;
    uptr size=ALIGN_UP(length,PAGE); if (size>LINUX_MAP_END-start) return -L_EINVAL;
    uptr end=start+size; struct linux_vma next[LINUX_VMAS];
    memcpy(next,current->linux_compat.maps,sizeof(next));
    /* Prepare every split before dropping a page; metadata exhaustion must
     * leave the entire old mapping intact. */
    for (u32 i=0;i<LINUX_VMAS;++i) {
        struct linux_vma old=next[i];
        if (!old.start || start>=old.end || old.start>=end) continue;
        if (start>old.start && end<old.end) {
            u32 j; for (j=0;j<LINUX_VMAS;++j) if (!next[j].start) break;
            if (j==LINUX_VMAS) return -L_ENOMEM;
            next[i].end=start; next[j]=(struct linux_vma){end,old.end,old.protection};
        } else if (start<=old.start && end>=old.end) next[i]=(struct linux_vma){0};
        else if (start<=old.start) next[i].start=end;
        else next[i].end=start;
    }
    for (uptr va=start;va<end;va+=PAGE) vm_unmap(current->pd,va);
    memcpy(current->linux_compat.maps,next,sizeof(next)); return 0;
}
static iptr linux_protect(uptr start,u64 length,u64 prot) {
    if (!linux_length(start,length) || (prot!=1 && prot!=3 && prot!=5)) return -L_EINVAL;
    uptr size=ALIGN_UP(length,PAGE);
    if (!user_range(current->pd,start,size,false)) return -L_ENOMEM;
    u32 flags=((prot&2)?P_WRITE:0)|((prot&4)?P_EXEC:0);
    for (uptr at=0;at<size;at+=PAGE) {
        int r=vm_protect(current->pd,start+at,flags);
        if (r<0) return linux_error(r);
    }
    for (u32 i=0;i<LINUX_VMAS;++i) {
        struct linux_vma *map=&current->linux_compat.maps[i];
        if (map->start>=start && map->end<=start+size) map->protection=prot;
    }
    return 0;
}
static iptr linux_brk(uptr address) {
    uptr old=current->linux_compat.brk;
    if (!address) return old;
    if (address<NV_USER_HEAP || address>=NV_USER_HEAP+LINUX_MEMORY_MAX) return old;
    uptr end=ALIGN_UP(address,PAGE),before=current->heap_end;
    if (end>before && mapped_bytes()>LINUX_MEMORY_MAX-(end-before)) return old;
    i64 pages=end>=before?(i64)((end-before)/PAGE):-(i64)((before-end)/PAGE);
    if (pages && task_grow(pages)<0) return old;
    current->linux_compat.brk=address; return address;
}
struct linux_stat {
    u64 device,inode,nlink;
    u32 mode,uid,gid,pad;
    u64 rdev;
    i64 size,blksize,blocks;
    i64 atime,atime_ns,mtime,mtime_ns,ctime,ctime_ns,reserved[3];
};
_Static_assert(sizeof(struct linux_stat)==144,"Linux x86-64 stat");
static iptr linux_stat_fd(u64 fd,uptr output) {
    if (fd>=NV_OPEN_MAX || (fd<3 && (current->linux_compat.closed_stdio&(1u<<fd)))) return -L_EBADF;
    if (!user_range(current->pd,output,sizeof(struct linux_stat),true)) return -L_EFAULT;
    struct linux_stat out={.nlink=1,.uid=current->uid,.gid=current->uid,.blksize=PAGE};
    if (fd<3) out.mode=0020000|0600;
    else {
        struct nv_stat64 st; int r=fs_stat64(current,fd,&st);
        if (r<0) return linux_error(r);
        out.mode=(st.kind==NV_DIR?0040000:st.kind==NV_DEVICE?0020000:0100000)|0600;
        out.size=st.size; out.blocks=(st.allocated+511)/512;
        out.inode=(u64)current->fd[fd].node+1;
    }
    memcpy((void *)output,&out,sizeof(out)); return 0;
}
static iptr linux_stat_path(uptr name,uptr output) {
    if (!user_range(current->pd,output,sizeof(struct linux_stat),true)) return -L_EFAULT;
    char path[NV_PATH_MAX];int r=user_string(name,path,sizeof(path));
    struct nv_stat64 st;
    if (r>=0) r=fs_stat_path64(current,path,&st);
    if (r<0) return linux_error(r);
    struct linux_stat out={.nlink=1,.uid=current->uid,.gid=current->uid,.blksize=PAGE,
        .inode=(u64)fs_lookup(current->cwd,path)+1,
        .mode=(st.kind==NV_DIR?0040000:st.kind==NV_DEVICE?0020000:0100000)|0600,
        .size=st.size,.blocks=(st.allocated+511)/512};
    memcpy((void *)output,&out,sizeof(out));return 0;
}
static iptr linux_readlink(uptr name,uptr output,u64 size) {
    if (!size) return -L_EINVAL;
    char path[NV_PATH_MAX];int r=user_string(name,path,sizeof(path));
    if (r<0) return linux_error(r);
    if (!user_range(current->pd,output,MIN(size,(u64)NV_PATH_MAX),true)) return -L_EFAULT;
    r=fs_authorize(current,current->cwd,path,0);
    /* The native filesystem has no symbolic links. Existing authorized
     * paths return EINVAL; missing/private paths retain their real errors. */
    return r<0?linux_error(r):-L_EINVAL;
}
static iptr linux_open(uptr name,u64 flags) {
    if (flags&~(3ull|64|128|256|512|1024|2048|32768|524288)) return -L_EINVAL;
    if ((flags&3)==3 || (flags&256)) return -L_EINVAL; /* no O_NOCTTY devices */
    u32 native=(flags&3)==0?NV_READ:(flags&3)==1?NV_WRITE:NV_READ|NV_WRITE;
    if (flags&64) native|=NV_CREATE;
    if (flags&128) native|=NV_EXCL;
    if (flags&512) native|=NV_TRUNC;
    if (flags&1024) native|=NV_APPEND;
    char path[NV_PATH_MAX]; int r=user_string(name,path,sizeof(path));
    r=r<0?r:fs_open(current,path,native);
    if (r>=0) {
        u32 bit=1u<<r;
        current->linux_compat.cloexec=(current->linux_compat.cloexec&~bit)|((flags&524288)?bit:0);
        current->linux_compat.nonblock=(current->linux_compat.nonblock&~bit)|((flags&2048)?bit:0);
    }
    return linux_error(r);
}
static iptr linux_io(bool write,u64 fd,uptr data,u64 count) {
    if (fd>=NV_OPEN_MAX || (fd<3 && (current->linux_compat.closed_stdio&(1u<<fd)))) return -L_EBADF;
    u32 bytes=(u32)MIN(count,16384ull);
    if (!user_range(current->pd,data,bytes,!write)) return -L_EFAULT;
    return linux_error(write?fs_write(current,fd,(const void *)data,bytes):fs_read(current,fd,(void *)data,bytes));
}
static iptr linux_vectors(bool write,u64 fd,uptr data,u64 count) {
    if (count>16) return -L_EINVAL;
    struct { uptr address; u64 length; } vectors[16];
    if (!user_range(current->pd,data,count*sizeof(vectors[0]),false)) return -L_EFAULT;
    memcpy(vectors,(const void *)data,count*sizeof(vectors[0])); iptr total=0;
    for (u32 i=0;i<count;++i) {
        if (!vectors[i].length) continue;
        iptr r=linux_io(write,fd,vectors[i].address,vectors[i].length);
        if (r<0) return total?total:r;
        total+=r;
        if ((u64)r<vectors[i].length) break;
    }
    return total;
}
struct linux_time { i64 seconds,nanoseconds; };
static iptr linux_clock(u64 id,uptr pointer) {
    if (id!=1 && id!=2 && id!=3 && id!=4 && id!=6 && id!=7) return -L_EINVAL;
    if (!user_range(current->pd,pointer,sizeof(struct linux_time),true)) return -L_EFAULT;
    u64 value=(id==2 || id==3)?current->cpu_ticks:ticks;
    struct linux_time out={(i64)(value/100),(i64)(value%100)*10000000};
    memcpy((void *)pointer,&out,sizeof(out)); return 0;
}
static iptr linux_random(uptr pointer,u64 count,u64 flags) {
    if (flags&~3ull) return -L_EINVAL;
    u32 n=MIN(count,256ull);
    if (!user_range(current->pd,pointer,n,true)) return -L_EFAULT;
    u8 bytes[256];
    if (n && !cpu_random(bytes,n)) return -L_EIO;
    memcpy((void *)pointer,bytes,n); memset(bytes,0,sizeof(bytes)); return n;
}
struct frame *linux_dispatch(struct frame *f) {
    if (!current || (f->cs&3)!=3) panic("Linux syscall outside user mode");
    if (!current->linux_compat.enabled) { f->eax=(u64)(i64)-L_ENOSYS; return f; }
    current->frame=f; task_reap(); usb_poll(); net_poll();
    u64 op=f->eax,a=f->edi,b=f->esi,c=f->edx,d=f->r10,e=f->r8,g=f->r9;
    iptr result=-L_ENOSYS; bool reschedule=false;
    switch (op) {
    case 0:
        result=linux_io(false,a,b,c);
        if (a==0 && result==-L_EAGAIN && !(current->linux_compat.nonblock&1)) { f->eip-=2; result=0;reschedule=true; }
        break;
    case 1: result=linux_io(true,a,b,c);break;
    case 2: result=linux_open(a,b);break;
    case 3:
        result=a>=NV_OPEN_MAX?-L_EBADF:a<3?(current->linux_compat.closed_stdio&(1u<<a))?-L_EBADF:0:linux_error(fs_close(current,a));
        if (!result) {
            if (a<3) current->linux_compat.closed_stdio|=1u<<a;
            current->linux_compat.cloexec&=~(1u<<a); current->linux_compat.nonblock&=~(1u<<a);
        }
        break;
    case 4: case 6: result=linux_stat_path(a,b);break;
    case 5: result=linux_stat_fd(a,b);break;
    case 8: {
        if (a>=NV_OPEN_MAX) { result=-L_EBADF;break; }
        if (a<3) { result=-29;break; }
        if (c>2) { result=-L_EINVAL;break; }
        struct nv_seek64 io={.offset=(i64)b,.origin=c};
        int r=fs_seek64(current,a,&io); result=r<0?linux_error(r):(iptr)io.position;break;
    }
    case 9: result=linux_map(a,b,c,d,e,g);break;
    case 10: result=linux_protect(a,b,c);break;
    case 11: result=linux_unmap(a,b);break;
    case 12: result=linux_brk(a);break;
    case 16:
        result=a>=NV_OPEN_MAX || (a>=3 && current->fd[a].node<0) ||
            (a<3 && (current->linux_compat.closed_stdio&(1u<<a)))?-L_EBADF:-L_ENOTTY;
        break;
    case 19: case 20:
        result=linux_vectors(op==20,a,b,c);
        if (op==19 && a==0 && result==-L_EAGAIN && !(current->linux_compat.nonblock&1)) {
            f->eip-=2;result=19;reschedule=true;
        }
        break;
    case 24: result=0;reschedule=true;break;
    case 35: {
        if (!user_range(current->pd,a,sizeof(struct linux_time),false) ||
            (b && !user_range(current->pd,b,sizeof(struct linux_time),true))) { result=-L_EFAULT;break; }
        struct linux_time io; memcpy(&io,(const void *)a,sizeof(io));
        if (io.seconds<0 || io.seconds>86400 || io.nanoseconds<0 || io.nanoseconds>=1000000000) { result=-L_EINVAL;break; }
        u64 duration=(u64)io.seconds*100+((u64)io.nanoseconds+9999999)/10000000;
        if (duration) { current->wake=ticks+duration;current->state=NV_SLEEPING; }
        if (b) memset((void *)b,0,sizeof(io));
        result=0;reschedule=true;break;
    }
    case 39: case 186: case 218: result=current->pid;break; /* one thread; no clone */
    case 60: case 231: task_exit(a&255);result=0;reschedule=true;break;
    case 63: {
        char info[6][65]={{0}};
        strlcpy(info[0],"Linux",65);strlcpy(info[1],"nuvora",65);
        strlcpy(info[2],"0.0.0-nuvora-compat",65);strlcpy(info[3],"Nuvora static process subset",65);
        strlcpy(info[4],"x86_64",65);
        if (!user_range(current->pd,a,sizeof(info),true)) { result=-L_EFAULT;break; }
        memcpy((void *)a,info,sizeof(info));result=0;break;
    }
    case 72: /* Only flags; no dup, advisory locks or descriptor passing. */
        if (a>=NV_OPEN_MAX || (a>=3 && current->fd[a].node<0) ||
            (a<3 && (current->linux_compat.closed_stdio&(1u<<a)))) result=-L_EBADF;
        else if (b==1) result=(current->linux_compat.cloexec>>a)&1;
        else if (b==2 && c<=1) {
            current->linux_compat.cloexec=(current->linux_compat.cloexec&~(1u<<a))|(c<<a); result=0;
        }
        else if (b==3) {
            result=a==0?0:a<3?1:(current->fd[a].flags&(NV_READ|NV_WRITE))==(NV_READ|NV_WRITE)?2:
                current->fd[a].flags&NV_WRITE?1:0;
            if (current->linux_compat.nonblock&(1u<<a)) result|=2048;
            if (a>=3 && (current->fd[a].flags&NV_APPEND)) result|=1024;
        } else if (b==4 && !(c&~(3ull|1024|2048))) {
            current->linux_compat.nonblock=(current->linux_compat.nonblock&~(1u<<a))|((c&2048)?1u<<a:0);
            if (a>=3) current->fd[a].flags=(current->fd[a].flags&~NV_APPEND)|((c&1024)?NV_APPEND:0);
            result=0;
        }
        else result=-L_EINVAL;
        break;
    case 79: {
        char path[NV_PATH_MAX]; int r=fs_path(current->cwd,path,sizeof(path));
        if (r<0) { result=linux_error(r);break; }
        u32 n=strlen(path)+1;
        if (b<n) { result=-34;break; }
        if (!user_range(current->pd,a,n,true)) { result=-L_EFAULT;break; }
        memcpy((void *)a,path,n);result=n;break;
    }
    case 89: result=linux_readlink(a,b,c);break;
    case 102: case 104: case 107: case 108: result=current->uid;break;
    case 158:
        if (a==0x1002) {
            if (b && !nv_user_address(b)) result=-L_EPERM;
            else { current->linux_compat.fs_base=b;arch_set_fs(b);result=0; }
        } else if (a==0x1003) {
            if (!user_range(current->pd,b,sizeof(u64),true)) result=-L_EFAULT;
            else { memcpy((void *)b,&current->linux_compat.fs_base,sizeof(u64));result=0; }
        } else result=-L_EINVAL;
        break;
    case 202: /* Single-thread futex: WAKE; WAIT can only fail immediately. */
        if (a%4 || !user_range(current->pd,a,4,false)) result=-L_EFAULT;
        else if ((b&~128ull)==1) result=0;
        else if ((b&~128ull)==0 && *(const u32 *)a!=(u32)c) result=-L_EAGAIN;
        break;
    case 228: result=linux_clock(a,b);break;
    case 257:
        if ((i64)a!=-100) result=-L_ENOTSUP;else result=linux_open(b,c);break;
    case 262:
        if ((i64)a!=-100) result=-L_ENOTSUP;
        else if (d&~256ull) result=-L_EINVAL;
        else result=linux_stat_path(b,c);
        break;
    case 267:
        if ((i64)a!=-100) result=-L_ENOTSUP;else result=linux_readlink(b,c,d);break;
    case 302:
        if (a && a!=current->pid) result=-L_EPERM;
        else if (b!=3 || c) result=-L_ENOTSUP;
        else if (d && !user_range(current->pd,d,16,true)) result=-L_EFAULT;
        else { u64 limit[2]={LINUX_STACK_PAGES*PAGE,LINUX_STACK_PAGES*PAGE};if (d) memcpy((void *)d,limit,16);result=0; }
        break;
    case 318: result=linux_random(a,b,c);break;
    default: break; /* clone/signals/sockets/dynamic libraries are not present. */
    }
    f->eax=(u64)result;
    return reschedule?schedule(f):f;
}
