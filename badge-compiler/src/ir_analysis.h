#ifndef BADGE_IR_ANALYSIS_H
#define BADGE_IR_ANALYSIS_H

#include "ir_pass.h"
#include <limits.h>

#define IR_NONE UINT_MAX

typedef struct {
  const char *name;
  unsigned value;
} ir_index_entry;
typedef struct {
  ir_index_entry *entries;
  unsigned capacity, count;
} ir_index;

typedef enum { IR_VOID, IR_INT, IR_PTR, IR_ARRAY, IR_STRUCT } ir_type_kind;
typedef struct {
  ir_type_kind kind;
  unsigned bits, count, element, first_field, packed;
  const char *key;
} ir_type;

typedef enum {
  IR_OP_ALLOCA,
  IR_OP_LOAD,
  IR_OP_STORE,
  IR_OP_BINARY,
  IR_OP_COMPARE,
  IR_OP_CAST,
  IR_OP_FREEZE,
  IR_OP_SELECT,
  IR_OP_GEP,
  IR_OP_EXTRACT,
  IR_OP_INSERT,
  IR_OP_PHI,
  IR_OP_CALL,
  IR_OP_RET,
  IR_OP_BR,
  IR_OP_SWITCH,
  IR_OP_UNREACHABLE
} ir_opcode;
enum {
  IR_EFFECT_READ = 1u,
  IR_EFFECT_WRITE = 2u,
  IR_EFFECT_TRAP = 4u,
  IR_EFFECT_VOLATILE = 8u,
  IR_EFFECT_CALL = 16u,
  IR_EFFECT_CONTROL = 32u,
  IR_EFFECT_OPAQUE = 64u,
  IR_EFFECT_ALLOCATE = 128u
};
typedef enum {
  IR_OPERAND_LOCAL,
  IR_OPERAND_GLOBAL,
  IR_OPERAND_LITERAL,
  IR_OPERAND_BLOCK
} ir_operand_kind;
typedef struct {
  ir_operand_kind kind;
  unsigned type, token, definition, block, next_use, instruction;
} ir_operand;
typedef struct {
  const char *name;
  unsigned type, instruction, block, first_use, use_count;
} ir_definition;
typedef struct {
  ir_opcode opcode;
  const char *operation;
  unsigned line, end_line, block, type, allocated_type, definition,
      first_operand, operand_count, effects, callee, first_call_parameter,
      call_parameter_count;
  int explicit_signature, variadic_call;
} ir_instruction;
typedef struct {
  const char *name;
  unsigned line, function, first_instruction, instruction_count,
      first_successor, successor_count, first_predecessor, predecessor_count;
  unsigned rpo, idom;
} ir_block;
typedef struct {
  const char *name;
  unsigned line, return_type, first_parameter, parameter_count, first_block,
      block_count, first_definition, definition_count;
  int declaration, variadic;
  ir_index locals, labels;
} ir_function;
typedef struct {
  const char *name;
  unsigned function, storage_type, line;
} ir_global;
typedef struct {
  ir_module *text;
  ir_type *types;
  unsigned type_count;
  unsigned *fields, field_count;
  ir_function *functions;
  unsigned function_count;
  ir_global *globals;
  unsigned global_count;
  ir_definition *definitions;
  unsigned definition_count;
  ir_instruction *instructions;
  unsigned instruction_count;
  ir_operand *operands;
  unsigned operand_count;
  ir_block *blocks;
  unsigned block_count;
  unsigned *successors, successor_count, *predecessors, predecessor_count;
  unsigned *parameters, parameter_count;
  ir_index type_index, global_index;
  unsigned capacity;
} ir_analysis;

/* The view owns no text: all storage has the context's scratch lifetime. */
void ir_analyze(ir_context *, ir_module *, ir_analysis *);
void ir_verify(ir_context *, ir_module *);
int ir_type_equal(const ir_analysis *, unsigned, unsigned);
int ir_dominates(const ir_analysis *, unsigned, unsigned);

#endif
