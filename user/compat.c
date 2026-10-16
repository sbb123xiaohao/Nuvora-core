#include "runtime.h"
int user_main(const char *args) {
    if (!*args || !strcmp(args,"--help")) {
        println("Linux x86-64 static-process compatibility (experimental)");
        println("compat FILE.elf [ARGS]");
        println("Static PIE with musl; file I/O, TLS, memory and monotonic clocks.");
        println("Dynamic JVMs, threads, signals and Minecraft still need platform work.");
        return 0;
    }
    char command[NV_ARG_MAX],quoted[NV_ARG_MAX],*argv[34];
    if (strlcpy(command,args,sizeof(command))>=sizeof(command)) return 2;
    int argc=tokenize(command,argv,ARRAY_LEN(argv));
    if (argc<1 || quote_args(quoted,sizeof(quoted),argv,1,argc)<0) return 2;
    /* Keep the shell's child PID so gather/quench control the real runtime. */
    int result=call(NV_COMPAT_EXEC,(uptr)argv[0],(uptr)quoted,NV_COMPAT_LINUX_X64);
    report_error("Linux compatibility launch",result); return 2;
}
