#include "ir_pass.h"
#include "target.h"
#include <inttypes.h>
#include <string.h>

static void narrow_helpers(ir_context *c, const char *udiv, const char *sdiv) {
  if (!udiv)
    return;
  /* Every caller supplies a nonzero divisor below 2^31. The shifted
     remainder therefore fits in one word, and the numerator's low word
     can carry both the remaining input bits and the growing quotient. */
  ir_emit(c,
      "define internal i64 %s(i64 %%numerator, i32 %%denominator) {\n"
      "entry:\n"
      "  %%upper = lshr i64 %%numerator, 32\n"
      "  %%hi = trunc i64 %%upper to i32\n"
      "  %%lo = trunc i64 %%numerator to i32\n"
      "  %%fits = icmp eq i32 %%hi, 0\n"
      "  br i1 %%fits, label %%small, label %%setup\n"
      "small:\n"
      "  %%short.q = udiv i32 %%lo, %%denominator\n"
      "  %%short.wide = zext i32 %%short.q to i64\n"
      "  ret i64 %%short.wide\n"
      "setup:\n"
      "  %%high.r = urem i32 %%hi, %%denominator\n"
      "  %%high.q = udiv i32 %%hi, %%denominator\n"
      "  br label %%loop\n"
      "loop:\n"
      "  %%lane = phi i32 [ %%lo, %%setup ], [ %%next.lane, %%loop ]\n"
      "  %%r = phi i32 [ %%high.r, %%setup ], [ %%next.r, %%loop ]\n"
      "  %%count = phi i32 [ 32, %%setup ], [ %%next.count, %%loop ]\n"
      "  %%shift.r = shl i32 %%r, 1\n"
      "  %%bit = lshr i32 %%lane, 31\n"
      "  %%candidate = or i32 %%shift.r, %%bit\n"
      "  %%enough = icmp uge i32 %%candidate, %%denominator\n"
      "  %%qbit = zext i1 %%enough to i32\n"
      "  %%selected.d = mul i32 %%denominator, %%qbit\n"
      "  %%next.r = sub i32 %%candidate, %%selected.d\n"
      "  %%shift.lane = shl i32 %%lane, 1\n"
      "  %%next.lane = or i32 %%shift.lane, %%qbit\n"
      "  %%next.count = sub i32 %%count, 1\n"
      "  %%done = icmp eq i32 %%next.count, 0\n"
      "  br i1 %%done, label %%exit, label %%loop\n"
      "exit:\n"
      "  %%wide.hi = zext i32 %%high.q to i64\n"
      "  %%wide.lo = zext i32 %%next.lane to i64\n"
      "  %%high.bits = shl i64 %%wide.hi, 32\n"
      "  %%quotient = or i64 %%high.bits, %%wide.lo\n"
      "  ret i64 %%quotient\n}\n",
      udiv);
  if (sdiv)
    ir_emit(c,
        "define internal i64 %s(i64 %%numerator, i32 %%denominator) {\n"
        "entry:\n"
        "  %%negative = icmp slt i64 %%numerator, 0\n"
        "  br i1 %%negative, label %%minus, label %%plus\n"
        "plus:\n"
        "  %%positive.q = call i64 %s(i64 %%numerator, i32 %%denominator)\n"
        "  ret i64 %%positive.q\n"
        "minus:\n"
        "  %%negated = sub i64 0, %%numerator\n"
        "  %%q = call i64 %s(i64 %%negated, i32 %%denominator)\n"
        "  %%minus.q = sub i64 0, %%q\n"
        "  ret i64 %%minus.q\n}\n",
        sdiv, udiv, udiv);
}

static void declare_clz(ir_context *c, ir_module *m) {
  const char *key = ir_name_key(c, "@llvm.ctlz.i64");
  for (unsigned l = 0; l < m->line_count; l++) {
    const ir_line *line = &m->lines[l];
    if (!line->count || strcmp(ir_text(m, line->first), "declare"))
      continue;
    for (unsigned k = line->first + 1; k < line->first + line->count; k++) {
      const char *name = ir_text(m, k);
      if (*name != '@')
        continue;
      if (!strcmp(name, "@llvm.ctlz.i64") ||
          (name[1] == '"' && !strcmp(ir_name_key(c, name), key)))
        return;
      break;
    }
  }
  ir_emit(c, "declare i64 @llvm.ctlz.i64(i64, i1 immarg)\n");
}

void ir_wide_division(ir_context *c, ir_module *m) {
  char *udiv = NULL, *sdiv = NULL, *narrow_udiv = NULL, *narrow_sdiv = NULL;
  for (unsigned l = 0; l < m->line_count; l++) {
    ir_line *line = &m->lines[l];
    c->line = l + 1;
    if (!line->instruction)
      continue;
    unsigned p = line->op, end = line->first + line->count;
    const char *op = ir_text(m, p);
    if (strcmp(op, "sdiv") && strcmp(op, "udiv") && strcmp(op, "srem") &&
        strcmp(op, "urem"))
      continue;
    ir_note(c, "candidate", "integer_division_or_remainder", 1);
    int sign = op[0] == 's', rem = !strcmp(op + 1, "rem");
    unsigned type = p + 1;
    int exact = !rem && !strcmp(ir_text(m, type), "exact");
    if (exact)
      type++;
    if (strcmp(ir_text(m, type), "i64")) {
      ir_note(c, "skipped", "backend_supported_width", 1);
      continue;
    }
    if (line->op != line->first + 2 || end - type != 4 ||
        strcmp(ir_text(m, type + 2), ","))
      ir_error(c, "unsupported wide division operands/flags");
    const char *a = ir_text(m, type + 1), *b = ir_text(m, type + 3);
    uint64_t divisor, ignored;
    int literal = ir_integer(b, &divisor);
    if (sign && !rem && !exact && literal && badge_direct_i64_sdiv(divisor)) {
      ir_note(c, "skipped", "direct_i64_power_of_two", 1);
      continue;
    }
    if ((*a != '%' && !ir_integer(a, &ignored)) ||
        (*b != '%' && !literal))
      ir_error(c, "wide division requires scalar integer operands");
    const char *result = ir_text(m, line->first);
    if (literal && divisor && !(divisor & (divisor - 1))) {
      unsigned shift = (unsigned)__builtin_ctzll(divisor);
      if (!sign || (!rem && exact && divisor <= INT64_MAX)) {
        if (rem)
          line->replacement = ir_format(c, "  %s = and i64 %s, %" PRIu64 "\n",
                                        result, a, divisor - 1);
        else
          line->replacement = ir_format(c, "  %s = %s%s i64 %s, %u\n",
              result, sign ? "ashr" : "lshr", exact ? " exact" : "", a, shift);
        ir_note(c, "transformed", sign ? "exact_signed_wide_power_of_two" :
                                       "unsigned_wide_power_of_two", 1);
        continue;
      }
      if (divisor <= INT64_MAX) {
        char *negative = ir_fresh(c, m, '%'), *bias = ir_fresh(c, m, '%');
        char *sum = ir_fresh(c, m, '%');
        char *prefix = ir_format(c,
            "  %s = ashr i64 %s, 63\n  %s = and i64 %s, %" PRIu64 "\n"
            "  %s = add i64 %s, %s\n", negative, a, bias, negative,
            divisor - 1, sum, a, bias);
        if (rem) {
          char *bits = ir_fresh(c, m, '%');
          line->replacement = ir_format(c,
              "%s  %s = and i64 %s, %" PRIu64 "\n  %s = sub i64 %s, %s\n",
              prefix, bits, sum, divisor - 1, result, bits, bias);
        } else
          line->replacement =
              ir_format(c, "%s  %s = ashr i64 %s, %u\n", prefix, result, sum, shift);
        ir_note(c, "transformed", rem ? "signed_wide_power_of_two_remainder" :
                                       "signed_wide_power_of_two_quotient", 1);
        continue;
      }
    }
    ir_note(c, "transformed", rem ? "remainder_lowered" : "division_lowered",
            1);
    int narrow = literal && divisor && divisor <= INT32_MAX;
    char **unsigned_helper = narrow ? &narrow_udiv : &udiv;
    char **signed_helper = narrow ? &narrow_sdiv : &sdiv;
    if (!*unsigned_helper)
      *unsigned_helper = ir_fresh(c, m, '@');
    if (sign && !*signed_helper)
      *signed_helper = ir_fresh(c, m, '@');
    const char *helper = sign ? *signed_helper : *unsigned_helper;
    const char *denominator_type = narrow ? "i32" : "i64";
    if (narrow)
      ir_note(c, "transformed", "positive_word_divisor", 1);
    if (!rem)
      line->replacement = ir_format(c, "  %s = call i64 %s(i64 %s, %s %s)\n",
                                    result, helper, a, denominator_type, b);
    else {
      char *quotient = ir_fresh(c, m, '%'), *product = ir_fresh(c, m, '%');
      line->replacement = ir_format(c,
                                    "  %s = call i64 %s(i64 %s, %s %s)\n  %s "
                                    "= mul i64 %s, %s\n  %s = sub i64 %s, %s\n",
                                    quotient, helper, a, denominator_type, b, product, quotient,
                                    b, result, a, product);
    }
  }
  if (!udiv && !narrow_udiv)
    return;
  if (udiv && !narrow_udiv)
    narrow_udiv = ir_fresh(c, m, '@');
  size_t start = c->size;
  if (narrow_udiv)
    ir_note(c, "generated", "unsigned_word_divisor_helper", 1);
  if (narrow_sdiv)
    ir_note(c, "generated", "signed_word_divisor_helper", 1);
  narrow_helpers(c, narrow_udiv, narrow_sdiv);
  if (udiv) {
    ir_note(c, "generated", "unsigned_division_helper", 1);
    declare_clz(c, m);
    /* On the wide path n >= d > 0, so CLZ(d)-CLZ(n) is nonnegative.
       Aligning the top set bits cannot overflow; each step emits one bit. */
    ir_emit(
      c,
      "define internal i64 %s(i64 %%numerator, i64 %%denominator) {\n"
      "entry:\n"
      "  %%high = lshr i64 %%denominator, 32\n"
      "  %%upper = trunc i64 %%high to i32\n"
      "  %%upper.zero = icmp eq i32 %%upper, 0\n"
      "  br i1 %%upper.zero, label %%dispatch, label %%wide\n"
      "dispatch:\n"
      "  %%d32 = trunc i64 %%denominator to i32\n"
      "  %%zero = icmp eq i32 %%d32, 0\n"
      "  br i1 %%zero, label %%invalid, label %%word.check\n"
      "invalid:\n  unreachable\n"
      "word.check:\n"
      "  %%word.d = icmp ult i32 %%d32, 2147483648\n"
      "  br i1 %%word.d, label %%word, label %%wide\n"
      "word:\n"
      "  %%word.q = call i64 %s(i64 %%numerator, i32 %%d32)\n"
      "  ret i64 %%word.q\n"
      "wide:\n"
      "  %%too.small = icmp ult i64 %%numerator, %%denominator\n"
      "  br i1 %%too.small, label %%no.quotient, label %%length\n"
      "no.quotient:\n  ret i64 0\n"
      "length:\n"
      "  %%zero.n = call i64 @llvm.ctlz.i64(i64 %%numerator, i1 false)\n"
      "  %%zero.d = call i64 @llvm.ctlz.i64(i64 %%denominator, i1 false)\n"
      "  %%leading.n = trunc i64 %%zero.n to i32\n"
      "  %%leading.d = trunc i64 %%zero.d to i32\n"
      "  %%delta = sub i32 %%leading.d, %%leading.n\n"
      "  %%one.bit = icmp eq i32 %%delta, 0\n"
      "  br i1 %%one.bit, label %%unit, label %%align\n"
      "unit:\n  ret i64 1\n"
      "align:\n"
      "  %%dist = zext i32 %%delta to i64\n"
      "  %%aligned.d = shl i64 %%denominator, %%dist\n"
      "  br label %%loop\n"
      "loop:\n"
      "  %%d = phi i64 [ %%aligned.d, %%align ], [ %%next.d, %%loop ]\n"
      "  %%r = phi i64 [ %%numerator, %%align ], [ %%next.r, %%loop ]\n"
      "  %%q = phi i64 [ 0, %%align ], [ %%next.q, %%loop ]\n"
      "  %%count = phi i32 [ %%delta, %%align ], [ %%next.count, %%loop ]\n"
      "  %%enough = icmp uge i64 %%r, %%d\n"
      "  %%difference = sub i64 %%r, %%d\n"
      "  %%next.r = select i1 %%enough, i64 %%difference, i64 %%r\n"
      "  %%shift.q = shl i64 %%q, 1\n"
      "  %%qbit = zext i1 %%enough to i64\n"
      "  %%next.q = or i64 %%shift.q, %%qbit\n"
      "  %%next.d = lshr i64 %%d, 1\n"
      "  %%next.count = sub i32 %%count, 1\n"
      "  %%done = icmp eq i32 %%count, 0\n"
      "  br i1 %%done, label %%exit, label %%loop\n"
      "exit:\n  ret i64 %%next.q\n}\n",
      udiv, narrow_udiv);
  }
  if (sdiv) {
    ir_note(c, "generated", "signed_division_helper", 1);
    ir_emit(c,
            "define internal i64 %s(i64 %%numerator, i64 %%denominator) {\n"
            "entry:\n"
            "  %%neg.n = icmp slt i64 %%numerator, 0\n"
            "  %%neg.d = icmp slt i64 %%denominator, 0\n"
            "  %%minus.n = sub i64 0, %%numerator\n"
            "  %%minus.d = sub i64 0, %%denominator\n"
            "  %%abs.n = select i1 %%neg.n, i64 %%minus.n, i64 %%numerator\n"
            "  %%abs.d = select i1 %%neg.d, i64 %%minus.d, i64 %%denominator\n"
            "  %%q = call i64 %s(i64 %%abs.n, i64 %%abs.d)\n"
            "  %%minus.q = sub i64 0, %%q\n"
            "  %%negative = xor i1 %%neg.n, %%neg.d\n"
            "  %%result = select i1 %%negative, i64 %%minus.q, i64 %%q\n"
            "  ret i64 %%result\n}\n",
            sdiv, udiv);
  }
  m->suffix = ir_copy(c, c->output + start, c->size - start);
  c->size = start;
}
