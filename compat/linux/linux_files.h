#ifndef NV_LINUX_FILES_H
#define NV_LINUX_FILES_H
static iptr linux_duplicate(u64 fd,u64 minimum,bool cloexec) {
    if(fd>=NV_OPEN_MAX || (fd>=3 && current->fd[fd].node<0) ||
       (fd<3 && (current->compat_state.closed_stdio&(1u<<fd))))return -L_EBADF;
    if(minimum>=NV_OPEN_MAX)return -L_EINVAL;
    if(fd<3)return -L_ENOTSUP; /* Console redirection needs a native endpoint. */
    int fresh=fs_duplicate(current,fd,minimum);
    if(fresh<0)return fresh==-NV_ENOSPC?-L_EMFILE:linux_error(fresh);
    u64 bit=1ull<<fresh;
    current->compat_state.cloexec=(current->compat_state.cloexec&~bit)|(cloexec?bit:0);
    current->compat_state.nonblock=(current->compat_state.nonblock&~bit)|
        ((current->compat_state.nonblock&(1ull<<fd))?bit:0);
    return fresh;
}
static iptr linux_getdents(u64 fd,uptr output,u64 count) {
    if(fd<3 || fd>=NV_OPEN_MAX || current->fd[fd].node<0)return -L_EBADF;
    struct descriptor *d=&current->fd[fd];
    if(fs_kind(d->node)!=NV_DIR)return -L_ENOTDIR;
    char path[NV_PATH_MAX];int r=fs_path(d->node,path,sizeof(path));
    if(r>=0)r=fs_authorize(current,0,path,0);
    if(r<0)return linux_error(r);
    u32 bytes=MIN(count,16384ull),used=0;
    if(!user_range(current->pd,output,bytes,true))return -L_EFAULT;
    u64 index=d->offset;
    if(index>0xffffffffull)return -L_EINVAL;
    for(;;) {
        struct nv_dirent64 entry={.kind=NV_DIR};
        if(index<2)strlcpy(entry.name,index?"..":".",sizeof(entry.name));
        else {
            r=fs_list64(d->node,".",(u32)index-2,&entry);
            if(r<=0) {if(r<0 && !used)return linux_error(r);break;}
        }
        u16 size=ALIGN_UP(19+strlen(entry.name)+1,8);
        if(size>bytes-used) {if(!used)return -L_EINVAL;break;}
        u8 record[64]={0};
        int node=fs_lookup(d->node,entry.name);
        if(node<0) {if(!used)return linux_error(node);break;}
        u64 inode=(u64)node+1,next=index+1;
        memcpy(record,&inode,8);memcpy(record+8,&next,8);memcpy(record+16,&size,2);
        record[18]=entry.kind==NV_DIR?4:entry.kind==NV_DEVICE?2:8;
        memcpy(record+19,entry.name,strlen(entry.name)+1);
        memcpy((void *)(output+used),record,size);used+=size;++index;
    }
    d->offset=index;fs_descriptor_sync(current,fd);return used;
}
/* Translate path operations into native services and native account checks.
 * Permissions remain the native per-account policy; no foreign ownership or
 * metadata can grant access that the native filesystem would deny. */
static iptr linux_path_operation(u32 operation,uptr name) {
    char path[NV_PATH_MAX];int r=user_string(name,path,sizeof(path));
    if(r<0)return linux_error(r);
    r=fs_authorize(current,current->cwd,path,operation==83?1:operation==80?0:2);
    if(r<0)return linux_error(r);
    if(operation==83)return linux_error(fs_mkdir(current->cwd,path));
    int node=fs_lookup(current->cwd,path);if(node<0)return linux_error(node);
    int kind=fs_kind(node);
    if(operation==80) {
        if(kind!=NV_DIR)return -L_ENOTDIR;
        current->cwd=node;return 0;
    }
    if(operation==84 && kind!=NV_DIR)return -L_ENOTDIR;
    if(operation==87 && kind==NV_DIR)return -L_EISDIR;
    return linux_error(fs_remove(current->cwd,path));
}
static iptr linux_rename(uptr source,uptr destination) {
    char from[NV_PATH_MAX],to[NV_PATH_MAX];
    int r=user_string(source,from,sizeof(from));
    if(r>=0)r=user_string(destination,to,sizeof(to));
    if(r>=0)r=fs_authorize(current,current->cwd,from,2);
    if(r>=0)r=fs_authorize(current,current->cwd,to,1);
    if(r<0)return linux_error(r);
    int node=fs_lookup(current->cwd,from);if(node<0)return linux_error(node);
    return linux_error(fs_kind(node)==NV_FILE?fs_replace(current->cwd,from,to):fs_move(current->cwd,from,to));
}
#endif
