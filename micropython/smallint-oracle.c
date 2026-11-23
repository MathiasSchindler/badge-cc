#include <stdint.h>

int32_t mp_small_int_modulo(int32_t, int32_t);
int32_t mp_small_int_floor_divide(int32_t, int32_t);
int32_t gcc_modulo(int32_t, int32_t);
int32_t gcc_floor_divide(int32_t, int32_t);

int main(void) {
    const int32_t values[] = {-1073741824, -8191, -257, -8, -3, -1, 0,
                               1, 2, 3, 8, 257, 8191, 1073741823};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (unsigned j = 0; j < sizeof(values) / sizeof(values[0]); ++j) {
            int32_t a = values[i], b = values[j];
            if (b == 0) continue;
            int32_t q = a / b, r = a % b;
            if (r != 0 && ((r < 0) != (b < 0))) {
                --q;
                r += b;
            }
            if (mp_small_int_floor_divide(a, b) != q || gcc_floor_divide(a, b) != q ||
                mp_small_int_modulo(a, b) != r || gcc_modulo(a, b) != r)
                return 1;
        }
    }
    return 0;
}
