#include "runtime.h"
#include "java_vm.h"
#include "java_demo.h"

struct console { bool verify; u32 at; };
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
static void usage(void) {
    println("Nuvora Java bootstrap (experimental Java 21 bytecode subset)");
    println("java --demo [ARGS] | java FILE.class [ARGS]");
    println("java --version | java --help");
    println("Single class, int/long, arrays, methods and basic strings.");
    println("JARs, full Java SE, JNI/LWJGL and Minecraft are not runnable yet.");
}
int user_main(const char *args) {
    if (!*args) { usage(); return 0; }
    char command[NV_ARG_MAX],*argv[33];
    if (strlcpy(command,args,sizeof(command))>=sizeof(command)) return 2;
    int argc=tokenize(command,argv,ARRAY_LEN(argv));
    if (argc<1) { usage(); return 2; }
    if (argc==1 && !strcmp(argv[0],"--version")) {
        println("Nuvora Java bootstrap 0.1 / class format 52..65 (Java 8..21)");
        println("Experimental interpreter; not a Java SE 21 runtime."); return 0;
    }
    if (argc==1 && !strcmp(argv[0],"--help")) { usage(); return 0; }
    bool self_test=!strcmp(argv[0],"--self-test"),demo=!strcmp(argv[0],"--demo") || self_test;
    if (self_test && argc!=1) return 2;
    if (!demo && argv[0][0]=='-') {
        println("Java: JAR/classpath/options are not implemented; pass one .class or --demo."); return 2;
    }
    const u8 *bytes=java_demo; u32 length=sizeof(java_demo);
    if (!demo) {
        int fd=open_file(argv[0],NV_READ);
        if (fd<0) { report_error("Java class",fd); return 2; }
        struct nv_stat64 st;
        int status=stat_file64(fd,&st);
        if (status || st.kind!=NV_FILE || st.size<10 || st.size>NV_JAVA_CLASS_MAX) {
            close_file(fd); println("Java: class must be a regular file, 10 bytes to 256 KiB."); return 2;
        }
        length=(u32)st.size;
        u8 *buffer=grow((length+NV_PAGE-1)/NV_PAGE);
        if ((iptr)buffer<0) { close_file(fd); println("Java: not enough memory to read class."); return 3; }
        u32 at=0;
        while (at<length) {
            int n=take(fd,buffer+at,length-at);
            if (n<=0 || (u32)n>length-at) { close_file(fd); println("Java: class read was incomplete."); return 2; }
            at+=n;
        }
        close_file(fd); bytes=buffer;
    }
    void *arena=grow(NV_JAVA_ARENA/NV_PAGE);
    if ((iptr)arena<0) { println("Java: cannot allocate 2 MiB runtime arena."); return 3; }
    struct console console={.verify=self_test};
    struct nv_java_host host={&console,java_output,java_poll}; struct nv_java_result result;
    int status=nv_java_run(bytes,length,arena,NV_JAVA_ARENA,(const char *const *)(argv+1),argc-1,
                           NV_JAVA_STEPS,&host,&result);
    if (status) {
        print("Java stopped: "); println(result.message);
        print("method="); print_u32(result.method); print(" pc="); print_u32(result.pc);
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
