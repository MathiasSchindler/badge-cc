#include "assembly_cost.h"
#include "codegen.h"
#include "ir_analysis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef IR_CROSS_FUNCTION_WORK_LIMIT
#define IR_CROSS_FUNCTION_WORK_LIMIT (UINT64_C(64) * 1024 * 1024)
#endif
#define CROSS_ATTEMPTS 8u
#define CROSS_PARAMETERS 8u
#define CROSS_INSTRUCTIONS 256u

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
  ir_context *c;
  ir_module *m;
  ir_analysis a;
  uint64_t work;
  unsigned *call_at_token, *attribute_lines, attribute_count,
      *function_at_token;
} cross;

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
static void analyze(cross *p) {
  ir_analyze(p->c, p->m, &p->a);
  p->call_at_token = ir_allocate(p->c, p->m->count * sizeof(*p->call_at_token));
  p->function_at_token =
      ir_allocate(p->c, p->m->count * sizeof(*p->function_at_token));
  for (unsigned k = 0; k < p->m->count; k++)
    p->call_at_token[k] = IR_NONE;
  ir_map functions = {0};
  ir_map_init(p->c, &functions, p->a.function_count + 1);
  for (unsigned f = 0; f < p->a.function_count; f++)
    ir_map_put(p->c, &functions, ir_name_key(p->c, p->a.functions[f].name),
               ir_format(p->c, "%u", f));
  for (unsigned k = 0; k < p->m->count; k++) {
    const char *s = ir_text(p->m, k);
    const char *f =
        *s == '@' ? ir_map_get(&functions, ir_name_key(p->c, s)) : NULL;
    p->function_at_token[k] = f ? (unsigned)strtoul(f, NULL, 10) : IR_NONE;
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
static module_cost measure(cross *p, const char *source, size_t size) {
  spend(p, size);
  char *assembly = NULL;
  size_t length = 0;
  if (!badge_codegen_compile_with_selections(
          source, size, p->c->file,
          (badge_codegen_options){BADGE_FRAME_DIRECT,
                                  BADGE_RESIDENCY_GLOBAL_COMPACT},
          BADGE_SELECTION_THUMB_OPERANDS, BADGE_MEMORY_WORD, &assembly,
          &length))
    ir_error(p->c, "cross-function backend admission failed");
  module_cost result = {0};
  result.functions =
      ir_allocate(p->c, (p->a.function_count + 1) * sizeof(*result.functions));
  unsigned frame = 0, saves = 0, spills = 0, allocated;
  int have_frame = 0, have_spills = 0;
  for (char *line = assembly; *line;) {
    char name[256], kind[32];
    if (sscanf(line, "@ global-allocation: allocated %u spilled %u", &allocated,
               &spills) == 2)
      have_spills = 1;
    if (sscanf(line, "@ stack-frame: %u + %u saved-register bytes", &frame,
               &saves) == 2)
      have_frame = 1;
    if (sscanf(line, ".type %255[^,], %31s", name, kind) == 2 &&
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
static int admit(cross *p, module_cost *before, module_cost *after, unsigned f,
                 const char *clone) {
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
    const char *reason = b->frame > a->frame     ? "final_frame_growth"
                         : b->saves > a->saves   ? "final_save_growth"
                         : b->spills > a->spills ? "final_spill_growth"
                                                 : NULL;
    if (reason) {
      ir_note(p->c, "skipped", reason, 1);
      return 0;
    }
  }
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
    for (unsigned i = 0; i < p->a.instruction_count; i++) {
      spend(p, 1);
      const ir_instruction *in = &p->a.instructions[i];
      if (p->a.blocks[in->block].function != queue[at] ||
          in->opcode != IR_OP_CALL || in->callee == IR_NONE)
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
  for (unsigned k = 0; k < p->m->count; k++) {
    spend(p, 1);
    if (k == name_token || p->function_at_token[k] != f)
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
static char *cleanup_helper(cross *p, unsigned f, const char **values,
                            const char *clone) {
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
static int specialize(cross *p, unsigned f, const unsigned *calls,
                      unsigned count, module_cost *baseline, char **best,
                      uint32_t *best_bytes, const char **best_clone) {
  const ir_function *fn = &p->a.functions[f];
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++)
    for (unsigned i = p->a.blocks[b].first_instruction;
         i <
         p->a.blocks[b].first_instruction + p->a.blocks[b].instruction_count;
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
  char *helper = cleanup_helper(p, f, values, clone);
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
  if (accepted) {
    *best = candidate;
    *best_bytes = cost.bytes;
    *best_clone = clone;
  }
  reset(p);
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
static int inline_calls(cross *p, unsigned f, const unsigned *calls,
                        unsigned count, module_cost *baseline, char **best,
                        uint32_t *best_bytes, int *forwarding) {
  const ir_function *fn = &p->a.functions[f];
  if (fn->block_count != 1)
    return 0;
  const ir_block *block = &p->a.blocks[fn->first_block];
  if (block->instruction_count > 16)
    return 0;
  if (p->a.instructions[block->first_instruction + block->instruction_count - 1]
          .opcode != IR_OP_RET)
    return 0;
  const ir_type *ret = &p->a.types[fn->return_type];
  if (ret->kind != IR_VOID && ret->kind != IR_INT && ret->kind != IR_PTR)
    return 0;
  const ir_instruction *first = &p->a.instructions[block->first_instruction];
  const ir_instruction *last = &p->a.instructions[block->first_instruction +
                                                  block->instruction_count - 1];
  int forward =
      block->instruction_count == 2 && first->opcode == IR_OP_CALL &&
      first->callee != IR_NONE &&
      p->a.operands[first->callee].kind == IR_OPERAND_GLOBAL &&
      ((ret->kind == IR_VOID && !last->operand_count) ||
       (last->operand_count == 1 &&
        p->a.operands[last->first_operand].kind == IR_OPERAND_LOCAL &&
        p->a.operands[last->first_operand].definition == first->definition));
  for (unsigned i = block->first_instruction;
       i < block->first_instruction + block->instruction_count; i++) {
    const ir_instruction *in = &p->a.instructions[i];
    if (in->effects & IR_EFFECT_OPAQUE || in->opcode == IR_OP_ALLOCA ||
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
  int accepted = admit(p, baseline, &cost, f, NULL) && cost.bytes < *best_bytes;
  if (accepted) {
    *best = candidate;
    *best_bytes = cost.bytes;
    *forwarding = forward;
  }
  reset(p);
  return accepted;
}
void ir_cross_function(ir_context *c, ir_module *m) {
  cross p = {.c = c, .m = m, .work = IR_CROSS_FUNCTION_WORK_LIMIT};
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
  if (strstr(m->source, "; badge-cross-function: bounded selection complete")) {
    ir_note(c, "skipped", "already_costed_module", 1);
    return;
  }
  ir_map attempted = {0};
  ir_map_init(c, &attempted, CROSS_ATTEMPTS * 2);
  unsigned changes = 0, attempts = 0;
  while (attempts < CROSS_ATTEMPTS) {
    ir_allocation *mark = c->allocations;
    ir_map original_names = m->names;
    analyze(&p);
    unsigned *calls = ir_allocate(c, p.a.instruction_count * sizeof(*calls));
    unsigned chosen = IR_NONE, count = 0;
    for (unsigned f = 0; f < p.a.function_count; f++) {
      if (ir_map_get(&attempted, p.a.functions[f].name))
        continue;
      if (eligible(&p, f, calls, &count)) {
        chosen = f;
        break;
      }
    }
    if (chosen == IR_NONE) {
      ir_release_to(c, mark);
      break;
    }
    const char *name = ir_copy(c, p.a.functions[chosen].name,
                               strlen(p.a.functions[chosen].name));
    /* Keep attempted names across reparsing without retaining analysis storage.
     */
    char name_buffer[256];
    if (strlen(name) >= sizeof(name_buffer))
      ir_error(c, "cross-function helper name limit exceeded");
    strcpy(name_buffer, name);
    attempts++;
    module_cost before = measure(&p, m->source, m->size);
    char *best = NULL;
    uint32_t best_bytes = before.bytes;
    const char *kind = "retained_shared_helper", *clone = NULL;
    if (share_helper(&p, chosen, calls, count, &before, &best, &best_bytes))
      kind = "identical_helpers_shared";
    if (specialize(&p, chosen, calls, count, &before, &best, &best_bytes,
                   &clone))
      kind = "constant_specialization";
    int forwarding = 0;
    if (inline_calls(&p, chosen, calls, count, &before, &best, &best_bytes,
                     &forwarding)) {
      kind = forwarding ? "tail_forwarder_inlined" : "straight_line_inlined";
      clone = NULL;
    }
    if (best) {
      for (unsigned l = 0; l < m->line_count; l++)
        m->lines[l].replacement = "";
      m->lines[0].replacement = best;
      m->suffix = ir_format(c, "; badge-cross-decision: %s %s %s %u %u\n", kind,
                            assembly_name(&p, name_buffer),
                            clone ? assembly_name(&p, clone) : "-",
                            before.bytes, best_bytes);
      ir_note(c, "transformed", kind, 1);
      ir_note(c, "transformed", "whole_module_code_bytes_saved",
              before.bytes - best_bytes);
      ir_reparse(c, m, mark);
      changes++;
    } else {
      ir_note(c, "skipped", kind, 1);
      ir_release_to(c, mark);
      m->names = original_names;
    }
    ir_map_put(c, &attempted, ir_copy(c, name_buffer, strlen(name_buffer)),
               "attempted");
  }
  if (attempts == CROSS_ATTEMPTS)
    ir_note(c, "skipped", "module_attempt_budget", 1);
  if (changes) {
    m->suffix = "; badge-cross-function: bounded selection complete\n";
    ir_reparse(c, m, c->allocations);
  }
  c->size = 0;
}
