#include "ir_analysis.h"
#include "ir_integer.h"
#include "target_cost.h"
#include "ir_loop_admission.h"
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
  char **extra, **phis;
  unsigned char *removed;
} loops;

static void spend(loops *p, uint64_t n) {
  if (n > p->work)
    ir_error(p->c, p->scalar_gated ? "loop-scalar work limit exceeded" :
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
  ir_note(p->c, p->scalar_gated ? "proposed" :
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
             .scalar_gated = cost_gated == 2};
  ir_analyze(c, m, &p.a);
  ir_analysis *a = &p.a;
  p.order = ir_allocate(c, a->block_count * sizeof(*p.order));
  p.location = ir_allocate(c, a->instruction_count * sizeof(*p.location));
  p.queue = ir_allocate(c, a->block_count * sizeof(*p.queue));
  p.member = ir_allocate(c, a->block_count);
  p.first = ir_allocate(c, a->block_count * sizeof(*p.first));
  p.last = ir_allocate(c, a->block_count * sizeof(*p.last));
  if (p.scalar_gated) {
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
    if (p.scalar_gated && p.extra[b])
      ir_append(c, p.extra[b], strlen(p.extra[b]));
    if (c->size) {
      ir_append(c, m->source + line->begin, line->end - line->begin);
      m->lines[term->line - 1].replacement = ir_copy(c, c->output, c->size);
    }
  }
  for (unsigned i = 0; i < a->instruction_count; i++)
    if (p.location[i] != a->instructions[i].block || (p.scalar_gated && p.removed[i]))
      m->lines[a->instructions[i].line - 1].replacement = "";
  if (p.scalar_gated)
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
  ir_reparse(c, m, mark);
}
void ir_loop_invariant(ir_context *c, ir_module *m) { run(c, m, 0); }
void ir_loop_cost(ir_context *c, ir_module *m) { run(c, m, 1); }
void ir_loop_scalar(ir_context *c, ir_module *m) { run(c, m, 2); }
