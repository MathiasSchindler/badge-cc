unsigned call_callback(unsigned value);
unsigned call_recursive(unsigned depth, unsigned value);
unsigned call_indirect(unsigned value);
extern unsigned (*volatile call_target)(unsigned);

#ifdef CALLER
unsigned call_callback(unsigned value) { return (value ^ 0xa5a5a5a5u) + 17u; }
static unsigned alternate(unsigned value) { return value * 3u + 9u; }
static volatile unsigned observation;
int main(void) {
  unsigned seed = 1;
  for (unsigned k = 0; k < 64; k++) {
    seed = seed * 1664525u + 1013904223u;
    observation = seed;
    unsigned depth = observation >> 29, expected = call_callback(seed + depth);
    for (unsigned n = 1; n <= depth; n++)
      expected = (expected ^ n) + seed + depth - n;
    if (call_recursive(depth, seed) != expected)
      return 1;
    unsigned route = observation & 1;
    call_target = route ? alternate : call_callback;
    unsigned observed = route ? alternate(seed) : call_callback(seed);
    if (call_indirect(seed) != observed + call_callback(seed ^ 7u))
      return 2;
  }
  return 0;
}
#else
unsigned (*volatile call_target)(unsigned);
unsigned call_recursive(unsigned depth, unsigned value) {
  if (!depth)
    return call_callback(value);
  unsigned result = call_recursive(depth - 1u, value + 1u);
  return (result ^ depth) + value;
}
unsigned call_indirect(unsigned value) {
  unsigned result = call_target(value);
  return result + call_callback(value ^ 7u);
}
#endif
