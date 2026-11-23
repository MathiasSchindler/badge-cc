#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *object;
typedef object (*index_fn)(const object *, size_t, size_t, const object *);

object mp_seq_index_obj(const object *, size_t, size_t, const object *);
object before_seq_index_obj(const object *, size_t, size_t, const object *);
object gcc_seq_index_obj(const object *, size_t, size_t, const object *);

static intptr_t unwind[5];
static const unsigned char *volatile escaped_message;
static volatile unsigned string_comparisons;

const void *mp_obj_get_type(object value) { return value; }
bool mp_obj_equal(object a, object b) { return a == b; }
size_t mp_get_index(const void *type, size_t length, object value, bool slice) {
    (void)type;
    (void)length;
    (void)slice;
    return (uintptr_t)value >> 1;
}

int strcmp(const char *a, const char *b) {
    ++string_comparisons;
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (*x && *x == *y) { ++x; ++y; }
    return (int)*x - (int)*y;
}

_Noreturn void mp_raise_ValueError(const void *message) {
    escaped_message = message;
    __builtin_longjmp(unwind, 1);
}

static object small_int(size_t n) { return (object)((n << 1) | 1); }

static int check(index_fn fn, const object *items, size_t count) {
    object args[] = {small_int(0), small_int(2), small_int(2), small_int(4)};
    return fn(items, count, 2, args) == small_int(1) &&
        fn(items, count, 3, args) == small_int(3) &&
        fn(items, count, 4, args) == small_int(3);
}

static const unsigned char *miss(index_fn fn, const object *items, size_t count) {
    object args[] = {small_int(0), small_int(2), small_int(2), small_int(3)};
    if (__builtin_setjmp(unwind) == 0) {
        fn(items, count, 4, args);
        return NULL;
    }
    return escaped_message;
}

int main(void) {
    const object items[] = {small_int(1), small_int(2), small_int(3), small_int(2)};
    if (!check(gcc_seq_index_obj, items, 4) || !check(before_seq_index_obj, items, 4) ||
        !check(mp_seq_index_obj, items, 4))
        return 1;
    const unsigned char *gcc = miss(gcc_seq_index_obj, items, 4);
    unsigned initial = string_comparisons;
    const unsigned char *before = miss(before_seq_index_obj, items, 4);
    unsigned previous = string_comparisons;
    const unsigned char *after = miss(mp_seq_index_obj, items, 4);
    if (!gcc || !before || !after || *gcc != 0xff ||
        previous <= initial || string_comparisons != previous)
        return 2;
    for (size_t i = 0; i < 128; ++i) {
        if (gcc[i] != before[i] || gcc[i] != after[i]) return 3;
        if (!gcc[i]) return 0;
    }
    return 4;
}
