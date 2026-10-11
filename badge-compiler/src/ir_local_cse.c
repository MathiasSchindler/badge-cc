#include "ir_analysis.h"
#include "ir_integer.h"
#include <inttypes.h>
#include <string.h>

#ifndef IR_CSE_WORK_LIMIT
#define IR_CSE_WORK_LIMIT UINT64_C(50000000)
#endif
#ifndef IR_CSE_HASH_BITS
#define IR_CSE_HASH_BITS 32
#endif
#if IR_CSE_HASH_BITS < 0 || IR_CSE_HASH_BITS > 32
#error Invalid CSE hash width
#endif

typedef struct {
  const char *key;
  size_t size;
  unsigned hash, instruction, generation;
} expression;
typedef struct {
  ir_context *c;
  ir_analysis a;
  unsigned *parents, *token_operands;
  expression *table;
  unsigned capacity;
  uint64_t work;
} cse;

void ir_dead_instructions(ir_context *, ir_module *);

static void spend(cse *p, uint64_t n) {
  if (n > p->work)
    ir_error(p->c, "local-cse work limit exceeded");
  p->work -= n;
}
static unsigned root(cse *p, unsigned d) {
  unsigned r = d;
  while (p->parents[r] != r) {
    spend(p, 1);
    r = p->parents[r];
  }
  while (p->parents[d] != d) {
    unsigned next = p->parents[d];
    p->parents[d] = r;
    d = next;
  }
  return r;
}
static int eligible(cse *p, const ir_instruction *in) {
  if (in->definition == IR_NONE || in->effects)
    return 0;
  if (in->opcode != IR_OP_BINARY && in->opcode != IR_OP_COMPARE &&
      in->opcode != IR_OP_CAST && in->opcode != IR_OP_GEP)
    return 0;
  for (unsigned k = in->first_operand;
       k < in->first_operand + in->operand_count; k++) {
    spend(p, 1);
    const ir_operand *op = &p->a.operands[k];
    if (op->kind == IR_OPERAND_LITERAL) {
      const char *text = ir_text(p->a.text, op->token);
      if (!strcmp(text, "undef") || !strcmp(text, "poison"))
        return 0;
    }
  }
  return 1;
}
static void key(cse *p, const ir_instruction *in) {
  ir_module *m = p->a.text;
  const ir_line *line = &m->lines[in->line - 1];
  p->c->size = 0;
  ir_emit(p->c, "R%u;", in->type);
  for (unsigned token = line->op; token < line->first + line->count; token++) {
    spend(p, 1);
    unsigned k = p->token_operands[token];
    if (k == IR_NONE) {
      const char *text = ir_text(m, token);
      ir_emit(p->c, "T%zu:%s;", strlen(text), text);
      continue;
    }
    const ir_operand *op = &p->a.operands[k];
    ir_emit(p->c, "V%u:%u:", op->type, op->kind);
    if (op->kind == IR_OPERAND_LOCAL)
      ir_emit(p->c, "%u;", root(p, op->definition));
    else if (op->kind == IR_OPERAND_GLOBAL)
      ir_emit(p->c, "%u;", op->definition);
    else {
      const char *text = ir_text(m, token);
      const ir_type *type = &p->a.types[op->type];
      uint64_t n;
      if (type->kind == IR_INT &&
          (!strcmp(text, "zeroinitializer") || ir_integer(text, &n))) {
        if (!strcmp(text, "zeroinitializer"))
          n = 0;
        ir_emit(p->c, "%" PRIu64 ";", n & ir_integer_mask(type->bits));
      } else if (type->kind == IR_PTR && !strcmp(text, "zeroinitializer"))
        ir_emit(p->c, "4:null;");
      else
        ir_emit(p->c, "%zu:%s;", strlen(text), text);
    }
  }
  spend(p, p->c->size + 1);
}
static unsigned lookup(cse *p, unsigned i, unsigned generation) {
  key(p, &p->a.instructions[i]);
  unsigned hash = ir_hash(p->c->output);
#if IR_CSE_HASH_BITS < 32
  hash &= (UINT32_C(1) << IR_CSE_HASH_BITS) - 1;
#endif
  unsigned slot = hash & (p->capacity - 1);
  for (;;) {
    spend(p, 1);
    expression *e = &p->table[slot];
    if (e->generation != generation) {
      *e = (expression){ir_copy(p->c, p->c->output, p->c->size), p->c->size,
                        hash, i, generation};
      return IR_NONE;
    }
    if (e->hash == hash && e->size == p->c->size) {
      spend(p, e->size + 1);
      if (!memcmp(e->key, p->c->output, e->size))
        return e->instruction;
    }
    slot = (slot + 1) & (p->capacity - 1);
  }
}
void ir_local_cse(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  cse p = {.c = c, .work = IR_CSE_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  ir_analysis *a = &p.a;
  p.parents = ir_allocate(c, a->definition_count * sizeof(*p.parents));
  p.token_operands = ir_allocate(c, m->count * sizeof(*p.token_operands));
  unsigned *order = ir_allocate(c, a->block_count * sizeof(*order));
  unsigned maximum = 0;
  for (unsigned d = 0; d < a->definition_count; d++)
    p.parents[d] = d;
  for (unsigned token = 0; token < m->count; token++)
    p.token_operands[token] = IR_NONE;
  for (unsigned k = 0; k < a->operand_count; k++)
    p.token_operands[a->operands[k].token] = k;
  for (unsigned b = 0; b < a->block_count; b++) {
    order[b] = IR_NONE;
    if (a->blocks[b].instruction_count > maximum)
      maximum = a->blocks[b].instruction_count;
  }
  p.capacity = 2;
  while (p.capacity < (uint64_t)maximum * 2)
    p.capacity *= 2;
  p.table = ir_allocate(c, p.capacity * sizeof(*p.table));
  /* Visit dominating definitions first, without sharing tables across blocks.
   */
  for (unsigned b = 0; b < a->block_count; b++) {
    const ir_block *block = &a->blocks[b];
    if (block->rpo != IR_NONE)
      order[a->functions[block->function].first_block + block->rpo] = b;
    else
      ir_note(c, "skipped", "unreachable_block", 1);
  }
  for (unsigned at = 0; at < a->block_count; at++) {
    unsigned b = order[at];
    if (b == IR_NONE)
      continue;
    const ir_block *block = &a->blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(&p, 1);
      const ir_instruction *in = &a->instructions[i];
      c->line = in->line;
      if (!eligible(&p, in)) {
        ir_note(c, "skipped", "effectful_opaque_or_not_whitelisted", 1);
        continue;
      }
      ir_note(c, "candidate", "pure_block_expression", 1);
      unsigned previous = lookup(&p, i, b + 1);
      if (previous == IR_NONE)
        continue;
      const ir_instruction *earlier = &a->instructions[previous];
      if (earlier->block != b || previous >= i ||
          !ir_type_equal(a, in->type, earlier->type))
        ir_error(c, "local-cse representative does not dominate replacement");
      p.parents[in->definition] = earlier->definition;
      for (unsigned l = in->line; l <= in->end_line; l++)
        m->lines[l - 1].replacement = "";
      ir_note(c, "transformed",
              in->opcode == IR_OP_BINARY    ? "arithmetic_reused"
              : in->opcode == IR_OP_COMPARE ? "comparison_reused"
              : in->opcode == IR_OP_CAST    ? "cast_reused"
                                            : "address_reused",
              1);
    }
  }
  c->size = 0;
  for (unsigned k = 0; k < a->operand_count; k++) {
    spend(&p, 1);
    const ir_operand *op = &a->operands[k];
    if (op->kind != IR_OPERAND_LOCAL)
      continue;
    unsigned d = root(&p, op->definition);
    if (d != op->definition) {
      m->tokens[op->token].replacement = a->definitions[d].name;
      ir_note(c, "propagated", "CSE_operand", 1);
    }
  }
  ir_reparse(c, m, mark);
  ir_dead_instructions(c, m);
  ir_reparse(c, m, mark);
}
