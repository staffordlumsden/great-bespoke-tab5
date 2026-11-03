#pragma once
/* Real SHA-256 through the host's system crypto implementation. */
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#define CC_SHA256_CTX SHA256_CTX
#define CC_SHA256_Init SHA256_Init
#define CC_SHA256_Update SHA256_Update
#define CC_SHA256_Final SHA256_Final
#define CC_SHA256 SHA256
#define CC_LONG size_t
#endif
#include <string.h>
typedef CC_SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }
static inline void mbedtls_sha256_free(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }
static inline int mbedtls_sha256_starts(mbedtls_sha256_context *c, int is224)
{ return !is224 && CC_SHA256_Init(c) ? 0 : -1; }
static inline int mbedtls_sha256_update(mbedtls_sha256_context *c, const void *p, size_t n)
{ return CC_SHA256_Update(c, p, (CC_LONG)n) ? 0 : -1; }
static inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char *out)
{ return CC_SHA256_Final(out, c) ? 0 : -1; }
static inline int mbedtls_sha256(const void *p, size_t n, unsigned char *out, int is224)
{ return !is224 && CC_SHA256(p, (CC_LONG)n, out) ? 0 : -1; }
