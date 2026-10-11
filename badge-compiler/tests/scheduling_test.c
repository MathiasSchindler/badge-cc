#include "codegen_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cg_context context(void) {
  return (cg_context){.scalar_selection = BADGE_SELECTION_THUMB_SCHEDULE,
                      .schedule_active = 1, .file = "scheduling-test"};
}
static void mul(cg_context *c, unsigned dst, unsigned left, unsigned right) {
  cg_machine(c, CG_M_MUL, dst, left, right, 0, 0);
}
static void add(cg_context *c, unsigned dst, unsigned left, unsigned right) {
  cg_machine(c, CG_M_ADD, dst, left, right, 0, 0);
}
static void cases(void) {
  cg_context c = context();
  mul(&c, 0, 1, 2);
  add(&c, 3, 0, 4);
  cg_machine(&c, CG_M_MOVW, 5, 0, 0, 0, 99);
  cg_schedule_flush(&c);
  assert(c.schedule_moves == 1 && c.schedule_hazards == 1);
  assert(strstr(c.out, "mul r0, r1, r2\nmovw r5, #99\nadd.w r3, r0, r4"));
  free(c.out);
  for (unsigned hazard = 0; hazard < 4; hazard++) {
    c = context();
    mul(&c, 0, 1, 2);
    add(&c, 3, 0, 4);
    if (hazard == 0) cg_move(&c, 5, 3);             /* RAW */
    if (hazard == 1) cg_move(&c, 4, 5);             /* WAR */
    if (hazard == 2) cg_move(&c, 3, 5);             /* WAW */
    if (hazard == 3) cg_move(&c, 5, 0);             /* delayed input */
    cg_schedule_flush(&c);
    assert(!c.schedule_moves && c.schedule_unfilled == 1);
    free(c.out);
  }
  c = context();
  cg_machine(&c, CG_M_LOAD_FRAME, 0, 0, 0, 0, 32);
  add(&c, 3, 0, 4);
  cg_machine(&c, CG_M_MOVW, 5, 0, 0, 0, 9);
  cg_schedule_flush(&c);
  assert(c.schedule_moves == 1);
  free(c.out);
  for (unsigned late = 0; late < 2; late++) {
    c = context();
    mul(&c, 0, 1, 2);
    cg_machine(&c, CG_M_MLA, 3, late ? 1 : 0, 2, late ? 0 : 4, 0);
    cg_machine(&c, CG_M_MOVW, 5, 0, 0, 0, 9);
    cg_machine(&c, CG_M_MOVW, 6, 0, 0, 0, 10);
    cg_schedule_flush(&c);
    assert(c.schedule_moves == !late);
    free(c.out);
  }
  const char *barriers[] = {
    "str.w r0, [r7, #32]\n", "ldr.w r0, [r4]\n", "str.w r0, [r4]\n",
    "bl function\n", "nop\n", ".Lnext:\n", "b.w .Lnext\n",
    "cmp r0, r1\nit eq\nmoveq r0, r2\n",
    "adds.w r0, r0, r2\nadc.w r1, r1, r3\n"
  };
  for (unsigned n = 0; n < sizeof(barriers) / sizeof(*barriers); n++) {
    c = context();
    mul(&c, 0, 1, 2);
    add(&c, 3, 0, 4);
    cg_emit(&c, "%s", barriers[n]);
    cg_machine(&c, CG_M_MOVW, 5, 0, 0, 0, 9);
    cg_schedule_flush(&c);
    assert(!c.schedule_moves && c.schedule_barriers == 1);
    assert(strstr(c.out, barriers[n]) < strstr(c.out, "movw r5"));
    free(c.out);
  }
  c = context();
  mul(&c, 0, 1, 2);
  add(&c, 3, 0, 4);
  cg_machine(&c, CG_M_MOVT, 3, 0, 0, 0, 9);
  cg_schedule_flush(&c);
  assert(!c.schedule_moves);                        /* implicit destination use */
  free(c.out);
  c = context();
  mul(&c, 0, 1, 2);
  add(&c, 3, 0, 4);
  mul(&c, 6, 3, 2);
  cg_machine(&c, CG_M_MOVW, 5, 0, 0, 0, 9);
  add(&c, 8, 6, 4);
  cg_schedule_flush(&c);
  assert(!c.schedule_moves);                        /* do not just relocate a stall */
  free(c.out);
  c = context();
  mul(&c, 0, 1, 2);
  add(&c, 3, 0, 4);
  mul(&c, 6, 3, 2);
  cg_machine(&c, CG_M_MOVW, 5, 0, 0, 0, 9);
  cg_schedule_flush(&c);
  assert(!c.schedule_moves);                        /* protect window-exit readiness */
  free(c.out);
}
static uint32_t random_state = 0x12345678u;
static uint32_t random_word(void) {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;
  return random_state;
}
static void execute(cg_machine_instruction *is, unsigned count, uint32_t *r) {
  for (unsigned n = 0; n < count; n++) {
    cg_machine_instruction i = is[n];
    uint32_t a = r[i.left], b = r[i.right], value;
    switch (i.op) {
    case CG_M_MOVW: value = i.immediate; break;
    case CG_M_MOVT: value = (r[i.dst] & 65535u) | (i.immediate << 16); break;
    case CG_M_MOV: value = a; break;
    case CG_M_MUL: value = a * b; break;
    case CG_M_ADD: value = a + b; break;
    case CG_M_SUB: value = a - b; break;
    case CG_M_AND: value = a & b; break;
    case CG_M_ORR: value = a | b; break;
    case CG_M_EOR: value = a ^ b; break;
    case CG_M_MLA: value = a * b + r[i.accumulator]; break;
    case CG_M_LOAD_FRAME: value = (r[7] + i.immediate) ^ 0xfedcba98u; break;
    default: abort();
    }
    r[i.dst] = value;
  }
}
static unsigned core_score(cg_machine_instruction *is, unsigned count,
                           unsigned *pending) {
  unsigned time = 0, waits = 0, ready[13] = {0};
  int multiply[13] = {0};
  for (unsigned n = 0; n < count; n++) {
    cg_machine_instruction i = is[n];
    unsigned earliest = time;
    for (unsigned reg = 0; reg < 13; reg++) {
      if (!(i.reads & (1u << reg))) continue;
      unsigned late = i.op == CG_M_MLA && reg == i.accumulator &&
                      reg != i.left && reg != i.right && multiply[reg];
      unsigned need = ready[reg] > late ? ready[reg] - late : 0;
      if (earliest < need) earliest = need;
    }
    waits += earliest - time;
    multiply[i.dst] = i.op == CG_M_MUL || i.op == CG_M_MLA;
    ready[i.dst] = earliest + 1 + (multiply[i.dst] || i.op == CG_M_LOAD_FRAME);
    time = earliest + 1;
  }
  for (unsigned reg = 0; reg < 13; reg++)
    pending[reg] = ready[reg] > time ? ready[reg] - time : 0;
  return waits;
}
int main(void) {
  cases();
  const cg_machine_opcode ops[] = {CG_M_MOVW, CG_M_MOVT, CG_M_MOV, CG_M_MUL,
    CG_M_ADD, CG_M_SUB, CG_M_AND, CG_M_ORR, CG_M_EOR, CG_M_MLA, CG_M_LOAD_FRAME};
  unsigned moved = 0;
  for (unsigned trial = 0; trial < 100000; trial++) {
    cg_context c = context();
    unsigned count = 1 + random_word() % CG_SCHEDULE_WINDOW;
    for (unsigned n = 0; n < count; n++) {
      cg_machine_opcode op = ops[random_word() % (sizeof(ops) / sizeof(*ops))];
      unsigned dst = random_word() % 13, left = random_word() % 13;
      unsigned right = random_word() % 13, accumulator = random_word() % 13;
      uint32_t imm = op == CG_M_LOAD_FRAME ? (random_word() % 32) * 4 :
                                           random_word() & 65535u;
      cg_machine(&c, op, dst, left, right, accumulator, imm);
    }
    cg_machine_instruction original[CG_SCHEDULE_WINDOW];
    memcpy(original, c.schedule, count * sizeof(*original));
    uint32_t before[13], after[13];
    for (unsigned n = 0; n < 13; n++) before[n] = random_word();
    memcpy(after, before, sizeof(after));
    cg_schedule_flush(&c);
    execute(original, count, before);
    execute(c.schedule, count, after);
    assert(!memcmp(before, after, sizeof(before)));
    unsigned before_pending[13], after_pending[13];
    unsigned before_stalls = core_score(original, count, before_pending);
    unsigned after_stalls = core_score(c.schedule, count, after_pending);
    assert(after_stalls <= before_stalls);
    if (c.schedule_moves) assert(after_stalls < before_stalls);
    for (unsigned reg = 0; reg < 13; reg++)
      assert(after_pending[reg] <= before_pending[reg]);
    assert(c.schedule_moves <= count && c.schedule_work <= 6000);
    moved += c.schedule_moves;
    free(c.out);
  }
  assert(moved > 0);
  printf("scheduling: 100000 random register-effect oracles, %u moves; "
         "RAW/WAR/WAW, forwarding, implicit operands and barriers passed\n", moved);
}
