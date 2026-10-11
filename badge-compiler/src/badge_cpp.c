#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "host_io.h"
#include "c_lexer.h"

#define MAX_MACROS C_LEXER_MAX_MACROS
#define MAX_PARAMS 32u
#define MAX_DEPTH 64u
#define MAX_TOKENS C_LEXER_MAX_TOKENS
#define MAX_BYTES (16u * 1024u * 1024u)
#define MAX_ARENA (128u * 1024u * 1024u)

typedef struct { char *data; size_t size, capacity; } string_t;
typedef struct block {
    struct block *next;
    max_align_t alignment;
    unsigned char data[];
} block_t;
typedef struct {
    char *name;
    char *params[MAX_PARAMS];
    size_t param_count;
    tokens_t body;
    int active, function, builtin;
} macro_t;
typedef struct {
    const char *path;
    dev_t device;
    ino_t inode;
    int once;
} input_t;
typedef struct {
    jmp_buf failure;
    block_t *blocks;
    size_t allocated, expansions;
    const char *file;
    unsigned line, expansion_depth;
    macro_t macros[MAX_MACROS];
    size_t macro_count;
    const char *includes[64];
    size_t include_count;
    input_t inputs[512];
    size_t input_count;
    string_t output;
    int assembly;
} cpp_t;

static void fail(cpp_t *c, const char *format, ...) {
    fprintf(stderr, "badge-cpp: %s:%u: ", c->file, c->line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    longjmp(c->failure, 1);
}

static void *allocate(cpp_t *c, size_t size) {
    if (size > MAX_ARENA - sizeof(block_t) ||
        size + sizeof(block_t) > MAX_ARENA - c->allocated)
        fail(c, "preprocessing memory limit exceeded");
    block_t *block = malloc(sizeof(*block) + size);
    if (block == NULL) fail(c, "out of memory");
    block->next = c->blocks;
    c->blocks = block;
    c->allocated += sizeof(*block) + size;
    memset(block->data, 0, size);
    return block->data;
}

static char *text(cpp_t *c, const char *start, size_t size) {
    char *copy = allocate(c, size + 1);
    memcpy(copy, start, size);
    return copy;
}

static void append(cpp_t *c, string_t *s, const char *bytes, size_t size) {
    if (size > MAX_BYTES - s->size) fail(c, "text size limit exceeded");
    size_t needed = s->size + size + 1;
    if (needed > s->capacity) {
        size_t capacity = s->capacity == 0 ? 256 : s->capacity;
        while (capacity < needed) capacity *= 2;
        char *data = allocate(c, capacity);
        if (s->size != 0) memcpy(data, s->data, s->size);
        s->data = data;
        s->capacity = capacity;
    }
    memcpy(s->data + s->size, bytes, size);
    s->size += size;
    s->data[s->size] = '\0';
}

static void reserve(cpp_t *c, tokens_t *v, size_t needed) {
    if (needed > MAX_TOKENS) fail(c, "token count limit exceeded");
    if (needed <= v->capacity) return;
    size_t capacity = v->capacity == 0 ? 16 : v->capacity;
    while (capacity < needed) capacity *= 2;
    token_t *items = allocate(c, capacity * sizeof(*items));
    if (v->count != 0) memcpy(items, v->items, v->count * sizeof(*items));
    v->items = items;
    v->capacity = capacity;
}

static void push(cpp_t *c, tokens_t *v, token_t token) {
    reserve(c, v, v->count + 1);
    v->items[v->count++] = token;
}

static int is_token(const token_t *t, const char *s) {
    return strcmp(t->text, s) == 0;
}

static void location(cpp_t *c, const token_t *t) {
    c->file = t->file;
    c->line = t->line;
}

static void *lexer_allocate(void *context, size_t size) { return allocate(context, size); }

static void lexer_error(void *context, const char *file, unsigned line, const char *message) {
    cpp_t *c = context;
    c->file = file;
    c->line = line;
    fail(c, "%s", message);
}

static tokens_t lex(cpp_t *c, const char *source, size_t length, const char *file) {
    c->file = file;
    c->line = 1;
    c_lexer_t lexer = {.context = c, .allocate = lexer_allocate, .error = lexer_error};
    tokens_t result = c_lex(&lexer, source, length, file);
    c->line = lexer.line;
    return result;
}

static macro_t *find_macro(cpp_t *c, const char *name) {
    for (size_t i = 0; i < c->macro_count; ++i)
        if (strcmp(c->macros[i].name, name) == 0) return &c->macros[i];
    return NULL;
}

static int defined(cpp_t *c, const char *name) {
    macro_t *m = find_macro(c, name);
    return m != NULL && m->active;
}

static void define_macro(cpp_t *c, tokens_t definition) {
    if (definition.count == 0 || definition.items[0].kind != IDENT) fail(c, "expected macro name");
    macro_t candidate = {.name = definition.items[0].text, .active = 1};
    size_t i = 1;
    if (i < definition.count && is_token(&definition.items[i], "(") && !definition.items[i].space) {
        candidate.function = 1;
        ++i;
        if (i < definition.count && !is_token(&definition.items[i], ")")) {
            for (;;) {
                if (i >= definition.count || definition.items[i].kind != IDENT)
                    fail(c, "expected macro parameter; variadic macros are unsupported");
                if (candidate.param_count == MAX_PARAMS) fail(c, "macro parameter limit exceeded");
                for (size_t j = 0; j < candidate.param_count; ++j)
                    if (strcmp(candidate.params[j], definition.items[i].text) == 0)
                        fail(c, "duplicate macro parameter");
                candidate.params[candidate.param_count++] = definition.items[i++].text;
                if (i < definition.count && is_token(&definition.items[i], ")")) break;
                if (i == definition.count || !is_token(&definition.items[i++], ",")) fail(c, "expected ',' or ')'");
            }
        }
        if (i == definition.count || !is_token(&definition.items[i++], ")")) fail(c, "unclosed macro parameters");
    }
    for (; i < definition.count; ++i) {
        if (is_token(&definition.items[i], "#") || is_token(&definition.items[i], "##"))
            fail(c, "macro stringification/token pasting is unsupported");
        push(c, &candidate.body, definition.items[i]);
    }
    macro_t *existing = find_macro(c, candidate.name);
    if (existing != NULL && existing->active) {
        int same = existing->builtin == 0 && existing->function == candidate.function &&
                   existing->param_count == candidate.param_count && existing->body.count == candidate.body.count;
        for (size_t j = 0; same && j < candidate.param_count; ++j)
            same = strcmp(existing->params[j], candidate.params[j]) == 0;
        for (size_t j = 0; same && j < candidate.body.count; ++j)
            same = strcmp(existing->body.items[j].text, candidate.body.items[j].text) == 0 &&
                (j == 0 || existing->body.items[j].space == candidate.body.items[j].space);
        if (!same) fail(c, "incompatible redefinition of %s", candidate.name);
        return;
    }
    if (existing == NULL) {
        if (c->macro_count == MAX_MACROS) fail(c, "macro count limit exceeded");
        existing = &c->macros[c->macro_count++];
    }
    *existing = candidate;
}

static char *quoted(cpp_t *c, const char *s) {
    string_t result = {0};
    append(c, &result, "\"", 1);
    for (; *s != '\0'; ++s) {
        if ((unsigned char)*s < 32 || (unsigned char)*s == 127) {
            char escape[5];
            snprintf(escape, sizeof(escape), "\\%03o", (unsigned char)*s);
            append(c, &result, escape, 4);
        } else {
            if (*s == '"' || *s == '\\') append(c, &result, "\\", 1);
            append(c, &result, s, 1);
        }
    }
    append(c, &result, "\"", 1);
    return result.data;
}

static void expand(cpp_t *c, tokens_t *v) {
    if (++c->expansion_depth > MAX_DEPTH) fail(c, "macro argument expansion depth exceeded");
    for (size_t i = 0; i < v->count;) {
        token_t invocation = v->items[i];
        location(c, &invocation);
        if (invocation.kind != IDENT) { ++i; continue; }
        if (is_token(&invocation, "_Pragma") || is_token(&invocation, "__COUNTER__") ||
            is_token(&invocation, "__DATE__") || is_token(&invocation, "__TIME__"))
            fail(c, "unsupported builtin %s", invocation.text);
        macro_t *m = find_macro(c, invocation.text);
        if (m == NULL || !m->active) { ++i; continue; }
        size_t id = (size_t)(m - c->macros);
        uint64_t bit = UINT64_C(1) << (id % 64u);
        if ((invocation.hide[id / 64u] & bit) != 0) { ++i; continue; }
        size_t end = i + 1;
        tokens_t raw[MAX_PARAMS] = {{0}}, args[MAX_PARAMS] = {{0}};
        int ready[MAX_PARAMS] = {0};
        uint64_t hide[MAX_MACROS / 64u];
        memcpy(hide, invocation.hide, sizeof(hide));
        if (m->function) {
            while (end < v->count && v->items[end].kind == NEWLINE) ++end;
            if (end == v->count || !is_token(&v->items[end], "(")) { ++i; continue; }
            size_t arg = 0;
            unsigned nesting = 0;
            ++end;
            for (; end < v->count; ++end) {
                token_t t = v->items[end];
                if (is_token(&t, ")") && nesting == 0) break;
                if (is_token(&t, ",") && nesting == 0) {
                    if (++arg == MAX_PARAMS) fail(c, "macro argument limit exceeded");
                    continue;
                }
                if (is_token(&t, "(")) ++nesting;
                if (is_token(&t, ")")) --nesting;
                if (t.kind != NEWLINE) push(c, &raw[arg], t);
            }
            if (end == v->count) fail(c, "unterminated invocation of %s", m->name);
            size_t argc = arg + 1;
            if (m->param_count == 0 && arg == 0 && raw[0].count == 0) argc = 0;
            if (argc != m->param_count) fail(c, "wrong argument count for %s", m->name);
            for (size_t j = 0; j < MAX_MACROS / 64u; ++j) hide[j] &= v->items[end].hide[j];
            ++end;
        }
        hide[id / 64u] |= bit;
        if (++c->expansions > 1000000u) fail(c, "macro expansion limit exceeded");
        tokens_t replacement = {0};
        if (m->builtin != 0) {
            token_t t = invocation;
            char number[32];
            if (m->builtin == 1) {
                snprintf(number, sizeof(number), "%u", invocation.line);
                t.text = text(c, number, strlen(number));
                t.kind = NUMBER;
            } else {
                t.text = quoted(c, invocation.file);
                t.kind = LITERAL;
            }
            memcpy(t.hide, hide, sizeof(hide));
            push(c, &replacement, t);
        } else for (size_t j = 0; j < m->body.count; ++j) {
            token_t t = m->body.items[j];
            size_t param = 0;
            while (param < m->param_count && strcmp(m->params[param], t.text) != 0) ++param;
            if (param < m->param_count) {
                if (!ready[param]) {
                    args[param] = raw[param];
                    expand(c, &args[param]);
                    ready[param] = 1;
                }
                for (size_t k = 0; k < args[param].count; ++k) {
                    token_t a = args[param].items[k];
                    for (size_t h = 0; h < MAX_MACROS / 64u; ++h) a.hide[h] |= hide[h];
                    push(c, &replacement, a);
                }
            } else {
                t.file = invocation.file;
                t.line = invocation.line;
                memcpy(t.hide, hide, sizeof(hide));
                push(c, &replacement, t);
            }
        }
        size_t tail = v->count - end;
        reserve(c, v, i + replacement.count + tail);
        memmove(v->items + i + replacement.count, v->items + end, tail * sizeof(token_t));
        if (replacement.count != 0)
            memcpy(v->items + i, replacement.items, replacement.count * sizeof(token_t));
        v->count = i + replacement.count + tail;
    }
    --c->expansion_depth;
}

typedef struct { uint64_t bits; int uns; } value_t;
typedef struct { cpp_t *cpp; tokens_t tokens; size_t at; unsigned depth; } expression_t;

static value_t expression(expression_t *e, int minimum, int evaluate);

static int take(expression_t *e, const char *s) {
    if (e->at == e->tokens.count || !is_token(&e->tokens.items[e->at], s)) return 0;
    ++e->at;
    return 1;
}

static value_t primary(expression_t *e, int evaluate) {
    cpp_t *c = e->cpp;
    if (++e->depth > MAX_DEPTH) fail(c, "conditional expression depth exceeded");
    value_t result;
    if (take(e, "(")) {
        result = expression(e, 1, evaluate);
        if (!take(e, ")")) fail(c, "expected ')' in conditional expression");
    } else if (e->at < e->tokens.count &&
               strchr("+-!~", e->tokens.items[e->at].text[0]) != NULL &&
               strlen(e->tokens.items[e->at].text) == 1) {
        char op = e->tokens.items[e->at++].text[0];
        result = primary(e, evaluate);
        if (evaluate) {
            if (op == '-') {
                if (!result.uns && result.bits == (UINT64_C(1) << 63)) fail(c, "signed overflow in conditional expression");
                result.bits = 0u - result.bits;
            }
            if (op == '~') result.bits = ~result.bits;
        }
        if (op == '!') result = (value_t){evaluate ? !result.bits : 0, 0};
    } else {
        if (e->at == e->tokens.count) fail(c, "missing conditional expression operand");
        token_t *t = &e->tokens.items[e->at++];
        location(c, t);
        if (t->kind == IDENT) {
            result = (value_t){0, 0};
        } else if (t->kind == NUMBER) {
            errno = 0;
            char *end;
            uint64_t number = strtoull(t->text, &end, 0);
            if (errno != 0 || end == t->text) fail(c, "invalid integer in conditional expression");
            size_t suffix_size = strlen(end);
            if (suffix_size > 3) fail(c, "invalid integer suffix in conditional expression");
            char suffix[4] = {0};
            for (size_t i = 0; i < suffix_size; ++i) {
                suffix[i] = (char)tolower((unsigned char)end[i]);
                if (i != 0 && suffix[i] == 'l' && suffix[i - 1] == 'l' && end[i] != end[i - 1])
                    fail(c, "invalid mixed-case long-long suffix");
            }
            if (strcmp(suffix, "") != 0 && strcmp(suffix, "u") != 0 &&
                strcmp(suffix, "l") != 0 && strcmp(suffix, "ul") != 0 &&
                strcmp(suffix, "lu") != 0 && strcmp(suffix, "ll") != 0 &&
                strcmp(suffix, "ull") != 0 && strcmp(suffix, "llu") != 0)
                fail(c, "invalid integer suffix in conditional expression");
            int uns = strchr(suffix, 'u') != NULL;
            if (!uns && number > INT64_MAX && t->text[0] != '0')
                fail(c, "decimal integer exceeds signed preprocessing range");
            if (number > INT64_MAX) uns = 1;
            result = (value_t){number, uns};
        } else {
            fail(c, "only integer operands are supported in conditional expressions");
            result = (value_t){0, 0};
        }
    }
    --e->depth;
    return result;
}

static int precedence(const char *op) {
    if (strcmp(op, "||") == 0) return 2;
    if (strcmp(op, "&&") == 0) return 3;
    if (strcmp(op, "|") == 0) return 4;
    if (strcmp(op, "^") == 0) return 5;
    if (strcmp(op, "&") == 0) return 6;
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) return 7;
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) return 8;
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) return 9;
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0) return 10;
    if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0) return 11;
    return 0;
}

static value_t operation(cpp_t *c, const char *op, value_t a, value_t b) {
    int uns = a.uns || b.uns;
    int64_t x = (int64_t)a.bits, y = (int64_t)b.bits, signed_result = 0;
    uint64_t result = 0;
    if (strcmp(op, "&&") == 0) return (value_t){a.bits != 0 && b.bits != 0, 0};
    if (strcmp(op, "||") == 0) return (value_t){a.bits != 0 || b.bits != 0, 0};
    if (strcmp(op, "==") == 0) return (value_t){a.bits == b.bits, 0};
    if (strcmp(op, "!=") == 0) return (value_t){a.bits != b.bits, 0};
    if (strcmp(op, "<") == 0) return (value_t){uns ? a.bits < b.bits : x < y, 0};
    if (strcmp(op, ">") == 0) return (value_t){uns ? a.bits > b.bits : x > y, 0};
    if (strcmp(op, "<=") == 0) return (value_t){uns ? a.bits <= b.bits : x <= y, 0};
    if (strcmp(op, ">=") == 0) return (value_t){uns ? a.bits >= b.bits : x >= y, 0};
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
        if (b.bits >= 64) fail(c, "invalid shift in conditional expression");
        if (strcmp(op, "<<") == 0) {
            if (!a.uns && (x < 0 || a.bits > (uint64_t)INT64_MAX >> b.bits))
                fail(c, "invalid signed left shift in conditional expression");
            result = a.bits << b.bits;
        } else result = a.uns ? a.bits >> b.bits : (uint64_t)(x >> b.bits);
        return (value_t){result, a.uns};
    }
    if (strcmp(op, "&") == 0) result = a.bits & b.bits;
    if (strcmp(op, "|") == 0) result = a.bits | b.bits;
    if (strcmp(op, "^") == 0) result = a.bits ^ b.bits;
    if (strcmp(op, "+") == 0) {
        if (!uns && __builtin_add_overflow(x, y, &signed_result)) fail(c, "signed overflow in conditional expression");
        result = a.bits + b.bits;
    }
    if (strcmp(op, "-") == 0) {
        if (!uns && __builtin_sub_overflow(x, y, &signed_result)) fail(c, "signed overflow in conditional expression");
        result = a.bits - b.bits;
    }
    if (strcmp(op, "*") == 0) {
        if (!uns && __builtin_mul_overflow(x, y, &signed_result)) fail(c, "signed overflow in conditional expression");
        result = a.bits * b.bits;
    }
    if (strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
        if (b.bits == 0) fail(c, "division by zero in conditional expression");
        if (!uns && x == INT64_MIN && y == -1) fail(c, "signed overflow in conditional expression");
        if (strcmp(op, "/") == 0) result = uns ? a.bits / b.bits : (uint64_t)(x / y);
        else result = uns ? a.bits % b.bits : (uint64_t)(x % y);
    }
    return (value_t){result, uns};
}

static value_t expression(expression_t *e, int minimum, int evaluate) {
    value_t left = primary(e, evaluate);
    while (e->at < e->tokens.count) {
        const char *op = e->tokens.items[e->at].text;
        int p = precedence(op);
        if (p < minimum) break;
        ++e->at;
        int run = evaluate;
        if (strcmp(op, "&&") == 0 && left.bits == 0) run = 0;
        if (strcmp(op, "||") == 0 && left.bits != 0) run = 0;
        value_t right = expression(e, p + 1, run);
        if (evaluate) left = operation(e->cpp, op, left, right);
        else {
            int uns = left.uns || right.uns;
            if (p <= 3 || p == 7 || p == 8) uns = 0;
            if (p == 9) uns = left.uns;
            left = (value_t){0, uns};
        }
    }
    if (minimum == 1 && take(e, "?")) {
        if (++e->depth > MAX_DEPTH) fail(e->cpp, "conditional expression depth exceeded");
        value_t yes = expression(e, 1, evaluate && left.bits != 0);
        if (!take(e, ":")) fail(e->cpp, "expected ':' in conditional expression");
        value_t no = expression(e, 1, evaluate && left.bits == 0);
        left = left.bits != 0 ? yes : no;
        left.uns = yes.uns || no.uns;
        --e->depth;
    }
    return left;
}

static int condition(cpp_t *c, tokens_t input) {
    tokens_t prepared = {0};
    for (size_t i = 0; i < input.count; ++i) {
        token_t t = input.items[i];
        if (is_token(&t, "defined")) {
            ++i;
            int paren = i < input.count && is_token(&input.items[i], "(");
            if (paren) ++i;
            if (i == input.count || input.items[i].kind != IDENT) fail(c, "expected identifier after defined");
            t.text = defined(c, input.items[i].text) ? "1" : "0";
            t.kind = NUMBER;
            if (paren && (++i == input.count || !is_token(&input.items[i], ")")))
                fail(c, "expected ')' after defined");
        }
        push(c, &prepared, t);
    }
    expand(c, &prepared);
    for (size_t i = 0; i < prepared.count; ++i)
        if (is_token(&prepared.items[i], "defined")) fail(c, "macro-generated defined is unsupported");
    expression_t e = {.cpp = c, .tokens = prepared};
    value_t result = expression(&e, 1, 1);
    if (e.at != prepared.count) fail(c, "unexpected token in conditional expression");
    return result.bits != 0;
}

static void emit(cpp_t *c, tokens_t *tokens) {
    expand(c, tokens);
    const char *file = NULL;
    unsigned line = 0;
    for (size_t i = 0; i < tokens->count; ++i) {
        token_t *t = &tokens->items[i];
        if (t->kind == NEWLINE) continue;
        if (file == NULL || strcmp(file, t->file) != 0 || line != t->line) {
            char number[48];
            snprintf(number, sizeof(number), "\n# %u ", t->line);
            append(c, &c->output, number, strlen(number));
            char *name = quoted(c, t->file);
            append(c, &c->output, name, strlen(name));
            append(c, &c->output, "\n", 1);
            file = t->file;
            line = t->line;
        }
        if(c->assembly && t->space)append(c,&c->output," ",1);
        append(c, &c->output, t->text, strlen(t->text));
        if(!c->assembly)append(c, &c->output, " ", 1);
    }
    append(c, &c->output, "\n", 1);
}

static char *read_source(cpp_t *c, const char *path, size_t *length) {
    struct stat info;
    if (stat(path, &info) != 0) fail(c, "cannot inspect %s: %s", path, strerror(errno));
    if (!S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > 8 * 1024 * 1024)
        fail(c, "source must be a regular file of at most 8 MiB");
    size_t size = (size_t)info.st_size;
    char *bytes = allocate(c, size + 1);
    FILE *file = fopen(path, "rb");
    if (file == NULL) fail(c, "cannot open %s: %s", path, strerror(errno));
    int ok = fread(bytes, 1, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    if (!ok) fail(c, "cannot read/close %s", path);
    *length = size;
    return bytes;
}

static void process_file(cpp_t *c, const char *path, unsigned depth);

static char *join_path(cpp_t *c, const char *base, size_t length, const char *name) {
    string_t s = {0};
    append(c, &s, base, length);
    if (length != 0) append(c, &s, "/", 1);
    append(c, &s, name, strlen(name));
    return s.data;
}

static int exists(cpp_t *c, const char *path) {
    struct stat info;
    if (stat(path, &info) == 0) return 1;
    if (errno == ENOENT || errno == ENOTDIR) return 0;
    fail(c, "cannot inspect include %s: %s", path, strerror(errno));
    return 0;
}

static void include_file(cpp_t *c, const char *parent, tokens_t args, unsigned depth) {
    expand(c, &args);
    char *name = NULL;
    int local = 0;
    if (args.count == 1 && args.items[0].kind == LITERAL &&
        args.items[0].text[0] == '"') {
        size_t length = strlen(args.items[0].text);
        name = text(c, args.items[0].text + 1, length - 2);
        if (strchr(name, '\\') != NULL) fail(c, "backslashes in include names are unsupported");
        local = 1;
    } else if (args.count >= 3 && is_token(&args.items[0], "<") &&
               is_token(&args.items[args.count - 1], ">")) {
        string_t s = {0};
        for (size_t i = 1; i + 1 < args.count; ++i) {
            if (args.items[i].space) fail(c, "whitespace inside angle include names is unsupported");
            append(c, &s, args.items[i].text, strlen(args.items[i].text));
        }
        name = s.data;
    } else fail(c, "expected a quoted or angle-bracket include name");
    if (name[0] == '\0') fail(c, "empty include name");
    if (name[0] == '/') {
        if (!exists(c, name)) fail(c, "include not found: %s", name);
        process_file(c, name, depth + 1);
        return;
    }
    if (local) {
        const char *slash = strrchr(parent, '/');
        char *candidate = join_path(c, parent, slash == NULL ? 0 : (size_t)(slash - parent), name);
        if (exists(c, candidate)) { process_file(c, candidate, depth + 1); return; }
    }
    for (size_t i = 0; i < c->include_count; ++i) {
        char *candidate = join_path(c, c->includes[i], strlen(c->includes[i]), name);
        if (exists(c, candidate)) { process_file(c, candidate, depth + 1); return; }
    }
    fail(c, "include not found: %s", name);
}

typedef struct { int parent, active, taken, saw_else; } conditional_t;

static void process_file(cpp_t *c, const char *path, unsigned depth) {
    c->file = path;
    c->line = 1;
    if (depth > MAX_DEPTH) fail(c, "include depth exceeded");
    char canonical[PATH_MAX];
    if (realpath(path, canonical) == NULL) fail(c, "cannot resolve input %s: %s", path, strerror(errno));
    struct stat info;
    if (stat(path, &info) != 0) fail(c, "cannot inspect %s: %s", path, strerror(errno));
    input_t *entry = NULL;
    for (size_t i = 0; i < c->input_count; ++i) {
        if (c->inputs[i].device == info.st_dev && c->inputs[i].inode == info.st_ino) {
            entry = &c->inputs[i]; break;
        }
    }
    if (entry != NULL && entry->once) return;
    if (entry == NULL) {
        if (c->input_count == 512) fail(c, "input file count limit exceeded");
        entry = &c->inputs[c->input_count++];
        *entry = (input_t){.path = text(c, canonical, strlen(canonical)),
                          .device = info.st_dev, .inode = info.st_ino};
    }
    size_t length;
    char *source = read_source(c, path, &length);
    tokens_t all = lex(c, source, length, path), pending = {0};
    conditional_t stack[MAX_DEPTH];
    size_t nesting = 0;
    int active = 1;
    for (size_t begin = 0; begin < all.count;) {
        size_t end = begin;
        while (end < all.count && all.items[end].kind != NEWLINE) ++end;
        if (begin < end && is_token(&all.items[begin], "#")) {
            if (pending.count != 0) { emit(c, &pending); pending.count = 0; }
            location(c, &all.items[begin]);
            if (begin + 1 < end) {
                const char *directive = all.items[begin + 1].text;
                tokens_t args = {0};
                for (size_t i = begin + 2; i < end; ++i) push(c, &args, all.items[i]);
                if (strcmp(directive, "if") == 0 || strcmp(directive, "ifdef") == 0 ||
                    strcmp(directive, "ifndef") == 0) {
                    if (nesting == MAX_DEPTH) fail(c, "conditional nesting limit exceeded");
                    int selected = 0;
                    if (strcmp(directive, "if") == 0) {
                        if (active) selected = condition(c, args);
                    } else {
                        if (args.count != 1 || args.items[0].kind != IDENT) fail(c, "expected one identifier");
                        selected = defined(c, args.items[0].text);
                        if (strcmp(directive, "ifndef") == 0) selected = !selected;
                    }
                    stack[nesting++] = (conditional_t){active, active && selected, active && selected, 0};
                    active = stack[nesting - 1].active;
                } else if (strcmp(directive, "elif") == 0 || strcmp(directive, "else") == 0 ||
                           strcmp(directive, "endif") == 0) {
                    if (nesting == 0) fail(c, "conditional directive without matching #if");
                    conditional_t *s = &stack[nesting - 1];
                    if (strcmp(directive, "endif") == 0) {
                        if (args.count != 0) fail(c, "tokens after #endif");
                        active = s->parent;
                        --nesting;
                    } else {
                        if (s->saw_else) fail(c, "conditional directive after #else");
                        int selected = 1;
                        if (strcmp(directive, "else") == 0) {
                            if (args.count != 0) fail(c, "tokens after #else");
                            s->saw_else = 1;
                        } else if (s->parent && !s->taken) selected = condition(c, args);
                        s->active = s->parent && !s->taken && selected;
                        s->taken |= s->active;
                        active = s->active;
                    }
                } else if (active) {
                    if (strcmp(directive, "define") == 0) define_macro(c, args);
                    else if (strcmp(directive, "undef") == 0) {
                        if (args.count != 1 || args.items[0].kind != IDENT) fail(c, "expected one identifier");
                        macro_t *m = find_macro(c, args.items[0].text);
                        if (m != NULL) m->active = 0;
                    } else if (strcmp(directive, "include") == 0) include_file(c, path, args, depth);
                    else if (strcmp(directive, "pragma") == 0 && args.count == 1 &&
                             is_token(&args.items[0], "once")) entry->once = 1;
                    else if (strcmp(directive, "error") == 0) {
                        string_t message = {0};
                        for (size_t i = 0; i < args.count; ++i) {
                            append(c, &message, args.items[i].text, strlen(args.items[i].text));
                            append(c, &message, " ", 1);
                        }
                        fail(c, "#error %s", message.data == NULL ? "" : message.data);
                    } else fail(c, "unsupported directive #%s", directive);
                }
            }
        } else if (active) {
            for (size_t i = begin; i < end; ++i) push(c, &pending, all.items[i]);
            if (end < all.count) push(c, &pending, all.items[end]);
        }
        begin = end < all.count ? end + 1 : end;
    }
    c->file = path;
    c->line = all.count == 0 ? 1 : all.items[all.count - 1].line;
    if (nesting != 0) fail(c, "unterminated conditional directive");
    if (pending.count != 0) emit(c, &pending);
}

static void install(cpp_t *c, const char *definition) {
    tokens_t tokens = lex(c, definition, strlen(definition), "<built-in>");
    define_macro(c, tokens);
}

static void builtins(cpp_t *c) {
    static const char *const definitions[] = {
        "__UINT8_TYPE__ unsigned char", "__UINT16_TYPE__ unsigned short",
        "__UINT32_TYPE__ unsigned int", "__UINT64_TYPE__ long long unsigned int",
        "__INT8_TYPE__ signed char", "__INT16_TYPE__ short", "__INT32_TYPE__ int",
        "__INT64_TYPE__ long long int", "__UINTPTR_TYPE__ unsigned int", "__INTPTR_TYPE__ int",
        "__SIZE_TYPE__ unsigned int", "__PTRDIFF_TYPE__ int",
        "__STDC__ 1", "__STDC_VERSION__ 201112L", "__STDC_HOSTED__ 0",
        "__CHAR_UNSIGNED__ 1", "__SIZEOF_POINTER__ 4", "__SIZEOF_INT__ 4",
        "__SIZEOF_LONG__ 4", "__SIZEOF_LONG_LONG__ 8", "__SIZEOF_SHORT__ 2",
        "__arm__ 1", "__thumb__ 1", "__ARM_ARCH 8", "__ARM_ARCH_8M_MAIN__ 1",
        "__ARM_ARCH_PROFILE 'M'", "__ARM_ARCH_ISA_THUMB 2", "__ARM_PCS 1", "__SOFTFP__ 1"
    };
    for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) install(c, definitions[i]);
    install(c, "__LINE__");
    find_macro(c, "__LINE__")->builtin = 1;
    install(c, "__FILE__");
    find_macro(c, "__FILE__")->builtin = 2;
}

static void normalized_tokens(cpp_t *c, const char *path) {
    size_t length;
    char *source = read_source(c, path, &length);
    tokens_t all = lex(c, source, length, path);
    for (size_t begin = 0; begin < all.count;) {
        size_t end = begin;
        while (end < all.count && all.items[end].kind != NEWLINE) ++end;
        if (begin < end && is_token(&all.items[begin], "#")) {
            if (begin + 1 == end ||
                (all.items[begin + 1].kind != NUMBER && !is_token(&all.items[begin + 1], "line")))
                fail(c, "only line markers are allowed in --tokens input");
        } else for (size_t i = begin; i < end; ++i) {
            append(c, &c->output, all.items[i].text, strlen(all.items[i].text));
            append(c, &c->output, "\n", 1);
        }
        begin = end < all.count ? end + 1 : end;
    }
}

int main(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        fprintf(argc == 1 ? stderr : stdout,
                "usage: badge-cpp [-E] [--assembly] [-I dir] [-D name[=value]] [-U name] [-o output.i] input.c\n"
                "       badge-cpp --tokens [-o output.tokens] input.i\n");
        return argc == 1 ? 2 : 0;
    }
    cpp_t *c = calloc(1, sizeof(*c));
    if (c == NULL) { fputs("badge-cpp: out of memory\n", stderr); return 1; }
    c->file = "<command line>";
    c->line = 1;
    int result = 1;
    if (setjmp(c->failure) == 0) {
        builtins(c);
        for(int j=1;j<argc;j++)if(!strcmp(argv[j],"--assembly")){
            c->assembly=1;define_macro(c,lex(c,"__ASSEMBLER__ 1",15,"<built-in>"));break;
        }
        c->file = "<command line>";
        c->line = 1;
        const char *input = NULL, *output = NULL;
        int tokens_only = 0;
        for (int i = 1; i < argc; ++i) {
            const char *arg = argv[i];
            if (strcmp(arg, "-E") == 0) continue;
            if (strcmp(arg, "--assembly") == 0) continue;
            if (strcmp(arg, "--tokens") == 0) { tokens_only = 1; continue; }
            if (strcmp(arg, "-o") == 0) {
                if (++i == argc || output != NULL) fail(c, "expected one output path");
                output = argv[i];
            } else if (arg[0] == '-' && strchr("IDU", arg[1]) != NULL && arg[1] != '\0') {
                char option = arg[1];
                const char *value = arg + 2;
                if (*value == '\0') {
                    if (++i == argc) fail(c, "missing option argument");
                    value = argv[i];
                }
                if (option == 'I') {
                    if (c->include_count == 64) fail(c, "include directory limit exceeded");
                    c->includes[c->include_count++] = value;
                } else if (option == 'D') {
                    const char *equal = strchr(value, '=');
                    string_t definition = {0};
                    append(c, &definition, value, equal == NULL ? strlen(value) : (size_t)(equal - value));
                    append(c, &definition, " ", 1);
                    const char *replacement = equal == NULL ? "1" : equal + 1;
                    append(c, &definition, replacement, strlen(replacement));
                    define_macro(c, lex(c, definition.data, definition.size, "<command line>"));
                } else {
                    tokens_t name = lex(c, value, strlen(value), "<command line>");
                    if (name.count != 1 || name.items[0].kind != IDENT) fail(c, "expected identifier after -U");
                    macro_t *m = find_macro(c, value);
                    if (m != NULL) m->active = 0;
                }
            } else if (arg[0] == '-') fail(c, "unsupported option %s", arg);
            else {
                if (input != NULL) fail(c, "expected one input file");
                input = arg;
            }
        }
        if (input == NULL) fail(c, "missing input file");
        if (tokens_only) normalized_tokens(c, input);
        else process_file(c, input, 0);
        if (output != NULL) {
            struct stat existing;
            if (stat(output, &existing) == 0) {
                for (size_t i = 0; i < c->input_count; ++i)
                    if (existing.st_dev == c->inputs[i].device && existing.st_ino == c->inputs[i].inode)
                        fail(c, "output aliases an input file");
                struct stat primary_input;
                if (stat(input, &primary_input) != 0) fail(c, "cannot inspect input");
                if (existing.st_dev == primary_input.st_dev && existing.st_ino == primary_input.st_ino)
                    fail(c, "output aliases an input file");
            } else if (errno != ENOENT) fail(c, "cannot inspect output: %s", strerror(errno));
            result = host_write_file("badge-cpp", output, (const unsigned char *)c->output.data, c->output.size) ? 0 : 1;
        } else {
            if (c->output.size != 0 && fwrite(c->output.data, 1, c->output.size, stdout) != c->output.size)
                fail(c, "cannot write stdout");
            if (fflush(stdout) != 0) fail(c, "cannot flush stdout");
            result = 0;
        }
    }
    block_t *block = c->blocks;
    while (block != NULL) { block_t *next = block->next; free(block); block = next; }
    free(c);
    return result;
}
