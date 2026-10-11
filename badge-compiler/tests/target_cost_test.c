#include "target_cost.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void exact(badge_cost_range r, unsigned value) {
  assert(r.known && r.min == value && r.max == value);
}
static badge_cost_estimate query(badge_cost_kind kind) {
  return badge_target_cost((badge_cost_request){.kind = kind});
}
int main(void) {
  for (unsigned kind = 0; kind < BADGE_COST_KIND_COUNT; kind++) {
    assert(badge_cost_rules[kind].fact[0]);
    badge_cost_estimate e = query((badge_cost_kind)kind);
    assert(!e.extra_temporaries.known);
    if (kind == BADGE_COST_MOV_PAIR) exact(e.code_bytes, 8);
    else assert(!e.code_bytes.known);
  }
  badge_cost_estimate mul = query(BADGE_COST_MUL);
  exact(mul.baseline_issue, 1);
  exact(mul.resource_occupancy, 1);
  exact(mul.result_ready, 2);
  exact(badge_target_dependency(mul, BADGE_COST_INPUT_EARLY, 0).bubble, 1);
  exact(badge_target_dependency(mul, BADGE_COST_INPUT_EARLY, 1).bubble, 0);
  exact(badge_target_dependency(mul, BADGE_COST_INPUT_EARLY, UINT32_MAX).bubble, 0);
  exact(badge_target_dependency(mul, BADGE_COST_INPUT_ACCUMULATOR, 0).bubble, 0);
  badge_cost_dependency data = badge_target_dependency(mul, BADGE_COST_INPUT_STORE_DATA, 0);
  exact(data.bubble, 0);
  assert(data.semantic_dependency);
  badge_cost_request r = {.kind = BADGE_COST_COMPLEX_ALU, .sets_flags = 1};
  exact(badge_target_cost(r).baseline_issue, 2);
  r.sets_flags = 0;
  exact(badge_target_cost(r).baseline_issue, 1);
  exact(badge_target_dependency(badge_target_cost(r), BADGE_COST_INPUT_EARLY, 0).bubble, 1);

  const uint32_t numerators[] = {1, 3, 255, 65535, UINT32_MAX};
  const unsigned cycles[] = {2, 4, 5, 7, 11};
  r = (badge_cost_request){.kind = BADGE_COST_DIVIDE, .operands_known = 1, .divisor = 3};
  for (unsigned i = 0; i < 5; i++) {
    r.dividend = numerators[i];
    exact(badge_target_cost(r).baseline_issue, cycles[i]);
    exact(badge_target_dependency(badge_target_cost(r), BADGE_COST_INPUT_EARLY, 0).bubble, 1);
  }
  for (unsigned bit = 0; bit < 32; bit++) {
    r.divisor = 1;
    r.dividend = 1u << bit;
    exact(badge_target_cost(r).baseline_issue, 4 + bit / 4);
  }
  r.divisor = 3; r.dividend = 0u - 255u; r.signed_divide = 1;
  exact(badge_target_cost(r).baseline_issue, 5);
  r.dividend = 0x80000000u; r.divisor = UINT32_MAX;
  assert(!badge_target_cost(r).baseline_issue.known);
  r.divisor = 0;
  assert(!badge_target_cost(r).baseline_issue.known);
  r.divisor = 1; r.dividend = 0;
  assert(!badge_target_cost(r).baseline_issue.known);
  r.operands_known = 0;
  badge_cost_estimate division = badge_target_cost(r);
  assert(division.baseline_issue.min == 2 && division.baseline_issue.max == 11);
  exact(badge_target_dependency(division, BADGE_COST_INPUT_EARLY, 0).bubble, 1);

  r = (badge_cost_request){.kind = BADGE_COST_LOAD, .registers = 4,
                          .memory = BADGE_COST_MEMORY_CORE_BASELINE};
  exact(badge_target_cost(r).baseline_issue, 4);
  exact(badge_target_dependency(badge_target_cost(r), BADGE_COST_INPUT_EARLY, 0).bubble, 1);
  exact(badge_target_dependency(badge_target_cost(r), BADGE_COST_INPUT_EARLIER_LOAD, 0).bubble, 0);
  r.registers = 1;
  assert(!badge_target_dependency(badge_target_cost(r), BADGE_COST_INPUT_EARLIER_LOAD, 0).bubble.known);
  assert(!badge_target_dependency(badge_target_cost(r), BADGE_COST_INPUT_ACCUMULATOR, 0).bubble.known);
  r.registers = 4;
  for (unsigned memory = 0; memory <= BADGE_COST_MEMORY_CONTENDED; memory++) {
    if (memory == BADGE_COST_MEMORY_CORE_BASELINE) continue;
    r.memory = (badge_cost_memory)memory;
    assert(badge_target_cost(r).memory_wait_unknown);
    assert(!badge_target_dependency(badge_target_cost(r), BADGE_COST_INPUT_EARLY, 0).bubble.known);
  }
  r = (badge_cost_request){.kind = BADGE_COST_BRANCH};
  assert(!badge_target_cost(r).baseline_issue.known);
  r.branch_predicates_known = 1; r.taken = 1;
  exact(badge_target_cost(r).baseline_issue, 2);
  r.target_halfword = 1; r.target_wide = 1;
  exact(badge_target_cost(r).baseline_issue, 3);
  r.preceding_push_pop = 1;
  exact(badge_target_cost(r).baseline_issue, 2);
  r.folded = 1; r.direction_known = 1;
  exact(badge_target_cost(r).baseline_issue, 1);
  r.taken = 0;
  exact(badge_target_cost(r).baseline_issue, 0);
  r.load_to_pc = 1; r.registers = 4; r.taken = 1; r.folded = 0;
  exact(badge_target_cost(r).baseline_issue, 7);

  exact(badge_target_add_bytes(0, 1, 7, 1, 1), 2);
  exact(badge_target_add_bytes(0, 1, 8, 1, 1), 4);
  exact(badge_target_add_bytes(7, 7, 255, 1, 1), 2);
  exact(badge_target_add_bytes(7, 7, 256, 1, 1), 4);
  exact(badge_target_add_bytes(8, 8, 7, 1, 1), 4);
  exact(badge_target_add_bytes(0, 0, 7, 0, 1), 4);
  exact(badge_target_add_bytes(0, 0, 7, 1, 0), 4);
  exact(badge_target_add_bytes(12, 12, 4095, 0, 1), 4);
  exact(badge_target_add_bytes(0, 0, 4096, 0, 1), 4);
  assert(!badge_target_add_bytes(0, 0, 4097, 0, 1).known);
  assert(!badge_target_add_bytes(13, 0, 1, 1, 1).known);
  exact(badge_target_literal_bytes(7, 0x1002, 0x1004), 2);
  exact(badge_target_literal_bytes(7, 0x1002, 0x1400), 2);
  exact(badge_target_literal_bytes(7, 0x1002, 0x1404), 4);
  exact(badge_target_literal_bytes(8, 0x1002, 0x1004), 4);
  exact(badge_target_literal_bytes(0, 0x2000, 0x1008), 4);
  assert(!badge_target_literal_bytes(0, 0x2000, 0x1004).known);
  assert(!badge_target_literal_bytes(0, 0x1000, 0x1005).known);
  assert(!badge_target_literal_bytes(15, 0x1000, 0x1004).known);
  assert(!badge_target_literal_bytes(0, UINT32_MAX - 1, 0).known);
  r = (badge_cost_request){.kind = (badge_cost_kind)-1};
  assert(!strcmp(badge_target_cost(r).status, "invalid-query"));
  r.kind = BADGE_COST_ALU; r.encoding_bytes = 3;
  assert(!strcmp(badge_target_cost(r).status, "invalid-query"));
  assert(!badge_target_dependency(query(BADGE_COST_UNKNOWN), BADGE_COST_INPUT_EARLY, 0).bubble.known);
  assert(!badge_target_dependency(query(BADGE_COST_STORE), BADGE_COST_INPUT_EARLY, 0).bubble.known);
  puts("target cost: encoding, timing, dependencies, resources and unknown boundaries passed");
  return 0;
}
