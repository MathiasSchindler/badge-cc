#include "ir_analysis.h"
#include "ir_integer.h"
#include "target.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const ir_instruction *definition(const ir_analysis *a, const ir_operand *o) {
  if (o->kind != IR_OPERAND_LOCAL)
    return NULL;
  unsigned i = a->definitions[o->definition].instruction;
  return i == IR_NONE ? NULL : &a->instructions[i];
}
static const ir_operand *inputs(const ir_analysis *a, const ir_instruction *i) {
  return &a->operands[i->first_operand];
}
static int plain(const ir_analysis *a, const ir_instruction *i,
                 const char *op, unsigned bits) {
  return i && !strcmp(i->operation, op) && a->types[i->type].bits == bits &&
         i->end_line == i->line && !a->text->lines[i->line - 1].replacement &&
         a->text->lines[i->line - 1].count == 7;
}
static int literal(const ir_analysis *a, const ir_operand *o, uint64_t *value) {
  return o->kind == IR_OPERAND_LITERAL && ir_integer(ir_text(a->text, o->token), value);
}
static int is_value(const ir_operand *o, const ir_instruction *i) {
  return o->kind == IR_OPERAND_LOCAL && o->definition == i->definition;
}
static const ir_instruction *terminator(const ir_analysis *a, unsigned b) {
  const ir_block *block = &a->blocks[b];
  return &a->instructions[block->first_instruction + block->instruction_count - 1];
}
static const ir_operand *incoming(const ir_analysis *a, const ir_instruction *phi,
                                  unsigned block) {
  if (!phi || phi->opcode != IR_OP_PHI || phi->operand_count != 4)
    return NULL;
  const ir_operand *o = inputs(a, phi);
  return o[1].block == block ? o : o[3].block == block ? o + 2 : NULL;
}

static int table_interval(const ir_analysis *a, const ir_operand *base, unsigned count,
                          int32_t *low, int32_t *high) {
  const ir_global *g = &a->globals[base->definition];
  if (g->storage_type == IR_NONE || a->types[g->storage_type].kind != IR_ARRAY)
    return 0;
  const ir_type *type = &a->types[g->storage_type];
  const ir_line *row = &a->text->lines[g->line - 1];
  unsigned at = row->first;
  if (!count || count > 64 || count * 2 > type->count ||
      a->types[type->element].bits != 8 || row->count < 11 ||
      (strcmp(ir_text(a->text, at + 2), "internal") &&
       strcmp(ir_text(a->text, at + 2), "private")) ||
      strcmp(ir_text(a->text, at + 3), "constant") ||
      strcmp(ir_text(a->text, at + 9), "c"))
    return 0;
  const char *p = ir_text(a->text, at + 10);
  if (*p++ != '"')
    return 0;
  *low = INT16_MAX;
  *high = INT16_MIN;
  for (unsigned n = 0; n < count; n++) {
    unsigned value = 0;
    for (unsigned byte = 0; byte < 2; byte++) {
      if (!*p || *p == '"')
        return 0;
      unsigned v = (unsigned char)*p++;
      if (v == '\\') {
        char hex[3] = {p[0], p[1], 0};
        v = (unsigned)strtoul(hex, NULL, 16);
        p += 2;
      }
      value |= v << (8 * byte);
    }
    int32_t sample = value < 32768 ? (int32_t)value : (int32_t)value - 65536;
    if (sample < *low) *low = sample;
    if (sample > *high) *high = sample;
  }
  return 1;
}

static int recurrence_bounds(const ir_analysis *a, const ir_instruction *delta,
                             const ir_instruction *quotient, uint64_t gain,
                             uint64_t divisor) {
  if (!plain(a, delta, "sub", 32) || gain >= divisor)
    return 0;
  const ir_operand *diff = inputs(a, delta);
  const ir_instruction *state = definition(a, diff + 1);
  const ir_instruction *target = definition(a, diff);
  if (!state || state->opcode != IR_OP_PHI || state->operand_count != 4 ||
      !plain(a, target, "mul", 32))
    return 0;
  uint64_t scale;
  const ir_operand *scaled = inputs(a, target);
  const ir_instruction *ext = definition(a, scaled);
  if (!plain(a, ext, "sext", 32) || a->types[inputs(a, ext)->type].bits != 16 ||
      !literal(a, scaled + 1, &scale) || !scale || scale > INT32_MAX)
    return 0;
  const ir_instruction *load = definition(a, inputs(a, ext));
  if (!load || load->opcode != IR_OP_LOAD || (load->effects & IR_EFFECT_VOLATILE))
    return 0;
  const ir_instruction *gep = definition(a, inputs(a, load));
  if (!gep || gep->opcode != IR_OP_GEP || gep->operand_count != 2 ||
      a->text->lines[gep->line - 1].count != 10 ||
      strcmp(ir_text(a->text, a->text->lines[gep->line - 1].op + 1), "i8"))
    return 0;
  const ir_operand *base = inputs(a, gep);
  const ir_instruction *offset = definition(a, base + 1);
  uint64_t stride;
  if (base->kind != IR_OPERAND_GLOBAL || !plain(a, offset, "mul", 32) ||
      !literal(a, inputs(a, offset) + 1, &stride) || stride != 2)
    return 0;
  const ir_instruction *counter = definition(a, inputs(a, offset));
  unsigned head = state->block;
  const ir_block *header = &a->blocks[head];
  if (!counter || counter->opcode != IR_OP_PHI || counter->block != head ||
      counter->operand_count != 4 || header->predecessor_count != 2)
    return 0;
  unsigned entry = a->predecessors[header->first_predecessor];
  unsigned latch = a->predecessors[header->first_predecessor + 1];
  if (ir_dominates(a, head, entry)) {
    unsigned swap = entry;
    entry = latch;
    latch = swap;
  }
  if (head == latch || ir_dominates(a, head, entry) || !ir_dominates(a, head, latch))
    return 0;
  const ir_operand *seed = incoming(a, counter, entry), *back = incoming(a, counter, latch);
  const ir_operand *init = incoming(a, state, entry), *update = incoming(a, state, latch);
  uint64_t zero, step;
  if (!seed || !back || !init || !update || !literal(a, seed, &zero) || zero)
    return 0;
  const ir_instruction *advance = definition(a, back), *next = definition(a, update);
  if (!plain(a, advance, "add", 32) || !is_value(inputs(a, advance), counter) ||
      !literal(a, inputs(a, advance) + 1, &step) || step != 1 ||
      !plain(a, next, "add", 32))
    return 0;
  const ir_operand *sum = inputs(a, next);
  if (!((is_value(sum, state) && is_value(sum + 1, quotient)) ||
        (is_value(sum + 1, state) && is_value(sum, quotient))))
    return 0;
  const ir_instruction *branch = terminator(a, head);
  const ir_operand *edge = inputs(a, branch);
  const ir_instruction *cmp = branch->operand_count == 3 ? definition(a, edge) : NULL;
  uint64_t count;
  if (branch->opcode != IR_OP_BR || !cmp || cmp->opcode != IR_OP_COMPARE ||
      strcmp(ir_text(a->text, a->text->lines[cmp->line - 1].op + 1), "ult") ||
      !is_value(inputs(a, cmp), counter) || !literal(a, inputs(a, cmp) + 1, &count) ||
      count > 64 || edge[1].block == head || edge[1].block == edge[2].block ||
      a->blocks[edge[1].block].predecessor_count != 1 ||
      !ir_dominates(a, edge[1].block, load->block))
    return 0;
  int32_t min_sample, max_sample;
  if (!table_interval(a, base, (unsigned)count, &min_sample, &max_sample))
    return 0;
  const ir_instruction *initial = definition(a, init);
  uint64_t bias, mask;
  if (!plain(a, initial, "sub", 32) || !literal(a, inputs(a, initial) + 1, &bias) ||
      bias > INT32_MAX)
    return 0;
  const ir_instruction *bounded = definition(a, inputs(a, initial));
  if (!plain(a, bounded, "and", 32) ||
      !literal(a, inputs(a, bounded) + 1, &mask) || mask > INT32_MAX)
    return 0;
  int64_t minimum = min_sample * (int64_t)scale, maximum = max_sample * (int64_t)scale;
  if (minimum < -(int64_t)bias || maximum > (int64_t)mask - (int64_t)bias)
    return 0;
  /* The rounded update stays in the hull of the initial state and target.
     Use division to check the product bound without overflowing the host. */
  uint64_t distance = (uint64_t)(maximum + (int64_t)bias);
  uint64_t reverse = (uint64_t)((int64_t)mask - (int64_t)bias - minimum);
  if (reverse > distance) distance = reverse;
  return !gain || distance <= (INT32_MAX - divisor / 2) / gain;
}

static int narrow_rounded_update(ir_context *c, ir_analysis *a, const ir_instruction *div) {
  uint64_t divisor;
  if (!plain(a, div, "sdiv", 64) || !literal(a, inputs(a, div) + 1, &divisor) ||
      divisor < 2 || divisor > (UINT64_C(1) << 30) || (divisor & (divisor - 1)) ||
      a->functions[a->blocks[div->block].function].block_count > 64 ||
      a->definitions[div->definition].use_count != 1)
    return 0;
  const ir_instruction *quotient = &a->instructions[a->operands[
      a->definitions[div->definition].first_use].instruction];
  const ir_instruction *rounded = definition(a, inputs(a, div));
  if (!plain(a, quotient, "trunc", 32) || !plain(a, rounded, "add", 64) ||
      a->definitions[rounded->definition].use_count != 1)
    return 0;
  const ir_instruction *product = definition(a, inputs(a, rounded));
  const ir_instruction *extend = definition(a, inputs(a, rounded) + 1);
  if (!plain(a, product, "mul", 64) || !plain(a, extend, "sext", 64) ||
      a->definitions[product->definition].use_count != 2 ||
      a->definitions[extend->definition].use_count != 1 ||
      a->types[inputs(a, extend)->type].bits != 32)
    return 0;
  const ir_instruction *bias = definition(a, inputs(a, extend));
  unsigned join = extend->block;
  const ir_block *merge = &a->blocks[join];
  if (!bias || &a->instructions[merge->first_instruction] != bias ||
      bias->opcode != IR_OP_PHI || bias->operand_count != 4 ||
      merge->instruction_count <= 1 || (bias + 1)->opcode == IR_OP_PHI ||
      merge->predecessor_count != 2 || a->definitions[bias->definition].use_count != 1)
    return 0;
  const ir_instruction *sign_branch = terminator(a, product->block);
  const ir_operand *edge = inputs(a, sign_branch);
  const ir_instruction *cmp = sign_branch->operand_count == 3 ? definition(a, edge) : NULL;
  uint64_t zero;
  if (sign_branch->opcode != IR_OP_BR || !cmp || cmp->opcode != IR_OP_COMPARE ||
      a->definitions[cmp->definition].use_count != 1 ||
      !is_value(inputs(a, cmp), product) || !literal(a, inputs(a, cmp) + 1, &zero) || zero)
    return 0;
  const char *pred = ir_text(a->text, a->text->lines[cmp->line - 1].op + 1);
  if (strcmp(pred, "sge") && strcmp(pred, "slt"))
    return 0;
  unsigned positive = edge[!strcmp(pred, "sge") ? 1 : 2].block;
  unsigned negative = edge[!strcmp(pred, "sge") ? 2 : 1].block;
  const ir_operand *plus = incoming(a, bias, positive), *minus = incoming(a, bias, negative);
  uint64_t half, neg;
  if (positive == negative || !plus || !minus ||
      !literal(a, plus, &half) || half != divisor / 2 ||
      !literal(a, minus, &neg) || (uint32_t)neg != 0u - (uint32_t)half)
    return 0;
  const unsigned arms[] = {positive, negative};
  for (unsigned n = 0; n < 2; n++) {
    const ir_block *b = &a->blocks[arms[n]];
    const ir_instruction *jump = terminator(a, arms[n]);
    if (b->predecessor_count != 1 || b->instruction_count != 1 ||
        jump->opcode != IR_OP_BR || jump->operand_count != 1 || inputs(a, jump)->block != join)
      return 0;
  }
  const ir_instruction *signed_delta = definition(a, inputs(a, product));
  const ir_instruction *unsigned_gain = definition(a, inputs(a, product) + 1);
  if (!plain(a, signed_delta, "sext", 64) || !plain(a, unsigned_gain, "zext", 64) ||
      a->types[inputs(a, signed_delta)->type].bits != 32 ||
      a->types[inputs(a, unsigned_gain)->type].bits != 32)
    return 0;
  const ir_instruction *gain = definition(a, inputs(a, unsigned_gain));
  uint64_t max_gain;
  if (!plain(a, gain, "and", 32) || !literal(a, inputs(a, gain) + 1, &max_gain) ||
      !recurrence_bounds(a, definition(a, inputs(a, signed_delta)), quotient, max_gain, divisor))
    return 0;

  ir_module *m = a->text;
  const char *p = a->definitions[product->definition].name;
  m->lines[product->line - 1].replacement = ir_format(c,
      "  %s = mul i32 %s, %s\n", p, ir_text(m, inputs(a, signed_delta)->token),
      ir_text(m, inputs(a, unsigned_gain)->token));
  m->lines[sign_branch->line - 1].replacement = ir_format(c, "  br label %%%s\n", merge->name);
  for (unsigned n = 0; n < 2; n++) {
    m->lines[a->blocks[arms[n]].line - 1].replacement = "";
    m->lines[terminator(a, arms[n])->line - 1].replacement = "";
  }
  const ir_instruction *removed[] = {cmp, bias, extend, rounded, div};
  for (unsigned n = 0; n < sizeof(removed) / sizeof(*removed); n++)
    m->lines[removed[n]->line - 1].replacement = "";
  for (unsigned n = 0; n < 2; n++) {
    const ir_instruction *cast = n ? unsigned_gain : signed_delta;
    if (a->definitions[cast->definition].use_count == 1)
      m->lines[cast->line - 1].replacement = "";
  }
  const char *sign = ir_fresh(c, m, '%'), *adjusted = ir_fresh(c, m, '%');
  const char *sum = ir_fresh(c, m, '%');
  /* For negative p, truncating (p - D/2) / D equals floor((p + D/2 - 1) / D). */
  m->lines[quotient->line - 1].replacement = ir_format(c,
      "  %s = ashr i32 %s, 31\n  %s = add i32 %s, %s\n"
      "  %s = add i32 %s, %" PRIu64 "\n  %s = ashr i32 %s, %u\n",
      sign, p, adjusted, p, sign, sum, adjusted, half,
      a->definitions[quotient->definition].name, sum, (unsigned)__builtin_ctzll(divisor));
  c->line = div->line;
  ir_note(c, "transformed", "bounded_signed_rounded_update", 1);
  return 1;
}

static int little_endian(const ir_module *m) {
  for (unsigned l = 0; l < m->line_count; l++) {
    const ir_line *row = &m->lines[l];
    unsigned at = row->first;
    if (row->count == 4 && !strcmp(ir_text(m, at), "target") &&
        !strcmp(ir_text(m, at + 1), "datalayout"))
      return !strncmp(ir_text(m, at + 3), "\"e-", 3);
  }
  return 0;
}

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
  if (strstr(m->source, "sdiv i64 ") && strstr(m->source, "constant [") && little_endian(m)) {
    ir_allocation *mark = c->allocations;
    ir_analysis a;
    ir_analyze(c, m, &a);
    int changed = 0;
    for (unsigned i = 0; i < a.instruction_count; i++)
      changed |= narrow_rounded_update(c, &a, &a.instructions[i]);
    if (changed)
      ir_reparse(c, m, mark);
    else
      ir_release_to(c, mark);
  }
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
