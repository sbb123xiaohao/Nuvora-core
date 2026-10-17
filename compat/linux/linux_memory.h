#ifndef NV_LINUX_MEMORY_H
#define NV_LINUX_MEMORY_H
/* Private mmap snapshots and virtual reservations. Metadata and replacement
 * pages are prepared before commit; failures preserve all old mappings/data. */
enum { L_MAP_SHARED=1,L_MAP_PRIVATE=2,L_MAP_FIXED=16,L_MAP_ANON=32,L_MAP_NOREPLACE=0x100000 };
struct linux_plan {struct linux_vma maps[LINUX_VMAS];u32 count;};
static u64 mapped_bytes(void) {return vm_page_count(current->pd)*PAGE;}
static bool linux_length(uptr at,u64 size) {
    return size && size<=LINUX_VIRTUAL_MAX && !(at%PAGE) && size<=~(uptr)0-at-PAGE+1;
}
static u32 linux_permissions(u32 prot) {
    return !prot?P_NOACCESS:((prot&2)?P_WRITE:0)|((prot&4)?P_EXEC:0)|(prot==7?P_JIT:0);
}
static bool linux_protection(u64 prot) {return prot==0 || prot==1 || prot==3 || prot==5 || (prot==7 && current->compat_state.jit);}
static struct linux_plan *linux_plan_new(void) {
    struct linux_plan *p=kmalloc(sizeof(*p));if(p)memset(p,0,sizeof(*p));return p;
}
static bool linux_plan_add(struct linux_plan *p,struct linux_vma v) {
    if(v.start>=v.end)return true;
    if(p->count) {
        struct linux_vma *last=&p->maps[p->count-1];
        if(last->end>v.start)return false;
        if(last->end==v.start && last->protection==v.protection && last->flags==v.flags) {
            last->end=v.end;return true;
        }
    }
    if(p->count==LINUX_VMAS)return false;
    p->maps[p->count++]=v;return true;
}
static bool linux_plan_replace(struct linux_plan *p,uptr start,uptr end,const struct linux_vma *fresh) {
    bool inserted=!fresh;
    for(u32 i=0;i<LINUX_VMAS && current->compat_state.maps[i].start;++i) {
        struct linux_vma old=current->compat_state.maps[i];
        if(old.end<=start) {if(!linux_plan_add(p,old))return false;continue;}
        if(old.start>=end) {
            if(!inserted) {if(!linux_plan_add(p,*fresh))return false;inserted=true;}
            if(!linux_plan_add(p,old))return false;
            continue;
        }
        if(old.start<start && !linux_plan_add(p,(struct linux_vma){old.start,start,old.protection,old.flags}))return false;
        if(!inserted) {if(!linux_plan_add(p,*fresh))return false;inserted=true;}
        if(old.end>end && !linux_plan_add(p,(struct linux_vma){end,old.end,old.protection,old.flags}))return false;
    }
    return inserted || linux_plan_add(p,*fresh);
}
static bool linux_plan_protect(struct linux_plan *p,uptr start,uptr end,u32 prot) {
    uptr covered=start;
    for(u32 i=0;i<LINUX_VMAS && current->compat_state.maps[i].start;++i) {
        struct linux_vma old=current->compat_state.maps[i];
        if(old.end<=start || old.start>=end) {if(!linux_plan_add(p,old))return false;continue;}
        uptr a=MAX(old.start,start),b=MIN(old.end,end);
        if(a!=covered)return false;
        covered=b;
        if(old.start<a && !linux_plan_add(p,(struct linux_vma){old.start,a,old.protection,old.flags}))return false;
        if(!linux_plan_add(p,(struct linux_vma){a,b,prot,old.flags}))return false;
        if(b<old.end && !linux_plan_add(p,(struct linux_vma){b,old.end,old.protection,old.flags}))return false;
    }
    return covered==end;
}
static bool linux_owned(uptr start,uptr end,u32 required) {
    uptr covered=start;
    for(u32 i=0;i<LINUX_VMAS && current->compat_state.maps[i].start && covered<end;++i) {
        const struct linux_vma *m=&current->compat_state.maps[i];
        if(m->end<=covered)continue;
        if(m->start>covered || (required && !(m->flags&required)))return false;
        covered=MIN(end,m->end);
    }
    return covered==end;
}
static bool linux_overlap(uptr start,uptr end) {
    for(u32 i=0;i<LINUX_VMAS && current->compat_state.maps[i].start;++i) {
        const struct linux_vma *m=&current->compat_state.maps[i];
        if(m->start<end && start<m->end)return true;
    }
    return false;
}
static uptr linux_find_hole(uptr hint,uptr size) {
    uptr at=hint;
    for(u32 i=0;i<LINUX_VMAS && current->compat_state.maps[i].start;++i) {
        const struct linux_vma *m=&current->compat_state.maps[i];
        if(at>=m->end)continue;
        if(at<m->start && size<=m->start-at)return at;
        at=m->end;
    }
    return at<=LINUX_MAP_END && size<=LINUX_MAP_END-at?at:0;
}
/* Empty destination page tables may be discarded after a failed prepare.
 * Existing leaves, including inaccessible retained pages, stay untouched. */
static void linux_prune_empty(uptr start,uptr end) {
    for(uptr at=start;at<end;at+=PAGE) {
        u32 flags;if(vm_page_flags(current->pd,at,&flags)<0)vm_unmap(current->pd,at);
    }
}
static int linux_commit_pages(pte_t *staged,uptr start,uptr end) {
    for(uptr at=start;at<end;at+=PAGE) {
        u32 flags;if(vm_page_flags(staged,at,&flags)<0)continue;
        int r=vm_prepare(current->pd,at);
        if(r<0) {linux_prune_empty(start,end);return r;}
    }
    for(uptr at=start;at<end;at+=PAGE) {
        u32 flags;if(vm_page_flags(staged,at,&flags)<0)continue;
        if(vm_move_page(current->pd,staged,at)<0)panic("prepared mmap transfer failed");
    }
    return 0;
}
static iptr linux_map(uptr hint,u64 length,u64 prot,u64 flags,u64 fd,u64 offset) {
    const u64 allowed=L_MAP_SHARED|L_MAP_PRIVATE|L_MAP_FIXED|L_MAP_ANON|L_MAP_NOREPLACE|0x800|0x1000|0x4000|0x20000;
    if(!length || length>LINUX_VIRTUAL_MAX || !linux_protection(prot) ||
        (flags&~allowed) || ((flags&3)!=L_MAP_PRIVATE && (flags&3)!=L_MAP_SHARED) ||
        offset%PAGE || offset>~0ull-length)return -L_EINVAL;
    bool anon=flags&L_MAP_ANON,fixed=flags&(L_MAP_FIXED|L_MAP_NOREPLACE);
    /* Read-only file snapshots also serve immutable runtime module archives.
     * Shared writable mappings and write upgrades require a coherent pager. */
    if((flags&3)==L_MAP_SHARED && (anon || (prot&2)))return -L_ENOTSUP;
    if(anon && (fd!=~0ull || offset))return -L_EINVAL;
    if(!anon) {
        if(fd<3 || fd>=NV_OPEN_MAX || current->fd[fd].node<0 || !(current->fd[fd].flags&NV_READ))return -L_EBADF;
        struct nv_stat64 st;int r=fs_stat64(current,fd,&st);
        if(r<0)return linux_error(r);
        if(st.kind!=NV_FILE)return -L_EINVAL;
    }
    uptr size=ALIGN_UP(length,PAGE),at=0;
    if(fixed) {
        if(hint%PAGE || hint<LINUX_MAP_BASE || hint>=LINUX_MAP_END || size>LINUX_MAP_END-hint)return -L_EINVAL;
        at=hint;
        if((flags&L_MAP_NOREPLACE) && linux_overlap(at,at+size))return -L_EEXIST;
    } else {
        if(hint>=LINUX_MAP_BASE && hint<LINUX_MAP_END)at=linux_find_hole(hint&~(uptr)(PAGE-1),size);
        if(!at)at=linux_find_hole(LINUX_MAP_BASE,size);
        if(!at)return -L_ENOMEM;
    }
    bool resident=!anon || prot;
    u64 old=vm_count_range(current->pd,at,at+size)*PAGE;
    if(resident && (size>LINUX_MEMORY_MAX || mapped_bytes()-old>LINUX_MEMORY_MAX-size))return -L_ENOMEM;
    struct linux_plan *plan=linux_plan_new();if(!plan)return -L_ENOMEM;
    struct linux_vma fresh={at,at+size,(u32)prot,anon?L_MAP_ANON:((flags&3)==L_MAP_SHARED?L_MAP_SHARED:0)};
    if(!linux_plan_replace(plan,at,at+size,&fresh)) {kfree(plan);return -L_ENOMEM;}
    pte_t *staged=NULL;int r=0;
    if(resident) {
        staged=vm_create();if(!staged) {kfree(plan);return -L_ENOMEM;}
        for(uptr pos=0;pos<size;pos+=PAGE) {
            r=vm_map(staged,at+pos,P_WRITE);if(r<0)break;
            if(!anon) {
                r=fs_pread64(current,fd,offset+pos,phys_ptr(vm_translate(staged,at+pos)),PAGE);
                if(r<0)break;
            }
            r=vm_protect(staged,at+pos,linux_permissions(prot));if(r<0)break;
        }
        if(r>=0)r=linux_commit_pages(staged,at,at+size);
    } else vm_unmap_range(current->pd,at,at+size);
    if(r>=0)memcpy(current->compat_state.maps,plan->maps,sizeof(plan->maps));
    vm_destroy(staged);kfree(plan);return r<0?linux_error(r):(iptr)at;
}
static iptr linux_unmap(uptr start,u64 length) {
    if(!linux_length(start,length) || start<LINUX_MAP_BASE || start>=LINUX_MAP_END)return -L_EINVAL;
    uptr size=ALIGN_UP(length,PAGE);if(size>LINUX_MAP_END-start)return -L_EINVAL;
    struct linux_plan *p=linux_plan_new();if(!p)return -L_ENOMEM;
    if(!linux_plan_replace(p,start,start+size,NULL)) {kfree(p);return -L_ENOMEM;}
    vm_unmap_range(current->pd,start,start+size);
    memcpy(current->compat_state.maps,p->maps,sizeof(p->maps));kfree(p);return 0;
}
static iptr linux_protect(uptr start,u64 length,u64 prot) {
    if(!linux_length(start,length) || !linux_protection(prot))return -L_EINVAL;
    uptr size=ALIGN_UP(length,PAGE),end=start+size;
    if(!nv_user_bounds(start,size))return -L_ENOMEM;
    if(prot&2)for(u32 i=0;i<LINUX_VMAS && current->compat_state.maps[i].start;++i) {
        const struct linux_vma *m=&current->compat_state.maps[i];
        if(start<m->end && m->start<end && (m->flags&L_MAP_SHARED))return -L_ENOTSUP;
    }
    struct linux_plan *p=linux_plan_new();if(!p)return -L_ENOMEM;
    bool owned=linux_owned(start,end,0);
    if(owned) {
        if(!linux_plan_protect(p,start,end,prot)) {kfree(p);return -L_ENOMEM;}
    } else {
        if(linux_overlap(start,end)) {kfree(p);return -L_ENOMEM;}
        for(uptr at=start;at<end;at+=PAGE) {
            u32 flags;if(vm_page_flags(current->pd,at,&flags)<0) {kfree(p);return -L_ENOMEM;}
        }
    }
    pte_t *staged=NULL;int r=0;
    if(prot && owned) {
        if(size>LINUX_MEMORY_MAX) {kfree(p);return -L_ENOMEM;}
        u64 present=vm_count_range(current->pd,start,end)*PAGE,need=size-present;
        if(mapped_bytes()>LINUX_MEMORY_MAX-need) {kfree(p);return -L_ENOMEM;}
        if(need) {
            staged=vm_create();if(!staged) {kfree(p);return -L_ENOMEM;}
            for(uptr at=start;at<end;at+=PAGE) {
                u32 flags;if(vm_page_flags(current->pd,at,&flags)==0)continue;
                r=vm_map(staged,at,linux_permissions(prot));if(r<0)break;
            }
            if(r>=0)r=linux_commit_pages(staged,start,end);
        }
    }
    if(r>=0) {
        vm_change_range(current->pd,start,end,linux_permissions(prot));
        if(owned)memcpy(current->compat_state.maps,p->maps,sizeof(p->maps));
    }
    vm_destroy(staged);kfree(p);return linux_error(r);
}
static iptr linux_advise(uptr start,u64 length,u64 advice) {
    if(!linux_length(start,length) || !nv_user_bounds(start,ALIGN_UP(length,PAGE)))return -L_EINVAL;
    uptr end=start+ALIGN_UP(length,PAGE);
    if(!linux_owned(start,end,0))return -L_ENOMEM;
    if(advice==4) {
        if(!linux_owned(start,end,L_MAP_ANON))return -L_ENOTSUP;
        vm_clear_range(current->pd,start,end);return 0;
    }
    /* Linux permits these advisory access/huge-page preferences to be ignored. */
    return advice==0 || advice==1 || advice==2 || advice==3 || advice==14 || advice==15?0:-L_EINVAL;
}
#endif
