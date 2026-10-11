typedef unsigned int u32;
typedef unsigned long long u64;

static volatile u32 seed = 19;
static u32 arithmetic(u32 x) {
  u32 a = x + 7;
  u32 b = a ^ 0xdeadbeefu;
  a = b + 0x12345678u;
  return (a ^ b) + x;
}
static u64 wide(u64 x) {
  u64 a = x + 0x100000001ULL;
  u64 b = a ^ 0xfedcba9876543210ULL;
  a = b + x;
  return a ^ b;
}
static int narrow(unsigned char x, short y) {
  unsigned char a = (unsigned char)(x + 251);
  short b = (short)(y + 17);
  return (int)a + (int)b;
}
static int pointers(int *p, int *q) {
  int *r = p;
  *r += 3;
  r = q;
  *r += 5;
  return *p + *q;
}
static void mutate(int *p) { *p = *p * 3 + 1; }
static int escaping(int x) {
  int a = x;
  mutate(&a);
  mutate(&a);
  return a;
}
static int loops(int x) {
  int sum = 0;
  for (int k = 0; k < 10; k++) {
    int local = x + k;
    sum += local;
  }
  return sum;
}
static int observable(int x) {
  volatile int a = x;
  a += 7;
  return a;
}
static u64 swap_wide(int n) {
  u64 a = 0x0123456789abcdefULL, b = 0xfedcba9876543210ULL;
  while (n--) {
    u64 previous = a;
    a = b;
    b = previous;
  }
  return a ^ (b >> 32);
}
static int narrow_loop(int n) {
  unsigned char a = 250;
  short b = -32000;
  while (n--) {
    a = (unsigned char)(a + 13);
    b = (short)(b + 97);
  }
  return (int)a + (int)b;
}
static _Bool boolean_loop(int n) {
  _Bool value = 0;
  while (n--)
    value = !value;
  return value;
}
static int markers[3] = {7, 11, 13};
static int *pointer_loop(int n) {
  int *p = &markers[0];
  while (n--) {
    if (p == &markers[2])
      p = &markers[0];
    else
      p++;
  }
  return p;
}
int main(void) {
  u32 start = seed;
  for (u32 k = 0; k < 64; k++) {
    u32 x = start + k * 0x1020304u;
    u32 b = (x + 7) ^ 0xdeadbeefu;
    if (arithmetic(x) != ((b + 0x12345678u) ^ b) + x)
      return 1;
    u64 n = ((u64)x << 32) | (0xffffffffu - x);
    u64 w = (n + 0x100000001ULL) ^ 0xfedcba9876543210ULL;
    if (wide(n) != ((w + n) ^ w))
      return 2;
    int y = ((int)k - 31) * 100;
    if (narrow((unsigned char)(k * 13), (short)y) !=
        (int)((k * 13 + 251) & 255) + y + 17)
      return 3;
    int a = (int)k, c = 2 * (int)k;
    if (pointers(&a, &c) != 3 * (int)k + 8 || a != (int)k + 3 ||
        c != 2 * (int)k + 5)
      return 4;
    if (escaping((int)k) != 9 * (int)k + 4)
      return 5;
    if (loops((int)k) != 10 * (int)k + 45)
      return 6;
    if (observable((int)k) != (int)k + 7)
      return 7;
    u64 wa = (k & 1) ? 0xfedcba9876543210ULL : 0x0123456789abcdefULL;
    u64 wb = (k & 1) ? 0x0123456789abcdefULL : 0xfedcba9876543210ULL;
    if (swap_wide((int)k) != (wa ^ (wb >> 32)))
      return 8;
    if (narrow_loop((int)k) !=
        (int)((250 + 13 * k) & 255) - 32000 + 97 * (int)k)
      return 9;
    if (boolean_loop((int)k) != (k & 1))
      return 10;
    if (pointer_loop((int)k) != &markers[k % 3])
      return 11;
  }
  return 0;
}
