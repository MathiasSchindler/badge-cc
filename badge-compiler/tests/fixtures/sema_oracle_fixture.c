struct Layout { char c; short s; int i; long l; long long ll; char tail; };
union Overlay { char c[9]; long long ll; };
enum Positive { P0, P2 = 2 };
enum Negative { N1 = -1, N0 };

unsigned oracle_pointer_size = sizeof(void *);
unsigned oracle_long_size = sizeof(long);
unsigned oracle_long_double_size = sizeof(long double);
unsigned oracle_layout_size = sizeof(struct Layout);
unsigned oracle_layout_align = _Alignof(struct Layout);
unsigned oracle_ll_offset = (unsigned)&((struct Layout *)0)->ll;
unsigned oracle_tail_offset = (unsigned)&((struct Layout *)0)->tail;
unsigned oracle_union_size = sizeof(union Overlay);
unsigned oracle_char_unsigned = (char)255 > 0;
unsigned oracle_enum_unsigned = ((enum Positive)0 - 1) > 0;
unsigned oracle_enum_signed = ((enum Negative)0 - 1) < 0;
unsigned oracle_unsigned_wrap = 0xffffffffu + 1u;
unsigned oracle_mixed_rank = (-1L < 1u);
long long oracle_sign_extension = (signed char)255;
unsigned oracle_short_circuit = 0 && (1 / 0);
unsigned oracle_conditional = 1 ? 7 : (1 / 0);
unsigned oracle_shift = 1ull << 40 >> 39;

_Static_assert(sizeof(struct Layout) == 32, "target struct layout");
_Static_assert(_Alignof(struct Layout) == 8, "target struct alignment");
_Static_assert(sizeof(union Overlay) == 16, "target union layout");
_Static_assert(sizeof(long double) == 8, "target long double");
