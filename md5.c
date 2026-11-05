/*
 * md5.c - MD5 message digest, written from the algorithm in RFC 1321
 * (https://www.rfc-editor.org/rfc/rfc1321).
 */
#include "md5.h"
#include <stdio.h>
#include <string.h>

/* Per-round constants: K[i] = floor(abs(sin(i + 1)) * 2^32) */
static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

/* Left-rotate amounts for each of the 64 steps */
static const unsigned S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* Process one 64-byte block */
static void md5_transform(uint32_t st[4], const unsigned char blk[64])
{
    uint32_t x[16];
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    int i;

    /* MD5 reads the block as 16 little-endian 32-bit words */
    for (i = 0; i < 16; i++)
        x[i] = (uint32_t)blk[i * 4] | ((uint32_t)blk[i * 4 + 1] << 8) |
               ((uint32_t)blk[i * 4 + 2] << 16) | ((uint32_t)blk[i * 4 + 3] << 24);

    for (i = 0; i < 64; i++) {
        uint32_t f, tmp;
        int g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) % 16; }
        else             { f = c ^ (b | ~d);       g = (7 * i) % 16; }
        tmp = d;
        d = c;
        c = b;
        b = b + ROTL(a + f + K[i] + x[g], S[i]);
        a = tmp;
    }

    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

void md5_init(md5_ctx *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
}

void md5_update(md5_ctx *ctx, const void *data, size_t len)
{
    const unsigned char *p = data;
    size_t used = ctx->count % 64;   /* bytes already sitting in the buffer */
    ctx->count += len;

    /* Top up a partially filled buffer first */
    if (used) {
        size_t need = 64 - used;
        if (len < need) {
            memcpy(ctx->buffer + used, p, len);
            return;
        }
        memcpy(ctx->buffer + used, p, need);
        md5_transform(ctx->state, ctx->buffer);
        p += need;
        len -= need;
    }
    /* Whole blocks straight from the input */
    while (len >= 64) {
        md5_transform(ctx->state, p);
        p += 64;
        len -= 64;
    }
    /* Keep the leftover for next time */
    if (len)
        memcpy(ctx->buffer, p, len);
}

void md5_final(md5_ctx *ctx, unsigned char digest[16])
{
    uint64_t bits = ctx->count * 8;   /* message length in bits, before padding */
    unsigned char pad = 0x80, zero = 0, lenbytes[8];
    int i;

    /* Pad with a single 1 bit, then zeros until length is 56 mod 64 */
    md5_update(ctx, &pad, 1);
    while (ctx->count % 64 != 56)
        md5_update(ctx, &zero, 1);

    /* Append original length as 64-bit little-endian */
    for (i = 0; i < 8; i++)
        lenbytes[i] = (unsigned char)(bits >> (8 * i));
    md5_update(ctx, lenbytes, 8);

    for (i = 0; i < 4; i++) {
        digest[i * 4]     = (unsigned char)(ctx->state[i]);
        digest[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 8);
        digest[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 16);
        digest[i * 4 + 3] = (unsigned char)(ctx->state[i] >> 24);
    }
}

/* Turn 16 raw digest bytes into 32 hex characters */
static void digest_to_hex(const unsigned char d[16], char out[33])
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 16; i++) {
        out[i * 2]     = hex[d[i] >> 4];
        out[i * 2 + 1] = hex[d[i] & 0x0f];
    }
    out[32] = '\0';
}

void md5_hex(const void *data, size_t len, char out[33])
{
    md5_ctx ctx;
    unsigned char d[16];
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, d);
    digest_to_hex(d, out);
}

int md5_file_hex(const char *path, char out[33])
{
    FILE *f = fopen(path, "rb");
    md5_ctx ctx;
    unsigned char buf[8192], d[16];
    size_t n;

    if (!f)
        return -1;
    md5_init(&ctx);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        md5_update(&ctx, buf, n);
    if (ferror(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    md5_final(&ctx, d);
    digest_to_hex(d, out);
    return 0;
}
