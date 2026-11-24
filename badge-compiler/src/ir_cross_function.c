#include "assembly_cost.h"
#include "codegen.h"
#include "ir_analysis.h"
#include "ir_integer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef IR_CROSS_FUNCTION_WORK_LIMIT
#define IR_CROSS_FUNCTION_WORK_LIMIT (UINT64_C(64) * 1024 * 1024)
#endif
#define CROSS_ATTEMPTS 8u
#define CROSS_PARAMETERS 8u
#define CROSS_INSTRUCTIONS 256u
#define GUARD_CALLER_SPILL_ALLOWANCE 4u

typedef struct {
  const char *name;
  unsigned frame, saves, spills;
} function_cost;
typedef struct {
  uint32_t bytes;
  unsigned count;
  function_cost *functions;
} module_cost;
typedef struct {
  badge_scalar_selection selection;
  size_t size;
  module_cost cost;
  function_cost functions[];
} cached_cost;
typedef struct {
  ir_context *c;
  ir_module *m;
  ir_analysis a;
  uint64_t work;
  unsigned *call_at_token, *attribute_lines, attribute_count,
      *first_reference, *next_reference;
  badge_scalar_selection selection;
} cross;
typedef enum { CROSS_SIZE, CROSS_HOT, CROSS_COMPACT } cross_policy;

static void spend(cross *p, uint64_t n) {
  if (n > p->work)
    ir_error(p->c, "cross-function work limit exceeded");
  p->work -= n;
}
static unsigned end_line(cross *p, unsigned f) {
  unsigned line = p->a.functions[f].line - 1;
  while (line < p->m->line_count) {
    spend(p, 1);
    const ir_line *row = &p->m->lines[line];
    if (row->count == 1 && !strcmp(ir_text(p->m, row->first), "}"))
      return line;
    line++;
  }
  ir_error(p->c, "cross-function cannot delimit function");
}
static int token(cross *p, unsigned line, const char *text) {
  const ir_line *row = &p->m->lines[line];
  for (unsigned k = row->first; k < row->first + row->count; k++) {
    spend(p, 1);
    if (!strcmp(ir_text(p->m, k), text))
      return 1;
  }
  return 0;
}
static int attribute(cross *p, unsigned f, const char *name) {
  unsigned header = p->a.functions[f].line - 1;
  if (token(p, header, name))
    return 1;
  const ir_line *row = &p->m->lines[header];
  for (unsigned k = row->first; k < row->first + row->count; k++) {
    const char *group = ir_text(p->m, k);
    if (*group != '#')
      continue;
    for (unsigned i = 0; i < p->attribute_count; i++) {
      unsigned l = p->attribute_lines[i];
      spend(p, 1);
      if (token(p, l, group) && token(p, l, name))
        return 1;
    }
  }
  return 0;
}
static void index_references(cross *p) {
  p->call_at_token = ir_allocate(p->c, p->m->count * sizeof(*p->call_at_token));
  p->first_reference =
      ir_allocate(p->c, p->a.function_count * sizeof(*p->first_reference));
  p->next_reference =
      ir_allocate(p->c, p->m->count * sizeof(*p->next_reference));
  for (unsigned k = 0; k < p->m->count; k++)
    p->call_at_token[k] = IR_NONE;
  ir_map functions = {0};
  ir_map_init(p->c, &functions, p->a.function_count + 1);
  for (unsigned f = 0; f < p->a.function_count; f++) {
    p->first_reference[f] = IR_NONE;
    ir_map_put(p->c, &functions, ir_name_key(p->c, p->a.functions[f].name),
               ir_format(p->c, "%u", f));
  }
  for (unsigned k = p->m->count; k-- > 0;) {
    const char *s = ir_text(p->m, k);
    const char *f =
        *s == '@' ? ir_map_get(&functions, ir_name_key(p->c, s)) : NULL;
    if (f) {
      unsigned target = (unsigned)strtoul(f, NULL, 10);
      p->next_reference[k] = p->first_reference[target];
      p->first_reference[target] = k;
    }
  }
  for (unsigned i = 0; i < p->a.instruction_count; i++) {
    const ir_instruction *in = &p->a.instructions[i];
    if (in->opcode == IR_OP_CALL && in->callee != IR_NONE &&
        !(in->effects & IR_EFFECT_OPAQUE) && !in->explicit_signature &&
        in->end_line == in->line)
      p->call_at_token[p->a.operands[in->callee].token] = i;
  }
  p->attribute_count = 0;
  p->attribute_lines =
      ir_allocate(p->c, p->m->line_count * sizeof(*p->attribute_lines));
  for (unsigned l = 0; l < p->m->line_count; l++) {
    const ir_line *row = &p->m->lines[l];
    if (row->count && !strcmp(ir_text(p->m, row->first), "attributes"))
      p->attribute_lines[p->attribute_count++] = l;
  }
  spend(p, p->m->count + p->a.instruction_count + p->m->line_count);
}
static void analyze(cross *p) {
  ir_analyze(p->c, p->m, &p->a);
  index_references(p);
}
static int symbolic_assembly(cross *p) {
  for (unsigned i = 0; i < p->a.instruction_count; i++) {
    const ir_instruction *in = &p->a.instructions[i];
    if (in->opcode != IR_OP_CALL || in->callee != IR_NONE)
      continue;
    const ir_line *row = &p->m->lines[in->line - 1];
    for (unsigned k = row->first; k < row->first + row->count; k++) {
      const char *s = ir_text(p->m, k);
      if (*s != '"')
        continue;
      spend(p, strlen(s));
      const char *const known[] = {"\"wfi\"",     "\"wfe\"",     "\"sev\"",
                                   "\"yield\"",   "\"dsb\"",     "\"dsb sy\"",
                                   "\"dmb sy\"",  "\"isb\"",     "\"isb sy\"",
                                   "\"cpsid i\"", "\"cpsie i\"", NULL};
      int safe = 0;
      for (unsigned j = 0; known[j]; j++)
        if (!strcmp(s, known[j]))
          safe = 1;
      if (!strncmp(s, "\"nop", 4)) {
        const char *q = s + 4;
        while (!strncmp(q, "\\0Anop", 6))
          q += 6;
        if (!strcmp(q, "\""))
          safe = 1;
      }
      if (!safe)
        return 1;
      break;
    }
  }
  return 0;
}
static void remember_cost(cross *p, unsigned slot, const char *source,
                          size_t size, module_cost cost) {
  const size_t limit = 8u * 1024 * 1024;
  if (slot == IR_ADMISSION_CACHE_ENTRIES || size > limit)
    return;
  size_t total = sizeof(cached_cost) + cost.count * sizeof(function_cost) + size;
  for (unsigned k = 0; k < cost.count; k++)
    total += strlen(cost.functions[k].name) + 1;
  if (total > limit - p->c->admission_cache_bytes)
    return;
  cached_cost *entry = malloc(total);
  if (!entry)
    ir_error(p->c, "out of memory caching cross-function admission");
  *entry = (cached_cost){.selection = p->selection, .size = size, .cost = cost};
  entry->cost.functions = entry->functions;
  char *text = (char *)(entry->functions + cost.count);
  memcpy(text, source, size);
  text += size;
  for (unsigned k = 0; k < cost.count; k++) {
    size_t n = strlen(cost.functions[k].name) + 1;
    memcpy(text, cost.functions[k].name, n);
    entry->functions[k] = cost.functions[k];
    entry->functions[k].name = text;
    text += n;
  }
  p->c->admission_cache[slot] = entry;
  p->c->admission_cache_bytes += total;
}
static module_cost measure(cross *p, const char *source, size_t size) {
  spend(p, size);
  unsigned slot = 0;
  for (; slot < IR_ADMISSION_CACHE_ENTRIES; slot++) {
    const cached_cost *entry = p->c->admission_cache[slot];
    if (!entry)
      break;
    if (entry->selection == p->selection && entry->size == size &&
        entry->cost.count <= p->a.function_count + 1 &&
        !memcmp(entry->functions + entry->cost.count, source, size))
      return entry->cost;
  }
  char *assembly = NULL;
  size_t length = 0;
  if (!badge_codegen_compile_for_admission(
          source, size, p->c->file, p->selection, &p->c->admission_functions,
          &assembly, &length))
    ir_error(p->c, "cross-function backend admission failed");
  module_cost result = {0};
  result.functions =
      ir_allocate(p->c, (p->a.function_count + 1) * sizeof(*result.functions));
  unsigned frame = 0, saves = 0, spills = 0, allocated;
  int have_frame = 0, have_spills = 0;
  for (char *line = assembly; *line;) {
    char name[256], kind[32];
    if (!strncmp(line, "@ global-allocation:", sizeof("@ global-allocation:") - 1) &&
        sscanf(line, "@ global-allocation: allocated %u spilled %u", &allocated,
               &spills) == 2)
      have_spills = 1;
    if (!strncmp(line, "@ stack-frame:", sizeof("@ stack-frame:") - 1) &&
        sscanf(line, "@ stack-frame: %u + %u saved-register bytes", &frame,
               &saves) == 2)
      have_frame = 1;
    if (!strncmp(line, ".type", sizeof(".type") - 1) &&
        sscanf(line, ".type %255[^,], %31s", name, kind) == 2 &&
        !strcmp(kind, "%function")) {
      if (!have_frame || !have_spills ||
          result.count == p->a.function_count + 1) {
        free(assembly);
        ir_error(p->c,
                 "cross-function admission lacks finalized allocation facts");
      }
      result.functions[result.count++] = (function_cost){
          ir_copy(p->c, name, strlen(name)), frame, saves, spills};
      have_frame = have_spills = 0;
    }
    char *next = strchr(line, '\n');
    if (!next)
      break;
    line = next + 1;
  }
  int ok =
      badge_assembly_code_bytes(assembly, length, p->c->file, &result.bytes);
  free(assembly);
  if (!ok)
    ir_error(p->c, "cross-function final-encoding admission failed");
  remember_cost(p, slot, source, size, result);
  return result;
}
static const char *assembly_name(cross *p, const char *name) {
  name++;
  if (*name == '"')
    return ir_copy(p->c, name + 1, strlen(name) - 2);
  return name;
}
static function_cost *find_cost(module_cost *cost, const char *name) {
  for (unsigned k = 0; k < cost->count; k++)
    if (!strcmp(cost->functions[k].name, name))
      return &cost->functions[k];
  return NULL;
}
static int fits_allocation(cross *p, module_cost *before, module_cost *after,
                           unsigned f, const char *clone,
                           const char *guard_caller) {
  function_cost *generic =
      find_cost(before, assembly_name(p, p->a.functions[f].name));
  if (!generic)
    ir_error(p->c, "cross-function missing helper allocation");
  for (unsigned k = 0; k < after->count; k++) {
    function_cost *b = &after->functions[k], *a = find_cost(before, b->name);
    if (!a && clone && !strcmp(b->name, assembly_name(p, clone)))
      a = generic;
    if (!a)
      ir_error(p->c, "cross-function unexpected function inventory");
    unsigned spill_allowance = guard_caller && !strcmp(b->name, guard_caller)
                                   ? GUARD_CALLER_SPILL_ALLOWANCE
                                   : 0;
    const char *reason = b->frame > a->frame     ? "final_frame_growth"
                         : b->saves > a->saves   ? "final_save_growth"
                         : b->spills > a->spills + spill_allowance ? "final_spill_growth"
                                                 : NULL;
    if (reason) {
      ir_note(p->c, "skipped", reason, 1);
      return 0;
    }
  }
  return 1;
}
static int admit(cross *p, module_cost *before, module_cost *after, unsigned f,
                 const char *clone) {
  if (!fits_allocation(p, before, after, f, clone, NULL))
    return 0;
  if (after->bytes >= before->bytes) {
    ir_note(p->c, "skipped", "whole_module_not_smaller", 1);
    return 0;
  }
  return 1;
}
static void reset(cross *p) {
  for (unsigned k = 0; k < p->m->count; k++)
    p->m->tokens[k].replacement = NULL;
  for (unsigned k = 0; k < p->m->line_count; k++)
    p->m->lines[k].replacement = NULL;
  p->m->suffix = NULL;
}
static char *write_module(cross *p, size_t *size) {
  p->c->size = 0;
  ir_write(p->c, p->m);
  *size = p->c->size;
  return ir_copy(p->c, p->c->output, *size);
}
static int recursive(cross *p, unsigned root) {
  unsigned char *seen = ir_allocate(p->c, p->a.function_count);
  unsigned *queue = ir_allocate(p->c, p->a.function_count * sizeof(*queue));
  unsigned count = 1;
  queue[0] = root;
  seen[root] = 1;
  for (unsigned at = 0; at < count; at++) {
    const ir_function *fn = &p->a.functions[queue[at]];
    if (!fn->block_count)
      continue;
    const ir_block *last = &p->a.blocks[fn->first_block + fn->block_count - 1];
    unsigned end = last->first_instruction + last->instruction_count;
    for (unsigned i = p->a.blocks[fn->first_block].first_instruction; i < end; i++) {
      spend(p, 1);
      const ir_instruction *in = &p->a.instructions[i];
      if (in->opcode != IR_OP_CALL || in->callee == IR_NONE)
        continue;
      const ir_operand *op = &p->a.operands[in->callee];
      if (op->kind != IR_OPERAND_GLOBAL)
        continue;
      unsigned f = p->a.globals[op->definition].function;
      if (f == root)
        return 1;
      if (f != IR_NONE && !seen[f]) {
        seen[f] = 1;
        queue[count++] = f;
      }
    }
  }
  return 0;
}
static int eligible(cross *p, unsigned f, unsigned *calls, unsigned *count) {
  const ir_function *fn = &p->a.functions[f];
  const ir_type *result = &p->a.types[fn->return_type];
  if (result->kind != IR_VOID && result->kind != IR_PTR &&
      (result->kind != IR_INT || result->bits > 32))
    return 0;
  if (fn->declaration || fn->variadic ||
      fn->parameter_count > CROSS_PARAMETERS ||
      (!token(p, fn->line - 1, "internal") &&
       !token(p, fn->line - 1, "private")) ||
      token(p, fn->line - 1, "section") || attribute(p, f, "noinline") ||
      attribute(p, f, "optnone") || attribute(p, f, "naked"))
    return 0;
  const char *const unsupported_abi[] = {
      "byval",     "sret",       "inalloca", "preallocated", "inreg",  "nest",
      "swiftself", "swifterror", "returned", "fastcc",       "coldcc", NULL};
  for (unsigned k = 0; unsupported_abi[k]; k++)
    if (token(p, fn->line - 1, unsupported_abi[k])) {
      ir_note(p->c, "skipped", "unsupported_helper_abi", 1);
      return 0;
    }
  unsigned instructions = 0;
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++)
    instructions += p->a.blocks[b].instruction_count;
  if (instructions > CROSS_INSTRUCTIONS)
    return 0;
  for (unsigned k = 0; k < fn->parameter_count; k++) {
    const ir_type *type = &p->a.types[p->a.parameters[fn->first_parameter + k]];
    if ((type->kind != IR_INT || type->bits > 32) && type->kind != IR_PTR)
      return 0;
  }
  *count = 0;
  unsigned name_token = IR_NONE;
  const ir_line *header = &p->m->lines[fn->line - 1];
  for (unsigned k = header->first; k < header->first + header->count; k++)
    if (!strcmp(ir_text(p->m, k), fn->name))
      name_token = k;
  for (unsigned k = p->first_reference[f]; k != IR_NONE; k = p->next_reference[k]) {
    spend(p, 1);
    if (k == name_token)
      continue;
    unsigned found = p->call_at_token[k];
    if (found == IR_NONE) {
      ir_note(p->c, "skipped", "address_taken_or_unknown_call", 1);
      return 0;
    }
    calls[(*count)++] = found;
  }
  if (!*count || recursive(p, f)) {
    ir_note(p->c, "skipped", "unused_or_recursive_helper", 1);
    return 0;
  }
  return 1;
}
static void remove_function(cross *p, unsigned f) {
  for (unsigned l = p->a.functions[f].line - 1, end = end_line(p, f); l <= end;
       l++)
    p->m->lines[l].replacement = "";
}
static char *reduced_arguments(cross *p, unsigned line, const char *callee,
                               const char **values) {
  const ir_line *row = &p->m->lines[line];
  p->c->size = 0;
  unsigned k = row->first, end = row->first + row->count;
  while (k < end) {
    const ir_token *t = &p->m->tokens[k];
    const char *s = t->replacement ? t->replacement : t->text;
    ir_emit(p->c, "%s ", s);
    k++;
    if (!strcmp(t->text, callee))
      break;
  }
  if (k == end || strcmp(ir_text(p->m, k), "("))
    ir_error(p->c, "cross-function cannot locate argument list");
  ir_emit(p->c, "( ");
  k++;
  unsigned parameter = 0, kept = 0;
  while (k < end && strcmp(ir_text(p->m, k), ")")) {
    unsigned begin = k, depth = 0;
    while (k < end) {
      const char *s = ir_text(p->m, k);
      if (!depth && (!strcmp(s, ",") || !strcmp(s, ")")))
        break;
      if (!strcmp(s, "("))
        depth++;
      if (!strcmp(s, ")"))
        depth--;
      k++;
    }
    if (parameter == CROSS_PARAMETERS)
      ir_error(p->c, "cross-function argument budget exceeded");
    if (!values[parameter]) {
      if (kept++)
        ir_emit(p->c, ", ");
      for (unsigned j = begin; j < k; j++) {
        const ir_token *t = &p->m->tokens[j];
        ir_emit(p->c, "%s ", t->replacement ? t->replacement : t->text);
      }
    }
    parameter++;
    if (k < end && !strcmp(ir_text(p->m, k), ","))
      k++;
  }
  for (; k < end; k++) {
    const ir_token *t = &p->m->tokens[k];
    ir_emit(p->c, "%s ", t->replacement ? t->replacement : t->text);
  }
  ir_emit(p->c, "\n");
  return ir_copy(p->c, p->c->output, p->c->size);
}
static const char *literal_helper_return(ir_context *c) {
  ir_module module = {0};
  ir_parse(c, &module, c->output, c->size);
  ir_analysis a;
  ir_analyze(c, &module, &a);
  for (unsigned f = 0; f < a.function_count; f++) {
    const ir_function *fn = &a.functions[f];
    if (fn->declaration)
      continue;
    unsigned block = fn->first_block;
    for (unsigned steps = 0; steps < fn->block_count; steps++) {
      const ir_block *b = &a.blocks[block];
      if (b->instruction_count != 1)
        break;
      const ir_instruction *in = &a.instructions[b->first_instruction];
      if (in->opcode == IR_OP_BR && b->successor_count == 1 && in->operand_count == 1) {
        block = a.successors[b->first_successor];
        continue;
      }
      if (in->opcode == IR_OP_RET && in->operand_count == 1) {
        const ir_operand *op = &a.operands[in->first_operand];
        const char *value = ir_text(&module, op->token);
        uint64_t bits;
        if (op->kind == IR_OPERAND_LITERAL &&
            (ir_integer(value, &bits) || !strcmp(value, "true") || !strcmp(value, "false")))
          return value;
      }
      break;
    }
  }
  return NULL;
}
static char *cleanup_helper(cross *p, unsigned f, const char **values,
                            const char *clone, const char **folded) {
  ir_module *m = p->m;
  const ir_function *fn = &p->a.functions[f];
  unsigned begin = fn->line - 1, end = end_line(p, f);
  reset(p);
  for (unsigned d = fn->first_definition;
       d < fn->first_definition + fn->parameter_count; d++) {
    unsigned parameter = d - fn->first_definition;
    if (!values[parameter])
      continue;
    for (unsigned u = p->a.definitions[d].first_use; u != IR_NONE;
         u = p->a.operands[u].next_use)
      m->tokens[p->a.operands[u].token].replacement = values[parameter];
  }
  if (clone) {
    const ir_line *header = &m->lines[begin];
    for (unsigned k = header->first; k < header->first + header->count; k++)
      if (!strcmp(ir_text(m, k), fn->name))
        m->tokens[k].replacement = clone;
  }
  m->lines[begin].replacement = reduced_arguments(p, begin, fn->name, values);
  p->c->size = 0;
  for (unsigned l = 0; l < m->line_count; l++) {
    if (l == begin) {
      l = end;
      continue;
    }
    const ir_line *row = &m->lines[l];
    if (row->count && !strcmp(ir_text(m, row->first), "define")) {
      /* Temporary declarations provide call signatures without optimizing
       * callers. */
      ir_emit(p->c, "declare ");
      for (unsigned k = row->first + 1; k < row->first + row->count; k++) {
        const char *s = ir_text(m, k);
        if (!strcmp(s, "{"))
          break;
        if (!strcmp(s, "internal") || !strcmp(s, "private"))
          continue;
        ir_emit(p->c, "%s ", s);
      }
      ir_emit(p->c, "\n");
      while (l < m->line_count && !token(p, l, "}"))
        l++;
      continue;
    }
    ir_append(p->c, m->source + row->begin, row->end - row->begin);
  }
  size_t prefix_size = p->c->size;
  char *prefix = ir_copy(p->c, p->c->output, prefix_size);
  for (unsigned l = 0; l < m->line_count; l++)
    if (l < begin || l > end)
      m->lines[l].replacement = "";
  size_t helper_size;
  char *helper = write_module(p, &helper_size);
  char *source = ir_format(p->c, "%s%s", prefix, helper);
  spend(p, prefix_size + helper_size);
  ir_context *child = calloc(1, sizeof(*child));
  if (!child)
    ir_error(p->c, "cross-function cannot allocate helper optimizer");
  child->file = p->c->file;
  char *result = NULL;
  int ok = 0;
  if (!setjmp(child->failure)) {
    ir_pipeline(child, source, prefix_size + helper_size,
                "constant-fold,scalar-cleanup,sccp,dead-instructions");
    const char *start = strstr(child->output, "define ");
    const char *finish = start ? strstr(start, "\n}") : NULL;
    if (!finish)
      ir_error(child, "cross-function missing optimized helper");
    result = ir_copy(p->c, start, (size_t)(finish + 3 - start));
    if (folded) {
      const char *value = literal_helper_return(child);
      if (value)
        *folded = ir_copy(p->c, value, strlen(value));
    }
    ok = 1;
  }
  ir_destroy(child);
  free(child);
  reset(p);
  if (!ok)
    ir_error(p->c,
             ir_format(p->c, "cross-function helper optimization failed for %s",
                       fn->name));
  return result;
}
static int fold_constant_calls(cross *p, unsigned f, const unsigned *calls,
                               unsigned count, module_cost *baseline,
                               char **best, uint32_t *best_bytes) {
  enum { LIMIT = 16 };
  const ir_function *fn = &p->a.functions[f];
  const ir_type *result = &p->a.types[fn->return_type];
  if (result->kind != IR_INT || result->bits > 32 || count > LIMIT)
    return 0;
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++)
    for (unsigned i = p->a.blocks[b].first_instruction;
         i < p->a.blocks[b].first_instruction + p->a.blocks[b].instruction_count; i++)
      if (p->a.instructions[i].effects & ~IR_EFFECT_CONTROL)
        return 0;
  const char *literals[LIMIT] = {0};
  unsigned folded = 0;
  for (unsigned n = 0; n < count; n++) {
    const ir_instruction *call = &p->a.instructions[calls[n]];
    if (call->definition == IR_NONE)
      continue;
    const char *values[CROSS_PARAMETERS] = {0};
    int constant = 1;
    for (unsigned k = 0; k < fn->parameter_count; k++) {
      const ir_operand *op = &p->a.operands[call->first_operand + 1 + k];
      const char *value = ir_text(p->m, op->token);
      uint64_t bits;
      if (op->kind != IR_OPERAND_LITERAL ||
          (!ir_integer(value, &bits) && strcmp(value, "true") && strcmp(value, "false"))) {
        constant = 0;
        break;
      }
      values[k] = value;
    }
    if (!constant)
      continue;
    int cached = 0;
    for (unsigned prev = 0; prev < n && !cached; prev++) {
      if (!literals[prev])
        continue;
      const ir_instruction *prior = &p->a.instructions[calls[prev]];
      unsigned k = 0;
      while (k < fn->parameter_count &&
             !strcmp(values[k], ir_text(p->m, p->a.operands[prior->first_operand + 1 + k].token)))
        k++;
      if (k == fn->parameter_count) {
        literals[n] = literals[prev];
        cached = 1;
      }
    }
    if (!cached)
      (void)cleanup_helper(p, f, values, NULL, &literals[n]);
    folded += literals[n] != NULL;
  }
  if (!folded)
    return 0;
  for (unsigned n = 0; n < count; n++) {
    if (!literals[n])
      continue;
    const ir_instruction *call = &p->a.instructions[calls[n]];
    for (unsigned u = p->a.definitions[call->definition].first_use; u != IR_NONE;
         u = p->a.operands[u].next_use)
      p->m->tokens[p->a.operands[u].token].replacement = literals[n];
    p->m->lines[call->line - 1].replacement = "";
  }
  if (folded == count)
    remove_function(p, f);
  size_t size;
  char *candidate = write_module(p, &size);
  module_cost cost = measure(p, candidate, size);
  ir_note(p->c, "candidate", "literal_result_calls", folded);
  int accepted = admit(p, baseline, &cost, f, NULL) && cost.bytes < *best_bytes;
  if (accepted) {
    *best = candidate;
    *best_bytes = cost.bytes;
  }
  reset(p);
  return accepted;
}
static int specialize_values(cross *p, unsigned f, const unsigned *calls,
                             unsigned count, const char **values,
                             module_cost *baseline, char **best,
                             uint32_t *best_bytes, const char **best_clone,
                             int check_default) {
  const ir_function *fn = &p->a.functions[f];
  unsigned char *selected = ir_allocate(p->c, count);
  unsigned matches = 0;
  for (unsigned i = 0; i < count; i++) {
    selected[i] = 1;
    const ir_instruction *in = &p->a.instructions[calls[i]];
    for (unsigned k = 0; k < fn->parameter_count; k++)
      if (values[k] &&
          strcmp(values[k],
                 ir_text(p->m, p->a.operands[in->first_operand + 1 + k].token)))
        selected[i] = 0;
    matches += selected[i];
  }
  const char *clone = matches == count ? NULL : ir_fresh(p->c, p->m, '@');
  char *helper = cleanup_helper(p, f, values, clone, NULL);
  if (clone) {
    p->m->suffix = helper;
  } else {
    remove_function(p, f);
    p->m->lines[fn->line - 1].replacement = helper;
  }
  for (unsigned i = 0; i < count; i++)
    if (selected[i]) {
      const ir_instruction *in = &p->a.instructions[calls[i]];
      if (clone)
        p->m->tokens[p->a.operands[in->callee].token].replacement = clone;
      p->m->lines[in->line - 1].replacement = reduced_arguments(
          p, in->line - 1, p->m->tokens[p->a.operands[in->callee].token].text,
          values);
    }
  size_t size;
  char *candidate = write_module(p, &size);
  module_cost cost = measure(p, candidate, size);
  ir_note(p->c, "candidate",
          clone ? "constant_clone_with_generic_retained"
                : "constant_helper_specialization",
          1);
  int accepted =
      admit(p, baseline, &cost, f, clone) && cost.bytes < *best_bytes;
  if (accepted && check_default) {
    badge_scalar_selection previous = p->selection;
    p->selection = BADGE_DEFAULT_SCALAR_SELECTION;
    module_cost current_before = measure(p, p->m->source, p->m->size);
    module_cost current_after = measure(p, candidate, size);
    p->selection = previous;
    accepted = admit(p, &current_before, &current_after, f, clone);
  }
  if (accepted) {
    *best = candidate;
    *best_bytes = cost.bytes;
    *best_clone = clone;
  }
  reset(p);
  return accepted;
}

static int specialize(cross *p, unsigned f, const unsigned *calls,
                      unsigned count, module_cost *baseline, char **best,
                      uint32_t *best_bytes, const char **best_clone) {
  const ir_function *fn = &p->a.functions[f];
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++)
    for (unsigned i = p->a.blocks[b].first_instruction;
         i < p->a.blocks[b].first_instruction + p->a.blocks[b].instruction_count;
         i++)
      if (p->a.instructions[i].effects & IR_EFFECT_OPAQUE) {
        ir_note(p->c, "skipped", "opaque_specialization_contract", 1);
        return 0;
      }
  const char *values[CROSS_PARAMETERS] = {0};
  unsigned literals = 0;
  for (unsigned i = 0; i < count; i++) {
    const ir_instruction *call = &p->a.instructions[calls[i]];
    const char *candidate[CROSS_PARAMETERS] = {0};
    unsigned known = 0;
    for (unsigned k = 0; k < fn->parameter_count; k++) {
      spend(p, 1);
      const ir_operand *op = &p->a.operands[call->first_operand + 1 + k];
      uint64_t unused;
      const char *text = ir_text(p->m, op->token);
      if (op->kind == IR_OPERAND_LITERAL &&
          (ir_integer(text, &unused) || !strcmp(text, "null"))) {
        candidate[k] = text;
        known++;
      }
    }
    if (known > literals) {
      memcpy(values, candidate, sizeof(values));
      literals = known;
    }
  }
  if (!literals)
    return 0;
  int accepted = specialize_values(p, f, calls, count, values, baseline, best,
                                    best_bytes, best_clone, 0);
  if (literals == 1 || count < 3 || count > 32)
    return accepted;
  unsigned shared = 1, parameter = 0;
  const char *value = NULL;
  for (unsigned k = 0; k < fn->parameter_count; k++) {
    for (unsigned i = 0; i < count; i++) {
      const ir_instruction *call = &p->a.instructions[calls[i]];
      const ir_operand *op = &p->a.operands[call->first_operand + 1 + k];
      const char *text = ir_text(p->m, op->token);
      uint64_t unused;
      if (op->kind != IR_OPERAND_LITERAL || !ir_integer(text, &unused))
        continue;
      unsigned matches = 1;
      for (unsigned j = i + 1; j < count; j++) {
        spend(p, 1);
        const ir_instruction *other = &p->a.instructions[calls[j]];
        const ir_operand *arg = &p->a.operands[other->first_operand + 1 + k];
        matches += arg->kind == IR_OPERAND_LITERAL &&
            !strcmp(text, ir_text(p->m, arg->token));
      }
      if (matches > shared) {
        shared = matches;
        parameter = k;
        value = text;
      }
    }
  }
  if (!value)
    return accepted;
  const char *group[CROSS_PARAMETERS] = {0};
  group[parameter] = value;
  if (specialize_values(p, f, calls, count, group, baseline, best,
                        best_bytes, best_clone, 1)) {
    ir_note(p->c, "transformed", "repeated_argument_group", shared);
    return 1;
  }
  return accepted;
}
static int share_helper(cross *p, unsigned f, const unsigned *calls,
                        unsigned count, module_cost *baseline, char **best,
                        uint32_t *best_bytes) {
  const ir_function *fn = &p->a.functions[f];
  const ir_line *header = &p->m->lines[fn->line - 1];
  unsigned *other_calls =
      ir_allocate(p->c, p->a.instruction_count * sizeof(*other_calls));
  for (unsigned other = 0; other < f; other++) {
    unsigned other_count;
    if (!eligible(p, other, other_calls, &other_count))
      continue;
    const ir_function *target = &p->a.functions[other];
    const ir_line *target_header = &p->m->lines[target->line - 1];
    if (target_header->count != header->count)
      continue;
    int same = 1;
    for (unsigned k = 0; k < header->count; k++) {
      const char *a = ir_text(p->m, header->first + k),
                 *b = ir_text(p->m, target_header->first + k);
      spend(p, 1);
      if (!strcmp(a, fn->name) && !strcmp(b, target->name))
        continue;
      if (strcmp(a, b)) {
        same = 0;
        break;
      }
    }
    if (!same)
      continue;
    unsigned begin = fn->line, end = end_line(p, f);
    unsigned target_begin = target->line, target_end = end_line(p, other);
    size_t from = p->m->lines[begin].begin, to = p->m->lines[end].end;
    size_t target_from = p->m->lines[target_begin].begin,
           target_to = p->m->lines[target_end].end;
    if (to - from != target_to - target_from)
      continue;
    spend(p, to - from);
    if (memcmp(p->m->source + from, p->m->source + target_from, to - from))
      continue;
    reset(p);
    for (unsigned i = 0; i < count; i++)
      p->m->tokens[p->a.operands[p->a.instructions[calls[i]].callee].token]
          .replacement = target->name;
    remove_function(p, f);
    size_t size;
    char *candidate = write_module(p, &size);
    module_cost cost = measure(p, candidate, size);
    ir_note(p->c, "candidate", "identical_helper_sharing", 1);
    int accepted =
        admit(p, baseline, &cost, f, NULL) && cost.bytes < *best_bytes;
    if (accepted) {
      *best = candidate;
      *best_bytes = cost.bytes;
    }
    reset(p);
    return accepted;
  }
  return 0;
}
static const ir_block *inline_body(cross *p, unsigned f, int entry_jump) {
  const ir_function *fn = &p->a.functions[f];
  if (!fn->block_count)
    return NULL;
  const ir_block *block = &p->a.blocks[fn->first_block];
  if (fn->block_count != 1) {
    if (!entry_jump || fn->block_count != 2 || block->instruction_count != 1 ||
        block->successor_count != 1 ||
        p->a.successors[block->first_successor] != fn->first_block + 1 ||
        p->a.instructions[block->first_instruction].opcode != IR_OP_BR ||
        p->a.instructions[block->first_instruction].operand_count != 1)
      return NULL;
    block++;
  }
  if (!block->instruction_count || block->instruction_count > 16 ||
      p->a.instructions[block->first_instruction + block->instruction_count - 1]
          .opcode != IR_OP_RET)
    return NULL;
  return block;
}

static int tiny_integer_leaf(cross *p, const ir_function *fn,
                             const ir_block *block) {
  if (fn->parameter_count != 1 ||
      p->a.types[p->a.parameters[fn->first_parameter]].bits != 32 ||
      p->a.types[fn->return_type].kind != IR_INT ||
      p->a.types[fn->return_type].bits != 32 ||
      block->instruction_count > 5)
    return 0;
  for (unsigned k = 0; k + 1 < block->instruction_count; k++) {
    const ir_instruction *in = &p->a.instructions[block->first_instruction + k];
    if (in->opcode != IR_OP_BINARY || in->effects ||
        p->a.types[in->type].bits != 32)
      return 0;
  }
  return 1;
}

static int leaf_read(cross *p, const ir_function *fn, const ir_block *block) {
  if (fn->parameter_count ||
      (block->instruction_count != 2 && block->instruction_count != 3) ||
      p->a.types[fn->return_type].kind != IR_INT ||
      p->a.types[fn->return_type].bits != 32)
    return 0;
  const ir_instruction *load =
      &p->a.instructions[block->first_instruction + block->instruction_count - 2];
  const ir_instruction *ret = load + 1;
  if (load->opcode != IR_OP_LOAD || load->operand_count != 1 ||
      ret->operand_count != 1)
    return 0;
  if (block->instruction_count == 3) {
    const ir_instruction *address = load - 1;
    const ir_operand *ptr = &p->a.operands[load->first_operand];
    if (address->opcode != IR_OP_CAST || strcmp(address->operation, "inttoptr") ||
        address->operand_count != 1 ||
        p->a.operands[address->first_operand].kind != IR_OPERAND_LITERAL ||
        ptr->kind != IR_OPERAND_LOCAL || ptr->definition != address->definition)
      return 0;
  }
  const ir_operand *value = &p->a.operands[ret->first_operand];
  return value->kind == IR_OPERAND_LOCAL && value->definition == load->definition;
}

static int inline_calls(cross *p, unsigned f, const unsigned *calls,
                        unsigned count, module_cost *baseline, char **best,
                        uint32_t *best_bytes, int *forwarding,
                        cross_policy policy, int tiny_budget) {
  const ir_function *fn = &p->a.functions[f];
  const ir_block *block = inline_body(p, f, policy != CROSS_SIZE);
  if (!block)
    return 0;
  const ir_type *ret = &p->a.types[fn->return_type];
  if (ret->kind != IR_VOID && ret->kind != IR_INT && ret->kind != IR_PTR)
    return 0;
  const ir_instruction *first = &p->a.instructions[block->first_instruction];
  const ir_instruction *last = &p->a.instructions[block->first_instruction +
                                                  block->instruction_count - 1];
  int forward =
      policy != CROSS_COMPACT && block->instruction_count == 2 && first->opcode == IR_OP_CALL &&
      first->callee != IR_NONE &&
      p->a.operands[first->callee].kind == IR_OPERAND_GLOBAL &&
      ((ret->kind == IR_VOID && !last->operand_count) ||
       (last->operand_count == 1 &&
        p->a.operands[last->first_operand].kind == IR_OPERAND_LOCAL &&
        p->a.operands[last->first_operand].definition == first->definition));
  for (unsigned i = block->first_instruction;
       i < block->first_instruction + block->instruction_count; i++) {
    const ir_instruction *in = &p->a.instructions[i];
    if (in->effects & IR_EFFECT_OPAQUE || in->opcode == IR_OP_ALLOCA || in->opcode == IR_OP_PHI ||
        (in->opcode == IR_OP_CALL && !forward) ||
        (in->opcode == IR_OP_RET &&
         i + 1 != block->first_instruction + block->instruction_count))
      return 0;
  }
  reset(p);
  for (unsigned n = 0; n < count; n++) {
    const ir_instruction *call = &p->a.instructions[calls[n]];
    const char **names =
        ir_allocate(p->c, fn->definition_count * sizeof(*names));
    for (unsigned d = fn->first_definition;
         d < fn->first_definition + fn->definition_count; d++) {
      unsigned k = d - fn->first_definition;
      names[k] =
          k < fn->parameter_count
              ? ir_text(p->m, p->a.operands[call->first_operand + 1 + k].token)
              : ir_fresh(p->c, p->m, '%');
    }
    p->c->size = 0;
    for (unsigned i = block->first_instruction;
         i < block->first_instruction + block->instruction_count; i++) {
      const ir_instruction *in = &p->a.instructions[i];
      if (in->opcode == IR_OP_RET) {
        if (call->definition != IR_NONE) {
          const ir_operand *op = &p->a.operands[in->first_operand];
          const char *value = op->kind == IR_OPERAND_LOCAL
                                  ? names[op->definition - fn->first_definition]
                                  : ir_text(p->m, op->token);
          ir_emit(p->c, "  %s = freeze %s %s\n",
                  p->a.definitions[call->definition].name, ret->key, value);
        }
        continue;
      }
      if (in->end_line != in->line) {
        reset(p);
        return 0;
      }
      const ir_line *line = &p->m->lines[in->line - 1];
      size_t at = line->begin;
      for (unsigned k = line->first; k < line->first + line->count; k++) {
        const char *replacement = NULL;
        for (unsigned d = fn->first_definition;
             d < fn->first_definition + fn->definition_count; d++) {
          spend(p, 1);
          if (*ir_text(p->m, k) == '%' &&
              !strcmp(ir_name_key(p->c, ir_text(p->m, k)),
                      ir_name_key(p->c, p->a.definitions[d].name)))
            replacement = names[d - fn->first_definition];
        }
        if (!replacement)
          continue;
        const ir_token *t = &p->m->tokens[k];
        ir_append(p->c, p->m->source + at, t->begin - at);
        ir_append(p->c, replacement, strlen(replacement));
        at = t->end;
      }
      ir_append(p->c, p->m->source + at, line->end - at);
    }
    p->m->lines[call->line - 1].replacement =
        ir_copy(p->c, p->c->output, p->c->size);
  }
  remove_function(p, f);
  size_t size;
  char *candidate = write_module(p, &size);
  module_cost cost = measure(p, candidate, size);
  ir_note(p->c, "candidate",
          forward ? "tail_forwarder_inline_with_helper_removed"
                  : "straight_line_inline_with_helper_removed",
          1);
  int fits = fits_allocation(p, baseline, &cost, f, NULL, NULL);
  /* A tiny pure integer leaf may spend one halfword per removed call,
     capped at four. Leaf reads still need a size tie. */
  unsigned allowance = count >= 4 ? 8 : count * 2;
  int equal_size_read = cost.bytes == baseline->bytes && leaf_read(p, fn, block);
  int tiny = policy == CROSS_COMPACT && tiny_budget && count >= 2 && !*best &&
      cost.bytes >= baseline->bytes &&
      (equal_size_read ||
       (cost.bytes - baseline->bytes <= allowance &&
        tiny_integer_leaf(p, fn, block)));
  int accepted = fits && (cost.bytes < *best_bytes || tiny);
  if (accepted && tiny)
    ir_note(p->c, "transformed", equal_size_read ? "leaf_read_calls_removed" :
                                                  "tiny_integer_leaf_calls_removed", count);
  else if (fits && !accepted && cost.bytes >= baseline->bytes)
    ir_note(p->c, "skipped", "whole_module_not_smaller", 1);
  if (accepted) {
    *best = candidate;
    *best_bytes = cost.bytes;
    *forwarding = forward;
  }
  reset(p);
  return accepted;
}

enum { GUARD_BLOCK_LIMIT = 16, GUARD_INSTRUCTION_LIMIT = 32, GUARD_ARGS = 4 };

static int guard_i32(cross *p, unsigned type) {
  return p->a.types[type].kind == IR_INT && p->a.types[type].bits == 32;
}
static int guard_load(cross *p, const ir_instruction *in, const ir_line *row) {
  const ir_operand *addr = &p->a.operands[in->first_operand];
  if (in->effects != (IR_EFFECT_READ | IR_EFFECT_TRAP) ||
      row->count != 10 || !guard_i32(p, in->type) ||
      addr->kind != IR_OPERAND_GLOBAL ||
      strcmp(ir_text(p->m, row->first + 8), "align"))
    return 0;
  const ir_global *g = &p->a.globals[addr->definition];
  if (g->storage_type == IR_NONE)
    return 0;
  const ir_type *storage = &p->a.types[g->storage_type];
  unsigned l = g->line - 1;
  uint64_t alignment;
  if (!ir_integer(ir_text(p->m, row->first + 9), &alignment) ||
      alignment != 4 ||
      (!guard_i32(p, g->storage_type) &&
       (storage->kind != IR_ARRAY || storage->count != 4 ||
        p->a.types[storage->element].kind != IR_INT ||
        p->a.types[storage->element].bits != 8)))
    return 0;
  return (token(p, l, "internal") || token(p, l, "private")) &&
         !token(p, l, "section") && !token(p, l, "thread_local") &&
         !token(p, l, "externally_initialized");
}
static int guard_instruction(cross *p, const ir_instruction *in) {
  const ir_line *row = &p->m->lines[in->line - 1];
  if (in->end_line != in->line)
    return 0;
  for (unsigned k = row->first; k < row->first + row->count; k++)
    if (*ir_text(p->m, k) == '!')
      return 0;
  switch (in->opcode) {
  case IR_OP_LOAD:
    return guard_load(p, in, row);
  case IR_OP_BINARY:
    return row->count == 7 && guard_i32(p, in->type) &&
           (!strcmp(in->operation, "add") || !strcmp(in->operation, "sub"));
  case IR_OP_COMPARE:
    return row->count == 8 &&
           guard_i32(p, p->a.operands[in->first_operand].type);
  case IR_OP_PHI:
    return p->a.types[in->type].bits == 1;
  case IR_OP_BR:
    return 1;
  case IR_OP_RET:
    return row->count == 2 && !in->operand_count;
  default:
    return 0;
  }
}
static int named_function(cross *p, const ir_function *fn) {
  for (unsigned k = 0; k < fn->definition_count; k++) {
    const char *name = p->a.definitions[fn->first_definition + k].name + 1;
    if (*name >= '0' && *name <= '9')
      return 0;
  }
  for (unsigned k = 0; k < fn->block_count; k++) {
    const char *name = p->a.blocks[fn->first_block + k].name;
    if (*name >= '0' && *name <= '9')
      return 0;
  }
  return 1;
}
static int guard_loop_call(cross *p, unsigned block, const ir_function *fn) {
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++) {
    const ir_block *latch = &p->a.blocks[b];
    spend(p, 1);
    if (!ir_dominates(&p->a, block, b))
      continue;
    for (unsigned k = 0; k < latch->successor_count; k++)
      if (ir_dominates(&p->a, p->a.successors[latch->first_successor + k], block))
        return 1;
  }
  return 0;
}
static void guard_emit_row(cross *p, const ir_line *row) {
  for (unsigned k = row->first; k < row->first + row->count; k++) {
    const ir_token *t = &p->m->tokens[k];
    ir_emit(p->c, "%s%s", k == row->first ? "  " : " ",
            t->replacement ? t->replacement : t->text);
  }
  ir_emit(p->c, "\n");
}
static void guard_emit_instruction(cross *p, const ir_instruction *in,
                                   unsigned def_start, unsigned first,
                                   const char **names, const char **labels) {
  const ir_line *row = &p->m->lines[in->line - 1];
  if (in->definition != IR_NONE)
    p->m->tokens[row->first].replacement = names[in->definition - def_start];
  for (unsigned u = in->first_operand; u < in->first_operand + in->operand_count; u++) {
    const ir_operand *op = &p->a.operands[u];
    if (op->kind == IR_OPERAND_LOCAL)
      p->m->tokens[op->token].replacement = names[op->definition - def_start];
    else if (op->kind == IR_OPERAND_BLOCK)
      p->m->tokens[op->token].replacement = labels[op->block - first];
  }
  guard_emit_row(p, row);
}
static const char *guard_label(cross *p, const ir_function *caller) {
  for (;;) {
    const char *name = ir_fresh(p->c, p->m, '%');
    unsigned k = 0;
    for (; k < caller->block_count; k++) {
      spend(p, 1);
      if (!strcmp(name + 1, p->a.blocks[caller->first_block + k].name))
        break;
    }
    if (k == caller->block_count)
      return name;
  }
}
static char *split_guard(cross *p, unsigned f, const ir_instruction *call,
                         size_t *size) {
  const ir_function *fn = &p->a.functions[f];
  const ir_block *at = &p->a.blocks[call->block];
  const ir_function *caller = &p->a.functions[at->function];
  unsigned argc = fn->parameter_count, first = fn->first_block;
  const ir_line *head = &p->m->lines[fn->line - 1],
                *site = &p->m->lines[call->line - 1];
  if (call->definition != IR_NONE ||
      call->operand_count != argc + 1 ||
      strcmp(ir_text(p->m, site->first), "call") ||
      strcmp(ir_text(p->m, site->first + 1), "void") ||
      site->count != 5 + (argc ? 3 * argc - 1 : 0) ||
      !named_function(p, fn) || !named_function(p, caller) ||
      !guard_loop_call(p, call->block, caller))
    return NULL;
  unsigned arg_tokens[GUARD_ARGS], recompute[GUARD_ARGS], sink[GUARD_ARGS],
      recompute_count = 0;
  for (unsigned k = 0; k < argc; k++) {
    unsigned h = head->first + 5 + 3 * k;
    const ir_operand *arg = &p->a.operands[call->first_operand + 1 + k];
    if (strcmp(ir_text(p->m, h), "i32") ||
        strcmp(ir_text(p->m, h + 1), p->a.definitions[fn->first_definition + k].name) ||
        strcmp(ir_text(p->m, h + 2), k + 1 < argc ? "," : ")") ||
        arg->kind != IR_OPERAND_LOCAL || arg->token != site->first + 5 + 3 * k)
      return NULL;
    arg_tokens[k] = arg->token;
  }
  unsigned end = first + fn->block_count, body = first, instructions = 0, checks = 0, exits = 0;
  for (; body < end && body - first < GUARD_BLOCK_LIMIT; body++) {
    const ir_block *b = &p->a.blocks[body];
    unsigned i = 0;
    for (; i < b->instruction_count; i++) {
      spend(p, 1);
      if (!guard_instruction(p, &p->a.instructions[b->first_instruction + i]))
        break;
    }
    if (i != b->instruction_count)
      break;
    instructions += i;
    checks += b->successor_count == 2;
    exits += p->a.instructions[b->first_instruction + i - 1].opcode == IR_OP_RET;
  }
  if (!exits || !checks || body == end || body - first == GUARD_BLOCK_LIMIT ||
      instructions > GUARD_INSTRUCTION_LIMIT || p->a.blocks[body].predecessor_count != 1 ||
      p->a.instructions[p->a.blocks[body].first_instruction].opcode == IR_OP_PHI)
    return NULL;
  for (unsigned b = first; b < end; b++) {
    const ir_block *block = &p->a.blocks[b];
    for (unsigned k = 0; k < block->successor_count; k++) {
      unsigned s = p->a.successors[block->first_successor + k];
      if (b < body ? (s <= b || s > body) : s <= body)
        return NULL;
    }
  }
  for (unsigned k = argc; k < fn->definition_count; k++) {
    const ir_definition *d = &p->a.definitions[fn->first_definition + k];
    if (d->block >= body)
      continue;
    unsigned use = IR_NONE;
    int single_use = 1, live_out = 0;
    for (unsigned u = d->first_use; u != IR_NONE; u = p->a.operands[u].next_use) {
      spend(p, 1);
      unsigned i = p->a.operands[u].instruction;
      if (p->a.instructions[i].block >= body)
        live_out = 1;
      else {
        single_use &= use == IR_NONE || use == i;
        use = i;
      }
    }
    if (!live_out)
      continue;
    const ir_instruction *in = &p->a.instructions[d->instruction];
    if (in->opcode != IR_OP_BINARY || recompute_count == GUARD_ARGS)
      return NULL;
    for (unsigned j = in->first_operand; j < in->first_operand + in->operand_count; j++) {
      const ir_operand *op = &p->a.operands[j];
      if (op->kind == IR_OPERAND_LOCAL &&
          op->definition >= fn->first_definition + argc)
        return NULL;
    }
    recompute[recompute_count] = d->instruction;
    sink[recompute_count++] = single_use && use != IR_NONE &&
        p->a.instructions[use].opcode != IR_OP_PHI ? use : IR_NONE;
  }
  const char **names = ir_allocate(p->c, fn->definition_count * sizeof(*names));
  const char *labels[GUARD_BLOCK_LIMIT + 1], *resume = guard_label(p, caller);
  labels[0] = ir_format(p->c, "%%%s", at->name);
  for (unsigned b = first + 1; b <= body; b++)
    labels[b - first] = guard_label(p, caller);
  for (unsigned k = 0; k < argc; k++)
    names[k] = ir_text(p->m, arg_tokens[k]);
  for (unsigned k = argc; k < fn->definition_count; k++)
    if (p->a.definitions[fn->first_definition + k].block < body)
      names[k] = ir_fresh(p->c, p->m, '%');
  const ir_instruction *loads[GUARD_INSTRUCTION_LIMIT];
  unsigned load_count = 0;
  if (recompute_count) {
    const ir_line *label = &p->m->lines[p->a.blocks[body].line - 1];
    p->c->size = 0;
    ir_append(p->c, p->m->source + label->begin, label->end - label->begin);
    for (unsigned k = 0; k < recompute_count; k++) {
      const ir_line *row = &p->m->lines[p->a.instructions[recompute[k]].line - 1];
      ir_append(p->c, p->m->source + row->begin, row->end - row->begin);
    }
    p->m->lines[p->a.blocks[body].line - 1].replacement =
        ir_copy(p->c, p->c->output, p->c->size);
  }
  /* Move the guard CFG; only the continuation calls the original symbol. */
  p->c->size = 0;
  for (unsigned b = first; b < body; b++) {
    const ir_block *block = &p->a.blocks[b];
    if (b != first)
      ir_emit(p->c, "%s:\n", labels[b - first] + 1);
    for (unsigned k = 0; k < block->instruction_count; k++) {
      unsigned index = block->first_instruction + k;
      int delayed = 0;
      for (unsigned j = 0; j < recompute_count; j++)
        delayed |= recompute[j] == index && sink[j] != IR_NONE;
      if (delayed)
        continue;
      const ir_instruction *in = &p->a.instructions[index];
      if (in->opcode == IR_OP_RET) {
        ir_emit(p->c, "  br label %s\n", resume);
        continue;
      }
      if (in->opcode == IR_OP_LOAD) {
        const ir_operand *addr = &p->a.operands[in->first_operand];
        for (unsigned j = 0; j < load_count; j++)
          if (p->a.operands[loads[j]->first_operand].definition == addr->definition &&
              ir_dominates(&p->a, loads[j]->block, in->block)) {
            names[in->definition - fn->first_definition] =
                names[loads[j]->definition - fn->first_definition];
            delayed = 1;
            break;
          }
        if (delayed)
          continue;
        loads[load_count++] = in;
      }
      for (unsigned j = 0; j < recompute_count; j++) {
        if (sink[j] == index)
          guard_emit_instruction(p, &p->a.instructions[recompute[j]],
                                 fn->first_definition, first, names, labels);
      }
      guard_emit_instruction(p, in, fn->first_definition, first, names, labels);
    }
  }
  ir_emit(p->c, "%s:\n", labels[body - first] + 1);
  guard_emit_row(p, site);
  ir_emit(p->c, "  br label %s\n%s:\n", resume, resume + 1);
  p->m->lines[call->line - 1].replacement = ir_copy(p->c, p->c->output, p->c->size);
  for (unsigned l = fn->line; l < p->a.blocks[body].line - 1; l++)
    p->m->lines[l].replacement = "";
  for (unsigned s = 0; s < at->successor_count; s++) {
    const ir_block *next = &p->a.blocks[p->a.successors[at->first_successor + s]];
    for (unsigned i = 0; i < next->instruction_count; i++) {
      const ir_instruction *phi = &p->a.instructions[next->first_instruction + i];
      if (phi->opcode != IR_OP_PHI)
        break;
      for (unsigned u = phi->first_operand; u < phi->first_operand + phi->operand_count; u++) {
        const ir_operand *op = &p->a.operands[u];
        if (op->kind == IR_OPERAND_BLOCK && op->block == call->block)
          p->m->tokens[op->token].replacement = resume;
      }
    }
  }
  char *candidate = write_module(p, size);
  reset(p);
  return candidate;
}

void ir_guard_split(ir_context *c, ir_module *m) {
  cross p = {.c = c, .m = m, .work = IR_CROSS_FUNCTION_WORK_LIMIT,
             .selection = BADGE_DEFAULT_SCALAR_SELECTION};
  ir_analyze(c, m, &p.a);
  unsigned *calls = NULL;
  for (unsigned f = 0, attempts = 0; f < p.a.function_count && attempts < 2; f++) {
    const ir_function *fn = &p.a.functions[f];
    if (p.a.types[fn->return_type].kind != IR_VOID || fn->block_count < 3 ||
        fn->parameter_count > GUARD_ARGS ||
        strcmp(ir_text(m, m->lines[fn->line - 1].first + 2), "void"))
      continue;
    if (!calls) {
      if (symbolic_assembly(&p))
        return;
      index_references(&p);
      calls = ir_allocate(c, p.a.instruction_count * sizeof(*calls));
    }
    unsigned count;
    if (!eligible(&p, f, calls, &count) || count != 1)
      continue;
    ir_allocation *mark = c->allocations;
    ir_map names = m->names;
    size_t size;
    char *candidate = split_guard(&p, f, &p.a.instructions[calls[0]], &size);
    if (candidate) {
      attempts++;
      ir_note(c, "candidate", "loop_entry_guard", 1);
      ir_module checked = {0};
      ir_parse(c, &checked, candidate, size);
      ir_verify(c, &checked);
      module_cost before = measure(&p, m->source, m->size);
      module_cost after = measure(&p, candidate, size);
      const char *caller = assembly_name(&p,
          p.a.functions[p.a.blocks[p.a.instructions[calls[0]].block].function].name);
      if (after.bytes >= before.bytes)
        ir_note(c, "skipped", "whole_module_not_smaller", 1);
      else if (
          fits_allocation(&p, &before, &after, f, NULL, caller)) {
        ir_note(c, "transformed", "loop_entry_guard", 1);
        ir_note(c, "transformed", "whole_module_code_bytes_saved", before.bytes - after.bytes);
        for (unsigned l = 0; l < m->line_count; l++)
          m->lines[l].replacement = "";
        m->lines[0].replacement = candidate;
        ir_reparse(c, m, mark);
        c->size = 0;
        return;
      }
    }
    ir_release_to(c, mark);
    m->names = names;
  }
  c->size = 0;
}

static uint64_t *entry_counts(cross *p) {
  ir_map profiles = {0};
  ir_map_init(p->c, &profiles, p->m->line_count);
  for (unsigned l = 0; l < p->m->line_count; l++) {
    spend(p, 1);
    const ir_line *line = &p->m->lines[l];
    unsigned k = line->first;
    if (line->count != 10 || *ir_text(p->m, k) != '!' ||
        strcmp(ir_text(p->m, k + 1), "=") ||
        strcmp(ir_text(p->m, k + 2), "!") ||
        strcmp(ir_text(p->m, k + 3), "{") ||
        strcmp(ir_text(p->m, k + 4), "!") ||
        strcmp(ir_text(p->m, k + 5), "\"function_entry_count\"") ||
        strcmp(ir_text(p->m, k + 6), ",") ||
        strcmp(ir_text(p->m, k + 7), "i64") ||
        strcmp(ir_text(p->m, k + 9), "}"))
      continue;
    const char *count = ir_text(p->m, k + 8);
    uint64_t number;
    if (*count != '-' && ir_integer(count, &number))
      ir_map_put(p->c, &profiles, ir_text(p->m, k), count);
  }
  uint64_t *counts = ir_allocate(p->c, p->a.function_count * sizeof(*counts));
  for (unsigned f = 0; f < p->a.function_count; f++) {
    const ir_line *line = &p->m->lines[p->a.functions[f].line - 1];
    for (unsigned k = line->first; k + 1 < line->first + line->count; k++) {
      spend(p, 1);
      if (strcmp(ir_text(p->m, k), "!prof"))
        continue;
      const char *count = ir_map_get(&profiles, ir_text(p->m, k + 1));
      if (count)
        ir_integer(count, &counts[f]);
      break;
    }
  }
  return counts;
}

static void run_cross_function(ir_context *c, ir_module *m, cross_policy policy) {
  const int hot_inline = policy == CROSS_HOT;
  const int compact_inline = policy == CROSS_COMPACT;
  cross p = {.c = c, .m = m, .work = IR_CROSS_FUNCTION_WORK_LIMIT,
             .selection = compact_inline ? BADGE_SELECTION_THUMB_CONSTANTS : BADGE_SELECTION_THUMB_OPERANDS};
  ir_map attempted = {0};
  ir_map_init(c, &attempted, CROSS_ATTEMPTS * 2);
  char attempted_names[CROSS_ATTEMPTS][256];
  ir_allocation *analysis_mark = c->allocations;
  analyze(&p);
  for (unsigned k = 0; k < m->count; k++)
    if (!strcmp(ir_text(m, k), "musttail"))
      ir_error(c, "cross-function musttail is outside the supported call-frame "
                  "contract");
  for (unsigned f = 0; f < p.a.function_count; f++)
    if (attribute(&p, f, "alwaysinline"))
      ir_error(c, "cross-function alwaysinline is outside the supported "
                  "size-gated contract");
  if (symbolic_assembly(&p)) {
    ir_note(c, "skipped", "untracked_assembly_symbol_references", 1);
    return;
  }
  const char *marker = compact_inline ? "; badge-compact-inline: bounded selection complete" :
      hot_inline ? "; badge-hot-inline: bounded selection complete" :
      "; badge-cross-function: bounded selection complete";
  if (strstr(m->source, marker)) {
    ir_note(c, "skipped", "already_costed_module", 1);
    return;
  }
  unsigned changes = 0, attempts = 0;
  int tiny_budget = 1;
  while (attempts < CROSS_ATTEMPTS) {
    ir_allocation *mark = c->allocations;
    ir_map original_names = m->names;
    unsigned *calls = ir_allocate(c, p.a.instruction_count * sizeof(*calls));
    unsigned *other_calls = hot_inline ?
        ir_allocate(c, p.a.instruction_count * sizeof(*other_calls)) : calls;
    uint64_t *entries = hot_inline ? entry_counts(&p) : NULL;
    unsigned chosen = IR_NONE, count = 0;
    uint64_t hottest = 0;
    for (unsigned f = 0; f < p.a.function_count; f++) {
      if (ir_map_get(&attempted, p.a.functions[f].name))
        continue;
      if (entries && entries[f] <= hottest)
        continue;
      if (compact_inline && !inline_body(&p, f, 1))
        continue;
      unsigned n;
      if (eligible(&p, f, other_calls, &n)) {
        chosen = f;
        count = n;
        if (!entries)
          break;
        hottest = entries[f];
        memcpy(calls, other_calls, count * sizeof(*calls));
      }
    }
    if (chosen == IR_NONE) {
      ir_release_to(c, mark);
      break;
    }
    const char *name = p.a.functions[chosen].name;
    char *name_buffer = attempted_names[attempts];
    if (strlen(name) >= sizeof(attempted_names[0]))
      ir_error(c, "cross-function helper name limit exceeded");
    strcpy(name_buffer, name);
    attempts++;
    module_cost before = measure(&p, m->source, m->size);
    char *best = NULL;
    uint32_t best_bytes = before.bytes;
    const char *kind = "retained_shared_helper", *clone = NULL;
    if (policy == CROSS_SIZE && share_helper(&p, chosen, calls, count, &before, &best, &best_bytes))
      kind = "identical_helpers_shared";
    if (policy == CROSS_SIZE && specialize(&p, chosen, calls, count, &before, &best, &best_bytes,
                   &clone))
      kind = "constant_specialization";
    if (compact_inline && fold_constant_calls(&p, chosen, calls, count, &before,
                                               &best, &best_bytes))
      kind = "literal_result_calls_folded";
    int forwarding = 0;
    if (inline_calls(&p, chosen, calls, count, &before, &best, &best_bytes,
                     &forwarding, policy, tiny_budget)) {
      kind = compact_inline ? "compact_helper_inlined" :
          hot_inline ? "profiled_helper_inlined" :
          forwarding ? "tail_forwarder_inlined" : "straight_line_inlined";
      clone = NULL;
    }
    if (best) {
      if (best_bytes >= before.bytes)
        tiny_budget = 0;
      for (unsigned l = 0; l < m->line_count; l++)
        m->lines[l].replacement = "";
      m->lines[0].replacement = best;
      m->suffix = ir_format(c, "; badge-cross-decision: %s %s %s %u %u\n", kind,
                            assembly_name(&p, name_buffer),
                            clone ? assembly_name(&p, clone) : "-",
                            before.bytes, best_bytes);
      ir_note(c, "transformed", kind, 1);
      if (best_bytes < before.bytes)
        ir_note(c, "transformed", "whole_module_code_bytes_saved",
                before.bytes - best_bytes);
      ir_reparse(c, m, analysis_mark);
      if (attempts < CROSS_ATTEMPTS) {
        analysis_mark = c->allocations;
        analyze(&p);
      }
      changes++;
    } else {
      ir_note(c, "skipped", kind, 1);
      ir_release_to(c, mark);
      m->names = original_names;
    }
    ir_map_put(c, &attempted, name_buffer, "attempted");
  }
  if (attempts == CROSS_ATTEMPTS)
    ir_note(c, "skipped", "module_attempt_budget", 1);
  if (changes) {
    m->suffix = ir_format(c, "%s\n", marker);
    ir_reparse(c, m, c->allocations);
  }
  c->size = 0;
}

void ir_cross_function(ir_context *c, ir_module *m) { run_cross_function(c, m, CROSS_SIZE); }
void ir_hot_inline(ir_context *c, ir_module *m) { run_cross_function(c, m, CROSS_HOT); }
void ir_compact_inline(ir_context *c, ir_module *m) { run_cross_function(c, m, CROSS_COMPACT); }
