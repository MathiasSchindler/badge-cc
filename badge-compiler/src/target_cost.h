#ifndef BADGE_COMPILER_TARGET_COST_H
#define BADGE_COMPILER_TARGET_COST_H

#include <stdint.h>

#define BADGE_TARGET_COST_VERSION 1
#ifndef BADGE_TARGET_COST_WORK_LIMIT
#define BADGE_TARGET_COST_WORK_LIMIT 16000000u
#endif

typedef struct {
  unsigned min, max;
  int known;
} badge_cost_range;
typedef enum {
  BADGE_COST_ALU, BADGE_COST_COMPLEX_ALU, BADGE_COST_MUL,
  BADGE_COST_MAC, BADGE_COST_LONG_MAC, BADGE_COST_DIVIDE,
  BADGE_COST_LOAD, BADGE_COST_STORE, BADGE_COST_BRANCH,
  BADGE_COST_LITERAL, BADGE_COST_MOV_PAIR, BADGE_COST_UNKNOWN,
  BADGE_COST_KIND_COUNT
} badge_cost_kind;
typedef enum {
  BADGE_COST_MEMORY_UNKNOWN, BADGE_COST_MEMORY_CORE_BASELINE,
  BADGE_COST_MEMORY_WARM_XIP, BADGE_COST_MEMORY_COLD_XIP,
  BADGE_COST_MEMORY_MMIO, BADGE_COST_MEMORY_CONTENDED
} badge_cost_memory;
typedef enum {
  BADGE_COST_INPUT_EARLY, BADGE_COST_INPUT_ACCUMULATOR,
  BADGE_COST_INPUT_STORE_DATA, BADGE_COST_INPUT_EARLIER_LOAD
} badge_cost_input;
typedef struct {
  const char *name, *fact, *experiment, *condition;
} badge_cost_rule;
extern const badge_cost_rule badge_cost_rules[BADGE_COST_KIND_COUNT];

typedef struct {
  badge_cost_kind kind;
  unsigned encoding_bytes, registers;
  int sets_flags, operands_known, signed_divide;
  uint32_t dividend, divisor;
  badge_cost_memory memory;
  int branch_predicates_known, taken, load_to_pc, folded, direction_known;
  int target_halfword, target_wide, preceding_push_pop;
} badge_cost_request;
typedef struct {
  badge_cost_kind kind;
  const char *status;
  badge_cost_range code_bytes, baseline_issue, result_ready;
  badge_cost_range resource_occupancy, extra_temporaries;
  unsigned resources;
  int memory_wait_unknown;
} badge_cost_estimate;
enum {
  BADGE_COST_RESOURCE_INTEGER = 1, BADGE_COST_RESOURCE_MULTIPLY = 2,
  BADGE_COST_RESOURCE_DIVIDER = 4, BADGE_COST_RESOURCE_MEMORY = 8,
  BADGE_COST_RESOURCE_CONTROL = 16
};
typedef struct {
  badge_cost_range bubble;
  int semantic_dependency;
} badge_cost_dependency;

/* Normalized single-machine-instruction queries, not a runtime predictor.
   Unknown memory waits and allocation effects remain independent dimensions. */
badge_cost_estimate badge_target_cost(badge_cost_request request);
badge_cost_dependency badge_target_dependency(badge_cost_estimate producer,
                                              badge_cost_input input,
                                              unsigned intervening);
badge_cost_range badge_target_add_bytes(unsigned rd, unsigned rn,
                                       uint32_t immediate, int flags_dead,
                                       int outside_it);
badge_cost_range badge_target_literal_bytes(unsigned rd, uint32_t instruction,
                                           uint32_t pool);

#endif
