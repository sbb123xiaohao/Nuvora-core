#ifndef NV_FILES_MODEL_H
#define NV_FILES_MODEL_H
#include <nv/abi.h>
#include <nv/string.h>

enum { FILE_SORT_NAME, FILE_SORT_KIND, FILE_SORT_SIZE, FILE_SORT_COUNT };
static inline char files_lower(char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; }
static inline int files_name_compare(const char *a,const char *b) {
    while (*a && files_lower(*a)==files_lower(*b)) { ++a;++b; }
    return (u8)files_lower(*a)-(u8)files_lower(*b);
}
static inline bool files_matches(const char *name,const char *query) {
    if (!*query) return true;
    for (;*name;++name) {
        const char *a=name,*b=query;
        while (*a && *b && files_lower(*a)==files_lower(*b)) { ++a;++b; }
        if (!*b) return true;
    }
    return false;
}
static inline u32 files_category(const struct nv_dirent64 *entry) {
    if (entry->kind==NV_DIR) return 0;
    const char *ext=entry->name;
    for (const char *p=entry->name;*p;++p) if (*p=='.') ext=p;
    if (!files_name_compare(ext,".txt") || !files_name_compare(ext,".md") ||
        !files_name_compare(ext,".nvd")) return 1;
    if (!files_name_compare(ext,".mp3") || !files_name_compare(ext,".mp2") ||
        !files_name_compare(ext,".wav") || !files_name_compare(ext,".flac")) return 2;
    if (!files_name_compare(ext,".mpg") || !files_name_compare(ext,".mpeg")) return 3;
    return 4;
}
static inline int files_compare(const struct nv_dirent64 *a,const struct nv_dirent64 *b,u32 sort) {
    if ((a->kind==NV_DIR)!=(b->kind==NV_DIR)) return a->kind==NV_DIR?-1:1;
    if (sort==FILE_SORT_KIND && files_category(a)!=files_category(b))
        return files_category(a)<files_category(b)?-1:1;
    if (sort==FILE_SORT_SIZE && a->kind!=NV_DIR && a->size!=b->size)
        return a->size>b->size?-1:1;
    int order=files_name_compare(a->name,b->name);
    return order?order:strcmp(a->name,b->name);
}
static inline void files_sort(struct nv_dirent64 *items,u32 count,u32 sort) {
    for (u32 i=1;i<count;++i) {
        struct nv_dirent64 item=items[i];u32 at=i;
        while (at && files_compare(&item,&items[at-1],sort)<0) {
            items[at]=items[at-1];--at;
        }
        items[at]=item;
    }
}
/* Version one kept the last word reserved. Version two uses eleven bits for
 * volume, card/list mode, sorting and dot-file visibility; unknown bits fail. */
struct desktop_preferences { u32 magic,theme,idle_minutes,options; };
static inline bool files_preferences_valid(const struct desktop_preferences *p) {
    if (p->theme>2 || (p->idle_minutes!=1 && p->idle_minutes!=5 && p->idle_minutes!=15)) return false;
    if (p->magic==0x3155494e) return !p->options;
    return p->magic==0x3255494e && !(p->options&~0x7ffu) &&
           (p->options&127u)<=100 && ((p->options>>8)&3u)<FILE_SORT_COUNT;
}
#endif
