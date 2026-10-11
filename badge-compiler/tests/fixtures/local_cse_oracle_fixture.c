static volatile unsigned input, effects;
struct pair {
  unsigned a, b;
};
static struct pair table[8];

static void touch(void) { effects++; }

static unsigned expression(unsigned x, unsigned y) {
  unsigned a = (x + y) * 3u;
  touch();
  unsigned b = (x + y) * 3u;
  return a ^ b;
}

static long long casts(signed char x) {
  long long a = (long long)x;
  long long b = (long long)x;
  return a + b;
}

static unsigned addresses(struct pair *p, unsigned i) {
  unsigned *a = &p[i].b;
  unsigned *b = &p[i].b;
  volatile unsigned *v = a;
  unsigned first = *v;
  unsigned second = *v;
  return a == b ? first + second : 0u;
}

int main(void) {
  for (unsigned i = 0u; i < 8u; ++i)
    table[i].b = i + 17u;
  for (unsigned i = 0u; i < 256u; ++i) {
    input = i;
    unsigned x = input;
    if (expression(x, x + 1u) != 0u ||
        casts((signed char)x) != 2ll * (long long)(signed char)x ||
        addresses(table, x & 7u) != 2u * ((x & 7u) + 17u))
      return 1;
  }
  return effects == 256u ? 0 : 1;
}
