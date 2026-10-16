#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <time.h>
static __thread uint64_t tls=0x12345678abcdeff0ull;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s (errno=%d)\n",__LINE__,#x,errno); return 71; } } while(0)
int main(int argc,char **argv) {
    if (argc>1 && !strcmp(argv[1],"fault")) { *(volatile int *)(uintptr_t)1=1; return 72; }
    if (argc>1 && !strcmp(argv[1],"stress")) {
        for(int i=0;i<40;i++) { CHECK(tls==0x12345678abcdeff0ull+(uint64_t)i);tls++;usleep(10000); }
        puts("TLS CONTEXT PASS");return 0;
    }
    if (argc>1 && !strcmp(argv[1],"private")) {
        errno=0; CHECK(open("/home/.system/accounts.db",O_RDONLY)<0 && errno==EACCES);
        FILE *proof=fopen("compat-privacy.txt","w");CHECK(proof);
        CHECK(fprintf(proof,"ACCOUNT DATABASE ACCESS DENIED; uid=%lu\n",(unsigned long)getuid())>0);
        CHECK(!fclose(proof));
        puts("COMPAT PRIVATE ACCESS DENIED");return 0;
    }
    CHECK(tls==0x12345678abcdeff0ull);tls++;
    CHECK(tls==0x12345678abcdeff1ull);
    CHECK(getenv("NUVORA_COMPAT") && !strcmp(getenv("NUVORA_COMPAT"),"linux-x86_64-v0"));
    unsigned char entropy[32];CHECK(getrandom(entropy,sizeof(entropy),0)==sizeof(entropy));
    char *allocation=calloc(32768,1);CHECK(allocation);
    for(int i=0;i<32768;i++)CHECK(!allocation[i]);
    allocation=realloc(allocation,100000);CHECK(allocation);free(allocation);
    unsigned char *pages=mmap(NULL,12288,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(pages!=MAP_FAILED);for(int i=0;i<12288;i++)CHECK(!pages[i]);
    pages[0]=21;pages[8192]=42;
    CHECK(!munmap(pages+4096,4096)); CHECK(pages[0]==21 && pages[8192]==42);
    CHECK(!mprotect(pages,4096,PROT_READ));CHECK(!mprotect(pages,4096,PROT_READ|PROT_WRITE));
    pages[0]=22;CHECK(!munmap(pages,12288));
    errno=0;CHECK(mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED && errno==EINVAL);
    errno=0;CHECK(syscall(99999)==-1 && errno==ENOSYS);
    errno=0;CHECK(syscall(SYS_write,1,(void *)(uintptr_t)1,16)==-1 && errno==EFAULT);
    errno=0;CHECK(syscall(SYS_write,0x100000001ull,"x",1)==-1 && errno==EBADF);
    errno=0;CHECK(syscall(SYS_arch_prctl,0x1002,0xffff800000001000ull)==-1 && errno==EPERM);
    const char *file="runtime-check.txt";
    FILE *out=fopen(file,"wb");CHECK(out);CHECK(fwrite("musl file I/O\n",1,14,out)==14);CHECK(!fclose(out));
    int fd=open(file,O_RDONLY|O_CLOEXEC);CHECK(fd>=0);struct stat st;CHECK(!fstat(fd,&st) && st.st_size==14);
    CHECK(fcntl(fd,F_GETFD)==FD_CLOEXEC);CHECK(!fcntl(fd,F_SETFD,0));CHECK(fcntl(fd,F_GETFD)==0);
    CHECK(lseek(fd,-4,SEEK_END)==10);char text[32]={0};CHECK(read(fd,text,4)==4 && !memcmp(text,"I/O\n",4));CHECK(!close(fd));
    errno=0;CHECK(close(fd)==-1 && errno==EBADF);
    CHECK(getcwd(text,sizeof(text)));struct timespec before,after,delay={0,20000000};
    CHECK(!clock_gettime(CLOCK_MONOTONIC,&before));CHECK(!nanosleep(&delay,NULL));CHECK(!clock_gettime(CLOCK_MONOTONIC,&after));
    CHECK(after.tv_sec>before.tv_sec || (after.tv_sec==before.tv_sec && after.tv_nsec>=before.tv_nsec+20000000));
    printf("musl C / TLS / allocation / mmap / file I/O / clocks\n");
    printf("args=%d, first=%s, empty=%s\n",argc-1,argc>1?argv[1]:"none",argc>2 && !argv[2][0]?"yes":"no");
    puts("LINUX ABI C PASS");return 0;
}
