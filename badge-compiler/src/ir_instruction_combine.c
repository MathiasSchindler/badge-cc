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
static void fold_narrow_distances(ir_context *c, ir_module *m);
void ir_scalar_cleanup(ir_context *c, ir_module *m) {
  run(c, m, 1);
  if (strstr(m->source, "zext i") && strstr(m->source, "icmp u"))
    fold_narrow_distances(c, m);
}

static int low_bits_only(combining *p, unsigned definition, uint32_t mask) {
  for (unsigned step = 0, shifted = 0; step < 8; step++) {
    const ir_definition *def = &p->a.definitions[definition];
    if (def->use_count != 1) return 0;
    const ir_operand *use = &p->a.operands[def->first_use];
    const ir_instruction *in = &p->a.instructions[use->instruction];
    spend(p);
    if (in->opcode != IR_OP_BINARY || in->operand_count != 2 ||
        p->a.types[in->type].bits != 32 || in->end_line != in->line ||
        p->a.text->lines[in->line - 1].count != 7)
      return 0;
    const ir_operand *left = &p->a.operands[in->first_operand];
    const ir_operand *other = left + (use == left);
    uint64_t value;
    int literal = other->kind == IR_OPERAND_LITERAL &&
        ir_integer(ir_text(p->a.text, other->token), &value);
    const char *op = in->operation;
    if (!strcmp(op, "and"))
      return literal && !(((uint32_t)value >> shifted) & ~mask);
    if (!strcmp(op, "shl") || !strcmp(op, "mul")) {
      if (!literal) return 0;
      unsigned n;
      if (!strcmp(op, "shl")) {
        if (use != left || value >= 32) return 0;
        n = (unsigned)value;
      } else {
        uint32_t multiplier = (uint32_t)value;
        n = multiplier ? (unsigned)__builtin_ctz(multiplier) : 32;
      }
      shifted += n;
      if (shifted >= 32) return 1;
    } else if (strcmp(op, "add") && strcmp(op, "sub") &&
               strcmp(op, "or") && strcmp(op, "xor"))
      return 0;
    definition = in->definition;
  }
  return 0;
}

static int masked_users(combining *p, const ir_definition *def, uint32_t mask) {
  if (def->use_count < 2 || def->use_count > 8)
    return 0;
  for (unsigned u = def->first_use; u != IR_NONE; u = p->a.operands[u].next_use) {
    spend(p);
    const ir_operand *use = &p->a.operands[u];
    const ir_instruction *in = &p->a.instructions[use->instruction];
    if (in->block != def->block || in->opcode != IR_OP_BINARY || strcmp(in->operation, "and") ||
        in->operand_count != 2 || p->a.types[in->type].bits != 32 ||
        in->end_line != in->line || p->a.text->lines[in->line - 1].count != 7)
      return 0;
    const ir_operand *left = &p->a.operands[in->first_operand];
    const ir_operand *other = left + (use == left);
    uint64_t value;
    if (other->kind != IR_OPERAND_LITERAL ||
        !ir_integer(ir_text(p->a.text, other->token), &value) || ((uint32_t)value & ~mask))
      return 0;
  }
  return 1;
}

void ir_low_bits(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  combining p = {.c = c, .work = IR_COMBINE_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  for (unsigned i = 0; i < p.a.instruction_count; i++) {
    spend(&p);
    const ir_instruction *in = &p.a.instructions[i];
    if (in->opcode != IR_OP_BINARY || strcmp(in->operation, "and") ||
        in->operand_count != 2 || p.a.types[in->type].bits != 32 ||
        in->end_line != in->line || m->lines[in->line - 1].count != 7)
      continue;
    const ir_operand *left = &p.a.operands[in->first_operand], *right = left + 1;
    const ir_operand *src = left->kind == IR_OPERAND_LOCAL ? left : right;
    const ir_operand *mask = src == left ? right : left;
    const ir_definition *def = &p.a.definitions[in->definition];
    uint64_t value;
    if (src->kind != IR_OPERAND_LOCAL || mask->kind != IR_OPERAND_LITERAL ||
        !ir_integer(ir_text(m, mask->token), &value) ||
        (!low_bits_only(&p, in->definition, (uint32_t)value) &&
         !masked_users(&p, def, (uint32_t)value)))
      continue;
    const ir_token *token = &m->tokens[src->token];
    for (unsigned u = def->first_use; u != IR_NONE; u = p.a.operands[u].next_use)
      m->tokens[p.a.operands[u].token].replacement = token->replacement ? token->replacement : token->text;
    m->lines[in->line - 1].replacement = "";
    ir_note(c, "transformed", "unobserved_upper_bits_mask", 1);
  }
  ir_reparse(c, m, mark);
}

static int plain_i32_binary(const ir_analysis *a, const ir_instruction *in,
                            const char *name) {
  return in->opcode == IR_OP_BINARY && !strcmp(in->operation, name) &&
      in->operand_count == 2 && a->types[in->type].bits == 32 &&
      in->end_line == in->line && a->text->lines[in->line - 1].count == 7 &&
      !a->text->lines[in->line - 1].replacement;
}

static const ir_instruction *from_operand(const ir_analysis *a, const ir_operand *o) {
  if (o->kind != IR_OPERAND_LOCAL)
    return NULL;
  unsigned i = a->definitions[o->definition].instruction;
  return i == IR_NONE ? NULL : &a->instructions[i];
}

static int signed_word_binary(const ir_analysis *a, const ir_instruction *i, const char *op) {
  if (!i || i->opcode != IR_OP_BINARY || i->operand_count != 2 ||
      a->types[i->type].bits != 32 || i->line != i->end_line || strcmp(i->operation, op))
    return 0;
  const ir_line *line = &a->text->lines[i->line - 1];
  return !line->replacement && (line->count == 7 ||
      (line->count == 8 && !strcmp(ir_text(a->text, line->op + 1), "nsw")));
}

static int fold_negated_bias(combining *p, const ir_instruction *bias) {
  ir_analysis *a = &p->a;
  int subtract = signed_word_binary(a, bias, "sub");
  if (!subtract && !signed_word_binary(a, bias, "add"))
    return 0;
  const ir_operand *ops = &a->operands[bias->first_operand];
  uint64_t one;
  if (ops[1].kind != IR_OPERAND_LITERAL ||
      !ir_integer(ir_text(a->text, ops[1].token), &one) || one != 1)
    return 0;
  const ir_instruction *neg = from_operand(a, ops);
  if (!signed_word_binary(a, neg, "sub") || neg->block != bias->block ||
      a->definitions[neg->definition].use_count != 1)
    return 0;
  const ir_operand *arg = &a->operands[neg->first_operand];
  uint64_t zero;
  if (arg->kind != IR_OPERAND_LITERAL || arg[1].kind != IR_OPERAND_LOCAL ||
      !ir_integer(ir_text(a->text, arg->token), &zero) || zero)
    return 0;
  // The replacement matches the original modulo 2^32 wherever its nsw operations are defined.
  const char *src = a->definitions[arg[1].definition].name;
  const char *dst = a->definitions[bias->definition].name;
  a->text->lines[bias->line - 1].replacement = subtract ?
      ir_format(p->c, "  %s = xor i32 %s, -1\n", dst, src) :
      ir_format(p->c, "  %s = sub i32 1, %s\n", dst, src);
  a->text->lines[neg->line - 1].replacement = "";
  ir_note(p->c, "transformed", "folded_word_negation_bias", 1);
  return 1;
}

static int zero_extended_word(const ir_analysis *a, const ir_operand *o) {
  const ir_instruction *i = from_operand(a, o);
  return i && i->opcode == IR_OP_CAST && !strcmp(i->operation, "zext") &&
         a->types[i->type].bits == 32 &&
         a->types[a->operands[i->first_operand].type].bits <= 16;
}

static int same_local(const ir_operand *a, const ir_operand *b) {
  return a->kind == IR_OPERAND_LOCAL && b->kind == IR_OPERAND_LOCAL &&
         a->definition == b->definition;
}

static void fold_narrow_distances(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  ir_analysis a;
  ir_analyze(c, m, &a);
  int changed = 0;
  for (unsigned b = 0; b < a.block_count; b++) {
    const ir_block *head = &a.blocks[b];
    const ir_instruction *branch = &a.instructions[head->first_instruction + head->instruction_count - 1];
    if (branch->opcode != IR_OP_BR || branch->operand_count != 3 ||
        m->lines[branch->line - 1].replacement)
      continue;
    const ir_operand *edges = &a.operands[branch->first_operand];
    const ir_instruction *cmp = from_operand(&a, edges);
    if (!cmp || cmp->opcode != IR_OP_COMPARE || cmp->block != b ||
        m->lines[cmp->line - 1].replacement ||
        a.definitions[cmp->definition].use_count != 1)
      continue;
    const char *pred = ir_text(m, m->lines[cmp->line - 1].op + 1);
    int reversed = !strcmp(pred, "ult") || !strcmp(pred, "ule");
    if (!reversed && strcmp(pred, "ugt") && strcmp(pred, "uge"))
      continue;
    const ir_operand *ops = &a.operands[cmp->first_operand];
    if (!zero_extended_word(&a, ops) || !zero_extended_word(&a, ops + 1))
      continue;
    const ir_instruction *difference[2];
    unsigned join = IR_NONE;
    unsigned n = 0;
    for (; n < 2; n++) {
      const ir_block *arm = &a.blocks[edges[n + 1].block];
      difference[n] = &a.instructions[arm->first_instruction];
      const ir_instruction *jump = difference[n] + 1;
      if (arm->predecessor_count != 1 || arm->instruction_count != 2 ||
          !plain_i32_binary(&a, difference[n], "sub") || jump->opcode != IR_OP_BR ||
          jump->operand_count != 1 || m->lines[jump->line - 1].replacement ||
          a.definitions[difference[n]->definition].use_count != 1)
        break;
      const ir_operand *values = &a.operands[difference[n]->first_operand];
      unsigned order = n ^ (unsigned)reversed;
      if (!same_local(values, ops + order) || !same_local(values + 1, ops + (1 - order)))
        break;
      unsigned destination = a.operands[jump->first_operand].block;
      if (n && destination != join)
        break;
      join = destination;
    }
    if (n != 2 || edges[1].block == edges[2].block)
      continue;
    const ir_block *merge = &a.blocks[join];
    const ir_instruction *phi = &a.instructions[merge->first_instruction];
    if (merge->predecessor_count != 2 || merge->instruction_count < 2 ||
        phi->opcode != IR_OP_PHI || phi->operand_count != 4 ||
        m->lines[phi->line - 1].replacement || (phi + 1)->opcode == IR_OP_PHI)
      continue;
    for (n = 0; n < 2; n++) {
      const ir_operand *v = &a.operands[phi->first_operand];
      if (v[1].block != edges[n + 1].block) v += 2;
      if (v[1].block != edges[n + 1].block || v->kind != IR_OPERAND_LOCAL ||
          v->definition != difference[n]->definition)
        break;
    }
    if (n != 2)
      continue;
    const char *delta = ir_fresh(c, m, '%');
    m->lines[cmp->line - 1].replacement = "";
    m->lines[branch->line - 1].replacement = ir_format(c,
        "  %s = sub i32 %s, %s\n  %s = call i32 @llvm.abs.i32(i32 %s, i1 false)\n"
        "  br label %%%s\n", delta, ir_text(m, ops->token), ir_text(m, ops[1].token),
        a.definitions[phi->definition].name, delta, merge->name);
    m->lines[phi->line - 1].replacement = "";
    for (n = 0; n < 2; n++) {
      m->lines[a.blocks[edges[n + 1].block].line - 1].replacement = "";
      m->lines[difference[n]->line - 1].replacement = "";
      m->lines[(difference[n] + 1)->line - 1].replacement = "";
    }
    c->line = branch->line;
    ir_note(c, "transformed", "bounded_unsigned_distance", 1);
    changed = 1;
  }
  if (changed) {
    int declared = 0;
    for (unsigned n = 0; n < a.global_count; n++)
      declared |= !strcmp(ir_name_key(c, a.globals[n].name), "@llvm.abs.i32");
    if (!declared)
      m->suffix = ir_format(c, "%sdeclare i32 @llvm.abs.i32(i32, i1 immarg)\n",
                           m->suffix ? m->suffix : "");
    ir_reparse(c, m, mark);
  } else
    ir_release_to(c, mark);
}

void ir_linear_bias(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  combining p = {.c = c, .work = IR_COMBINE_WORK_LIMIT};
  ir_analysis *a = &p.a;
  ir_analyze(c, m, a);
  for (unsigned i = 0; i < a->instruction_count; i++) {
    spend(&p);
    const ir_instruction *add = &a->instructions[i];
    if (fold_negated_bias(&p, add))
      continue;
    if (!plain_i32_binary(a, add, "add"))
      continue;
    const ir_instruction *sub[2];
    const ir_operand *input[2], *biased[2];
    uint32_t correction = 0;
    unsigned n = 0;
    for (; n < 2; n++) {
      const ir_operand *sum = &a->operands[add->first_operand + n];
      if (sum->kind != IR_OPERAND_LOCAL) break;
      const ir_definition *scaled = &a->definitions[sum->definition];
      if (scaled->use_count != 1 || scaled->instruction == IR_NONE) break;
      const ir_instruction *mul = &a->instructions[scaled->instruction];
      if (mul->block != add->block || !plain_i32_binary(a, mul, "mul")) break;
      const ir_operand *left = &a->operands[mul->first_operand];
      biased[n] = left->kind == IR_OPERAND_LOCAL ? left : left + 1;
      const ir_operand *factor = left + (biased[n] == left);
      uint64_t scale, bias;
      if (biased[n]->kind != IR_OPERAND_LOCAL || factor->kind != IR_OPERAND_LITERAL ||
          !ir_integer(ir_text(m, factor->token), &scale))
        break;
      const ir_definition *def = &a->definitions[biased[n]->definition];
      if (def->use_count != 1 || def->instruction == IR_NONE) break;
      sub[n] = &a->instructions[def->instruction];
      if (sub[n]->block != add->block || !plain_i32_binary(a, sub[n], "sub")) break;
      input[n] = &a->operands[sub[n]->first_operand];
      const ir_operand *offset = input[n] + 1;
      if (input[n]->kind != IR_OPERAND_LOCAL || offset->kind != IR_OPERAND_LITERAL ||
          !ir_integer(ir_text(m, offset->token), &bias))
        break;
      correction += (uint32_t)scale * (uint32_t)bias;
    }
    if (n != 2)
      continue;
    for (n = 0; n < 2; n++) {
      m->tokens[biased[n]->token].replacement = ir_text(m, input[n]->token);
      m->lines[sub[n]->line - 1].replacement = "";
    }
    if (correction) {
      const char *sum = ir_fresh(c, m, '%');
      m->lines[add->line - 1].replacement = ir_format(c,
          "  %s = add i32 %s, %s\n  %s = sub i32 %s, %u\n",
          sum, ir_text(m, a->operands[add->first_operand].token),
          ir_text(m, a->operands[add->first_operand + 1].token),
          a->definitions[add->definition].name, sum, correction);
    }
    ir_note(c, "transformed", "combined_linear_bias", 1);
  }
  ir_reparse(c, m, mark);
}
