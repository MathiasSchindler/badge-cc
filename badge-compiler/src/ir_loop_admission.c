#include "ir_loop_admission.h"
#include "assembly_cost.h"
#include "codegen.h"
#include "host_diagnostics.h"
#include "target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef BADGE_ENABLE_PARALLEL_ADMISSION
#include <pthread.h>
_Thread_local int badge_defer_cost_diagnostics;
#endif

#ifndef IR_LOOP_ADMISSION_WORK_LIMIT
#define IR_LOOP_ADMISSION_WORK_LIMIT (UINT64_C(64) * 1024 * 1024)
#endif

typedef struct {
  uint32_t bytes;
  unsigned frame, saves, spills;
} loop_cost;
typedef struct {
  const char *file, *source;
  size_t size;
  badge_scalar_selection selection;
  loop_cost cost;
  const char *failure;
} cost_job;
static void measure(cost_job *job) {
  job->failure = NULL;
  char *assembly = NULL;
  size_t length = 0;
  if (!badge_codegen_compile_with_selections(job->source, job->size, job->file,
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL_COMPACT},
      job->selection, BADGE_MEMORY_WORD, &assembly, &length)) {
    job->failure = "loop-scalar backend admission failed";
    return;
  }
  loop_cost *cost = &job->cost;
  const char *frame = strstr(assembly, "@ stack-frame:");
  const char *allocation = strstr(assembly, "@ global-allocation:");
  unsigned allocated;
  if (!frame || !allocation ||
      sscanf(frame, "@ stack-frame: %u + %u saved-register bytes", &cost->frame, &cost->saves) != 2 ||
      sscanf(allocation, "@ global-allocation: allocated %u spilled %u", &allocated, &cost->spills) != 2) {
    free(assembly);
    job->failure = "loop-scalar admission lacks finalized frame/allocation facts";
    return;
  }
  int ok = badge_assembly_code_bytes(assembly, length, job->file, &cost->bytes);
  free(assembly);
  if (!ok)
    job->failure = "loop-scalar final-encoding admission failed";
}

#ifdef BADGE_ENABLE_PARALLEL_ADMISSION
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t ready;
  pthread_t thread;
  cost_job *pending;
  int stop;
} cost_worker;
static void *run_worker(void *arg) {
  cost_worker *w = arg;
  pthread_mutex_lock(&w->mutex);
  while (!w->stop) {
    if (!w->pending) {
      pthread_cond_wait(&w->ready, &w->mutex);
      continue;
    }
    cost_job *job = w->pending;
    pthread_mutex_unlock(&w->mutex);
    badge_defer_cost_diagnostics = 1;
    measure(job);
    badge_defer_cost_diagnostics = 0;
    pthread_mutex_lock(&w->mutex);
    w->pending = NULL;
    pthread_cond_signal(&w->ready);
  }
  pthread_mutex_unlock(&w->mutex);
  return NULL;
}
static void destroy_worker(void *arg) {
  cost_worker *w = arg;
  pthread_mutex_lock(&w->mutex);
  w->stop = 1;
  pthread_cond_signal(&w->ready);
  pthread_mutex_unlock(&w->mutex);
  if (pthread_join(w->thread, NULL) != 0) {
    fputs("badge-opt: failed to join admission worker\n", stderr);
    abort();
  }
  pthread_cond_destroy(&w->ready);
  pthread_mutex_destroy(&w->mutex);
  free(w);
}
static cost_worker *get_worker(ir_context *c) {
  if (c->admission_helper)
    return c->admission_helper;
  cost_worker *w = calloc(1, sizeof(*w));
  if (!w)
    ir_error(c, "out of memory creating admission worker");
  if (pthread_mutex_init(&w->mutex, NULL) == 0) {
    if (pthread_cond_init(&w->ready, NULL) == 0) {
      if (pthread_create(&w->thread, NULL, run_worker, w) == 0) {
        c->admission_helper = w;
        c->destroy_admission_helper = destroy_worker;
        return w;
      }
      pthread_cond_destroy(&w->ready);
    }
    pthread_mutex_destroy(&w->mutex);
  }
  free(w);
  ir_error(c, "cannot start admission worker");
}
#endif

static void measure_pair(ir_context *c, cost_job *original, cost_job *proposed) {
#ifdef BADGE_ENABLE_PARALLEL_ADMISSION
  if (c->parallel_admission) {
    cost_worker *w = get_worker(c);
    pthread_mutex_lock(&w->mutex);
    w->pending = proposed;
    pthread_cond_signal(&w->ready);
    pthread_mutex_unlock(&w->mutex);
    measure(original);
    pthread_mutex_lock(&w->mutex);
    while (w->pending)
      pthread_cond_wait(&w->ready, &w->mutex);
    pthread_mutex_unlock(&w->mutex);
    if (!original->failure && proposed->failure) {
      // Reproduce the worker's diagnostics in serial order, never admit a failed trial.
      cost_job replay = *proposed;
      measure(&replay);
    }
  } else
#endif
  {
    measure(original);
    if (!original->failure)
      measure(proposed);
  }
  if (original->failure)
    ir_error(c, original->failure);
  if (proposed->failure)
    ir_error(c, proposed->failure);
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
static int needs_sibling_local(const ir_analysis *a, unsigned function) {
  const ir_function *fn = &a->functions[function];
  for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++) {
    const ir_block *block = &a->blocks[b];
    for (unsigned i = block->first_instruction; i < block->first_instruction + block->instruction_count; i++) {
      const ir_instruction *in = &a->instructions[i];
      for (unsigned k = in->first_operand; k < in->first_operand + in->operand_count; k++) {
        const ir_operand *op = &a->operands[k];
        if (op->kind != IR_OPERAND_GLOBAL)
          continue;
        const ir_global *g = &a->globals[op->definition];
        const char *name = g->name + 1;
        if (*name == '"')
          name++;
        if (*name == '.' && g->function != IR_NONE && g->function != function)
          return 1;
      }
    }
  }
  return 0;
}
void ir_function_admission(ir_context *c, ir_module *m, const ir_analysis *a,
                           badge_scalar_selection selection) {
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
    // The isolated assembler cannot resolve a sibling's assembler-local symbol.
    if (needs_sibling_local(a, f)) {
      for (unsigned line = begin; line <= end; line++) m->lines[line].replacement = NULL;
      ir_note(c, "skipped", "isolated_sibling_local_symbol", 1);
      continue;
    }
    size_t before_size, after_size;
    char *before = function_text(c, m, a, begin, end, 0, &before_size);
    char *after = function_text(c, m, a, begin, end, 1, &after_size);
    if (before_size > work || after_size > work - before_size)
      ir_error(c, "loop-scalar backend admission work limit exceeded");
    work -= before_size + after_size;
    cost_job before_job = {.file = c->file, .source = before, .size = before_size, .selection = selection};
    cost_job after_job = {.file = c->file, .source = after, .size = after_size, .selection = selection};
    measure_pair(c, &before_job, &after_job);
    loop_cost original = before_job.cost, proposed = after_job.cost;
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
void ir_loop_admission(ir_context *c, ir_module *m, const ir_analysis *a) {
  ir_function_admission(c, m, a, BADGE_SELECTION_THUMB_OPERANDS);
}
