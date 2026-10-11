static volatile unsigned input;

static unsigned identity(unsigned x) {
  unsigned a = x + 0u;
  unsigned b = a * 1u;
  unsigned c = b & ~0u;
  return ((c | 0u) ^ 0u) << 0u;
}

static unsigned char roundtrip(unsigned char x) {
  unsigned long long a = (unsigned long long)x;
  return (unsigned char)a;
}

static long long signed_chain(signed char x) {
  short a = (short)x;
  return (long long)a;
}

static unsigned long long unsigned_chain(unsigned char x) {
  unsigned short a = (unsigned short)x;
  return (unsigned long long)a;
}

int main(void) {
  for (unsigned i = 0u; i < 256u; ++i) {
    input = i;
    unsigned x = input;
    if (identity(x) != x || roundtrip((unsigned char)x) != (unsigned char)x ||
        signed_chain((signed char)x) != (long long)(signed char)x ||
        unsigned_chain((unsigned char)x) !=
            (unsigned long long)(unsigned char)x)
      return 1;
  }
  return 0;
}
