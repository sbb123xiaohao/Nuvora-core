#include "runtime.h"
#include "java_vm.h"
#include "java_demo.h"

struct console { bool verify; u32 at; const char *root; };
/* Independently compared with OpenJDK by tests/java_compat_test.py. */
static const char expected_demo[] =
    "Nuvora Java bootstrap / class format 65\n"
    "checks=20, chunk=2672080966\n"
    "int=-2147483648, long=-9223372036854775808, flag=true, char=J\n"
    "JAVA21 DEMO PASS\n";
static int java_output(void *context,const char *bytes,u32 count) {
    struct console *out=context;
    if (out->verify) {
        if (count>sizeof(expected_demo)-1-out->at || memcmp(bytes,expected_demo+out->at,count)) return -1;
        out->at+=count;
        return 0;
    }
    while (count) {
        int n=emit(1,bytes,count);
        if (n<=0 || (u32)n>count) return -1;
        bytes+=n; count-=n;
    }
    return 0;
}
static int java_poll(void *context) { (void)context; return yield()<0?-1:0; }
static int read_class_file(const char *path,void *buffer,u32 capacity,u32 *length) {
    int fd=open_file(path,NV_READ);
    if (fd<0) return fd;
    struct nv_stat64 st;
    int status=stat_file64(fd,&st);
    if (status || st.kind!=NV_FILE || st.size<10 || st.size>NV_JAVA_CLASS_MAX) {
        close_file(fd); return -NV_EINVAL;
    }
    *length=(u32)st.size;
    if (!buffer) { close_file(fd); return 0; }
    if (capacity!=*length) { close_file(fd); return -NV_EINVAL; }
    u32 at=0;
    while (at<*length) {
        int n=take(fd,(u8 *)buffer+at,*length-at);
        if (n<=0 || (u32)n>*length-at) { close_file(fd); return -NV_EIO; }
        at+=n;
    }
    close_file(fd); return 0;
}
static int java_load(void *context,const char *name,void *buffer,u32 capacity,u32 *length) {
    struct console *console=context; char path[NV_PATH_MAX];
    if (nv_java_class_path(path,sizeof(path),console->root,name)) return -NV_EINVAL;
    return read_class_file(path,buffer,capacity,length);
}
static void usage(void) {
    println("Nuvora Java bootstrap (experimental Java 21 bytecode subset)");
    println("java --demo [ARGS] | java FILE.class [ARGS]");
    println("java -cp DIRECTORY package.Main [ARGS]");
    println("java --version | java --help");
    println("Up to 16 classes, int/long, arrays and basic strings.");
    println("JARs, full Java SE, JNI/LWJGL and Minecraft are not runnable yet.");
}
int user_main(const char *args) {
    if (!*args) { usage(); return 0; }
    char command[NV_ARG_MAX],*argv[33];
    if (strlcpy(command,args,sizeof(command))>=sizeof(command)) return 2;
    int argc=tokenize(command,argv,ARRAY_LEN(argv));
    if (argc<1) { usage(); return 2; }
    if (argc==1 && !strcmp(argv[0],"--version")) {
        println("Nuvora Java bootstrap 0.2 / class format 52..65 (Java 8..21)");
        println("Experimental interpreter; not a Java SE 21 runtime."); return 0;
    }
    if (argc==1 && !strcmp(argv[0],"--help")) { usage(); return 0; }
    bool self_test=!strcmp(argv[0],"--self-test"),demo=!strcmp(argv[0],"--demo") || self_test;
    if (self_test && argc!=1) return 2;
    char root[NV_PATH_MAX]=".",path[NV_PATH_MAX],main_name[NV_JAVA_NAME_MAX];
    const char *entry=argv[0],*expected=NULL; int first=1;
    if (!strcmp(argv[0],"-cp") || !strcmp(argv[0],"--class-path")) {
        if (argc<3 || !argv[1][0] || strlcpy(root,argv[1],sizeof(root))>=sizeof(root) ||
            strlcpy(main_name,argv[2],sizeof(main_name))>=sizeof(main_name)) { usage(); return 2; }
        for (u32 i=0;main_name[i];++i) if (main_name[i]=='.') main_name[i]='/';
        if (nv_java_class_path(path,sizeof(path),root,main_name)) {
            println("Java: invalid class name or classpath is too long."); return 2;
        }
        entry=path; expected=main_name; first=3;
    } else if (!demo && argv[0][0]=='-') {
        println("Java: JAR/options are not implemented; use .class, -cp DIRECTORY MAIN or --demo."); return 2;
    } else if (!demo) {
        /* Direct files resolve dependencies against their containing directory.
         * Packaged entry points should use -cp with the package root. */
        u32 parent=0;
        for (u32 i=0;entry[i];++i) if (entry[i]=='/') parent=i+1;
        if (parent>=sizeof(root)) return 2;
        if (parent) { memcpy(root,entry,parent); root[parent]=0; }
    }
    const u8 *bytes=java_demo; u32 length=sizeof(java_demo);
    if (!demo) {
        int status=read_class_file(entry,NULL,0,&length);
        if (status) { report_error("Java class",status); return 2; }
        u8 *buffer=grow((length+NV_PAGE-1)/NV_PAGE);
        if ((iptr)buffer<0) { println("Java: not enough memory to read class."); return 3; }
        u32 actual=0;
        if (read_class_file(entry,buffer,length,&actual) || actual!=length) {
            println("Java: class read changed or was incomplete."); return 2;
        }
        bytes=buffer;
    }
    void *arena=grow(NV_JAVA_ARENA/NV_PAGE);
    if ((iptr)arena<0) { println("Java: cannot allocate 2 MiB runtime arena."); return 3; }
    struct console console={.verify=self_test,.root=root};
    struct nv_java_host host={.context=&console,.write=java_output,.poll=java_poll,
                              .read_class=demo?NULL:java_load,.main_class=expected};
    struct nv_java_result result;
    int status=nv_java_run(bytes,length,arena,NV_JAVA_ARENA,(const char *const *)(argv+first),argc-first,
                           NV_JAVA_STEPS,&host,&result);
    if (status) {
        print("Java stopped: "); println(result.message);
        print("class="); print_u32(result.class_index); print(" method="); print_u32(result.method); print(" pc="); print_u32(result.pc);
        print(" opcode="); print_hex(result.opcode); print("\n");
        return status;
    }
    if (self_test) {
        if (console.at!=sizeof(expected_demo)-1) return 4;
        println("JAVA21 GUEST PASS: real class bytes, objects, arrays, strings and arithmetic");
        return 74;
    }
    return 0;
}
