#ifndef TC_SHA256_H
#define TC_SHA256_H

#include <stddef.h>
#include <stdint.h>

/* SHA-256 (FIPS 180-4). The identity of a track is the hash of its audio file (TRAKCRATE-FORMAT.md 13). */

struct tc_sha256 {
    uint32_t h[8];
    uint64_t length;        /* bytes hashed */
    uint8_t block[64];
    size_t fill;
};

void tc_sha256_init(struct tc_sha256 *c);
void tc_sha256_update(struct tc_sha256 *c, const void *data, size_t len);
void tc_sha256_final(struct tc_sha256 *c, uint8_t out[32]);

/* lower case hex of a buffer (65 bytes with the NUL) */
void tc_sha256_hex(const void *data, size_t len, char hex[65]);

/* hex of a file's content; 0 on success, -1 with errno set */
int tc_sha256_file(const char *path, char hex[65]);

#endif /* TC_SHA256_H */
