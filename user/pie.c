/* Freestanding static PIE example: no fixed linker address or shared libc. */
#include <nv/sdk.h>
#include <nv/string.h>
static const char message[]="Nuvora static PIE: relative pointers, BSS and RELRO verified.\n";
static const char *const text_pointer=message;
static usize (*const length_pointer)(const char *)=strlen;
static volatile u8 zero_fill[8193];
int user_main(const char *args) {
    /* Volatile reads prevent constant folding from hiding missing fixups. */
    const char *text=*(const char *const volatile *)&text_pointer;
    usize (*length)(const char *)=*(usize (*const volatile *)(const char *))&length_pointer;
    if (text!=message || length!=strlen || length(text)!=sizeof(message)-1) return 81;
    if (nv_syscall(NV_INFO,(uptr)&text_pointer,0,0)!=-NV_EFAULT) return 82;
    for (u32 i=0;i<sizeof(zero_fill);++i) if (zero_fill[i]) return 83;
    zero_fill[0]=0xa5;zero_fill[sizeof(zero_fill)-1]=0x5a;
    if (!strcmp(args,"--exec")) {
        nv_syscall(NV_EXEC,(uptr)"/apps/pie",(uptr)"--self-test",0);
        return 84;
    }
    if (*args && strcmp(args,"--self-test")) return 85;
    if (nv_syscall(NV_EMIT,1,(uptr)text,length(text))!=(iptr)length(text)) return 86;
    return *args?73:0;
}
