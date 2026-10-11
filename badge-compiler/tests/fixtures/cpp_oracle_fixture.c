/* Preprocessor-only fixture: the resulting tokens need not form a C program. */
#define PLUS(x) ((x) + 1)
#define ALIAS PLUS
PLUS(PLUS(1)) ALIAS(2)
PLUS
(3)
#define ZERO() 0
#define EMPTY(x) x
ZERO() EMPTY()
#define IGNORE(x) 5
IGNORE(PLUS(1, 2))
#define RECURSE RECURSE
RECURSE RECURSE
#define FIRST SECOND
#define SECOND FIRST
FIRST SECOND
#define f(a) a*g
#define g(a) f(a)
f(2)(9)
#undef f
#undef g
#define f(a) f(x * (a))
#define x 2
#define g f
#define z z[0]
#define h g(~
#define m(a) a(w)
#define w 0,1
#define t(a) a
f(y+1) + f(f(z)) % t(t(g)(0) + t)(1);
g(x+(3,4)-w) | h 5) & m(f) ^ m(m);
#define COMMENT /* a multiline
comment */ 7
COMMENT
int/**/separated;
"// literal" '\'' u8"/* literal */"
CLI
#ifdef REMOVED
#error -U did not remove the macro
#endif
#if defined(CLI) && CLI == 7 && !defined(REMOVED)
selected
#else
#error wrong branch
#endif
#if (1 ? -1 : 1 + 1U) < 0
#error wrong conditional type
#endif
#if (1 ? -1 : !1U) < 0 && (1 || 1/0) && (0 ? 1/0 : 2) == 2
conditional
#endif
__LINE__ __FILE__ __UINT32_TYPE__ __INT64_TYPE__
#define LINE_AT_USE __LINE__
LINE_AT_USE\
 + __LINE__
#define BLOCK_LINE /* across
physical lines */ __LINE__
BLOCK_LINE
#define COMMENTED /* a multiline
comment */ (x)
COMMENTED
