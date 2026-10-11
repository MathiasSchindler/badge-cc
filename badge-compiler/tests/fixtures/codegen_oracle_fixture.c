typedef unsigned int u32;
typedef unsigned long long u64;
static volatile u32 seed = 17;
static int array[5] = {2, 3, 5, 7, 11};
static const char text[] = "backend";
static volatile int wide_inputs[6] = {-65537, 65537,      -1023,
                                      1,      2147483647, -2147483647};
static volatile u64 wide_value = 0xfedcba9876543210ULL;
static volatile int sink;
static int arithmetic(int x, int y) {
  unsigned char a = x;
  short b = y;
  return (a + 3) * 5 + b / 7 + b % 7 + (x >> 3) + (int)((u32)y >> 28);
}
static int loop(int n) {
  int a = 1, b = 2;
  for (int k = 0; k < n; k++) {
    int previous = a;
    a = b;
    b = previous + b;
  }
  return a + b;
}
static int flow(int x) {
  switch (x) {
  case 0:
    return 19;
  case 1:
    return 23;
  case 2:
    x += 3;
  case 5:
    return x + 7;
  default:
    return -31;
  }
}
static int (*volatile flow_pointer)(int) = flow;
struct pair {
  int x, y;
};
static struct pair record(struct pair p, int n) {
  p.x += n;
  p.y -= n;
  return p;
}
static int many(int a, int b, int c, int d, int e, int f) {
  return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
}
static int (*volatile six_pointer)(int, int, int, int, int, int) = many;
static int indirect(int (*f)(int), int x) { return f(x); }
static int swap_loop(int n) {
  int a = 13, b = 29;
  while (n--) {
    int old = a;
    a = b;
    b = old;
    sink = a;
  }
  return a * 100 + b;
}
static int large_frame(int x) {
  volatile unsigned char bytes[8192];
  bytes[x] = 3;
  bytes[8191] = x;
  return bytes[x] + bytes[8191];
}
static int wide(int x, int y) {
  long long product = (long long)x * y;
  return (int)(product / 1024);
}
int main(void) {
  int x = (int)seed;
  if (arithmetic(x, -83) != arithmetic(17, -83))
    return 1;
  if (loop(x) != 10946)
    return 2;
  if (flow(2) != 12 || flow(9) != -31 || indirect(flow_pointer, 1) != 23)
    return 3;
  if (six_pointer(1, 2, 3, 4, 5, x) != 157)
    return 4;
  if (swap_loop(x) != 2913 || large_frame(x) != 20)
    return 13;
  struct pair p = record((struct pair){7, 13}, x);
  if (p.x != 24 || p.y != -4)
    return 5;
  if (wide(wide_inputs[0], wide_inputs[1]) != -4194432)
    return 6;
  if (wide(wide_inputs[2], wide_inputs[3]) != 0)
    return 9;
  u64 a64 = wide_value, b64 = 0xfedcba9876543210ULL;
  if (a64 != b64 || a64 < b64 || a64 > b64 || !(a64 <= b64) || !(a64 >= b64))
    return 10;
  if ((long long)a64 >= 0 || a64 <= 0xffffffffULL ||
      (long long)a64 > -(long long)seed)
    return 11;
  u64 product = (u64)(u32)wide_inputs[4] * (u32)wide_inputs[5];
  if (product != 0x3fffffffffffffffULL)
    return 12;
  if (wide_value + 0x12345678ULL != 0xfedcba9888888888ULL)
    return 14;
  if (wide_value - 0x76543211ULL != 0xfedcba97ffffffffULL)
    return 15;
  if ((wide_value << 1) != 0xfdb97530eca86420ULL ||
      (wide_value >> 1) != 0x7f6e5d4c3b2a1908ULL)
    return 16;
  if ((wide_value << 32) != 0x7654321000000000ULL ||
      (wide_value >> 32) != 0xfedcba98ULL)
    return 17;
  if ((wide_value << 63) != 0 || (wide_value >> 63) != 1 ||
      ((long long)wide_value >> 63) != -1)
    return 18;
  int a[5] = {1, 2, 3, 4, 5};
  a[2] = x;
  if (a[2] + array[4] != 28 || text[3] != 'k' || &array[4] - array != 4)
    return 7;
  seed++;
  if (seed != 18 || !(x && seed) || (0 && ++seed))
    return 8;
  return 0;
}
