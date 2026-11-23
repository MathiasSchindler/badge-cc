#include <stdarg.h>
#include <stdint.h>

_Noreturn void mp_raise_msg_varg(const void *, const void *, ...);
_Noreturn void gcc_mp_raise_msg_varg(const void *, const void *, ...);

typedef void (*raise_fn)(const void *, const void *, ...);
static intptr_t unwind[5];
static unsigned marker, format;
static volatile uint32_t consumed;

void *mp_obj_new_exception_msg_vlist(const void *type, const void *fmt, va_list args) {
    if (type != &marker || fmt != &format) return 0;
    unsigned first = va_arg(args, unsigned);
    uint64_t wide = va_arg(args, uint64_t);
    unsigned last = va_arg(args, unsigned);
    if (first != 0x1234u || wide != UINT64_C(0xa1b2c3d4e5f67890) || last != 0xabcd)
        return 0;
    return &marker;
}

_Noreturn void nlr_jump(void *value) {
    consumed = value == &marker;
    __builtin_longjmp(unwind, 1);
}

static int check(raise_fn fn) {
    consumed = 0;
    if (__builtin_setjmp(unwind) == 0) {
        fn(&marker, &format, 0x1234u, UINT64_C(0xa1b2c3d4e5f67890), 0xabcdu);
        return 0;
    }
    return consumed != 0;
}

int main(void) {
    return check(gcc_mp_raise_msg_varg) && check(mp_raise_msg_varg) ? 0 : 1;
}
