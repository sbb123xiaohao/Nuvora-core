#include <assert.h>
#include <stdio.h>
#include <nv/crypto.h>
#include <nv/string.h>
static void digest(const u8 actual[32],const char *expected) {
    static const char hex[]="0123456789abcdef";char text[65];
    for (u32 i=0;i<32;++i) { text[i*2]=hex[actual[i]>>4];text[i*2+1]=hex[actual[i]&15]; }
    text[64]=0;assert(!strcmp(text,expected));
}
int main(void) {
    u8 out[32],key[131];struct nv_sha256 s;struct nv_hmac256 h;struct nv_pbkdf2 p;
    nv_sha256_sum("",0,out);digest(out,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    nv_sha256_sum("abc",3,out);digest(out,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    nv_sha256_begin(&s);
    for (u32 i=0;i<1000000;++i) nv_sha256_update(&s,"a",1);
    nv_sha256_end(&s,out);digest(out,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    memset(key,0x0b,20);nv_hmac256_begin(&h,key,20);nv_hmac256_sum(&h,"Hi There",8,out);
    digest(out,"b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    nv_hmac256_begin(&h,"Jefe",4);nv_hmac256_sum(&h,"what do ya want for nothing?",28,out);
    digest(out,"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    memset(key,0xaa,sizeof(key));nv_hmac256_begin(&h,key,sizeof(key));
    nv_hmac256_sum(&h,"Test Using Larger Than Block-Size Key - Hash Key First",54,out);
    digest(out,"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    nv_pbkdf2_begin(&p,"password",8,"salt",4,1);assert(nv_pbkdf2_step(&p,0));
    digest(p.result,"120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    nv_pbkdf2_begin(&p,"password",8,"salt",4,2);assert(!nv_pbkdf2_step(&p,0));assert(nv_pbkdf2_step(&p,1));
    digest(p.result,"ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    nv_pbkdf2_begin(&p,"password",8,"salt",4,4096);
    while (!nv_pbkdf2_step(&p,73)) {}
    digest(p.result,"c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    memcpy(out,p.result,32);assert(nv_secret_equal(out,p.result,32));out[31]^=1;
    assert(!nv_secret_equal(out,p.result,32));nv_secret_clear(&p,sizeof(p));
    for (u32 i=0;i<sizeof(p);++i) assert(!((u8 *)&p)[i]);
    puts("PASS crypto: SHA-256, RFC 4231 HMAC, PBKDF2 vectors, batching, long keys and secret erasure");
}
