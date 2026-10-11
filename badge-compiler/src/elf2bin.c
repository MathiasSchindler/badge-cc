#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "host_io.h"

#define FLASH_BASE UINT32_C(0x10000000)
#define FLASH_SIZE (16u * 1024u * 1024u)
#define MAX_INPUT_SIZE (64u * 1024u * 1024u)
#define ELF_HEADER_SIZE 52u
#define PROGRAM_HEADER_SIZE 32u
#define SECTION_HEADER_SIZE 40u
#define PT_LOAD 1u
#define SHT_PROGBITS 1u
#define SHT_NOBITS 8u
#define SHF_ALLOC 2u
#define SHF_TLS 0x400u
#define SHF_COMPRESSED 0x800u

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t address;
} image_section_t;

static uint16_t u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int error(const char *message) {
    fprintf(stderr, "elf2bin: %s\n", message);
    return 0;
}

static int file_range(size_t length, uint32_t offset, uint64_t size) {
    return offset <= length && size <= length - offset;
}

static int section_order(const void *left, const void *right) {
    const image_section_t *a = left;
    const image_section_t *b = right;
    return (a->address > b->address) - (a->address < b->address);
}

static int extract_image(const unsigned char *elf, size_t length,
                         unsigned char **image, size_t *image_size) {
    *image = NULL;
    *image_size = 0;
    if (length < ELF_HEADER_SIZE) return error("truncated ELF header");
    if (memcmp(elf, "\177ELF", 4) != 0 || elf[4] != 1 || elf[5] != 1 ||
        elf[6] != 1 || u16(elf + 16) != 2 || u16(elf + 18) != 40 ||
        u32(elf + 20) != 1 || u16(elf + 40) != ELF_HEADER_SIZE) {
        return error("expected a little-endian ELF32 Arm executable");
    }
    uint32_t phoff = u32(elf + 28);
    uint32_t shoff = u32(elf + 32);
    uint16_t phnum = u16(elf + 44);
    uint16_t shnum = u16(elf + 48);
    if (u16(elf + 42) != PROGRAM_HEADER_SIZE ||
        u16(elf + 46) != SECTION_HEADER_SIZE ||
        phnum == 0 || phnum == UINT16_MAX || shnum == 0 ||
        u16(elf + 50) == UINT16_MAX) {
        return error("missing or unsupported ELF header tables");
    }
    if (!file_range(length, phoff, (uint64_t)phnum * PROGRAM_HEADER_SIZE) ||
        !file_range(length, shoff, (uint64_t)shnum * SECTION_HEADER_SIZE)) {
        return error("truncated ELF header tables");
    }

    for (uint16_t i = 0; i < phnum; ++i) {
        const unsigned char *ph = elf + phoff + (size_t)i * PROGRAM_HEADER_SIZE;
        uint32_t type = u32(ph);
        if (type == 2 || type == 3) return error("dynamic loading is unsupported");
        if (type != PT_LOAD) continue;
        uint32_t filesz = u32(ph + 16);
        uint32_t memsz = u32(ph + 20);
        uint32_t alignment = u32(ph + 28);
        if (filesz > memsz ||
            (filesz != 0 && !file_range(length, u32(ph + 4), filesz)) ||
            (uint64_t)u32(ph + 8) + memsz > UINT64_C(0x100000000) ||
            (uint64_t)u32(ph + 12) + memsz > UINT64_C(0x100000000) ||
            (alignment > 1 && ((alignment & (alignment - 1)) != 0 ||
             (u32(ph + 8) % alignment) != (u32(ph + 4) % alignment)))) {
            return error("invalid load segment bounds or alignment");
        }
    }

    image_section_t *sections = calloc(shnum, sizeof(*sections));
    if (sections == NULL) return error("out of memory reading sections");
    size_t count = 0;
    int ok = 0;
    for (uint16_t i = 0; i < shnum; ++i) {
        const unsigned char *sh = elf + shoff + (size_t)i * SECTION_HEADER_SIZE;
        uint32_t type = u32(sh + 4);
        uint32_t flags = u32(sh + 8);
        uint32_t vma = u32(sh + 12);
        uint32_t offset = u32(sh + 16);
        uint32_t size = u32(sh + 20);
        if (type != 0 && type != SHT_NOBITS && !file_range(length, offset, size)) {
            error("section contents extend beyond the input file");
            goto done;
        }
        if ((flags & SHF_ALLOC) == 0 || size == 0) continue;
        if ((flags & (SHF_TLS | SHF_COMPRESSED)) != 0) {
            error("allocated TLS/compressed sections are unsupported");
            goto done;
        }
        if (type == SHT_NOBITS) continue;
        if (type != SHT_PROGBITS) {
            error("unsupported file-backed allocated section type");
            goto done;
        }
        uint64_t address = 0;
        unsigned matches = 0;
        for (uint16_t j = 0; j < phnum; ++j) {
            const unsigned char *ph = elf + phoff + (size_t)j * PROGRAM_HEADER_SIZE;
            if (u32(ph) != PT_LOAD || offset < u32(ph + 4) || vma < u32(ph + 8)) continue;
            uint32_t delta = offset - u32(ph + 4);
            uint32_t filesz = u32(ph + 16);
            if (delta > filesz || size > filesz - delta ||
                vma - u32(ph + 8) != delta) continue;
            address = (uint64_t)u32(ph + 12) + delta;
            ++matches;
        }
        if (matches != 1) {
            error("allocated section must map to exactly one load segment");
            goto done;
        }
        if (address < FLASH_BASE || address + size > (uint64_t)FLASH_BASE + FLASH_SIZE) {
            error("section load address lies outside RP2350 flash");
            goto done;
        }
        sections[count++] = (image_section_t){offset, size, (uint32_t)address};
    }
    if (count == 0) {
        error("no flash contents found");
        goto done;
    }
    qsort(sections, count, sizeof(*sections), section_order);
    if (sections[0].address != FLASH_BASE) {
        error("flash image must start at 0x10000000");
        goto done;
    }
    uint64_t end = FLASH_BASE;
    for (size_t i = 0; i < count; ++i) {
        if (sections[i].address < end) {
            error("overlapping flash sections");
            goto done;
        }
        end = (uint64_t)sections[i].address + sections[i].size;
    }
    size_t size = (size_t)(end - FLASH_BASE);
    unsigned char *bytes = calloc(size, 1);
    if (bytes == NULL) {
        error("out of memory creating flash image");
        goto done;
    }
    for (size_t i = 0; i < count; ++i) {
        memcpy(bytes + (sections[i].address - FLASH_BASE),
               elf + sections[i].offset, sections[i].size);
    }
    *image = bytes;
    *image_size = size;
    ok = 1;
done:
    free(sections);
    return ok;
}

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "elf2bin: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    long size;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 ||
        (unsigned long)size > MAX_INPUT_SIZE || fseek(file, 0, SEEK_SET) != 0) {
        error("cannot read input size, or input exceeds 64 MiB");
        fclose(file);
        return NULL;
    }
    unsigned char *bytes = malloc(size == 0 ? 1 : (size_t)size);
    if (bytes == NULL) {
        error("out of memory reading input");
        fclose(file);
        return NULL;
    }
    int ok = fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file) != 0) ok = 0;
    if (!ok) {
        error("failed to read or close input");
        free(bytes);
        return NULL;
    }
    *length = (size_t)size;
    return bytes;
}

int main(int argc, char **argv) {
    if (argc != 5 || strcmp(argv[1], "-O") != 0 || strcmp(argv[2], "binary") != 0) {
        fprintf(stderr, "usage: %s -O binary input.elf output.bin\n", argv[0]);
        return 2;
    }
    struct stat input_stat, output_stat;
    if (stat(argv[3], &input_stat) != 0) {
        fprintf(stderr, "elf2bin: cannot inspect input %s: %s\n", argv[3], strerror(errno));
        return 1;
    }
    if (!S_ISREG(input_stat.st_mode)) {
        error("input must be a regular file");
        return 1;
    }
    if (lstat(argv[4], &output_stat) == 0) {
        if (!S_ISREG(output_stat.st_mode)) {
            error("existing output must be a regular file, not a symlink or device");
            return 1;
        }
        if (input_stat.st_dev == output_stat.st_dev && input_stat.st_ino == output_stat.st_ino) {
            error("input and output must be different files");
            return 1;
        }
    } else if (errno != ENOENT) {
        fprintf(stderr, "elf2bin: cannot inspect output %s: %s\n", argv[4], strerror(errno));
        return 1;
    }
    size_t length;
    unsigned char *elf = read_file(argv[3], &length);
    if (elf == NULL) return 1;
    unsigned char *image;
    size_t image_size;
    int ok = extract_image(elf, length, &image, &image_size);
    free(elf);
    if (!ok) return 1;
    ok = host_write_file("elf2bin", argv[4], image, image_size);
    free(image);
    if (!ok) return 1;
    printf("%s: %zu bytes at 0x%08x\n", argv[4], image_size, FLASH_BASE);
    return 0;
}
