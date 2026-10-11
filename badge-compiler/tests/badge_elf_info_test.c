#define main badge_elf_info_main
#include "badge_elf_info.c"
#undef main
#include <assert.h>

static void write16(unsigned char *p, uint16_t n) {
  p[0] = (unsigned char)n;
  p[1] = (unsigned char)(n >> 8);
}
static void write32(unsigned char *p, uint32_t n) {
  for (unsigned k = 0; k < 4; k++)
    p[k] = (unsigned char)(n >> (k * 8));
}
int main(void) {
  unsigned char elf[253] = {0};
  memcpy(elf, "\177ELF\1\1\1", 7);
  write16(elf + 16, 2);
  write16(elf + 18, 40);
  write32(elf + 20, 1);
  write16(elf + 40, 52);
  write32(elf + 32, 52);
  write16(elf + 46, 40);
  write16(elf + 48, 4);
  write32(elf + 92 + 4, 1);
  write32(elf + 92 + 8, 6);
  write32(elf + 92 + 16, 212);
  write32(elf + 92 + 20, 4);
  write32(elf + 132 + 4, 3);
  write32(elf + 132 + 16, 216);
  write32(elf + 132 + 20, 5);
  write32(elf + 172 + 4, 2);
  write32(elf + 172 + 16, 221);
  write32(elf + 172 + 20, 32);
  write32(elf + 172 + 24, 2);
  write32(elf + 172 + 36, 16);
  memcpy(elf + 216, "\0foo\0", 5);
  write32(elf + 237, 1);
  elf[237 + 12] = 0x10;
  assert(report(elf, sizeof(elf), "fixture.elf", 0) == 0);
  assert(report(elf, sizeof(elf), "fixture.elf", 1) == 0);
  assert(report(elf, sizeof(elf), "fixture.elf", 2) == 0);
  for (size_t k = 0; k < sizeof(elf); k++)
    assert(report(elf, k, "truncated.elf", 1) == 1);
  for (size_t k = 0; k < sizeof(elf); k++)
    assert(report(elf, k, "truncated-metrics.elf", 2) == 1);
  unsigned char invalid[sizeof(elf)];
  memcpy(invalid, elf, sizeof(elf));
  write32(invalid + 237, 5);
  assert(report(invalid, sizeof(invalid), "bad-name.elf", 1) == 1);
  memcpy(invalid, elf, sizeof(elf));
  write32(invalid + 172 + 24, 4);
  assert(report(invalid, sizeof(invalid), "bad-link.elf", 1) == 1);
  memcpy(invalid, elf, sizeof(elf));
  write32(invalid + 172 + 36, 15);
  assert(report(invalid, sizeof(invalid), "bad-entry.elf", 1) == 1);
  memcpy(invalid, elf, sizeof(elf));
  memset(invalid + 216, 'x', 5);
  assert(report(invalid, sizeof(invalid), "bad-strings.elf", 1) == 1);
  memcpy(invalid, elf, sizeof(elf));
  write16(invalid + 50, 4);
  assert(report(invalid, sizeof(invalid), "bad-section-names.elf", 2) == 1);
  memcpy(invalid, elf, sizeof(elf));
  write16(invalid + 237 + 14, 9);
  assert(report(invalid, sizeof(invalid), "bad-symbol-section.elf", 2) == 1);
  memcpy(invalid, elf, sizeof(elf));
  invalid[237 + 12] = 0x12;
  write16(invalid + 237 + 14, 1);
  write32(invalid + 237 + 4, 212);
  assert(report(invalid, sizeof(invalid), "outside-function.elf", 2) == 1);
  puts("badge-elf-info: executable sizes/undefined symbols, 253 truncations "
       "and malformed symbol tables PASS");
  return 0;
}
