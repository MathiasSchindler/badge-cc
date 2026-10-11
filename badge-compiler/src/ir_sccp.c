#include "ir_analysis.h"
#include "ir_integer.h"
#include <inttypes.h>
#include <string.h>

#ifndef IR_SCCP_WORK_LIMIT
#define IR_SCCP_WORK_LIMIT UINT64_C(50000000)
#endif

typedef enum { UNKNOWN, CONSTANT, OVERDEFINED } value_kind;
typedef struct {
  value_kind kind;
  uint64_t integer;
} value;
typedef struct {
  ir_context *c;
  ir_analysis a;
  value *values;
  unsigned char *blocks, *edges, *queued;
  unsigned *queue, count, *phi_edges, *selected, *aliases, *parents;
  const char **literals, **resolved;
  uint64_t work;
} propagation;

void ir_constant_fold(ir_context *, ir_module *);
void ir_dead_instructions(ir_context *, ir_module *);

static void spend(propagation *p) {
  if (!p->work--)
    ir_error(p->c, "SCCP work limit exceeded");
}
static value meet(value a, value b) {
  if (a.kind == UNKNOWN)
    return b;
  if (b.kind == UNKNOWN)
    return a;
  if (a.kind == OVERDEFINED || b.kind == OVERDEFINED || a.integer != b.integer)
    return (value){OVERDEFINED, 0};
  return a;
}
static value operand_value(propagation *p, unsigned k) {
  ir_operand *op = &p->a.operands[k];
  if (op->kind == IR_OPERAND_LOCAL)
    return p->values[op->definition];
  if (op->kind == IR_OPERAND_LITERAL && p->a.types[op->type].kind == IR_INT) {
    const char *text = ir_text(p->a.text, op->token);
    uint64_t n;
    if (!strcmp(text, "zeroinitializer"))
      n = 0;
    else if (!ir_integer(text, &n))
      return (value){OVERDEFINED, 0};
    return (value){CONSTANT, n & ir_integer_mask(p->a.types[op->type].bits)};
  }
  return (value){OVERDEFINED, 0};
}
static void enqueue(propagation *p, unsigned i) {
  spend(p);
  if (!p->queued[i] && p->blocks[p->a.instructions[i].block]) {
    p->queued[i] = 1;
    p->queue[p->count++] = i;
  }
}
static void update(propagation *p, unsigned d, value v) {
  value old = p->values[d], next = meet(old, v);
  if (old.kind == next.kind &&
      (old.kind != CONSTANT || old.integer == next.integer))
    return;
  p->values[d] = next;
  for (unsigned k = p->a.definitions[d].first_use; k != IR_NONE;
       k = p->a.operands[k].next_use)
    enqueue(p, p->a.operands[k].instruction);
}
static void activate_block(propagation *p, unsigned b) {
  ir_block *block = &p->a.blocks[b];
  p->blocks[b] = 1;
  for (unsigned k = block->first_instruction;
       k < block->first_instruction + block->instruction_count; k++)
    enqueue(p, k);
}
static void activate_edge(propagation *p, unsigned e) {
  spend(p);
  if (p->edges[e])
    return;
  p->edges[e] = 1;
  unsigned b = p->a.successors[e];
  if (!p->blocks[b]) {
    activate_block(p, b);
    return;
  }
  ir_block *block = &p->a.blocks[b];
  for (unsigned k = block->first_instruction;
       k < block->first_instruction + block->instruction_count &&
       p->a.instructions[k].opcode == IR_OP_PHI;
       k++)
    enqueue(p, k);
}
static value evaluate(propagation *p, ir_instruction *in) {
  unsigned first = in->first_operand;
  if ((in->effects & IR_EFFECT_OPAQUE) || p->a.types[in->type].kind != IR_INT)
    return (value){OVERDEFINED, 0};
  if (in->opcode == IR_OP_PHI) {
    value result = {UNKNOWN, 0};
    for (unsigned k = first; k < first + in->operand_count; k += 2) {
      spend(p);
      if (p->edges[p->phi_edges[k]])
        result = meet(result, operand_value(p, k));
    }
    return result;
  }
  if (in->opcode != IR_OP_BINARY && in->opcode != IR_OP_COMPARE &&
      in->opcode != IR_OP_CAST && in->opcode != IR_OP_FREEZE &&
      in->opcode != IR_OP_SELECT)
    return (value){OVERDEFINED, 0};
  value a = operand_value(p, first);
  if (in->opcode == IR_OP_SELECT) {
    if (a.kind != CONSTANT)
      return a;
    return operand_value(p, first + (a.integer ? 1 : 2));
  }
  if (in->opcode == IR_OP_FREEZE)
    return a;
  unsigned bits = p->a.types[in->type].bits;
  if (in->opcode == IR_OP_CAST) {
    if (p->a.types[p->a.operands[first].type].kind != IR_INT)
      return (value){OVERDEFINED, 0};
    if (a.kind != CONSTANT)
      return a;
    unsigned from = p->a.types[p->a.operands[first].type].bits;
    if (!strcmp(in->operation, "sext"))
      a.integer = ir_integer_extend(a.integer, from);
    else if (strcmp(in->operation, "zext") && strcmp(in->operation, "trunc") &&
             strcmp(in->operation, "bitcast"))
      return (value){OVERDEFINED, 0};
    a.integer &= ir_integer_mask(bits);
    return a;
  }
  ir_line *line = &p->a.text->lines[in->line - 1];
  if (in->opcode == IR_OP_BINARY && !ir_bits(ir_text(p->a.text, line->op + 1)))
    return (value){OVERDEFINED, 0};
  value b = operand_value(p, first + 1);
  if (a.kind == OVERDEFINED || b.kind == OVERDEFINED)
    return (value){OVERDEFINED, 0};
  if (a.kind == UNKNOWN || b.kind == UNKNOWN)
    return (value){UNKNOWN, 0};
  uint64_t result;
  int folded;
  if (in->opcode == IR_OP_COMPARE) {
    if (p->a.types[p->a.operands[first].type].kind != IR_INT)
      return (value){OVERDEFINED, 0};
    folded = ir_integer_compare(ir_text(p->a.text, line->op + 1),
                                p->a.types[p->a.operands[first].type].bits,
                                a.integer, b.integer, &result);
  } else
    folded =
        ir_integer_binary(in->operation, bits, a.integer, b.integer, &result);
  return folded ? (value){CONSTANT, result} : (value){OVERDEFINED, 0};
}
static unsigned selected_edge(propagation *p, ir_instruction *in) {
  ir_block *b = &p->a.blocks[in->block];
  if (in->effects & IR_EFFECT_OPAQUE)
    return IR_NONE;
  if (in->opcode == IR_OP_BR && in->operand_count == 1)
    return b->first_successor;
  value condition = operand_value(p, in->first_operand);
  if (condition.kind != CONSTANT)
    return IR_NONE;
  if (in->opcode == IR_OP_BR)
    return b->first_successor + (condition.integer ? 0 : 1);
  for (unsigned k = 2, edge = b->first_successor + 1; k < in->operand_count;
       k += 2, edge++) {
    spend(p);
    if (operand_value(p, in->first_operand + k).integer == condition.integer)
      return edge;
  }
  return b->first_successor;
}
static void drain(propagation *p) {
  while (p->count) {
    spend(p);
    unsigned i = p->queue[--p->count];
    p->queued[i] = 0;
    ir_instruction *in = &p->a.instructions[i];
    p->c->line = in->line;
    if (in->definition != IR_NONE)
      update(p, in->definition, evaluate(p, in));
    if (in->opcode != IR_OP_BR && in->opcode != IR_OP_SWITCH)
      continue;
    unsigned selected = selected_edge(p, in);
    ir_block *b = &p->a.blocks[in->block];
    if (selected != IR_NONE)
      activate_edge(p, selected);
    else if ((in->effects & IR_EFFECT_OPAQUE) ||
             operand_value(p, in->first_operand).kind == OVERDEFINED)
      for (unsigned k = 0; k < b->successor_count; k++)
        activate_edge(p, b->first_successor + k);
  }
}

/* Map each PHI occurrence to an actual CFG edge, including parallel edges. */
static void index_phis(propagation *p) {
  ir_analysis *a = &p->a;
  unsigned *incoming =
      ir_allocate(p->c, a->predecessor_count * sizeof(unsigned));
  unsigned *next = ir_allocate(p->c, a->predecessor_count * sizeof(unsigned));
  unsigned *head = ir_allocate(p->c, a->block_count * sizeof(unsigned));
  unsigned *cursor = ir_allocate(p->c, a->block_count * sizeof(unsigned));
  unsigned *stamp = ir_allocate(p->c, a->block_count * sizeof(unsigned));
  for (unsigned b = 0; b < a->block_count; b++)
    cursor[b] = a->blocks[b].first_predecessor;
  for (unsigned b = 0; b < a->block_count; b++) {
    ir_block *block = &a->blocks[b];
    for (unsigned e = block->first_successor;
         e < block->first_successor + block->successor_count; e++) {
      spend(p);
      unsigned k = cursor[a->successors[e]]++;
      if (a->predecessors[k] != b)
        ir_error(p->c, "SCCP predecessor index mismatch");
      incoming[k] = e;
    }
  }
  for (unsigned b = 0; b < a->block_count; b++) {
    ir_block *block = &a->blocks[b];
    for (unsigned k = block->first_predecessor;
         k < block->first_predecessor + block->predecessor_count; k++)
      head[a->predecessors[k]] = IR_NONE;
    for (unsigned k = block->first_predecessor + block->predecessor_count;
         k > block->first_predecessor;) {
      spend(p);
      k--;
      unsigned from = a->predecessors[k];
      next[k] = head[from];
      head[from] = k;
    }
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count &&
         a->instructions[i].opcode == IR_OP_PHI;
         i++) {
      ir_instruction *in = &a->instructions[i];
      for (unsigned k = in->first_operand;
           k < in->first_operand + in->operand_count; k += 2) {
        spend(p);
        unsigned from = a->operands[k + 1].block;
        if (stamp[from] != i + 1) {
          stamp[from] = i + 1;
          cursor[from] = head[from];
        }
        if (cursor[from] == IR_NONE)
          ir_error(p->c, "SCCP PHI edge index mismatch");
        p->phi_edges[k] = incoming[cursor[from]];
        cursor[from] = next[cursor[from]];
      }
    }
  }
}
static int retained(propagation *p, unsigned e, unsigned from) {
  return p->blocks[from] &&
         (p->selected[from] == IR_NONE || p->selected[from] == e);
}
static unsigned root(propagation *p, unsigned d) {
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
static unsigned canonical_index(propagation *p, unsigned k) {
  ir_operand *op = &p->a.operands[k];
  if (op->kind == IR_OPERAND_LOCAL) {
    unsigned r = root(p, op->definition);
    if (p->aliases[r] != IR_NONE)
      return p->aliases[r];
  }
  return k;
}
static ir_operand canonical_operand(propagation *p, unsigned k) {
  k = canonical_index(p, k);
  ir_operand op = p->a.operands[k];
  if (op.kind == IR_OPERAND_LOCAL) {
    op.definition = root(p, op.definition);
  }
  return op;
}
static int same_operand(propagation *p, unsigned x, unsigned y) {
  ir_operand a = canonical_operand(p, x), b = canonical_operand(p, y);
  if (a.kind != b.kind)
    return 0;
  if (a.kind == IR_OPERAND_LOCAL || a.kind == IR_OPERAND_GLOBAL)
    return a.definition == b.definition;
  const char *at = ir_text(p->a.text, a.token),
             *bt = ir_text(p->a.text, b.token);
  uint64_t av, bv;
  if (p->a.types[a.type].kind == IR_INT && ir_integer(at, &av) &&
      ir_integer(bt, &bv))
    return (av & ir_integer_mask(p->a.types[a.type].bits)) ==
           (bv & ir_integer_mask(p->a.types[b.type].bits));
  return !strcmp(at, bt) &&
         (!strcmp(at, "null") || !strcmp(at, "zeroinitializer"));
}
static void find_aliases(propagation *p) {
  ir_analysis *a = &p->a;
  unsigned *heads = ir_allocate(p->c, a->definition_count * sizeof(unsigned));
  unsigned *tails = ir_allocate(p->c, a->definition_count * sizeof(unsigned));
  unsigned *next = ir_allocate(p->c, a->operand_count * sizeof(unsigned));
  for (unsigned k = 0; k < a->operand_count; k++)
    next[k] = a->operands[k].next_use;
  for (unsigned d = 0; d < a->definition_count; d++) {
    p->parents[d] = d;
    p->aliases[d] = IR_NONE;
    heads[d] = a->definitions[d].first_use;
    tails[d] = IR_NONE;
    for (unsigned k = heads[d]; k != IR_NONE; k = next[k]) {
      spend(p);
      tails[d] = k;
    }
    if (p->values[d].kind == CONSTANT)
      p->literals[d] = ir_format(p->c, "%" PRIu64, p->values[d].integer);
  }
  for (unsigned i = 0; i < a->instruction_count; i++)
    if (a->instructions[i].opcode == IR_OP_PHI)
      enqueue(p, i);
  while (p->count) {
    spend(p);
    unsigned i = p->queue[--p->count];
    p->queued[i] = 0;
    ir_instruction *in = &a->instructions[i];
    if (!p->blocks[in->block] || in->opcode != IR_OP_PHI ||
        (in->effects & IR_EFFECT_OPAQUE) ||
        (a->types[in->type].kind != IR_INT &&
         a->types[in->type].kind != IR_PTR) ||
        p->parents[in->definition] != in->definition ||
        p->aliases[in->definition] != IR_NONE)
      continue;
    unsigned representative = IR_NONE;
    int different = 0;
    for (unsigned k = in->first_operand;
         k < in->first_operand + in->operand_count; k += 2) {
      spend(p);
      if (!retained(p, p->phi_edges[k], a->operands[k + 1].block))
        continue;
      ir_operand op = canonical_operand(p, k);
      if (op.kind == IR_OPERAND_LOCAL && op.definition == in->definition)
        continue;
      const char *text = ir_text(a->text, op.token);
      if (!strcmp(text, "undef") || !strcmp(text, "poison"))
        different = 1;
      if (representative == IR_NONE)
        representative = k;
      else if (!same_operand(p, representative, k))
        different = 1;
    }
    if (different || representative == IR_NONE)
      continue;
    unsigned d = in->definition;
    ir_operand op = canonical_operand(p, representative);
    for (unsigned k = heads[d]; k != IR_NONE; k = next[k]) {
      spend(p);
      if (a->instructions[a->operands[k].instruction].opcode == IR_OP_PHI)
        enqueue(p, a->operands[k].instruction);
    }
    if (op.kind == IR_OPERAND_LOCAL) {
      unsigned r = op.definition;
      p->parents[d] = r;
      if (heads[d] != IR_NONE) {
        if (tails[r] == IR_NONE)
          heads[r] = heads[d];
        else
          next[tails[r]] = heads[d];
        tails[r] = tails[d];
      }
    } else
      p->aliases[d] = canonical_index(p, representative);
  }
  for (unsigned d = 0; d < a->definition_count; d++) {
    unsigned r = root(p, d);
    if (p->aliases[r] != IR_NONE)
      p->resolved[d] = ir_text(a->text, a->operands[p->aliases[r]].token);
    else if (r != d)
      p->resolved[d] = p->literals[r] ? p->literals[r] : a->definitions[r].name;
  }
}
static void erase(propagation *p, ir_instruction *in) {
  for (unsigned l = in->line - 1; l < in->end_line; l++)
    p->a.text->lines[l].replacement = "";
}
static void rewrite(propagation *p) {
  ir_analysis *a = &p->a;
  ir_module *m = a->text;
  for (unsigned b = 0; b < a->block_count; b++) {
    ir_block *block = &a->blocks[b];
    ir_instruction *term = &a->instructions[block->first_instruction +
                                            block->instruction_count - 1];
    p->selected[b] = IR_NONE;
    if (p->blocks[b] &&
        (term->opcode == IR_OP_BR || term->opcode == IR_OP_SWITCH))
      p->selected[b] = selected_edge(p, term);
  }
  find_aliases(p);
  for (unsigned k = 0; k < a->operand_count; k++) {
    spend(p);
    ir_operand *op = &a->operands[k];
    if (op->kind != IR_OPERAND_LOCAL)
      continue;
    const char *literal = p->literals[op->definition]
                              ? p->literals[op->definition]
                              : p->resolved[op->definition];
    if (literal) {
      m->tokens[op->token].replacement = literal;
      ir_note(p->c, "propagated", "sccp_operand", 1);
    }
  }
  for (unsigned b = 0; b < a->block_count; b++) {
    ir_block *block = &a->blocks[b];
    if (!p->blocks[b]) {
      m->lines[block->line - 1].replacement = "";
      for (unsigned i = block->first_instruction;
           i < block->first_instruction + block->instruction_count; i++)
        erase(p, &a->instructions[i]);
      ir_note(p->c, "removed", "unreachable_block", 1);
      ir_note(p->c, "removed", "CFG_edge", block->successor_count);
      continue;
    }
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(p);
      ir_instruction *in = &a->instructions[i];
      ir_line *line = &m->lines[in->line - 1];
      if (in->definition != IR_NONE &&
          (p->literals[in->definition] || p->resolved[in->definition])) {
        erase(p, in);
        ir_note(p->c, "removed",
                in->opcode == IR_OP_PHI ? "constant_or_trivial_PHI"
                                        : "constant_definition",
                1);
      } else if (in->opcode == IR_OP_PHI) {
        unsigned kept = 0;
        for (unsigned k = in->first_operand;
             k < in->first_operand + in->operand_count; k += 2)
          kept += retained(p, p->phi_edges[k], a->operands[k + 1].block);
        if (!kept)
          ir_error(p->c, "SCCP reached PHI without incoming edges");
        if (kept * 2 == in->operand_count)
          continue;
        p->c->size = 0;
        unsigned first = a->operands[in->first_operand].token;
        ir_append(p->c, m->source + line->begin,
                  m->tokens[first - 1].begin - line->begin);
        unsigned emitted = 0;
        for (unsigned k = in->first_operand;
             k < in->first_operand + in->operand_count; k += 2) {
          if (!retained(p, p->phi_edges[k], a->operands[k + 1].block))
            continue;
          ir_emit(p->c, "%s[ %s, %s ]", emitted++ ? ", " : "",
                  ir_text(m, a->operands[k].token),
                  ir_text(m, a->operands[k + 1].token));
        }
        unsigned last =
            a->operands[in->first_operand + in->operand_count - 1].token;
        size_t suffix = m->tokens[last + 1].end;
        ir_append(p->c, m->source + suffix, line->end - suffix);
        line->replacement = ir_copy(p->c, p->c->output, p->c->size);
        p->c->size = 0;
        ir_note(p->c, "removed", "PHI_incoming_edge",
                in->operand_count / 2 - kept);
      } else if ((in->opcode == IR_OP_BR || in->opcode == IR_OP_SWITCH) &&
                 p->selected[b] != IR_NONE &&
                 (in->opcode == IR_OP_SWITCH || in->operand_count != 1)) {
        erase(p, in);
        line->replacement =
            ir_format(p->c, "  br label %%%s\n",
                      a->blocks[a->successors[p->selected[b]]].name);
        ir_note(p->c, "transformed", "constant_terminator", 1);
        ir_note(p->c, "removed", "CFG_edge", block->successor_count - 1);
      }
    }
  }
}
void ir_sccp(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  for (unsigned k = 0; k < m->count; k++)
    if (!strcmp(ir_text(m, k), "blockaddress")) {
      ir_note(c, "skipped", "blockaddress_module", 1);
      return;
    }
  propagation p = {.c = c, .work = IR_SCCP_WORK_LIMIT};
  ir_analyze(c, m, &p.a);
  ir_analysis *a = &p.a;
#define ALLOC(member, n) p.member = ir_allocate(c, (n) * sizeof(*p.member))
  ALLOC(values, a->definition_count);
  ALLOC(blocks, a->block_count);
  ALLOC(edges, a->successor_count);
  ALLOC(queued, a->instruction_count);
  ALLOC(queue, a->instruction_count);
  ALLOC(phi_edges, a->operand_count);
  ALLOC(selected, a->block_count);
  ALLOC(aliases, a->definition_count);
  ALLOC(parents, a->definition_count);
  ALLOC(literals, a->definition_count);
  ALLOC(resolved, a->definition_count);
#undef ALLOC
  index_phis(&p);
  for (unsigned d = 0; d < a->definition_count; d++)
    if (a->definitions[d].instruction == IR_NONE)
      p.values[d].kind = OVERDEFINED;
  for (unsigned f = 0; f < a->function_count; f++)
    if (!a->functions[f].declaration)
      activate_block(&p, a->functions[f].first_block);
  drain(&p);
  /* Bottom cycles cannot justify pruning: resolve them conservatively. */
  int changed;
  do {
    changed = 0;
    for (unsigned d = 0; d < a->definition_count; d++) {
      spend(&p);
      if (p.values[d].kind == UNKNOWN && a->definitions[d].block != IR_NONE &&
          p.blocks[a->definitions[d].block]) {
        update(&p, d, (value){OVERDEFINED, 0});
        changed = 1;
      }
    }
    drain(&p);
  } while (changed);
  rewrite(&p);
  ir_reparse(c, m, mark);
  ir_constant_fold(c, m);
  ir_reparse(c, m, mark);
  ir_dead_instructions(c, m);
  ir_reparse(c, m, mark);
}
