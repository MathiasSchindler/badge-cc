struct Record {
    char tag;
    union {
        struct {
            unsigned char low : 3;
            unsigned char high : 5;
            unsigned short middle : 9;
        };
        unsigned int raw;
    };
};

struct Record record = { .tag = 11, .low = 5, .high = 20, .middle = 263 };
struct __attribute__((packed)) Packed {
    unsigned char low : 7;
    unsigned char : 2;
    unsigned short high : 10;
    _Bool flag : 1;
};
struct Packed packed = { .low = 97, .high = 1001, .flag = 1 };
float minus_zero = -0.0f;
double infinity = __builtin_huge_val();

extern int missing(void) __attribute__((weak));
__attribute__((weak)) int optional(void) { return 11; }
__attribute__((weak)) int optional_value = 31;
__attribute__((naked)) unsigned plus_one(unsigned x)
{
    __asm__ volatile ("adds r0, #1\nbx lr");
}
unsigned initial_address = (unsigned)(void (*)(void)) plus_one;

struct __attribute__((packed)) Padded {
    unsigned char low : 2;
    unsigned : 0;
    unsigned char high : 2;
};
struct Padded padded = { 1, 2 };

unsigned read_record(const struct Record *p)
{
    return p->low + p->high * 8 + p->middle * 256 + p->tag * 131072;
}

void write_record(struct Record *p, unsigned x)
{
    p->low = x;
    p->middle += x;
}
unsigned access_packed(struct Packed *p)
{
    p->high += 7;
    return p->low + p->high * 128 + p->flag * 131072;
}

unsigned long long swap64(unsigned long long x) { return __builtin_bswap64(x); }
unsigned swap32(unsigned x) { return __builtin_bswap32(x); }
unsigned short swap16(unsigned short x) { return __builtin_bswap16(x); }
int pop(unsigned x) { return __builtin_popcount(x); }
int constant_probe(int *p) { return __builtin_constant_p((*p)++) + __builtin_constant_p(2 + 5); }
int float_probe(float x, double y)
{
    return __builtin_signbitf(x) + __builtin_isnan(x) * 2 +
        __builtin_isfinite(y) * 4 + __builtin_isinf_sign(y) * 8;
}
void *fill(void *data, unsigned n) { return __builtin_memset(data, 0x159, n); }
void *copy(void *to, const void *from, unsigned n) { return __builtin_memcpy(to, from, n); }
int compare(const void *a, const void *b, unsigned n) { return __builtin_memcmp(a, b, n); }
int weak_probe(void) { return optional() + optional_value + (missing != 0); }

unsigned register_operands(unsigned *p, unsigned long long value, unsigned live)
{
    unsigned x, y;
    __asm__ volatile ("eor.w %[x], %[in], #85\nadd.w %[y], %[in], #7" :
                      [x] "=&r"(x), [y] "=&l"(y) : [in] "r"(value) : "cc");
    __asm__ volatile ("adds %0, #3" : "+l"(*p++) : : "cc", "memory");
    __asm__ volatile ("str %0, [%1]" : : "r"(y), "r"(p) : "memory");
    return x ^ y ^ live;
}

struct __attribute__((packed)) PackedScalar {
    char tag;
    unsigned value;
};
unsigned packed_scalar(struct PackedScalar *p, unsigned new_value)
{
    unsigned old = p->value;
    p->value = new_value;
    return old;
}

void wide_shifts(unsigned long long x, unsigned count, unsigned long long *out)
{
    out[0] = x << count;
    out[1] = x >> count;
    out[2] = (unsigned long long)((long long)x >> count);
}
