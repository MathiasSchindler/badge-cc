#include <stddef.h>

size_t mp_parse_num_base(const char *, size_t, int *);
size_t gcc_parse_num_base(const char *, size_t, int *);

int main(void) {
    static const int bases[] = {0, 2, 8, 10, 16, 36};
    for (unsigned first = 0; first < 2; ++first) {
        for (unsigned second = 0; second < 256; ++second) {
            const char input[3] = {first ? '0' : '7', (char)second, '1'};
            for (size_t len = 0; len < 4; ++len) {
                for (unsigned j = 0; j < sizeof(bases) / sizeof(*bases); ++j) {
                    int base = bases[j], reference = base, expected = base;
                    size_t skip = 0;
                    if (len >= 2 && first) {
                        int prefix = (second == 'b' || second == 'B') ? 2 :
                                     (second == 'o' || second == 'O') ? 8 :
                                     (second == 'x' || second == 'X') ? 16 : 0;
                        if (prefix && (!base || base == prefix)) {
                            expected = prefix;
                            skip = 2;
                        } else if (!base) {
                            expected = 1;
                        }
                    } else if (!base) {
                        expected = 10;
                    }
                    if (mp_parse_num_base(input, len, &base) != skip ||
                        gcc_parse_num_base(input, len, &reference) != skip ||
                        base != expected || reference != expected)
                        return 1;
                }
            }
        }
    }
    return 0;
}
