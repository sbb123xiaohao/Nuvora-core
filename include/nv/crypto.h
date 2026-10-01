#ifndef NV_CRYPTO_H
#define NV_CRYPTO_H
#include <nv/types.h>
/* SHA-256 / HMAC / PBKDF2 as specified in FIPS 180-4 and RFC 8018.
 * Password derivation is incremental so the desktop can repaint and poll
 * input between bounded batches. No password or derived key is logged. */
struct nv_sha256 { u32 h[8]; u64 bytes; u8 block[64]; u32 used; };
struct nv_hmac256 { struct nv_sha256 inner, outer; };
struct nv_pbkdf2 { struct nv_hmac256 hmac; u8 u[32], result[32]; u32 done, total; };
void nv_secret_clear(void *, usize);
bool nv_secret_equal(const void *, const void *, usize);
void nv_sha256_begin(struct nv_sha256 *);
void nv_sha256_update(struct nv_sha256 *, const void *, usize);
void nv_sha256_end(struct nv_sha256 *, u8 out[32]);
void nv_sha256_sum(const void *, usize, u8 out[32]);
void nv_hmac256_begin(struct nv_hmac256 *, const void *, usize);
void nv_hmac256_sum(const struct nv_hmac256 *, const void *, usize, u8 out[32]);
void nv_pbkdf2_begin(struct nv_pbkdf2 *, const void *, usize, const void *, usize, u32);
bool nv_pbkdf2_step(struct nv_pbkdf2 *, u32);
#endif
