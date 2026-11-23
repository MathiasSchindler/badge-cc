#include <stddef.h>
#include <stdint.h>

typedef struct pool {
    const struct pool *prev;
    size_t total_prev_len : 31;
    size_t is_sorted : 1;
    size_t alloc, len;
    const uint8_t *hashes, *lengths;
    const char *qstrs[];
} pool;

_Static_assert(sizeof(pool) == 24 && offsetof(pool, qstrs) == 24, "RP2 qstr pool layout");

extern const pool mp_qstr_const_pool, mp_qstr_const_pool_static;
extern const pool gcc_mp_qstr_const_pool, gcc_mp_qstr_const_pool_static;
size_t qstr_compute_hash(const uint8_t *, size_t);
size_t gcc_qstr_compute_hash(const uint8_t *, size_t);

static int matches(const pool *a, const pool *b) {
    if (a->total_prev_len != b->total_prev_len || a->is_sorted != b->is_sorted ||
        a->alloc != b->alloc || a->len != b->len || !a->len)
        return 0;
    for (size_t i = 0; i < a->len; ++i) {
        if (a->hashes[i] != b->hashes[i] || a->lengths[i] != b->lengths[i])
            return 0;
        size_t length = b->lengths[i];
        for (size_t n = 0; n <= length; ++n)
            if (a->qstrs[i][n] != b->qstrs[i][n])
                return 0;
        if (qstr_compute_hash((const uint8_t *)a->qstrs[i], length) !=
            gcc_qstr_compute_hash((const uint8_t *)b->qstrs[i], length))
            return 0;
    }
    return 1;
}

int main(void) {
    if (mp_qstr_const_pool_static.prev || gcc_mp_qstr_const_pool_static.prev ||
        mp_qstr_const_pool.prev != &mp_qstr_const_pool_static ||
        gcc_mp_qstr_const_pool.prev != &gcc_mp_qstr_const_pool_static)
        return 1;
    if (!matches(&mp_qstr_const_pool_static, &gcc_mp_qstr_const_pool_static))
        return 2;
    return matches(&mp_qstr_const_pool, &gcc_mp_qstr_const_pool) ? 0 : 3;
}
