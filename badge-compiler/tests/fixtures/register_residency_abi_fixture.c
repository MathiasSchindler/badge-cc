unsigned residency_callback(unsigned x);
unsigned residency_mix(unsigned a, unsigned b, unsigned c, unsigned d,
                       unsigned e, unsigned f, unsigned g, unsigned h);
unsigned long long residency_wide(unsigned long long x, unsigned y);
struct residency_pair {
  unsigned x, y;
};
struct residency_pair residency_record(struct residency_pair p, unsigned n);
unsigned residency_narrow(signed char a, unsigned char b, short c,
                          unsigned short d, unsigned count);
#ifdef CALLER
unsigned residency_callback(unsigned x) { return (x ^ 0xa5a5a5a5u) + 17u; }
static volatile unsigned source[4] = {0x12345678u, 0xffffffffu, 1u,
                                      0x80000000u};
int main(void) {
  for (unsigned k = 0; k < 40; k++) {
    unsigned x = source[k & 3] + k;
    unsigned a = x + 1, b = x ^ 9, c = x * 3, d = x - 7;
    unsigned e = x >> 3, f = x << 5, g = ~x, h = x + 101;
    unsigned q = ((a + b) ^ (c + d)) + ((e + f) ^ (g + h));
    unsigned expected = residency_callback(q);
    for (unsigned j = 0; j < 9; j++)
      expected = (expected << 3) ^ (expected >> 5) ^ (j + x);
    unsigned (*fn)(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                   unsigned, unsigned) = residency_mix;
    if (fn(a, b, c, d, e, f, g, h) != expected)
      return 1;
    unsigned long long wide = 0x12345678ffffffffULL + x;
    if (residency_wide(wide, x) != ((wide + x) ^ ((unsigned long long)x << 33)))
      return 2;
    struct residency_pair p =
        residency_record((struct residency_pair){x, ~x}, k);
    if (p.x != x + k || p.y != (~x ^ k))
      return 3;
    signed char a8 = (signed char)(x + 0x80u);
    unsigned char b8 = (unsigned char)(x ^ 0xa5u);
    short c16 = (short)(x + 0x8000u);
    unsigned short d16 = (unsigned short)(x ^ 0xff01u);
    unsigned adjusted = residency_callback((unsigned char)a8 + (unsigned)b8);
    unsigned count = k % 9;
    for (unsigned j = 0; j < count; j++) {
      a8 = (signed char)((unsigned char)a8 + b8);
      b8 = (unsigned char)(b8 ^ ((unsigned short)c16 >> 3));
      c16 = (short)((unsigned short)c16 + d16);
      d16 = (unsigned short)(d16 + (unsigned char)a8);
    }
    unsigned narrow = ((unsigned char)a8 | ((unsigned)b8 << 8)) ^
                       ((unsigned short)c16 + ((unsigned)d16 << 16)) ^ adjusted;
    if (residency_narrow((signed char)(x + 0x80u),
                        (unsigned char)(x ^ 0xa5u), (short)(x + 0x8000u),
                        (unsigned short)(x ^ 0xff01u), count) != narrow)
      return 4;
  }
  return 0;
}
#else
unsigned residency_narrow(signed char a, unsigned char b, short c,
                          unsigned short d, unsigned count) {
  unsigned adjusted = residency_callback((unsigned char)a + (unsigned)b);
  for (unsigned j = 0; j < count; j++) {
    a = (signed char)((unsigned char)a + b);
    b = (unsigned char)(b ^ ((unsigned short)c >> 3));
    c = (short)((unsigned short)c + d);
    d = (unsigned short)(d + (unsigned char)a);
  }
  return ((unsigned char)a | ((unsigned)b << 8)) ^
         ((unsigned short)c + ((unsigned)d << 16)) ^ adjusted;
}
unsigned residency_mix(unsigned a, unsigned b, unsigned c, unsigned d,
                       unsigned e, unsigned f, unsigned g, unsigned h) {
  unsigned left = (a + b) ^ (c + d);
  unsigned right = (e + f) ^ (g + h);
  unsigned x = a - 1;
  __asm__ volatile("nop" ::: "memory");
  unsigned result = residency_callback(left + right);
  for (unsigned j = 0; j < 9; j++)
    result = (result << 3) ^ (result >> 5) ^ (j + x);
  return result;
}
unsigned long long residency_wide(unsigned long long x, unsigned y) {
  return (x + y) ^ ((unsigned long long)y << 33);
}
struct residency_pair residency_record(struct residency_pair p, unsigned n) {
  p.x += n;
  p.y ^= n;
  return p;
}
#endif
