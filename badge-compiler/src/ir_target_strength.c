#include "ir_analysis.h"
#include "ir_integer.h"
#include "ir_loop_admission.h"
#include "target_cost.h"
#include "thumb_immediate.h"
#include <inttypes.h>
#include <string.h>

#ifndef IR_TARGET_STRENGTH_WORK_LIMIT
#define IR_TARGET_STRENGTH_WORK_LIMIT UINT64_C(50000000)
#endif

typedef struct {
  ir_context *c;
  ir_analysis a;
  uint64_t work;
} strength;

static void spend(strength *p) {
  if (!p->work--)
    ir_error(p->c, "target-strength-reduce work limit exceeded");
}
static const char *plain_divrem(strength *p, unsigned i, uint32_t *divisor) {
  spend(p);
  const ir_instruction *in = &p->a.instructions[i];
  const ir_line *line = &p->a.text->lines[in->line - 1];
  if (p->a.types[in->type].kind != IR_INT ||
      p->a.types[in->type].bits != 32)
    return "unsupported_width";
  if (in->end_line != in->line || line->count != 7 ||
      line->op != line->first + 2)
    return "flags_or_metadata_preserved";
  const ir_operand *left = &p->a.operands[in->first_operand], *right = left + 1;
  uint64_t n;
  if (left->kind != IR_OPERAND_LOCAL ||
      right->kind != IR_OPERAND_LITERAL ||
      !ir_integer(ir_text(p->a.text, right->token), &n))
    return "requires_local_dividend_and_literal_divisor";
  *divisor = (uint32_t)n;
  return NULL;
}
static const char *eligible(strength *p, unsigned i, uint32_t *divisor) {
  const char *reason = plain_divrem(p, i, divisor);
  if (reason)
    return reason;
  if (!*divisor || (*divisor & (*divisor - 1)))
    return "not_nonzero_power_of_two";
  return NULL;
}

static unsigned trailing_bits(strength *p, const ir_operand *value, unsigned depth) {
  const ir_analysis *a = &p->a;
  uint64_t bits;
  spend(p);
  if (value->kind == IR_OPERAND_LITERAL && ir_integer(ir_text(a->text, value->token), &bits))
    return (uint32_t)bits ? (unsigned)__builtin_ctz((uint32_t)bits) : 32;
  if (value->kind != IR_OPERAND_LOCAL || depth == 3)
    return 0;
  unsigned def = a->definitions[value->definition].instruction;
  if (def == IR_NONE) return 0;
  const ir_instruction *in = &a->instructions[def];
  if (in->opcode == IR_OP_BINARY && in->operand_count == 2 &&
      a->types[in->type].kind == IR_INT && a->types[in->type].bits == 32) {
    const ir_operand *x = &a->operands[in->first_operand], *y = x + 1;
    if (!strcmp(in->operation, "mul")) {
      unsigned sum = trailing_bits(p, x, depth + 1) + trailing_bits(p, y, depth + 1);
      return sum > 32 ? 32 : sum;
    }
    if (!strcmp(in->operation, "shl") && y->kind == IR_OPERAND_LITERAL &&
        ir_integer(ir_text(a->text, y->token), &bits) && bits < 32)
      return (unsigned)bits;
  }
  if (in->opcode != IR_OP_CALL || in->callee == IR_NONE ||
      a->operands[in->callee].kind != IR_OPERAND_GLOBAL)
    return 0;
  unsigned target = a->globals[a->operands[in->callee].definition].function;
  if (target == IR_NONE || a->functions[target].declaration)
    return 0;
  const ir_function *fn = &a->functions[target];
  const ir_line *line = &a->text->lines[fn->line - 1];
  if (fn->block_count > 4 ||
      (strcmp(ir_text(a->text, line->first + 1), "internal") &&
       strcmp(ir_text(a->text, line->first + 1), "private")))
    return 0;
  unsigned minimum = 32, returns = 0;
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++) {
    spend(p);
    const ir_block *block = &a->blocks[b];
    if (!block->instruction_count) return 0;
    const ir_instruction *ret = &a->instructions[block->first_instruction + block->instruction_count - 1];
    if (ret->opcode != IR_OP_RET || ret->operand_count != 1)
      continue;
    unsigned width = trailing_bits(p, &a->operands[ret->first_operand], depth + 1);
    if (width < minimum) minimum = width;
    returns++;
  }
  return returns ? minimum : 0;
}

static int exact_pow2(strength *p) {
  ir_module *m = p->a.text;
  int changed = 0;
  for (unsigned i = 0; i < p->a.instruction_count; i++) {
    const ir_instruction *in = &p->a.instructions[i];
    spend(p);
    if (in->opcode != IR_OP_BINARY || strcmp(in->operation, "sdiv"))
      continue;
    uint32_t divisor;
    if (eligible(p, i, &divisor) || divisor < 2 || divisor > INT32_MAX)
      continue;
    unsigned shift = (unsigned)__builtin_ctz(divisor);
    if (trailing_bits(p, &p->a.operands[in->first_operand], 0) < shift)
      continue;
    m->tokens[m->lines[in->line - 1].op].replacement = "ashr";
    m->tokens[p->a.operands[in->first_operand + 1].token].replacement =
        ir_format(p->c, "%u", shift);
    ir_note(p->c, "transformed", "signed_dividend_known_multiple", 1);
    changed = 1;
  }
  return changed;
}

void ir_exact_pow2(ir_context *c, ir_module *m) {
  strength p = {.c = c, .work = IR_TARGET_STRENGTH_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  exact_pow2(&p);
}

static void reduce_unsigned(strength *p, int gated);

static int plain_signed_pair_operand(strength *p, const ir_instruction *in) {
  const ir_line *line = &p->a.text->lines[in->line - 1];
  return in->opcode == IR_OP_BINARY && in->operand_count == 2 &&
         p->a.types[in->type].kind == IR_INT && p->a.types[in->type].bits == 32 &&
         in->end_line == in->line && line->count == 7 &&
         line->op == line->first + 2 && !line->replacement &&
         p->a.operands[in->first_operand].kind == IR_OPERAND_LOCAL &&
         p->a.operands[in->first_operand + 1].kind == IR_OPERAND_LOCAL;
}

static ir_line *duplicate_extension(strength *p, const ir_operand *first,
                                    const ir_operand *second, unsigned after) {
  const ir_definition *a = &p->a.definitions[first->definition];
  const ir_definition *b = &p->a.definitions[second->definition];
  if (a->instruction == IR_NONE || b->instruction == IR_NONE || b->use_count != 1)
    return NULL;
  const ir_instruction *x = &p->a.instructions[a->instruction];
  const ir_instruction *y = &p->a.instructions[b->instruction];
  const ir_line *prior = &p->a.text->lines[x->line - 1];
  ir_line *line = &p->a.text->lines[y->line - 1];
  if (x->opcode != IR_OP_CAST || y->opcode != IR_OP_CAST ||
      x->operand_count != 1 || y->operand_count != 1 ||
      (strcmp(x->operation, "zext") && strcmp(x->operation, "sext")) ||
      strcmp(x->operation, y->operation) || b->instruction <= after ||
      x->end_line != x->line || prior->count != 7 || prior->replacement ||
      y->end_line != y->line || line->count != 7 || line->replacement)
    return NULL;
  const ir_operand *src = &p->a.operands[x->first_operand];
  const ir_operand *other = &p->a.operands[y->first_operand];
  return src->kind == IR_OPERAND_LOCAL && other->kind == IR_OPERAND_LOCAL &&
         src->definition == other->definition && src->type == other->type ? line : NULL;
}

static void pair_signed_divrem(strength *p, unsigned i) {
  enum { LOOKAHEAD = 6 };
  ir_module *m = p->a.text;
  const ir_instruction *first = &p->a.instructions[i];
  if (!plain_signed_pair_operand(p, first))
    return;
  int remainder_first = !strcmp(first->operation, "srem");
  const ir_operand *x = &p->a.operands[first->first_operand], *y = x + 1;
  for (unsigned j = i + 1; j <= i + LOOKAHEAD && j < p->a.instruction_count; ++j) {
    spend(p);
    const ir_instruction *second = &p->a.instructions[j];
    if (second->block != first->block)
      break;
    if (plain_signed_pair_operand(p, second) &&
        !strcmp(second->operation, remainder_first ? "sdiv" : "srem")) {
      const ir_operand *other = &p->a.operands[second->first_operand];
      if (y->definition != other[1].definition)
        break;
      ir_line *duplicate = NULL;
      if (x->definition != other->definition) {
        duplicate = duplicate_extension(p, x, other, i);
        if (!duplicate)
          break;
      } else if (j == i + 1)
        break;
      const char *a = p->a.definitions[first->definition].name;
      const char *b = p->a.definitions[second->definition].name;
      const char *dividend = ir_text(m, x->token), *divisor = ir_text(m, y->token);
      m->lines[first->line - 1].replacement = ir_format(
          p->c, "  %s = sdiv i32 %s, %s\n  %s = srem i32 %s, %s\n",
          remainder_first ? b : a, dividend, divisor, remainder_first ? a : b,
          dividend, divisor);
      m->lines[second->line - 1].replacement = "";
      if (duplicate)
        duplicate->replacement = "";
      ir_note(p->c, "candidate", "adjacent_signed_divrem", 1);
      break;
    }
    if (second->effects)
      break;
  }
}

void ir_divrem_pairs(ir_context *c, ir_module *m) {
  enum { LOOKAHEAD = 6 };
  ir_allocation *mark = c->allocations;
  strength p = {.c = c, .work = IR_TARGET_STRENGTH_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  for (unsigned i = 0; i < p.a.instruction_count; i++) {
    spend(&p);
    const ir_instruction *rem = &p.a.instructions[i];
    if (rem->opcode == IR_OP_BINARY &&
        (!strcmp(rem->operation, "srem") || !strcmp(rem->operation, "sdiv"))) {
      pair_signed_divrem(&p, i);
      continue;
    }
    if (rem->opcode != IR_OP_BINARY || strcmp(rem->operation, "urem"))
      continue;
    c->line = rem->line;
    uint32_t divisor;
    if (plain_divrem(&p, i, &divisor) || !divisor)
      continue;
    ir_line *first = &m->lines[rem->line - 1];
    if (first->replacement)
      continue;
    for (unsigned j = i + 1; j <= i + LOOKAHEAD && j < p.a.instruction_count; j++) {
      spend(&p);
      const ir_instruction *div = &p.a.instructions[j];
      if (div->block != rem->block ||
          (div->effects & (IR_EFFECT_CONTROL | IR_EFFECT_CALL | IR_EFFECT_OPAQUE | IR_EFFECT_VOLATILE)))
        break;
      if (div->opcode != IR_OP_BINARY || strcmp(div->operation, "udiv"))
        continue;
      uint32_t other;
      ir_line *second = &m->lines[div->line - 1];
      if (plain_divrem(&p, j, &other) || other != divisor || second->replacement ||
          p.a.operands[rem->first_operand].definition != p.a.operands[div->first_operand].definition)
        continue;
      if (j != i + 1) {
        first->replacement = ir_format(c, "%.*s%.*s", (int)(first->end - first->begin),
            m->source + first->begin, (int)(second->end - second->begin), m->source + second->begin);
        second->replacement = "";
        ir_note(c, "candidate", "adjacent_unsigned_divrem", 1);
      }
      break;
    }
  }
  ir_function_admission(c, m, &p.a, BADGE_SELECTION_THUMB_CONSTANTS);
  ir_reparse(c, m, mark);
  ir_analyze(c, m, &p.a);
  /* Compare the unsigned rewrites against the already-optimized signed form. */
  if (exact_pow2(&p)) {
    ir_reparse(c, m, mark);
    ir_analyze(c, m, &p.a);
  }
  reduce_unsigned(&p, 1);
  ir_function_admission(c, m, &p.a, BADGE_SELECTION_THUMB_CONSTANTS);
  c->size = 0;
}
static badge_cost_estimate cost(badge_cost_kind kind) {
  return badge_target_cost((badge_cost_request){
      .kind = kind, .encoding_bytes = kind == BADGE_COST_MOV_PAIR ? 8 : 4});
}
static badge_cost_estimate materialization(uint32_t n) {
  return cost(n <= 65535 ? BADGE_COST_ALU : BADGE_COST_MOV_PAIR);
}
static int profitable(strength *p, uint32_t divisor, int rem, int paired) {
  badge_cost_estimate divide = cost(BADGE_COST_DIVIDE);
  badge_cost_estimate alu = cost(BADGE_COST_ALU);
  badge_cost_estimate mac = cost(BADGE_COST_MAC);
  badge_cost_estimate old_constant = materialization(divisor);
  unsigned old_bytes = divide.code_bytes.max + old_constant.code_bytes.max;
  unsigned old_issue = divide.baseline_issue.min +
                       old_constant.baseline_issue.min;
  unsigned new_bytes = alu.code_bytes.max, new_issue = alu.baseline_issue.max;
  if (rem || paired) {
    old_bytes += mac.code_bytes.max;
    old_issue += mac.baseline_issue.min;
    uint32_t mask = divisor - 1;
    if (thumb_modified(mask) < 0) {
      badge_cost_estimate constant = materialization(mask);
      if (!constant.code_bytes.known || !constant.baseline_issue.known)
        return 0;
      new_bytes += constant.code_bytes.max;
      new_issue += constant.baseline_issue.max;
    }
  }
  if (paired) {
    new_bytes += alu.code_bytes.max;
    new_issue += alu.baseline_issue.max;
  }
  /* One SSA result replaces one: no added live value, address work or motion.
     Adjacent div/rem is charged once, including its lost quotient sharing.
     Divide timing remains conditional on nonexceptional dynamic operands. */
  if (!divide.baseline_issue.known || !alu.baseline_issue.known ||
      !mac.baseline_issue.known || !old_constant.baseline_issue.known ||
      new_bytes > old_bytes || new_issue >= old_issue)
    return 0;
  ir_note(p->c, "profitability", "conditional_issue_min_saved",
          old_issue - new_issue);
  ir_note(p->c, "profitability", "lowered_operation_bytes_saved",
          old_bytes - new_bytes);
  if (paired)
    ir_note(p->c, "profitability", "shared_divrem_charged_once", 1);
  return 1;
}
static int unsigned_division(const ir_instruction *in) {
  return in->opcode == IR_OP_BINARY &&
         (!strcmp(in->operation, "udiv") || !strcmp(in->operation, "urem"));
}
static void replace(strength *p, unsigned i, uint32_t divisor, int gated) {
  const ir_instruction *in = &p->a.instructions[i];
  ir_line *line = &p->a.text->lines[in->line - 1];
  int rem = !strcmp(in->operation, "urem");
  unsigned shift = 0;
  if (!rem)
    for (uint32_t n = divisor; n > 1; n >>= 1) {
      spend(p);
      shift++;
    }
  ir_token *op = &p->a.text->tokens[line->op];
  ir_token *value = &p->a.text->tokens[p->a.operands[in->first_operand + 1].token];
  op->replacement = rem ? "and" : "lshr";
  value->replacement =
      ir_format(p->c, "%" PRIu32, rem ? divisor - 1 : shift);
  if (gated) {
    ir_write_line(p->c, p->a.text, line);
    line->replacement = ir_copy(p->c, p->c->output, p->c->size);
    p->c->size = 0;
    op->replacement = value->replacement = NULL;
  }
  ir_note(p->c, gated ? "candidate" : "transformed",
          rem ? "unsigned_power_of_two_remainder"
              : "unsigned_power_of_two_division",
          1);
}
static void reduce_unsigned(strength *p, int gated) {
  ir_context *c = p->c;
  const ir_analysis *a = &p->a;
  for (unsigned i = 0; i < a->instruction_count; i++) {
    spend(p);
    const ir_instruction *in = &a->instructions[i];
    if (!unsigned_division(in))
      continue;
    c->line = in->line;
    ir_note(c, "candidate", "unsigned_division_or_remainder", 1);
    uint32_t divisor;
    const char *reason = eligible(p, i, &divisor);
    if (reason) {
      ir_note(c, "skipped", reason, 1);
      continue;
    }
    int paired = 0;
    if (i + 1 < a->instruction_count) {
      const ir_instruction *next = &a->instructions[i + 1];
      uint32_t other;
      if (next->block == in->block && unsigned_division(next) &&
          strcmp(next->operation, in->operation) &&
          !eligible(p, i + 1, &other) && other == divisor &&
          a->operands[next->first_operand].definition ==
              a->operands[in->first_operand].definition)
        paired = 1;
    }
    if (!profitable(p, divisor, !strcmp(in->operation, "urem"), paired)) {
      ir_note(c, "skipped", "lowered_cost_not_profitable", 1);
      continue;
    }
    replace(p, i, divisor, gated);
    if (paired) {
      ir_note(c, "candidate", "unsigned_division_or_remainder", 1);
      replace(p, ++i, divisor, gated);
    }
  }
}

void ir_target_strength_reduce(ir_context *c, ir_module *m) {
  strength p = {.c = c, .work = IR_TARGET_STRENGTH_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  reduce_unsigned(&p, 0);
}

static int uint_literal(const ir_analysis *a, const ir_operand *op,
                        uint32_t *value) {
  uint64_t n;
  if (op->kind != IR_OPERAND_LITERAL ||
      !ir_integer(ir_text(a->text, op->token), &n))
    return 0;
  *value = (uint32_t)n;
  return 1;
}

static uint32_t edge_upper_bound(strength *p, const ir_operand *value,
                                 unsigned predecessor, unsigned target) {
  const ir_analysis *a = &p->a;
  const ir_block *b = &a->blocks[predecessor];
  if (!b->instruction_count || b->successor_count != 2 ||
      value->kind != IR_OPERAND_LOCAL)
    return UINT32_MAX;
  const ir_instruction *branch = &a->instructions[b->first_instruction + b->instruction_count - 1];
  if (branch->opcode != IR_OP_BR || branch->operand_count != 3)
    return UINT32_MAX;
  const ir_operand *cond = &a->operands[branch->first_operand];
  const ir_operand *yes = cond + 1, *no = cond + 2;
  if (yes->block == no->block || (yes->block != target && no->block != target) ||
      cond->kind != IR_OPERAND_LOCAL)
    return UINT32_MAX;
  unsigned def = a->definitions[cond->definition].instruction;
  if (def == IR_NONE)
    return UINT32_MAX;
  const ir_instruction *cmp = &a->instructions[def];
  if (cmp->opcode != IR_OP_COMPARE || cmp->operand_count != 2)
    return UINT32_MAX;
  const ir_operand *lhs = &a->operands[cmp->first_operand], *rhs = lhs + 1;
  uint32_t limit;
  if (lhs->kind != IR_OPERAND_LOCAL || lhs->definition != value->definition ||
      !uint_literal(a, rhs, &limit))
    return UINT32_MAX;
  const char *pred = ir_text(a->text, a->text->lines[cmp->line - 1].op + 1);
  if (yes->block == target)
    return !strcmp(pred, "ule") ? limit :
           !strcmp(pred, "ult") && limit ? limit - 1 : UINT32_MAX;
  return !strcmp(pred, "ugt") ? limit :
         !strcmp(pred, "uge") && limit ? limit - 1 : UINT32_MAX;
}

static uint32_t upper_bound(strength *p, const ir_operand *value, unsigned depth) {
  const ir_analysis *a = &p->a;
  uint32_t n;
  spend(p);
  if (uint_literal(a, value, &n))
    return n;
  if (!depth || value->kind != IR_OPERAND_LOCAL)
    return UINT32_MAX;
  unsigned def = a->definitions[value->definition].instruction;
  if (def == IR_NONE)
    return UINT32_MAX;
  const ir_instruction *in = &a->instructions[def];
  if (in->opcode == IR_OP_CAST && !strcmp(in->operation, "zext")) {
    unsigned bits = a->types[a->operands[in->first_operand].type].bits;
    if (bits && bits < 32)
      return (UINT32_C(1) << bits) - 1;
  }
  if (in->opcode == IR_OP_BINARY && !strcmp(in->operation, "and") &&
      uint_literal(a, &a->operands[in->first_operand + 1], &n))
    return n;
  if (in->opcode != IR_OP_PHI || in->operand_count > 8)
    return UINT32_MAX;
  uint32_t max = 0;
  int seeded = 0;
  for (unsigned k = 0; k < in->operand_count; k += 2) {
    const ir_operand *input = &a->operands[in->first_operand + k], *label = input + 1;
    spend(p);
    if (input->kind == IR_OPERAND_LOCAL) {
      unsigned other = a->definitions[input->definition].instruction;
      if (other != IR_NONE) {
        const ir_instruction *next = &a->instructions[other];
        const ir_operand *x = &a->operands[next->first_operand];
        if (next->opcode == IR_OP_BINARY && !strcmp(next->operation, "udiv") &&
            x->kind == IR_OPERAND_LOCAL && x->definition == value->definition &&
            uint_literal(a, x + 1, &n) && n)
          continue;
      }
    }
    seeded = 1;
    uint32_t guarded = edge_upper_bound(p, input, label->block, in->block);
    uint32_t limit = upper_bound(p, input, depth - 1);
    if (guarded < limit)
      limit = guarded;
    if (limit > max)
      max = limit;
  }
  return seeded ? max : UINT32_MAX;
}

void ir_bounded_decimal(ir_context *c, ir_module *m) {
  strength p = {.c = c, .work = IR_TARGET_STRENGTH_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  for (unsigned i = 0; i + 1 < p.a.instruction_count; i++) {
    spend(&p);
    const ir_instruction *first = &p.a.instructions[i], *second = first + 1;
    if (first->block != second->block || !unsigned_division(first) ||
        !unsigned_division(second) || !strcmp(first->operation, second->operation))
      continue;
    uint32_t divisora, divisorb;
    if (plain_divrem(&p, i, &divisora) || divisora != 10 ||
        plain_divrem(&p, i + 1, &divisorb) || divisorb != 10)
      continue;
    const ir_operand *a = &p.a.operands[first->first_operand];
    const ir_operand *b = &p.a.operands[second->first_operand];
    if (a->definition != b->definition || upper_bound(&p, a, 5) > 99999)
      continue;
    const char *input = p.a.definitions[a->definition].name;
    const ir_instruction *div = !strcmp(first->operation, "udiv") ? first : second;
    const ir_instruction *rem = div == first ? second : first;
    const char *q = p.a.definitions[div->definition].name;
    const char *r = p.a.definitions[rem->definition].name;
    const char *frozen = ir_fresh(c, m, '%');
    const char *half = ir_fresh(c, m, '%'), *mul = ir_fresh(c, m, '%');
    const char *product = ir_fresh(c, m, '%');
    /* floor(x/10) = floor(floor(x/2) * ceil(2^18/5) / 2^18)
       on this domain; the product is below 2^32. */
    m->lines[first->line - 1].replacement = ir_format(c,
        "  %s = freeze i32 %s\n  %s = lshr i32 %s, 1\n  %s = mul i32 %s, 52429\n"
        "  %s = lshr i32 %s, 18\n  %s = mul i32 %s, 10\n"
        "  %s = sub i32 %s, %s\n", frozen, input, half, frozen,
        mul, half, q, mul, product, q, r, frozen, product);
    m->lines[second->line - 1].replacement = "";
    ir_note(c, "transformed", "bounded_decimal_divrem", 1);
    i++;
  }
}
