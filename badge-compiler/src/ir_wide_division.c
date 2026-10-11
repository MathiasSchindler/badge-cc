#include "ir_pass.h"
#include "target.h"
#include <string.h>

void ir_wide_division(ir_context *c, ir_module *m) {
  char *udiv = NULL, *sdiv = NULL;
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
    if (!rem && !strcmp(ir_text(m, type), "exact"))
      type++;
    if (strcmp(ir_text(m, type), "i64")) {
      ir_note(c, "skipped", "backend_supported_width", 1);
      continue;
    }
    if (line->op != line->first + 2 || end - type != 4 ||
        strcmp(ir_text(m, type + 2), ","))
      ir_error(c, "unsupported wide division operands/flags");
    const char *a = ir_text(m, type + 1), *b = ir_text(m, type + 3);
    uint64_t divisor;
    if (sign && !rem && ir_integer(b, &divisor) &&
        badge_direct_i64_sdiv(divisor)) {
      ir_note(c, "skipped", "direct_i64_power_of_two", 1);
      continue;
    }
    if ((*a != '%' && !ir_integer(a, &divisor)) ||
        (*b != '%' && !ir_integer(b, &divisor)))
      ir_error(c, "wide division requires scalar integer operands");
    ir_note(c, "transformed", rem ? "remainder_lowered" : "division_lowered",
            1);
    if (!udiv)
      udiv = ir_fresh(c, m, '@');
    if (sign && !sdiv)
      sdiv = ir_fresh(c, m, '@');
    const char *helper = sign ? sdiv : udiv, *result = ir_text(m, line->first);
    if (!rem)
      line->replacement = ir_format(c, "  %s = call i64 %s(i64 %s, i64 %s)\n",
                                    result, helper, a, b);
    else {
      char *quotient = ir_fresh(c, m, '%'), *product = ir_fresh(c, m, '%');
      line->replacement = ir_format(c,
                                    "  %s = call i64 %s(i64 %s, i64 %s)\n  %s "
                                    "= mul i64 %s, %s\n  %s = sub i64 %s, %s\n",
                                    quotient, helper, a, b, product, quotient,
                                    b, result, a, product);
    }
  }
  if (!udiv)
    return;
  ir_note(c, "generated", "unsigned_division_helper", 1);
  if (sdiv)
    ir_note(c, "generated", "signed_division_helper", 1);
  size_t start = c->size;
  ir_emit(
      c,
      "define internal i64 %s(i64 %%numerator, i64 %%denominator) {\n"
      "entry:\n  %%zero = icmp eq i64 %%denominator, 0\n"
      "  br i1 %%zero, label %%invalid, label %%loop\n"
      "invalid:\n  unreachable\n"
      "loop:\n"
      "  %%n = phi i64 [ %%numerator, %%entry ], [ %%next.n, %%loop ]\n"
      "  %%r = phi i64 [ 0, %%entry ], [ %%next.r, %%loop ]\n"
      "  %%q = phi i64 [ 0, %%entry ], [ %%next.q, %%loop ]\n"
      "  %%count = phi i32 [ 64, %%entry ], [ %%next.count, %%loop ]\n"
      "  %%bit = lshr i64 %%n, 63\n"
      "  %%shift.r = shl i64 %%r, 1\n"
      "  %%candidate = or i64 %%shift.r, %%bit\n"
      "  %%overflow = icmp slt i64 %%r, 0\n"
      "  %%enough = icmp uge i64 %%candidate, %%denominator\n"
      "  %%subtract = or i1 %%overflow, %%enough\n"
      "  %%difference = sub i64 %%candidate, %%denominator\n"
      "  %%next.r = select i1 %%subtract, i64 %%difference, i64 %%candidate\n"
      "  %%shift.q = shl i64 %%q, 1\n"
      "  %%qbit = zext i1 %%subtract to i64\n"
      "  %%next.q = or i64 %%shift.q, %%qbit\n"
      "  %%next.n = shl i64 %%n, 1\n"
      "  %%next.count = sub i32 %%count, 1\n"
      "  %%done = icmp eq i32 %%next.count, 0\n"
      "  br i1 %%done, label %%exit, label %%loop\n"
      "exit:\n  ret i64 %%next.q\n}\n",
      udiv);
  if (sdiv)
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
  m->suffix = ir_copy(c, c->output + start, c->size - start);
  c->size = start;
}
