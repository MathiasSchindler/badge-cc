typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef unsigned short u16;
struct Small { i8 a; u16 b; };
struct Layout { i8 head; int *pointer; u64 wide; u16 tail; };
union Overlay { u64 wide; unsigned char bytes[8]; };
static volatile unsigned calls, trace;
static volatile int observed;
static int values[8];
static int effect(unsigned tag, int value) {
    calls++;
    trace = trace * 10u + tag;
    observed = value;
    return value;
}
int relation(int a, u32 b) { return a < b; }
int condition(int a, u32 b) { if (a < b) return 3; return 7; }
int nested(int a, int b, int c) {
    return a ? (b ? effect(1u, 11) : effect(2u, 22)) :
        (c && effect(3u, -3) ? effect(4u, 44) : effect(5u, 55));
}
u64 choose_wide(int flag, u64 a, u64 b) { return flag ? a : b; }
int *choose_pointer(int flag, int *a, int *b) { return flag ? a : b; }
_Bool choose_bool(int flag, int a, int b) { return flag ? a : b; }
i8 choose_narrow(int flag, i8 a, i8 b) { return flag ? a : b; }
u32 choose_mixed(int flag, int a, u32 b) { return flag ? a : b; }
struct Small choose_record(int flag, struct Small a, struct Small b) { return flag ? a : b; }
int logical_value(int a, int b, int c) { return (a < b && b < c) || !(a == c); }
int use_logical_arithmetic(int a, int b) { return ((a < b && b != 0) + 5) * 3; }
unsigned layout_size(void) { return sizeof(struct Layout); }
unsigned layout_alignment(void) { return _Alignof(struct Layout); }
unsigned overlay_size(void) { return sizeof(union Overlay); }
unsigned pointer_width(void) { return sizeof(int *); }
static int loops(void) {
    int n = 0, sum = 0;
    while (n < 8 && (n != 5 || n == 5)) {
        ++n;
        if (n == 3 || n == 4) continue;
        sum += n;
    }
    do { --n; } while (n > 2 && !(n == 2));
    for (int i = 0; i < 4 && n == 2; ++i) sum += i;
    goto finish;
finish:
    return sum + n;
}
static int edge_effects(int flag) {
    calls = trace = 0u;
    flag ? (void)effect(1u,7) : (void)effect(2u,8);
    if (calls != 1u || trace != (flag ? 1u : 2u)) return 1;
    if ((unsigned char)effect(3u,256)) return 2;
    if (_Generic(1, int: _Generic(1u, unsigned: effect(4u,0), default: 1), default: 1)) return 3;
    if (calls != 3u || trace != (flag ? 134u : 234u)) return 4;
    if (flag ? (effect(5u,1) && effect(6u,0)) : (effect(7u,0) || effect(8u,0))) return 5;
    if (calls != 5u || trace != (flag ? 13456u : 23478u)) return 6;
    struct Small a = {-2,9}, b = {3,11};
    struct Small saved = flag ? a : b;
    a.a = b.a = 0;
    if (saved.a != (flag ? -2 : 3) || saved.b != (flag ? 9 : 11)) return 7;
    return 0;
}
int main(void) {
    const int signed_values[6] = {-7, -1, 0, 1, 7, 32767};
    const u32 unsigned_values[6] = {0u, 1u, 7u, 32767u, 2147483648u, 4294967295u};
    for (unsigned i = 0; i < 6u; ++i) for (unsigned j = 0; j < 6u; ++j) {
        int a = signed_values[i];
        u32 b = unsigned_values[j];
        if (relation(a,b) != ((u32)a < b)) return 1;
        if (condition(a,b) != ((u32)a < b ? 3 : 7)) return 2;
        if (choose_mixed(i & 1u,a,b) != (i & 1u ? (u32)a : b)) return 3;
        if (use_logical_arithmetic(a, (int)j) != (((a < (int)j && j != 0u) + 5) * 3)) return 4;
    }
    for (unsigned a = 0; a < 2u; ++a) for (unsigned b = 0; b < 2u; ++b)
        for (unsigned c = 0; c < 2u; ++c) {
            calls = trace = 0u;
            int result = nested((int)a,(int)b,(int)c);
            unsigned expected_calls = a ? 1u : c ? 2u : 1u;
            unsigned expected_trace = a ? b ? 1u : 2u : c ? 34u : 5u;
            int expected_result = a ? b ? 11 : 22 : c ? 44 : 55;
            if (result != expected_result || calls != expected_calls || trace != expected_trace) return 5;
        }
    calls = trace = 0u;
    if (0 && effect(1u,7)) return 6;
    if (!(1 || effect(2u,8))) return 7;
    if (calls || trace) return 8;
    if (!(effect(1u,0) || effect(2u,3)) || calls != 2u || trace != 12u) return 9;
    calls = trace = 0u;
    if ((effect(3u,1), effect(4u,0)) || !(effect(5u,1) && effect(6u,-1))) return 10;
    if (calls != 4u || trace != 3456u) return 11;
    _Bool flag = choose_bool(1,256,0);
    if (!flag || choose_bool(0,1,-4) != 1 || choose_narrow(1,-128,127) != -128) return 12;
    if (choose_wide(0,0x123456789abcdef0ull,0xfedcba9876543210ull) != 0xfedcba9876543210ull) return 13;
    if (choose_pointer(0,values,values+3) != values+3 || choose_pointer(1,0,values) != 0) return 14;
    struct Small a = {-7,65535}, b = {5,32768};
    struct Small picked = choose_record(0,a,b);
    if (picked.a != 5 || picked.b != 32768) return 15;
    if (logical_value(1,2,3) != 1 || logical_value(3,2,3) != 0) return 16;
    if (_Generic(1u, unsigned: (effect(7u,1) && effect(8u,2)), default: effect(9u,0)) != 1) return 17;
    if ((unsigned char)(256 ? 256 : 1) != 0) return 18;
    if (loops() != 37) return 19;
    if (layout_size() != 24 || layout_alignment() != 8 || overlay_size() != 8 || pointer_width() != 4) return 20;
    if (edge_effects(0) || edge_effects(1)) return 21;
    return 0;
}
