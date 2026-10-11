#include "ir_promotion.h"
#include <inttypes.h>
#include <string.h>

typedef struct frontier {
  unsigned block;
  struct frontier *next;
} frontier;
typedef struct phi phi;
typedef struct phi_use {
  phi *consumer;
  struct phi_use *next;
} phi_use;
typedef struct {
  unsigned parent, type;
  const char *text, *key;
  phi_use *users;
} value;
struct phi {
  unsigned slot, block, id, type, instruction, queued;
  const char *name;
  unsigned *incoming;
  phi *next_block, *next_slot, *next;
};
typedef struct {
  unsigned instruction, definition, type, function, first_access;
  phi *phis;
} slot;
typedef struct {
  unsigned block, child, current, entered;
} frame;
typedef struct {
  ir_context *c;
  ir_analysis a;
  slot *slots;
  unsigned slot_count, generated_count, phi_count, base, undef_base;
  phi *phis, **block_phis, **slot_phi;
  frontier **frontiers;
  unsigned *child, *sibling, *work, *stamp, *first_load, *first_store;
  unsigned *live, *defs, *queued, *end_value;
  unsigned *first_access, *next_access;
  frame *stack;
  value *values;
  uint64_t budget;
} promotion;

static void spend(promotion *p) {
  if (!p->budget--)
    ir_error(p->c, "mem2reg work limit exceeded");
}
static unsigned operand_value(const promotion *p, unsigned k) {
  return p->a.operands[k].kind == IR_OPERAND_LOCAL ? p->a.operands[k].definition
                                                   : p->a.definition_count + k;
}
static unsigned root(promotion *p, unsigned v) {
  unsigned r = v;
  while (p->values[r].parent != r) {
    spend(p);
    r = p->values[r].parent;
  }
  while (p->values[v].parent != v) {
    unsigned next = p->values[v].parent;
    p->values[v].parent = r;
    v = next;
  }
  return r;
}
static int equal(const promotion *p, unsigned x, unsigned y) {
  return x == y ||
         (p->values[x].type == p->values[y].type && p->values[x].key &&
          p->values[y].key && !strcmp(p->values[x].key, p->values[y].key));
}
static phi *add_phi(promotion *p, unsigned block, unsigned type,
                    unsigned instruction, unsigned s) {
  if (p->phi_count == 1000000)
    ir_error(p->c, "mem2reg PHI limit exceeded");
  phi *q = ir_allocate(p->c, sizeof(*q));
  q->block = block;
  q->type = type;
  q->slot = s;
  q->instruction = instruction;
  if (instruction == IR_NONE) {
    q->id = p->base + p->generated_count++;
    q->name = ir_fresh(p->c, p->a.text, '%');
    q->next_block = p->block_phis[block];
    p->block_phis[block] = q;
    q->next_slot = p->slots[s].phis;
    p->slots[s].phis = q;
    p->slot_phi[block] = q;
    ir_note(p->c, "transformed", "phi_inserted", 1);
  } else {
    q->id = p->a.instructions[instruction].definition;
    q->name = p->a.definitions[q->id].name;
  }
  q->incoming = ir_allocate(p->c, (size_t)p->a.blocks[block].predecessor_count *
                                      sizeof(*q->incoming));
  q->next = p->phis;
  p->phis = q;
  p->phi_count++;
  return q;
}
static void graphs(promotion *p) {
  ir_analysis *a = &p->a;
  unsigned *seen = ir_allocate(p->c, (size_t)a->block_count * sizeof(*seen));
  for (unsigned b = 0; b < a->block_count; b++) {
    p->child[b] = p->sibling[b] = seen[b] = IR_NONE;
  }
  for (unsigned b = 0; b < a->block_count; b++) {
    ir_block *block = &a->blocks[b];
    if (block->idom == IR_NONE)
      continue;
    if (block->idom != b) {
      p->sibling[b] = p->child[block->idom];
      p->child[block->idom] = b;
    }
    if (block->predecessor_count < 2)
      continue;
    for (unsigned k = 0; k < block->predecessor_count; k++) {
      unsigned from = a->predecessors[block->first_predecessor + k];
      if (a->blocks[from].idom == IR_NONE)
        continue;
      while (from != block->idom && seen[from] != b) {
        spend(p);
        seen[from] = b;
        frontier *f = ir_allocate(p->c, sizeof(*f));
        *f = (frontier){b, p->frontiers[from]};
        p->frontiers[from] = f;
        from = a->blocks[from].idom;
      }
    }
  }
}
static void place(promotion *p, unsigned s) {
  ir_analysis *a = &p->a;
  slot *v = &p->slots[s];
  unsigned tag = s + 1, count = 0;
  ir_function *function = &a->functions[v->function];
  for (unsigned k = a->definitions[v->definition].first_use; k != IR_NONE;
       k = a->operands[k].next_use) {
    spend(p);
    unsigned i = a->operands[k].instruction, b = a->instructions[i].block;
    if (p->stamp[b] != tag) {
      p->stamp[b] = tag;
      p->first_load[b] = p->first_store[b] = IR_NONE;
    }
    unsigned *first = a->instructions[i].opcode == IR_OP_LOAD
                          ? &p->first_load[b]
                          : &p->first_store[b];
    if (i < *first)
      *first = i;
    if (a->instructions[i].opcode == IR_OP_STORE)
      p->defs[b] = tag;
  }
  for (unsigned b = function->first_block;
       b < function->first_block + function->block_count; b++) {
    spend(p);
    if (p->stamp[b] == tag && p->first_load[b] < p->first_store[b]) {
      p->live[b] = tag;
      p->work[count++] = b;
    }
  }
  for (unsigned at = 0; at < count; at++) {
    ir_block *b = &a->blocks[p->work[at]];
    for (unsigned k = 0; k < b->predecessor_count; k++) {
      spend(p);
      unsigned pred = a->predecessors[b->first_predecessor + k];
      if (a->blocks[pred].rpo == IR_NONE || p->live[pred] == tag ||
          p->defs[pred] == tag)
        continue;
      p->live[pred] = tag;
      p->work[count++] = pred;
    }
  }
  count = 0;
  for (unsigned b = function->first_block;
       b < function->first_block + function->block_count; b++) {
    spend(p);
    if (p->defs[b] == tag) {
      p->queued[b] = tag;
      p->work[count++] = b;
    }
  }
  for (unsigned at = 0; at < count; at++) {
    for (frontier *f = p->frontiers[p->work[at]]; f; f = f->next) {
      spend(p);
      unsigned b = f->block;
      if (p->live[b] != tag || (p->slot_phi[b] && p->slot_phi[b]->slot == s))
        continue;
      add_phi(p, b, v->type, IR_NONE, s);
      if (p->queued[b] != tag) {
        p->queued[b] = tag;
        p->work[count++] = b;
      }
    }
  }
}
static void rename_slot(promotion *p, unsigned s) {
  ir_analysis *a = &p->a;
  slot *v = &p->slots[s];
  unsigned undef = p->undef_base + v->type, depth = 1;
  for (phi *q = v->phis; q; q = q->next_slot)
    p->slot_phi[q->block] = q;
  ir_function *function = &a->functions[v->function];
  for (unsigned b = function->first_block;
       b < function->first_block + function->block_count; b++) {
    spend(p);
    p->first_access[b] = IR_NONE;
  }
  for (unsigned i = v->first_access; i != IR_NONE; i = p->next_access[i]) {
    unsigned b = a->instructions[i].block;
    if (p->first_access[b] == IR_NONE)
      p->first_access[b] = i;
  }
  p->stack[0] =
      (frame){.block = a->functions[v->function].first_block, .current = undef};
  while (depth) {
    frame *f = &p->stack[depth - 1];
    if (!f->entered) {
      spend(p);
      phi *q = p->slot_phi[f->block];
      if (q && q->slot == s)
        f->current = q->id;
      for (unsigned i = p->first_access[f->block];
           i != IR_NONE && a->instructions[i].block == f->block;
           i = p->next_access[i]) {
        spend(p);
        const ir_instruction *in = &a->instructions[i];
        if (in->opcode == IR_OP_STORE) {
          f->current = operand_value(p, in->first_operand);
          ir_note(p->c, "transformed", "store_removed", 1);
        } else {
          p->values[in->definition].parent = f->current;
          ir_note(p->c, "transformed", "load_removed", 1);
          if (f->current == undef)
            ir_note(p->c, "propagated", "uninitialized_undef", 1);
        }
        ir_promotion_erase(a->text, in);
      }
      p->end_value[f->block] = f->current;
      f->entered = 1;
      f->child = p->child[f->block];
    }
    if (f->child == IR_NONE) {
      depth--;
      continue;
    }
    unsigned child = f->child;
    f->child = p->sibling[child];
    p->stack[depth++] = (frame){.block = child, .current = f->current};
  }
  for (phi *q = v->phis; q; q = q->next_slot) {
    ir_block *b = &a->blocks[q->block];
    for (unsigned k = 0; k < b->predecessor_count; k++) {
      spend(p);
      unsigned pred = a->predecessors[b->first_predecessor + k];
      q->incoming[k] =
          a->blocks[pred].rpo == IR_NONE ? undef : p->end_value[pred];
    }
  }
  ir_promotion_erase(a->text, &a->instructions[v->instruction]);
  ir_note(p->c, "transformed", "promoted_slot", 1);
}
static void queue(promotion *p, phi **work, unsigned *count, phi *q) {
  if (!q->queued && p->values[q->id].parent == q->id) {
    if (*count == p->phi_count)
      ir_error(p->c, "mem2reg PHI worklist limit exceeded");
    work[(*count)++] = q;
    q->queued = 1;
  }
}
static void cleanup(promotion *p) {
  phi **work = ir_allocate(p->c, (size_t)p->phi_count * sizeof(*work));
  unsigned count = 0;
  for (phi *q = p->phis; q; q = q->next) {
    queue(p, work, &count, q);
    ir_block *b = &p->a.blocks[q->block];
    for (unsigned k = 0; k < b->predecessor_count; k++) {
      spend(p);
      unsigned r = root(p, q->incoming[k]);
      phi_use *use = ir_allocate(p->c, sizeof(*use));
      *use = (phi_use){q, p->values[r].users};
      p->values[r].users = use;
    }
  }
  while (count) {
    spend(p);
    phi *q = work[--count];
    q->queued = 0;
    unsigned replacement = IR_NONE;
    int trivial = 1;
    for (unsigned k = 0; k < p->a.blocks[q->block].predecessor_count; k++) {
      spend(p);
      unsigned r = root(p, q->incoming[k]);
      if (r == q->id)
        continue;
      if (replacement == IR_NONE)
        replacement = r;
      else if (!equal(p, replacement, r)) {
        trivial = 0;
        break;
      }
    }
    if (!trivial)
      continue;
    if (replacement == IR_NONE)
      replacement = p->undef_base + q->type;
    p->values[q->id].parent = replacement;
    if (q->instruction != IR_NONE)
      ir_promotion_erase(p->a.text, &p->a.instructions[q->instruction]);
    ir_note(p->c, "transformed",
            q->instruction == IR_NONE ? "generated_phi_removed"
                                      : "existing_phi_removed",
            1);
    phi_use *use = p->values[q->id].users;
    p->values[q->id].users = NULL;
    while (use) {
      spend(p);
      phi_use *next = use->next;
      queue(p, work, &count, use->consumer);
      use->next = p->values[replacement].users;
      p->values[replacement].users = use;
      use = next;
    }
  }
}
static void serialize(promotion *p) {
  ir_module *m = p->a.text;
  for (unsigned k = 0; k < p->a.operand_count; k++) {
    spend(p);
    ir_operand *op = &p->a.operands[k];
    if (op->kind != IR_OPERAND_LOCAL)
      continue;
    unsigned r = root(p, op->definition);
    if (r != op->definition) {
      m->tokens[op->token].replacement = p->values[r].text;
      ir_note(p->c, "propagated", "SSA_operand", 1);
    }
  }
  for (unsigned b = 0; b < p->a.block_count; b++) {
    if (!p->block_phis[b])
      continue;
    ir_line *line = &m->lines[p->a.blocks[b].line - 1];
    p->c->size = 0;
    ir_append(p->c, m->source + line->begin, line->end - line->begin);
    for (phi *q = p->block_phis[b]; q; q = q->next_block) {
      if (root(p, q->id) != q->id)
        continue;
      ir_note(p->c, "retained", "phi_definition", 1);
      ir_emit(p->c, "  %s = phi %s ", q->name, p->a.types[q->type].key);
      ir_block *block = &p->a.blocks[b];
      for (unsigned k = 0; k < block->predecessor_count; k++) {
        spend(p);
        unsigned pred = p->a.predecessors[block->first_predecessor + k];
        ir_emit(p->c, "%s[%s, %%%s]", k ? ", " : "",
                p->values[root(p, q->incoming[k])].text,
                p->a.blocks[pred].name);
      }
      ir_append(p->c, "\n", 1);
    }
    line->replacement = ir_copy(p->c, p->c->output, p->c->size);
  }
  p->c->size = 0;
}
void ir_mem2reg(ir_context *c, ir_module *m) {
  promotion p = {.c = c, .budget = 50000000};
  ir_analyze(c, m, &p.a);
  ir_analysis *a = &p.a;
  p.slots = ir_allocate(c, (size_t)a->definition_count * sizeof(*p.slots));
  for (unsigned i = 0; i < a->instruction_count; i++) {
    ir_instruction *in = &a->instructions[i];
    if (in->opcode != IR_OP_ALLOCA)
      continue;
    c->line = in->line;
    ir_note(c, "candidate", "stack_slot", 1);
    const char *reason = ir_scalar_slot_reason(a, i);
    if (reason)
      ir_note(c, "skipped", reason, 1);
    else
      p.slots[p.slot_count++] =
          (slot){.instruction = i,
                 .definition = in->definition,
                 .type = in->allocated_type,
                 .first_access = IR_NONE,
                 .function = a->blocks[in->block].function};
  }
  p.undef_base = a->definition_count + a->operand_count;
  p.base = p.undef_base + a->type_count;
  p.block_phis = ir_allocate(c, (size_t)a->block_count * sizeof(*p.block_phis));
  p.slot_phi = ir_allocate(c, (size_t)a->block_count * sizeof(*p.slot_phi));
  p.frontiers = ir_allocate(c, (size_t)a->block_count * sizeof(*p.frontiers));
  p.child = ir_allocate(c, (size_t)a->block_count * sizeof(*p.child));
  p.sibling = ir_allocate(c, (size_t)a->block_count * sizeof(*p.sibling));
  p.work = ir_allocate(c, (size_t)a->block_count * sizeof(*p.work));
  p.stamp = ir_allocate(c, (size_t)a->block_count * sizeof(*p.stamp));
  p.first_load = ir_allocate(c, (size_t)a->block_count * sizeof(*p.first_load));
  p.first_store =
      ir_allocate(c, (size_t)a->block_count * sizeof(*p.first_store));
  p.live = ir_allocate(c, (size_t)a->block_count * sizeof(*p.live));
  p.defs = ir_allocate(c, (size_t)a->block_count * sizeof(*p.defs));
  p.queued = ir_allocate(c, (size_t)a->block_count * sizeof(*p.queued));
  p.end_value = ir_allocate(c, (size_t)a->block_count * sizeof(*p.end_value));
  p.stack = ir_allocate(c, (size_t)a->block_count * sizeof(*p.stack));
  p.first_access =
      ir_allocate(c, (size_t)a->block_count * sizeof(*p.first_access));
  p.next_access =
      ir_allocate(c, (size_t)a->instruction_count * sizeof(*p.next_access));
  unsigned *owner =
      ir_allocate(c, (size_t)a->definition_count * sizeof(*owner));
  for (unsigned d = 0; d < a->definition_count; d++)
    owner[d] = IR_NONE;
  for (unsigned s = 0; s < p.slot_count; s++)
    owner[p.slots[s].definition] = s;
  for (unsigned i = a->instruction_count; i-- > 0;) {
    ir_instruction *in = &a->instructions[i];
    if (in->opcode != IR_OP_LOAD && in->opcode != IR_OP_STORE)
      continue;
    ir_operand *op =
        &a->operands[in->first_operand + (in->opcode == IR_OP_STORE)];
    if (op->kind != IR_OPERAND_LOCAL || owner[op->definition] == IR_NONE)
      continue;
    slot *s = &p.slots[owner[op->definition]];
    p.next_access[i] = s->first_access;
    s->first_access = i;
  }
  graphs(&p);
  for (unsigned s = 0; s < p.slot_count; s++)
    place(&p, s);
  for (unsigned i = 0; i < a->instruction_count; i++) {
    ir_instruction *in = &a->instructions[i];
    ir_type_kind kind = a->types[in->type].kind;
    if (in->opcode == IR_OP_PHI && !in->effects &&
        (kind == IR_INT || kind == IR_PTR)) {
      phi *q = add_phi(&p, in->block, in->type, i, IR_NONE);
      for (unsigned k = 0; k < in->operand_count; k += 2)
        q->incoming[k / 2] = operand_value(&p, in->first_operand + k);
    }
  }
  unsigned count = p.base + p.generated_count;
  p.values = ir_allocate(c, (size_t)count * sizeof(*p.values));
  for (unsigned v = 0; v < count; v++)
    p.values[v].parent = v;
  for (unsigned d = 0; d < a->definition_count; d++) {
    p.values[d].type = a->definitions[d].type;
    p.values[d].text = a->definitions[d].name;
  }
  for (unsigned k = 0; k < a->operand_count; k++) {
    ir_operand *op = &a->operands[k];
    value *v = &p.values[a->definition_count + k];
    v->type = op->type;
    v->text = m->tokens[op->token].text;
    v->key = v->text;
    if (op->kind == IR_OPERAND_BLOCK)
      continue;
    uint64_t n;
    if (op->kind == IR_OPERAND_GLOBAL)
      v->key = ir_name_key(c, v->text);
    else if (a->types[v->type].kind == IR_INT &&
             (ir_integer(v->text, &n) || !strcmp(v->text, "zeroinitializer"))) {
      if (!strcmp(v->text, "zeroinitializer"))
        n = 0;
      unsigned bits = a->types[v->type].bits;
      if (bits < 64)
        n &= (UINT64_C(1) << bits) - 1;
      v->key = ir_format(c, "%" PRIu64, n);
    } else if (a->types[v->type].kind == IR_PTR &&
               !strcmp(v->text, "zeroinitializer"))
      v->key = "null";
  }
  for (unsigned t = 0; t < a->type_count; t++)
    p.values[p.undef_base + t] = (value){
        .parent = p.undef_base + t, .type = t, .text = "undef", .key = "undef"};
  for (phi *q = p.phis; q; q = q->next) {
    p.values[q->id].type = q->type;
    p.values[q->id].text = q->name;
  }
  for (unsigned s = 0; s < p.slot_count; s++)
    rename_slot(&p, s);
  cleanup(&p);
  serialize(&p);
}
