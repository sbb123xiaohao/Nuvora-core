#include "../../kernel/kernel.h"
/* Linux x86-64 ABI numbers/layouts are public interface definitions. This is a
 * bounded process/thread subset, not a claim to emulate all Linux behavior. */
#include "linux_errno.h"
static iptr linux_error(iptr n) {
    static const u8 errors[]={0,L_EINVAL,L_ENOENT,L_ENOMEM,L_EFAULT,L_EACCES,L_EEXIST,
        L_ENOTDIR,L_EISDIR,L_ENOSPC,16,L_EBADF,L_ENOTEMPTY,8,10,L_ENOSYS,L_EIO,L_ENODEV,L_E2BIG,L_EAGAIN};
    if (n>=0) return n;
    u64 index=0ull-(u64)n;
    return index<ARRAY_LEN(errors)?-(iptr)errors[index]:-L_EIO;
}
#include "linux_memory.h"
#include "linux_process.h"
#include "linux_wait.h"
#include "linux_signal.h"
static iptr linux_brk(uptr address) {
    uptr old=current->compat_state.brk;
    if (!address) return old;
    if (address<NV_USER_HEAP || address>=NV_USER_HEAP+LINUX_MEMORY_MAX) return old;
    uptr end=ALIGN_UP(address,PAGE),before=current->heap_end;
    if (end>before && mapped_bytes()>LINUX_MEMORY_MAX-(end-before)) return old;
    i64 pages=end>=before?(i64)((end-before)/PAGE):-(i64)((before-end)/PAGE);
    if (pages && task_grow(pages)<0) return old;
    current->compat_state.brk=address; return address;
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
    if (fd>=NV_OPEN_MAX || (fd<3 && (current->compat_state.closed_stdio&(1u<<fd)))) return -L_EBADF;
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
    if(!strcmp(path,"/proc/self/exe") || !strcmp(path,"/proc/thread-self/exe")) {
        r=fs_path(current->program_node,path,sizeof(path));
        if(r>=0)r=fs_authorize(current,0,path,0);
        if(r<0)return linux_error(r);
        u32 n=MIN(size,(u64)strlen(path));memcpy((void *)output,path,n);return n;
    }
    r=fs_authorize(current,current->cwd,path,0);
    /* The native filesystem has no symbolic links. Existing authorized
     * paths return EINVAL; missing/private paths retain their real errors. */
    return r<0?linux_error(r):-L_EINVAL;
}
static iptr linux_open(uptr name,u64 flags) {
    if (flags&~(3ull|64|128|256|512|1024|2048|32768|65536|131072|524288)) return -L_EINVAL;
    if ((flags&3)==3 || (flags&256)) return -L_EINVAL; /* no O_NOCTTY devices */
    u32 native=(flags&3)==0?NV_READ:(flags&3)==1?NV_WRITE:NV_READ|NV_WRITE;
    if (flags&64) native|=NV_CREATE;
    if (flags&128) native|=NV_EXCL;
    if (flags&512) native|=NV_TRUNC;
    if (flags&1024) native|=NV_APPEND;
    char path[NV_PATH_MAX]; int r=user_string(name,path,sizeof(path));
    if(r<0)return linux_error(r);
    u32 free_fd=3;
    while(free_fd<NV_OPEN_MAX && current->fd[free_fd].node>=0)++free_fd;
    if(free_fd==NV_OPEN_MAX)return -L_EMFILE;
    if(r>=0 && current->compat_state.trace)kprintf("[linux %u] open %s\n",current->pid,path);
    if(flags&65536) {
        if(native&~NV_READ)return -L_EINVAL;
        r=fs_open_directory(current,path);
    } else r=fs_open(current,path,native);
    if (r>=0) {
        u64 bit=1ull<<r;
        current->compat_state.cloexec=(current->compat_state.cloexec&~bit)|((flags&524288)?bit:0);
        current->compat_state.nonblock=(current->compat_state.nonblock&~bit)|((flags&2048)?bit:0);
    }
    return linux_error(r);
}
static iptr linux_io(bool write,u64 fd,uptr data,u64 count) {
    if (fd>=NV_OPEN_MAX || (fd<3 && (current->compat_state.closed_stdio&(1u<<fd)))) return -L_EBADF;
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
static bool linux_clock_value(u64 id,u64 *value) {
    if(id==0 || id==5) {*value=clock_realtime_ticks();return true;}
    if(id==1 || id==4 || id==6 || id==7) {*value=ticks;return true;}
    u64 group_pid=current->compat_group?current->compat_group->pid:current->pid;
    bool process=id==2 || ((i64)id<0 && ((~id)>>3)==group_pid && (id&7)==2);
    if(process) {
        *value=current->compat_group?current->compat_group->retired_ticks:0;
        for(u32 i=0;i<NV_TASK_MAX;++i)if(tasks[i].state && tasks[i].pd==current->pd)*value+=tasks[i].cpu_ticks;
        return true;
    }
    if(id==3) {*value=current->cpu_ticks;return true;}
    if((i64)id<0 && (id&7)==6)for(u32 i=0;i<NV_TASK_MAX;++i)
        if(tasks[i].state && tasks[i].pd==current->pd && tasks[i].pid==((~id)>>3)) {*value=tasks[i].cpu_ticks;return true;}
    return false;
}
static iptr linux_clock(u64 id,uptr pointer) {
    u64 value;if(!linux_clock_value(id,&value))return -L_EINVAL;
    if (!user_range(current->pd,pointer,sizeof(struct linux_time),true)) return -L_EFAULT;
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
#include "linux_files.h"
struct frame *linux_dispatch(struct frame *f) {
    if (!current || (f->cs&3)!=3) panic("Linux syscall outside user mode");
    if (!current->compat_state.enabled) { f->eax=(u64)(i64)-L_ENOSYS; return f; }
    current->frame=f; task_reap(); usb_poll(); net_poll();
    compat_enter(current);
    u64 op=f->eax,a=f->edi,b=f->esi,c=f->edx,d=f->r10,e=f->r8,g=f->r9;
    iptr result=-L_ENOSYS; bool reschedule=false;
    switch (op) {
    case 0:
        result=linux_io(false,a,b,c);
        if (a==0 && result==-L_EAGAIN && !(current->compat_state.nonblock&1)) { f->eip-=2; result=0;reschedule=true; }
        break;
    case 1: result=linux_io(true,a,b,c);break;
    case 2: result=linux_open(a,b);break;
    case 13: result=linux_sigaction(a,b,c,d);break;
    case 131: result=linux_sigaltstack(a,b,f->useresp);break;
    case 15:
        if(linux_sigreturn(f)<0) {task_linux_exit_group(139);return schedule(f);}
        linux_pending(f);if(current->state!=NV_ZOMBIE)compat_leave(current);
        return current->state==NV_ZOMBIE?schedule(f):f;
    case 62: case 200: result=linux_self_signal(a,b);break;
    case 234: result=task_linux_signal(a,b,c);break;
    case 14: {
        if(d!=8 || a>2) {result=-L_EINVAL;break;}
        if((b && !user_range(current->pd,b,8,false)) || (c && !user_range(current->pd,c,8,true))) {result=-L_EFAULT;break;}
        u64 old=current->compat_state.signal_mask,next=old,value;
        if(b) {memcpy(&value,(const void *)b,8);next=a==0?old|value:a==1?old&~value:value;}
        if(c)memcpy((void *)c,&old,8);
        current->compat_state.signal_mask=next&~((1ull<<8)|(1ull<<18));result=0;break;
    }
    case 3:
        result=a>=NV_OPEN_MAX?-L_EBADF:a<3?(current->compat_state.closed_stdio&(1u<<a))?-L_EBADF:0:linux_error(fs_close(current,a));
        if (!result) {
            if (a<3) current->compat_state.closed_stdio|=1u<<a;
            current->compat_state.cloexec&=~(1ull<<a); current->compat_state.nonblock&=~(1ull<<a);
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
    case 17: {
        if(a<3 || a>=NV_OPEN_MAX) {result=-L_EBADF;break;}
        u32 n=MIN(c,0x7ffff000ull);
        if(!user_range(current->pd,b,n,true)) {result=-L_EFAULT;break;}
        result=linux_error(fs_pread64(current,a,d,(void *)b,n));break;
    }
    case 28: result=linux_advise(a,b,c);break;
    case 10: result=linux_protect(a,b,c);break;
    case 11: result=linux_unmap(a,b);break;
    case 12: result=linux_brk(a);break;
    case 16:
        result=a>=NV_OPEN_MAX || (a>=3 && current->fd[a].node<0) ||
            (a<3 && (current->compat_state.closed_stdio&(1u<<a)))?-L_EBADF:-L_ENOTTY;
        break;
    case 19: case 20:
        result=linux_vectors(op==20,a,b,c);
        if (op==19 && a==0 && result==-L_EAGAIN && !(current->compat_state.nonblock&1)) {
            f->eip-=2;result=19;reschedule=true;
        }
        break;
    case 24: result=0;reschedule=true;break;
    case 32: result=linux_duplicate(a,0,false);break;
    case 217: result=linux_getdents(a,b,c);break;
    case 21: result=linux_access(a,b);break;
    case 59: result=linux_execve(a,b,c);break;
    case 56: result=task_linux_clone(f,a,b,c,d,e);break;
    case 35: {
        if (!user_range(current->pd,a,sizeof(struct linux_time),false) ||
            (b && !user_range(current->pd,b,sizeof(struct linux_time),true))) { result=-L_EFAULT;break; }
        struct linux_time io; memcpy(&io,(const void *)a,sizeof(io));
        if (io.seconds<0 || io.seconds>86400 || io.nanoseconds<0 || io.nanoseconds>=1000000000) { result=-L_EINVAL;break; }
        u64 duration=(u64)io.seconds*100+((u64)io.nanoseconds+9999999)/10000000;
        if (duration) {
            current->compat_state.sleep_remaining=b;current->compat_state.sleep_left=0;
            current->compat_state.sleeping=true;current->compat_state.sleep_interrupted=false;
            current->wake=duration>~0ull-ticks?~0ull:ticks+duration;current->state=NV_SLEEPING;
        }
        result=0;reschedule=duration!=0;break;
    }
    case 39: result=current->compat_group?current->compat_group->pid:current->pid;break;
    /* No socket family is connected to native networking yet. Capability
     * probes (notably HotSpot's IPv6 probe) need the protocol-family error,
     * rather than ENOSYS poisoning their Java class initialization. */
    case 41: result=-L_EAFNOSUPPORT;break;
    case 186: result=current->pid;break;
    case 218: current->compat_state.clear_tid=a;result=current->pid;break;
    case 60: compat_leave(current);task_exit(a&255);result=0;reschedule=true;break;
    case 231: compat_leave(current);task_linux_exit_group(a&255);result=0;reschedule=true;break;
    case 63: {
        char info[6][65]={{0}};
        strlcpy(info[0],"Linux",65);strlcpy(info[1],"nuvora",65);
        strlcpy(info[2],"0.0.0-nuvora-compat",65);strlcpy(info[3],"Nuvora static process subset",65);
        strlcpy(info[4],"x86_64",65);
        if (!user_range(current->pd,a,sizeof(info),true)) { result=-L_EFAULT;break; }
        memcpy((void *)a,info,sizeof(info));result=0;break;
    }
    case 72: /* Descriptor/status flags and shared file/directory duplication. */
        if (a>=NV_OPEN_MAX || (a>=3 && current->fd[a].node<0) ||
            (a<3 && (current->compat_state.closed_stdio&(1u<<a)))) result=-L_EBADF;
        else if (b==0 || b==1030) result=linux_duplicate(a,c,b==1030);
        else if (b==1) result=(current->compat_state.cloexec>>a)&1;
        else if (b==2 && c<=1) {
            current->compat_state.cloexec=(current->compat_state.cloexec&~(1ull<<a))|(c<<a); result=0;
        }
        else if (b==3) {
            result=a==0?0:a<3?1:(current->fd[a].flags&(NV_READ|NV_WRITE))==(NV_READ|NV_WRITE)?2:
                current->fd[a].flags&NV_WRITE?1:0;
            if (current->compat_state.nonblock&(1ull<<a)) result|=2048;
            if (a>=3 && (current->fd[a].flags&NV_APPEND)) result|=1024;
        } else if (b==4 && !(c&~(3ull|1024|2048))) {
            current->compat_state.nonblock=(current->compat_state.nonblock&~(1ull<<a))|((c&2048)?1ull<<a:0);
            if (a>=3) {
                current->fd[a].flags=(current->fd[a].flags&~NV_APPEND)|((c&1024)?NV_APPEND:0);
                fs_descriptor_sync(current,a);
                u32 group=current->fd[a].sharing;
                if(group)for(u32 i=3;i<NV_OPEN_MAX;++i)if(current->fd[i].node>=0 && current->fd[i].sharing==group)
                    current->compat_state.nonblock=(current->compat_state.nonblock&~(1ull<<i))|((c&2048)?1ull<<i:0);
            }
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
    case 80: case 83: case 84: case 87: result=linux_path_operation(op,a);break;
    case 82: result=linux_rename(a,b);break;
    case 258:
        result=(i64)a==-100?linux_path_operation(83,b):-L_ENOTSUP;break;
    case 263:
        if((i64)a!=-100)result=-L_ENOTSUP;
        else if(c && c!=512)result=-L_EINVAL;
        else result=linux_path_operation(c?84:87,b);
        break;
    case 89: result=linux_readlink(a,b,c);break;
    case 102: case 104: case 107: case 108: result=current->uid;break;
    case 158:
        if (a==0x1002) {
            if (b && !nv_user_address(b)) result=-L_EPERM;
            else { current->compat_state.fs_base=b;arch_set_fs(b);result=0; }
        } else if (a==0x1003) {
            if (!user_range(current->pd,b,sizeof(u64),true)) result=-L_EFAULT;
            else { memcpy((void *)b,&current->compat_state.fs_base,sizeof(u64));result=0; }
        } else result=-L_EINVAL;
        break;
    case 202:
        result=task_linux_futex(a,b,c,d,e,g);reschedule=current->state==NV_SLEEPING;break;
    case 228: result=linux_clock(a,b);break;
    case 229: {
        u64 value;if(!linux_clock_value(a,&value)) {result=-L_EINVAL;break;}
        if(b && !user_range(current->pd,b,sizeof(struct linux_time),true)) {result=-L_EFAULT;break;}
        struct linux_time resolution={0,10000000};if(b)memcpy((void *)b,&resolution,sizeof(resolution));result=0;break;
    }
    case 96:
        if((a && !user_range(current->pd,a,16,true)) || (b && !user_range(current->pd,b,8,true)))result=-L_EFAULT;
        else {
            u64 value=clock_realtime_ticks();i64 out[2]={(i64)(value/100),(i64)(value%100)*10000};
            if(a)memcpy((void *)a,out,16);
            if(b) {u64 zero=0;memcpy((void *)b,&zero,8);}result=0;
        }
        break;
    case 99: {
        struct {i64 uptime;u64 loads[3],totalram,freeram,sharedram,bufferram,totalswap,freeswap;
                u16 processes,pad;u32 pad2;u64 totalhigh,freehigh;u32 unit,pad3;} info={0};
        _Static_assert(sizeof(info)==112,"Linux sysinfo layout");
        if(!user_range(current->pd,a,sizeof(info),true)) {result=-L_EFAULT;break;}
        info.uptime=ticks/100;info.totalram=(u64)pages_total()*PAGE;info.freeram=(u64)pages_free()*PAGE;
        info.processes=task_count();info.unit=1;memcpy((void *)a,&info,sizeof(info));result=0;break;
    }
    case 204:
        if(a && a!=current->pid)result=-L_EPERM;
        else if(b<8)result=-L_EINVAL;
        else if(!user_range(current->pd,c,8,true))result=-L_EFAULT;
        else {u64 mask=1;memcpy((void *)c,&mask,8);result=8;}
        break;
    case 309:
        if((a && !user_range(current->pd,a,4,true)) || (b && !user_range(current->pd,b,4,true)))result=-L_EFAULT;
        else {u32 zero=0;if(a)memcpy((void *)a,&zero,4);if(b)memcpy((void *)b,&zero,4);result=0;}
        break;
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
        else if ((b!=3 && b!=7) || c) result=-L_ENOTSUP;
        else if (d && !user_range(current->pd,d,16,true)) result=-L_EFAULT;
        else { u64 maximum=b==7?NV_OPEN_MAX:LINUX_STACK_PAGES*PAGE;
            u64 limit[2]={maximum,maximum};if (d) memcpy((void *)d,limit,16);result=0; }
        break;
    case 318: result=linux_random(a,b,c);break;
    default: break; /* Unsupported calls fail explicitly. */
    }
    if(current->compat_state.trace)kprintf("[linux %u] %llu(%llx,%llx,%llx,%llx,%llx,%llx) -> %lld\n",current->pid,op,a,b,c,d,e,g,(i64)result);
    f->eax=(u64)result;
    if(current->state!=NV_ZOMBIE && linux_pending(f))reschedule=true;
    if(current->state!=NV_ZOMBIE)compat_leave(current);
    return reschedule?schedule(f):f;
}
