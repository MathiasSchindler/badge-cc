#define _DARWIN_C_SOURCE
#define main elf2bin_main
#include "elf2bin.c"
#undef main
#include <assert.h>

#define FIXTURE_SIZE 512u
#define SHOFF 256u

static void put16(unsigned char *p, uint16_t value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (8u * i));
}

static void segment(unsigned char *ph, uint32_t offset, uint32_t vma,
                    uint32_t lma, uint32_t size) {
    put32(ph, PT_LOAD);
    put32(ph + 4, offset);
    put32(ph + 8, vma);
    put32(ph + 12, lma);
    put32(ph + 16, size);
    put32(ph + 20, size);
    put32(ph + 24, 4);
    put32(ph + 28, 1);
}

static void section(unsigned char *sh, uint32_t type, uint32_t flags,
                    uint32_t address, uint32_t offset, uint32_t size) {
    put32(sh + 4, type);
    put32(sh + 8, flags);
    put32(sh + 12, address);
    put32(sh + 16, offset);
    put32(sh + 20, size);
    put32(sh + 32, 1);
}

static void fixture(unsigned char *elf) {
    memset(elf, 0, FIXTURE_SIZE);
    memcpy(elf, "\177ELF", 4);
    elf[4] = elf[5] = elf[6] = 1;
    put16(elf + 16, 2);
    put16(elf + 18, 40);
    put32(elf + 20, 1);
    put32(elf + 24, FLASH_BASE | 1u);
    put32(elf + 28, ELF_HEADER_SIZE);
    put32(elf + 32, SHOFF);
    put32(elf + 36, 0x05000200);
    put16(elf + 40, ELF_HEADER_SIZE);
    put16(elf + 42, PROGRAM_HEADER_SIZE);
    put16(elf + 44, 3);
    put16(elf + 46, SECTION_HEADER_SIZE);
    put16(elf + 48, 6);
    segment(elf + 52, 152, FLASH_BASE, FLASH_BASE, 4);
    segment(elf + 84, 160, 0x20000000, FLASH_BASE + 8, 4);
    segment(elf + 116, 168, FLASH_BASE + 16, FLASH_BASE + 16, 2);
    section(elf + SHOFF + 40, SHT_PROGBITS, SHF_ALLOC, FLASH_BASE, 152, 4);
    section(elf + SHOFF + 80, SHT_PROGBITS, SHF_ALLOC | 1u, 0x20000000, 160, 4);
    section(elf + SHOFF + 120, SHT_NOBITS, SHF_ALLOC | 1u, 0x20000004, UINT32_MAX, 4096);
    section(elf + SHOFF + 160, SHT_PROGBITS, 0, 0, 172, 4);
    section(elf + SHOFF + 200, SHT_PROGBITS, SHF_ALLOC, FLASH_BASE + 16, 168, 2);
    memcpy(elf + 152, "\x11\x22\x33\x44", 4);
    memcpy(elf + 160, "\x55\x66\x77\x88", 4);
    memcpy(elf + 168, "\x99\xaa", 2);
    memcpy(elf + 172, "META", 4);
}

static const unsigned char expected[] = {
    0x11, 0x22, 0x33, 0x44, 0, 0, 0, 0,
    0x55, 0x66, 0x77, 0x88, 0, 0, 0, 0, 0x99, 0xaa
};

static void check_image(const unsigned char *elf) {
    unsigned char *image;
    size_t size;
    assert(extract_image(elf, FIXTURE_SIZE, &image, &size));
    assert(size == sizeof(expected));
    assert(memcmp(image, expected, size) == 0);
    free(image);
}

static void reject(unsigned char *elf, size_t length) {
    unsigned char *image = (unsigned char *)elf;
    size_t size = 123;
    assert(!extract_image(elf, length, &image, &size));
    assert(image == NULL && size == 0);
}

static void save(const char *path, const unsigned char *bytes, size_t length) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(bytes, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static void check_output(const char *path) {
    unsigned char bytes[sizeof(expected) + 1];
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    assert(fread(bytes, 1, sizeof(bytes), file) == sizeof(expected));
    assert(memcmp(bytes, expected, sizeof(expected)) == 0);
    assert(fclose(file) == 0);
}

int main(void) {
    unsigned char elf[FIXTURE_SIZE];
    fixture(elf);
    check_image(elf);
    save("build/elf2bin-fixture.elf", elf, sizeof(elf));
    unsigned char sh[SECTION_HEADER_SIZE];
    memcpy(sh, elf + SHOFF + 40, sizeof(sh));
    memcpy(elf + SHOFF + 40, elf + SHOFF + 200, sizeof(sh));
    memcpy(elf + SHOFF + 200, sh, sizeof(sh));
    check_image(elf);
    fixture(elf);
    put32(elf + 52 + 16, 8);
    put32(elf + 52 + 20, 8);
    memset(elf + 156, 0xff, 4);
    check_image(elf);
    fixture(elf);
    put32(elf + 116 + 12, FLASH_BASE + FLASH_SIZE - 2);
    unsigned char *large_image;
    size_t large_size;
    assert(extract_image(elf, sizeof(elf), &large_image, &large_size));
    assert(large_size == FLASH_SIZE);
    assert(memcmp(large_image, expected, 12) == 0);
    assert(large_image[FLASH_SIZE - 3] == 0);
    assert(large_image[FLASH_SIZE - 2] == 0x99 && large_image[FLASH_SIZE - 1] == 0xaa);
    free(large_image);

    const struct { size_t offset; uint32_t value; } invalid[] = {
        {0, 0}, {4, 0x00010102}, {16, 0x00280001}, {18, 62}, {20, 2},
        {28, UINT32_MAX}, {32, UINT32_MAX}, {40, 0}, {42, 0}, {44, UINT16_MAX},
        {46, 0}, {48, 0}, {50, UINT16_MAX},
        {52, 2}, {52 + 16, 5}, {52 + 28, 3},
        {52 + 4, UINT32_MAX}, {52 + 8, UINT32_MAX}, {52 + 12, UINT32_MAX},
        {SHOFF + 40 + 16, UINT32_MAX}, {SHOFF + 40 + 20, UINT32_MAX},
        {SHOFF + 40 + 4, 7}, {SHOFF + 40 + 8, SHF_ALLOC | SHF_COMPRESSED},
        {SHOFF + 40 + 8, SHF_ALLOC | SHF_TLS},
        {SHOFF + 40 + 12, FLASH_BASE + 1},
        {84 + 12, 0x20000000}, {84 + 12, FLASH_BASE + 2},
        {116 + 12, FLASH_BASE + FLASH_SIZE - 1}
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        fixture(elf);
        put32(elf + invalid[i].offset, invalid[i].value);
        reject(elf, sizeof(elf));
    }
    fixture(elf);
    put32(elf + SHOFF + 40 + 8, 0);
    put32(elf + SHOFF + 80 + 8, 0);
    put32(elf + SHOFF + 200 + 8, 0);
    reject(elf, sizeof(elf));
    fixture(elf);
    memcpy(elf + 116, elf + 52, PROGRAM_HEADER_SIZE);
    reject(elf, sizeof(elf));
    fixture(elf);
    put32(elf + 52 + 12, FLASH_BASE + 1);
    reject(elf, sizeof(elf));
    fixture(elf);
    reject(elf, 0);
    reject(elf, 51);
    reject(elf, 147);
    reject(elf, 495);

    for (size_t i = 0; i < sizeof(elf); ++i) {
        fixture(elf);
        elf[i] ^= 0xff;
        unsigned char *image;
        size_t size;
        if (extract_image(elf, sizeof(elf), &image, &size)) free(image);
    }

    char directory[] = "build/elf2bin-test-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char input[128], output[128], alias[128], absent[128];
    assert(snprintf(input, sizeof(input), "%s/input.elf", directory) > 0);
    assert(snprintf(output, sizeof(output), "%s/output.bin", directory) > 0);
    assert(snprintf(alias, sizeof(alias), "%s/alias.elf", directory) > 0);
    assert(snprintf(absent, sizeof(absent), "%s/missing/output.bin", directory) > 0);
    fixture(elf);
    save(input, elf, sizeof(elf));
    char *args[] = {"elf2bin", "-O", "binary", input, output, NULL};
    assert(elf2bin_main(5, args) == 0);
    check_output(output);
    elf[0] = 0;
    save(input, elf, sizeof(elf));
    assert(elf2bin_main(5, args) == 1);
    check_output(output);
    fixture(elf);
    save(input, elf, sizeof(elf));
    args[4] = input;
    assert(elf2bin_main(5, args) == 1);
    assert(link(input, alias) == 0);
    args[4] = alias;
    assert(elf2bin_main(5, args) == 1);
    assert(unlink(alias) == 0);
    assert(symlink("input.elf", alias) == 0);
    assert(elf2bin_main(5, args) == 1);
    args[4] = directory;
    assert(elf2bin_main(5, args) == 1);
    args[4] = output;
    assert(truncate(input, MAX_INPUT_SIZE + 1) == 0);
    assert(elf2bin_main(5, args) == 1);
    check_output(output);
    save(input, elf, sizeof(elf));
    args[4] = absent;
    assert(elf2bin_main(5, args) == 1);
    args[3] = absent;
    assert(elf2bin_main(5, args) == 1);
    assert(elf2bin_main(1, args) == 2);
    assert(unlink(input) == 0 && unlink(output) == 0 && unlink(alias) == 0);
    assert(rmdir(directory) == 0);
    puts("elf2bin: layout, RAM initializers, gaps, NOBITS, malformed ELF, mutation and CLI tests PASS");
    return 0;
}
