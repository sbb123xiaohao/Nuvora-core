#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>

#define CHECK(x) do {if(!(x)){printf("DYNAMIC FAIL line %d errno %d: %s\n",__LINE__,errno,#x);return 10;}}while(0)
static unsigned char signal_stack[32768];
static volatile sig_atomic_t signals_seen,signal_stack_seen;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int threads_ready,threads_go,thread_sum;
static _Atomic int thread_failed,thread_signal_seen;
static __thread int thread_value=11;
static int (*shared_tls_probe)(int);
static void thread_signal(int signo){if(signo==SIGUSR1)atomic_fetch_add(&thread_signal_seen,1);}
static void *worker(void *argument) {
    int number=(int)(uintptr_t)argument;
    if(thread_value!=11 || !shared_tls_probe || shared_tls_probe(0)!=23 || shared_tls_probe(number)!=23+number)atomic_store(&thread_failed,1);
    thread_value=number;
    pthread_mutex_lock(&mutex);++threads_ready;pthread_cond_broadcast(&condition);
    while(!threads_go)pthread_cond_wait(&condition,&mutex);
    pthread_mutex_unlock(&mutex);
    for(int i=0;i<1000;++i) {
        pthread_mutex_lock(&mutex);++thread_sum;pthread_mutex_unlock(&mutex);
        if(thread_value!=number)atomic_store(&thread_failed,1);
    }
    return argument;
}
static void signal_handler(int signo,siginfo_t *info,void *context) {
    unsigned char marker;
    if((uintptr_t)&marker>=(uintptr_t)signal_stack && (uintptr_t)&marker<(uintptr_t)signal_stack+sizeof(signal_stack))++signal_stack_seen;
    if(info->si_signo==signo)++signals_seen;
    if(signo==SIGILL) {
        ((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP]+=2;
        __asm__ volatile("pxor %%xmm7,%%xmm7" ::: "xmm7");
    }
}

int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--thread-check")) {
        void *library=dlopen("/home/libnvprobe.so",RTLD_NOW);CHECK(library);
        shared_tls_probe=dlsym(library,"nv_dynamic_probe");CHECK(shared_tls_probe);
        struct sigaction action={.sa_handler=thread_signal};CHECK(!sigaction(SIGUSR1,&action,NULL));
        pthread_t threads[3];
        for(int i=0;i<3;++i)CHECK(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)(i+1)));
        pthread_mutex_lock(&mutex);while(threads_ready!=3)pthread_cond_wait(&condition,&mutex);pthread_mutex_unlock(&mutex);
        CHECK(!pthread_kill(threads[0],SIGUSR1));
        for(int i=0;i<10000 && !atomic_load(&thread_signal_seen);++i)sched_yield();
        CHECK(atomic_load(&thread_signal_seen)==1);
        pthread_mutex_lock(&mutex);threads_go=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&mutex);
        for(int i=0;i<3;++i){void *result;CHECK(!pthread_join(threads[i],&result) && result==(void *)(uintptr_t)(i+1));}
        CHECK(!atomic_load(&thread_failed) && thread_value==11 && thread_sum==3000 && shared_tls_probe(0)==42);
        CHECK(!dlclose(library));
        puts("THREAD PASS: pthread TLS/DSO TLS, mutex/condition, join and targeted signal delivery");
    }
    if(argc>=2 && !strcmp(argv[1],"--signal-check")) {
        stack_t stack={.ss_sp=signal_stack,.ss_size=sizeof(signal_stack)};
        CHECK(!sigaltstack(&stack,NULL));
        struct sigaction action={.sa_sigaction=signal_handler,.sa_flags=SA_SIGINFO|SA_ONSTACK};
        CHECK(!sigemptyset(&action.sa_mask) && !sigaction(SIGUSR1,&action,NULL) && !sigaction(SIGILL,&action,NULL));
        sigset_t mask;CHECK(!sigemptyset(&mask) && !sigaddset(&mask,SIGUSR1) && !sigprocmask(SIG_BLOCK,&mask,NULL));
        CHECK(!raise(SIGUSR1) && !signals_seen);
        CHECK(!sigprocmask(SIG_UNBLOCK,&mask,NULL) && signals_seen==1 && signal_stack_seen==1);
        unsigned long before=0x123456789abcdef0ul,after;
        __asm__ volatile("movq %1,%%xmm7; ud2; movq %%xmm7,%0":"=r"(after):"r"(before):"xmm7","memory");
        CHECK(after==before && signals_seen==2 && signal_stack_seen==2);
        puts("SIGNAL PASS: masked self signal, alternate stack, SIGILL context recovery and XMM restoration");
    }
    if(argc==2 && !strcmp(argv[1],"--exec")) {
        int fd=open("/home/libnvprobe.so",O_RDONLY|O_CLOEXEC);CHECK(fd>=3);
        char fd_env[32];snprintf(fd_env,sizeof(fd_env),"NV_EXEC_FD=%d",fd);
        char *environment[]={"NV_EXEC_PROBE=ok",fd_env,NULL};
        char *arguments[]={"re-executed","two words","quote\"slash\\","",NULL};
        errno=0;CHECK(execve("/home/absent.elf",arguments,environment)==-1 && errno==ENOENT);
        CHECK(fcntl(fd,F_GETFD)==FD_CLOEXEC);
        execve("/home/dynamic.elf",arguments,environment);CHECK(0);
    }
    if(argc==4 && !strcmp(argv[0],"re-executed")) {
        CHECK(!strcmp(argv[1],"two words") && !strcmp(argv[2],"quote\"slash\\") && !argv[3][0]);
        CHECK(getenv("NV_EXEC_PROBE") && !strcmp(getenv("NV_EXEC_PROBE"),"ok") && !getenv("LANG"));
        CHECK(!strcmp((const char *)getauxval(AT_EXECFN),"/home/dynamic.elf"));
        errno=0;CHECK(getenv("NV_EXEC_FD") && fcntl(atoi(getenv("NV_EXEC_FD")),F_GETFD)==-1 && errno==EBADF);
        puts("EXEC PASS: argv0, quoted/empty-safe arguments, exact environment, AT_EXECFN, failure preservation and CLOEXEC");
    }
    if(argc==2 && !strcmp(argv[1],"--guard-check")) {
        size_t size=(size_t)16*1024*1024*1024;
        unsigned char *p=mmap(NULL,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(p!=MAP_FAILED);
        CHECK(!mprotect(p+4096,4096,PROT_READ|PROT_WRITE));p[4096]=73;
        CHECK(!mprotect(p+4096,4096,PROT_NONE));
        CHECK(!mprotect(p+4096,4096,PROT_READ|PROT_WRITE) && p[4096]==73);
        CHECK(!munmap(p,size));
        int fd=open("/home/libnvprobe.so",O_RDONLY);CHECK(fd>=3);
        p=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,0);CHECK(p!=MAP_FAILED && !memcmp(p,"\177ELF",4));
        errno=0;CHECK(mprotect(p,4096,PROT_READ|PROT_WRITE)==-1 && errno==ENOTSUP);
        CHECK(!munmap(p,4096) && !close(fd));
        puts("GUARD PASS: sparse 16 GiB reservation, retained protected-page data and read-only file snapshot");
    }
    if(argc==2 && !strcmp(argv[1],"--jit-check")) {
        errno=0;unsigned char *p=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==MAP_FAILED) {CHECK(errno==EINVAL);puts("W^X DEFAULT PASS");}
        else {
            static const unsigned char code[]={0xb8,42,0,0,0,0xc3};memcpy(p,code,sizeof(code));
            CHECK(((int (*)(void))p)()==42 && !mprotect(p,4096,PROT_READ|PROT_EXEC));
            CHECK(((int (*)(void))p)()==42 && !munmap(p,4096));puts("JIT OPT IN PASS: generated x86-64 code executed");
        }
    }

    {
        const char *path=argc==2 && argv[1][0]!='-'?argv[1]:argc==3 && argv[1][0]=='-'?argv[2]:"/home/libnvprobe.so";
        void *library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        if (!library) {printf("FAIL dlopen: %s\n",dlerror());return 1;}
        int (*probe)(int)=dlsym(library,"nv_dynamic_probe");
        if (!probe || probe(0)!=42 || probe(5)!=47) {
            puts("FAIL shared-library constructor/TLS/symbol lookup");return 2;
        }
        if (dlclose(library)) {puts("FAIL dlclose");return 3;}
    }
    if (dlopen("/home/missing-library.so",RTLD_NOW)) {
        puts("FAIL missing-library error");return 4;
    }
    puts("DYNAMIC PASS: real musl linker, shared-library relocation, TLS, constructor, dlsym and dlclose");
    return 0;
}
