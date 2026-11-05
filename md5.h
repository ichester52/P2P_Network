/*
 * md5.h - MD5 message digest (used for tracker GET replies and file checksums)
 *
 * Implementation follows the algorithm in RFC 1321
 * (https://www.rfc-editor.org/rfc/rfc1321). No external library needed.
 */
#ifndef MD5_H
#define MD5_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[4];        /* A, B, C, D */
    uint64_t count;           /* total bytes processed so far */
    unsigned char buffer[64]; /* partial block waiting to be processed */
} md5_ctx;

void md5_init(md5_ctx *ctx);
void md5_update(md5_ctx *ctx, const void *data, size_t len);
void md5_final(md5_ctx *ctx, unsigned char digest[16]);

/* Convenience: MD5 of a memory buffer as a 32-char lowercase hex string. */
void md5_hex(const void *data, size_t len, char out[33]);

/* Convenience: MD5 of a whole file. Returns 0 on success, -1 on error.
 * (Peers can use this to compute the MD5 for createtracker.) */
int md5_file_hex(const char *path, char out[33]);

#endif
