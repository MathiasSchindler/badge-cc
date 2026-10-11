static volatile unsigned effects;

static unsigned choose(unsigned x) {
  unsigned n;
  if (2 + 3 == 5)
    n = 2;
  else {
    effects++;
    n = 7;
  }
  switch (n) {
  case 2:
    return x + 7;
  default:
    effects++;
    return 1;
  }
}

static unsigned loop(unsigned x) {
  unsigned sum = 0;
  for (unsigned i = 0; i < x; i++)
    sum += i;
  return sum;
}

int main(void) {
  unsigned a = choose(9);
  unsigned b = loop(7);
  return a == 16 && b == 21 && effects == 0 ? 0 : 1;
}
