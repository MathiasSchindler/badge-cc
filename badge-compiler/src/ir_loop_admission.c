#include "ir_loop_admission.h"
#include "assembly_cost.h"
#include "codegen.h"
#include "target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef IR_LOOP_ADMISSION_WORK_LIMIT
#define IR_LOOP_ADMISSION_WORK_LIMIT (UINT64_C(64) * 1024 * 1024)
#endif

typedef struct {
  uint32_t bytes;
  unsigned frame, saves, spills;
} loop_cost;
static loop_cost measure(ir_context *c, const char *source, size_t size) {
  char *assembly = NULL;
  size_t length = 0;
  if (!badge_codegen_compile_with_selections(source, size, c->file,
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL_COMPACT},
      BADGE_SELECTION_THUMB_OPERANDS, BADGE_MEMORY_WORD, &assembly, &length))
    ir_error(c, "loop-scalar backend admission failed");
  loop_cost cost = {0};
  const char *frame = strstr(assembly, "@ stack-frame:");
  const char *allocation = strstr(assembly, "@ global-allocation:");
  unsigned allocated;
  if (!frame || !allocation ||
      sscanf(frame, "@ stack-frame: %u + %u saved-register bytes", &cost.frame, &cost.saves) != 2 ||
      sscanf(allocation, "@ global-allocation: allocated %u spilled %u", &allocated, &cost.spills) != 2) {
    free(assembly);
    ir_error(c, "loop-scalar admission lacks finalized frame/allocation facts");
  }
  int ok = badge_assembly_code_bytes(assembly, length, c->file, &cost.bytes);
  free(assembly);
  if (!ok) ir_error(c, "loop-scalar final-encoding admission failed");
  return cost;
}
static char *function_text(ir_context *c, ir_module *m, const ir_analysis *a, unsigned begin,
                           unsigned end, int candidate, size_t *size) {
  c->size = 0;
  unsigned function = 0;
  for (unsigned line = 0; line < m->line_count; line++) {
    while (function < a->function_count && a->functions[function].line - 1 < line)
      function++;
    if (function < a->function_count && a->functions[function].line - 1 == line &&
        !a->functions[function].declaration) {
      while (line < m->line_count &&
          !(m->lines[line].count == 1 && !strcmp(ir_text(m, m->lines[line].first), "}")))
        line++;
      continue;
    }
    const ir_line *row = &m->lines[line];
    ir_append(c, m->source + row->begin, row->end - row->begin);
  }
  for (unsigned line = begin; line <= end; line++) {
    const ir_line *row = &m->lines[line];
    if (candidate && row->replacement)
      ir_append(c, row->replacement, strlen(row->replacement));
    else ir_append(c, m->source + row->begin, row->end - row->begin);
  }
  *size = c->size;
  return ir_copy(c, c->output, c->size);
}
void ir_loop_admission(ir_context *c, ir_module *m, const ir_analysis *a) {
  uint64_t work = IR_LOOP_ADMISSION_WORK_LIMIT;
  for (unsigned f = 0; f < a->function_count; f++) {
    const ir_function *fn = &a->functions[f];
    if (fn->declaration) continue;
    unsigned begin = fn->line - 1, end = begin;
    int changed = 0;
    for (; end < m->line_count; end++) {
      changed |= m->lines[end].replacement != NULL;
      if (m->lines[end].count == 1 && !strcmp(ir_text(m, m->lines[end].first), "}")) break;
    }
    if (end == m->line_count) ir_error(c, "loop-scalar admission cannot delimit function");
    if (!changed) continue;
    size_t before_size, after_size;
    char *before = function_text(c, m, a, begin, end, 0, &before_size);
    char *after = function_text(c, m, a, begin, end, 1, &after_size);
    if (before_size > work || after_size > work - before_size)
      ir_error(c, "loop-scalar backend admission work limit exceeded");
    work -= before_size + after_size;
    loop_cost original = measure(c, before, before_size);
    loop_cost proposed = measure(c, after, after_size);
    const char *reason = proposed.frame > original.frame ? "final_frame_growth" :
                         proposed.saves > original.saves ? "final_save_growth" :
                         proposed.spills > original.spills ? "final_spill_growth" :
                         proposed.bytes > original.bytes ? "final_code_growth" : NULL;
    if (reason) {
      for (unsigned line = begin; line <= end; line++) m->lines[line].replacement = NULL;
      ir_note(c, "skipped", reason, 1);
    } else {
      ir_note(c, "transformed", "backend_admitted_function", 1);
      ir_note(c, "transformed", "final_code_bytes_saved", original.bytes - proposed.bytes);
    }
  }
}
