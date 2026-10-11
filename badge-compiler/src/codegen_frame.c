#include "codegen_internal.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

unsigned cg_slot(cg_context *c, unsigned size, unsigned align) {
  if (!align || (align & (align - 1)) || align > 8)
    cg_error(c, "unsupported stack alignment");
  c->fn->frame = cg_round(c->fn->frame, align);
  unsigned slot = c->fn->frame;
  if (size > 65536 - slot)
    cg_error(c, "stack frame exceeds 64 KiB");
  c->fn->frame += size;
  return slot;
}
static unsigned value_slot(cg_function *f, const char *name) {
  uint32_t hash = 2166136261u;
  for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    hash = (hash ^ *p) * 16777619u;
  unsigned slot = hash & (f->value_capacity - 1);
  while (f->value_table[slot] &&
         strcmp(f->value_table[slot]->name, name))
    slot = (slot + 1) & (f->value_capacity - 1);
  return slot;
}
cg_value *cg_find(cg_context *c, const char *name) {
  if (c->fn->value_capacity) {
    cg_value *v = c->fn->value_table[value_slot(c->fn, name)];
    if (v)
      return v;
  }
  cg_error(c, "undefined SSA value");
}
cg_value *cg_define(cg_context *c, char *name, cg_type *t) {
  cg_function *f = c->fn;
  unsigned slot = 0;
  if (f->value_capacity) {
    slot = value_slot(f, name);
    if (f->value_table[slot])
      cg_error(c, "duplicate SSA value");
  }
  if (!f->value_capacity || f->value_count >= f->value_capacity / 2) {
    f->value_capacity = f->value_capacity ? f->value_capacity * 2 : 32;
    f->value_table = cg_alloc(c, f->value_capacity * sizeof(*f->value_table));
    for (cg_value *v = f->values; v; v = v->next)
      f->value_table[value_slot(f, v->name)] = v;
    slot = value_slot(f, name);
  }
  cg_value *v = cg_alloc(c, sizeof(*v));
  v->name = name;
  v->type = t;
  v->slot =
      cg_slot(c, cg_round(t->size ? t->size : 4, 4), t->align > 4 ? 8 : 4);
  v->next = c->fn->values;
  c->fn->values = v;
  f->value_table[slot] = v;
  f->value_count++;
  return v;
}
static void function_header(cg_context *c, cg_function *f) {
  cg_emit(c, ".balign %u\n@ stack-frame: %u + %u saved-register bytes\n",
          f->alignment, f->frame, c->register_saved_bytes);
  if (!f->internal) cg_emit(c, ".global %s\n", f->name);
  cg_emit(c, ".type %s, %%function\n.thumb_func\n%s:\n", f->name, f->name);
}
static void register_list(cg_context *c, unsigned mask, int pop) {
  cg_emit(c, "%s%s {", pop ? "pop" : "push", mask & 0x1f00u ? ".w" : ".n");
  int first = 1;
  for (unsigned r = 0; r < 15; r++)
    if (mask & (1u << r)) {
      cg_emit(c, "%s", first ? "" : ", ");
      if (r == 14) cg_emit(c, "%s", pop ? "pc" : "lr");
      else cg_emit(c, "r%u", r);
      first = 0;
    }
  cg_emit(c, "}\n");
}
static unsigned mentioned_registers(cg_context *c, const char *line) {
  unsigned mask = 0;
  for (const char *p = line; *p; p++) {
    cg_register_work(c);
    if (*p != 'r' || (p != line && (isalnum((unsigned char)p[-1]) || p[-1] == '_' || p[-1] == '.')))
      continue;
    const char *q = p + 1;
    unsigned n = 0, digits = 0;
    while (isdigit((unsigned char)*q)) {
      n = n * 10 + (unsigned)(*q++ - '0');
      if (++digits > 2) break;
    }
    if (digits && digits <= 2 && n <= 15 && !isalnum((unsigned char)*q) && *q != '_')
      mask |= 1u << n;
  }
  return mask;
}
int cg_internal_call_mentions(cg_context *c, const char *name, unsigned *mask) {
  for (cg_function *f = c->functions; f; f = f->next) {
    cg_register_work(c);
    if (!strcmp(f->name, name)) {
      if (!f->internal || !f->machine_registers_known)
        return 0;
      *mask = f->machine_register_mentions;
      return 1;
    }
  }
  return 0;
}
static void summarize_registers(cg_context *c, cg_function *f, size_t start) {
  static const char *const operations[] = {
    "mov", "movs", "movw", "movt", "mvn", "mvns",
    "add", "adds", "addw", "sub", "subs", "subw", "rsb", "rsbs",
    "adc", "adcs", "sbc", "sbcs", "and", "ands", "orr", "orrs",
    "eor", "eors", "bic", "bics", "orn", "orns",
    "lsl", "lsls", "lsr", "lsrs", "asr", "asrs", "ror", "rors",
    "mul", "muls", "mla", "mls", "sdiv", "udiv", "cmp", "cmn",
    "tst", "teq", "clz", "uxtb", "uxth", "sxtb", "sxth",
    "rev", "rev16", "revsh", "ldr", "ldrb", "ldrh", "ldrsb", "ldrsh",
    "str", "strb", "strh", "push", "pop", "nop", "it", "cbz", "cbnz"
  };
  unsigned mask = 0;
  int known = 1;
  char *body = cg_alloc(c, c->size - start + 1);
  memcpy(body, c->out + start, c->size - start);
  for (char *p = body, *limit = body + c->size - start; p < limit;) {
    char *end = strchr(p, '\n');
    if (!end) cg_error(c, "unterminated register summary instruction");
    *end = 0;
    if (!strncmp(p, "@ assembly-effects: known 0", 27)) known = 0;
    if (*p && *p != '@' && !strchr(p, ':')) {
      if (*p == '.') {
        if (strncmp(p, ".balign ", 8) && strncmp(p, ".type ", 6) &&
            strcmp(p, ".thumb_func") && strncmp(p, ".global ", 8) &&
            strncmp(p, ".word ", 6))
          known = 0;
      } else if (!strncmp(p, "bl ", 3)) {
        unsigned called;
        if (!cg_internal_call_mentions(c, p + 3, &called)) known = 0;
        else mask |= called;
      } else if (!strcmp(p, "bx lr")) {
        /* The return only changes PC; other BX/BLX targets remain opaque. */
      } else {
        char operation[16];
        unsigned n = 0;
        while (p[n] && p[n] != ' ' && p[n] != '.' && n < sizeof(operation) - 1) {
          operation[n] = p[n];
          n++;
        }
        operation[n] = 0;
        int supported = 0;
        for (unsigned k = 0; k < sizeof(operations) / sizeof(*operations); k++)
          if (!strcmp(operation, operations[k])) supported = 1;
        if (!strcmp(operation, "b") ||
            (operation[0] == 'b' && cg_inverse_condition(operation + 1))) {
          const char *target = strchr(p, ' ');
          supported = operation[0] == 'b' && target && !strncmp(target + 1, ".Lcg_", 5);
        }
        if (!supported) known = 0;
        mask |= mentioned_registers(c, p);
      }
    }
    p = end + 1;
  }
  f->machine_register_mentions = mask;
  f->machine_registers_known = known;
  cg_emit(c, "@ finalized-register-mentions: known %d mask %u\n", known, mask);
}
static void finalize_frame(cg_context *c, size_t header, size_t start) {
  cg_function *f = c->fn;
  char *body = cg_alloc(c, c->size - start + 1);
  memcpy(body, c->out + start, c->size - start);
  unsigned touched = 0;
  int calls = 0, opaque = 0;
  for (char *p = body; *p;) {
    char *end = strchr(p, '\n');
    if (!end) cg_error(c, "unterminated finalized frame instruction");
    *end = 0;
    if (!strncmp(p, "@ assembly-effects: known 0", 27)) opaque = 1;
    if (!strncmp(p, "@ frame-incoming:", 17)) touched |= 1u << 5 | 1u << 7;
    else if (*p != '@' && *p != '.' && !strchr(p, ':')) {
      touched |= mentioned_registers(c, p);
      if (!strncmp(p, "bl ", 3) || !strncmp(p, "blx ", 4)) calls = 1;
    }
    *end = '\n';
    p = end + 1;
  }
  int based = (touched & (1u << 7)) && (calls || opaque);
  if (!based) touched &= ~(1u << 7);
  unsigned mask = touched & 0x0ff0u;
  if (opaque) {
    mask |= c->register_saved_bytes == 32 ? 0x0df0u : 0x01f0u;
    based = 1;
    calls = 1;
  }
  if (based) mask |= 1u << 7;
  if (calls) mask |= 1u << 14;
  unsigned count = (unsigned)__builtin_popcount(mask);
  if ((count & 1u) && (calls || f->frame || (count == 1 && (mask & 0xff00u))))
    mask |= 1u << 3;
  c->register_saved_bytes = (unsigned)__builtin_popcount(mask) * 4;
  c->size = header;
  c->out[header] = 0;
  function_header(c, f);
  cg_emit(c, "@ lightweight-frame: mask %u base %s calls %d\n", mask, based ? "r7" : "sp", calls);
  if (mask) register_list(c, mask, 0);
  if (f->frame) {
    cg_immediate(c, 12, f->frame);
    cg_emit(c, "sub.w sp, sp, r12\n");
  }
  if (based) cg_emit(c, "mov r7, sp\n");
  c->frame_sp_relative = !based;
  for (char *p = body; *p;) {
    char *end = strchr(p, '\n');
    *end = 0;
    unsigned reg, offset;
    if (sscanf(p, "@ frame-incoming: %u %u", &reg, &offset) == 2)
      cg_stack(c, 0, reg, f->frame + c->register_saved_bytes + offset);
    else if (!based && *p != '@' && *p != '.') {
      for (char *q = p; *q; q++) {
        if (q[0] == 'r' && q[1] == '7' &&
            (q == p || (!isalnum((unsigned char)q[-1]) && q[-1] != '_')) &&
            !isalnum((unsigned char)q[2]) && q[2] != '_') {
          cg_emit(c, "sp");
          q++;
        } else cg_emit(c, "%c", *q);
      }
      cg_emit(c, "\n");
    } else cg_emit(c, "%s\n", p);
    p = end + 1;
  }
  cg_emit(c, ".Lcg_return%u:\n", f->id);
  if (based) cg_emit(c, "mov sp, r7\n");
  if (f->frame) {
    cg_immediate(c, 12, f->frame);
    cg_emit(c, "add.w sp, sp, r12\n");
  }
  if (mask) register_list(c, mask, 1);
  if (!calls) cg_emit(c, "bx lr\n");
  c->frame_sp_relative = 0;
}
static void finish_function(cg_context *c, size_t header, size_t body_start) {
  cg_function *f = c->fn;
  if (cg_light_frames(c)) finalize_frame(c, header, body_start);
  else {
    cg_emit(c, ".Lcg_return%u:\nmov sp, r7\n", f->id);
    cg_immediate(c, 12, f->frame);
    cg_emit(c, "add.w sp, sp, r12\n");
    cg_emit(c, c->register_saved_bytes == 32 ? "pop.w {r4-r8, r10, r11, pc}\n"
                                          : "pop.w {r4-r8, pc}\n");
  }
  if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS)
    summarize_registers(c, f, header);
  cg_emit(c, ".size %s, .-%s\n", f->name, f->name);
  c->fn = NULL;
}
void cg_generate_function(cg_context *c, cg_function *f) {
  c->schedule_active = 0;
  c->schedule_moves = c->schedule_windows = c->schedule_hazards = 0;
  c->schedule_unfilled = c->schedule_barriers = 0;
  c->fn = f;
  c->scalar_stream_retry = 0;
  c->frame_sp_relative = 0;
  c->call_entry_copies = c->call_stack_addresses = 0;
  c->word_copies = c->word_fills = c->word_bytes = c->tail_bytes = 0;
  c->layout_addresses = c->layout_literal_fallbacks = c->layout_relax_branches = 0;
  if (c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT)
    cg_register_prepare(c);
  unsigned phi = 0;
  for (cg_block *b = f->blocks; b; b = b->next) {
    unsigned n = 0;
    for (cg_inst *i = b->first; i; i = i->next)
      if (i->result && !strcmp(c->tokens[i->begin + 2].text, "phi"))
        n += cg_phi_buffer_size(c, i);
    if (n > phi)
      phi = n;
  }
  c->phi_space = cg_slot(c, phi, 8);
  unsigned objects[256] = {0};
  for (unsigned k = 0; k < f->argc; k++)
    if (f->byval[k])
      objects[k] =
          cg_slot(c, cg_round(f->byval[k], 4), f->arg_align[k] >= 8 ? 8 : 4);
  f->frame = cg_round(f->frame, 8);
  if (c->register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT)
    cg_register_prepare(c);
  cg_selection_prepare(c);
#ifdef CG_ATTRIBUTION_TRACE
  if (cg_schedule_selection(c))
    cg_error(c, "attribution trace requires unscheduled emission");
  cg_emit(c, "@ attribution-function %s\n", f->name);
  for (cg_value *v = f->values; v; v = v->next) {
    unsigned parameter = 0;
    for (unsigned k = 0; k < f->argc; k++)
      if (f->args[k] == v) parameter = 1;
    cg_emit(c, "@ attribution-slot %s %u %u %u %u %u\n", v->name,
            v->slot, cg_round(v->type->size, 4), v->object, v->object_size, parameter);
  }
  for (unsigned k = 0; k < c->register_capacity; k++)
    if (c->register_table[k].value)
      cg_emit(c, "@ attribution-register %s %u\n",
              c->register_table[k].value->name, c->register_table[k].reg);
  cg_emit(c, "@ attribution-phi %u %u\n", c->phi_space, phi);
#endif
  c->layout_active = (c->scalar_selection == BADGE_SELECTION_THUMB_LAYOUT ||
                      cg_branch_selection(c) ||
                      cg_compact_selection(c) || cg_schedule_selection(c) ||
                      cg_relax_selection(c)) &&
                     cg_global_residency(c);
  c->layout_fallthroughs = c->layout_inversions = 0;
  if (c->layout_active && !cg_control_selection(c))
    for (cg_block *b = f->blocks; b; b = b->next)
      for (cg_inst *i = b->first; i; i = i->next)
        for (unsigned k = i->begin; k < i->end; k++) {
          cg_register_work(c);
          const char *token = c->tokens[k].text;
          if (!strcmp(token, "volatile") || !strcmp(token, "asm") ||
              !strcmp(token, "call") || !strcmp(token, "inttoptr"))
            c->layout_active = 0;
        }
  if (f->section)
    cg_emit(c, ".section \"%s\",\"ax\",%%progbits\n", f->section);
  else
    cg_emit(c, ".section .text.%s,\"ax\",%%progbits\n", f->name);
  size_t header = c->size;
  function_header(c, f);
  size_t body_start = c->size;
  if (cg_emit_counted_leaf(c)) {
    finish_function(c, header, body_start);
    return;
  }
  if (!cg_light_frames(c)) {
    cg_emit(c, c->register_saved_bytes == 32 ? "push.w {r4-r8, r10, r11, lr}\n"
                                           : "push.w {r4-r8, lr}\n");
  cg_immediate(c, 12, f->frame);
  cg_emit(c, "sub.w sp, sp, r12\nmov r7, sp\n");
  }
  unsigned cursor = 0;
  for (unsigned k = 0; k < f->argc; k++) {
    if (cg_selection_forward_argument(c)) {
      c->call_entry_copies++;
      continue;
    }
    cg_value *v = f->args[k];
    if (f->arg_align[k] >= 8)
      cursor = cg_round(cursor, 2);
    unsigned words = cg_round(f->byval[k] ? f->byval[k] : v->type->size, 4) / 4;
    for (unsigned w = 0; w < words; w++, cursor++) {
      unsigned source = 5;
      if (cursor < 4 && cg_call_selection(c) && !f->byval[k]) {
        source = cursor;
        c->call_entry_copies++;
      } else if (cursor < 4)
        cg_emit(c, "mov r5, r%u\n", cursor);
      else if (cg_light_frames(c))
        cg_emit(c, "@ frame-incoming: %u %u\n", 5u, (cursor - 4) * 4);
      else
        cg_stack(c, 0, 5,
                 f->frame + c->register_saved_bytes + (cursor - 4) * 4);
      if (f->byval[k])
        cg_stack(c, 1, 5, objects[k] + w * 4);
      else {
        if (!w)
          cg_normalize(c, v->type, source);
        if (!cg_register_save(c, v, source, w))
          cg_stack(c, 1, source, v->slot + w * 4);
      }
    }
    if (f->byval[k]) {
      cg_address(c, 5, objects[k]);
      if (!cg_register_save(c, v, 5, 0))
        cg_stack(c, 1, 5, v->slot);
    }
  }
  for (cg_block *b = f->blocks; b; b = b->next) {
    c->schedule_active = 0;
    cg_register_block(c, b);
    cg_emit(c, ".Lcg_%u_%s:\n", f->id, b->name);
    c->schedule_active = cg_schedule_selection(c);
    for (cg_inst *i = b->first; i; i = i->next) {
#ifdef CG_ATTRIBUTION_TRACE
      cg_emit(c, "@ attribution-ir %u\n", c->tokens[i->begin].line);
#endif
      cg_instruction(c, b, i);
      cg_register_after_instruction(c);
    }
    cg_schedule_flush(c);
    c->schedule_active = 0;
  }
  cg_register_finish(c);
  cg_selection_finish(c);
  if (cg_call_selection(c))
    cg_emit(c, "@ call-transport: entry-copies-removed %u stack-addresses-removed %u\n",
            c->call_entry_copies, c->call_stack_addresses);
  if (c->scalar_selection == BADGE_SELECTION_THUMB_LAYOUT ||
      cg_branch_selection(c) ||
      cg_compact_selection(c) || cg_schedule_selection(c) || cg_relax_selection(c))
    cg_emit(c, "@ branch-layout: active %d fallthroughs %u inversions %u\n",
            c->layout_active, c->layout_fallthroughs, c->layout_inversions);
  if (cg_relax_selection(c))
    cg_emit(c, "@ final-layout: addresses %u literal-fallbacks %u relax-branches %u\n",
            c->layout_addresses, c->layout_literal_fallbacks, c->layout_relax_branches);
  if (cg_schedule_selection(c))
    cg_emit(c, "@ local-scheduling: windows %u hazards %u moved %u "
               "unfilled %u opaque-barriers %u work %u\n",
            c->schedule_windows, c->schedule_hazards, c->schedule_moves,
            c->schedule_unfilled, c->schedule_barriers, c->schedule_work);
  if (c->memory_selection == BADGE_MEMORY_WORD)
    cg_emit(c,
            "@ memory-selection: word-copies %u word-fills %u "
            "word-bytes %" PRIu64 " tail-bytes %" PRIu64 "\n",
            c->word_copies, c->word_fills, c->word_bytes, c->tail_bytes);
  if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS)
    cg_fold_memory_addresses(c, body_start);
  if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS)
    cg_forward_byte_arguments(c, body_start);
  if (cg_operand_selection(c)) cg_reuse_constants(c, body_start);
  if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS) {
    cg_reduce_branches(c, body_start);
    cg_fold_zero_branches(c, body_start);
  }
  finish_function(c, header, body_start);
}
