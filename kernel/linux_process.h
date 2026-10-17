#ifndef NV_LINUX_PROCESS_H
#define NV_LINUX_PROCESS_H
/* Copy every pointer/string before replacing the old address space. Native
 * argument and filesystem limits remain explicit; failed exec is atomic. */
struct linux_exec_data {
    char path[NV_PATH_MAX],arg0[NV_PATH_MAX],arguments[NV_ARG_MAX],environment[4096];
};
static int linux_vector(uptr pointer,u32 index,uptr *value) {
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
    strlcpy(copy->arg0,copy->path,sizeof(copy->arg0));
    u32 used=0,environment_bytes=0;
    for(u32 i=0;argv;++i) {
        uptr value;r=linux_vector(argv,i,&value);if(r<0)goto done;
        if(!value)break;
        if(i==34) {r=-L_E2BIG;goto done;}
        char word[NV_ARG_MAX];
        r=user_string(value,word,sizeof(word));if(r<0) {r=linux_error(r);goto done;}
        if(!i) {
            if(strlcpy(copy->arg0,word,sizeof(copy->arg0))>=sizeof(copy->arg0)) {r=-L_E2BIG;goto done;}
            continue;
        }
        if(used && used+1<sizeof(copy->arguments))copy->arguments[used++]=' ';
        if(used+2>=sizeof(copy->arguments)) {r=-L_E2BIG;goto done;}
        copy->arguments[used++]='"';
        for(const char *p=word;*p;++p) {
            u32 bytes=(*p=='"' || *p=='\\')?2:1;
            if(used+bytes+2>=sizeof(copy->arguments)) {r=-L_E2BIG;goto done;}
            if(bytes==2)copy->arguments[used++]='\\';
            copy->arguments[used++]=*p;
        }
        copy->arguments[used++]='"';
    }
    copy->arguments[used]=0;
    for(u32 i=0;envp;++i) {
        uptr value;r=linux_vector(envp,i,&value);if(r<0)goto done;
        if(!value)break;
        if(i==32 || environment_bytes==sizeof(copy->environment)) {r=-L_E2BIG;goto done;}
        r=user_string(value,copy->environment+environment_bytes,sizeof(copy->environment)-environment_bytes);
        if(r<0) {r=linux_error(r);goto done;}
        environment_bytes+=strlen(copy->environment+environment_bytes)+1;
    }
    struct linux_startup startup={copy->arg0,copy->environment,environment_bytes};
    u16 closed=current->linux_compat.closed_stdio,cloexec=current->linux_compat.cloexec,nonblock=current->linux_compat.nonblock;
    u64 mask=current->linux_compat.signal_mask;
    u64 pending=current->linux_compat.pending_signals,ignored=0;
    for(u32 signo=0;signo<64;++signo)if(current->linux_compat.actions[signo].handler==1)ignored|=1ull<<signo;
    u32 options=(current->linux_compat.trace?NV_COMPAT_TRACE:0)|(current->linux_compat.jit?NV_COMPAT_JIT:0);
    r=linux_error(task_exec_linux_start(copy->path,copy->arguments,options,&startup));
    if(!r) {
        for(u32 fd=0;fd<NV_OPEN_MAX;++fd)if(cloexec&(1u<<fd)) {
            if(fd<3)closed|=1u<<fd;else fs_close(current,fd);
        }
        current->linux_compat.closed_stdio=closed;
        current->linux_compat.nonblock=nonblock&~cloexec;
        current->linux_compat.signal_mask=mask;
        current->linux_compat.pending_signals=pending;
        for(u32 signo=0;signo<64;++signo)if(ignored&(1ull<<signo))current->linux_compat.actions[signo].handler=1;
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
