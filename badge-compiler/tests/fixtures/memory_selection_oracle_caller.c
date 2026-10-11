void memory_copy(unsigned char *, unsigned);
void memory_fill(unsigned char *, unsigned char, unsigned);
void memory_fallback(unsigned char *);
static const unsigned lengths[] = {0, 1,  2,  3,  4,  5,  7, 8,
                                   9, 15, 16, 17, 63, 64, 65};
static const unsigned char fills[] = {0, 1, 127, 128, 255};
int main(void) {
  unsigned char out[100];
  for (unsigned n = 0; n < sizeof(lengths) / sizeof(*lengths); n++) {
    for (unsigned j = 0; j < sizeof(out); j++)
      out[j] = 187;
    memory_copy(out + 1, n);
    for (unsigned j = 0; j < sizeof(out); j++) {
      unsigned char expected = j < 1 || j >= 97 ? 187
                               : j >= 5 && j < 5 + lengths[n]
                                   ? (unsigned char)((j - 1) * 37 + 11)
                                   : 165;
      if (out[j] != expected)
        return 1;
    }
    for (unsigned k = 0; k < sizeof(fills) / sizeof(*fills); k++) {
      for (unsigned j = 0; j < sizeof(out); j++)
        out[j] = 187;
      memory_fill(out + 1, fills[k], n);
      for (unsigned j = 0; j < sizeof(out); j++) {
        unsigned char expected = j < 1 || j >= 97               ? 187
                                 : j >= 5 && j < 5 + lengths[n] ? fills[k]
                                                                : 165;
        if (out[j] != expected)
          return 2;
      }
    }
  }
  for (unsigned j = 0; j < sizeof(out); j++)
    out[j] = 187;
  memory_fallback(out + 1);
  for (unsigned j = 0; j < sizeof(out); j++) {
    unsigned char expected = j < 1 || j >= 97   ? 187
                             : j >= 5 && j < 23 ? 51
                                                : 165;
    if (out[j] != expected)
      return 3;
  }
  return 0;
}
