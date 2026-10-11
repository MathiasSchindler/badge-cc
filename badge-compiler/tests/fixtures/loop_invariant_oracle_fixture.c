static volatile unsigned input = 0x81234567u;
static volatile unsigned guard;
static unsigned sum(unsigned x, unsigned n) {
  unsigned acc = 0;
  for (unsigned i = 0; i < n; i++) {
    unsigned v = x * 7u + 11u + x / 7u;
    acc += v;
    guard++;
  }
  return acc;
}
static unsigned nested(unsigned x, unsigned n, unsigned m) {
  unsigned acc = 0;
  for (unsigned i = 0; i < n; i++)
    for (unsigned j = 0; j < m; j++)
      acc += x * 7u + i;
  return acc;
}
int main(void) {
  unsigned x = input;
  if (sum(x, 0) != 0 || guard != 0)
    return 1;
  for (unsigned n = 1; n < 18; n++) {
    if (sum(x, n) != (x * 7u + 11u + x / 7u) * n)
      return 2;
    for (unsigned m = 0; m < 5; m++) {
      unsigned expected = 0;
      for (unsigned i = 0; i < n; i++)
        expected += (x * 7u + i) * m;
      if (nested(x, n, m) != expected)
        return 3;
    }
  }
  return guard != 153u;
}
