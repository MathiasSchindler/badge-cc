#include "codegen.h"
#include "codegen_internal.h"
#include "target.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Noreturn void cg_error(cg_context *c, const char *message) {
  fprintf(stderr, "badge-codegen: %s:%u: %s\n", c->file, c->line, message);
  longjmp(c->failure, 1);
}
void *cg_alloc(cg_context *c, size_t n) {
  if (n > 128u * 1024u * 1024u - c->memory)
    cg_error(c, "backend allocation limit exceeded");
  cg_allocation *a = calloc(1, sizeof(*a) + n);
  if (!a)
    cg_error(c, "out of memory");
  a->next = c->allocations;
  c->allocations = a;
  c->memory += n;
  return a + 1;
}
static char *cg_copy(cg_context *c, const char *p, size_t n) {
  char *s = cg_alloc(c, n + 1);
  memcpy(s, p, n);
  return s;
}
void cg_emit(cg_context *c, const char *format, ...) {
  if (cg_schedule_selection(c) && !c->schedule_flushing) {
    if (c->schedule_active)
      c->schedule_barriers++;
    cg_schedule_flush(c);
  }
  if (!c->capacity) {
    c->out = malloc(4096);
    if (!c->out)
      cg_error(c, "out of memory");
    c->capacity = 4096;
  }
  va_list ap, copy;
  va_start(ap, format);
  va_copy(copy, ap);
  int n = vsnprintf(c->out + c->size, c->capacity - c->size, format, copy);
  va_end(copy);
  if (n < 0 || (size_t)n > 64u * 1024u * 1024u - c->size)
    cg_error(c, "assembly output limit exceeded");
  size_t needed = c->size + (size_t)n + 1;
  if (needed > c->capacity) {
    size_t cap = c->capacity ? c->capacity : 4096;
    while (cap < needed)
      cap *= 2;
    char *p = realloc(c->out, cap);
    if (!p)
      cg_error(c, "out of memory");
    c->out = p;
    c->capacity = cap;
    vsnprintf(c->out + c->size, (size_t)n + 1, format, ap);
  }
  va_end(ap);
  c->size += (size_t)n;
}
static int cg_is(cg_context *c, const char *s) {
  return c->pos < c->count && !strcmp(c->tokens[c->pos].text, s);
}
static char *cg_take(cg_context *c) {
  if (c->pos >= c->count)
    cg_error(c, "unexpected end of IR");
  c->line = c->tokens[c->pos].line;
  return c->tokens[c->pos++].text;
}
static int cg_accept(cg_context *c, const char *s) {
  if (!cg_is(c, s))
    return 0;
  cg_take(c);
  return 1;
}
static void cg_expect(cg_context *c, const char *s) {
  if (!cg_accept(c, s))
    cg_error(c, "unexpected IR token");
}
static uint64_t cg_number(cg_context *c, const char *s) {
  char *end;
  errno = 0;
  uint64_t n = strtoull(s, &end, 10);
  if (end == s || *end || errno == ERANGE)
    cg_error(c, "invalid integer constant");
  return n;
}
unsigned cg_round(unsigned n, unsigned a) { return (n + a - 1) & ~(a - 1); }
static void cg_lex(cg_context *c, const char *source, size_t length) {
  size_t maximum = length < 1000000 ? length : 1000000;
  c->tokens = cg_alloc(c, maximum * sizeof(*c->tokens));
  unsigned line = 1;
  for (size_t k = 0; k < length;) {
    unsigned char ch = (unsigned char)source[k];
    if (ch == '\n') {
      line++;
      k++;
      continue;
    }
    if (isspace(ch)) {
      k++;
      continue;
    }
    if (ch == ';') {
      while (k < length && source[k] != '\n')
        k++;
      continue;
    }
    size_t start = k;
    if (ch == '"' ||
        ((ch == '@' || ch == '%') && k + 1 < length && source[k + 1] == '"')) {
      if (ch != '"')
        k++;
      k++;
      while (k < length && source[k] != '"') {
        if (source[k] == '\\') {
          if (k + 2 >= length)
            cg_error(c, "truncated IR escape");
          k += 3;
        } else
          k++;
      }
      if (k == length)
        cg_error(c, "unterminated IR string");
      k++;
    } else if (strchr("()[]{},=<>:*", ch)) {
      k++;
    } else {
      while (k < length && !isspace((unsigned char)source[k]) &&
             !strchr("()[]{},=<>:*;\"", source[k]))
        k++;
    }
    if (k == start)
      cg_error(c, "invalid IR character");
    if (c->count == 1000000)
      cg_error(c, "IR token limit exceeded");
    c->tokens[c->count++] =
        (cg_token){cg_copy(c, source + start, k - start), line};
  }
}
char *cg_name(cg_context *c, const char *s) {
  if (*s == '@' || *s == '%')
    s++;
  if (*s == '"') {
    size_t n = strlen(s);
    if (n < 2)
      cg_error(c, "invalid name");
    s = cg_copy(c, s + 1, n - 2);
  }
  if (!*s)
    cg_error(c, "empty symbol name");
  for (const char *p = s; *p; p++)
    if (!isalnum((unsigned char)*p) && !strchr("._$", *p))
      cg_error(c, "unsupported symbol spelling");
  return (char *)s;
}
static char *cg_section(cg_context *c) {
  char *s = cg_take(c);
  if (*s != '"')
    cg_error(c, "section requires a quoted name");
  return cg_name(c, s);
}
static void cg_abi_attribute(cg_context *c, const char *s) {
  if (!strcmp(s, "inreg") || !strcmp(s, "inalloca") ||
      !strcmp(s, "preallocated") || !strcmp(s, "byref") ||
      !strcmp(s, "swiftself") || !strcmp(s, "swifterror") || !strcmp(s, "nest"))
    cg_error(c, "unsupported ABI parameter attribute");
}
static int cg_return_hint(cg_context *c, const char *s) {
  if (!strcmp(s, "align")) {
    uint64_t align = cg_number(c, cg_take(c));
    if (!align || (align & (align - 1)))
      cg_error(c, "invalid return alignment");
    return 1;
  }
  return !strcmp(s, "noundef") || !strcmp(s, "zeroext") ||
         !strcmp(s, "signext") || !strcmp(s, "range") ||
         !strcmp(s, "nonnull") || !strcmp(s, "noalias") ||
         !strcmp(s, "dereferenceable") || !strcmp(s, "dereferenceable_or_null");
}
static cg_type *cg_type_parse(cg_context *c, unsigned depth) {
  if (depth > 64)
    cg_error(c, "IR type nesting limit");
  char *s = cg_take(c);
  cg_type *t = cg_alloc(c, sizeof(*t));
  if (!strcmp(s, "void")) {
    t->align = 1;
    return t;
  }
  if (!strcmp(s, "ptr")) {
    t->size = t->align = 4;
    t->bits = 32;
    return t;
  }
  if (s[0] == 'i' && isdigit((unsigned char)s[1])) {
    uint64_t bits = cg_number(c, s + 1);
    if (bits > 64)
      cg_error(c, "unsupported integer width");
    t->bits = (unsigned)bits;
    if (t->bits != 1 && t->bits != 8 && t->bits != 16 && t->bits != 32 &&
        t->bits != 64)
      cg_error(c, "unsupported integer width");
    t->size = (t->bits + 7) / 8;
    t->align = t->size;
    return t;
  }
  if (!strcmp(s, "[")) {
    uint64_t n = cg_number(c, cg_take(c));
    cg_expect(c, "x");
    t->base = cg_type_parse(c, depth + 1);
    cg_expect(c, "]");
    if (!t->base->size || n > 16u * 1024u * 1024u / t->base->size)
      cg_error(c, "array type exceeds backend bound");
    t->count = (unsigned)n;
    t->size = t->count * t->base->size;
    t->align = t->base->align;
    return t;
  }
  if (!strcmp(s, "<") || !strcmp(s, "{")) {
    t->packed = !strcmp(s, "<");
    if (t->packed)
      cg_expect(c, "{");
    t->fields = cg_alloc(c, 256 * sizeof(*t->fields));
    t->align = 1;
    while (!cg_accept(c, "}")) {
      if (t->count == 256)
        cg_error(c, "too many aggregate fields");
      cg_type *f = cg_type_parse(c, depth + 1);
      t->fields[t->count++] = f;
      unsigned a = t->packed ? 1 : f->align;
      unsigned next = cg_round(t->size, a);
      if (next > 16u * 1024u * 1024u || f->size > 16u * 1024u * 1024u - next)
        cg_error(c, "aggregate type exceeds backend bound");
      t->size = next + f->size;
      if (a > t->align)
        t->align = a;
      if (cg_accept(c, ","))
        continue;
      cg_expect(c, "}");
      break;
    }
    if (t->packed)
      cg_expect(c, ">");
    t->size = cg_round(t->size, t->align);
    return t;
  }
  cg_error(c,
           "unsupported IR type (integer/opaque-pointer/array/record subset)");
}
static int cg_type_start(cg_context *c) {
  const char *s = c->pos < c->count ? c->tokens[c->pos].text : "";
  return !strcmp(s, "ptr") || !strcmp(s, "void") || !strcmp(s, "[") ||
         !strcmp(s, "{") || !strcmp(s, "<") ||
         (s[0] == 'i' && isdigit((unsigned char)s[1]));
}
static void cg_to_type(cg_context *c) {
  while (!cg_type_start(c)) {
    cg_take(c);
    if (cg_accept(c, "(")) {
      unsigned depth = 1;
      while (depth) {
        char *s = cg_take(c);
        if (!strcmp(s, "("))
          depth++;
        if (!strcmp(s, ")"))
          depth--;
      }
    }
  }
}
static void cg_attributes(cg_context *c) {
  while (c->pos < c->count) {
    const char *s = c->tokens[c->pos].text;
    if (*s == '%' || *s == '@' || *s == '-' || isdigit((unsigned char)*s) ||
        strchr("[{<", *s) || !strcmp(s, "null") || !strcmp(s, "true") ||
        !strcmp(s, "false") || !strcmp(s, "poison") || !strcmp(s, "undef") ||
        !strcmp(s, "inttoptr") || !strcmp(s, "ptrtoint") ||
        !strcmp(s, "getelementptr") || !strcmp(s, "add") || !strcmp(s, "sub") ||
        !strcmp(s, "zeroinitializer"))
      return;
    if (!strcmp(s, ",") || !strcmp(s, ")"))
      return;
    cg_take(c);
    if (cg_accept(c, "(")) {
      unsigned d = 1;
      while (d) {
        char *x = cg_take(c);
        if (!strcmp(x, "("))
          d++;
        if (!strcmp(x, ")"))
          d--;
      }
    } else if (!strcmp(s, "align"))
      cg_take(c);
  }
}
static void cg_constant(cg_context *c, cg_type *t, unsigned r, unsigned word) {
  char *s = cg_take(c);
  if (!strcmp(s, "[") && t && t->base) {
    unsigned index = word * 4 / t->base->size,
             subword = (word * 4 % t->base->size) / 4;
    if (t->base->size < 4 || index >= t->count)
      cg_error(c, "unsupported aggregate constant component");
    for (unsigned k = 0; k < t->count; k++) {
      if (k)
        cg_expect(c, ",");
      cg_type *part = cg_type_parse(c, 0);
      if (k == index)
        cg_value_read(c, part, r, subword);
      else {
        unsigned depth = 0;
        while (c->pos < c->count) {
          char *x = c->tokens[c->pos].text;
          if (!depth && (!strcmp(x, ",") || !strcmp(x, "]")))
            break;
          cg_take(c);
          if (!strcmp(x, "(") || !strcmp(x, "["))
            depth++;
          if (!strcmp(x, ")") || !strcmp(x, "]"))
            depth--;
        }
      }
    }
    cg_expect(c, "]");
    return;
  }
  if (!strcmp(s, "add") || !strcmp(s, "sub")) {
    if (word || (t && t->size > 4))
      cg_error(c, "wide constant arithmetic unsupported");
    cg_expect(c, "(");
    cg_type *a = cg_type_parse(c, 0);
    cg_value_read(c, a, r, 0);
    cg_expect(c, ",");
    (void)cg_type_parse(c, 0);
    uint32_t n = (uint32_t)cg_number(c, cg_take(c));
    cg_expect(c, ")");
    cg_immediate(c, 6, n);
    cg_emit(c, "%s.w r%u, r%u, r6\n", s, r, r);
    return;
  }
  if (*s == '%') {
    cg_value *v = cg_find(c, s);
    if (word * 4 >= cg_round(v->type->size, 4))
      cg_error(c, "SSA word outside value");
    if (!cg_register_read(c, v, r, word)) {
      if (v->slot == UINT32_MAX)
        cg_error(c, "read requires an omitted scalar home");
      cg_stack(c, 0, r, v->slot + word * 4);
      cg_register_loaded(c, v, r, word);
    }
    return;
  }
  if (*s == '@') {
    if (word)
      cg_immediate(c, r, 0);
    else
      cg_symbol(c, r, cg_name(c, s), 0);
    return;
  }
  if (!strcmp(s, "null") || !strcmp(s, "false") || !strcmp(s, "undef") ||
      !strcmp(s, "poison") || !strcmp(s, "zeroinitializer")) {
    cg_immediate(c, r, 0);
    return;
  }
  if (!strcmp(s, "true")) {
    cg_immediate(c, r, word ? 0 : 1);
    return;
  }
  if (!strcmp(s, "inttoptr") || !strcmp(s, "ptrtoint")) {
    cg_expect(c, "(");
    cg_type *from = cg_type_parse(c, 0);
    cg_value_read(c, from, r, word);
    cg_expect(c, "to");
    (void)cg_type_parse(c, 0);
    cg_expect(c, ")");
    return;
  }
  if (!strcmp(s, "getelementptr")) {
    cg_accept(c, "inbounds");
    cg_accept(c, "nuw");
    cg_accept(c, "nusw");
    cg_expect(c, "(");
    cg_type *base = cg_type_parse(c, 0);
    cg_expect(c, ",");
    (void)cg_type_parse(c, 0);
    char *symbol = cg_take(c);
    if (*symbol != '@')
      cg_error(c, "unsupported constant GEP base");
    int64_t offset = 0;
    unsigned index = 0;
    while (cg_accept(c, ",")) {
      (void)cg_type_parse(c, 0);
      int64_t n = (int64_t)cg_number(c, cg_take(c));
      if (index++ == 0)
        offset = (int64_t)((uint64_t)offset + (uint64_t)n * base->size);
      else if (base->base) {
        offset = (int64_t)((uint64_t)offset + (uint64_t)n * base->base->size);
        base = base->base;
      } else
        cg_error(c, "unsupported aggregate constant GEP");
    }
    cg_expect(c, ")");
    if (word)
      cg_immediate(c, r, 0);
    else
      cg_symbol(c, r, cg_name(c, symbol), offset);
    return;
  }
  uint64_t n = cg_number(c, s);
  if (t && t->bits && t->bits < 64)
    n &= (UINT64_C(1) << t->bits) - 1;
  cg_immediate(c, r, (uint32_t)(n >> (word * 32)));
}
void cg_value_read(cg_context *c, cg_type *t, unsigned r, unsigned word) {
  cg_attributes(c);
  cg_constant(c, t, r, word);
}
static void cg_skip_line(cg_context *c, unsigned line) {
  while (c->pos < c->count && c->tokens[c->pos].line == line)
    cg_take(c);
}
static unsigned cg_field_offset(cg_context *c, cg_type **type, unsigned index) {
  cg_type *t = *type;
  unsigned offset = 0;
  if (t->base) {
    if (index >= t->count)
      cg_error(c, "aggregate index outside type");
    *type = t->base;
    return index * t->base->size;
  }
  if (!t->fields || index >= t->count)
    cg_error(c, "invalid aggregate index");
  for (unsigned k = 0; k <= index; k++) {
    cg_type *f = t->fields[k];
    offset = cg_round(offset, t->packed ? 1 : f->align);
    if (k == index) {
      *type = f;
      return offset;
    }
    offset += f->size;
  }
  cg_error(c, "invalid field offset");
}
static unsigned cg_aggregate_index(cg_context *c) {
  uint64_t index = cg_number(c, cg_take(c));
  if (index > UINT32_MAX)
    cg_error(c, "aggregate index out of range");
  if (cg_is(c, ",") && c->pos + 1 < c->count &&
      isdigit((unsigned char)c->tokens[c->pos + 1].text[0]))
    cg_error(c, "nested aggregate SSA indices unsupported");
  return (unsigned)index;
}
static void cg_global_constant(cg_context *c, cg_type *t) {
  if (cg_accept(c, "zeroinitializer") || cg_accept(c, "undef") ||
      cg_accept(c, "poison")) {
    cg_emit(c, ".zero %u\n", t->size);
    return;
  }
  if (t->base && cg_accept(c, "c")) {
    char *s = cg_take(c);
    if (*s != '"')
      cg_error(c, "expected byte string");
    unsigned n = 0;
    for (size_t k = 1; s[k] && s[k] != '"'; k++) {
      unsigned b = (unsigned char)s[k];
      if (b == '\\') {
        char hex[3] = {s[k + 1], s[k + 2], 0};
        char *end;
        b = (unsigned)strtoul(hex, &end, 16);
        if (*end)
          cg_error(c, "invalid IR byte escape");
        k += 2;
      }
      cg_emit(c, ".byte %u\n", b);
      n++;
    }
    if (n != t->size)
      cg_error(c, "byte string size mismatch");
    return;
  }
  if (t->base || t->fields) {
    if (t->packed)
      cg_expect(c, "<");
    cg_expect(c, t->base ? "[" : "{");
    unsigned at = 0;
    for (unsigned k = 0; k < t->count; k++) {
      if (k)
        cg_expect(c, ",");
      cg_type *f = cg_type_parse(c, 0);
      unsigned align = t->packed ? 1 : f->align;
      unsigned next = cg_round(at, align);
      if (next > at)
        cg_emit(c, ".zero %u\n", next - at);
      cg_global_constant(c, f);
      at = next + f->size;
    }
    cg_expect(c, t->base ? "]" : "}");
    if (t->packed)
      cg_expect(c, ">");
    if (t->size > at)
      cg_emit(c, ".zero %u\n", t->size - at);
    return;
  }
  char *s = cg_take(c);
  if (*s == '@') {
    cg_emit(c, ".word %s\n", cg_name(c, s));
    return;
  }
  if (!strcmp(s, "inttoptr")) {
    cg_expect(c, "(");
    (void)cg_type_parse(c, 0);
    uint64_t n = cg_number(c, cg_take(c));
    cg_expect(c, "to");
    (void)cg_type_parse(c, 0);
    cg_expect(c, ")");
    cg_emit(c, ".word %" PRIu32 "\n", (uint32_t)n);
    return;
  }
  if (!strcmp(s, "getelementptr")) {
    cg_accept(c, "inbounds");
    cg_accept(c, "nuw");
    cg_accept(c, "nusw");
    cg_expect(c, "(");
    cg_type *base = cg_type_parse(c, 0);
    cg_expect(c, ",");
    (void)cg_type_parse(c, 0);
    char *symbol = cg_take(c);
    int64_t offset = 0;
    unsigned index = 0;
    while (cg_accept(c, ",")) {
      (void)cg_type_parse(c, 0);
      int64_t n = (int64_t)cg_number(c, cg_take(c));
      if (index++ == 0)
        offset = (int64_t)((uint64_t)offset + (uint64_t)n * base->size);
      else if (base->base) {
        offset = (int64_t)((uint64_t)offset + (uint64_t)n * base->base->size);
        base = base->base;
      } else
        cg_error(c, "unsupported global GEP");
    }
    cg_expect(c, ")");
    if (*symbol != '@')
      cg_error(c, "invalid global GEP symbol");
    cg_emit(c, ".word %s%+" PRId64 "\n", cg_name(c, symbol), offset);
    return;
  }
  uint64_t n = !strcmp(s, "null") || !strcmp(s, "false") ? 0
               : !strcmp(s, "true")                      ? 1
                                                         : cg_number(c, s);
  if (t->size == 8)
    cg_emit(c, ".word %" PRIu32 ", %" PRIu32 "\n", (uint32_t)n,
            (uint32_t)(n >> 32));
  else
    cg_emit(c, ".%s %" PRIu32 "\n",
            t->size == 1   ? "byte"
            : t->size == 2 ? "short"
                           : "word",
            (uint32_t)n & (t->size == 1   ? 255
                           : t->size == 2 ? 65535
                                          : UINT32_MAX));
}
static void cg_global(cg_context *c) {
  char *name = cg_name(c, cg_take(c));
  unsigned line = c->line;
  cg_expect(c, "=");
  if (!strcmp(name, "llvm.compiler.used")) {
    cg_skip_line(c, line);
    return;
  }
  if (!strncmp(name, "llvm.", 5))
    cg_error(c, "unsupported LLVM special global");
  int internal = 0, external = 0, constant = 0;
  while (!cg_is(c, "global") && !cg_is(c, "constant")) {
    char *s = cg_take(c);
    if (!strcmp(s, "internal") || !strcmp(s, "private"))
      internal = 1;
    else if (!strcmp(s, "external"))
      external = 1;
    else if (strcmp(s, "dso_local") && strcmp(s, "local_unnamed_addr") &&
             strcmp(s, "unnamed_addr"))
      cg_error(c, "unsupported global linkage/attribute");
  }
  constant = cg_accept(c, "constant");
  if (!constant)
    cg_expect(c, "global");
  cg_type *t = cg_type_parse(c, 0);
  unsigned start = c->pos, align = t->align;
  char *section = NULL;
  for (unsigned k = start; k < c->count && c->tokens[k].line == line; k++) {
    if (!strcmp(c->tokens[k].text, "align") && k + 1 < c->count)
      align = (unsigned)cg_number(c, c->tokens[k + 1].text);
    if (!strcmp(c->tokens[k].text, "section") && k + 1 < c->count) {
      unsigned saved = c->pos;
      c->pos = k + 1;
      section = cg_section(c);
      c->pos = saved;
    }
  }
  if (!align || (align & (align - 1)) || align > 65536)
    cg_error(c, "invalid global alignment");
  if (!external) {
    if (section)
      cg_emit(c, ".section \"%s\",\"%s\",%%progbits\n", section,
              constant ? "a" : "aw");
    else
      cg_emit(c, ".section .%s.%s,\"%s\",%%%s\n",
              constant                      ? "rodata"
              : cg_is(c, "zeroinitializer") ? "bss"
                                            : "data",
              name, constant ? "a" : "aw",
              !constant && cg_is(c, "zeroinitializer") ? "nobits" : "progbits");
    if (!internal)
      cg_emit(c, ".global %s\n", name);
    cg_emit(c, ".type %s, %%object\n.balign %u\n%s:\n", name, align, name);
    cg_global_constant(c, t);
    cg_emit(c, ".size %s, %u\n", name, t->size);
  }
  cg_skip_line(c, line);
}
static void cg_function_parse(cg_context *c) {
  cg_function *f = cg_alloc(c, sizeof(*f));
  c->fn = f;
  f->id = ++c->unique;
  f->alignment = 4;
  int fast = 0;
  while (!cg_type_start(c)) {
    char *s = cg_take(c);
    if (!strcmp(s, "internal") || !strcmp(s, "private"))
      f->internal = 1;
    else if (!strcmp(s, "signext"))
      f->return_ext = 1;
    else if (!strcmp(s, "fastcc"))
      fast = 1;
    else if (strcmp(s, "dso_local") && strcmp(s, "ccc") &&
             !cg_return_hint(c, s))
      cg_error(c, "unsupported function calling convention/linkage");
    if (cg_accept(c, "(")) {
      unsigned depth = 1;
      while (depth) {
        char *x = cg_take(c);
        if (!strcmp(x, "("))
          depth++;
        if (!strcmp(x, ")"))
          depth--;
      }
    }
  }
  f->ret = cg_type_parse(c, 0);
  if (fast && !f->internal)
    cg_error(c, "fastcc is only supported for module-internal functions");
  if (c->pos >= c->count || c->tokens[c->pos].text[0] != '@')
    cg_error(c, "function requires a global name");
  f->name = cg_name(c, cg_take(c));
  cg_expect(c, "(");
  f->args = cg_alloc(c, 256 * sizeof(*f->args));
  while (!cg_accept(c, ")")) {
    if (f->argc == 256)
      cg_error(c, "too many parameters");
    if (cg_accept(c, "..."))
      cg_error(c, "variadic function definitions are unsupported");
    cg_type *t = cg_type_parse(c, 0);
    unsigned byval = 0, parameter_align = t->align;
    while (c->pos < c->count && !cg_is(c, ",") && !cg_is(c, ")") &&
           c->tokens[c->pos].text[0] != '%') {
      char *s = cg_take(c);
      cg_abi_attribute(c, s);
      if (cg_accept(c, "(")) {
        if (!strcmp(s, "byval")) {
          cg_type *b = cg_type_parse(c, 0);
          byval = b->size;
          parameter_align = b->align >= 8 ? 8 : 4;
          cg_expect(c, ")");
        } else {
          unsigned d = 1;
          while (d) {
            char *x = cg_take(c);
            if (!strcmp(x, "("))
              d++;
            if (!strcmp(x, ")"))
              d--;
          }
        }
      } else if (!strcmp(s, "align"))
        parameter_align = (unsigned)cg_number(c, cg_take(c));
    }
    char *name = cg_take(c);
    if (*name != '%')
      cg_error(c, "function parameter requires an SSA name");
    f->args[f->argc] = cg_define(c, name, t);
    f->byval[f->argc] = byval;
    if (!parameter_align || (parameter_align & (parameter_align - 1)))
      cg_error(c, "invalid parameter alignment");
    if (byval && parameter_align > 8)
      cg_error(c, "over-aligned byval argument unsupported");
    f->arg_align[f->argc++] = byval ? parameter_align : t->align;
    if (cg_accept(c, ","))
      continue;
    cg_expect(c, ")");
    break;
  }
  while (!cg_accept(c, "{")) {
    if (cg_accept(c, "internal"))
      f->internal = 1;
    else if (cg_accept(c, "section"))
      f->section = cg_section(c);
    else if (cg_accept(c, "align")) {
      uint64_t align = cg_number(c, cg_take(c));
      if (!align || (align & (align - 1)) || align > 65536)
        cg_error(c, "invalid function alignment");
      f->alignment = (unsigned)align;
    } else
      cg_take(c);
  }
  while (!cg_accept(c, "}")) {
    char *s = cg_take(c);
    unsigned line = c->line;
    if (cg_accept(c, ":")) {
      if (*s == '%' || *s == '@')
        cg_error(c, "invalid basic block name");
      cg_block *b = cg_alloc(c, sizeof(*b));
      b->name = cg_name(c, s);
      for (cg_block *existing = f->blocks; existing; existing = existing->next)
        if (!strcmp(existing->name, b->name))
          cg_error(c, "duplicate basic block");
      if (f->last)
        f->last->next = b;
      else
        f->blocks = b;
      f->last = b;
      continue;
    }
    if (!f->last)
      cg_error(c, "function requires named basic blocks");
    cg_inst *i = cg_alloc(c, sizeof(*i));
    i->begin = c->pos - 1;
    if (f->last->last)
      f->last->last->next = i;
    else
      f->last->first = i;
    f->last->last = i;
    if (*s == '%' && cg_accept(c, "=")) {
      char *op = cg_take(c);
      cg_type *t = NULL;
      if (!strcmp(op, "icmp") || !strcmp(op, "fcmp")) {
        t = cg_alloc(c, sizeof(*t));
        t->bits = t->size = t->align = 1;
      } else if (!strcmp(op, "alloca") || !strcmp(op, "getelementptr")) {
        t = cg_alloc(c, sizeof(*t));
        t->bits = 32;
        t->size = t->align = 4;
      } else if (!strcmp(op, "trunc") || !strcmp(op, "zext") ||
                 !strcmp(op, "sext") || !strcmp(op, "inttoptr") ||
                 !strcmp(op, "ptrtoint")) {
        while (!cg_accept(c, "to"))
          cg_take(c);
        t = cg_type_parse(c, 0);
      } else if (!strcmp(op, "extractvalue")) {
        cg_to_type(c);
        t = cg_type_parse(c, 0);
        cg_take(c);
        cg_expect(c, ",");
        unsigned index = cg_aggregate_index(c);
        (void)cg_field_offset(c, &t, index);
      } else if (!strcmp(op, "select")) {
        cg_to_type(c);
        (void)cg_type_parse(c, 0);
        cg_take(c);
        cg_expect(c, ",");
        t = cg_type_parse(c, 0);
      } else {
        cg_to_type(c);
        t = cg_type_parse(c, 0);
      }
      i->result = cg_define(c, s, t);
      if (!strcmp(op, "alloca")) {
        unsigned saved = c->pos;
        c->pos = i->begin + 3;
        cg_type *object = cg_type_parse(c, 0);
        unsigned align = object->align;
        if (cg_accept(c, ",") && !cg_is(c, "align")) {
          (void)cg_type_parse(c, 0);
          if (cg_number(c, cg_take(c)) != 1)
            cg_error(c, "variable/count alloca unsupported");
        }
        for (unsigned k = c->pos; k < c->count && c->tokens[k].line == line;
             k++)
          if (!strcmp(c->tokens[k].text, "align") && k + 1 < c->count)
            align = (unsigned)cg_number(c, c->tokens[k + 1].text);
        i->result->object = cg_slot(c, object->size, align);
        i->result->object_size = object->size;
        i->result->object_align = align;
        if (c->memory_selection == BADGE_MEMORY_WORD) {
          i->result->memory_align = align;
          i->result->memory_size = object->size;
        }
        c->pos = saved;
      }
    }

    while (c->pos < c->count && c->tokens[c->pos].line == line)
      cg_take(c);
    if (!strcmp(s, "switch")) {
      while (!cg_accept(c, "]"))
        cg_take(c);
    } else if (i->result && !strcmp(c->tokens[i->begin + 2].text, "switch"))
      cg_error(c, "invalid switch result");
    i->end = c->pos;
  }
  if (c->last)
    c->last->next = f;
  else
    c->functions = f;
  c->last = f;
  c->fn = NULL;
}

typedef struct {
  unsigned destination, source, scratch;
  unsigned constant;
  cg_type *type;
} cg_phi_register_copy;

int cg_phi_constant(const char *token) {
  if (!strcmp(token, "null") || !strcmp(token, "true") || !strcmp(token, "false"))
    return 1;
  if (*token == '-') token++;
  if (!*token) return 0;
  for (; *token; token++)
    if (*token < '0' || *token > '9') return 0;
  return 1;
}
unsigned cg_phi_buffer_size(cg_context *c, cg_inst *i) {
  unsigned bytes = cg_round(i->result->type->size, 4);
  if (!cg_home_selection(c) || !cg_global_register(c, i->result->name))
    return bytes;
  /* Only one-token register/constant sources can be deferred without capturing
     an expression's dependencies before parallel copies. */
  unsigned begin = i->begin + 4;
  if (begin >= i->end) return bytes;
  for (unsigned k = begin; k < i->end; k += 6) {
    cg_register_work(c);
    if (k + 4 >= i->end || strcmp(c->tokens[k].text, "[") ||
        strcmp(c->tokens[k + 2].text, ",") ||
        strcmp(c->tokens[k + 4].text, "]"))
      return bytes;
    const char *source = c->tokens[k + 1].text;
    if (!cg_phi_constant(source) &&
        !(*source == '%' && cg_global_register(c, source)))
      return bytes;
    if (k + 5 < i->end && strcmp(c->tokens[k + 5].text, ","))
      return bytes;
  }
  return 0;
}
static void cg_phi_register_copies(cg_context *c, cg_phi_register_copy *copies,
                                   unsigned count) {
  unsigned pending = count;
  while (pending) {
    int emitted = 0;
    for (unsigned k = 0; k < count; k++) {
      cg_phi_register_copy *copy = &copies[k];
      if (!copy->destination)
        continue;
      int blocked = 0;
      for (unsigned n = 0; n < count; n++)
        if (n != k && copies[n].destination &&
            copies[n].source == copy->destination)
          blocked = 1;
      if (copy->source != copy->destination && blocked)
        continue;
      if (copy->type) {
        c->pos = copy->constant;
        cg_value_read(c, copy->type, copy->destination, 0);
        cg_normalize(c, copy->type, copy->destination);
      } else if (!copy->source)
        cg_stack(c, 0, copy->destination, copy->scratch);
      else if (copy->source != copy->destination)
        cg_emit(c, "mov r%u, r%u\n", copy->destination, copy->source);
      copy->destination = 0;
      pending--;
      emitted = 1;
    }
    if (!emitted) {
      /* Edge-copy scratch must not overlap the optional scalar register pool. */
      unsigned saved = 0;
      for (unsigned k = 0; k < count; k++)
        if (copies[k].destination) {
          saved = copies[k].destination;
          break;
        }
      unsigned scratch = c->allocate_r6 ? 12 : 6;
      cg_emit(c, "mov r%u, r%u\n", scratch, saved);
      for (unsigned k = 0; k < count; k++)
        if (copies[k].destination && copies[k].source == saved)
          copies[k].source = scratch;
    }
  }
}
static int cg_next_block(cg_context *c, cg_block *from, const char *target) {
  return c->layout_active && from->next &&
         !strcmp(from->next->name, cg_name(c, target));
}
static void cg_edge(cg_context *c, cg_block *from, const char *target,
                    int fallthrough) {
  target = cg_name(c, target);
  if (cg_control_selection(c)) {
    unsigned limit = 0;
    for (cg_block *p = c->fn->blocks; p; p = p->next) limit++;
    for (unsigned n = 0; n < limit; n++) {
      cg_register_work(c);
      cg_block *p = c->fn->blocks;
      while (p && strcmp(p->name, target)) p = p->next;
      if (!p || !p->first || p->first != p->last || p->first->result ||
          p->first->end - p->first->begin != 3 ||
          strcmp(c->tokens[p->first->begin].text, "br") ||
          strcmp(c->tokens[p->first->begin + 1].text, "label")) break;
      const char *next = c->tokens[p->first->begin + 2].text;
      if (cg_target_has_phi(c, next) || !strcmp(target, cg_name(c, next))) break;
      target = cg_name(c, next);
    }
  }
  cg_block *b = c->fn->blocks;
  while (b && strcmp(b->name, target))
    b = b->next;
  if (!b)
    cg_error(c, "branch target does not exist");
  unsigned scratch = c->phi_space;
  cg_phi_register_copy copies[CG_GLOBAL_REGISTER_COUNT] = {{0}};
  unsigned copy_count = 0;
  for (cg_inst *i = b->first; i; i = i->next) {
    if (!i->result || strcmp(c->tokens[i->begin + 2].text, "phi"))
      continue;
    c->pos = i->begin + 3;
    cg_type *t = cg_type_parse(c, 0);
    int found = 0;
    while (c->pos < i->end) {
      cg_expect(c, "[");
      unsigned value = c->pos;
      unsigned depth = 0;
      while (depth || !cg_is(c, ",")) {
        char *s = cg_take(c);
        if (!strcmp(s, "(") || !strcmp(s, "["))
          depth++;
        if (!strcmp(s, ")") || !strcmp(s, "]"))
          depth--;
      }
      unsigned operand_end = c->pos;
      cg_take(c);
      char *label = cg_name(c, cg_take(c));
      cg_expect(c, "]");
      unsigned next = c->pos;
      if (!strcmp(label, from->name)) {
        unsigned destination = cg_global_register(c, i->result->name);
        unsigned source = destination && operand_end == value + 1
                              ? cg_global_register(c, c->tokens[value].text)
                              : 0;
        int constant = cg_home_selection(c) && destination &&
            operand_end == value + 1 && cg_phi_constant(c->tokens[value].text);
        if (destination) {
          if (copy_count == CG_GLOBAL_REGISTER_COUNT)
            cg_error(c, "too many allocated PHIs on an edge");
          copies[copy_count++] =
              (cg_phi_register_copy){.destination = destination, .source = source,
                .scratch = scratch, .constant = value, .type = constant ? t : NULL};
          c->avoided_stores++;
          if (source) {
            c->phi_register_copies++;
            c->avoided_loads++;
          }
        }
        if (!source && !constant) {
          for (unsigned w = 0; w < cg_round(t->size, 4) / 4; w++) {
            c->pos = value;
            cg_value_read(c, t, 0, w);
            if (destination && !w && t->bits < 32)
              cg_normalize(c, t, 0);
            cg_stack(c, 1, 0, scratch + w * 4);
          }
        }
        found = 1;
      }
      c->pos = next;
      if (!cg_accept(c, ","))
        break;
    }
    if (!found)
      cg_error(c, "phi lacks incoming predecessor");
    scratch += cg_phi_buffer_size(c, i);
  }
  scratch = c->phi_space;
  for (cg_inst *i = b->first; i; i = i->next) {
    if (!i->result || strcmp(c->tokens[i->begin + 2].text, "phi"))
      continue;
    if (cg_global_register(c, i->result->name)) {
      scratch += cg_phi_buffer_size(c, i);
      continue;
    }
    for (unsigned w = 0; w < cg_round(i->result->type->size, 4) / 4; w++) {
      cg_stack(c, 0, 0, scratch + w * 4);
      cg_save(c, i->result, 0, w);
    }
    scratch += cg_phi_buffer_size(c, i);
  }
  cg_phi_register_copies(c, copies, copy_count);
  if (fallthrough && cg_next_block(c, from, target))
    c->layout_fallthroughs++;
  else
    cg_emit(c, "b%s .Lcg_%u_%s\n", cg_branch_width(c), c->fn->id, target);
}
typedef struct {
  cg_type *type;
  unsigned start, byval, align;
  int sign;
} cg_arg;
static unsigned cg_arguments(cg_context *c, cg_arg *args) {
  unsigned count = 0;
  cg_expect(c, "(");
  while (!cg_accept(c, ")")) {
    if (count == 256)
      cg_error(c, "too many call arguments");
    cg_arg *a = &args[count++];
    a->type = cg_type_parse(c, 0);
    a->align = a->type->align;
    while (c->pos < c->count) {
      char *s = c->tokens[c->pos].text;
      cg_abi_attribute(c, s);
      if (!strcmp(s, "byval")) {
        cg_take(c);
        cg_expect(c, "(");
        cg_type *b = cg_type_parse(c, 0);
        a->byval = b->size;
        a->align = b->align < 4 ? 4 : b->align;
        cg_expect(c, ")");
      } else if (!strcmp(s, "signext")) {
        a->sign = 1;
        cg_take(c);
      } else if (!strcmp(s, "align")) {
        cg_take(c);
        a->align = (unsigned)cg_number(c, cg_take(c));
      } else {
        if (*s == '%' || *s == '@' || *s == '-' || isdigit((unsigned char)*s) ||
            strchr("[{<", *s) || !strcmp(s, "null") || !strcmp(s, "true") ||
            !strcmp(s, "false") || !strcmp(s, "poison") ||
            !strcmp(s, "undef") || !strcmp(s, "inttoptr") ||
            !strcmp(s, "ptrtoint") || !strcmp(s, "getelementptr") ||
            !strcmp(s, "sub") || !strcmp(s, "add") ||
            !strcmp(s, "zeroinitializer"))
          break;
        cg_take(c);
        if (cg_accept(c, "(")) {
          unsigned d = 1;
          while (d) {
            char *x = cg_take(c);
            if (!strcmp(x, "("))
              d++;
            if (!strcmp(x, ")"))
              d--;
          }
        }
      }
    }
    if (!a->align || (a->align & (a->align - 1)))
      cg_error(c, "invalid call argument alignment");
    if (a->byval && a->align > 8)
      cg_error(c, "over-aligned byval call argument unsupported");
    if (!a->byval)
      a->align = a->type->align;
    cg_attributes(c);
    a->start = c->pos;
    unsigned depth = 0;
    while (c->pos < c->count) {
      char *s = c->tokens[c->pos].text;
      if (!depth && (!strcmp(s, ",") || !strcmp(s, ")")))
        break;
      cg_take(c);
      if (!strcmp(s, "(") || !strcmp(s, "[") || !strcmp(s, "{") ||
          !strcmp(s, "<"))
        depth++;
      if (!strcmp(s, ")") || !strcmp(s, "]") || !strcmp(s, "}") ||
          !strcmp(s, ">"))
        depth--;
    }
    if (cg_accept(c, ","))
      continue;
    cg_expect(c, ")");
    break;
  }
  return count;
}
static void cg_memory_offset(cg_value *proof, uint64_t offset) {
  if (offset > proof->memory_size) {
    proof->memory_align = proof->memory_size = 0;
    return;
  }
  proof->memory_size -= (unsigned)offset;
  while (proof->memory_align && (offset & (proof->memory_align - 1)))
    proof->memory_align /= 2;
}
static int cg_memory_pointer(cg_context *c, cg_arg *arg, unsigned length) {
  const char *s = c->tokens[arg->start].text;
  if (*s != '%')
    return 0;
  cg_value *v = cg_find(c, s);
  return v->memory_align >= 4 && length <= v->memory_size;
}
static int cg_word_memory(cg_context *c, cg_arg *args, int copy,
                          unsigned length) {
  if (c->memory_selection != BADGE_MEMORY_WORD || length < 4 ||
      (strcmp(c->tokens[args[3].start].text, "false") &&
       strcmp(c->tokens[args[3].start].text, "0")) ||
      !cg_memory_pointer(c, &args[0], length) ||
      (copy && !cg_memory_pointer(c, &args[1], length)))
    return 0;
  unsigned words = length / 4, tail = length % 4, id = ++c->unique;
  if (!copy)
    cg_emit(c, "uxtb r0, r5\norr.w r0, r0, r0, lsl #8\n"
               "orr.w r0, r0, r0, lsl #16\n");
  if (words > 1) {
    cg_immediate(c, 6, words);
    cg_emit(c, ".Lcg_wordmem%u:\n", id);
  }
  if (copy)
    cg_emit(c, "ldr.w r0, [r5], #4\n");
  cg_emit(c, "str.w r0, [r4], #4\n");
  if (words > 1)
    cg_emit(c, "subs r6, #1\nbne.w .Lcg_wordmem%u\n", id);
  for (unsigned k = 0; k < tail; k++) {
    if (copy)
      cg_emit(c, "ldrb.w r0, [r5], #1\n");
    cg_emit(c, "strb.w r0, [r4], #1\n");
  }
  if (copy)
    c->word_copies++;
  else
    c->word_fills++;
  c->word_bytes += words * 4;
  c->tail_bytes += tail;
  return 1;
}
static void cg_call(cg_context *c, cg_inst *i) {
  int fast = 0;
  while (!cg_type_start(c)) {
    char *s = cg_take(c);
    if (!strcmp(s, "fastcc"))
      fast = 1;
    else if (strcmp(s, "ccc") && !cg_return_hint(c, s))
      cg_error(c, "unsupported call convention/attribute");
    if (cg_accept(c, "(")) {
      unsigned d = 1;
      while (d) {
        char *x = cg_take(c);
        if (!strcmp(x, "("))
          d++;
        if (!strcmp(x, ")"))
          d--;
      }
    }
  }
  cg_to_type(c);
  cg_type *ret = cg_type_parse(c, 0);
  if (cg_accept(c, "(")) {
    unsigned d = 1;
    while (d) {
      char *s = cg_take(c);
      if (!strcmp(s, "("))
        d++;
      if (!strcmp(s, ")"))
        d--;
    }
  }
  char *callee = cg_take(c);
  if (fast) {
    cg_function *f = c->functions;
    if (*callee != '@')
      cg_error(c, "indirect fastcc call unsupported");
    char *name = cg_name(c, callee);
    while (f && strcmp(f->name, name))
      f = f->next;
    if (!f || !f->internal)
      cg_error(c, "fastcc call requires a module-internal definition");
  }
  if (!strcmp(callee, "asm")) {
    cg_assembly *effects = cg_instruction_assembly(c, i);
    if (ret->size) cg_error(c, "unsupported inline assembly return type");
    cg_accept(c, "sideeffect");
    cg_take(c);
    cg_expect(c, ",");
    cg_take(c);
    cg_expect(c, "(");
    cg_expect(c, ")");
    if (cg_effect_selection(c))
      cg_emit(c, "@ assembly-effects: known %d reads %u writes %u flags %d/%d "
              "memory %d/%d barrier %d control %d\n",
              effects->known, effects->reads, effects->writes, effects->flags_read,
              effects->flags_write, effects->memory_read, effects->memory_write,
              effects->memory_barrier, effects->control);
    if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS) {
      unsigned lines = 1;
      for (const char *p = effects->text; *p; p++) {
        cg_register_work(c);
        if (*p == '\n') lines++;
      }
      cg_emit(c, "@ constant-assembly-lines: %u\n", lines);
    }
    cg_emit(c, "%s\n", effects->text);
    return;
  }
  cg_arg args[256] = {0};
  unsigned count = cg_arguments(c, args);
  unsigned after = c->pos;
  if (!strncmp(callee, "@llvm.", 6)) {
    if (!strncmp(callee, "@llvm.lifetime.", 15))
      return;
    if (!strncmp(callee, "@llvm.mem", 9)) {
      if (count != 4)
        cg_error(c, "invalid memory intrinsic");
      c->pos = args[0].start;
      cg_value_read(c, args[0].type, 4, 0);
      c->pos = args[1].start;
      cg_value_read(c, args[1].type, 5, 0);
      c->pos = args[2].start;
      uint64_t length = cg_number(c, cg_take(c));
      if (length > 16u * 1024u * 1024u)
        cg_error(c, "memory intrinsic length bound");
      int copy = !strncmp(callee, "@llvm.memcpy", 12);
      if (!copy && strncmp(callee, "@llvm.memset", 12))
        cg_error(c, "unsupported memory intrinsic");
      if (cg_word_memory(c, args, copy, (unsigned)length)) {
        c->pos = after;
        return;
      }
      unsigned id = ++c->unique;
      cg_immediate(c, 6, (uint32_t)length);
      cg_emit(c, "cmp r6, #0\nbeq.w .Lcg_memend%u\n.Lcg_mem%u:\n", id, id);
      if (copy)
        cg_emit(c, "ldrb.w r0, [r5], #1\n");
      else
        cg_emit(c, "mov r0, r5\n");
      cg_emit(c,
              "strb.w r0, [r4], #1\nsubs r6, #1\nbne.w "
              ".Lcg_mem%u\n.Lcg_memend%u:\n",
              id, id);
      c->pos = after;
      return;
    }
    if (count != 2 || !i->result)
      cg_error(c, "unsupported LLVM intrinsic");
    if (args[0].type->size > 4)
      cg_error(c, "wide min/max/abs intrinsic unsupported");
    c->pos = args[0].start;
    cg_value_read(c, args[0].type, 0, 0);
    if (!strncmp(callee, "@llvm.abs.", 10)) {
      cg_signed(c, args[0].type, 0);
      cg_emit(c, "cmp r0, #0\nit lt\nrsblt r0, r0, #0\n");
    } else {
      c->pos = args[1].start;
      cg_value_read(c, args[1].type, 1, 0);
      int sign = !strncmp(callee, "@llvm.smin.", 11) ||
                 !strncmp(callee, "@llvm.smax.", 11);
      int minimum = !strncmp(callee, "@llvm.smin.", 11) ||
                    !strncmp(callee, "@llvm.umin.", 11);
      if (!sign && strncmp(callee, "@llvm.umin.", 11) &&
          strncmp(callee, "@llvm.umax.", 11))
        cg_error(c, "unsupported LLVM intrinsic");
      if (sign) {
        cg_signed(c, args[0].type, 0);
        cg_signed(c, args[1].type, 1);
      }
      cg_emit(c, "cmp r0, r1\nit %s\nmov%s r0, r1\n",
              sign ? (minimum ? "gt" : "lt") : (minimum ? "hi" : "lo"),
              sign ? (minimum ? "gt" : "lt") : (minimum ? "hi" : "lo"));
    }
    cg_save(c, i->result, 0, 0);
    c->pos = after;
    return;
  }
  unsigned cursor = 0, positions[256];
  for (unsigned k = 0; k < count; k++) {
    if (args[k].align >= 8)
      cursor = cg_round(cursor, 2);
    positions[k] = cursor;
    cursor +=
        cg_round(args[k].byval ? args[k].byval : args[k].type->size, 4) / 4;
    if (cursor > 16388)
      cg_error(c, "outgoing arguments exceed 64 KiB stack bound");
  }
  unsigned outgoing = cursor > 4 ? cg_round((cursor - 4) * 4, 8) : 0;
  if (outgoing) {
    cg_immediate(c, 12, outgoing);
    cg_emit(c, "sub.w sp, sp, r12\n");
  }
  for (unsigned k = 0; k < count; k++) {
    unsigned words =
        cg_round(args[k].byval ? args[k].byval : args[k].type->size, 4) / 4;
    for (unsigned w = 0; w < words; w++) {
      unsigned position = positions[k] + w, r = position < 4 ? position : 5;
      unsigned direct = cg_operand_selection(c) && cg_call_selection(c) &&
                        position >= 4 && (position - 4) * 4 <= 4095 && !w &&
                        !args[k].byval && !args[k].sign && args[k].type->size <= 4
                            ? cg_global_register(c, c->tokens[args[k].start].text) : 0;
      if (direct) r = direct;
      c->pos = args[k].start;
      if (args[k].byval) {
        cg_value_read(c, args[k].type, 4, 0);
        unsigned remain = args[k].byval - w * 4;
        if (remain >= 4)
          cg_emit(c, "ldr.w r%u, [r4, #%u]\n", r, w * 4);
        else {
          cg_immediate(c, r, 0);
          for (unsigned b = 0; b < remain; b++)
            cg_emit(c, "ldrb.w r6, [r4, #%u]\norr.w r%u, r%u, r6, lsl #%u\n",
                    w * 4 + b, r, r, b * 8);
        }
      } else if (!direct) {
        cg_value_read(c, args[k].type, r, w);
        if (args[k].sign && !w)
          cg_signed(c, args[k].type, r);
      }
      if (position >= 4) {
        unsigned offset = (position - 4) * 4;
        if (cg_call_selection(c) && offset <= 4095) {
          cg_emit(c, "str.w r%u, [sp, #%u]\n", r, offset);
          c->call_stack_addresses++;
        } else {
          cg_immediate(c, 12, offset);
          cg_emit(c, "add.w r12, sp, r12\nstr.w r5, [r12]\n");
        }
      }
    }
  }
  if (*callee == '@') {
    cg_emit(c, "bl %s\n", cg_name(c, callee));
    if (c->scalar_stream_selection && i->scalar_call_arguments_plus_one == 2)
      cg_emit(c, "@ scalar-call: arguments 1\n");
  }
  else if (*callee == '%') {
    c->pos = after;
    cg_value *v = cg_find(c, callee);
    if (!cg_register_read(c, v, 12, 0)) {
      if (v->slot == UINT32_MAX)
        cg_error(c, "callee requires an omitted scalar home");
      cg_stack(c, 0, 12, v->slot);
    }
    cg_emit(c, "blx r12\n");
  } else
    cg_error(c, "invalid callee");
  if (outgoing) {
    cg_immediate(c, 12, outgoing);
    cg_emit(c, "add.w sp, sp, r12\n");
  }
  if (i->result)
    for (unsigned w = 0; w < cg_round(ret->size, 4) / 4; w++) {
      if (w >= 4)
        cg_error(c, "unsupported direct aggregate return");
      cg_save(c, i->result, w, w);
    }
  c->pos = after;
}
unsigned cg_instruction_scratch(cg_context *c, cg_inst *i) {
  unsigned at = i->begin + (i->result ? 2 : 0);
  if (cg_light_frames(c) && (!strcmp(c->tokens[at].text, "tail") || !strcmp(c->tokens[at].text, "notail")))
    at++;
  const char *op = c->tokens[at].text;
  if (!strcmp(op, "call")) {
    for (unsigned k = at + 1; k < i->end; k++) {
      cg_register_work(c);
      if (!strcmp(c->tokens[k].text, "asm"))
        return 0;
    }
    if (c->scalar_stream_selection && at + 4 < i->end &&
        (!strcmp(c->tokens[at + 1].text, "void") ||
         !strcmp(c->tokens[at + 1].text, "i32") ||
         !strcmp(c->tokens[at + 1].text, "ptr")) &&
        (c->tokens[at + 2].text[0] == '@' || c->tokens[at + 2].text[0] == '%') &&
        strncmp(cg_name(c, c->tokens[at + 2].text), "llvm.", 5) &&
        !strcmp(c->tokens[at + 3].text, "(")) {
      unsigned pos = at + 4, arguments = 0;
      while (pos + 1 < i->end && arguments < 4 &&
             (!strcmp(c->tokens[pos].text, "i32") || !strcmp(c->tokens[pos].text, "ptr")) &&
             (c->tokens[pos + 1].text[0] == '%' ||
              isdigit((unsigned char)c->tokens[pos + 1].text[0]) ||
              c->tokens[pos + 1].text[0] == '-')) {
        arguments++;
        pos += 2;
        if (!strcmp(c->tokens[pos].text, ")")) break;
        if (strcmp(c->tokens[pos].text, ",")) break;
        pos++;
      }
      if (pos + 1 == i->end && !strcmp(c->tokens[pos].text, ")")) {
        if (c->tokens[at + 2].text[0] == '@')
          i->scalar_call_arguments_plus_one = arguments + 1;
        return 1u << 3;
      }
    }
    return CG_SCRATCH_R4 | CG_SCRATCH_R5 | (cg_light_frames(c) ? 1u << 3 : 0);
  }
  if (!strcmp(op, "load") || !strcmp(op, "store") || !strcmp(op, "icmp") ||
      !strcmp(op, "add") || !strcmp(op, "sub") || !strcmp(op, "mul") ||
      !strcmp(op, "and") || !strcmp(op, "or") || !strcmp(op, "xor") ||
      !strcmp(op, "shl") || !strcmp(op, "lshr") || !strcmp(op, "ashr") ||
      !strcmp(op, "sdiv") || !strcmp(op, "udiv") ||
      !strcmp(op, "srem") || !strcmp(op, "urem")) {
    unsigned saved = c->pos, line = c->line;
    c->pos = at + 1;
    c->line = c->tokens[at].line;
    cg_to_type(c);
    cg_type *t = cg_type_parse(c, 0);
    unsigned mask = 0;
    if (!strcmp(op, "load") || !strcmp(op, "store")) {
      if (t->size > 8)
        cg_error(c, "aggregate load/store is outside backend scalar subset");
      unsigned align = t->align;
      for (unsigned k = c->pos; k + 1 < i->end; k++) {
        cg_register_work(c);
        if (!strcmp(c->tokens[k].text, "align")) {
          align = (unsigned)cg_number(c, c->tokens[k + 1].text);
          break;
        }
      }
      mask = cg_memory_scratch(t, align);
      if (c->scalar_stream_selection &&
          !strcmp(op, "load") && (t->size == 1 || t->size == 2 || t->size == 4) &&
          align >= t->size)
        mask = 0;
    } else if (t->size > 4)
      mask = CG_SCRATCH_R4 | CG_SCRATCH_R5;
    else if (!strcmp(op, "sdiv") || !strcmp(op, "udiv") ||
             !strcmp(op, "srem") || !strcmp(op, "urem"))
      mask = CG_SCRATCH_R4;
    c->pos = saved;
    c->line = line;
    return mask;
  }
  if (!strcmp(op, "select"))
    return CG_SCRATCH_R4;
  const char *safe[] = {"phi", "br", "switch", "ret", "alloca", "getelementptr",
                        "freeze", "trunc", "zext", "sext", "ptrtoint",
                        "inttoptr", "unreachable"};
  for (unsigned k = 0; k < sizeof(safe) / sizeof(*safe); k++)
    if (!strcmp(op, safe[k]))
      return 0;
  return CG_SCRATCH_R4 | CG_SCRATCH_R5;
}
const char *cg_comparison_condition(const char *pred) {
  return !strcmp(pred, "eq") ? "eq" : !strcmp(pred, "ne") ? "ne" :
         !strcmp(pred, "slt") ? "lt" : !strcmp(pred, "sle") ? "le" :
         !strcmp(pred, "sgt") ? "gt" : !strcmp(pred, "sge") ? "ge" :
         !strcmp(pred, "ult") ? "lo" : !strcmp(pred, "ule") ? "ls" :
         !strcmp(pred, "ugt") ? "hi" : !strcmp(pred, "uge") ? "hs" : NULL;
}
const char *cg_inverse_condition(const char *condition) {
  const char *pairs[][2] = {{"eq", "ne"}, {"lt", "ge"}, {"le", "gt"},
                            {"lo", "hs"}, {"ls", "hi"}};
  for (unsigned k = 0; k < sizeof(pairs) / sizeof(*pairs); k++) {
    if (!strcmp(condition, pairs[k][0])) return pairs[k][1];
    if (!strcmp(condition, pairs[k][1])) return pairs[k][0];
  }
  return NULL;
}
static void cg_conditional_edge(cg_context *c, cg_block *block, cg_inst *i,
                                char *yes, char *no, const char *condition) {
  unsigned id = ++c->unique;
  int invert = i == block->last && cg_next_block(c, block, yes) &&
               !cg_target_has_phi(c, no);
  /* Consume APSR before any PHI copies; keep Bcc local even for distant edges. */
  cg_emit(c, "b%s%s .Lcg_edge%u\n", invert ? condition : cg_inverse_condition(condition),
          cg_branch_width(c), id);
  cg_edge(c, block, invert ? no : yes, 0);
  cg_emit(c, ".Lcg_edge%u:\n", id);
  cg_edge(c, block, invert ? yes : no, i == block->last);
  c->layout_inversions += invert;
}

void cg_instruction(cg_context *c, cg_block *block, cg_inst *i) {
  c->pos = i->begin;
  c->line = c->tokens[i->begin].line;
  if (i->result) {
    cg_take(c);
    cg_expect(c, "=");
  }
  char *op = cg_take(c);
  if (!strcmp(op, "tail") || !strcmp(op, "notail"))
    op = cg_take(c);
  cg_register_instruction(c, i, cg_selection_barrier(c, i) ? "call" : op);
  if (cg_selection_instruction(c, i))
    return;
  if (!strcmp(op, "phi"))
    return;
  if (!strcmp(op, "call")) {
    cg_call(c, i);
    return;
  }
  if (!strcmp(op, "alloca")) {
    cg_address(c, 0, i->result->object);
    cg_save(c, i->result, 0, 0);
    return;
  }
  if (!strcmp(op, "unreachable")) {
    cg_emit(c, ".short 0xde00\n");
    return;
  }
  if (!strcmp(op, "br")) {
    if (cg_accept(c, "label")) {
      char *target = cg_take(c);
      cg_edge(c, block, target, i == block->last);
      return;
    }
    cg_type *t = cg_type_parse(c, 0);
    if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
        t->bits == 1 &&
        (cg_is(c, "true") || cg_is(c, "false") || cg_is(c, "0") || cg_is(c, "1"))) {
      char *value = cg_take(c);
      int taken = !strcmp(value, "true") || !strcmp(value, "1");
      cg_expect(c, ",");
      cg_expect(c, "label");
      char *yes = cg_take(c);
      cg_expect(c, ",");
      cg_expect(c, "label");
      char *no = cg_take(c);
      cg_edge(c, block, taken ? yes : no, i == block->last);
      return;
    }
    cg_inst *compare = cg_selection_branch_compare(c, i);
    if (compare) cg_take(c);
    else cg_value_read(c, t, 0, 0);
    cg_expect(c, ",");
    cg_expect(c, "label");
    char *yes = cg_take(c);
    cg_expect(c, ",");
    cg_expect(c, "label");
    char *no = cg_take(c);
    const char *condition = "ne";
    if (compare) {
      cg_inst *countdown = cg_selection_countdown(c, i);
      if (countdown) {
        c->pos = countdown->begin + 4;
        unsigned reg = cg_global_register(c, countdown->result->name);
        cg_value_read(c, countdown->result->type, reg, 0);
        cg_emit(c, "subs.w r%u, r%u, #1\n@ countdown-branch: fused 1\n", reg, reg);
        condition = cg_comparison_condition(c->tokens[compare->begin + 3].text);
      } else {
        c->pos = compare->begin + 4;
        cg_type *operand_type = cg_type_parse(c, 0);
        if (!cg_selection_compare_operands(c, compare, operand_type)) {
          cg_value_read(c, operand_type, 0, 0);
          if (cg_control_selection(c)) cg_normalize(c, operand_type, 0);
          if (cg_control_selection(c) && c->tokens[compare->begin + 3].text[0] == 's')
            cg_signed(c, operand_type, 0);
          cg_expect(c, ",");
          cg_value_read(c, operand_type, 1, 0);
          if (cg_control_selection(c)) cg_normalize(c, operand_type, 1);
          if (cg_control_selection(c) && c->tokens[compare->begin + 3].text[0] == 's')
            cg_signed(c, operand_type, 1);
          cg_emit(c, "cmp r0, r1\n");
        }
        condition = cg_comparison_condition(c->tokens[compare->begin + 3].text);
      }
    } else cg_emit(c, "cmp r0, #0\n");
    cg_conditional_edge(c, block, i, yes, no, condition);
    return;
  }
  if (!strcmp(op, "ret")) {
    cg_type *t = cg_type_parse(c, 0);
    if (t->size) {
      unsigned at = c->pos;
      for (unsigned w = 0; w < cg_round(t->size, 4) / 4; w++) {
        c->pos = at;
        cg_value_read(c, t, w, w);
      }
      if (c->fn->return_ext)
        cg_signed(c, t, 0);
    }
    if (c->layout_active && !block->next && i == block->last)
      c->layout_fallthroughs++;
    else
      cg_emit(c, "b%s .Lcg_return%u\n", cg_branch_width(c), c->fn->id);
    return;
  }
  if (!strcmp(op, "switch")) {
    cg_type *t = cg_type_parse(c, 0);
    if (t->size > 4)
      cg_error(c, "64-bit switch unsupported");
    cg_value_read(c, t, 6, 0);
    cg_expect(c, ",");
    cg_expect(c, "label");
    char *fallback = cg_take(c);
    cg_expect(c, "[");
    while (!cg_accept(c, "]")) {
      (void)cg_type_parse(c, 0);
      cg_value_read(c, t, 0, 0);
      cg_expect(c, ",");
      cg_expect(c, "label");
      char *target = cg_take(c);
      unsigned next = c->pos, id = ++c->unique;
      cg_emit(c, "cmp r6, r0\nbne%s .Lcg_switch%u\n", cg_branch_width(c), id);
      cg_edge(c, block, target, 0);
      cg_emit(c, ".Lcg_switch%u:\n", id);
      c->pos = next;
    }
    cg_edge(c, block, fallback, i == block->last);
    return;
  }
  if (!strcmp(op, "store") || !strcmp(op, "load")) {
    int store = !strcmp(op, "store");
    int is_volatile = cg_accept(c, "volatile");
    cg_type *t = cg_type_parse(c, 0);
    if (t->size > 8)
      cg_error(c, "aggregate load/store is outside backend scalar subset");
    unsigned value = c->pos;
    if (store) {
      unsigned d = 0;
      while (c->pos < i->end) {
        char *s = cg_take(c);
        if (!strcmp(s, "("))
          d++;
        if (!strcmp(s, ")"))
          d--;
        if (!d && !strcmp(s, ","))
          break;
      }
    } else
      cg_expect(c, ",");
    (void)cg_type_parse(c, 0);
    unsigned pointer = c->pos;
    unsigned direct_base = cg_operand_selection(c)
                               ? cg_global_register(c, c->tokens[pointer].text) : 0;
    unsigned fallback_base = c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
        !store && (t->size == 1 || t->size == 2 || t->size == 4) ? 0 : 4;
    if (direct_base) cg_take(c);
    else cg_value_read(c, NULL, fallback_base, 0);
    unsigned align = t->align;
    while (c->pos < i->end) {
      if (cg_accept(c, "align")) {
        align = (unsigned)cg_number(c, cg_take(c));
        break;
      }
      cg_take(c);
    }
    if (is_volatile && align < (t->size > 4 ? 4 : t->size))
      cg_error(c, "unaligned volatile scalar access unsupported");
    if (cg_operand_selection(c) && (t->size == 1 || t->size == 2 || t->size == 4) &&
        align >= t->size) {
      unsigned base = direct_base;
      unsigned reg = store ? cg_global_register(c, c->tokens[value].text)
                           : cg_global_register(c, i->result->name);
      if (!base) base = fallback_base;
      if (!reg) {
        reg = 0;
        if (store) {
          c->pos = value;
          cg_value_read(c, t, reg, 0);
        }
      }
      const char *suffix = t->size == 1 ? "b" : t->size == 2 ? "h" : "";
      cg_emit(c, "%s%s.%s r%u, [r%u]\n@ operand-memory: direct 1\n",
              store ? "str" : "ldr", suffix,
              cg_width_selection(c) && reg < 8 && base < 8 ? "n" : "w",
              reg, base);
      if (!store) {
        if (!t->bits || t->bits == 8 || t->bits == 16 || t->bits == 32)
          cg_save_normalized(c, i->result, reg);
        else cg_save(c, i->result, reg, 0);
      }
      return;
    }
    if (direct_base) cg_move(c, 4, direct_base);
    else if (fallback_base != 4) cg_move(c, 4, fallback_base);
    if (store)
      for (unsigned w = 0; w < cg_round(t->size, 4) / 4; w++) {
        c->pos = value;
        cg_value_read(c, t, w, w);
      }
    cg_memory(c, store, t, align);
    if (!store)
      for (unsigned w = 0; w < cg_round(t->size, 4) / 4; w++)
        cg_save(c, i->result, w, w);
    return;
  }
  if (!strcmp(op, "getelementptr")) {
    cg_to_type(c);
    cg_type *t = cg_type_parse(c, 0);
    cg_expect(c, ",");
    (void)cg_type_parse(c, 0);
    cg_value proof = {0};
    if (c->memory_selection == BADGE_MEMORY_WORD &&
        c->tokens[c->pos].text[0] == '%') {
      cg_value *base = cg_find(c, c->tokens[c->pos].text);
      proof.memory_align = base->memory_align;
      proof.memory_size = base->memory_size;
    }
    if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
        t->size == 1 && c->pos + 4 == i->end &&
        !strcmp(c->tokens[c->pos + 1].text, ",") &&
        !strcmp(c->tokens[c->pos + 2].text, "i32")) {
      unsigned base = cg_global_register(c, c->tokens[c->pos].text);
      unsigned dst = cg_global_register(c, i->result->name);
      const char *index_text = c->tokens[c->pos + 3].text;
      unsigned index_reg = cg_global_register(c, index_text);
      if (base && dst && (index_reg || isdigit((unsigned char)*index_text))) {
        if (index_reg) {
          cg_emit(c, "add.w r%u, r%u, r%u\n", dst, base, index_reg);
          proof.memory_align = proof.memory_size = 0;
        } else {
          uint32_t offset = (uint32_t)cg_number(c, index_text);
          cg_memory_offset(&proof, offset);
          if (offset <= 4095)
            cg_emit(c, "%s r%u, r%u, #%u\n",
                    offset <= 255 ? "add.w" : "addw", dst, base, offset);
          else {
            cg_immediate(c, 0, offset);
            cg_emit(c, "add.w r%u, r%u, r0\n", dst, base);
          }
        }
        i->result->memory_align = proof.memory_align;
        i->result->memory_size = proof.memory_size;
        cg_save_normalized(c, i->result, dst);
        return;
      }
    }
    cg_value_read(c, NULL, 0, 0);
    unsigned index = 0;
    while (cg_accept(c, ",")) {
      cg_type *it = cg_type_parse(c, 0);
      unsigned stride;
      if (!index++)
        stride = t->size;
      else if (t->base) {
        t = t->base;
        stride = t->size;
      } else {
        unsigned field = (unsigned)cg_number(c, cg_take(c));
        unsigned offset = cg_field_offset(c, &t, field);
        cg_memory_offset(&proof, offset);
        if (!cg_selection_offset(c, offset)) {
          cg_immediate(c, 1, offset);
          cg_emit(c, "add.w r0, r0, r1\n");
        }
        continue;
      }
      const char *index_text = c->tokens[c->pos].text;
      if (proof.memory_align && it->bits >= 32 &&
          isdigit((unsigned char)*index_text)) {
        uint64_t n = cg_number(c, index_text);
        if (n <= INT32_MAX)
          cg_memory_offset(&proof, n * stride);
        else
          proof.memory_align = proof.memory_size = 0;
      } else
        proof.memory_align = proof.memory_size = 0;
      if (cg_selection_index(c, it, stride))
        continue;
      cg_value_read(c, it, 1, 0);
      if (stride != 1) {
        cg_immediate(c, 2, stride);
        cg_emit(c, "mul r1, r1, r2\n");
      }
      cg_emit(c, "add.w r0, r0, r1\n");
    }
    i->result->memory_align = proof.memory_align;
    i->result->memory_size = proof.memory_size;
    cg_save(c, i->result, 0, 0);
    return;
  }
  if (!strcmp(op, "insertvalue") || !strcmp(op, "extractvalue")) {
    cg_type *t = cg_type_parse(c, 0);
    unsigned value = c->pos;
    cg_take(c);
    cg_expect(c, ",");
    int insert = !strcmp(op, "insertvalue");
    cg_type *part = NULL;
    unsigned part_at = 0;
    if (insert) {
      part = cg_type_parse(c, 0);
      part_at = c->pos;
      cg_take(c);
      cg_expect(c, ",");
    }
    unsigned index = cg_aggregate_index(c);
    cg_type *member = t;
    unsigned offset = cg_field_offset(c, &member, index);
    if (offset % 4 || member->size % 4)
      cg_error(c, "non-word aggregate SSA component unsupported");
    if (insert) {
      for (unsigned w = 0; w < cg_round(t->size, 4) / 4; w++) {
        c->pos = value;
        cg_value_read(c, t, 0, w);
        cg_save(c, i->result, 0, w);
      }
      for (unsigned w = 0; w < part->size / 4; w++) {
        c->pos = part_at;
        cg_value_read(c, part, 0, w);
        cg_stack(c, 1, 0, i->result->slot + offset + w * 4);
      }
    } else {
      for (unsigned w = 0; w < member->size / 4; w++) {
        c->pos = value;
        cg_value_read(c, t, 0, offset / 4 + w);
        cg_save(c, i->result, 0, w);
      }
    }
    return;
  }
  if (!strcmp(op, "trunc") || !strcmp(op, "zext") || !strcmp(op, "sext") ||
      !strcmp(op, "ptrtoint") || !strcmp(op, "inttoptr") ||
      !strcmp(op, "freeze")) {
    cg_to_type(c);
    cg_type *from = cg_type_parse(c, 0);
    unsigned at = c->pos;
    if (cg_operand_selection(c) && !strcmp(op, "zext") &&
        from->size <= 4 && i->result->type->size == 4 &&
        c->tokens[at].text[0] == '%') {
      unsigned source = cg_global_register(c, c->tokens[at].text);
      if (source) {
        unsigned dst = cg_global_register(c, i->result->name);
        if (!dst) dst = 0;
        if (dst != source) cg_move(c, dst, source);
        cg_save_normalized(c, i->result, dst);
        return;
      }
    }
    if ((!strcmp(op, "freeze") || !strcmp(op, "ptrtoint") ||
         !strcmp(op, "inttoptr")) &&
        ((from->size == 4 && from->bits == 32) ||
         (!strcmp(op, "freeze") &&
          c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT)) &&
        c->tokens[at].text[0] == '%' &&
        cg_register_copy(c, i->result, cg_find(c, c->tokens[at].text)))
      return;
    cg_value_read(c, from, 0, 0);
    if (!strcmp(op, "sext"))
      cg_signed(c, from, 0);
    cg_save(c, i->result, 0, 0);
    if (i->result->type->size == 8) {
      if (from->size == 8) {
        c->pos = at;
        cg_value_read(c, from, 1, 1);
      } else if (!strcmp(op, "sext"))
        cg_emit(c, "asr.w r1, r0, #31\n");
      else
        cg_immediate(c, 1, 0);
      cg_save(c, i->result, 1, 1);
    }
    return;
  }
  if (!strcmp(op, "select")) {
    cg_type *ct = cg_type_parse(c, 0);
    cg_value_read(c, ct, 4, 0);
    cg_expect(c, ",");
    cg_type *t = cg_type_parse(c, 0);
    unsigned yes = c->pos;
    unsigned d = 0;
    while (c->pos < i->end) {
      char *s = cg_take(c);
      if (!strcmp(s, "("))
        d++;
      if (!strcmp(s, ")"))
        d--;
      if (!d && !strcmp(s, ","))
        break;
    }
    (void)cg_type_parse(c, 0);
    unsigned no = c->pos;
    for (unsigned w = 0; w < cg_round(t->size, 4) / 4; w++) {
      c->pos = yes;
      cg_value_read(c, t, 0, w);
      c->pos = no;
      cg_value_read(c, t, 1, w);
      cg_emit(c, "cmp r4, #0\nit eq\nmoveq r0, r1\n");
      cg_save(c, i->result, 0, w);
    }
    return;
  }
  if (!strcmp(op, "icmp")) {
    cg_accept(c, "samesign");
    char *pred = cg_take(c);
    cg_type *t = cg_type_parse(c, 0);
    unsigned left = c->pos;
    cg_value_read(c, t, 0, 0);
    cg_expect(c, ",");
    unsigned right = c->pos;
    cg_value_read(c, t, 1, 0);
    if (t->size == 8) {
      c->pos = left;
      cg_value_read(c, t, 2, 1);
      c->pos = right;
      cg_value_read(c, t, 3, 1);
      if (!strcmp(pred, "eq") || !strcmp(pred, "ne")) {
        cg_emit(c,
                "eor.w r0, r0, r1\neor.w r2, r2, r3\norr.w r0, r0, r2\ncmp r0, "
                "#0\nmov.w r0, #0\nit %s\nmov%s r0, #1\n",
                pred, pred);
      } else {
        if (strlen(pred) != 3 || !strchr("su", pred[0]) ||
            !strchr("lg", pred[1]) || !strchr("te", pred[2]))
          cg_error(c, "unsupported wide comparison");
        unsigned id = ++c->unique;
        int less = pred[1] == 'l', sign = pred[0] == 's';
        const char *highyes =
            sign ? (less ? "lt" : "gt") : (less ? "lo" : "hi");
        const char *highno = sign ? (less ? "gt" : "lt") : (less ? "hi" : "lo");
        const char *low = less ? (pred[2] == 'e' ? "ls" : "lo")
                               : (pred[2] == 'e' ? "hs" : "hi");
        cg_emit(c,
                "mov.w r4, #0\ncmp r2, r3\nb%s.w .Lcg_cmpyes%u\nb%s.w "
                ".Lcg_cmpend%u\ncmp r0, r1\nb%s.w .Lcg_cmpyes%u\nb.w "
                ".Lcg_cmpend%u\n.Lcg_cmpyes%u:\nmov.w r4, "
                "#1\n.Lcg_cmpend%u:\nmov r0, r4\n",
                highyes, id, highno, id, low, id, id, id, id);
      }
      cg_save(c, i->result, 0, 0);
      return;
    }
    int sign = pred[0] == 's';
    if (sign) {
      cg_signed(c, t, 0);
      cg_signed(c, t, 1);
    }
    const char *cond = cg_comparison_condition(pred);
    if (!cond)
      cg_error(c, "unsupported comparison");
    cg_emit(c, "cmp r0, r1\nmov.w r0, #0\nit %s\nmov%s r0, #1\n", cond, cond);
    cg_save(c, i->result, 0, 0);
    return;
  }
  cg_to_type(c);
  cg_type *t = cg_type_parse(c, 0);
  unsigned left = c->pos;
  cg_value_read(c, t, 0, 0);
  cg_expect(c, ",");
  unsigned right = c->pos;
  cg_value_read(c, t, 2, 0);
  if (t->size == 8) {
    c->pos = left;
    cg_value_read(c, t, 1, 1);
    c->pos = right;
    cg_value_read(c, t, 3, 1);
  }
  if (!strcmp(op, "add") || !strcmp(op, "sub")) {
    if (t->size == 8 || !cg_compact_binary(c, op, 0, 0, 2))
      cg_emit(c, "%s.w r0, r0, r2\n",
              t->size == 8 ? (!strcmp(op, "add") ? "adds" : "subs") : op);
    if (t->size == 8)
      cg_emit(c, "%s.w r1, r1, r3\n", !strcmp(op, "add") ? "adc" : "sbc");
  } else if (!strcmp(op, "mul")) {
    if (t->size == 8)
      cg_emit(c, "mul r1, r1, r2\nmla r1, r0, r3, r1\numull r4, r5, r0, "
                 "r2\nadd.w r1, r1, r5\nmov r0, r4\n");
    else if (!cg_compact_binary(c, op, 0, 0, 2))
      cg_emit(c, "mul r0, r0, r2\n");
  } else if (!strcmp(op, "and") || !strcmp(op, "or") || !strcmp(op, "xor")) {
    const char *ins = !strcmp(op, "and")  ? "and"
                      : !strcmp(op, "or") ? "orr"
                                          : "eor";
    if (t->size == 8 || !cg_compact_binary(c, op, 0, 0, 2))
      cg_emit(c, "%s.w r0, r0, r2\n", ins);
    if (t->size == 8)
      cg_emit(c, "%s.w r1, r1, r3\n", ins);
  } else if (!strcmp(op, "shl") || !strcmp(op, "lshr") || !strcmp(op, "ashr")) {
    if (t->size == 8) {
      c->pos = right;
      uint64_t amount = cg_number(c, cg_take(c));
      if (amount >= 64)
        cg_error(c, "64-bit shift count out of range");
      unsigned shift = (unsigned)amount;
      int left_shift = !strcmp(op, "shl"), arithmetic = !strcmp(op, "ashr");
      if (shift && shift < 32) {
        if (left_shift)
          cg_emit(c,
                  "lsr.w r4, r0, #%u\nlsl.w r1, r1, #%u\norr.w r1, r1, "
                  "r4\nlsl.w r0, r0, #%u\n",
                  32 - shift, shift, shift);
        else
          cg_emit(c,
                  "lsl.w r4, r1, #%u\nlsr.w r0, r0, #%u\norr.w r0, r0, "
                  "r4\n%s.w r1, r1, #%u\n",
                  32 - shift, shift, arithmetic ? "asr" : "lsr", shift);
      } else if (shift >= 32) {
        if (left_shift) {
          if (shift == 32)
            cg_emit(c, "mov r1, r0\n");
          else
            cg_emit(c, "lsl.w r1, r0, #%u\n", shift - 32);
          cg_immediate(c, 0, 0);
        } else {
          if (shift == 32)
            cg_emit(c, "mov r0, r1\n");
          else
            cg_emit(c, "%s.w r0, r1, #%u\n", arithmetic ? "asr" : "lsr",
                    shift - 32);
          if (arithmetic)
            cg_emit(c, "asr.w r1, r1, #31\n");
          else
            cg_immediate(c, 1, 0);
        }
      }
      cg_save(c, i->result, 0, 0);
      cg_save(c, i->result, 1, 1);
      return;
    }
    if (!strcmp(op, "ashr"))
      cg_signed(c, t, 0);
    if (!cg_compact_binary(c, op, 0, 0, 2))
      cg_emit(c, "%s.w r0, r0, r2\n",
              !strcmp(op, "shl")    ? "lsl"
              : !strcmp(op, "lshr") ? "lsr"
                                    : "asr");
  } else if (!strcmp(op, "sdiv") || !strcmp(op, "udiv") ||
             !strcmp(op, "srem") || !strcmp(op, "urem")) {
    int sign = op[0] == 's', rem = strstr(op, "rem") != NULL;
    if (t->size == 8) {
      c->pos = right;
      uint64_t divisor = cg_number(c, cg_take(c));
      if (!sign || rem || !badge_direct_i64_sdiv(divisor))
        cg_error(c,
                 "64-bit division supports signed positive powers of two only");
      unsigned shift = 0;
      while ((UINT64_C(1) << shift) != divisor)
        shift++;
      if (!shift) {
        cg_save(c, i->result, 0, 0);
        cg_save(c, i->result, 1, 1);
        return;
      }
      cg_emit(c, "asr.w r4, r1, #31\n");
      cg_immediate(c, 5, (uint32_t)divisor - 1);
      cg_emit(c, "and.w r4, r4, r5\nadds.w r0, r0, r4\nadc r1, r1, #0\n");
      if (shift)
        cg_emit(
            c,
            "lsr.w r0, r0, #%u\norr.w r0, r0, r1, lsl #%u\nasr.w r1, r1, #%u\n",
            shift, 32 - shift, shift);
    } else {
      if (sign) {
        cg_signed(c, t, 0);
        cg_signed(c, t, 2);
      }
      cg_emit(c, "%s r4, r0, r2\n", sign ? "sdiv" : "udiv");
      if (rem)
        cg_emit(c, "mls r0, r4, r2, r0\n");
      else
        cg_emit(c, "mov r0, r4\n");
    }
  } else
    cg_error(c, "unsupported instruction");
  cg_save(c, i->result, 0, 0);
  if (t->size == 8)
    cg_save(c, i->result, 1, 1);
}
static void cg_process(cg_context *c, const char *source, size_t size) {
  cg_lex(c, source, size);
  cg_emit(c, ".syntax unified\n.cpu cortex-m33\n.thumb\n.eabi_attribute 18, "
             "4\n.eabi_attribute 24, 1\n.eabi_attribute 25, 1\n.eabi_attribute "
             "28, 0\n");
  int target = 0;
  while (c->pos < c->count) {
    unsigned line = c->tokens[c->pos].line;
    char *s = c->tokens[c->pos].text;
    if (*s == '@') {
      cg_global(c);
      continue;
    }
    cg_take(c);
    if (!strcmp(s, "define"))
      cg_function_parse(c);
    else if (!strcmp(s, "target")) {
      char *kind = cg_take(c);
      cg_expect(c, "=");
      char *value = cg_take(c);
      if (!strcmp(kind, "triple")) {
        if (strcmp(value, BADGE_IR_TRIPLE))
          cg_error(c, "unsupported target triple");
        target = 1;
      } else if (!strcmp(kind, "datalayout") && strcmp(value, BADGE_IR_LAYOUT))
        cg_error(c, "unsupported data layout");
      cg_skip_line(c, line);
    } else if (!strcmp(s, "declare") || !strcmp(s, "attributes") ||
               !strcmp(s, "source_filename") || *s == '!')
      cg_skip_line(c, line);
    else
      cg_error(c, "unsupported module construct");
  }
  if (!target)
    cg_error(c, "missing target triple");
  cg_prepare_cfg(c);
  for (cg_function *f = c->functions; f; f = f->next)
    cg_generate_function(c, f);
  cg_emit(c, ".section .note.GNU-stack,\"\",%%progbits\n");
}
int badge_codegen_compile(const char *source, size_t length,
                          const char *filename, char **assembly,
                          size_t *assembly_size) {
  return badge_codegen_compile_with_options(
      source, length, filename,
      (badge_codegen_options){BADGE_DEFAULT_FRAME_ADDRESSING,
                              BADGE_DEFAULT_REGISTER_RESIDENCY},
      assembly, assembly_size);
}
int badge_codegen_compile_with_frame_addressing(
    const char *source, size_t length, const char *filename,
    badge_frame_addressing frame_addressing, char **assembly,
    size_t *assembly_size) {
  return badge_codegen_compile_with_options(
      source, length, filename,
      (badge_codegen_options){frame_addressing, BADGE_DEFAULT_REGISTER_RESIDENCY},
      assembly, assembly_size);
}
int badge_codegen_compile_with_options(const char *source, size_t length,
                                       const char *filename,
                                       badge_codegen_options options,
                                       char **assembly, size_t *assembly_size) {
  return badge_codegen_compile_with_scalar_selection(
      source, length, filename, options, BADGE_DEFAULT_SCALAR_SELECTION, assembly,
      assembly_size);
}
int badge_codegen_compile_with_scalar_selection(
    const char *source, size_t length, const char *filename,
    badge_codegen_options options, badge_scalar_selection selection,
    char **assembly, size_t *assembly_size) {
  return badge_codegen_compile_with_selections(
      source, length, filename, options, selection, BADGE_DEFAULT_MEMORY_SELECTION, assembly,
      assembly_size);
}
int badge_codegen_compile_with_selections(
    const char *source, size_t length, const char *filename,
    badge_codegen_options options, badge_scalar_selection selection,
    badge_memory_selection memory_selection, char **assembly,
    size_t *assembly_size) {
  if (assembly)
    *assembly = NULL;
  if (assembly_size)
    *assembly_size = 0;
  if (!source || !filename || !assembly || !assembly_size) {
    fputs("badge-codegen: invalid compile arguments\n", stderr);
    return 0;
  }
  if (options.frame_addressing != BADGE_FRAME_MATERIALIZED &&
      options.frame_addressing != BADGE_FRAME_DIRECT) {
    fputs("badge-codegen: invalid frame addressing selection\n", stderr);
    return 0;
  }
  if (options.register_residency != BADGE_RESIDENCY_STACK &&
      options.register_residency != BADGE_RESIDENCY_LOCAL &&
      options.register_residency != BADGE_RESIDENCY_GLOBAL &&
      options.register_residency != BADGE_RESIDENCY_GLOBAL_FLEX &&
      options.register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT) {
    fputs("badge-codegen: invalid register residency selection\n", stderr);
    return 0;
  }
  if (selection != BADGE_SELECTION_LEGACY &&
      selection != BADGE_SELECTION_THUMB &&
      selection != BADGE_SELECTION_THUMB_REGISTERS &&
      selection != BADGE_SELECTION_THUMB_LAYOUT &&
      selection != BADGE_SELECTION_THUMB_COMPACT &&
      selection != BADGE_SELECTION_THUMB_SCHEDULE &&
      selection != BADGE_SELECTION_THUMB_RELAX &&
      selection != BADGE_SELECTION_THUMB_BRANCH &&
      selection != BADGE_SELECTION_THUMB_HOMES &&
      selection != BADGE_SELECTION_THUMB_CALLS &&
      selection != BADGE_SELECTION_THUMB_EFFECTS &&
      selection != BADGE_SELECTION_THUMB_FRAMES &&
      selection != BADGE_SELECTION_THUMB_CONTROL &&
      selection != BADGE_SELECTION_THUMB_OPERANDS &&
      selection != BADGE_SELECTION_THUMB_WIDTHS &&
      selection != BADGE_SELECTION_THUMB_CONSTANTS) {
    fputs("badge-codegen: invalid scalar selection\n", stderr);
    return 0;
  }
  if (memory_selection != BADGE_MEMORY_BYTE &&
      memory_selection != BADGE_MEMORY_WORD) {
    fputs("badge-codegen: invalid memory selection\n", stderr);
    return 0;
  }
  cg_context *c = calloc(1, sizeof(*c));
  if (!c) {
    fputs("badge-codegen: out of memory\n", stderr);
    return 0;
  }
  c->file = filename;
  c->frame_addressing = options.frame_addressing;
  c->register_residency = options.register_residency;
  c->scalar_selection = selection;
  c->memory_selection = memory_selection;
  int result = 0;
  if (!setjmp(c->failure)) {
    if (length > 16u * 1024u * 1024u)
      cg_error(c, "IR input exceeds 16 MiB");
    /* Keep the original input charge in the allocation budget while borrowing.
     */
    c->memory = length + 1;
    if (memchr(source, 0, length))
      cg_error(c, "NUL byte in IR");
    cg_process(c, source, length);
    *assembly = c->out;
    *assembly_size = c->size;
    c->out = NULL;
    result = 1;
  }
  while (c->allocations) {
    cg_allocation *a = c->allocations;
    c->allocations = a->next;
    free(a);
  }
  free(c->out);
  free(c);
  return result;
}
