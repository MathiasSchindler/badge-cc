#include "ir_analysis.h"
#include "ir_integer.h"
#include <string.h>

#ifndef IR_COMBINE_WORK_LIMIT
#define IR_COMBINE_WORK_LIMIT UINT64_C(50000000)
#endif

typedef struct {
  ir_context *c;
  ir_analysis a;
  unsigned *parents, *leaves, *tokens, *queue, count;
  unsigned char *queued, *removed, *rewritten;
  uint64_t work;
  int booleans;
} combining;

void ir_dead_instructions(ir_context *, ir_module *);

static void spend(combining *p) {
  if (!p->work--)
    ir_error(p->c, p->booleans ? "scalar-cleanup work limit exceeded"
                             : "instruction-combine work limit exceeded");
}
static unsigned root(combining *p, unsigned d) {
  unsigned r = d;
  while (p->parents[r] != r) {
    spend(p);
    r = p->parents[r];
  }
  while (p->parents[d] != d) {
    unsigned next = p->parents[d];
    p->parents[d] = r;
    d = next;
  }
  return r;
}
static void enqueue(combining *p, unsigned i) {
  spend(p);
  if (!p->queued[i]) {
    p->queued[i] = 1;
    p->queue[p->count++] = i;
  }
}
static void users(combining *p, unsigned d) {
  for (unsigned k = p->a.definitions[d].first_use; k != IR_NONE;
       k = p->a.operands[k].next_use)
    enqueue(p, p->a.operands[k].instruction);
}
static int normalize(combining *p, unsigned k) {
  ir_operand *op = &p->a.operands[k];
  if (op->kind != IR_OPERAND_LOCAL)
    return 0;
  unsigned d = root(p, op->definition);
  if (p->leaves[d] != IR_NONE) {
    const ir_operand *leaf = &p->a.operands[p->leaves[d]];
    op->kind = leaf->kind;
    op->definition = leaf->definition;
    op->token = leaf->token;
    return 1;
  }
  if (d == op->definition)
    return 0;
  op->definition = d;
  return 1;
}
static const char *operand_text(const combining *p, unsigned k) {
  const ir_operand *op = &p->a.operands[k];
  return op->kind == IR_OPERAND_LOCAL ? p->a.definitions[op->definition].name
                                      : ir_text(p->a.text, op->token);
}
static int constant(combining *p, unsigned k, uint64_t expected) {
  const ir_operand *op = &p->a.operands[k];
  if (op->kind != IR_OPERAND_LITERAL)
    return 0;
  const char *text = operand_text(p, k);
  uint64_t n;
  if (!strcmp(text, "zeroinitializer"))
    n = 0;
  else if (!ir_integer(text, &n))
    return 0;
  return (n & ir_integer_mask(p->a.types[op->type].bits)) == expected;
}
static int forward(combining *p, ir_instruction *in, unsigned k,
                   const char *rule) {
  normalize(p, k);
  ir_operand *op = &p->a.operands[k];
  const char *text = operand_text(p, k);
  if (!strcmp(text, "undef") || !strcmp(text, "poison")) {
    ir_note(p->c, "skipped", "direct_undef_or_poison_literal", 1);
    return 0;
  }
  if (!ir_type_equal(&p->a, in->type, op->type))
    ir_error(p->c, "instruction-combine replacement type mismatch");
  unsigned d = in->definition;
  if (op->kind == IR_OPERAND_LOCAL) {
    if (op->definition == d) {
      ir_note(p->c, "skipped", "cyclic_alias_preserved", 1);
      return 0;
    }
    p->parents[d] = op->definition;
  } else
    p->leaves[d] = k;
  p->removed[d] = 1;
  ir_note(p->c, "transformed", rule, 1);
  users(p, d);
  return 1;
}
static int arithmetic(combining *p, ir_instruction *in) {
  unsigned first = in->first_operand, last = first + 1;
  const char *op = in->operation;
  /* Signed i1's bit-pattern 1 denotes -1, not the multiplicative identity. */
  if (!strcmp(op, "mul") && p->a.types[in->type].bits == 1) {
    ir_line *line = &p->a.text->lines[in->line - 1];
    for (unsigned k = line->op + 1;
         k < line->first + line->count && !ir_bits(ir_text(p->a.text, k)); k++)
      if (!strcmp(ir_text(p->a.text, k), "nsw"))
        return 0;
  }
  if (!strcmp(op, "add") || !strcmp(op, "or") || !strcmp(op, "xor")) {
    if (constant(p, last, 0))
      return forward(p, in, first,
                     !strcmp(op, "add")  ? "add_zero"
                     : !strcmp(op, "or") ? "or_zero"
                                         : "xor_zero");
    if (constant(p, first, 0))
      return forward(p, in, last,
                     !strcmp(op, "add")  ? "add_zero"
                     : !strcmp(op, "or") ? "or_zero"
                                         : "xor_zero");
  } else if (!strcmp(op, "sub") && constant(p, last, 0))
    return forward(p, in, first, "sub_zero");
  else if (!strcmp(op, "mul")) {
    if (constant(p, last, 1))
      return forward(p, in, first, "mul_one");
    if (constant(p, first, 1))
      return forward(p, in, last, "mul_one");
  } else if (!strcmp(op, "and")) {
    uint64_t ones = ir_integer_mask(p->a.types[in->type].bits);
    if (constant(p, last, ones))
      return forward(p, in, first, "and_all_ones");
    if (constant(p, first, ones))
      return forward(p, in, last, "and_all_ones");
  } else if ((!strcmp(op, "shl") || !strcmp(op, "lshr") ||
              !strcmp(op, "ashr")) &&
             constant(p, last, 0))
    return forward(p, in, first, "shift_zero");
  return 0;
}
static int casts(combining *p, ir_instruction *in, unsigned i) {
  unsigned first = in->first_operand;
  ir_operand *op = &p->a.operands[first];
  /* Retain global/literal pointer casts to preserve indirect-call ABI checks.
   */
  if (!strcmp(in->operation, "bitcast") &&
      ir_type_equal(&p->a, in->type, op->type) &&
      (p->a.types[in->type].kind == IR_INT || op->kind == IR_OPERAND_LOCAL))
    return forward(p, in, first, "identity_bitcast");
  if (p->a.types[in->type].kind != IR_INT ||
      p->a.types[op->type].kind != IR_INT || op->kind != IR_OPERAND_LOCAL)
    return 0;
  unsigned producer = p->a.definitions[op->definition].instruction;
  if (producer == IR_NONE)
    return 0;
  ir_instruction *inner = &p->a.instructions[producer];
  if (inner->opcode != IR_OP_CAST || inner->effects)
    return 0;
  unsigned source = inner->first_operand;
  normalize(p, source);
  if (p->a.types[p->a.operands[source].type].kind != IR_INT)
    return 0;
  const char *replacement = NULL, *rule = NULL;
  unsigned from = p->a.types[p->a.operands[source].type].bits,
           to = p->a.types[in->type].bits;
  if (!strcmp(in->operation, "trunc")) {
    if (!strcmp(inner->operation, "zext") ||
        !strcmp(inner->operation, "sext")) {
      if (from == to)
        return forward(p, in, source, "trunc_extension_roundtrip");
      replacement = from > to ? "trunc" : inner->operation;
      rule = "trunc_extension_chain";
    } else if (!strcmp(inner->operation, "trunc")) {
      replacement = "trunc";
      rule = "trunc_chain";
    }
  } else if (!strcmp(in->operation, "zext") &&
             !strcmp(inner->operation, "zext")) {
    replacement = "zext";
    rule = "zext_chain";
  } else if (!strcmp(in->operation, "sext") &&
             (!strcmp(inner->operation, "sext") ||
              !strcmp(inner->operation, "zext"))) {
    replacement = inner->operation;
    rule = !strcmp(replacement, "sext") ? "sext_chain" : "sext_after_zext";
  }
  if (!replacement)
    return 0;
  ir_operand value = p->a.operands[source];
  op->kind = value.kind;
  op->type = value.type;
  op->token = value.token;
  op->definition = value.definition;
  in->operation = replacement;
  p->rewritten[i] = 1;
  ir_note(p->c, "transformed", rule, 1);
  users(p, in->definition);
  return 1;
}
static int boolean_test(combining *p, ir_instruction *in) {
  if (in->operand_count != 2)
    return 0;
  const ir_line *line = &p->a.text->lines[in->line - 1];
  const char *predicate = ir_text(p->a.text, line->op + 1);
  if (strcmp(predicate, "eq") && strcmp(predicate, "ne"))
    return 0;
  for (unsigned side = 0; side < 2; side++) {
    spend(p);
    unsigned k = in->first_operand + side;
    const ir_operand *op = &p->a.operands[k];
    if (op->kind != IR_OPERAND_LOCAL)
      continue;
    unsigned producer = p->a.definitions[op->definition].instruction;
    if (producer == IR_NONE)
      continue;
    const ir_instruction *extension = &p->a.instructions[producer];
    if (extension->opcode != IR_OP_CAST || extension->effects ||
        (strcmp(extension->operation, "zext") &&
         strcmp(extension->operation, "sext")))
      continue;
    unsigned source = extension->first_operand;
    const ir_type *type = &p->a.types[p->a.operands[source].type];
    if (type->kind != IR_INT || type->bits != 1)
      continue;
    unsigned literal = in->first_operand + (1 - side);
    uint64_t one = !strcmp(extension->operation, "zext")
                       ? 1
                       : ir_integer_mask(p->a.types[op->type].bits);
    if ((!strcmp(predicate, "ne") && constant(p, literal, 0)) ||
        (!strcmp(predicate, "eq") && constant(p, literal, one)))
      return forward(p, in, source, "boolean_extension_test");
  }
  return 0;
}
static void run(ir_context *c, ir_module *m, int booleans) {
  ir_allocation *mark = c->allocations;
  combining p = {.c = c, .work = IR_COMBINE_WORK_LIMIT, .booleans = booleans};
  ir_analyze(c, m, &p.a);
  ir_analysis *a = &p.a;
#define ALLOC(member, n) p.member = ir_allocate(c, (n) * sizeof(*p.member))
  ALLOC(parents, a->definition_count);
  ALLOC(leaves, a->definition_count);
  ALLOC(removed, a->definition_count);
  ALLOC(tokens, a->operand_count);
  ALLOC(queue, a->instruction_count);
  ALLOC(queued, a->instruction_count);
  ALLOC(rewritten, a->instruction_count);
#undef ALLOC
  for (unsigned d = 0; d < a->definition_count; d++) {
    p.parents[d] = d;
    p.leaves[d] = IR_NONE;
  }
  for (unsigned k = 0; k < a->operand_count; k++)
    p.tokens[k] = a->operands[k].token;
  for (unsigned i = 0; i < a->instruction_count; i++)
    enqueue(&p, i);
  while (p.count) {
    spend(&p);
    unsigned i = p.queue[--p.count];
    p.queued[i] = 0;
    ir_instruction *in = &a->instructions[i];
    c->line = in->line;
    if (in->definition != IR_NONE && p.removed[in->definition])
      continue;
    int changed = 0;
    for (unsigned k = in->first_operand;
         k < in->first_operand + in->operand_count; k++) {
      spend(&p);
      changed |= normalize(&p, k);
    }
    if (changed && in->definition != IR_NONE)
      users(&p, in->definition);
    if (in->definition == IR_NONE || in->effects ||
        (in->opcode != IR_OP_BINARY && in->opcode != IR_OP_CAST &&
         !(booleans && in->opcode == IR_OP_COMPARE))) {
      ir_note(c, "skipped", "effectful_or_not_whitelisted", 1);
      continue;
    }
    ir_note(c, "candidate", "neutral_arithmetic_or_cast", 1);
    if (!(in->opcode == IR_OP_COMPARE ? boolean_test(&p, in)
          : in->opcode == IR_OP_BINARY ? arithmetic(&p, in)
                                      : casts(&p, in, i)))
      ir_note(c, "skipped", "rule_precondition_not_met", 1);
  }
  for (unsigned k = 0; k < a->operand_count; k++) {
    spend(&p);
    normalize(&p, k);
    if (a->operands[k].kind != IR_OPERAND_BLOCK) {
      const char *text = operand_text(&p, k);
      if (strcmp(text, ir_text(m, p.tokens[k]))) {
        m->tokens[p.tokens[k]].replacement = text;
        ir_note(c, "propagated", "combined_operand", 1);
      }
    }
  }
  for (unsigned i = 0; i < a->instruction_count; i++) {
    ir_instruction *in = &a->instructions[i];
    if (in->definition != IR_NONE && p.removed[in->definition]) {
      for (unsigned l = in->line; l <= in->end_line; l++)
        m->lines[l - 1].replacement = "";
    } else if (p.rewritten[i])
      m->lines[in->line - 1].replacement = ir_format(
          c, "  %s = %s %s %s to %s\n", a->definitions[in->definition].name,
          in->operation, a->types[a->operands[in->first_operand].type].key,
          operand_text(&p, in->first_operand), a->types[in->type].key);
  }
  ir_reparse(c, m, mark);
  ir_dead_instructions(c, m);
  ir_reparse(c, m, mark);
}
void ir_instruction_combine(ir_context *c, ir_module *m) { run(c, m, 0); }
void ir_scalar_cleanup(ir_context *c, ir_module *m) { run(c, m, 1); }
