#include "codegen_internal.h"
#include "target_cost.h"
#include <inttypes.h>
#include <string.h>

static void work(cg_context *c) {
  if (++c->schedule_work > CG_SCHEDULE_WORK_LIMIT)
    cg_error(c, "local scheduling analysis work limit exceeded");
}
static badge_cost_estimate cost(cg_machine_instruction i) {
  badge_cost_kind kind = i.op == CG_M_LOAD_FRAME ? BADGE_COST_LOAD :
                         i.op == CG_M_MUL ? BADGE_COST_MUL :
                         i.op == CG_M_MLA ? BADGE_COST_MAC : BADGE_COST_ALU;
  /* Expanded immediates are conservative complex candidates. Plain imm12 and
     standalone shifts are exempt; memory estimates are core-only. */
  if (i.op == CG_M_ADD_IMM || i.op == CG_M_SUB_IMM ||
      i.op == CG_M_AND_IMM || i.op == CG_M_ORR_IMM || i.op == CG_M_EOR_IMM) {
    uint32_t byte = i.immediate & 255u;
    uint32_t middle = (i.immediate >> 8) & 255u;
    if (i.immediate > 255u &&
        i.immediate != byte * 0x01010101u &&
        i.immediate != byte * 0x00010001u &&
        i.immediate != middle * 0x01000100u)
      kind = BADGE_COST_COMPLEX_ALU;
  }
  return badge_target_cost((badge_cost_request){
      .kind = kind, .memory = BADGE_COST_MEMORY_CORE_BASELINE});
}
static unsigned bubbles(cg_machine_instruction producer,
                        cg_machine_instruction consumer) {
  if (!(producer.writes & consumer.reads))
    return 0;
  badge_cost_input role = BADGE_COST_INPUT_EARLY;
  if (consumer.op == CG_M_MLA &&
      producer.writes == (1u << consumer.accumulator) &&
      !(producer.writes & ((1u << consumer.left) | (1u << consumer.right))))
    role = BADGE_COST_INPUT_ACCUMULATOR;
  badge_cost_dependency d = badge_target_dependency(cost(producer), role, 0);
  return d.bubble.known ? d.bubble.max : 0;
}
typedef struct {
  unsigned stalls, pending[13];
} cg_schedule_score;
static cg_schedule_score score(cg_context *c, const cg_machine_instruction *is,
                               unsigned count) {
  cg_schedule_score out = {0};
  unsigned time = 0, ready[13] = {0};
  badge_cost_kind kinds[13] = {0};
  for (unsigned n = 0; n < count; n++) {
    unsigned start = time;
    cg_machine_instruction i = is[n];
    for (unsigned reg = 0; reg < 13; reg++) {
      work(c);
      if (!(i.reads & (1u << reg)))
        continue;
      unsigned late = i.op == CG_M_MLA && reg == i.accumulator &&
                      reg != i.left && reg != i.right &&
                      (kinds[reg] == BADGE_COST_MUL || kinds[reg] == BADGE_COST_MAC);
      unsigned needed = ready[reg] > late ? ready[reg] - late : 0;
      if (needed > start)
        start = needed;
    }
    badge_cost_estimate estimate = cost(i);
    if (!estimate.baseline_issue.known || !estimate.result_ready.known)
      cg_error(c, "unsupported local scheduling timing query");
    out.stalls += start - time;
    ready[i.dst] = start + estimate.result_ready.max;
    kinds[i.dst] = estimate.kind;
    time = start + estimate.baseline_issue.max;
  }
  for (unsigned reg = 0; reg < 13; reg++) {
    work(c);
    out.pending[reg] = ready[reg] > time ? ready[reg] - time : 0;
  }
  return out;
}
static void emit(cg_context *c, cg_machine_instruction i) {
  static const char *const reg[] = {
    [CG_M_ADD] = "add.w", [CG_M_SUB] = "sub.w", [CG_M_AND] = "and.w",
    [CG_M_ORR] = "orr.w", [CG_M_EOR] = "eor.w", [CG_M_MUL] = "mul",
    [CG_M_LSL] = "lsl.w", [CG_M_LSR] = "lsr.w", [CG_M_ASR] = "asr.w"
  };
  static const char *const imm[] = {
    [CG_M_ADD_IMM] = "add.w", [CG_M_SUB_IMM] = "sub.w",
    [CG_M_ADDW] = "addw", [CG_M_SUBW] = "subw",
    [CG_M_AND_IMM] = "and.w", [CG_M_ORR_IMM] = "orr.w",
    [CG_M_EOR_IMM] = "eor.w", [CG_M_LSL_IMM] = "lsl.w",
    [CG_M_LSR_IMM] = "lsr.w", [CG_M_ASR_IMM] = "asr.w"
  };
  if (i.op == CG_M_MOVW || i.op == CG_M_MOVT)
    cg_emit(c, "%s r%u, #%" PRIu32 "\n",
            i.op == CG_M_MOVW ? "movw" : "movt", i.dst, i.immediate);
  else if (i.op == CG_M_MOV)
    cg_emit(c, "mov r%u, r%u\n", i.dst, i.left);
  else if (i.op == CG_M_LOAD_FRAME)
    cg_emit(c, "ldr.w r%u, [r7, #%" PRIu32 "]\n", i.dst, i.immediate);
  else if (i.op == CG_M_MLA)
    cg_emit(c, "mla r%u, r%u, r%u, r%u\n",
            i.dst, i.left, i.right, i.accumulator);
  else if (i.op >= CG_M_ADD && i.op <= CG_M_ASR)
    cg_emit(c, "%s r%u, r%u, r%u\n", reg[i.op], i.dst, i.left, i.right);
  else
    cg_emit(c, "%s r%u, r%u, #%" PRIu32 "\n",
            imm[i.op], i.dst, i.left, i.immediate);
}
void cg_schedule_flush(cg_context *c) {
  if (!c->schedule_count || c->schedule_flushing)
    return;
  c->schedule_flushing = 1;
  c->schedule_windows++;
  for (unsigned at = 1; at < c->schedule_count; at++) {
    work(c);
    cg_machine_instruction producer = c->schedule[at - 1];
    if (!bubbles(producer, c->schedule[at]))
      continue;
    c->schedule_hazards++;
    int filled = 0;
    for (unsigned next = at + 1; next < c->schedule_count; next++) {
      work(c);
      cg_machine_instruction candidate = c->schedule[next];
      if (candidate.op == CG_M_LOAD_FRAME ||
          (candidate.reads & producer.writes))
        continue;
      badge_cost_estimate estimate = cost(candidate);
      if (!estimate.baseline_issue.known || estimate.baseline_issue.max != 1)
        continue;
      int independent = 1;
      for (unsigned crossed = at; crossed < next; crossed++) {
        work(c);
        cg_machine_instruction other = c->schedule[crossed];
        if ((candidate.reads & other.writes) ||
            (candidate.writes & (other.reads | other.writes))) {
          independent = 0;
          break;
        }
      }
      if (!independent)
        continue;
      cg_machine_instruction trial[CG_SCHEDULE_WINDOW];
      memcpy(trial, c->schedule, c->schedule_count * sizeof(*trial));
      memmove(trial + at + 1, trial + at,
              (next - at) * sizeof(candidate));
      trial[at] = candidate;
      cg_schedule_score before = score(c, c->schedule, c->schedule_count);
      cg_schedule_score after = score(c, trial, c->schedule_count);
      if (after.stalls >= before.stalls)
        continue;
      int exit_safe = 1;
      for (unsigned reg = 0; reg < 13; reg++) {
        work(c);
        if (after.pending[reg] > before.pending[reg])
          exit_safe = 0;
      }
      if (!exit_safe)
        continue;
      memcpy(c->schedule, trial, c->schedule_count * sizeof(*trial));
      c->schedule_moves++;
      filled = 1;
      break;
    }
    if (!filled)
      c->schedule_unfilled++;
  }
  for (unsigned n = 0; n < c->schedule_count; n++)
    emit(c, c->schedule[n]);
  c->schedule_count = 0;
  c->schedule_flushing = 0;
}
void cg_machine(cg_context *c, cg_machine_opcode op, unsigned dst,
                unsigned left, unsigned right, unsigned accumulator,
                uint32_t immediate) {
  if ((unsigned)op >= CG_M_COUNT || dst > 12 || left > 12 ||
      right > 12 || accumulator > 12 ||
      ((op == CG_M_MOVW || op == CG_M_MOVT) && immediate > 65535) ||
      (op == CG_M_LOAD_FRAME && (immediate > 4095 || immediate % 4)) ||
      ((op == CG_M_ADDW || op == CG_M_SUBW) && immediate > 4095) ||
      ((op == CG_M_LSL_IMM || op == CG_M_LSR_IMM || op == CG_M_ASR_IMM) &&
       (!immediate || immediate > 31)))
    cg_error(c, "invalid structured scheduling instruction");
  cg_machine_instruction i = {op, dst, left, right, accumulator, immediate,
                              0, 1u << dst};
  if (op == CG_M_MOVT)
    i.reads = i.writes;
  else if (op == CG_M_LOAD_FRAME)
    i.reads = 1u << 7;
  else if (op != CG_M_MOVW) {
    i.reads = 1u << left;
    if (op >= CG_M_ADD && op <= CG_M_ASR)
      i.reads |= 1u << right;
    if (op == CG_M_MLA)
      i.reads |= (1u << right) | (1u << accumulator);
  }
  if (!cg_schedule_selection(c) || !c->schedule_active) {
    emit(c, i);
    return;
  }
  work(c);
  if (c->schedule_count == CG_SCHEDULE_WINDOW)
    cg_schedule_flush(c);
  c->schedule[c->schedule_count++] = i;
}
void cg_move(cg_context *c, unsigned dst, unsigned src) {
  cg_machine(c, CG_M_MOV, dst, src, 0, 0, 0);
}
