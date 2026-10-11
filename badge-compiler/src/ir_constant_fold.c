#include "ir_integer.h"
#include "ir_pass.h"
#include <inttypes.h>
#include <string.h>

uint64_t ir_integer_mask(unsigned bits) {
  return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}
uint64_t ir_integer_extend(uint64_t n, unsigned bits) {
  return n & (UINT64_C(1) << (bits - 1)) ? n | ~ir_integer_mask(bits) : n;
}
int ir_integer_binary(const char *op, unsigned bits, uint64_t a, uint64_t b,
                      uint64_t *result) {
  a &= ir_integer_mask(bits);
  b &= ir_integer_mask(bits);
  if (!strcmp(op, "add"))
    *result = a + b;
  else if (!strcmp(op, "sub"))
    *result = a - b;
  else if (!strcmp(op, "mul"))
    *result = a * b;
  else if (!strcmp(op, "and"))
    *result = a & b;
  else if (!strcmp(op, "or"))
    *result = a | b;
  else if (!strcmp(op, "xor"))
    *result = a ^ b;
  else if (!strcmp(op, "shl")) {
    if (b >= bits)
      return 0;
    *result = a << b;
  } else if (!strcmp(op, "lshr")) {
    if (b >= bits)
      return 0;
    *result = a >> b;
  } else if (!strcmp(op, "ashr")) {
    if (b >= bits)
      return 0;
    *result = a >> b;
    if (b && (a & (UINT64_C(1) << (bits - 1))))
      *result |= ir_integer_mask(bits) ^ ir_integer_mask(bits - (unsigned)b);
  } else if (!strcmp(op, "udiv") || !strcmp(op, "urem")) {
    if (!b)
      return 0;
    *result = !strcmp(op, "udiv") ? a / b : a % b;
  } else if (!strcmp(op, "sdiv") || !strcmp(op, "srem")) {
    int neg_a = (a >> (bits - 1)) != 0, neg_b = (b >> (bits - 1)) != 0;
    uint64_t aa = neg_a ? (-a) & ir_integer_mask(bits) : a,
             bb = neg_b ? (-b) & ir_integer_mask(bits) : b;
    if (!bb || (a == (UINT64_C(1) << (bits - 1)) && b == ir_integer_mask(bits)))
      return 0;
    int rem = !strcmp(op, "srem");
    uint64_t n = rem ? aa % bb : aa / bb;
    *result = (rem ? neg_a : neg_a != neg_b) ? -n : n;
  } else
    return 0;
  *result &= ir_integer_mask(bits);
  return 1;
}
int ir_integer_compare(const char *pred, unsigned bits, uint64_t a, uint64_t b,
                       uint64_t *result) {
  a &= ir_integer_mask(bits);
  b &= ir_integer_mask(bits);
  if (!strcmp(pred, "eq"))
    *result = a == b;
  else if (!strcmp(pred, "ne"))
    *result = a != b;
  else {
    if (strlen(pred) != 3 || (pred[0] != 's' && pred[0] != 'u'))
      return 0;
    if (pred[0] == 's') {
      a ^= UINT64_C(1) << (bits - 1);
      b ^= UINT64_C(1) << (bits - 1);
    }
    if (!strcmp(pred + 1, "lt"))
      *result = a < b;
    else if (!strcmp(pred + 1, "le"))
      *result = a <= b;
    else if (!strcmp(pred + 1, "gt"))
      *result = a > b;
    else if (!strcmp(pred + 1, "ge"))
      *result = a >= b;
    else
      return 0;
  }
  return 1;
}
void ir_constant_fold(ir_context *c, ir_module *m) {
  ir_map known = {0};
  ir_map_init(c, &known, m->count);
  for (unsigned l = 0; l < m->line_count; l++) {
    ir_line *line = &m->lines[l];
    c->line = l + 1;
    if (!strcmp(ir_text(m, line->first), "define"))
      ir_map_clear(&known);
    if (!line->instruction)
      continue;
    unsigned start = line->op, end = line->first + line->count;
    for (unsigned j = start + 1; j < end; j++) {
      const char *text = ir_text(m, j);
      if (*text != '%')
        continue;
      const char *literal = ir_map_get(&known, text);
      if (literal) {
        m->tokens[j].replacement = literal;
        ir_note(c, "propagated", "SSA_literal_operand", 1);
      }
    }
    if (start != line->first + 2)
      continue;
    const char *op = ir_text(m, start);
    static const char *const supported[] = {
        "add",  "sub",  "mul",  "and",   "or",   "xor",
        "shl",  "lshr", "ashr", "udiv",  "urem", "sdiv",
        "srem", "sext", "zext", "trunc", "icmp", "freeze"};
    int candidate = 0;
    if (c->observe)
      for (unsigned k = 0; k < sizeof(supported) / sizeof(*supported); k++)
        if (!strcmp(op, supported[k]))
          candidate = 1;
    if (candidate)
      ir_note(c, "candidate", "scalar_expression", 1);
    unsigned bits = ir_bits(ir_text(m, start + 1)), out_bits = bits;
    uint64_t a, b, result = 0;
    int folded = 0;
    if (bits && line->count == 7 && ir_integer(ir_text(m, start + 2), &a) &&
        !strcmp(ir_text(m, start + 3), ",") &&
        ir_integer(ir_text(m, start + 4), &b))
      folded = ir_integer_binary(op, bits, a, b, &result);
    else if (bits && line->count == 7 &&
             ir_integer(ir_text(m, start + 2), &a) &&
             !strcmp(ir_text(m, start + 3), "to") &&
             (out_bits = ir_bits(ir_text(m, start + 4))) != 0) {
      a &= ir_integer_mask(bits);
      if (!strcmp(op, "sext") && out_bits > bits) {
        result = ir_integer_extend(a, bits) & ir_integer_mask(out_bits);
        folded = 1;
      } else if (!strcmp(op, "zext") && out_bits > bits) {
        result = a;
        folded = 1;
      } else if (!strcmp(op, "trunc") && out_bits < bits) {
        result = a & ir_integer_mask(out_bits);
        folded = 1;
      }
    } else if (!strcmp(op, "icmp") && line->count == 8 &&
               (bits = ir_bits(ir_text(m, start + 2))) &&
               ir_integer(ir_text(m, start + 3), &a) &&
               !strcmp(ir_text(m, start + 4), ",") &&
               ir_integer(ir_text(m, start + 5), &b)) {
      out_bits = 1;
      folded = ir_integer_compare(ir_text(m, start + 1), bits, a, b, &result);
    } else if (!strcmp(op, "freeze") && bits && line->count == 5 &&
               ir_integer(ir_text(m, start + 2), &result)) {
      result &= ir_integer_mask(bits);
      folded = 1;
    }
    if (!folded) {
      if (candidate) {
        const char *flag = ir_text(m, start + 1);
        ir_note(c, "skipped",
                !strcmp(flag, "nsw") || !strcmp(flag, "nuw") ||
                        !strcmp(flag, "exact")
                    ? "flags_preserved"
                : bits && line->count == 7 &&
                        ir_integer(ir_text(m, start + 2), &a) &&
                        !strcmp(ir_text(m, start + 3), ",") &&
                        ir_integer(ir_text(m, start + 4), &b)
                    ? "undefined_or_unsupported_constant_operation"
                    : "nonconstant_or_unsupported_shape",
                1);
      }
      continue;
    }
    ir_note(c, "transformed", "folded_or_normalized_definition", 1);
    char *literal = ir_format(c, "%" PRIu64, result);
    const char *name = ir_text(m, line->first);
    if (!ir_map_get(&known, name))
      ir_map_put(c, &known, name, literal);
    line->replacement =
        ir_format(c, "  %s = freeze i%u %s\n", name, out_bits, literal);
  }
}
