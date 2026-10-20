#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif

typedef struct {
    uint32_t state[8];
    uint64_t size;
    unsigned char block[64];
    size_t used;
} sha256;

static uint32_t rotate(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

static void transform(sha256 *hash, const unsigned char *block) {
    static const uint32_t constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i) {
        const unsigned char *p = block + i * 4;
        words[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                   (uint32_t)p[2] << 8 | p[3];
    }
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t x = words[i - 15], y = words[i - 2];
        words[i] = words[i - 16] + (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) +
                   words[i - 7] + (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
    }
    uint32_t a = hash->state[0], b = hash->state[1], c = hash->state[2],
             d = hash->state[3], e = hash->state[4], f = hash->state[5],
             g = hash->state[6], h = hash->state[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t first = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) +
                         ((e & f) ^ (~e & g)) + constants[i] + words[i];
        uint32_t second = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) +
                          ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    hash->state[0] += a; hash->state[1] += b;
    hash->state[2] += c; hash->state[3] += d;
    hash->state[4] += e; hash->state[5] += f;
    hash->state[6] += g; hash->state[7] += h;
}

static void update(sha256 *hash, const unsigned char *bytes, size_t length) {
    hash->size += length;
    while (length) {
        if (!hash->used && length >= 64) {
            transform(hash, bytes);
            bytes += 64;
            length -= 64;
        } else {
            size_t take = 64 - hash->used;
            if (take > length) take = length;
            memcpy(hash->block + hash->used, bytes, take);
            hash->used += take;
            bytes += take;
            length -= take;
            if (hash->used == 64) {
                transform(hash, hash->block);
                hash->used = 0;
            }
        }
    }
}

static int hash_file(const char *path) {
#ifdef _WIN32
    int fd = open(path, O_RDONLY | O_BINARY);
#else
    int fd = open(path, O_RDONLY | O_NONBLOCK);
#endif
    if (fd < 0) {
        fprintf(stderr, "badge-sha256: cannot open %s: %s\n", path, strerror(errno));
        return 0;
    }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "badge-sha256: input must be a regular file: %s\n", path);
        if (close(fd)) fprintf(stderr, "badge-sha256: failed to close %s\n", path);
        return 0;
    }
    FILE *file = fdopen(fd, "rb");
    if (!file) {
        fprintf(stderr, "badge-sha256: cannot open input stream %s: %s\n", path, strerror(errno));
        if (close(fd)) fprintf(stderr, "badge-sha256: failed to close %s\n", path);
        return 0;
    }
    sha256 hash = {{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, 0, {0}, 0};
    unsigned char buffer[65536];
    size_t length;
    int ok = 1;
    while ((length = fread(buffer, 1, sizeof(buffer), file))) {
        if (length > UINT64_MAX / 8 - hash.size) {
            fprintf(stderr, "badge-sha256: input exceeds SHA-256 length limit: %s\n", path);
            ok = 0;
            break;
        }
        update(&hash, buffer, length);
    }
    if (ferror(file)) {
        fprintf(stderr, "badge-sha256: failed to read %s\n", path);
        ok = 0;
    }
    if (fclose(file)) {
        fprintf(stderr, "badge-sha256: failed to close %s\n", path);
        ok = 0;
    }
    if (!ok) return 0;
    uint64_t bits = hash.size * 8;
    hash.block[hash.used++] = 0x80;
    if (hash.used > 56) {
        memset(hash.block + hash.used, 0, 64 - hash.used);
        transform(&hash, hash.block);
        hash.used = 0;
    }
    memset(hash.block + hash.used, 0, 56 - hash.used);
    for (unsigned i = 0; i < 8; ++i) hash.block[63 - i] = (unsigned char)(bits >> (i * 8));
    transform(&hash, hash.block);
    int escaped = strpbrk(path, "\\\n") != NULL;
    if (escaped) putchar('\\');
    for (unsigned i = 0; i < 8; ++i) printf("%08x", (unsigned)hash.state[i]);
    fputs("  ", stdout);
    for (const char *p = path; *p; ++p) {
        if (*p == '\\') fputs("\\\\", stdout);
        else if (*p == '\n') fputs("\\n", stdout);
        else putchar((unsigned char)*p);
    }
    putchar('\n');
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: badge-sha256 file...\n", stderr);
        return 2;
    }
    for (int i = 1; i < argc; ++i)
        if (!hash_file(argv[i])) return 1;
    if (fflush(stdout) || ferror(stdout)) {
        fputs("badge-sha256: failed to write manifest\n", stderr);
        return 1;
    }
    return 0;
}
