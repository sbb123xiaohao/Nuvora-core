#include <nv/crypto.h>
#include <nv/string.h>
void nv_secret_clear(void *p, usize n) {
    volatile u8 *out=p;
    while (n--) *out++=0;
}
bool nv_secret_equal(const void *a, const void *b, usize n) {
    const volatile u8 *x=a,*y=b;
    u32 different=0;
    for (usize i=0;i<n;++i) different|=x[i]^y[i];
    return different==0;
}
static u32 rotate(u32 x,u32 n) { return (x>>n)|(x<<(32-n)); }
static void compress(struct nv_sha256 *s,const u8 *p) {
    static const u32 k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    u32 w[64];
    for (u32 i=0;i<16;++i) w[i]=((u32)p[i*4]<<24)|((u32)p[i*4+1]<<16)|((u32)p[i*4+2]<<8)|p[i*4+3];
    for (u32 i=16;i<64;++i) {
        u32 a=w[i-15],b=w[i-2];
        w[i]=w[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+w[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
    }
    u32 a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for (u32 i=0;i<64;++i) {
        u32 t=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^((~e)&g))+k[i]+w[i];
        u32 v=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+v;
    }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
    nv_secret_clear(w,sizeof(w));
}
void nv_sha256_begin(struct nv_sha256 *s) {
    *s=(struct nv_sha256){.h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
}
void nv_sha256_update(struct nv_sha256 *s,const void *data,usize n) {
    const u8 *p=data;
    s->bytes+=n;
    while (n) {
        u32 part=(u32)MIN(n,64u-s->used);
        memcpy(s->block+s->used,p,part);s->used+=part;p+=part;n-=part;
        if (s->used==64) { compress(s,s->block);s->used=0; }
    }
}
void nv_sha256_end(struct nv_sha256 *s,u8 out[32]) {
    u64 bits=s->bytes*8;
    s->block[s->used++]=0x80;
    if (s->used>56) { memset(s->block+s->used,0,64-s->used);compress(s,s->block);s->used=0; }
    memset(s->block+s->used,0,56-s->used);
    for (u32 i=0;i<8;++i) s->block[63-i]=(u8)(bits>>(8*i));
    compress(s,s->block);
    for (u32 i=0;i<32;++i) out[i]=(u8)(s->h[i/4]>>(24-8*(i%4)));
    nv_secret_clear(s,sizeof(*s));
}
void nv_sha256_sum(const void *p,usize n,u8 out[32]) {
    struct nv_sha256 s;nv_sha256_begin(&s);nv_sha256_update(&s,p,n);nv_sha256_end(&s,out);
}
void nv_hmac256_begin(struct nv_hmac256 *h,const void *key,usize n) {
    u8 block[64]={0};
    if (n>64) nv_sha256_sum(key,n,block);else memcpy(block,key,n);
    for (u32 i=0;i<64;++i) block[i]^=0x36;
    nv_sha256_begin(&h->inner);nv_sha256_update(&h->inner,block,64);
    for (u32 i=0;i<64;++i) block[i]^=0x36^0x5c;
    nv_sha256_begin(&h->outer);nv_sha256_update(&h->outer,block,64);
    nv_secret_clear(block,sizeof(block));
}
void nv_hmac256_sum(const struct nv_hmac256 *h,const void *data,usize n,u8 out[32]) {
    struct nv_sha256 inner=h->inner,outer=h->outer;u8 digest[32];
    nv_sha256_update(&inner,data,n);nv_sha256_end(&inner,digest);
    nv_sha256_update(&outer,digest,32);nv_sha256_end(&outer,out);
    nv_secret_clear(digest,sizeof(digest));
}
void nv_pbkdf2_begin(struct nv_pbkdf2 *s,const void *password,usize n,const void *salt,usize sn,u32 iterations) {
    *s=(struct nv_pbkdf2){.total=MAX(iterations,1u),.done=1};
    nv_hmac256_begin(&s->hmac,password,n);
    struct nv_sha256 inner=s->hmac.inner,outer=s->hmac.outer;
    const u8 index[4]={0,0,0,1};u8 digest[32];
    nv_sha256_update(&inner,salt,sn);nv_sha256_update(&inner,index,4);nv_sha256_end(&inner,digest);
    nv_sha256_update(&outer,digest,32);nv_sha256_end(&outer,s->u);
    memcpy(s->result,s->u,32);nv_secret_clear(digest,sizeof(digest));
}
bool nv_pbkdf2_step(struct nv_pbkdf2 *s,u32 budget) {
    while (budget-- && s->done<s->total) {
        nv_hmac256_sum(&s->hmac,s->u,32,s->u);
        for (u32 i=0;i<32;++i) s->result[i]^=s->u[i];
        ++s->done;
    }
    return s->done==s->total;
}
