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
struct __attribute__((packed)) Packed {
    unsigned char low : 7;
    unsigned char : 2;
    unsigned short high : 10;
    _Bool flag : 1;
};
extern struct Packed packed;
struct __attribute__((packed)) Padded {
    unsigned char low : 2;
    unsigned : 0;
    unsigned char high : 2;
};
extern struct Padded padded;
extern unsigned initial_address;
extern struct Record record;
extern float minus_zero;
extern double infinity;
unsigned read_record(const struct Record *);
void write_record(struct Record *, unsigned);
unsigned access_packed(struct Packed *);
unsigned long long swap64(unsigned long long);
unsigned swap32(unsigned);
unsigned short swap16(unsigned short);
int pop(unsigned);
int constant_probe(int *);
int float_probe(float, double);
void *fill(void *, unsigned);
void *copy(void *, const void *, unsigned);
int compare(const void *, const void *, unsigned);
int weak_probe(void);
unsigned plus_one(unsigned);
unsigned register_operands(unsigned *, unsigned long long, unsigned);
struct __attribute__((packed)) PackedScalar {
    char tag;
    unsigned value;
};
unsigned packed_scalar(struct PackedScalar *, unsigned);
void wide_shifts(unsigned long long, unsigned, unsigned long long *);

int optional(void) { return 32; }
int optional_value = 42;
void *memset(void *data, int byte, unsigned size)
{
    unsigned char *p = data;
    while (size--) *p++ = byte;
    return data;
}
void *memcpy(void *to, const void *from, unsigned size)
{
    unsigned char *dest = to;
    const unsigned char *src = from;
    while (size--) *dest++ = *src++;
    return to;
}
int memcmp(const void *a, const void *b, unsigned size)
{
    const unsigned char *left = a, *right = b;
    while (size--) {
        if (*left != *right) return *left - *right;
        left++;
        right++;
    }
    return 0;
}

int main(void)
{
    struct Record s = { .tag = 7, .low = 3, .high = 9, .middle = 507 };
    if (record.tag != 11 || record.low != 5 || record.high != 20 || record.middle != 263)
        return 1;
    if (read_record(&s) != 3 + 9 * 8 + 507 * 256 + 7 * 131072) return 2;
    write_record(&s, 15);
    if (s.low != 7 || s.high != 9 || s.middle != 10 || s.tag != 7) return 3;
    if (packed.low != 97 || packed.high != 1001 || !packed.flag || sizeof(packed) != 3 ||
        access_packed(&packed) != 97 + 1008 * 128 + 131072 || packed.high != 1008)
        return 9;
    if (swap64(0xfedcba9876543210ULL) != 0x1032547698badcfeULL ||
        swap32(0x01234567u) != 0x67452301u || swap16(0x8123u) != 0x2381u)
        return 4;
    int data = 3;
    if (pop(0xf0200001u) != 6 || pop(~0u) != 32 || constant_probe(&data) != 1 || data != 3)
        return 5;
    union { unsigned bits; float real; } f = { .bits = 0xffc12345u };
    if (float_probe(f.real, 1.) != 7 || float_probe(minus_zero, -infinity) != -7 ||
        float_probe(0.0f, infinity) != 8)
        return 6;
    char a[4] = { 0, 0, 0, 0 }, b[4] = { 0, 0, 0, 0 };
    if (fill(a, 3) != a || copy(b, a, 3) != b ||
        b[0] != 0x59 || b[2] != 0x59 || b[3] != 0 || compare(a, b, 4))
        return 7;
    if (plus_one(41) != 42 || initial_address != (unsigned)(void (*)(void))plus_one) return 10;
    if (sizeof(padded) != 8 || padded.low != 1 || padded.high != 2) return 11;
    unsigned scratch[3] = { 12, 0, 123 };
    if (register_operands(scratch, 0x1122334400000155ULL, 0xabc) !=
        ((0x155 ^ 85) ^ (0x155 + 7) ^ 0xabc) ||
        scratch[0] != 15 || scratch[1] != 0x155 + 7 || scratch[2] != 123)
        return 12;
    struct PackedScalar pair = { 17, 0x01234567u };
    if (packed_scalar(&pair, 0x89abcdefu) != 0x01234567u ||
        pair.value != 0x89abcdefu || pair.tag != 17) return 13;
    unsigned long long shifts[3];
    for (unsigned count = 0; count < 64; ++count) {
        unsigned long long value = 0x89abcdef01234567ULL;
        wide_shifts(value, count, shifts);
        if (shifts[0] != (value << count) || shifts[1] != (value >> count) ||
            shifts[2] != (unsigned long long)((long long)value >> count))
            return 14;
    }
    return weak_probe() == 74 ? 0 : 8;
}
