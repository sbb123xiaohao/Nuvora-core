#ifndef NV_LINUX_PROCESS_H
#define NV_LINUX_PROCESS_H
/* Copy every pointer/string before replacing the old address space. This ABI's
 * exec budgets are independent from the native shell; failed exec is atomic. */
struct linux_exec_data {
    char path[NV_PATH_MAX],arguments[NV_COMPAT_ARG_BYTES],environment[NV_COMPAT_ENV_BYTES];
};
static int linux_vector(uptr pointer,u32 index,uptr *value) {
    if(pointer>~(uptr)0-(uptr)index*sizeof(uptr))return -L_EFAULT;
    if(!user_range(current->pd,pointer+index*sizeof(uptr),sizeof(uptr),false))return -L_EFAULT;
    memcpy(value,(const void *)(pointer+index*sizeof(uptr)),sizeof(*value));return 0;
}
static iptr linux_execve(uptr name,uptr argv,uptr envp) {
    if(!account_session_allowed(current) || current->pid==1 || account_manager(current))return -L_EACCES;
    struct linux_exec_data *copy=kmalloc(sizeof(*copy));if(!copy)return -L_ENOMEM;
    int r=user_string(name,copy->path,sizeof(copy->path));
    if(r<0) {kfree(copy);return linux_error(r);}
    int node=fs_lookup(current->cwd,copy->path);
    if(node<0) {kfree(copy);return linux_error(node);}
    r=fs_authorize(current,current->cwd,copy->path,0);
    if(r>=0)r=fs_path(node,copy->path,sizeof(copy->path));
    if(r<0) {kfree(copy);return linux_error(r);}
    u32 used=0,argument_count=0,environment_bytes=0;
    for(u32 i=0;argv;++i) {
        uptr value;r=linux_vector(argv,i,&value);if(r<0)goto done;
        if(!value)break;
        if(i==NV_COMPAT_ARG_COUNT || used==sizeof(copy->arguments)) {r=-L_E2BIG;goto done;}
        r=user_string(value,copy->arguments+used,sizeof(copy->arguments)-used);
        if(r<0) {r=linux_error(r);goto done;}
        used+=strlen(copy->arguments+used)+1;++argument_count;
    }
    if(!argument_count) {
        used=strlcpy(copy->arguments,copy->path,sizeof(copy->arguments))+1;argument_count=1;
    }
    for(u32 i=0;envp;++i) {
        uptr value;r=linux_vector(envp,i,&value);if(r<0)goto done;
        if(!value)break;
        if(i==NV_COMPAT_ENV_COUNT || environment_bytes==sizeof(copy->environment)) {r=-L_E2BIG;goto done;}
        r=user_string(value,copy->environment+environment_bytes,sizeof(copy->environment)-environment_bytes);
        if(r<0) {r=linux_error(r);goto done;}
        environment_bytes+=strlen(copy->environment+environment_bytes)+1;
    }
    struct compat_startup startup={.arg0=copy->arguments,.environment=copy->environment,
        .environment_bytes=environment_bytes,.arguments=copy->arguments,.argument_bytes=used,.argument_count=argument_count};
    u16 closed=current->compat_state.closed_stdio;
    u64 cloexec=current->compat_state.cloexec,nonblock=current->compat_state.nonblock;
    u64 mask=current->compat_state.signal_mask;
    u64 pending=current->compat_state.pending_signals,ignored=0;
    for(u32 signo=0;signo<64;++signo)if(current->compat_state.actions[signo].handler==1)ignored|=1ull<<signo;
    u32 options=(current->compat_state.trace?NV_COMPAT_TRACE:0)|(current->compat_state.jit?NV_COMPAT_JIT:0);
    r=linux_error(compat_exec_start(copy->path,"",options,&startup));
    if(!r) {
        for(u32 fd=0;fd<NV_OPEN_MAX;++fd)if(cloexec&(1ull<<fd)) {
            if(fd<3)closed|=1u<<fd;else fs_close(current,fd);
        }
        current->compat_state.closed_stdio=closed;
        current->compat_state.nonblock=nonblock&~cloexec;
        current->compat_state.signal_mask=mask;
        current->compat_state.pending_signals=pending;
        for(u32 signo=0;signo<64;++signo)if(ignored&(1ull<<signo))current->compat_state.actions[signo].handler=1;
    }
done:
    kfree(copy);return r;
}
static iptr linux_access(uptr name,u64 mode) {
    if(mode&~7ull)return -L_EINVAL;
    char path[NV_PATH_MAX];struct nv_stat64 st;
    int r=user_string(name,path,sizeof(path));
    if(r>=0)r=fs_stat_path64(current,path,&st);
    if(r>=0 && (mode&2))r=fs_authorize(current,current->cwd,path,1);
    return linux_error(r);
}
#endif
