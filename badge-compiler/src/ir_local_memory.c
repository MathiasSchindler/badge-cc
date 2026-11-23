#include "ir_analysis.h"
#include "ir_loop_admission.h"
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

static const ir_instruction *simple_definition(memory *p, const ir_operand *value,
                                               ir_opcode opcode, unsigned tokens) {
  if (value->kind != IR_OPERAND_LOCAL)
    return NULL;
  unsigned definition = p->a.definitions[value->definition].instruction;
  if (definition == IR_NONE)
    return NULL;
  const ir_instruction *in = &p->a.instructions[definition];
  const ir_line *line = &p->a.text->lines[in->line - 1];
  return in->opcode == opcode && in->line == in->end_line && line->count == tokens &&
                 line->op == line->first + 2
             ? in : NULL;
}
static const ir_instruction *indexed_global(memory *p, const ir_operand *address) {
  const ir_instruction *gep = simple_definition(p, address, IR_OP_GEP, 10);
  if (!gep)
    return NULL;
  const ir_line *line = &p->a.text->lines[gep->line - 1];
  const ir_operand *base = &p->a.operands[gep->first_operand];
  if (gep->operand_count != 2 || base->kind != IR_OPERAND_GLOBAL ||
      strcmp(ir_text(p->a.text, line->op + 1), "i8") ||
      p->a.types[base[1].type].bits != 32)
    return NULL;
  return gep;
}
static int same_operand(memory *p, const ir_operand *a, const ir_operand *b) {
  if (a->kind != b->kind)
    return 0;
  if (a->kind == IR_OPERAND_LOCAL || a->kind == IR_OPERAND_GLOBAL)
    return a->definition == b->definition;
  uint64_t x, y;
  return a->kind == IR_OPERAND_LITERAL &&
         ir_integer(ir_text(p->a.text, a->token), &x) &&
         ir_integer(ir_text(p->a.text, b->token), &y) && x == y;
}
static int same_index(memory *p, const ir_operand *left, const ir_operand *right) {
  if (same_operand(p, left, right))
    return 1;
  const ir_instruction *a = simple_definition(p, left, IR_OP_BINARY, 7);
  const ir_instruction *b = simple_definition(p, right, IR_OP_BINARY, 7);
  if (!a || !b || strcmp(a->operation, b->operation) ||
      (strcmp(a->operation, "mul") && strcmp(a->operation, "shl")) ||
      p->a.types[a->type].bits != 32 || p->a.types[b->type].bits != 32)
    return 0;
  const ir_operand *x = &p->a.operands[a->first_operand], *y = &p->a.operands[b->first_operand];
  return x[1].kind == IR_OPERAND_LITERAL && same_operand(p, x, y) && same_operand(p, x + 1, y + 1);
}
static int same_global_address(memory *p, const ir_operand *a, const ir_operand *b) {
  if (a->kind == IR_OPERAND_GLOBAL)
    return b->kind == IR_OPERAND_GLOBAL && a->definition == b->definition;
  const ir_instruction *left = indexed_global(p, a), *right = indexed_global(p, b);
  if (!left || !right)
    return 0;
  const ir_operand *x = &p->a.operands[left->first_operand], *y = &p->a.operands[right->first_operand];
  return same_operand(p, x, y) && same_index(p, x + 1, y + 1);
}
static void drop_dead_address(memory *p, const ir_operand *address) {
  const ir_instruction *gep = indexed_global(p, address);
  if (!gep || p->a.definitions[gep->definition].use_count != 1)
    return;
  p->a.text->lines[gep->line - 1].replacement = "";
  const ir_instruction *index = simple_definition(p, &p->a.operands[gep->first_operand + 1],
                                                  IR_OP_BINARY, 7);
  if (index && (!strcmp(index->operation, "mul") || !strcmp(index->operation, "shl")) &&
      p->a.definitions[index->definition].use_count == 1)
    p->a.text->lines[index->line - 1].replacement = "";
}
static int private_global(memory *p, const ir_operand *address) {
  if (address->kind != IR_OPERAND_GLOBAL)
    return 0;
  const ir_global *global = &p->a.globals[address->definition];
  if (global->storage_type == IR_NONE || global->function != IR_NONE)
    return 0;
  const ir_line *row = &p->a.text->lines[global->line - 1];
  if (row->count < 4)
    return 0;
  const char *linkage = ir_text(p->a.text, row->first + 2);
  return (!strcmp(linkage, "internal") || !strcmp(linkage, "private")) &&
         !strcmp(ir_text(p->a.text, row->first + 3), "global");
}
static unsigned preceding_value(memory *p, unsigned load, int *store_forwarded) {
  const ir_instruction *in = &p->a.instructions[load];
  const ir_operand *wanted = &p->a.operands[in->first_operand];
  if (wanted->kind != IR_OPERAND_GLOBAL && !indexed_global(p, wanted))
    return IR_NONE;
  int may_forward = private_global(p, wanted);
  unsigned current = in->block, i = load, edges = 0;
  for (unsigned work = 0; work < GLOBAL_WINDOW; work++) {
    spend(p);
    const ir_block *block = &p->a.blocks[current];
    if (i == block->first_instruction) {
      if (edges == 3 || block->predecessor_count != 1)
        break;
      unsigned previous = p->a.predecessors[block->first_predecessor];
      const ir_block *pred = &p->a.blocks[previous];
      if (pred->first_instruction >= block->first_instruction ||
          !ir_dominates(&p->a, previous, current) || !pred->instruction_count)
        break;
      unsigned last = pred->first_instruction + pred->instruction_count - 1;
      if (p->a.instructions[last].opcode != IR_OP_BR)
        break;
      current = previous;
      i = last;
      edges++;
      continue;
    }
    const ir_instruction *next = &p->a.instructions[--i];
    if (may_forward && next->opcode == IR_OP_STORE &&
        next->effects == (IR_EFFECT_WRITE | IR_EFFECT_TRAP) &&
        next->line == next->end_line) {
      const ir_operand *stored = &p->a.operands[next->first_operand];
      const ir_line *row = &p->a.text->lines[next->line - 1];
      if (row->count == 9 && stored->kind == IR_OPERAND_LOCAL &&
          same_operand(p, wanted, stored + 1) &&
          ir_type_equal(&p->a, in->type, stored->type)) {
        *store_forwarded = 1;
        return stored->definition;
      }
    }
    if (next->effects & (IR_EFFECT_WRITE | IR_EFFECT_CALL | IR_EFFECT_VOLATILE |
                         IR_EFFECT_OPAQUE | IR_EFFECT_CONTROL))
      break;
    if (next->opcode != IR_OP_LOAD)
      continue;
    if (!plain(p, next))
      break;
    const ir_operand *pointer = &p->a.operands[next->first_operand];
    if (same_global_address(p, wanted, pointer) && ir_type_equal(&p->a, in->type, next->type))
      return edges ? next->definition : IR_NONE;
  }
  return IR_NONE;
}
static void run_global_chains(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  memory p = {.c = c, .work = IR_LOCAL_MEMORY_WORK_LIMIT, .global_only = 1};
  ir_analyze(c, m, &p.a);
  p.values = ir_allocate(c, p.a.definition_count * sizeof(*p.values));
  unsigned changed = 0;
  for (unsigned i = 0; i < p.a.instruction_count; i++) {
    spend(&p);
    const ir_instruction *in = &p.a.instructions[i];
    if (in->opcode != IR_OP_LOAD || !plain(&p, in) || !bounded_users(&p, i))
      continue;
    int stored = 0;
    unsigned previous = preceding_value(&p, i, &stored);
    if (previous == IR_NONE)
      continue;
    p.values[in->definition] = p.values[previous] ? p.values[previous] : p.a.definitions[previous].name;
    m->lines[in->line - 1].replacement = "";
    drop_dead_address(&p, &p.a.operands[in->first_operand]);
    ir_note(c, "candidate", stored ? "private_store_forwarded" :
                                     "load_across_single_predecessor", 1);
    changed++;
  }
  if (!changed) {
    ir_release_to(c, mark);
    return;
  }
  for (unsigned i = 0; i < p.a.operand_count; i++) {
    spend(&p);
    const ir_operand *op = &p.a.operands[i];
    if (op->kind == IR_OPERAND_LOCAL && p.values[op->definition])
      m->tokens[op->token].replacement = p.values[op->definition];
  }
  for (unsigned n = 0; n < m->line_count; n++) {
    ir_line *line = &m->lines[n];
    int rewritten = 0;
    for (unsigned k = line->first; k < line->first + line->count; k++) {
      spend(&p);
      rewritten |= m->tokens[k].replacement != NULL;
    }
    if (rewritten) {
      size_t start = c->size;
      ir_write_line(c, m, line);
      line->replacement = ir_copy(c, c->output + start, c->size - start);
      c->size = start;
      for (unsigned k = line->first; k < line->first + line->count; k++)
        m->tokens[k].replacement = NULL;
    }
  }
  ir_function_admission(c, m, &p.a, BADGE_SELECTION_THUMB_CONSTANTS);
  c->size = 0;
  ir_reparse(c, m, mark);
}
void ir_local_memory(ir_context *c, ir_module *m) { run_memory(c, m, 0); }
void ir_global_memory(ir_context *c, ir_module *m) {
  run_memory(c, m, 1);
  run_global_chains(c, m);
}
