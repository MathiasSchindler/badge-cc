unsigned scalar_immediates(unsigned);
unsigned scalar_mla(unsigned, unsigned, unsigned);
unsigned scalar_multiuse(unsigned, unsigned, unsigned);
unsigned scalar_deferred(unsigned, unsigned, unsigned);
unsigned scalar_deferred_division(unsigned, unsigned);
int scalar_flagged(int);
unsigned scalar_unsigned(unsigned, unsigned);
unsigned scalar_signed_reverse(int, int);
unsigned scalar_exact(unsigned);
unsigned scalar_addresses(unsigned *, unsigned);
unsigned scalar_negative_address(unsigned *);
unsigned scalar_stride3(unsigned char *, unsigned);
unsigned scalar_register_mix(unsigned, unsigned);
unsigned scalar_register_shifts(unsigned, unsigned);
unsigned scalar_register_zero(unsigned, unsigned);
static volatile unsigned inputs[] = {
    0, 1, 7, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x12345678u};
int main(void) {
  unsigned array[] = {0x12345678u, 19, 37, 0xffffffffu, 53};
  unsigned char bytes[] = {1, 9, 2, 11, 3, 7, 13, 5, 8, 17};
  for (unsigned k = 0; k < 7; k++) {
    unsigned x = inputs[k], y = inputs[(k + 2) % 7], z = inputs[(k + 3) % 7];
    unsigned mixed = ((((x + y) * (x - y)) ^ x) & y) | (x - y);
    if (scalar_register_mix(x, y) != mixed ||
        scalar_register_zero(x, y) != y)
      return 9;
    for (unsigned amount = 0; amount < 32; amount++)
      if (scalar_register_shifts(x, amount) !=
          ((x << amount) ^ (x >> amount) ^ (unsigned)((int)x >> amount)))
        return 10;
    unsigned a = ((x + 4095u + 4095u) & 0x00ff00ffu) | 0x80000000u;
    a ^= 0x55555555u;
    a = ((a << 5) >> 3) ^ ((x & 0x80000000u) ? 0xffffffffu : 0u);
    if (scalar_immediates(x) != (a ^ 0x12345678u))
      return 1;
    if (scalar_mla(x, y, z) != x * y + z ||
        scalar_multiuse(x, y, z) != ((x * y + z) ^ (x * y)) ||
        scalar_deferred(x, y, z) != (x + 1u) * (y ^ 0x55555555u) + z)
      return 2;
    int signed_x = (int)x;
    for (unsigned j = 0; j < 5; j++) {
      unsigned d = j == 0   ? 1u
                   : j == 1 ? 7u
                   : j == 2 ? 0xfffffffdu
                   : j == 3 ? 0x80000000u
                            : 0xffffffffu;
      if (scalar_unsigned(x, d) != x / d + x % d)
        return 3;
      if (scalar_deferred_division(x, d) != (x + 3u) / d + (x + 3u) % d)
        return 3;
      int signed_d = (int)d;
      if (signed_x == (-2147483647 - 1) && signed_d == -1)
        continue;
      if (scalar_signed_reverse(signed_x, signed_d) !=
          (unsigned)(signed_x / signed_d) + (unsigned)(signed_x % signed_d))
        return 4;
    }
    if (scalar_exact(x & ~7u) != (x & ~7u) / 8u)
      return 5;
  }
  for (int x = -1024; x <= 1024; x += 17)
    if (scalar_flagged(x) != x * 7 + 19)
      return 6;
  for (unsigned k = 0; k < 4; k++)
    if (scalar_addresses(array, k) != array[k] + array[3] ||
        scalar_stride3(bytes, k) != bytes[k * 3])
      return 7;
  if (scalar_negative_address(array + 1) != array[0] ||
      array[0] != 0x12345678u || array[4] != 53u || bytes[9] != 17)
    return 8;
  return 0;
}
