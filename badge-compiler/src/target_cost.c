#include "target_cost.h"
#include "thumb_immediate.h"

const badge_cost_rule badge_cost_rules[BADGE_COST_KIND_COUNT] = {
  {"alu", "M33-TIMING-ALU-001", "simple-alu-dependency", "simple/exempt operand form"},
  {"complex-alu", "M33-TIMING-ALU-001", "complex-alu-result-delay", "non-exempt complex operand; flags explicit"},
  {"multiply", "M33-TIMING-MUL-001", "multiply-dependency", "ordinary scalar multiply"},
  {"mac", "M33-TIMING-FWD-001", "mac-operand-position", "early multiplier versus late accumulator inputs"},
  {"long-mac", "M33-TIMING-FWD-001", "long-mac-accumulator", "both accumulator registers remain semantic dependencies"},
  {"divide", "M33-TIMING-DIV-001", "divide-255", "nonzero magnitudes; signed overflow excluded"},
  {"load", "M33-TIMING-LOAD-001", "last-ldm-register", "baseline only; last loaded register differs"},
  {"store", "M33-TIMING-STORE-001", "store-address-versus-data", "late data input is still a semantic use"},
  {"branch", "M33-BRANCH-001", "wide-target-halfword", "requires dynamic folding/history and final target width/alignment"},
  {"literal", "THUMB-LITERAL-001", "literal", "instruction bytes exclude pool, padding and skip branches"},
  {"mov-pair", "THUMB-MOV-001", "mov-pair", "explicit MOVW/MOVT pair, not a constant-materialization policy"},
  {"unknown", "RP2350-CONFIG-001", "", "unsupported or not yet selected; no guessed cost"}
};

static badge_cost_range exact(unsigned n) {
  return (badge_cost_range){n, n, 1};
}
static unsigned highest(uint32_t n) {
  unsigned bit = 0;
  while (n >>= 1)
    bit++;
  return bit;
}
badge_cost_estimate badge_target_cost(badge_cost_request r) {
  badge_cost_estimate out = {0};
  out.kind = r.kind;
  out.status = "conditional";
  if ((unsigned)r.kind >= BADGE_COST_KIND_COUNT ||
      (r.encoding_bytes && r.encoding_bytes != 2 && r.encoding_bytes != 4 &&
       !(r.kind == BADGE_COST_MOV_PAIR && r.encoding_bytes == 8)) ||
      (r.kind == BADGE_COST_MOV_PAIR && r.encoding_bytes && r.encoding_bytes != 8) ||
      (unsigned)r.memory > BADGE_COST_MEMORY_CONTENDED || r.registers > 16) {
    out.kind = BADGE_COST_UNKNOWN;
    out.status = "invalid-query";
    return out;
  }
  if (r.encoding_bytes)
    out.code_bytes = exact(r.encoding_bytes);
  if (r.kind == BADGE_COST_UNKNOWN) {
    out.status = "unsupported";
    return out;
  }
  out.baseline_issue = exact(1);
  out.resource_occupancy = exact(1);
  out.result_ready = exact(1);
  out.resources = BADGE_COST_RESOURCE_INTEGER;
  switch (r.kind) {
  case BADGE_COST_COMPLEX_ALU:
    out.baseline_issue = exact(r.sets_flags ? 2 : 1);
    out.result_ready = exact(2);
    out.resource_occupancy = out.baseline_issue;
    break;
  case BADGE_COST_MUL:
  case BADGE_COST_MAC:
  case BADGE_COST_LONG_MAC:
    out.result_ready = exact(2);
    out.resources = BADGE_COST_RESOURCE_MULTIPLY;
    break;
  case BADGE_COST_DIVIDE: {
    out.resources = BADGE_COST_RESOURCE_DIVIDER;
    uint32_t a = r.dividend, b = r.divisor;
    out.result_ready.known = 0;
    if (!r.operands_known) {
      out.baseline_issue = (badge_cost_range){2, 11, 1};
      out.status = "conditional-nonexceptional-operands";
    } else if (!b || !a ||
               (r.signed_divide && a == 0x80000000u && b == UINT32_MAX)) {
      out.baseline_issue.known = 0;
      out.status = "unknown-zero-or-exceptional-division";
    } else {
      if (r.signed_divide) {
        if (a & 0x80000000u) a = 0u - a;
        if (b & 0x80000000u) b = 0u - b;
      }
      int difference = (int)highest(a) - (int)highest(b);
      out.baseline_issue = exact(difference < 0 ? 2u : 4u + (unsigned)difference / 4u);
    }
    out.resource_occupancy = out.baseline_issue;
    if (out.baseline_issue.known)
      out.result_ready = (badge_cost_range){out.baseline_issue.min + 1,
                                          out.baseline_issue.max + 1, 1};
    break;
  }
  case BADGE_COST_LOAD:
  case BADGE_COST_LITERAL:
  case BADGE_COST_STORE:
    out.resources = BADGE_COST_RESOURCE_MEMORY;
    out.baseline_issue = exact(r.registers ? r.registers : 1);
    out.resource_occupancy = out.baseline_issue;
    out.memory_wait_unknown = r.memory != BADGE_COST_MEMORY_CORE_BASELINE;
    out.result_ready = exact(out.baseline_issue.min +
                             (r.kind != BADGE_COST_STORE));
    if (out.memory_wait_unknown || r.kind == BADGE_COST_STORE)
      out.result_ready.known = 0;
    break;
  case BADGE_COST_BRANCH:
    out.resources = BADGE_COST_RESOURCE_CONTROL |
                    (r.load_to_pc ? BADGE_COST_RESOURCE_MEMORY : 0);
    out.result_ready.known = 0;
    if (!r.branch_predicates_known) {
      out.baseline_issue.known = 0;
      out.resource_occupancy.known = 0;
      out.status = "unknown-branch-history-or-layout";
    } else {
      unsigned basic = r.load_to_pc ? (r.registers ? r.registers : 1) : 0;
      unsigned u = r.target_halfword && r.target_wide && !r.preceding_push_pop;
      unsigned cycles = r.taken ? basic + 2u + (r.load_to_pc != 0) + u -
                                     (r.direction_known && r.folded)
                               : basic + 1u - (r.folded != 0);
      out.baseline_issue = out.resource_occupancy = exact(cycles);
      out.memory_wait_unknown = r.load_to_pc &&
                               r.memory != BADGE_COST_MEMORY_CORE_BASELINE;
    }
    break;
  case BADGE_COST_MOV_PAIR:
    out.code_bytes = exact(8);
    out.baseline_issue = out.resource_occupancy = out.result_ready = exact(2);
    break;
  default:
    break;
  }
  return out;
}

badge_cost_dependency badge_target_dependency(badge_cost_estimate p,
                                              badge_cost_input input,
                                              unsigned intervening) {
  badge_cost_dependency out = {{0, 0, 0}, 1};
  if ((unsigned)input > BADGE_COST_INPUT_EARLIER_LOAD ||
      p.kind == BADGE_COST_UNKNOWN || !p.result_ready.known)
    return out;
  if (input == BADGE_COST_INPUT_ACCUMULATOR &&
      p.kind != BADGE_COST_MUL && p.kind != BADGE_COST_MAC &&
      p.kind != BADGE_COST_LONG_MAC)
    return out;
  if (input == BADGE_COST_INPUT_EARLIER_LOAD &&
      ((p.kind != BADGE_COST_LOAD && p.kind != BADGE_COST_LITERAL) ||
       p.baseline_issue.max <= 1))
    return out;
  unsigned late = input == BADGE_COST_INPUT_STORE_DATA;
  if (input == BADGE_COST_INPUT_ACCUMULATOR)
    late = p.kind == BADGE_COST_MUL || p.kind == BADGE_COST_MAC ||
           p.kind == BADGE_COST_LONG_MAC;
  unsigned latency = p.result_ready.max;
  if (input == BADGE_COST_INPUT_EARLIER_LOAD &&
      (p.kind == BADGE_COST_LOAD || p.kind == BADGE_COST_LITERAL))
    latency = p.baseline_issue.max;
  unsigned elapsed = p.baseline_issue.max + late;
  unsigned bubble = latency > elapsed ? latency - elapsed : 0;
  out.bubble = exact(intervening >= bubble ? 0 : bubble - intervening);
  return out;
}

badge_cost_range badge_target_add_bytes(unsigned rd, unsigned rn,
                                       uint32_t imm, int flags_dead,
                                       int outside_it) {
  if (rd > 12 || rn > 12)
    return (badge_cost_range){0};
  if (flags_dead && outside_it && rd < 8 && rn < 8 &&
      (imm <= 7 || (rd == rn && imm <= 255)))
    return exact(2);
  if (imm <= 4095 || thumb_modified(imm) >= 0)
    return exact(4);
  return (badge_cost_range){0};
}
badge_cost_range badge_target_literal_bytes(unsigned rd, uint32_t instruction,
                                           uint32_t pool) {
  if (rd > 12 || (instruction & 1u) || (pool & 3u) ||
      instruction > UINT32_MAX - 4)
    return (badge_cost_range){0};
  int64_t displacement = (int64_t)pool - ((instruction + 4u) & ~3u);
  if (rd < 8 && displacement >= 0 && displacement <= 1020)
    return exact(2);
  if (displacement >= -4095 && displacement <= 4095)
    return exact(4);
  return (badge_cost_range){0};
}
