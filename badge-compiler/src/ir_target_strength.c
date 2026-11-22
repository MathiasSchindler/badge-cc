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

void ir_exact_pow2(ir_context *c, ir_module *m) {
  strength p = {.c = c, .work = IR_TARGET_STRENGTH_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  for (unsigned i = 0; i < p.a.instruction_count; i++) {
    const ir_instruction *in = &p.a.instructions[i];
    spend(&p);
    if (in->opcode != IR_OP_BINARY || strcmp(in->operation, "sdiv"))
      continue;
    uint32_t divisor;
    if (eligible(&p, i, &divisor) || divisor < 2 || divisor > INT32_MAX)
      continue;
    unsigned shift = (unsigned)__builtin_ctz(divisor);
    if (trailing_bits(&p, &p.a.operands[in->first_operand], 0) < shift)
      continue;
    m->tokens[m->lines[in->line - 1].op].replacement = "ashr";
    m->tokens[p.a.operands[in->first_operand + 1].token].replacement = ir_format(c, "%u", shift);
    ir_note(c, "transformed", "signed_dividend_known_multiple", 1);
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
  c->size = 0;
  ir_reparse(c, m, mark);
  ir_exact_pow2(c, m);
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
static void replace(strength *p, unsigned i, uint32_t divisor) {
  const ir_instruction *in = &p->a.instructions[i];
  ir_line *line = &p->a.text->lines[in->line - 1];
  int rem = !strcmp(in->operation, "urem");
  unsigned shift = 0;
  if (!rem)
    for (uint32_t n = divisor; n > 1; n >>= 1) {
      spend(p);
      shift++;
    }
  p->a.text->tokens[line->op].replacement = rem ? "and" : "lshr";
  unsigned token = p->a.operands[in->first_operand + 1].token;
  p->a.text->tokens[token].replacement =
      ir_format(p->c, "%" PRIu32, rem ? divisor - 1 : shift);
  ir_note(p->c, "transformed",
          rem ? "unsigned_power_of_two_remainder"
              : "unsigned_power_of_two_division",
          1);
}
void ir_target_strength_reduce(ir_context *c, ir_module *m) {
  strength p = {.c = c, .work = IR_TARGET_STRENGTH_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  for (unsigned i = 0; i < p.a.instruction_count; i++) {
    spend(&p);
    const ir_instruction *in = &p.a.instructions[i];
    c->line = in->line;
    if (!unsigned_division(in))
      continue;
    ir_note(c, "candidate", "unsigned_division_or_remainder", 1);
    uint32_t divisor;
    const char *reason = eligible(&p, i, &divisor);
    if (reason) {
      ir_note(c, "skipped", reason, 1);
      continue;
    }
    int paired = 0;
    if (i + 1 < p.a.instruction_count) {
      const ir_instruction *next = &p.a.instructions[i + 1];
      uint32_t other;
      if (next->block == in->block && unsigned_division(next) &&
          strcmp(next->operation, in->operation) &&
          !eligible(&p, i + 1, &other) && other == divisor &&
          p.a.operands[next->first_operand].definition ==
              p.a.operands[in->first_operand].definition)
        paired = 1;
    }
    if (!profitable(&p, divisor, !strcmp(in->operation, "urem"), paired)) {
      ir_note(c, "skipped", "lowered_cost_not_profitable", 1);
      continue;
    }
    replace(&p, i, divisor);
    if (paired) {
      ir_note(c, "candidate", "unsigned_division_or_remainder", 1);
      replace(&p, ++i, divisor);
    }
  }
}
