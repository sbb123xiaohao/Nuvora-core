/* Compile each actual editor against controlled 64-bit filesystem calls. */
#include <assert.h>
#include <stdio.h>
#include <nv/sdk.h>
#include <nv/string.h>
static u64 file_size,file_pos;
static int seek_error,rewind_error,read_error,sync_error,info_error;
static u32 reads,closes,writes,syncs;
static iptr editor_syscall(u32 op,uptr a,uptr b,uptr c) {
    if (op==NV_OPEN) { file_pos=0;return 3; }
    if (op==NV_CLOSE) { assert(a==3);++closes;return 0; }
    if (op==NV_SEEK64) {
        struct nv_seek64 *io=(void *)b;assert(a==3);
        if (io->origin==2 && seek_error) return seek_error;
        if (io->origin==0 && rewind_error) return rewind_error;
        file_pos=io->origin==2?file_size:0;io->position=file_pos;return 0;
    }
    if (op==NV_TAKE) {
        assert(a==3);++reads;if (read_error) return read_error;
        u32 amount=(u32)MIN((u64)c,file_size-file_pos);
        memset((void *)b,'x',amount);file_pos+=amount;return amount;
    }
    if (op==NV_EMIT) { assert(a==3);++writes;return c; }
    if (op==NV_REPLACE || op==NV_REMOVE) return 0;
    if (op==NV_CLOCK64) return 100;
    if (op==NV_INFO) {
        if (info_error) return info_error;
        *(struct nv_info *)a=(struct nv_info){.disk_present=1};return 0;
    }
    if (op==NV_CONTROL) { assert(a==NV_CTL_SYNC);++syncs;return sync_error; }
    if (op==NV_GETCWD) { strlcpy((char *)a,"/home",b);return 0; }
    if (op==NV_LIST64 || op==NV_VOLUME) return 0;
    assert(!"unexpected editor syscall");return -NV_ENOSYS;
}
#define nv_syscall editor_syscall
#include "../user/runtime.c"
#ifdef NV_TEST_FOLIO
#include "../user/document.c"
#include "../user/folio.c"
#else
#include "../user/desktop.c"
#endif

static int load(const char *name) {
#ifdef NV_TEST_FOLIO
    return read_document(name);
#else
    return editor_load(name);
#endif
}
static void seed(void) {
    file_size=file_pos=reads=closes=writes=syncs=0;
    seek_error=rewind_error=read_error=sync_error=info_error=0;
#ifdef NV_TEST_FOLIO
    doc_init(&doc);assert(!doc_type(&doc,'K'));strlcpy(path,"/home/keep.txt",sizeof(path));
    saved_hash=0;hist_count=2;
#else
    strlcpy(editor_text,"Keep",sizeof(editor_text));editor_length=4;editor_cursor=2;editor_dirty=true;
    strlcpy(editor_path,"/home/keep.txt",sizeof(editor_path));
    editor_mode=DESKTOP_EDIT_NORMAL;editor_next_action=0;
    windows[DESKTOP_EDITOR].open=true;
#endif
}
static void preserved(void) {
#ifdef NV_TEST_FOLIO
    assert(doc.len==1 && doc.cell[0].ch=='K' && hist_count==2 && saved_hash==0);
    assert(!strcmp(path,"/home/keep.txt"));
#else
    assert(!strcmp(editor_text,"Keep") && editor_length==4 && editor_cursor==2 && editor_dirty);
    assert(!strcmp(editor_path,"/home/keep.txt"));
#endif
}
static void large_files(void) {
    const u64 sizes[]={1ull<<32,(1ull<<32)+1,(1ull<<32)+128*1024,1ull<<31,128*1024+1,NV_FILE_MAX64};
    for (u32 i=0;i<ARRAY_LEN(sizes);++i) {
        seed();file_size=sizes[i];
        assert(load("/home/huge.txt")==-NV_E2BIG);
        assert(!reads && closes==1);preserved();
    }
    puts("PASS editor large-file rejection: 2 GiB, 4 GiB wrap, near-limit lengths; no reads or document changes");
}
static void io_errors(void) {
    seed();file_size=4;seek_error=-NV_EIO;
    assert(load("/home/read.txt")==-NV_EIO && !reads && closes==1);preserved();
    seed();file_size=0;rewind_error=-NV_EIO;
    assert(load("/home/read.txt")==-NV_EIO && !reads && closes==1);preserved();
    seed();file_size=4;read_error=-NV_EIO;
    assert(load("/home/read.txt")==-NV_EIO && reads==1 && closes==1);preserved();
    seed();file_size=4;assert(!load("/home/read.txt") && closes==1);
#ifdef NV_TEST_FOLIO
    assert(doc.len==4 && doc.cell[0].ch=='x' && !dirty());
#else
    assert(editor_length==4 && !strcmp(editor_text,"xxxx") && !editor_dirty);
#endif
    seed();file_size=0;assert(!load("/home/empty.txt") && !reads && closes==1);
    puts("PASS editor I/O: original seek/read errors retained, failed load preserves document, empty and small files open");
}
#ifndef NV_TEST_FOLIO
static void failed_save(void) {
    seed();sync_error=-NV_EIO;
    assert(editor_save_target("/home/keep.txt")==-NV_EIO && writes==1 && syncs==1);
    preserved();
    close_window(DESKTOP_EDITOR);
    assert(windows[DESKTOP_EDITOR].open && editor_mode==DESKTOP_EDIT_CLOSE && editor_next_action==2);
    sync_error=0;assert(!editor_save() && !editor_dirty && !windows[DESKTOP_EDITOR].open && syncs==2);
    seed();info_error=-NV_EIO;
    assert(editor_save_target("/home/keep.txt")==-NV_EIO && writes==1 && !syncs);
    preserved();
    puts("PASS editor failed disk sync: retained unsaved state and close confirmation, successful retry completes pending close");
}
#endif
int main(int argc,char **argv) {
    if (argc==1 || !strcmp(argv[1],"large")) large_files();
    if (argc==1 || !strcmp(argv[1],"io")) io_errors();
#ifndef NV_TEST_FOLIO
    if (argc==1 || !strcmp(argv[1],"save")) failed_save();
#endif
    return 0;
}
