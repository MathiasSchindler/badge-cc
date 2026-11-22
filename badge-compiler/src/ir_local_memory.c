#include "ir_analysis.h"
#include <string.h>

#ifndef IR_LOCAL_MEMORY_WORK_LIMIT
#define IR_LOCAL_MEMORY_WORK_LIMIT UINT64_C(50000000)
#endif

typedef struct {
  ir_context *c;
  ir_analysis a;
  const char **values;
  uint64_t work;
  unsigned global_only;
} memory;
enum { MEMORY_WINDOW = 2, GLOBAL_WINDOW = 12, GLOBAL_ENTRIES = 4 };

static void spend(memory *p) {
  if (!p->work--)
    ir_error(p->c, "local-memory work limit exceeded");
}
static const char *value(memory *p, const ir_operand *op) {
  return op->kind == IR_OPERAND_LOCAL && p->values[op->definition]
             ? p->values[op->definition]
             : ir_text(p->a.text, op->token);
}
static int scalar(const ir_analysis *a, unsigned type) {
  const ir_type *t = &a->types[type];
  return t->kind == IR_PTR ||
         (t->kind == IR_INT && (t->bits == 8 || t->bits == 16 ||
                               t->bits == 32));
}
static int plain(memory *p, const ir_instruction *in) {
  if (in->effects != (IR_EFFECT_READ | IR_EFFECT_TRAP) ||
      in->end_line != in->line)
    return 0;
  const ir_line *line = &p->a.text->lines[in->line - 1];
  for (unsigned k = line->first; k < line->first + line->count; ++k) {
    spend(p);
    const char *text = ir_text(p->a.text, k);
    if (*text == '!' || !strcmp(text, "atomic") ||
        !strcmp(text, "volatile"))
      return 0;
  }
  return scalar(&p->a, in->type);
}
static int bounded_users(memory *p, unsigned i) {
  const ir_instruction *in = &p->a.instructions[i];
  unsigned end = i;
  for (unsigned k = p->a.definitions[in->definition].first_use;
       k != IR_NONE; k = p->a.operands[k].next_use) {
    spend(p);
    unsigned user = p->a.operands[k].instruction;
    if (user <= i || user > i + (p->global_only ? GLOBAL_WINDOW : MEMORY_WINDOW) ||
        p->a.instructions[user].block != in->block ||
        p->a.instructions[user].opcode == IR_OP_PHI)
      return 0;
    if (user > end)
      end = user;
  }
  for (unsigned k = i + 1; k <= end; ++k) {
    spend(p);
    const ir_instruction *user = &p->a.instructions[k];
    if ((user->effects & IR_EFFECT_OPAQUE) ||
        ((user->effects & IR_EFFECT_CALL) && !(p->global_only && k == end)) ||
        ((user->effects & IR_EFFECT_CONTROL) &&
         !(k == end && user->opcode == IR_OP_RET)))
      return 0;
  }
  return end != i;
}
static void run_memory(ir_context *c, ir_module *m, unsigned global_only) {
  ir_allocation *mark = c->allocations;
  memory p = {.c = c, .work = IR_LOCAL_MEMORY_WORK_LIMIT, .global_only = global_only};
  ir_analyze(c, m, &p.a);
  p.values = ir_allocate(c, p.a.definition_count * sizeof(*p.values));
  for (unsigned b = 0; b < p.a.block_count; ++b) {
    const ir_block *block = &p.a.blocks[b];
    unsigned previous[GLOBAL_ENTRIES] = {IR_NONE, IR_NONE, IR_NONE, IR_NONE};
    unsigned entries = global_only ? GLOBAL_ENTRIES : 1;
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; ++i) {
      spend(&p);
      const ir_instruction *in = &p.a.instructions[i];
      c->line = in->line;
      for (unsigned k = 0; k < entries; k++)
        if (previous[k] != IR_NONE && i - previous[k] > (global_only ? GLOBAL_WINDOW : MEMORY_WINDOW))
          previous[k] = IR_NONE;
      if ((in->effects & (IR_EFFECT_CALL | IR_EFFECT_OPAQUE | IR_EFFECT_WRITE |
                          IR_EFFECT_VOLATILE | IR_EFFECT_CONTROL)) ||
          (in->opcode == IR_OP_LOAD && !plain(&p, in))) {
        for (unsigned k = 0; k < entries; k++) previous[k] = IR_NONE;
        continue;
      }
      if (in->opcode != IR_OP_LOAD) {
        continue;
      }
      unsigned address = in->first_operand;
      const ir_operand *ptr = &p.a.operands[address];
      if (global_only && ptr->kind != IR_OPERAND_GLOBAL)
        continue;
      if (ptr->kind != IR_OPERAND_LOCAL && ptr->kind != IR_OPERAND_GLOBAL) {
        previous[0] = IR_NONE;
        continue;
      }
      int forwarded = 0;
      for (unsigned k = 0; k < entries && !forwarded; k++) {
        if (previous[k] == IR_NONE)
          continue;
        const ir_instruction *old = &p.a.instructions[previous[k]];
        const ir_operand *old_ptr = &p.a.operands[old->first_operand];
        if (ir_type_equal(&p.a, in->type, old->type) &&
            !strcmp(value(&p, ptr), value(&p, old_ptr)) && bounded_users(&p, i)) {
          p.values[in->definition] = p.a.definitions[old->definition].name;
          m->lines[in->line - 1].replacement = "";
          ir_note(c, "transformed", "load_to_load", 1);
          forwarded = 1;
        }
      }
      if (!forwarded) {
        for (unsigned k = entries - 1; k > 0; k--) previous[k] = previous[k - 1];
        previous[0] = i;
      }
    }
  }
  for (unsigned k = 0; k < p.a.operand_count; ++k) {
    spend(&p);
    const ir_operand *op = &p.a.operands[k];
    if (op->kind == IR_OPERAND_LOCAL && p.values[op->definition])
      m->tokens[op->token].replacement = p.values[op->definition];
  }
  ir_reparse(c, m, mark);
}
void ir_local_memory(ir_context *c, ir_module *m) { run_memory(c, m, 0); }
void ir_global_memory(ir_context *c, ir_module *m) { run_memory(c, m, 1); }
