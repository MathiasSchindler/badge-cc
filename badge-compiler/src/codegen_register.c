#include "codegen_internal.h"
#include <string.h>

#ifndef CG_REGISTER_WORK_LIMIT
#define CG_REGISTER_WORK_LIMIT (32u * 1024u * 1024u)
#endif

int cg_register_eligible(const cg_context *c, const cg_value *v) {
  return c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT
             ? v->type->size <= 4 && v->type->bits && v->type->bits <= 32
             : v->type->size == 4 && v->type->bits == 32;
}
static int barrier(const char *op) {
  return !strcmp(op, "call") || !strcmp(op, "tail") || !strcmp(op, "notail") ||
         !strcmp(op, "br") || !strcmp(op, "switch") || !strcmp(op, "ret") ||
         !strcmp(op, "phi") || !strcmp(op, "unreachable");
}
void cg_register_work(cg_context *c) {
  if (++c->register_work > CG_REGISTER_WORK_LIMIT)
    cg_error(c, c->scalar_selection != BADGE_SELECTION_LEGACY
                    ? "native scalar selection analysis work limit exceeded"
                : cg_global_residency(c)
                    ? "global register allocation work limit exceeded"
                    : "local register residency work limit exceeded");
}
cg_register_value *cg_register_lookup(cg_context *c, const char *name) {
  uint32_t hash = 2166136261u;
  for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
    cg_register_work(c);
    hash = (hash ^ *p) * 16777619u;
  }
  unsigned slot = hash & (c->register_capacity - 1);
  for (;;) {
    cg_register_work(c);
    cg_register_value *entry = &c->register_table[slot];
    if (!entry->value || !strcmp(entry->value->name, name))
      return entry;
    slot = (slot + 1) & (c->register_capacity - 1);
  }
}
static void references(cg_context *c, cg_block *b, int local) {
  for (cg_inst *i = b->first; i; i = i->next) {
    unsigned start = i->begin + (i->result ? 3 : 1);
    int can_cache = !barrier(c->tokens[start - 1].text);
    int phi = i->result && !strcmp(c->tokens[i->begin + 2].text, "phi");
    for (unsigned k = start; k < i->end; k++) {
      const char *name = c->tokens[k].text;
      if (*name != '%')
        continue;
      cg_register_value *entry = cg_register_lookup(c, name);
      if (!entry->value)
        continue;
      if (local) {
        if (entry->use_block != b)
          entry->last_cache_use = 0;
        entry->use_block = b;
        entry->last_use = k;
        if (can_cache)
          entry->last_cache_use = k;
      } else if (entry->definition != b || phi)
        entry->live_out = 1;
    }
  }
}
void cg_register_prepare(cg_context *c) {
  c->register_active = 0;
  c->resident = NULL;
  c->avoided_loads = c->avoided_stores = 0;
  c->register_saved_bytes = 24;
  c->phi_register_copies = c->asm_spills = 0;
  c->register_copies = 0;
  c->global = NULL;
  if (c->register_residency == BADGE_RESIDENCY_STACK &&
      c->scalar_selection == BADGE_SELECTION_LEGACY)
    return;
  unsigned count = 0;
  for (cg_value *v = c->fn->values; v; v = v->next)
    if (cg_register_eligible(c, v) ||
        c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT)
      count++;
  c->register_capacity = 2;
  while (c->register_capacity < count * 2)
    c->register_capacity *= 2;
  c->register_table =
      cg_alloc(c, c->register_capacity * sizeof(*c->register_table));
  for (cg_value *v = c->fn->values; v; v = v->next)
    if (cg_register_eligible(c, v) ||
        c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT)
      cg_register_lookup(c, v->name)->value = v;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next)
      if (i->result && (cg_register_eligible(c, i->result) ||
                       c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT))
        cg_register_lookup(c, i->result->name)->definition = b;
  if (c->register_residency == BADGE_RESIDENCY_STACK)
    return;
  if (cg_global_residency(c)) {
    cg_global_prepare(c);
    return;
  }
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    references(c, b, 0);
}
static int needed(cg_context *c, cg_register_value *entry, unsigned at) {
  return entry->live_out ||
         (entry->use_block == c->register_block && entry->last_use >= at);
}
static void flush(cg_context *c, unsigned at) {
  if (c->resident && c->resident_dirty && needed(c, c->resident, at)) {
    cg_stack(c, 1, c->resident_register, c->resident->value->slot);
    c->avoided_stores--;
  }
  c->resident = NULL;
  c->resident_dirty = 0;
}
void cg_register_block(cg_context *c, cg_block *block) {
  if (c->register_residency != BADGE_RESIDENCY_LOCAL)
    return;
  flush(c, UINT32_MAX);
  c->register_block = block;
  references(c, block, 1);
}
void cg_register_instruction(cg_context *c, cg_inst *i, const char *op) {
  if (cg_global_residency(c)) {
    cg_global_instruction(c, i);
    return;
  }
  c->register_instruction = i;
  c->instruction_resident = c->resident;
  c->register_active = c->register_residency == BADGE_RESIDENCY_LOCAL;
  if (cg_effect_selection(c)) {
    cg_assembly *a = cg_instruction_assembly(c, i);
    if (a && a->known && !a->writes) return;
  }
  if (barrier(op)) {
    flush(c, i->begin);
    c->register_active = 0;
  } else if (c->resident && c->resident_register == 0) {
    /* These selectors read both operands before overwriting r0. */
    int consume = 0;
    const char *binary[] = {"add",  "sub",  "mul",  "and",  "or",
                            "xor",  "shl",  "lshr", "ashr", "sdiv",
                            "udiv", "srem", "urem"};
    for (unsigned n = 0; n < sizeof(binary) / sizeof(*binary); n++)
      if (!strcmp(op, binary[n]))
        consume = 1;
    unsigned start = i->begin + (i->result ? 3 : 1);
    while (start < i->end && strcmp(c->tokens[start].text, "i32"))
      start++;
    consume = consume && start + 1 < i->end &&
              !strcmp(c->tokens[start + 1].text, c->resident->value->name);
    for (unsigned k = start; k < i->end; k++)
      if (!strcmp(c->tokens[k].text, "("))
        consume = 0;
    if (!needed(c, c->resident, i->begin))
      flush(c, i->begin);
    else if (!consume ||
             (c->resident_dirty && needed(c, c->resident, i->end))) {
      cg_emit(c, "mov r8, r0\n");
      c->resident_register = 8;
    }
  }
}
void cg_register_after_instruction(cg_context *c) {
  if (cg_global_residency(c)) {
    cg_global_after_instruction(c);
    return;
  }
  if (cg_effect_selection(c)) {
    cg_assembly *a = cg_instruction_assembly(c, c->register_instruction);
    if (a && a->known && !a->writes) return;
  }
  if (c->resident && c->resident_register == 0 &&
      c->resident == c->instruction_resident) {
    c->resident = NULL;
    c->resident_dirty = 0;
  }
}
void cg_register_finish(cg_context *c) {
  if (cg_global_residency(c)) {
    cg_emit(c,
            "@ global-residency: avoided-loads %u avoided-stores %u "
            "phi-register-copies %u asm-spills %u\n",
            c->avoided_loads, c->avoided_stores, c->phi_register_copies,
            c->asm_spills);
    if (cg_flexible_residency(c))
      cg_emit(c, "@ allocation-copies: direct %u\n", c->register_copies);
    return;
  }
  if (c->register_residency != BADGE_RESIDENCY_LOCAL)
    return;
  flush(c, UINT32_MAX);
  c->register_active = 0;
  cg_emit(c, "@ local-residency: avoided-loads %u avoided-stores %u\n",
          c->avoided_loads, c->avoided_stores);
}
int cg_register_read(cg_context *c, cg_value *v, unsigned r, unsigned word) {
  if (cg_global_residency(c) && !word) {
    unsigned source = cg_global_register(c, v->name);
    if (source) {
      if (r != source)
        cg_move(c, r, source);
      c->avoided_loads++;
      return 1;
    }
  }
  if (!c->register_active || word || r > 6 || !cg_register_eligible(c, v) || !c->resident ||
      c->resident->value != v)
    return 0;
  if (r != c->resident_register)
    cg_move(c, r, c->resident_register);
  c->avoided_loads++;
  return 1;
}
void cg_register_loaded(cg_context *c, cg_value *v, unsigned r, unsigned word) {
  if (!c->register_active || word || r > 6 || !cg_register_eligible(c, v))
    return;
  cg_register_value *entry = cg_register_lookup(c, v->name);
  if (entry->use_block != c->register_block || entry->last_cache_use < c->pos)
    return;
  flush(c, c->register_instruction->begin);
  cg_emit(c, "mov r8, r%u\n", r);
  c->resident = entry;
  c->resident_register = 8;
}
int cg_register_copy(cg_context *c, cg_value *dst, cg_value *src) {
  if (!cg_flexible_residency(c) ||
      !cg_register_eligible(c, dst) || !cg_register_eligible(c, src) ||
      dst->type->bits != src->type->bits)
    return 0;
  unsigned reg = cg_global_register(c, src->name);
  if (!reg)
    return 0;
  c->avoided_loads++;
  c->register_copies++;
  cg_save(c, dst, reg, 0);
  return 1;
}
int cg_register_save(cg_context *c, cg_value *v, unsigned r, unsigned word) {
  if (cg_global_residency(c) && !word && cg_register_eligible(c, v)) {
    cg_register_value *entry = cg_register_lookup(c, v->name);
    if (entry->reg || !entry->uses) {
      if (entry->reg && r != entry->reg)
        cg_move(c, entry->reg, r);
      c->avoided_stores++;
      return 1;
    }
  }
  if (!c->register_active || word || r > 6 || !cg_register_eligible(c, v))
    return 0;
  cg_register_value *entry = cg_register_lookup(c, v->name);
  unsigned end = c->register_instruction->end;
  if (entry->use_block != c->register_block || entry->last_cache_use < end) {
    if (needed(c, entry, end))
      return 0;
    c->avoided_stores++;
    return 1;
  }
  flush(c, end);
  /* PHIs and other blocks must always see a current stack home. */
  if (entry->live_out)
    cg_stack(c, 1, r, v->slot);
  else
    c->avoided_stores++;
  if (r)
    cg_emit(c, "mov r8, r%u\n", r);
  c->resident = entry;
  c->resident_register = r ? 8 : 0;
  c->resident_dirty = !entry->live_out;
  return 1;
}
