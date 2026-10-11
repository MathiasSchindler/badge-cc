struct frame_big {
  unsigned x[1000];
};
unsigned long long frame_call(unsigned a, unsigned b, unsigned c,
                              struct frame_big x, unsigned long long wide,
                              unsigned suffix);
#ifdef CALLER
static struct frame_big input;
int main(void) {
  input.x[0] = 1;
  input.x[996] = 2;
  input.x[997] = 3;
  input.x[998] = 4;
  input.x[999] = 5;
  unsigned long long (*fn)(unsigned, unsigned, unsigned, struct frame_big,
                           unsigned long long, unsigned) = frame_call;
  if (fn(3, 5, 7, input, 0x11223344fffffff0ULL, 11) != 0x112233450000004fULL)
    return 1;
  if (input.x[0] != 1 || input.x[996] != 2 || input.x[997] != 3 ||
      input.x[998] != 4 || input.x[999] != 5)
    return 2;
  return 0;
}
#else
unsigned long long frame_call(unsigned a, unsigned b, unsigned c,
                              struct frame_big x, unsigned long long wide,
                              unsigned suffix) {
  volatile unsigned char pad[61000];
  pad[0] = 23;
  pad[60999] = 31;
  x.x[996] += a;
  x.x[997] += b;
  x.x[998] += c;
  return wide +
         (unsigned long long)(x.x[0] + x.x[996] + x.x[997] + x.x[998] +
                              x.x[999] + suffix + pad[0] + pad[60999]);
}
#endif
