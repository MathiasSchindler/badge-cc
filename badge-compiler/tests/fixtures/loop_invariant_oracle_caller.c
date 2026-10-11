unsigned loop_sum(unsigned, unsigned);
unsigned loop_divide(unsigned, unsigned);
unsigned loop_remainder(int, unsigned);
unsigned loop_nested(unsigned, unsigned, unsigned);
unsigned loop_variable(unsigned, unsigned, unsigned);
unsigned loop_overflow(int, unsigned);
unsigned loop_irreducible(unsigned, _Bool);
extern volatile unsigned loop_guard;
static volatile unsigned values[] = {0,           1,           7,
                                     0x7fffffffu, 0x80000000u, 0xffffffffu};
int main(void) {
  const unsigned counts[] = {0, 1, 17, 64};
  unsigned guard = 0;
  for (unsigned k = 0; k < 6; k++) {
    unsigned x = values[k];
    for (unsigned j = 0; j < 4; j++) {
      unsigned n = counts[j];
      if (loop_divide(x, n) != (x / 7u) * n ||
          loop_remainder((int)x, n) != (unsigned)((int)x % -7) * n)
        return 7;
      unsigned one = x * 7u + 11u + x / 7u + (unsigned)((int)x / 7);
      if (loop_sum(x, n) != one * n)
        return 1;
      guard += n;
      if (loop_guard != guard)
        return 2;
      for (unsigned m = 0; m < 5; m++) {
        unsigned expected = 0;
        for (unsigned i = 0; i < n; i++)
          expected += (x * 7u + i) * m;
        if (loop_nested(x, n, m) != expected)
          return 3;
      }
      if (loop_variable(x, n, 7) != (x / 7u) * n ||
          loop_variable(x, 0, 0) != 0 ||
          loop_overflow((int)0x80000000u, 0) != 0)
        return 4;
      if (x != 0x80000000u && loop_overflow((int)x, n) != (0u - x) * n)
        return 5;
    }
    if (loop_irreducible(x, 0) != x + 11u || loop_irreducible(x, 1) != x + 11u)
      return 6;
  }
  return 0;
}
