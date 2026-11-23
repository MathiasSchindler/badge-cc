#include "ir_analysis.h"
#include "ir_integer.h"
#include "target_cost.h"
#include "ir_loop_admission.h"
#include <inttypes.h>
#include <string.h>

#ifndef IR_LOOP_WORK_LIMIT
#define IR_LOOP_WORK_LIMIT UINT64_C(50000000)
#endif

typedef struct motion {
  unsigned instruction;
  struct motion *next;
} motion;
typedef struct {
  ir_context *c;
  ir_analysis a;
  unsigned *order, *location, *queue;
  unsigned char *member;
  motion **first, **last;
  uint64_t work;
  unsigned moves;
  int cost_gated;
  int scalar_gated;
  int bounds_gated;
  int pointer_gated;
  int symbol_gated;
  char **extra, **phis;
  unsigned char *removed;
} loops;

static void spend(loops *p, uint64_t n) {
  if (n > p->work)
    ir_error(p->c, p->bounds_gated ? "loop-bounds work limit exceeded" :
                   p->pointer_gated ? "pointer-induction work limit exceeded" :
                   p->scalar_gated ? "loop-scalar work limit exceeded" :
                   p->cost_gated ? "loop-cost work limit exceeded"
                              : "loop-invariant work limit exceeded");
  p->work -= n;
}
static int dominates(loops *p, unsigned definition, unsigned use) {
  /* Charge the shared query's worst-case dominator-chain traversal. */
  spend(p, p->a.functions[p->a.blocks[use].function].block_count);
  return ir_dominates(&p->a, definition, use);
}
static int literal(loops *p, const ir_operand *op, uint64_t *value) {
  return op->kind == IR_OPERAND_LITERAL &&
         ir_integer(ir_text(p->a.text, op->token), value);
}
static int eligible(loops *p, const ir_instruction *in) {
  ir_analysis *a = &p->a;
  ir_module *m = a->text;
  const ir_line *line = &m->lines[in->line - 1];
  if (in->definition == IR_NONE || in->end_line != in->line ||
      (in->effects & ~IR_EFFECT_TRAP))
    return 0;
  if (in->opcode != IR_OP_BINARY && in->opcode != IR_OP_COMPARE &&
      in->opcode != IR_OP_CAST && in->opcode != IR_OP_GEP)
    return 0;
  if (p->scalar_gated) {
    const ir_type *type = &a->types[in->type];
    if (in->opcode == IR_OP_COMPARE ||
        (type->kind != IR_PTR &&
         (type->kind != IR_INT || !type->bits || type->bits > 32)))
      return 0;
    if (in->opcode == IR_OP_BINARY &&
        (!strcmp(in->operation, "udiv") || !strcmp(in->operation, "urem") ||
         !strcmp(in->operation, "sdiv") || !strcmp(in->operation, "srem")))
      return 0;
  }
  for (unsigned k = line->op + 1; k < line->first + line->count; k++) {
    spend(p, 1);
    const char *s = ir_text(m, k);
    if (!strcmp(s, "nsw") || !strcmp(s, "nuw") || !strcmp(s, "exact") ||
        !strcmp(s, "disjoint") || !strcmp(s, "inbounds") || *s == '!')
      return 0;
  }
  for (unsigned k = in->first_operand;
       k < in->first_operand + in->operand_count; k++) {
    spend(p, 1);
    const ir_operand *op = &a->operands[k];
    if (op->kind == IR_OPERAND_LITERAL) {
      uint64_t unused;
      const char *s = ir_text(m, op->token);
      if (!ir_integer(s, &unused) && strcmp(s, "null") && strcmp(s, "true") &&
          strcmp(s, "false") && strcmp(s, "zeroinitializer"))
        return 0;
    }
  }
  if (in->opcode != IR_OP_BINARY)
    return !in->effects;
  if (in->operand_count != 2 || line->count != 7)
    return 0;
  const char *op = in->operation;
  const ir_operand *left = &a->operands[in->first_operand];
  const ir_operand *right = left + 1;
  unsigned bits = a->types[in->type].bits;
  uint64_t n;
  if (!strcmp(op, "shl") || !strcmp(op, "lshr") || !strcmp(op, "ashr"))
    return literal(p, right, &n) && n < bits;
  if (!strcmp(op, "udiv") || !strcmp(op, "urem") || !strcmp(op, "sdiv") ||
      !strcmp(op, "srem")) {
    if (bits != 32 || !literal(p, right, &n))
      return 0;
    n &= ir_integer_mask(bits);
    if (!n)
      return 0;
    if (*op == 's' && n == ir_integer_mask(bits)) {
      uint64_t x;
      if (!literal(p, left, &x) ||
          (x & ir_integer_mask(bits)) == (UINT64_C(1) << (bits - 1)))
        return 0;
    }
    return 1;
  }
  return !strcmp(op, "add") || !strcmp(op, "sub") || !strcmp(op, "mul") ||
         !strcmp(op, "and") || !strcmp(op, "or") || !strcmp(op, "xor");
}
static int available(loops *p, const ir_instruction *in, unsigned preheader) {
  for (unsigned k = in->first_operand;
       k < in->first_operand + in->operand_count; k++) {
    spend(p, 1);
    const ir_operand *op = &p->a.operands[k];
    if (op->kind == IR_OPERAND_BLOCK)
      return 0;
    if (op->kind != IR_OPERAND_LOCAL)
      continue;
    const ir_definition *d = &p->a.definitions[op->definition];
    if (d->instruction == IR_NONE)
      continue;
    unsigned b = p->location[d->instruction];
    if (p->member[b] || !dominates(p, b, preheader))
      return 0;
  }
  return 1;
}
static int costly(loops *p, const ir_instruction *in) {
  if (in->opcode != IR_OP_BINARY || in->operand_count != 2 ||
      p->a.types[in->type].kind != IR_INT ||
      p->a.types[in->type].bits != 32)
    return 0;
  const ir_operand *left = &p->a.operands[in->first_operand];
  const ir_operand *right = left + 1;
  uint64_t n;
  if (!strcmp(in->operation, "mul")) {
    if (!((left->kind == IR_OPERAND_LOCAL && literal(p, right, &n)) ||
          (right->kind == IR_OPERAND_LOCAL && literal(p, left, &n))))
      return 0;
  } else {
    if ((strcmp(in->operation, "udiv") && strcmp(in->operation, "urem") &&
         strcmp(in->operation, "sdiv") && strcmp(in->operation, "srem")) ||
        left->kind != IR_OPERAND_LOCAL || !literal(p, right, &n))
      return 0;
  }
  n &= UINT32_MAX;
  /* Exclude cheap negation and signed powers of two. */
  if ((!strcmp(in->operation, "mul") || *in->operation == 's') &&
      (n & UINT64_C(0x80000000)))
    n = UINT64_C(0x100000000) - n;
  return n && (n & (n - 1));
}
static int used_in_loop(loops *p, const ir_instruction *in) {
  if (in->definition == IR_NONE)
    return 0;
  for (unsigned u = p->a.definitions[in->definition].first_use; u != IR_NONE;
       u = p->a.operands[u].next_use) {
    spend(p, 1);
    if (p->member[p->location[p->a.operands[u].instruction]])
      return 1;
  }
  return 0;
}
static unsigned cost_candidate(loops *p, const ir_function *f,
                               unsigned preheader) {
  unsigned candidate = IR_NONE;
  for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++) {
    const ir_block *block = &p->a.blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(p, 1);
      const ir_instruction *in = &p->a.instructions[i];
      unsigned location = p->location[i];
      if (!costly(p, in) || !used_in_loop(p, in))
        continue;
      /* Include outer motion, so nested dependency chains stay bounded too. */
      if (!p->member[location]) {
        if (dominates(p, location, preheader)) {
          ir_note(p->c, "skipped", "cost_live_range_budget", 1);
          return IR_NONE;
        }
        continue;
      }
      if (!eligible(p, in) || !available(p, in, preheader))
        continue;
      if (candidate != IR_NONE) {
        ir_note(p->c, "skipped", "cost_live_range_budget", 1);
        return IR_NONE;
      }
      candidate = i;
    }
  }
  return candidate;
}
static void move(loops *p, unsigned i, unsigned preheader) {
  const ir_instruction *in = &p->a.instructions[i];
  p->c->line = in->line;
  spend(p, 1);
  if (!p->member[p->location[i]] || !eligible(p, in))
    return;
  ir_note(p->c, "candidate", "pure_loop_expression", 1);
  if (!available(p, in, preheader)) {
    ir_note(p->c, "skipped", "operand_not_available_in_preheader", 1);
    return;
  }
  ir_note(p->c, p->scalar_gated || p->bounds_gated ? "proposed" :
          p->location[i] == in->block ? "transformed" : "propagated",
          p->location[i] == in->block ? "invariant_hoisted" : "nested_motion",
          1);
  motion *q = ir_allocate(p->c, sizeof(*q));
  q->instruction = i;
  if (p->last[preheader])
    p->last[preheader]->next = q;
  else
    p->first[preheader] = q;
  p->last[preheader] = q;
  p->location[i] = preheader;
  p->moves++;
}

typedef struct write_call {
  const struct write_call *parent;
  const ir_instruction *site;
  unsigned function, depth;
} write_call;

static int private_storage(loops *p, unsigned global) {
  const ir_global *g = &p->a.globals[global];
  const ir_line *line = &p->a.text->lines[g->line - 1];
  return g->function == IR_NONE && line->count >= 6 &&
         (!strcmp(ir_text(p->a.text, line->first + 2), "internal") ||
          !strcmp(ir_text(p->a.text, line->first + 2), "private")) &&
         !strcmp(ir_text(p->a.text, line->first + 3), "global");
}
static int disjoint_pointer(loops *p, const ir_operand *op, unsigned global,
                            const write_call *call) {
  ir_analysis *a = &p->a;
  for (unsigned steps = 0; steps < 16; steps++) {
    spend(p, 1);
    if (op->kind == IR_OPERAND_GLOBAL)
      return op->definition != global && private_storage(p, op->definition);
    if (op->kind != IR_OPERAND_LOCAL)
      return 0;
    const ir_definition *d = &a->definitions[op->definition];
    if (d->instruction == IR_NONE) {
      if (!call)
        return 0;
      const ir_function *f = &a->functions[call->function];
      unsigned index = op->definition - f->first_definition;
      if (index >= f->parameter_count)
        return 0;
      op = &a->operands[call->site->first_operand + 1 + index];
      call = call->parent;
      continue;
    }
    const ir_instruction *in = &a->instructions[d->instruction];
    if (in->opcode == IR_OP_ALLOCA)
      return 1;
    if (in->opcode != IR_OP_GEP)
      return 0;
    op = &a->operands[in->first_operand];
  }
  return 0;
}

static int no_bound_write(loops *p, const ir_instruction *in, unsigned global,
                          const write_call *caller) {
  ir_analysis *a = &p->a;
  spend(p, 1);
  if (in->effects & (IR_EFFECT_OPAQUE | IR_EFFECT_VOLATILE))
    return 0;
  if (in->opcode == IR_OP_LOAD || in->opcode == IR_OP_STORE) {
    const ir_line *line = &a->text->lines[in->line - 1];
    for (unsigned k = line->op + 1; k < line->first + line->count; k++) {
      spend(p, 1);
      if (!strcmp(ir_text(a->text, k), "atomic"))
        return 0;
    }
  }
  if (!(in->effects & (IR_EFFECT_WRITE | IR_EFFECT_CALL)))
    return 1;
  if (in->opcode == IR_OP_STORE)
    return disjoint_pointer(p, &a->operands[in->first_operand + 1], global, caller);
  if (in->opcode != IR_OP_CALL || in->callee == IR_NONE ||
      a->operands[in->callee].kind != IR_OPERAND_GLOBAL)
    return 0;
  const ir_global *callee = &a->globals[a->operands[in->callee].definition];
  if ((!strcmp(callee->name, "@llvm.memcpy.inline.p0.p0.i32") ||
       !strcmp(callee->name, "@llvm.memset.inline.p0.i32")) &&
      in->operand_count == 5)
    return !strcmp(ir_text(a->text, a->operands[in->first_operand + 4].token), "false") &&
           disjoint_pointer(p, &a->operands[in->first_operand + 1], global, caller);
  unsigned target = callee->function;
  if (target == IR_NONE || a->functions[target].declaration ||
      (caller && caller->depth == 6))
    return 0;
  const ir_function *fn = &a->functions[target];
  const ir_line *line = &a->text->lines[fn->line - 1];
  if (strcmp(ir_text(a->text, line->first + 1), "internal") &&
      strcmp(ir_text(a->text, line->first + 1), "private"))
    return 0;
  for (const write_call *c = caller; c; c = c->parent)
    if (target == c->function)
      return 0;
  write_call call = {caller, in, target, caller ? caller->depth + 1 : 1};
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++) {
    const ir_block *block = &a->blocks[b];
    for (unsigned k = block->first_instruction; k < block->first_instruction + block->instruction_count; k++)
      if (!no_bound_write(p, &a->instructions[k], global, &call))
        return 0;
  }
  return 1;
}

static unsigned bound_global(loops *p, const ir_instruction *in) {
  ir_analysis *a = &p->a;
  if (in->opcode != IR_OP_LOAD || in->effects != (IR_EFFECT_READ | IR_EFFECT_TRAP) ||
      in->line != in->end_line || a->types[in->type].kind != IR_INT || a->types[in->type].bits != 32)
    return IR_NONE;
  const ir_line *line = &a->text->lines[in->line - 1];
  const ir_operand *op = &a->operands[in->first_operand];
  if (op->kind != IR_OPERAND_GLOBAL || line->count != 10 ||
      strcmp(ir_text(a->text, op->token + 2), "align") ||
      strcmp(ir_text(a->text, op->token + 3), "4"))
    return IR_NONE;
  if (!private_storage(p, op->definition))
    return IR_NONE;
  return op->definition;
}

static void bound_loop(loops *p, const ir_function *f, unsigned header, unsigned preheader) {
  ir_analysis *a = &p->a;
  const ir_block *h = &a->blocks[header];
  unsigned first = h->first_instruction, end = first + h->instruction_count;
  if (h->successor_count != 2 || h->instruction_count < 4 ||
      p->member[a->successors[h->first_successor]] ==
          p->member[a->successors[h->first_successor + 1]])
    return;
  while (first < end && a->instructions[first].opcode == IR_OP_PHI)
    first++;
  unsigned loads = end - first == 3 ? 1 : end - first == 5 ? 2 : 0;
  if (!loads)
    return;
  const ir_instruction *cmp = &a->instructions[end - 2], *term = cmp + 1;
  if (cmp->opcode != IR_OP_COMPARE || cmp->operand_count != 2 ||
      term->opcode != IR_OP_BR || term->operand_count != 3 ||
      a->operands[term->first_operand].kind != IR_OPERAND_LOCAL ||
      a->operands[term->first_operand].definition != cmp->definition)
    return;
  const ir_operand *induction = &a->operands[cmp->first_operand], *bound = induction + 1;
  const ir_instruction *product = loads == 2 ? &a->instructions[first + 2] : NULL;
  if (induction->kind != IR_OPERAND_LOCAL || bound->kind != IR_OPERAND_LOCAL ||
      a->definitions[induction->definition].instruction == IR_NONE ||
      a->instructions[a->definitions[induction->definition].instruction].opcode != IR_OP_PHI ||
      a->definitions[induction->definition].block != header ||
      !ir_type_equal(a, induction->type, a->instructions[first].type) ||
      bound->definition != (product ? product : &a->instructions[first])->definition)
    return;
  if (product) {
    const ir_operand *x = &a->operands[product->first_operand], *y = x + 1;
    if (product->opcode != IR_OP_BINARY || strcmp(product->operation, "mul") ||
        !eligible(p, product) || x->kind != IR_OPERAND_LOCAL || y->kind != IR_OPERAND_LOCAL ||
        x->definition != a->instructions[first].definition ||
        y->definition != a->instructions[first + 1].definition)
      return;
  }
  for (unsigned k = 0; k < loads; k++) {
    unsigned global = bound_global(p, &a->instructions[first + k]);
    if (global == IR_NONE)
      return;
    for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++) {
      spend(p, 1);
      if (!p->member[b])
        continue;
      const ir_block *block = &a->blocks[b];
      for (unsigned i = block->first_instruction; i < block->first_instruction + block->instruction_count; i++)
        if (!no_bound_write(p, &a->instructions[i], global, NULL)) {
          ir_note(p->c, "skipped", "loop_bound_write_not_excluded", 1);
          return;
        }
    }
  }
  for (unsigned i = first; i < end - 2; i++) {
    motion *q = ir_allocate(p->c, sizeof(*q));
    q->instruction = i;
    if (p->last[preheader])
      p->last[preheader]->next = q;
    else
      p->first[preheader] = q;
    p->last[preheader] = q;
    p->location[i] = preheader;
  }
  ir_note(p->c, "proposed", "private_global_loop_bound", 1);
  p->moves++;
}
static unsigned pressure(loops *p, const ir_function *f, int *calls) {
  unsigned count = 0;
  *calls = 0;
  for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++)
    if (p->member[b]) {
      const ir_block *block = &p->a.blocks[b];
      for (unsigned i = block->first_instruction;
           i < block->first_instruction + block->instruction_count; i++) {
        spend(p, 1);
        const ir_instruction *in = &p->a.instructions[i];
        if (in->effects & IR_EFFECT_OPAQUE) return IR_NONE;
        if (in->opcode == IR_OP_CALL) *calls = 1;
        if (in->opcode == IR_OP_PHI) count++;
      }
    }
  for (unsigned d = f->first_definition; d < f->first_definition + f->definition_count; d++) {
    const ir_definition *def = &p->a.definitions[d];
    spend(p, 1);
    if (def->instruction != IR_NONE && p->member[p->location[def->instruction]]) continue;
    for (unsigned u = def->first_use; u != IR_NONE; u = p->a.operands[u].next_use) {
      spend(p, 1);
      const ir_operand *operand = &p->a.operands[u];
      if (p->a.instructions[operand->instruction].opcode == IR_OP_PHI &&
          u + 1 < p->a.operand_count &&
          p->a.operands[u + 1].kind == IR_OPERAND_BLOCK &&
          !p->member[p->a.operands[u + 1].block]) continue;
      if (p->member[p->location[p->a.operands[u].instruction]]) {
        count++;
        break;
      }
    }
  }
  return count;
}
static void append_text(loops *p, char **out, const char *text) {
  *out = ir_format(p->c, "%s%s", *out ? *out : "", text);
}
static int induction(loops *p, const ir_function *f, unsigned header,
                     unsigned preheader) {
  ir_analysis *a = &p->a;
  const ir_block *h = &a->blocks[header];
  if (h->predecessor_count != 2) return 0;
  unsigned latch = a->predecessors[h->first_predecessor];
  if (latch == preheader) latch = a->predecessors[h->first_predecessor + 1];
  if (!p->member[latch]) return 0;
  for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++) {
    if (!p->member[b]) continue;
    const ir_block *block = &a->blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(p, 1);
      const ir_instruction *in = &a->instructions[i];
      if (p->removed[i] || p->location[i] != in->block || !eligible(p, in) ||
          in->opcode != IR_OP_BINARY || strcmp(in->operation, "mul") ||
          a->types[in->type].bits != 32 || in->operand_count != 2)
        continue;
      const ir_operand *iv = &a->operands[in->first_operand], *factor = iv + 1;
      uint64_t scale;
      if (iv->kind != IR_OPERAND_LOCAL || !literal(p, factor, &scale)) continue;
      scale &= UINT32_MAX;
      if (scale < 2 || !dominates(p, in->block, latch)) continue;
      const ir_definition *d = &a->definitions[iv->definition];
      if (d->instruction == IR_NONE || d->block != header) continue;
      const ir_instruction *phi = &a->instructions[d->instruction];
      if (phi->opcode != IR_OP_PHI || phi->operand_count != 4) continue;
      const ir_operand *start = NULL, *back = NULL;
      for (unsigned k = phi->first_operand; k < phi->first_operand + 4; k += 2) {
        const ir_operand *value = &a->operands[k], *edge = value + 1;
        if (edge->kind != IR_OPERAND_BLOCK) continue;
        if (edge->block == preheader) start = value;
        if (edge->block == latch) back = value;
      }
      if (!start || !back || back->kind != IR_OPERAND_LOCAL ||
          (start->kind != IR_OPERAND_LOCAL && start->kind != IR_OPERAND_LITERAL)) continue;
      uint64_t initial;
      if (start->kind == IR_OPERAND_LITERAL && !literal(p, start, &initial)) continue;
      unsigned update = a->definitions[back->definition].instruction;
      if (update == IR_NONE) continue;
      const ir_instruction *step = &a->instructions[update];
      if (step->block != latch || !eligible(p, step) || step->operand_count != 2 ||
          (strcmp(step->operation, "add") && strcmp(step->operation, "sub"))) continue;
      const ir_operand *left = &a->operands[step->first_operand], *right = left + 1;
      uint64_t increment;
      if (left->kind != IR_OPERAND_LOCAL || left->definition != iv->definition ||
          !literal(p, right, &increment)) continue;
      uint32_t delta = (uint32_t)increment * (uint32_t)scale;
      if (!strcmp(step->operation, "sub")) delta = 0u - delta;
      badge_cost_range cost = badge_target_add_bytes(8, 8, delta, 0, 0);
      if (!cost.known || cost.max > 4 || !delta) {
        ir_note(p->c, "skipped", "induction_step_rematerialization_cheaper", 1);
        continue;
      }
      const char *seed;
      if (start->kind == IR_OPERAND_LITERAL)
        seed = ir_format(p->c, "%u", (uint32_t)initial * (uint32_t)scale);
      else {
        seed = ir_fresh(p->c, a->text, '%');
        append_text(p, &p->extra[preheader], ir_format(p->c,
            "  %s = mul i32 %s, %llu\n", seed, ir_text(a->text, start->token),
            (unsigned long long)scale));
      }
      const char *next = ir_fresh(p->c, a->text, '%');
      const char *name = a->definitions[in->definition].name;
      append_text(p, &p->phis[header], ir_format(p->c,
          "  %s = phi i32 [%s, %%%s], [%s, %%%s]\n", name, seed,
          a->blocks[preheader].name, next, a->blocks[latch].name));
      append_text(p, &p->extra[latch], ir_format(p->c,
          "  %s = add i32 %s, %u\n", next, name, delta));
      p->removed[i] = 1;
      if (d->use_count == 2 && a->definitions[step->definition].use_count == 1) {
        p->removed[d->instruction] = 1;
        p->removed[update] = 1;
      }
      p->moves++;
      ir_note(p->c, "proposed", "modular_affine_induction", 1);
      return 1;
    }
  }
  return 0;
}
static void scalar_loop(loops *p, const ir_function *f, unsigned header,
                        unsigned preheader) {
  int calls;
  unsigned live = pressure(p, f, &calls);
  if (live == IR_NONE || live > (calls ? 2u : 3u)) {
    ir_note(p->c, "skipped", live == IR_NONE ? "opaque_loop_effect" : "scalar_live_range_budget", 1);
    return;
  }
  const ir_block *pre = &p->a.blocks[preheader];
  for (unsigned i = pre->first_instruction; i < pre->first_instruction + pre->instruction_count; i++) {
    spend(p, 1);
    if (eligible(p, &p->a.instructions[i]) && used_in_loop(p, &p->a.instructions[i])) {
      ir_note(p->c, "skipped", "existing_preheader_scalar_budget", 1);
      return;
    }
  }
  unsigned candidate = IR_NONE, best = 0;
  for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++) {
    if (!p->member[b]) continue;
    const ir_block *block = &p->a.blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(p, 1);
      const ir_instruction *in = &p->a.instructions[i];
      if (p->location[i] != in->block || !eligible(p, in) ||
          !used_in_loop(p, in) || !available(p, in, preheader)) continue;
      unsigned score = in->opcode == IR_OP_GEP ? 4 :
                       in->opcode == IR_OP_CAST ? 1 :
                       !strcmp(in->operation, "mul") ? 3 : 2;
      if (score > best) { candidate = i; best = score; }
    }
  }
  if (candidate != IR_NONE) {
    ir_note(p->c, "candidate", "bounded_scalar_motion", 1);
    move(p, candidate, preheader);
  } else if (!calls) induction(p, f, header, preheader);
  else ir_note(p->c, "skipped", "call_crossing_induction_budget", 1);
}
static void pointer_induction(loops *p, const ir_function *f, unsigned header,
                              unsigned preheader) {
  ir_analysis *a = &p->a;
  const ir_block *h = &a->blocks[header];
  if (h->predecessor_count != 2)
    return;
  unsigned latch = a->predecessors[h->first_predecessor];
  if (latch == preheader)
    latch = a->predecessors[h->first_predecessor + 1];
  if (!p->member[latch])
    return;
  for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++) {
    if (!p->member[b])
      continue;
    const ir_block *block = &a->blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(p, 1);
      const ir_instruction *gep = &a->instructions[i];
      const ir_line *line = &a->text->lines[gep->line - 1];
      if (gep->opcode != IR_OP_GEP || p->removed[i] || gep->operand_count != 2 ||
          gep->end_line != gep->line || line->count != 10 ||
          strcmp(ir_text(a->text, line->op + 1), "i8") ||
          !dominates(p, b, latch))
        continue;
      const ir_operand *base = &a->operands[gep->first_operand], *index = base + 1;
      if (base->kind != IR_OPERAND_GLOBAL || index->kind != IR_OPERAND_LOCAL ||
          a->types[index->type].bits != 32)
        continue;
      unsigned scale_instruction = a->definitions[index->definition].instruction;
      const ir_operand *iv = index;
      uint64_t scale = 1;
      if (scale_instruction != IR_NONE) {
        const ir_instruction *expr = &a->instructions[scale_instruction];
        if (expr->opcode == IR_OP_BINARY && !strcmp(expr->operation, "mul") &&
            eligible(p, expr) && literal(p, &a->operands[expr->first_operand + 1], &scale))
          iv = &a->operands[expr->first_operand];
      }
      if (!scale || scale > 64 || iv->kind != IR_OPERAND_LOCAL)
        continue;
      const ir_definition *def = &a->definitions[iv->definition];
      if (def->block != header || def->instruction == IR_NONE)
        continue;
      const ir_instruction *phi = &a->instructions[def->instruction];
      if (phi->opcode != IR_OP_PHI || phi->operand_count != 4)
        continue;
      const ir_operand *seed = NULL, *back = NULL;
      for (unsigned k = 0; k < 4; k += 2) {
        const ir_operand *op = &a->operands[phi->first_operand + k];
        if (op[1].block == preheader) seed = op;
        if (op[1].block == latch) back = op;
      }
      uint64_t zero;
      if (!seed || !literal(p, seed, &zero) || zero || !back ||
          back->kind != IR_OPERAND_LOCAL)
        continue;
      unsigned update = a->definitions[back->definition].instruction;
      if (update == IR_NONE)
        continue;
      const ir_instruction *step = &a->instructions[update];
      if (step->block != latch || step->opcode != IR_OP_BINARY ||
          strcmp(step->operation, "add") || !eligible(p, step))
        continue;
      const ir_operand *left = &a->operands[step->first_operand];
      uint64_t delta;
      if (left->kind != IR_OPERAND_LOCAL || left->definition != iv->definition ||
          !literal(p, left + 1, &delta) || delta != 1)
        continue;
      const char *next = ir_fresh(p->c, a->text, '%');
      const char *name = a->definitions[gep->definition].name;
      append_text(p, &p->phis[header], ir_format(p->c,
          "  %s = phi ptr [%s, %%%s], [%s, %%%s]\n", name,
          ir_text(a->text, base->token), a->blocks[preheader].name,
          next, a->blocks[latch].name));
      append_text(p, &p->extra[latch], ir_format(p->c,
          "  %s = getelementptr i8, ptr %s, i32 %u\n", next, name, (unsigned)scale));
      p->removed[i] = 1;
      if (iv != index && a->definitions[index->definition].use_count == 1)
        p->removed[scale_instruction] = 1;
      p->moves++;
      ir_note(p->c, "proposed", "byte_pointer_recurrence", 1);
      return;
    }
  }
}

static const ir_operand *indexed_global(loops *p, unsigned i) {
  ir_analysis *a = &p->a;
  const ir_instruction *in = &a->instructions[i];
  if (in->opcode != IR_OP_GEP || in->end_line != in->line ||
      in->operand_count != 2 || a->text->lines[in->line - 1].count != 10)
    return NULL;
  const ir_line *line = &a->text->lines[in->line - 1];
  const ir_operand *base = &a->operands[in->first_operand], *index = base + 1;
  if (base->kind != IR_OPERAND_GLOBAL || index->kind != IR_OPERAND_LOCAL ||
      strcmp(ir_text(a->text, line->op + 1), "i8") ||
      a->types[index->type].bits != 32)
    return NULL;
  const ir_global *g = &a->globals[base->definition];
  if (g->storage_type == IR_NONE || a->types[g->storage_type].kind != IR_ARRAY)
    return NULL;
  const ir_line *decl = &a->text->lines[g->line - 1];
  for (unsigned k = decl->first + 2; k < decl->first + decl->count; k++) {
    spend(p, 1);
    if (!strcmp(ir_text(a->text, k), "global"))
      break;
    if (!strcmp(ir_text(a->text, k), "constant"))
      return base;
  }
  return NULL;
}

static void symbol_base(loops *p, const ir_function *f, unsigned preheader) {
  ir_analysis *a = &p->a;
  unsigned symbol = IR_NONE;
  for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++) {
    if (!p->member[b]) continue;
    const ir_block *block = &a->blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(p, 1);
      const ir_operand *base = indexed_global(p, i);
      if (!base) continue;
      if (symbol != IR_NONE && symbol != base->definition)
        return;
      symbol = base->definition;
    }
  }
  if (symbol == IR_NONE) return;
  unsigned matches[8], count = 0;
  for (unsigned b = f->first_block; b < f->first_block + f->block_count; b++) {
    if (b == preheader || !dominates(p, preheader, b)) continue;
    const ir_block *block = &a->blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      spend(p, 1);
      const ir_operand *base = indexed_global(p, i);
      if (!base || base->definition != symbol) continue;
      if (count == sizeof(matches) / sizeof(*matches) ||
          a->text->lines[a->instructions[i].line - 1].replacement)
        return;
      matches[count++] = i;
    }
  }
  if (count < 2) return;
  const char *address = ir_fresh(p->c, a->text, '%');
  append_text(p, &p->extra[preheader], ir_format(p->c,
      "  %s = getelementptr i8, ptr %s, i32 0\n", address, a->globals[symbol].name));
  for (unsigned k = 0; k < count; k++) {
    const ir_instruction *in = &a->instructions[matches[k]];
    const ir_operand *index = &a->operands[in->first_operand + 1];
    a->text->lines[in->line - 1].replacement = ir_format(p->c,
        "  %s = getelementptr i8, ptr %s, i32 %s\n",
        a->definitions[in->definition].name, address, ir_text(a->text, index->token));
  }
  p->moves++;
  ir_note(p->c, "proposed", "resident_read_only_table_base", 1);
}

static void loop(loops *p, unsigned header) {
  ir_analysis *a = &p->a;
  const ir_block *h = &a->blocks[header];
  unsigned count = 0, backedges = 0;
  for (unsigned k = 0; k < h->predecessor_count; k++) {
    unsigned b = a->predecessors[h->first_predecessor + k];
    spend(p, 1);
    if (a->blocks[b].rpo == IR_NONE || a->blocks[b].rpo < h->rpo ||
        !dominates(p, header, b))
      continue;
    if (!backedges) {
      spend(p, a->block_count);
      memset(p->member, 0, a->block_count);
      p->member[header] = 1;
    }
    backedges++;
    if (!p->member[b]) {
      p->member[b] = 1;
      p->queue[count++] = b;
    }
  }
  if (!backedges)
    return;
  for (unsigned at = 0; at < count; at++) {
    const ir_block *b = &a->blocks[p->queue[at]];
    for (unsigned k = 0; k < b->predecessor_count; k++) {
      spend(p, 1);
      unsigned pred = a->predecessors[b->first_predecessor + k];
      if (!p->member[pred]) {
        p->member[pred] = 1;
        p->queue[count++] = pred;
      }
    }
  }
  unsigned preheader = IR_NONE;
  for (unsigned k = 0; k < h->predecessor_count; k++) {
    spend(p, 1);
    unsigned b = a->predecessors[h->first_predecessor + k];
    if (!p->member[b]) {
      if (preheader != IR_NONE && preheader != b) {
        ir_note(p->c, "skipped", "multiple_loop_entries", 1);
        return;
      }
      preheader = b;
    }
  }
  if (preheader == IR_NONE || a->blocks[preheader].rpo == IR_NONE ||
      a->blocks[preheader].successor_count != 1 ||
      a->successors[a->blocks[preheader].first_successor] != header ||
      !dominates(p, preheader, header)) {
    ir_note(p->c, "skipped", "no_existing_unique_preheader", 1);
    return;
  }
  const ir_function *f = &a->functions[h->function];
  for (unsigned at = f->first_block; at < f->first_block + f->block_count;
       at++) {
    unsigned b = at;
    spend(p, 1);
    if (p->member[b] &&
        (a->blocks[b].rpo == IR_NONE || !dominates(p, header, b))) {
      ir_note(p->c, "skipped", "irreducible_or_unreachable_member", 1);
      return;
    }
  }
  ir_note(p->c, "candidate", "natural_loop_with_preheader", 1);
  if (p->symbol_gated) {
    symbol_base(p, f, preheader);
    return;
  }
  if (p->bounds_gated) {
    bound_loop(p, f, header, preheader);
    return;
  }
  if (p->pointer_gated) {
    pointer_induction(p, f, header, preheader);
    return;
  }
  if (p->scalar_gated) {
    scalar_loop(p, f, header, preheader);
    return;
  }
  if (p->cost_gated) {
    unsigned i = cost_candidate(p, f, preheader);
    if (i != IR_NONE)
      move(p, i, preheader);
    return;
  }
  for (unsigned at = f->first_block; at < f->first_block + f->block_count;
       at++) {
    unsigned b = p->order[at];
    spend(p, 1);
    if (b == IR_NONE || !p->member[b])
      continue;
    const ir_block *block = &a->blocks[b];
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++)
      if (p->location[i] == b)
        move(p, i, preheader);
    /* Inner-loop motion is already at the end of this block. */
    for (motion *q = p->first[b]; q; q = q->next) {
      spend(p, 1);
      if (p->location[q->instruction] == b)
        move(p, q->instruction, preheader);
    }
  }
}
static void run(ir_context *c, ir_module *m, int cost_gated) {
  ir_allocation *mark = c->allocations;
  loops p = {.c = c, .work = IR_LOOP_WORK_LIMIT, .cost_gated = cost_gated == 1,
             .scalar_gated = cost_gated == 2, .bounds_gated = cost_gated == 3,
             .pointer_gated = cost_gated == 4, .symbol_gated = cost_gated == 5};
  int recurrences = p.scalar_gated || p.pointer_gated || p.symbol_gated;
  ir_analyze(c, m, &p.a);
  ir_analysis *a = &p.a;
  p.order = ir_allocate(c, a->block_count * sizeof(*p.order));
  p.location = ir_allocate(c, a->instruction_count * sizeof(*p.location));
  p.queue = ir_allocate(c, a->block_count * sizeof(*p.queue));
  p.member = ir_allocate(c, a->block_count);
  p.first = ir_allocate(c, a->block_count * sizeof(*p.first));
  p.last = ir_allocate(c, a->block_count * sizeof(*p.last));
  if (recurrences) {
    p.extra = ir_allocate(c, a->block_count * sizeof(*p.extra));
    p.phis = ir_allocate(c, a->block_count * sizeof(*p.phis));
    p.removed = ir_allocate(c, a->instruction_count);
  }
  spend(&p, a->block_count + (uint64_t)a->instruction_count);
  for (unsigned b = 0; b < a->block_count; b++)
    p.order[b] = IR_NONE;
  for (unsigned b = 0; b < a->block_count; b++)
    if (a->blocks[b].rpo != IR_NONE)
      p.order[a->functions[a->blocks[b].function].first_block +
              a->blocks[b].rpo] = b;
  for (unsigned i = 0; i < a->instruction_count; i++)
    p.location[i] = a->instructions[i].block;
  for (unsigned at = a->block_count; at-- > 0;)
    if (p.order[at] != IR_NONE)
      loop(&p, p.order[at]);
  if (!p.moves)
    return;
  for (unsigned b = 0; b < a->block_count; b++) {
    const ir_block *block = &a->blocks[b];
    const ir_instruction *term = &a->instructions[block->first_instruction +
                                                  block->instruction_count - 1];
    const ir_line *line = &m->lines[term->line - 1];
    c->size = 0;
    for (motion *q = p.first[b]; q; q = q->next) {
      spend(&p, 1);
      if (p.location[q->instruction] != b)
        continue;
      const ir_line *moved =
          &m->lines[a->instructions[q->instruction].line - 1];
      spend(&p, moved->end - moved->begin);
      ir_append(c, m->source + moved->begin, moved->end - moved->begin);
    }
    if (recurrences && p.extra[b])
      ir_append(c, p.extra[b], strlen(p.extra[b]));
    if (c->size) {
      ir_append(c, m->source + line->begin, line->end - line->begin);
      m->lines[term->line - 1].replacement = ir_copy(c, c->output, c->size);
    }
  }
  for (unsigned i = 0; i < a->instruction_count; i++)
    if (p.location[i] != a->instructions[i].block || (recurrences && p.removed[i]))
      m->lines[a->instructions[i].line - 1].replacement = "";
  if (recurrences)
    for (unsigned b = 0; b < a->block_count; b++) {
      if (!p.phis[b]) continue;
      const ir_block *block = &a->blocks[b];
      unsigned i = block->first_instruction;
      while (a->instructions[i].opcode == IR_OP_PHI) i++;
      ir_line *line = &m->lines[a->instructions[i].line - 1];
      const char *original = line->replacement ? line->replacement :
          ir_copy(c, m->source + line->begin, line->end - line->begin);
      line->replacement = ir_format(c, "%s%s", p.phis[b], original);
    }
  if (p.scalar_gated) ir_loop_admission(c, m, a);
  if (p.bounds_gated || p.pointer_gated || p.symbol_gated)
    ir_function_admission(c, m, a, BADGE_SELECTION_THUMB_CONSTANTS);
  ir_reparse(c, m, mark);
}
void ir_loop_invariant(ir_context *c, ir_module *m) { run(c, m, 0); }
void ir_loop_cost(ir_context *c, ir_module *m) { run(c, m, 1); }
void ir_span_fusion(ir_context *, ir_module *);

typedef struct {
  const ir_instruction *gep, *offset;
  const ir_operand *base, *index;
  int32_t shift;
} neighboring_load;

static int affine_load(loops *p, unsigned i, neighboring_load *out) {
  ir_analysis *a = &p->a;
  const ir_instruction *gep = &a->instructions[i];
  if (gep->opcode != IR_OP_GEP || gep->operand_count != 2 ||
      a->text->lines[gep->line - 1].count != 10 ||
      strcmp(ir_text(a->text, a->text->lines[gep->line - 1].op + 1), "i8"))
    return 0;
  const ir_definition *d = &a->definitions[gep->definition];
  if (d->use_count != 1)
    return 0;
  const ir_instruction *load = &a->instructions[a->operands[d->first_use].instruction];
  if (load->opcode != IR_OP_LOAD || load->block != gep->block ||
      load->effects != (IR_EFFECT_READ | IR_EFFECT_TRAP) ||
      a->types[load->type].bits != 8)
    return 0;
  const ir_operand *base = &a->operands[gep->first_operand], *index = base + 1;
  if (base->kind != IR_OPERAND_LOCAL || index->kind != IR_OPERAND_LOCAL ||
      a->types[index->type].bits != 32)
    return 0;
  unsigned n = a->definitions[index->definition].instruction;
  if (n == IR_NONE)
    return 0;
  const ir_instruction *offset = &a->instructions[n];
  int subtract = !strcmp(offset->operation, "sub");
  if ((!subtract && strcmp(offset->operation, "add")) || offset->opcode != IR_OP_BINARY ||
      offset->block != gep->block || offset->operand_count != 2 ||
      a->text->lines[offset->line - 1].count != 7 ||
      a->definitions[offset->definition].use_count != 1)
    return 0;
  const ir_operand *args = &a->operands[offset->first_operand];
  uint64_t value;
  if (args->kind != IR_OPERAND_LOCAL || !literal(p, args + 1, &value))
    return 0;
  uint32_t bits = (uint32_t)value;
  if (subtract) bits = 0u - bits;
  if (bits > 31 && bits < UINT32_MAX - 30)
    return 0;
  int32_t shift = bits <= 31 ? (int32_t)bits : -(int32_t)(0u - bits);
  *out = (neighboring_load){gep, offset, base, args, shift};
  return 1;
}

static int share_neighbors(loops *p, const ir_block *block) {
  ir_analysis *a = &p->a;
  ir_module *m = a->text;
  unsigned end = block->first_instruction + block->instruction_count;
  for (unsigned i = block->first_instruction; i < end; i++) {
    spend(p, 1);
    neighboring_load group[8];
    if (!affine_load(p, i, &group[0]))
      continue;
    unsigned count = 1;
    int32_t low = group[0].shift, high = low;
    for (unsigned j = i + 1; j < end && j - i <= 24 && count < 8; j++) {
      spend(p, 1);
      if (a->instructions[j].effects & (IR_EFFECT_OPAQUE | IR_EFFECT_CALL))
        break;
      neighboring_load load;
      if (!affine_load(p, j, &load) ||
          load.base->definition != group[0].base->definition ||
          load.index->definition != group[0].index->definition)
        continue;
      if (load.shift < low) low = load.shift;
      if (load.shift > high) high = load.shift;
      group[count++] = load;
    }
    if (count < 3 || high - low > 31)
      continue;
    const char *shared = ir_fresh(p->c, m, '%');
    const char *base = ir_text(m, group[0].base->token);
    const char *index = ir_text(m, group[0].index->token);
    /* A low anchor keeps all loads within the unsigned Thumb byte offset. */
    const char *biased = low ? ir_fresh(p->c, m, '%') : base;
    const char *prefix = low ? ir_format(p->c, "  %s = getelementptr i8, ptr %s, i32 %" PRId32 "\n",
                                         biased, base, low) : "";
    const char *setup = ir_format(p->c, "%s  %s = getelementptr i8, ptr %s, i32 %s\n",
                                  prefix, shared, biased, index);
    for (unsigned j = 0; j < count; j++) {
      neighboring_load *load = &group[j];
      m->lines[load->offset->line - 1].replacement = "";
      m->lines[load->gep->line - 1].replacement = ir_format(p->c,
          "%s  %s = getelementptr i8, ptr %s, i32 %" PRId32 "\n", j ? "" : setup,
          a->definitions[load->gep->definition].name, shared, load->shift - low);
    }
    p->c->line = group[0].gep->line;
    ir_note(p->c, "proposed", "shared_neighbor_load_base", 1);
    return 1;
  }
  return 0;
}

static void neighboring_addresses(ir_context *c, ir_module *m) {
  if (!strstr(m->source, "load i8, ptr %"))
    return;
  unsigned line = 0;
  for (; line < m->line_count; line++) {
    const ir_line *row = &m->lines[line];
    if (row->count == 4 && !strcmp(ir_text(m, row->first), "target") &&
        !strcmp(ir_text(m, row->first + 1), "datalayout")) {
      if (strcmp(ir_text(m, row->first + 3),
                 "\"e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64\""))
        return;
      break;
    }
  }
  if (line == m->line_count)
    return;
  ir_allocation *mark = c->allocations;
  loops p = {.c = c, .work = IR_LOOP_WORK_LIMIT, .scalar_gated = 1};
  ir_analyze(c, m, &p.a);
  for (unsigned f = 0; f < p.a.function_count; f++) {
    const ir_function *fn = &p.a.functions[f];
    for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++)
      if (p.a.blocks[b].rpo != IR_NONE && share_neighbors(&p, &p.a.blocks[b])) {
        p.moves++;
        break;
      }
  }
  if (p.moves) {
    ir_function_admission(c, m, &p.a, BADGE_SELECTION_THUMB_CONSTANTS);
    ir_reparse(c, m, mark);
  } else
    ir_release_to(c, mark);
}

void ir_loop_scalar(ir_context *c, ir_module *m) {
  ir_span_fusion(c, m);
  run(c, m, 2);
  if (strstr(m->source, "getelementptr i8, ptr @") && strstr(m->source, "constant ["))
    run(c, m, 5);
  neighboring_addresses(c, m);
}
void ir_loop_bounds(ir_context *c, ir_module *m) { run(c, m, 3); }
void ir_pointer_induction(ir_context *c, ir_module *m) { run(c, m, 4); }
