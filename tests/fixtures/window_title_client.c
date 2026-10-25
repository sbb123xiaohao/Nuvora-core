/* Native test client: a window title must not reserve a launcher identity. */
#include "runtime.h"

static int checkpoint(const char *path,const char *bytes,u32 length) {
    int fd=open_file(path,NV_WRITE|NV_CREATE|NV_TRUNC);
    if (fd<0) return fd;
    int r=emit(fd,bytes,length),closed=close_file(fd);
    if (r!=(int)length || closed<0) return -NV_EIO;
    return control(NV_CTL_SYNC,0);
}
int user_main(const char *args) {
    /* Let the shell that starts us close before we request the chosen title. */
    nap(5000);
    struct nv_window_create w={.width=480,.height=270};
    strlcpy(w.title,args,sizeof(w.title));
    if (nv_window_create(&w)<0) return 1;
    struct nv_window_id id={w.id};
    if (nv_window_call(NV_WINDOW_BIND_STDIO,&id)<0) return 2;
    println("Unregistered window title fixture");
    char path[NV_PATH_MAX];default_home_file(path,"title-client.txt");
    char record[256]="READY\n";u32 used=6;
    if (checkpoint(path,record,used)<0) return 3;
    for (;;) {
        struct nv_window_event e;
        int r=nv_window_poll(w.id,&e);
        if (r<0) return 4;
        if (r>0 && e.type==NV_WINDOW_EVENT_CLOSE) break;
        u32 key=e.key&4095u; /* Native key events also carry modifier/direct flags. */
        if (r>0 && e.type==NV_WINDOW_EVENT_KEY && key<128 && used+1<sizeof(record)) {
            record[used++]=(char)key;
            if (checkpoint(path,record,used)<0) return 5;
        }
        nap(1);
    }
    nv_window_destroy(w.id);
    return 0;
}
