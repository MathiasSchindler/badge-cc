#include "codegen_internal.h"
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

typedef struct cg_successor {
  unsigned block;
  struct cg_successor *next;
} cg_successor;
typedef struct {
  cg_block *block;
  unsigned first, count, loop_depth;
  uint64_t *use, *def, *in, *out, *edge;
  cg_successor *successors;
} cg_live_block;
typedef struct {
  cg_inst *instruction;
  cg_register_value *result;
  unsigned first_use, use_count;
} cg_live_instruction;
typedef struct cg_asm_live {
  cg_inst *instruction;
  uint64_t *live;
  cg_register_value *values[CG_GLOBAL_REGISTER_COUNT];
  struct cg_asm_live *next;
} cg_asm_live;
typedef struct cg_affinity {
  unsigned left, right, weight;
  struct cg_affinity *next;
} cg_affinity;
struct cg_global {
  unsigned count, words, blocks, instructions;
  cg_register_value **values;
  cg_live_block *block;
  cg_live_instruction *instruction;
  unsigned *uses, use_count, use_capacity;
  uint64_t *graph;
  cg_asm_live *assembly, *current_assembly;
  cg_affinity *affinities;
  unsigned registers;
  int wide_r6;
};

static const unsigned registers[CG_GLOBAL_REGISTER_COUNT] = {8, 10, 11, 4, 5, 3, 6};

static uint64_t *bits(cg_context *c) {
  return cg_alloc(c, c->global->words * sizeof(uint64_t));
}
static uint64_t *scratch_bits(uint64_t **next,unsigned words) {
  uint64_t *row = *next;
  *next += words;
  return row;
}
static void add(uint64_t *set, unsigned index) {
  set[index / 64] |= UINT64_C(1) << (index % 64);
}
static void remove_bit(uint64_t *set, unsigned index) {
  set[index / 64] &= ~(UINT64_C(1) << (index % 64));
}
static void merge(cg_context *c, uint64_t *dst, const uint64_t *src) {
  for (unsigned w = 0; w < c->global->words; w++) {
    cg_register_work(c);
    dst[w] |= src[w];
  }
}
static unsigned block_index(cg_context *c, const char *token) {
  cg_block *block = cg_find_block(c, token);
  if (!block) cg_error(c, "register allocation branch target does not exist");
  return block->live_index;
}
enum { LOOP_BLOCK_LIMIT = 256, LOOP_DEPTH_LIMIT = 2 };

static void reachable(cg_context *c, unsigned skip, unsigned char *seen) {
  if (skip == 0)
    return;
  unsigned queue[LOOP_BLOCK_LIMIT] = {0};
  unsigned end = 1;
  seen[0] = 1;
  for (unsigned i = 0; i < end; i++)
    for (cg_successor *s = c->global->block[queue[i]].successors; s; s = s->next) {
      cg_register_work(c);
      if (s->block != skip && !seen[s->block]) {
        seen[s->block] = 1;
        queue[end++] = s->block;
      }
    }
}

static void weight_loops(cg_context *c) {
  struct cg_global *g = c->global;
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      c->register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT ||
      c->scalar_stream_selection || !g->blocks || g->blocks > LOOP_BLOCK_LIMIT)
    return;
  cg_successor *incoming[LOOP_BLOCK_LIMIT] = {0};
  unsigned char live[LOOP_BLOCK_LIMIT] = {0};
  reachable(c, g->blocks, live);
  for (unsigned b = 0; b < g->blocks; b++)
    if (live[b])
      for (cg_successor *s = g->block[b].successors; s; s = s->next) {
        cg_register_work(c);
        cg_successor *p = cg_alloc(c, sizeof(*p));
        *p = (cg_successor){b, incoming[s->block]};
        incoming[s->block] = p;
      }
  unsigned loops = 0;
  for (unsigned h = 0; h < g->blocks; h++) {
    if (!incoming[h])
      continue;
    unsigned char bypass[LOOP_BLOCK_LIMIT] = {0}, member[LOOP_BLOCK_LIMIT] = {0};
    unsigned queue[LOOP_BLOCK_LIMIT] = {h}, end = 1;
    reachable(c, h, bypass);
    member[h] = 1;
    int has_latch = 0;
    for (unsigned i = 0; i < end; i++)
      for (cg_successor *p = incoming[queue[i]]; p; p = p->next) {
        cg_register_work(c);
        /* A reachable predecessor that cannot bypass the header is dominated. */
        if (bypass[p->block])
          continue;
        if (!i)
          has_latch = 1;
        if (!member[p->block]) {
          member[p->block] = 1;
          queue[end++] = p->block;
        }
      }
    if (has_latch) {
      loops++;
      for (unsigned i = 0; i < end; i++)
        if (g->block[queue[i]].loop_depth < LOOP_DEPTH_LIMIT)
          g->block[queue[i]].loop_depth++;
    }
  }
  if (loops)
    cg_emit(c, "@ loop-allocation: weighted headers %u\n", loops);
}

static void uses(cg_context *c, unsigned start, unsigned end, uint64_t *set,
                 const cg_live_block *block, unsigned bonus, unsigned depth) {
  for (unsigned k = start; k < end; k++) {
    cg_register_work(c);
    if (c->tokens[k].text[0] != '%')
      continue;
    cg_register_value *v = cg_register_lookup(c, c->tokens[k].text);
    if (!v->value)
      continue;
    if (set) add(set, v->index);
    else {
      struct cg_global *g = c->global;
      if (g->use_count == g->use_capacity)
        cg_error(c, "register use inventory exceeded");
      g->uses[g->use_count++] = v->index;
    }
    v->uses++;
    unsigned score = 1 + bonus + (v->definition != block->block ? 4 : 0);
    v->score += score + depth;
  }
}
static void affinity(cg_context *c, cg_register_value *a, const char *name,
                     unsigned weight) {
  if (!cg_flexible_residency(c) ||
      !a || !a->value || name[0] != '%')
    return;
  cg_register_value *b = cg_register_lookup(c, name);
  if (!b->value || a == b)
    return;
  cg_affinity *edge = cg_alloc(c, sizeof(*edge));
  edge->left = a->index;
  edge->right = b->index;
  edge->weight = weight;
  edge->next = c->global->affinities;
  c->global->affinities = edge;
}
static void phi_uses(cg_context *c, cg_inst *i, cg_live_block *block) {
  for (unsigned k = i->begin + 3; k < i->end; k++) {
    cg_register_work(c);
    if (strcmp(c->tokens[k].text, "["))
      continue;
    unsigned start = k + 1, depth = 1, comma = 0;
    while (++k < i->end && depth) {
      cg_register_work(c);
      const char *s = c->tokens[k].text;
      if (!strcmp(s, "[") || !strcmp(s, "(") || !strcmp(s, "{") ||
          !strcmp(s, "<"))
        depth++;
      else if (!strcmp(s, "]") || !strcmp(s, ")") || !strcmp(s, "}") ||
               !strcmp(s, ">"))
        depth--;
      else if (depth == 1 && !strcmp(s, ","))
        comma = k;
    }
    if (depth)
      cg_error(c, "unterminated PHI in register allocation");
    if (comma && comma + 2 < k && c->tokens[comma + 1].text[0] == '%') {
      cg_live_block *predecessor =
          &c->global->block[block_index(c, c->tokens[comma + 1].text)];
      uses(c, start, comma, predecessor->edge, block, 8, predecessor->loop_depth);
      if (comma == start + 1 && i->result)
        affinity(c, cg_register_lookup(c, i->result->name),
                 c->tokens[start].text, 8);
    }
    k--;
  }
}
static void conflict(cg_context *c, unsigned index, const uint64_t *live) {
  struct cg_global *g = c->global;
  uint64_t *row = g->graph + (size_t)index * g->words;
  for (unsigned w = 0; w < g->words; w++) {
    cg_register_work(c);
    uint64_t mask = live[w];
    if (w == index / 64)
      mask &= ~(UINT64_C(1) << (index % 64));
    row[w] |= mask;
    while (mask) {
      cg_register_work(c);
      unsigned other = w * 64 + (unsigned)__builtin_ctzll(mask);
      add(g->graph + (size_t)other * g->words, index);
      mask &= mask - 1;
    }
  }
}
static int priority(const void *a, const void *b) {
  const cg_register_value *x = *(cg_register_value *const *)a;
  const cg_register_value *y = *(cg_register_value *const *)b;
  if (x->score != y->score)
    return x->score > y->score ? -1 : 1;
  return x->index < y->index ? -1 : x->index > y->index;
}
static int pressure_priority(const void *a, const void *b) {
  const cg_register_value *x = *(cg_register_value *const *)a;
  const cg_register_value *y = *(cg_register_value *const *)b;
  /* Degree must not discount the use cost of long-lived loop/PHI values. */
  if (x->score != y->score)
    return x->score > y->score ? -1 : 1;
  unsigned xc = CG_GLOBAL_REGISTER_COUNT -
                (unsigned)__builtin_popcount(x->forbidden);
  unsigned yc = CG_GLOBAL_REGISTER_COUNT -
                (unsigned)__builtin_popcount(y->forbidden);
  if (xc != yc)
    return xc < yc ? -1 : 1;
  if (x->degree != y->degree)
    return x->degree < y->degree ? -1 : 1;
  return priority(a, b);
}
static void constrain(cg_context *c, cg_live_instruction *i,
                      const uint64_t *live) {
  if (!cg_flexible_residency(c))
    return;
  unsigned mask = cg_instruction_scratch(c, i->instruction);
  if (c->global->registers < 6) mask &= ~(1u << 3);
  if (!mask)
    return;
  struct cg_global *g = c->global;
  int transient_call_uses = c->scalar_stream_selection &&
      i->instruction->scalar_call_arguments_plus_one &&
      i->instruction->scalar_call_arguments_plus_one <= 4;
  if (!transient_call_uses)
    for (unsigned n = i->first_use; n < i->first_use + i->use_count; n++) {
      cg_register_work(c);
      g->values[g->uses[n]]->forbidden |= mask;
    }
  for (unsigned w = 0; w < g->words; w++) {
    cg_register_work(c);
    uint64_t values = live[w];
    while (values) {
      cg_register_work(c);
      unsigned bit = (unsigned)__builtin_ctzll(values);
      cg_register_value *v = g->values[w * 64 + bit];
      v->forbidden |= mask;
      values &= values - 1;
    }
  }
  /* Results must not overwrite hidden scratch state, notably div/rem's r4. */
  if (i->result)
    i->result->forbidden |= mask;
}
static unsigned preferred(cg_context *c, cg_register_value *v,
                          unsigned occupied) {
  unsigned scores[CG_GLOBAL_REGISTER_COUNT] = {0};
  for (cg_affinity *a = c->global->affinities; a; a = a->next) {
    cg_register_work(c);
    unsigned other;
    if (a->left == v->index)
      other = a->right;
    else if (a->right == v->index)
      other = a->left;
    else
      continue;
    unsigned reg = c->global->values[other]->reg;
    for (unsigned r = 0; r < c->global->registers; r++)
      if (registers[r] == reg)
        scores[r] += a->weight;
  }
  unsigned best = 0, score = 0;
  const unsigned flexible_order[] = {3, 4, 0, 1, 2};
  const unsigned caller_order[] = {5, 3, 4, 0, 1, 2};
  const unsigned scalar_order[] = {5, 3, 4, 6, 0, 1, 2};
  for (unsigned k = 0; k < c->global->registers; k++) {
    unsigned r = c->global->registers == 7 ? scalar_order[k] :
                 c->global->registers == 6 ? caller_order[k] : cg_flexible_residency(c)
                     ? flexible_order[k] : k;
    if (!(occupied & (1u << registers[r])) &&
        (!best || scores[r] > score)) {
      best = registers[r];
      score = scores[r];
    }
  }
  return best;
}

static void plan_wide_pairs(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      c->register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT)
    return;
  int reduction = 0;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      reduction |= i->next && cg_truncated_sdiv(c, i, i->next);
    }
  if (!reduction)
    return;
  /* r6 is otherwise scratch for calls and constant expressions. */
  int spare_r6 = 1;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      if (cg_instruction_assembly(c, i))
        return;
      unsigned op = i->begin + (i->result ? 2 : 0);
      for (unsigned k = op; k < i->end; k++) {
        cg_register_work(c);
        const char *token = c->tokens[k].text;
        if (!strcmp(token, "call") || !strcmp(token, "("))
          spare_r6 = 0;
      }
    }
  c->global->wide_r6 = spare_r6;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      if (!i->result || i->result->type->bits != 64 || i->end - i->begin != 7)
        continue;
      const char *op = c->tokens[i->begin + 2].text;
      if (!strcmp(op, "mul") || !strcmp(op, "add")) {
        cg_register_lookup(c, i->result->name)->wide_pair = 1;
        i->wide_pair_safe = 1;
      }
    }
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      unsigned op = i->begin + (i->result ? 2 : 0);
      if (i->next && cg_truncated_sdiv(c, i, i->next))
        i->wide_pair_safe = 1;
      if (i->end - op == 6 && !strcmp(c->tokens[op].text, "icmp") &&
          !strcmp(c->tokens[op + 2].text, "i64") &&
          !strcmp(c->tokens[op + 5].text, "0") &&
          (!strcmp(c->tokens[op + 1].text, "sge") ||
           !strcmp(c->tokens[op + 1].text, "slt")))
        i->wide_pair_safe = 1;
      int reads_words = i->wide_pair_safe || !strcmp(c->tokens[op].text, "icmp");
      if (reads_words)
        continue;
      for (unsigned k = op + 1; k < i->end; k++) {
        cg_register_work(c);
        if (c->tokens[k].text[0] == '%')
          cg_register_lookup(c, c->tokens[k].text)->wide_pair = 0;
      }
    }
}

static int spare_memory_register(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      c->register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT)
    return 0;
  unsigned loads = 0, table_mixes = 0;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      unsigned op = i->begin + (i->result ? 2 : 0);
      const char *name = c->tokens[op].text;
      loads += i->immediate_address != 0;
      table_mixes += i->indexed_source && i->result && i->next &&
                     i->next->shifted_producer && i->result->type->bits == 32;
      if ((!strcmp(name, "call") && !cg_abs_operand(c, i)) ||
          !strcmp(name, "tail") || !strcmp(name, "notail"))
        return 0;
      /* Other calls and constant expressions can use r6 as scratch. */
      if (!cg_abs_operand(c, i))
        for (unsigned k = op + 1; k < i->end; k++) {
          cg_register_work(c);
          if (!strcmp(c->tokens[k].text, "("))
            return 0;
        }
    }
  return loads >= 3 || table_mixes >= 2;
}

static void compact_frame(cg_context *c, cg_register_value **order) {
  struct cg_global *g = c->global;
  uint64_t *occupied = bits(c);
  unsigned colors = 0, original = c->fn->frame, narrow = 0;
  unsigned omitted = 0, rematerialized = 0;
  /* Addressable objects are laid out before private, shareable scalar homes. */
  c->fn->frame = 0;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      cg_value *v = i->result;
      if (v && v->object_align)
        v->object = cg_slot(c, v->object_size, v->object_align);
    }
  for (unsigned n = 0; n < g->count; n++) {
    cg_register_value *v = order[n];
    if (v->high_reg) {
      v->value->slot = UINT32_MAX;
      omitted++;
      continue;
    }
    if (!cg_register_eligible(c, v->value))
      continue;
    narrow += v->reg && v->value->type->bits < 32;
    if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
        c->frame_addressing == BADGE_FRAME_DIRECT && !v->reg && v->uses &&
        v->value->object_align && v->value->object <= 4095 &&
        (!v->value->object || v->uses <= 2 || c->fn->frame >= 128)) {
      v->value->rematerialize_frame_address = 1;
      v->value->slot = UINT32_MAX;
      rematerialized++;
      continue;
    }
    if (cg_home_selection(c) && !v->assembly_home && (v->reg || !v->uses)) {
      v->value->slot = UINT32_MAX;
      omitted++;
      continue;
    }
    memset(occupied, 0, g->words * sizeof(uint64_t));
    for (unsigned w = 0; w < g->words; w++) {
      cg_register_work(c);
      uint64_t mask = g->graph[(size_t)v->index * g->words + w];
      while (mask) {
        cg_register_work(c);
        unsigned other = w * 64 + (unsigned)__builtin_ctzll(mask);
        unsigned color = g->values[other]->home_color;
        if (color)
          add(occupied, color - 1);
        mask &= mask - 1;
      }
    }
    unsigned color = 0;
    while (color < colors &&
           (occupied[color / 64] & (UINT64_C(1) << (color % 64)))) {
      cg_register_work(c);
      color++;
    }
    if (color == colors)
      colors++;
    v->home_color = color + 1;
  }
  for (unsigned n = 0; n < g->count; n++) {
    cg_register_work(c);
    cg_register_value *v = g->values[n];
    if (!v->home_color && v->value->slot != UINT32_MAX)
      v->value->slot = cg_slot(
          c, cg_round(v->value->type->size ? v->value->type->size : 4, 4),
          v->value->type->align > 4 ? 8 : 4);
  }
  unsigned base = cg_slot(c, colors * 4, 4);
  for (unsigned n = 0; n < g->count; n++) {
    cg_register_work(c);
    cg_register_value *v = g->values[n];
    if (v->home_color)
      v->value->slot = base + (v->home_color - 1) * 4;
  }
  cg_emit(c,
          "@ private-stack: original-bytes %u compact-bytes %u "
          "scalar-slots %u narrow-register-values %u\n",
          original, c->fn->frame, colors, narrow);
  if (cg_home_selection(c))
    cg_emit(c, "@ scalar-homes: omitted %u retained-slots %u\n", omitted, colors);
  if (rematerialized)
    cg_emit(c, "@ frame-rematerialization: addresses %u\n", rematerialized);
}
void cg_global_prepare(cg_context *c) {
  struct cg_global *g = cg_alloc(c, sizeof(*g));
  c->global = g;
  c->allocate_r6 = 0;
  c->neighbor_selection = 0;
  c->scalar_stream_selection = 0;
  plan_wide_pairs(c);
  g->registers = cg_flexible_residency(c)
                     ? 5 : 3;
  if (cg_light_frames(c) && cg_flexible_residency(c) && c->fn->argc <= 3 &&
      c->fn->ret->size <= 4) {
    int eligible = 1;
    for (cg_value *v = c->fn->values; v; v = v->next)
      if (v->type->size > 4) eligible = 0;
    for (unsigned k = 0; k < c->fn->argc; k++)
      if (c->fn->byval[k]) eligible = 0;
    for (cg_block *b = c->fn->blocks; b; b = b->next)
      for (cg_inst *i = b->first; i; i = i->next) {
        cg_assembly *a = cg_instruction_assembly(c, i);
        if (a && !a->known) eligible = 0;
        for (unsigned k = i->begin; k < i->end; k++) {
          cg_register_work(c);
          const char *token = c->tokens[k].text;
          if (token[0] == 'i' && token[1] >= '0' && token[1] <= '9' &&
              strtoul(token + 1, NULL, 10) > 32) eligible = 0;
        }
      }
    if (eligible) g->registers = 6;
  }
  c->scalar_stream_selection =
      c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS && g->registers == 6 &&
      !c->scalar_stream_retry;
  if (c->scalar_stream_selection) {
    int eligible = 1;
    unsigned calls = 0, phis = 0;
    for (cg_value *v = c->fn->values; v; v = v->next)
      if (v->object_size) eligible = 0;
    for (cg_block *b = c->fn->blocks; b; b = b->next)
      for (cg_inst *i = b->first; i; i = i->next) {
        unsigned op = i->begin + (i->result ? 2 : 0);
        const char *name = c->tokens[op].text;
        if (!strcmp(name, "tail") || !strcmp(name, "notail"))
          name = c->tokens[++op].text;
        if (cg_instruction_assembly(c, i) || !strcmp(name, "switch"))
          eligible = 0;
        if (!strcmp(name, "call")) {
          calls++;
          (void)cg_instruction_scratch(c, i);
          if (!i->scalar_call_arguments_plus_one) eligible = 0;
        } else {
          phis += !strcmp(name, "phi");
          for (unsigned k = op; k < i->end; k++) {
            cg_register_work(c);
            if (!strcmp(c->tokens[k].text, "(")) eligible = 0;
          }
        }
      }
    if (eligible && calls && phis) {
      g->registers = 7;
      c->allocate_r6 = 1;
    } else c->scalar_stream_selection = 0;
  }
  if (g->registers == 6 && spare_memory_register(c)) {
    g->registers = 7;
    c->allocate_r6 = 1;
    c->neighbor_selection = 1;
    cg_plan_resident_loads(c);
  }
  for (unsigned n = 0; n < c->register_capacity; n++) {
    cg_register_value *v = &c->register_table[n];
    if (v->value)
      v->index = g->count++;
  }
  g->words = (g->count + 63) / 64;
  for (cg_block *b = c->fn->blocks; b; b = b->next) {
    g->blocks++;
    for (cg_inst *i = b->first; i; i = i->next) {
      g->instructions++;
      g->use_capacity += i->end - i->begin;
    }
  }
  g->values = cg_alloc(c, g->count * sizeof(*g->values));
  g->block = cg_alloc(c, g->blocks * sizeof(*g->block));
  g->instruction = cg_alloc(c, g->instructions * sizeof(*g->instruction));
  g->uses = cg_alloc(c, (size_t)g->use_capacity * sizeof(*g->uses));
  size_t rows = (size_t)g->count + (size_t)g->blocks * 5 + 2;
  size_t word_bytes = (size_t)g->words * sizeof(uint64_t);
  if (word_bytes && rows > (CG_ALLOCATION_LIMIT - c->memory) / word_bytes)
    cg_error(c, "backend allocation limit exceeded");
  size_t scratch_bytes = rows * word_bytes;
  c->liveness_scratch = calloc(1, scratch_bytes ? scratch_bytes : sizeof(uint64_t));
  if (!c->liveness_scratch) cg_error(c, "out of memory");
  c->memory += scratch_bytes;
  g->graph = c->liveness_scratch;
  uint64_t *scratch = g->graph + (size_t)g->count * g->words;
  for (unsigned n = 0; n < c->register_capacity; n++) {
    cg_register_value *v = &c->register_table[n];
    if (v->value)
      g->values[v->index] = v;
  }
  unsigned bn = 0, in = 0;
  for (cg_block *b = c->fn->blocks; b; b = b->next, bn++) {
    cg_live_block *lb = &g->block[bn];
    b->live_index = bn;
    lb->block = b;
    lb->first = in;
    lb->use = scratch_bits(&scratch, g->words);
    lb->def = scratch_bits(&scratch, g->words);
    lb->in = scratch_bits(&scratch, g->words);
    lb->out = scratch_bits(&scratch, g->words);
    lb->edge = scratch_bits(&scratch, g->words);
    for (cg_inst *i = b->first; i; i = i->next, in++) {
      g->instruction[in].instruction = i;
      if (i->result) {
        cg_value *result = i->resident_extension ? i->resident_extension->result : i->result;
        cg_register_value *v = cg_register_lookup(c, result->name);
        if (v->value && !i->folded_operand)
          g->instruction[in].result = v;
      }
      lb->count++;
    }
  }
  for (bn = 0; bn < g->blocks; bn++) {
    cg_live_block *b = &g->block[bn];
    for (in = b->first; in < b->first + b->count; in++) {
      cg_inst *i = g->instruction[in].instruction;
      unsigned op = i->begin + (i->result ? 2 : 0);
      if (strcmp(c->tokens[op].text, "br") &&
          strcmp(c->tokens[op].text, "switch"))
        continue;
      for (unsigned k = op + 1; k + 1 < i->end; k++) {
        cg_register_work(c);
        if (strcmp(c->tokens[k].text, "label"))
          continue;
        cg_successor *edge = cg_alloc(c, sizeof(*edge));
        edge->block = block_index(c, c->tokens[k + 1].text);
        edge->next = b->successors;
        b->successors = edge;
      }
    }
  }
  weight_loops(c);
  for (bn = 0; bn < g->blocks; bn++) {
    cg_live_block *b = &g->block[bn];
    for (in = b->first; in < b->first + b->count; in++) {
      cg_live_instruction *li = &g->instruction[in];
      cg_inst *i = li->instruction;
      li->first_use = g->use_count;
      unsigned op = i->begin + (i->result ? 2 : 0);
      int phi = !strcmp(c->tokens[op].text, "phi");
      if (phi) {
        phi_uses(c, i, b);
        if (li->result)
          li->result->score += 16;
      } else if (i->immediate_address) {
        uses(c, i->immediate_address, i->immediate_address + 1,
             NULL, b, b->loop_depth && c->neighbor_selection ? 6 : 0, b->loop_depth);
      } else if (i->indexed_source) {
        /* Operands of a folded address are live at the access, not at the GEP. */
        unsigned value = op + 2;
        if (!strcmp(c->tokens[op].text, "store"))
          uses(c, value, value + 1, NULL, b, 0, b->loop_depth);
        unsigned base = i->indexed_address->begin + 6;
        uses(c, base, base + 1, NULL, b, 0, b->loop_depth);
        uses(c, i->indexed_source, i->indexed_source + 1,
             NULL, b, 0, b->loop_depth);
      } else if (i->shifted_producer) {
        uses(c, i->shifted_source, i->shifted_source + 1,
             NULL, b, 0, b->loop_depth);
        uses(c, i->shifted_other, i->shifted_other + 1,
             NULL, b, 0, b->loop_depth);
      } else if (i->forwarded_call_argument) {
        unsigned argument = i->forwarded_call_argument;
        uses(c, op + 1, argument, NULL, b, 0, b->loop_depth);
        uses(c, argument + 1, i->end, NULL, b, 0, b->loop_depth);
      } else if (!i->folded_operand)
        uses(c, op + 1, i->end, NULL, b, 0, b->loop_depth);
      li->use_count = g->use_count - li->first_use;
      if (li->result && (!strcmp(c->tokens[op].text, "freeze") ||
                         !strcmp(c->tokens[op].text, "ptrtoint") ||
                         !strcmp(c->tokens[op].text, "inttoptr")) &&
          op + 2 < i->end)
        affinity(c, li->result, c->tokens[op + 2].text, 2);
      if (c->scalar_stream_selection && li->result &&
          !strcmp(c->tokens[op].text, "zext") &&
          li->result->value->type->bits == 32 && op + 2 < i->end)
        affinity(c, li->result, c->tokens[op + 2].text, 4);
      if (c->scalar_stream_selection && li->result &&
          !strcmp(c->tokens[op].text, "getelementptr") &&
          i->end - op == 8 && !strcmp(c->tokens[op + 1].text, "i8"))
        affinity(c, li->result, c->tokens[op + 4].text, 4);
      for (unsigned n = li->first_use; n < g->use_count; n++) {
        cg_register_work(c);
        unsigned value = g->uses[n];
        if (!(b->def[value / 64] & (UINT64_C(1) << (value % 64))))
          add(b->use, value);
      }
      if (li->result)
        add(b->def, li->result->index);
    }
  }
  uint64_t *out = scratch_bits(&scratch, g->words);
  uint64_t *live = scratch_bits(&scratch, g->words);
  int changed;
  do {
    changed = 0;
    for (bn = g->blocks; bn-- > 0;) {
      cg_live_block *b = &g->block[bn];
      memcpy(out, b->edge, g->words * sizeof(uint64_t));
      for (cg_successor *s = b->successors; s; s = s->next)
        merge(c, out, g->block[s->block].in);
      for (unsigned w = 0; w < g->words; w++) {
        cg_register_work(c);
        uint64_t next = b->use[w] | (out[w] & ~b->def[w]);
        changed |= next != b->in[w] || out[w] != b->out[w];
        b->in[w] = next;
        b->out[w] = out[w];
      }
    }
  } while (changed);
  for (bn = 0; bn < g->blocks; bn++) {
    cg_live_block *b = &g->block[bn];
    memcpy(live, b->out, g->words * sizeof(uint64_t));
    for (in = b->first + b->count; in-- > b->first;) {
      cg_live_instruction *li = &g->instruction[in];
      cg_inst *i = li->instruction;
      constrain(c, li, live);
      if (li->result) {
        conflict(c, li->result->index, live);
        remove_bit(live, li->result->index);
      }
      unsigned op = i->begin + (i->result ? 2 : 0);
      if (!strcmp(c->tokens[op].text, "tail") ||
          !strcmp(c->tokens[op].text, "notail"))
        op++;
      if (!strcmp(c->tokens[op].text, "call") && op + 1 < i->end) {
        for (unsigned k = op + 1; k < i->end; k++) {
          cg_register_work(c);
          if (strcmp(c->tokens[k].text, "asm"))
            continue;
          cg_assembly *effects = cg_instruction_assembly(c, i);
          if (cg_effect_selection(c) && !effects->writes)
            break;
          cg_asm_live *a = cg_alloc(c, sizeof(*a));
          a->instruction = i;
          a->live = bits(c);
          memcpy(a->live, live, g->words * sizeof(uint64_t));
          a->next = g->assembly;
          g->assembly = a;
          break;
        }
      }
      for (unsigned n = li->first_use; n < li->first_use + li->use_count; n++) {
        cg_register_work(c);
        add(live, g->uses[n]);
      }
    }
  }
  if (g->blocks)
    for (unsigned k = 0; k < c->fn->argc; k++) {
      cg_register_value *v = cg_register_lookup(c, c->fn->args[k]->name);
      if (v->value)
        conflict(c, v->index, g->block[0].in);
      if (cg_flexible_residency(c) && v->value)
        v->forbidden |= CG_SCRATCH_R5;
    }
  if (c->scalar_stream_selection) {
    int changed_masks;
    do {
      changed_masks = 0;
      for (cg_affinity *a = g->affinities; a; a = a->next) {
        cg_register_work(c);
        if (g->graph[(size_t)a->left * g->words + a->right / 64] &
            (UINT64_C(1) << (a->right % 64)))
          continue;
        unsigned mask = g->values[a->left]->forbidden | g->values[a->right]->forbidden;
        changed_masks |= mask != g->values[a->left]->forbidden ||
                         mask != g->values[a->right]->forbidden;
        g->values[a->left]->forbidden = g->values[a->right]->forbidden = mask;
      }
    } while (changed_masks);
  }
  cg_register_value **order = cg_alloc(c, g->count * sizeof(*order));
  memcpy(order, g->values, g->count * sizeof(*order));
  if (cg_flexible_residency(c))
    for (unsigned n = 0; n < g->count; n++)
      for (unsigned w = 0; w < g->words; w++) {
        cg_register_work(c);
        g->values[n]->degree +=
            (unsigned)__builtin_popcountll(g->graph[(size_t)n * g->words + w]);
      }
  qsort(order, g->count, sizeof(*order),
        cg_flexible_residency(c)
            ? pressure_priority : priority);
  unsigned allocated = 0, spilled = 0;
  for (unsigned n = 0; n < g->count; n++) {
    cg_register_value *v = order[n];
    if (!v->uses || (!v->wide_pair && !cg_register_eligible(c, v->value)))
      continue;
    unsigned occupied = v->forbidden;
    uint64_t *row = g->graph + (size_t)v->index * g->words;
    for (unsigned w = 0; w < g->words; w++) {
      cg_register_work(c);
      uint64_t mask = row[w];
      while (mask) {
        cg_register_work(c);
        unsigned other = w * 64 + (unsigned)__builtin_ctzll(mask);
        for (unsigned r = 0; r < g->registers; r++)
          if (g->values[other]->reg == registers[r])
            occupied |= 1u << registers[r];
        if (g->values[other]->high_reg)
          occupied |= 1u << g->values[other]->high_reg;
        mask &= mask - 1;
      }
    }
    if (v->wide_pair) {
      /* Keep pairs disjoint so a dying pair can be reused without overlap copies. */
      const unsigned lows[] = {4, 10}, highs[] = {g->wide_r6 ? 6u : 5u, 11};
      for (unsigned k = 0; k < sizeof(lows) / sizeof(*lows); k++)
        if (!(occupied & ((1u << lows[k]) | (1u << highs[k])))) {
          v->reg = lows[k];
          v->high_reg = highs[k];
          break;
        }
      if (v->high_reg) {
        allocated++;
        if (v->high_reg == 6)
          c->allocate_r6 = 1;
        if (v->high_reg == 11)
          c->register_saved_bytes = 32;
      }
      continue;
    }
    v->reg = preferred(c, v, occupied);
    if (v->reg) {
      allocated++;
      if (v->reg == 10 || v->reg == 11)
        c->register_saved_bytes = 32;
    }
    if (!v->reg)
      spilled++;
  }
  if (c->scalar_stream_selection) {
    int high = 0;
    for (unsigned n = 0; n < g->count; n++)
      high |= g->values[n]->reg >= 8;
    if (high || spilled) {
      c->scalar_stream_retry = 1;
      cg_register_prepare(c);
      return;
    }
  }
  if (c->scalar_stream_selection && c->fn->blocks && c->fn->blocks->first) {
    cg_inst *first = c->fn->blocks->first;
    unsigned block = 0;
    int entry_safe = 1;
    for (unsigned n = 0; n < g->blocks; n++)
      for (cg_successor *s = g->block[n].successors; s; s = s->next)
        if (s->block == 0) entry_safe = 0;
    for (unsigned k = 0; k < c->fn->argc; k++)
      if (c->fn->arg_align[k] >= 8) entry_safe = 0;
    for (unsigned steps = 0; steps < 8 && first &&
         first == g->block[block].block->last && !first->result &&
         first->end - first->begin == 3 &&
         !strcmp(c->tokens[first->begin].text, "br"); steps++) {
      unsigned target = block_index(c, c->tokens[first->begin + 2].text);
      unsigned predecessors = 0;
      for (unsigned n = 0; n < g->blocks; n++)
        for (cg_successor *s = g->block[n].successors; s; s = s->next)
          predecessors += s->block == target;
      if (target == 0 || predecessors != 1) break;
      block = target;
      first = g->block[block].block->first;
    }
    if (!first) cg_error(c, "empty entry block in register allocation");
    unsigned op = first->begin + (first->result ? 2 : 0);
    if (entry_safe && first->result && first->end - op == 8 &&
        !strcmp(c->tokens[op].text, "getelementptr") &&
        !strcmp(c->tokens[op + 1].text, "i8") &&
        !strcmp(c->tokens[op + 6].text, "i32") &&
        cg_global_register(c, first->result->name) &&
        cg_global_register(c, c->tokens[op + 4].text))
      for (unsigned k = 1; k < c->fn->argc; k++) {
        cg_register_value *v = cg_register_lookup(c, c->fn->args[k]->name);
        if (v->uses == 1 && v->value->type->bits == 32 &&
            !strcmp(v->value->name, c->tokens[op + 7].text))
          v->reg = k;
      }
  }
  for (cg_asm_live *a = g->assembly; a; a = a->next)
    for (unsigned n = 0; n < g->count; n++) {
      cg_register_work(c);
      cg_register_value *v = g->values[n];
      if (v->reg && (a->live[n / 64] & (UINT64_C(1) << (n % 64))))
        for (unsigned r = 0; r < g->registers; r++)
          if (v->reg == registers[r]) {
            if (a->values[r])
              cg_error(c, "overlapping assembly register live ranges");
            a->values[r] = v;
            v->assembly_home = 1;
          }
    }
  cg_emit(c, "@ global-allocation: allocated %u spilled %u\n", allocated,
          spilled);
  if (cg_flexible_residency(c)) {
    unsigned constrained = 0, extra = 0;
    for (unsigned n = 0; n < g->count; n++) {
      constrained += g->values[n]->forbidden != 0;
      extra += g->values[n]->reg == 4 || g->values[n]->reg == 5;
    }
    cg_emit(c, "@ flexible-allocation: constrained %u extra-register-values %u\n",
            constrained, extra);
  }
  if (c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT)
    compact_frame(c, order);
  free(c->liveness_scratch);
  c->liveness_scratch = NULL;
  c->memory -= scratch_bytes;
}
unsigned cg_global_register(cg_context *c, const char *name) {
  if (!cg_global_residency(c))
    return 0;
  cg_register_value *entry = cg_register_lookup(c, name);
  return entry->high_reg ? 0 : entry->reg;
}
void cg_global_instruction(cg_context *c, cg_inst *instruction) {
  struct cg_global *g = c->global;
  g->current_assembly = NULL;
  for (cg_asm_live *a = g->assembly; a; a = a->next) {
    cg_register_work(c);
    if (a->instruction != instruction)
      continue;
    g->current_assembly = a;
    for (unsigned r = 0; r < g->registers; r++)
      if (a->values[r]) {
        cg_stack(c, 1, a->values[r]->reg, a->values[r]->value->slot);
        c->asm_spills++;
      }
    break;
  }
}
void cg_global_after_instruction(cg_context *c) {
  cg_asm_live *a = c->global->current_assembly;
  if (a)
    for (unsigned r = 0; r < c->global->registers; r++)
      if (a->values[r])
        cg_stack(c, 0, a->values[r]->reg, a->values[r]->value->slot);
}
