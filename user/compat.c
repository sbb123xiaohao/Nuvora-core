#include "runtime.h"
int user_main(const char *args) {
    if (!*args || !strcmp(args,"--help")) {
        println("Linux x86-64 process compatibility (experimental)");
        println("compat [--trace] [--jit] FILE.elf [ARGS]");
        println("Linux PIE with musl; optional syscall diagnostics on the serial port.");
        println("--jit explicitly allows this process to write executable memory.");
        println("OpenJDK 21 examples are experimental; Minecraft graphics still need work.");
        return 0;
    }
    char command[NV_ARG_MAX],quoted[NV_ARG_MAX],*argv[34];
    if (strlcpy(command,args,sizeof(command))>=sizeof(command)) return 2;
    int argc=tokenize(command,argv,ARRAY_LEN(argv));
    if(argc<1)return 2;
    u32 index=0,options=0;
    while(index<(u32)argc) {
        if(!strcmp(argv[index],"--trace"))options|=NV_COMPAT_TRACE;
        else if(!strcmp(argv[index],"--jit"))options|=NV_COMPAT_JIT;
        else break;
        ++index;
    }
    if (argc<=(int)index || quote_args(quoted,sizeof(quoted),argv,index+1,argc)<0) return 2;
    /* Keep the shell's child PID so gather/quench control the real runtime. */
    int result=call(NV_COMPAT_EXEC,(uptr)argv[index],(uptr)quoted,NV_COMPAT_LINUX_X64|options);
    report_error("Linux compatibility launch",result); return 2;
}
