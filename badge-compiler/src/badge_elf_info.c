#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint16_t read16(const unsigned char *p) {
  return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t read32(const unsigned char *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static int range(size_t length, uint32_t offset, uint64_t size) {
  return offset <= length && size <= length - offset;
}
static void json_string(const unsigned char *text) {
  putchar('"');
  for (; *text; text++) {
    if (*text == '"' || *text == '\\')
      printf("\\%c", *text);
    else if (*text < 32 || *text >= 127)
      printf("\\u%04x", *text);
    else
      putchar(*text);
  }
  putchar('"');
}
static int report(const unsigned char *elf, size_t length, const char *file,
                  int undefined) {
  if (length < 52 || memcmp(elf, "\177ELF\1\1\1", 7) || read16(elf + 16) != 2 ||
      read16(elf + 18) != 40 || read32(elf + 20) != 1 ||
      read16(elf + 40) != 52 || read16(elf + 46) != 40)
    goto invalid;
  uint32_t offset = read32(elf + 32);
  unsigned count = read16(elf + 48);
  unsigned names = read16(elf + 50);
  uint64_t text = 0, data = 0, bss = 0;
  if (!count || !range(length, offset, (uint64_t)count * 40))
    goto invalid;
  if (undefined == 2 && names) {
    if (names >= count)
      goto invalid;
    const unsigned char *strings = elf + offset + names * 40;
    if (read32(strings + 4) != 3 ||
        !range(length, read32(strings + 16), read32(strings + 20)))
      goto invalid;
    for (unsigned k = 0; k < count; k++) {
      uint32_t name = read32(elf + offset + k * 40);
      if (name >= read32(strings + 20) ||
          !memchr(elf + read32(strings + 16) + name, 0,
                  read32(strings + 20) - name))
        goto invalid;
    }
  }
  for (unsigned k = 0; k < count; k++) {
    const unsigned char *sh = elf + offset + k * 40;
    uint32_t type = read32(sh + 4), flags = read32(sh + 8),
             at = read32(sh + 16), size = read32(sh + 20);
    if (type != 8 && !range(length, at, size))
      goto invalid;
    if (flags & 2) {
      if (type == 8)
        bss += size;
      else if (flags & 1)
        data += size;
      else
        text += size;
    }
    if (type != 2)
      continue;
    if (read32(sh + 36) != 16 || size % 16 || read32(sh + 24) >= count)
      goto invalid;
    const unsigned char *strings = elf + offset + read32(sh + 24) * 40;
    uint32_t str_at = read32(strings + 16), str_size = read32(strings + 20);
    if (read32(strings + 4) != 3 || !range(length, str_at, str_size))
      goto invalid;
    for (unsigned n = 0; n < size / 16; n++) {
      const unsigned char *sym = elf + at + n * 16;
      uint32_t name = read32(sym);
      if (name >= str_size || !memchr(elf + str_at + name, 0, str_size - name))
        goto invalid;
      if (undefined == 2) {
        unsigned section = read16(sym + 14);
        if (section >= count && section != 0xfff1 && section != 0xfff2)
          goto invalid;
        if ((sym[12] & 15) == 2 && section && section < count) {
          const unsigned char *code = elf + offset + section * 40;
          uint32_t address = read32(sym + 4) & ~1u, start = read32(code + 12);
          if ((read32(code + 8) & 6) != 6 || address < start ||
              (uint64_t)address + read32(sym + 8) >
                  (uint64_t)start + read32(code + 20))
            goto invalid;
        }
      }
    }
  }
  if (undefined == 2) {
    printf("{\"schema\":1,\"entry\":%" PRIu32 ",\"text\":%" PRIu64
           ",\"data\":%" PRIu64 ",\"bss\":%" PRIu64 ",\"sections\":[",
           read32(elf + 24), text, data, bss);
    for (unsigned k = 0; k < count; k++) {
      const unsigned char *sh = elf + offset + k * 40;
      if (k)
        putchar(',');
      fputs("{\"name\":", stdout);
      const unsigned char *name = (const unsigned char *)"";
      if (names) {
        const unsigned char *strings = elf + offset + names * 40;
        name = elf + read32(strings + 16) + read32(sh);
      }
      json_string(name);
      printf(",\"type\":%" PRIu32 ",\"flags\":%" PRIu32 ",\"address\":%" PRIu32
             ",\"size\":%" PRIu32 "}",
             read32(sh + 4), read32(sh + 8), read32(sh + 12), read32(sh + 20));
    }
    fputs("],\"symbols\":[", stdout);
    unsigned emitted = 0;
    for (unsigned k = 0; k < count; k++) {
      const unsigned char *sh = elf + offset + k * 40;
      if (read32(sh + 4) != 2)
        continue;
      const unsigned char *strings = elf + offset + read32(sh + 24) * 40;
      for (unsigned n = 1; n < read32(sh + 20) / 16; n++) {
        const unsigned char *sym = elf + read32(sh + 16) + n * 16;
        if (emitted++)
          putchar(',');
        fputs("{\"name\":", stdout);
        json_string(elf + read32(strings + 16) + read32(sym));
        printf(",\"value\":%" PRIu32 ",\"size\":%" PRIu32
               ",\"type\":%u,\"binding\":%u,\"section\":%u}",
               read32(sym + 4), read32(sym + 8), sym[12] & 15, sym[12] >> 4,
               read16(sym + 14));
      }
    }
    puts("]}");
  } else if (undefined == 1) {
    for (unsigned k = 0; k < count; k++) {
      const unsigned char *sh = elf + offset + k * 40;
      if (read32(sh + 4) != 2)
        continue;
      const unsigned char *strings = elf + offset + read32(sh + 24) * 40;
      for (unsigned n = 1; n < read32(sh + 20) / 16; n++) {
        const unsigned char *sym = elf + read32(sh + 16) + n * 16;
        if (!read16(sym + 14) && sym[12] >> 4)
          printf("%s\n", elf + read32(strings + 16) + read32(sym));
      }
    }
  } else
    printf("text\tdata\tbss\ttotal\tfile\n%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
           "\t%" PRIu64 "\t%s\n",
           text, data, bss, text + data + bss, file);
  return 0;
invalid:
  fprintf(stderr,
          "badge-elf-info: %s: malformed/unsupported ELF32 Arm executable\n",
          file);
  return 1;
}
int main(int argc, char **argv) {
  int undefined = argc == 3 && !strcmp(argv[1], "-u");
  int metrics = argc == 3 && !strcmp(argv[1], "--metrics");
  if ((argc != 2 && !undefined && !metrics) ||
      (argc == 2 && argv[1][0] == '-')) {
    fputs("usage: badge-elf-info [-u|--metrics] image.elf\n", stderr);
    return 2;
  }
  const char *path = argv[argc - 1];
  struct stat info;
  if (stat(path, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
      info.st_size > 64 * 1024 * 1024) {
    fprintf(stderr,
            "badge-elf-info: %s: input must be a regular file within 64 MiB\n",
            path);
    return 1;
  }
  size_t size = (size_t)info.st_size;
  unsigned char *bytes = malloc(size ? size : 1);
  FILE *f = fopen(path, "rb");
  if (!bytes || !f) {
    fprintf(stderr, "badge-elf-info: %s: cannot allocate/open input\n", path);
    free(bytes);
    if (f)
      fclose(f);
    return 1;
  }
  int ok = fread(bytes, 1, size, f) == size;
  if (fgetc(f) != EOF || ferror(f))
    ok = 0;
  if (fclose(f))
    ok = 0;
  if (!ok) {
    fprintf(stderr, "badge-elf-info: %s: read failed\n", path);
    free(bytes);
    return 1;
  }
  int result = report(bytes, size, path, metrics ? 2 : undefined);
  if (!result && (fflush(stdout) || ferror(stdout))) {
    fputs("badge-elf-info: output write failed\n", stderr);
    result = 1;
  }
  free(bytes);
  return result;
}
