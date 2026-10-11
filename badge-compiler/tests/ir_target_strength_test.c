#include "ir_pass.h"
#include "target.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned changes, pairs, fixtures;
static void observe(void *context, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t n) {
  (void)context;
  (void)step;
  assert(!strcmp(pass, "target-strength-reduce"));
  if (!strcmp(event, "transformed"))
    changes += (unsigned)n;
  if (!strcmp(reason, "shared_divrem_charged_once"))
    pairs += (unsigned)n;
}
static void fixture(const char *source, const char *present, unsigned expected,
                    unsigned sharing) {
  const char *header = "target triple = " BADGE_IR_TRIPLE
                       "\ntarget datalayout = " BADGE_IR_LAYOUT "\n";
  char *input = malloc(strlen(header) + strlen(source) + 1);
  assert(input);
  strcpy(input, header);
  strcat(input, source);
  source = input;
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "target-strength.ll";
  c->observe = observe;
  if (setjmp(c->failure))
    abort();
  changes = pairs = 0;
  ir_pipeline(c, source, strlen(source), "target-strength-reduce");
  if (c->memory || changes != expected || pairs != sharing) {
    fprintf(stderr, "fixture %u: changes %u/%u, pairs %u/%u, memory %zu\n%s\n",
            fixtures, changes, expected, pairs, sharing, c->memory, c->output);
    abort();
  }
  assert(strstr(c->output, present));
  if (!expected)
    assert(c->size == strlen(source) && !memcmp(c->output, source, c->size));
  char *copy = malloc(c->size + 1);
  assert(copy);
  memcpy(copy, c->output, c->size + 1);
  size_t size = c->size;
  changes = pairs = 0;
  ir_pipeline(c, copy, size, "target-strength-reduce");
  assert(!changes && !pairs && c->size == size &&
         !memcmp(c->output, copy, size));
  free(copy);
  ir_destroy(c);
  free(c);
  free(input);
  fixtures++;
}
static void unary(const char *op, unsigned bits, const char *divisor,
                  const char *present, unsigned expected) {
  char source[512];
  snprintf(source, sizeof(source),
           "define i%u @f(i%u %%x) {\nentry:\n"
           "%%v = %s i%u %%x, %s\nret i%u %%v\n}\n",
           bits, bits, op, bits, divisor, bits);
  fixture(source, present, expected, 0);
}
int main(void) {
  for (unsigned shift = 0; shift < 32; shift++) {
    char divisor[32], quotient[64], remainder[64], source[512];
    uint32_t d = UINT32_C(1) << shift;
    snprintf(divisor, sizeof(divisor), "%u", d);
    snprintf(quotient, sizeof(quotient), "lshr i32 %%x, %u", shift);
    snprintf(remainder, sizeof(remainder), "and i32 %%x, %u", d - 1);
    unary("udiv", 32, divisor, quotient, 1);
    unary("urem", 32, divisor, remainder, 1);
    for (unsigned reverse = 0; reverse < 2; reverse++) {
      snprintf(source, sizeof(source),
               "define i32 @f(i32 %%x) {\nentry:\n"
               "%%a = %s i32 %%x, %s\n%%b = %s i32 %%x, %s\n"
               "%%v = xor i32 %%a, %%b\nret i32 %%v\n}\n",
               reverse ? "urem" : "udiv", divisor,
               reverse ? "udiv" : "urem", divisor);
      fixture(source, quotient, 2, 1);
    }
  }
  const unsigned widths[] = {1, 8, 16, 64};
  for (unsigned i = 0; i < sizeof(widths) / sizeof(*widths); i++) {
    unary("udiv", widths[i], "1", "udiv", 0);
    unary("urem", widths[i], "1", "urem", 0);
  }
  unary("sdiv", 32, "8", "sdiv", 0);
  unary("srem", 32, "8", "srem", 0);
  unary("udiv exact", 32, "8", "udiv exact", 0);
  unary("udiv", 32, "0", "udiv", 0);
  unary("urem", 32, "3", "urem", 0);
  unary("udiv", 32, "-1", "udiv", 0);
  unary("udiv", 32, "-2147483648", "lshr i32 %x, 31", 1);
  fixture("define i32 @f(i32 %x, i32 %y) {\nentry:\n"
          "%v = udiv i32 %x, %y\nret i32 %v\n}\n", "udiv", 0, 0);
  fixture("define i32 @f() {\nentry:\n"
          "%v = urem i32 poison, 8\nret i32 %v\n}\n", "poison", 0, 0);
  fixture("define i32 @f() {\nentry:\n"
          "%v = udiv i32 undef, 8\nret i32 %v\n}\n", "undef", 0, 0);
  fixture("define i32 @f() {\nentry:\n"
          "%x = freeze i32 poison\n%v = udiv i32 %x, 8\nret i32 %v\n}\n",
          "lshr i32 %x, 3", 1, 0);
  fixture("define i32 @f(i32 %x) {\nentry:\n"
          "%v = udiv i32 %x, 8, !foo !0\nret i32 %v\n}\n!0 = !{}\n",
          "!foo !0", 0, 0);
  fixture("define i32 @f(i1 %c, i32 %x) {\nentry:\n"
          "br i1 %c, label %yes, label %no\nyes:\n"
          "%q = udiv i32 %x, 8\nbr label %end\nno:\n"
          "%r = urem i32 %x, 8\nbr label %end\nend:\n"
          "%v = phi i32 [ %q, %yes ], [ %r, %no ]\nret i32 %v\n}\n",
          "phi i32 [ %q, %yes ], [ %r, %no ]", 2, 0);
  printf("target-strength: %u typed/fusion/flags/poison/PHI/idempotence fixtures PASS\n",
         fixtures);
  return 0;
}
