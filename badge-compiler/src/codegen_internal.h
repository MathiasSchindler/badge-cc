#ifndef BADGE_COMPILER_CODEGEN_INTERNAL_H
#define BADGE_COMPILER_CODEGEN_INTERNAL_H

#include "codegen.h"
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cg_type cg_type;
struct cg_type {
  unsigned size, align, bits, count;
  cg_type *base, **fields;
  int packed;
};
typedef struct cg_token {
  char *text;
  unsigned line;
} cg_token;
typedef struct cg_value {
  char *name;
  cg_type *type;
  unsigned slot, object;
  unsigned object_size, object_align;
  unsigned memory_align, memory_size;
  struct cg_value *next;
} cg_value;
typedef struct cg_inst {
  unsigned begin, end;
  cg_value *result;
  struct cg_inst *next;
  struct cg_assembly *assembly;
  int assembly_checked;
  unsigned scalar_call_arguments_plus_one;
} cg_inst;
typedef struct cg_assembly {
  char *text;
  unsigned reads, writes;
  int flags_read, flags_write, memory_read, memory_write, memory_barrier;
  int control, known;
} cg_assembly;
typedef struct cg_block {
  char *name;
  cg_inst *first, *last;
  struct cg_block *next;
} cg_block;
typedef struct cg_function {
  char *name, *section;
  cg_type *ret;
  cg_value *values, **args;
  cg_value **value_table;
  unsigned value_count, value_capacity;
  unsigned argc, frame, byval[256], arg_align[256];
  cg_block *blocks, *last;
  unsigned id, alignment;
  unsigned machine_register_mentions;
  int machine_registers_known;
  int internal, return_ext;
  struct cg_function *next;
} cg_function;
typedef struct cg_allocation {
  struct cg_allocation *next;
} cg_allocation;
typedef struct {
  cg_value *value;
  cg_block *definition, *use_block;
  unsigned last_use, last_cache_use;
  int live_out;
  unsigned index, reg, uses, score;
  unsigned selection_uses;
  int assembly_home;
  unsigned forbidden, degree;
  unsigned home_color;
} cg_register_value;
typedef enum {
  CG_M_MOVW, CG_M_MOVT, CG_M_MOV, CG_M_ADD, CG_M_SUB,
  CG_M_AND, CG_M_ORR, CG_M_EOR, CG_M_MUL, CG_M_LSL, CG_M_LSR,
  CG_M_ASR, CG_M_LOAD_FRAME, CG_M_ADD_IMM, CG_M_SUB_IMM,
  CG_M_ADDW, CG_M_SUBW, CG_M_AND_IMM, CG_M_ORR_IMM, CG_M_EOR_IMM,
  CG_M_LSL_IMM, CG_M_LSR_IMM, CG_M_ASR_IMM, CG_M_MLA, CG_M_COUNT
} cg_machine_opcode;
typedef struct {
  cg_machine_opcode op;
  unsigned dst, left, right, accumulator;
  uint32_t immediate;
  unsigned reads, writes;
} cg_machine_instruction;
#define CG_SCHEDULE_WINDOW 8
#ifndef CG_SCHEDULE_WORK_LIMIT
#define CG_SCHEDULE_WORK_LIMIT 16000000u
#endif
typedef struct {
  jmp_buf failure;
  cg_allocation *allocations;
  size_t memory;
  cg_token *tokens;
  unsigned count, pos, line;
  const char *file;
  char *out;
  size_t size, capacity;
  cg_function *functions, *last, *fn;
  unsigned unique;
  unsigned phi_space;
  badge_frame_addressing frame_addressing;
  badge_register_residency register_residency;
  cg_register_value *register_table, *resident, *instruction_resident;
  unsigned register_capacity, register_work;
  unsigned avoided_loads, avoided_stores;
  cg_block *register_block;
  cg_inst *register_instruction;
  int register_active, resident_dirty;
  unsigned resident_register;
  unsigned register_saved_bytes, phi_register_copies, asm_spills;
  unsigned register_copies;
  unsigned call_entry_copies, call_stack_addresses;
  struct cg_global *global;
  badge_scalar_selection scalar_selection;
  struct cg_selection *selection;
  int layout_active;
  unsigned layout_fallthroughs, layout_inversions;
  unsigned layout_addresses, layout_literal_fallbacks, layout_relax_branches;
  badge_memory_selection memory_selection;
  unsigned word_copies, word_fills;
  uint64_t word_bytes, tail_bytes;
  cg_machine_instruction schedule[CG_SCHEDULE_WINDOW];
  unsigned schedule_count, schedule_work, schedule_moves, schedule_windows;
  unsigned schedule_hazards, schedule_unfilled, schedule_barriers;
  int schedule_active, schedule_flushing;
  int frame_sp_relative;
  int allocate_r6;
  int scalar_stream_selection;
  int scalar_stream_retry;
} cg_context;

#define CG_GLOBAL_REGISTER_COUNT 7
#define CG_SCRATCH_R4 (1u << 4)
#define CG_SCRATCH_R5 (1u << 5)

static inline int cg_width_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_WIDTHS ||
         c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS;
}
static inline int cg_light_frames(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_FRAMES ||
         c->scalar_selection == BADGE_SELECTION_THUMB_CONTROL ||
         c->scalar_selection == BADGE_SELECTION_THUMB_OPERANDS ||
         cg_width_selection(c);
}
static inline int cg_control_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_CONTROL ||
         c->scalar_selection == BADGE_SELECTION_THUMB_OPERANDS ||
         cg_width_selection(c);
}
static inline int cg_operand_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_OPERANDS ||
         cg_width_selection(c);
}
void cg_reuse_constants(cg_context *c, size_t body_start);
void cg_reduce_branches(cg_context *c, size_t body_start);
void cg_forward_byte_arguments(cg_context *c, size_t body_start);
void cg_fold_zero_branches(cg_context *c, size_t body_start);
void cg_fold_memory_addresses(cg_context *c, size_t body_start);
const char *cg_inverse_condition(const char *condition);
void cg_save_normalized(cg_context *c, cg_value *v, unsigned r);
static inline int cg_effect_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_EFFECTS || cg_light_frames(c);
}
cg_assembly *cg_instruction_assembly(cg_context *c, cg_inst *instruction);
cg_inst *cg_selection_countdown(cg_context *c, cg_inst *instruction);
int cg_emit_counted_leaf(cg_context *c);

static inline int cg_global_residency(const cg_context *c) {
  return c->register_residency == BADGE_RESIDENCY_GLOBAL ||
         c->register_residency == BADGE_RESIDENCY_GLOBAL_FLEX ||
         c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT;
}

static inline int cg_flexible_residency(const cg_context *c) {
  return c->register_residency == BADGE_RESIDENCY_GLOBAL_FLEX ||
         c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT;
}

static inline int cg_register_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_REGISTERS ||
         c->scalar_selection == BADGE_SELECTION_THUMB_LAYOUT ||
         c->scalar_selection == BADGE_SELECTION_THUMB_COMPACT ||
         c->scalar_selection == BADGE_SELECTION_THUMB_SCHEDULE ||
         c->scalar_selection == BADGE_SELECTION_THUMB_RELAX ||
         c->scalar_selection == BADGE_SELECTION_THUMB_BRANCH ||
         c->scalar_selection == BADGE_SELECTION_THUMB_HOMES ||
         c->scalar_selection == BADGE_SELECTION_THUMB_CALLS ||
         cg_effect_selection(c);
}
static inline int cg_home_selection(const cg_context *c) {
  return (c->scalar_selection == BADGE_SELECTION_THUMB_HOMES ||
          c->scalar_selection == BADGE_SELECTION_THUMB_CALLS ||
          cg_effect_selection(c)) &&
         c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT;
}
static inline int cg_call_selection(const cg_context *c) {
  return (c->scalar_selection == BADGE_SELECTION_THUMB_CALLS ||
          cg_effect_selection(c)) &&
         c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT;
}
static inline int cg_branch_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_BRANCH ||
         c->scalar_selection == BADGE_SELECTION_THUMB_HOMES ||
         c->scalar_selection == BADGE_SELECTION_THUMB_CALLS ||
         cg_effect_selection(c);
}
int cg_phi_constant(const char *token);
unsigned cg_phi_buffer_size(cg_context *c, cg_inst *i);

static inline int cg_relax_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_RELAX ||
         cg_width_selection(c);
}
static inline const char *cg_branch_width(cg_context *c) {
  if (cg_relax_selection(c)) {
    c->layout_relax_branches++;
    return "";
  }
  return ".w";
}
static inline int cg_schedule_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_SCHEDULE;
}
void cg_schedule_flush(cg_context *c);
void cg_machine(cg_context *c, cg_machine_opcode op, unsigned dst,
                unsigned left, unsigned right, unsigned accumulator,
                uint32_t immediate);
void cg_move(cg_context *c, unsigned dst, unsigned src);

static inline int cg_compact_selection(const cg_context *c) {
  return c->scalar_selection == BADGE_SELECTION_THUMB_COMPACT;
}
static inline int cg_narrow_memory_selection(const cg_context *c) {
  return cg_compact_selection(c) || cg_width_selection(c);
}

_Noreturn void cg_error(cg_context *c, const char *message);
void *cg_alloc(cg_context *c, size_t n);
void cg_emit(cg_context *c, const char *format, ...);
unsigned cg_round(unsigned n, unsigned a);
char *cg_name(cg_context *c, const char *s);
void cg_instruction(cg_context *c, cg_block *block, cg_inst *i);
void cg_value_read(cg_context *c, cg_type *t, unsigned r, unsigned word);

unsigned cg_slot(cg_context *c, unsigned size, unsigned align);
cg_value *cg_find(cg_context *c, const char *name);
cg_value *cg_define(cg_context *c, char *name, cg_type *t);
void cg_generate_function(cg_context *c, cg_function *f);
void cg_prepare_cfg(cg_context *c);
int cg_target_has_phi(cg_context *c, const char *target);

void cg_immediate(cg_context *c, unsigned r, uint32_t n);
int cg_compact_binary(cg_context *c, const char *op, unsigned dst,
                      unsigned left, unsigned right);
void cg_address(cg_context *c, unsigned r, unsigned offset);
void cg_stack(cg_context *c, int store, unsigned r, unsigned offset);
void cg_symbol(cg_context *c, unsigned r, const char *name, int64_t addend);
void cg_normalize(cg_context *c, cg_type *t, unsigned r);
void cg_save(cg_context *c, cg_value *v, unsigned r, unsigned word);
void cg_signed(cg_context *c, cg_type *t, unsigned r);
void cg_memory(cg_context *c, int store, cg_type *t, unsigned align);
unsigned cg_memory_scratch(cg_type *t, unsigned align);
unsigned cg_instruction_scratch(cg_context *c, cg_inst *instruction);
void cg_register_prepare(cg_context *c);
int cg_register_eligible(const cg_context *c, const cg_value *value);
void cg_register_block(cg_context *c, cg_block *block);
void cg_register_instruction(cg_context *c, cg_inst *instruction,
                             const char *op);
void cg_register_after_instruction(cg_context *c);
void cg_register_finish(cg_context *c);
int cg_register_read(cg_context *c, cg_value *value, unsigned r, unsigned word);
void cg_register_loaded(cg_context *c, cg_value *value, unsigned r,
                        unsigned word);
int cg_register_save(cg_context *c, cg_value *value, unsigned r, unsigned word);
int cg_register_copy(cg_context *c, cg_value *dst, cg_value *src);
void cg_register_work(cg_context *c);
cg_register_value *cg_register_lookup(cg_context *c, const char *name);
void cg_global_prepare(cg_context *c);
void cg_global_instruction(cg_context *c, cg_inst *instruction);
void cg_global_after_instruction(cg_context *c);
unsigned cg_global_register(cg_context *c, const char *name);
void cg_selection_prepare(cg_context *c);
int cg_selection_forward_argument(cg_context *c);
int cg_internal_call_mentions(cg_context *c, const char *name, unsigned *mask);
int cg_selection_barrier(cg_context *c, cg_inst *i);
int cg_selection_instruction(cg_context *c, cg_inst *i);
cg_inst *cg_selection_branch_compare(cg_context *c, cg_inst *i);
int cg_selection_compare_operands(cg_context *c, cg_inst *i, cg_type *type);
const char *cg_comparison_condition(const char *predicate);
int cg_selection_offset(cg_context *c, uint32_t offset);
int cg_selection_index(cg_context *c, cg_type *type, unsigned stride);
void cg_selection_finish(cg_context *c);

#endif
