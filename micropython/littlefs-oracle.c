#include "lfs2.h"

enum { BLOCK_SIZE = 256, BLOCKS = 32, CACHE = 32, LOOKAHEAD = 8, FILE_SIZE = 1249 };
static unsigned char disk[BLOCKS][BLOCK_SIZE], read_cache[CACHE], prog_cache[CACHE];
static unsigned char file_cache[CACHE], lookahead[LOOKAHEAD], payload[FILE_SIZE];
static unsigned reads, programs, erases, flushes;
static lfs2_t fs;
static lfs2_file_t file;

static int read_block(const struct lfs2_config *cfg, lfs2_block_t block,
    lfs2_off_t offset, void *buffer, lfs2_size_t size) {
    (void)cfg;
    if (block >= BLOCKS || offset > BLOCK_SIZE || size > BLOCK_SIZE - offset ||
        offset % 16 || size % 16)
        return LFS2_ERR_IO;
    ++reads;
    memcpy(buffer, &disk[block][offset], size);
    return 0;
}

static int program_block(const struct lfs2_config *cfg, lfs2_block_t block,
    lfs2_off_t offset, const void *buffer, lfs2_size_t size) {
    (void)cfg;
    const unsigned char *src = buffer;
    if (block >= BLOCKS || offset > BLOCK_SIZE || size > BLOCK_SIZE - offset ||
        offset % 16 || size % 16)
        return LFS2_ERR_IO;
    ++programs;
    for (unsigned i = 0; i < size; ++i) {
        unsigned char *dst = &disk[block][offset + i];
        if ((*dst & src[i]) != src[i])
            return LFS2_ERR_IO;
        *dst &= src[i];
    }
    return 0;
}

static int erase_block(const struct lfs2_config *cfg, lfs2_block_t block) {
    (void)cfg;
    if (block >= BLOCKS)
        return LFS2_ERR_IO;
    ++erases;
    memset(disk[block], 0xff, BLOCK_SIZE);
    return 0;
}

static int sync_blocks(const struct lfs2_config *cfg) {
    (void)cfg;
    ++flushes;
    return 0;
}

static const struct lfs2_config config = {
    .read = read_block, .prog = program_block, .erase = erase_block, .sync = sync_blocks,
    .read_size = 16, .prog_size = 16, .block_size = BLOCK_SIZE, .block_count = BLOCKS,
    .block_cycles = 16, .cache_size = CACHE, .lookahead_size = LOOKAHEAD,
    .read_buffer = read_cache, .prog_buffer = prog_cache, .lookahead_buffer = lookahead,
};
static const struct lfs2_file_config file_config = { .buffer = file_cache };

int main(void) {
    memset(disk, 0xff, sizeof(disk));
    for (unsigned i = 0; i < FILE_SIZE; ++i)
        payload[i] = (unsigned char)(13 * i + (i >> 4));
    if (lfs2_format(&fs, &config) || lfs2_mount(&fs, &config) ||
        lfs2_mkdir(&fs, "test"))
        return 1;
    if (lfs2_file_opencfg(&fs, &file, "test/first", LFS2_O_WRONLY | LFS2_O_CREAT, &file_config) ||
        lfs2_file_write(&fs, &file, payload, FILE_SIZE) != FILE_SIZE ||
        lfs2_file_close(&fs, &file) || lfs2_rename(&fs, "test/first", "test/final") ||
        lfs2_unmount(&fs))
        return 2;
    memset(&fs, 0, sizeof(fs));
    struct lfs2_info info;
    if (lfs2_mount(&fs, &config) || lfs2_stat(&fs, "test/final", &info) ||
        info.type != LFS2_TYPE_REG || info.size != FILE_SIZE ||
        lfs2_file_opencfg(&fs, &file, "test/final", LFS2_O_RDONLY, &file_config))
        return 3;
    unsigned char chunk[37];
    for (unsigned off = 0; off < FILE_SIZE;) {
        unsigned length = FILE_SIZE - off < sizeof(chunk) ? FILE_SIZE - off : sizeof(chunk);
        if (lfs2_file_read(&fs, &file, chunk, length) != (lfs2_ssize_t)length ||
            memcmp(chunk, payload + off, length))
            return 4;
        off += length;
    }
    if (lfs2_file_read(&fs, &file, chunk, 1) != 0 ||
        lfs2_file_close(&fs, &file) || lfs2_remove(&fs, "test/final") ||
        lfs2_stat(&fs, "test/final", &info) != LFS2_ERR_NOENT || lfs2_unmount(&fs))
        return 5;
    return !reads || !programs || !erases || !flushes;
}
