typedef unsigned long long u64;
typedef long long i64;
struct division {
  u64 n, d, q, r;
};
static const struct division unsigned_cases[] = {
    {0, 1, 0, 0},
    {1, 3, 0, 1},
    {0xffffffffffffffffULL, 1, 0xffffffffffffffffULL, 0},
    {0xffffffffffffffffULL, 3, 0x5555555555555555ULL, 0},
    {0xffffffffffffffffULL, 0x8000000000000000ULL, 1, 0x7fffffffffffffffULL},
    {0x8000000000000000ULL, 0x8000000000000001ULL, 0, 0x8000000000000000ULL},
    {0xffffffffffffffffULL, 0xffffffffffffffffULL, 1, 0},
    {0x123456789abcdef0ULL, 0x100000001ULL, 0x12345678ULL, 0x88888878ULL},
    {65537, 17, 3855, 2},
};
static const i64 signed_cases[][4] = {
    {17, 3, 5, 2},
    {-17, 3, -5, -2},
    {17, -3, -5, 2},
    {-17, -3, 5, -2},
    {-9223372036854775807LL - 1, 1, -9223372036854775807LL - 1, 0},
    {-9223372036854775807LL - 1, -9223372036854775807LL - 1, 1, 0},
    {9223372036854775807LL, -9223372036854775807LL - 1, 0,
     9223372036854775807LL},
    {-9223372036854775807LL - 1, 3, -3074457345618258602LL, -2},
    {-65537, 1024, -64, -1},
    {65537, 1024, 64, 1},
};
static volatile u64 numerator, denominator;
struct carrier {
  int x, y, z;
};
static struct carrier exchange(struct carrier a) {
  a.x += a.z;
  a.y -= a.z;
  return a;
}
int main(void) {
  for (unsigned k = 0; k < sizeof(unsigned_cases) / sizeof(unsigned_cases[0]);
       k++) {
    numerator = unsigned_cases[k].n;
    denominator = unsigned_cases[k].d;
    if (numerator / denominator != unsigned_cases[k].q)
      return 1;
    if (numerator % denominator != unsigned_cases[k].r)
      return 2;
  }
  for (unsigned k = 0; k < sizeof(signed_cases) / sizeof(signed_cases[0]);
       k++) {
    numerator = (u64)signed_cases[k][0];
    denominator = (u64)signed_cases[k][1];
    if ((i64)numerator / (i64)denominator != signed_cases[k][2])
      return 3;
    if ((i64)numerator % (i64)denominator != signed_cases[k][3])
      return 4;
  }
  struct carrier a = exchange((struct carrier){1, 7, 3});
  if (a.x != 4 || a.y != 4 || a.z != 3)
    return 5;
  return 0;
}
