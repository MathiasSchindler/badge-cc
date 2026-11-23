#ifdef OWNED_FLOAT_FIXTURE
double owned_fp_mix(int first, float f, double d, int last) {
    if (!f || f != f) return (double)(first + last);
    return (double)(-f * 100.0F) + d;
}

unsigned owned_fp_compare(float x, float y) {
    return (x ? 1u : 0u) | (x == y ? 2u : 0u) |
        (x < y ? 4u : 0u) | (x != y ? 8u : 0u);
}

int owned_fp_to_int(double d) { return (int)d; }
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *object;
typedef unsigned char byte;

object mp_binary_get_val(char, char, byte *, byte **);
object gcc_mp_binary_get_val(char, char, byte *, byte **);
void mp_binary_set_val(char, char, object, byte *, byte **);
void gcc_mp_binary_set_val(char, char, object, byte *, byte **);
double owned_fp_mix(int, float, double, int);
unsigned owned_fp_compare(float, float);
int owned_fp_to_int(double);

static unsigned unexpected_integer_path;
const uint32_t mp_type_int = 0;

object mp_obj_new_float(float value) {
    union { float value; uint32_t bits; } fp = { .value = value };
    return (object)(uintptr_t)fp.bits;
}

float mp_obj_get_float(object value) {
    union { uint32_t bits; float value; } fp = { .bits = (uintptr_t)value };
    return fp.value;
}

object mp_obj_new_int(long n) { ++unexpected_integer_path; return (object)n; }
object mp_obj_new_int_from_uint(unsigned long n) { ++unexpected_integer_path; return (object)n; }
object mp_obj_new_int_from_ll(long long n) { ++unexpected_integer_path; return (object)(uintptr_t)n; }
object mp_obj_new_int_from_ull(unsigned long long n) { ++unexpected_integer_path; return (object)(uintptr_t)n; }
object mp_obj_new_str_from_cstr(const char *s) { ++unexpected_integer_path; return (object)s; }
long mp_obj_get_int(object value) { ++unexpected_integer_path; return (long)value; }
long mp_obj_int_get_truncated(object value) { ++unexpected_integer_path; return (long)value; }
void mp_obj_int_to_bytes(object value, size_t size, byte *bytes, bool big, bool sign, bool overflow) {
    (void)value; (void)size; (void)bytes; (void)big; (void)sign; (void)overflow;
    ++unexpected_integer_path;
}
_Noreturn void mp_raise_ValueError(const void *message) { (void)message; __builtin_trap(); }

static const uint32_t samples[] = {
    0, 0x80000000, 0x3f800000, 0xc0490fdb, 0x00000001,
    0x00800000, 0x477fe000, 0x4e800000, 0x7f800000, 0xff800000, 0x7fc00000,
};

int main(void) {
    const float nan = mp_obj_get_float((object)(uintptr_t)0x7fc00000);
    if (owned_fp_mix(3, -1.5F, 4.0, 7) != 154.0 || owned_fp_mix(3, -0.0F, 4.0, 7) != 10.0 ||
        owned_fp_mix(3, nan, 4.0, 7) != 10.0 || owned_fp_to_int(-32.75) != -32)
        return 5;
    if (owned_fp_compare(-0.0F, 0.0F) != 2 || owned_fp_compare(nan, 3.0F) != 9 ||
        owned_fp_compare(1.0F, 3.0F) != 13)
        return 6;
    const char formats[] = {'<', '>', '@'}, types[] = {'e', 'f', 'd'};
    for (size_t s = 0; s < sizeof(samples) / sizeof(*samples); ++s) {
        for (size_t i = 0; i < sizeof(formats); ++i) {
            for (size_t j = 0; j < sizeof(types); ++j) {
                _Alignas(8) byte a[16], b[16];
                for (size_t n = 0; n < sizeof(a); ++n) a[n] = b[n] = 0xa5;
                byte *pa = a + 1, *pb = b + 1;
                object input = (object)(uintptr_t)samples[s];
                mp_binary_set_val(formats[i], types[j], input, a, &pa);
                gcc_mp_binary_set_val(formats[i], types[j], input, b, &pb);
                if (pa - a != pb - b) return 1;
                for (size_t n = 0; n < sizeof(a); ++n)
                    if (a[n] != b[n]) return 2;
                pa = a + 1; pb = b + 1;
                object x = mp_binary_get_val(formats[i], types[j], a, &pa);
                object y = gcc_mp_binary_get_val(formats[i], types[j], b, &pb);
                if (x != y || pa - a != pb - b) return 3;
            }
        }
    }
    return unexpected_integer_path ? 4 : 0;
}
#endif
