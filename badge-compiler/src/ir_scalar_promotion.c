#include "ir_promotion.h"
#include <string.h>

enum { RETAIN, SINGLE_BLOCK, ENTRY_STORE };

const char *ir_scalar_slot_reason(const ir_analysis *a, unsigned i) {
  const ir_instruction *slot = &a->instructions[i];
  unsigned entry = a->functions[a->blocks[slot->block].function].first_block;
  if (slot->block != entry || a->blocks[entry].predecessor_count)
    return "not_once_executed_entry_alloca";
  if (slot->effects != IR_EFFECT_ALLOCATE)
    return "opaque_allocation";
  const ir_type *type = &a->types[slot->allocated_type];
  if (type->kind != IR_PTR &&
      !(type->kind == IR_INT &&
        (type->bits == 1 || type->bits == 8 || type->bits == 16 ||
         type->bits == 32 || type->bits == 64)))
    return "unsupported_storage_type";
  if (slot->operand_count) {
    const ir_operand *count = &a->operands[slot->first_operand];
    uint64_t n;
    if (count->kind != IR_OPERAND_LITERAL ||
        !ir_integer(a->text->tokens[count->token].text, &n) || !n ||
        *a->text->tokens[count->token].text == '-')
      return "nonpositive_or_dynamic_allocation";
  }
  for (unsigned k = a->definitions[slot->definition].first_use; k != IR_NONE;
       k = a->operands[k].next_use) {
    const ir_operand *use = &a->operands[k];
    const ir_instruction *in = &a->instructions[use->instruction];
    if (a->blocks[in->block].rpo == IR_NONE)
      return "unreachable_access";
    unsigned address;
    if (in->opcode == IR_OP_LOAD)
      address = in->first_operand;
    else if (in->opcode == IR_OP_STORE)
      address = in->first_operand + 1;
    else
      return "escaping_or_indirect_use";
    if (k != address)
      return "escaping_or_indirect_use";
    unsigned expected =
        in->opcode == IR_OP_LOAD ? IR_EFFECT_READ : IR_EFFECT_WRITE;
    if (in->effects != (expected | IR_EFFECT_TRAP))
      return "volatile_or_opaque_access";
    unsigned accessed = in->opcode == IR_OP_LOAD
                            ? in->type
                            : a->operands[in->first_operand].type;
    if (!ir_type_equal(a, slot->allocated_type, accessed))
      return "mismatched_access_type";
  }
  return NULL;
}

static const char *classify(const ir_analysis *a, unsigned i, unsigned *mode) {
  const char *reason = ir_scalar_slot_reason(a, i);
  if (reason)
    return reason;
  const ir_instruction *slot = &a->instructions[i];
  unsigned entry = slot->block;
  unsigned block = IR_NONE, multiple_blocks = 0, stores = 0;
  unsigned first_store = IR_NONE, first_load = IR_NONE;
  for (unsigned k = a->definitions[slot->definition].first_use; k != IR_NONE;
       k = a->operands[k].next_use) {
    const ir_operand *use = &a->operands[k];
    const ir_instruction *in = &a->instructions[use->instruction];
    if (block == IR_NONE)
      block = in->block;
    else if (block != in->block)
      multiple_blocks = 1;
    if (in->opcode == IR_OP_STORE) {
      stores++;
      if (use->instruction < first_store)
        first_store = use->instruction;
    } else if (use->instruction < first_load)
      first_load = use->instruction;
  }
  if (!multiple_blocks) {
    if (stores && first_load < first_store && block != entry)
      return "read_before_store_outside_entry";
    *mode = SINGLE_BLOCK;
    return NULL;
  }
  if (stores != 1 || a->instructions[first_store].block != entry)
    return "requires_cfg_promotion";
  for (unsigned k = a->definitions[slot->definition].first_use; k != IR_NONE;
       k = a->operands[k].next_use) {
    const ir_instruction *in = &a->instructions[a->operands[k].instruction];
    if (in->opcode != IR_OP_LOAD)
      continue;
    /* Entry dominates every reachable block; position matters within entry. */
    if (a->blocks[in->block].rpo == IR_NONE ||
        (in->block == entry && a->operands[k].instruction <= first_store))
      return "load_not_preceded_by_entry_store";
  }
  *mode = ENTRY_STORE;
  return NULL;
}

static void resolve(ir_context *c, const ir_analysis *a, unsigned definition,
                    const unsigned *aliases, const char **values,
                    unsigned *path) {
  unsigned d = definition, length = 0;
  const char *value;
  for (;;) {
    if (values[d]) {
      value = values[d];
      break;
    }
    if (length == a->definition_count)
      ir_error(c, "scalar-promotion substitution cycle");
    path[length++] = d;
    unsigned operand = aliases[d];
    if (operand == a->operand_count) {
      value = "undef";
      break;
    }
    if (operand == IR_NONE)
      ir_error(c, "missing scalar-promotion substitution");
    const ir_operand *source = &a->operands[operand];
    if (source->kind != IR_OPERAND_LOCAL ||
        aliases[source->definition] == IR_NONE) {
      value = a->text->tokens[source->token].text;
      break;
    }
    d = source->definition;
  }
  while (length)
    values[path[--length]] = value;
}

void ir_scalar_promotion(ir_context *c, ir_module *m) {
  ir_analysis a;
  ir_analyze(c, m, &a);
  unsigned *modes = ir_allocate(c, (size_t)a.definition_count * sizeof(*modes));
  unsigned *current =
      ir_allocate(c, (size_t)a.definition_count * sizeof(*current));
  unsigned *aliases =
      ir_allocate(c, (size_t)a.definition_count * sizeof(*aliases));
  const char **values =
      ir_allocate(c, (size_t)a.definition_count * sizeof(*values));
  unsigned *path = ir_allocate(c, (size_t)a.definition_count * sizeof(*path));
  for (unsigned d = 0; d < a.definition_count; d++) {
    current[d] = a.operand_count;
    aliases[d] = IR_NONE;
  }
  for (unsigned i = 0; i < a.instruction_count; i++) {
    const ir_instruction *in = &a.instructions[i];
    if (in->opcode != IR_OP_ALLOCA)
      continue;
    c->line = in->line;
    ir_note(c, "candidate", "stack_slot", 1);
    const char *reason = classify(&a, i, &modes[in->definition]);
    if (reason)
      ir_note(c, "skipped", reason, 1);
    else {
      ir_note(c, "eligible",
              modes[in->definition] == SINGLE_BLOCK ? "single_block"
                                                    : "single_entry_store",
              1);
      ir_promotion_erase(m, in);
      ir_note(c, "transformed", "promoted_slot", 1);
    }
  }
  for (unsigned i = 0; i < a.instruction_count; i++) {
    const ir_instruction *in = &a.instructions[i];
    if (in->opcode != IR_OP_LOAD && in->opcode != IR_OP_STORE)
      continue;
    unsigned address = in->first_operand + (in->opcode == IR_OP_STORE);
    const ir_operand *pointer = &a.operands[address];
    if (pointer->kind != IR_OPERAND_LOCAL || !modes[pointer->definition])
      continue;
    unsigned slot = pointer->definition;
    if (in->opcode == IR_OP_STORE) {
      current[slot] = in->first_operand;
      ir_note(c, "transformed", "store_removed", 1);
    } else {
      aliases[in->definition] = current[slot];
      ir_note(c, "transformed", "load_removed", 1);
      if (current[slot] == a.operand_count)
        ir_note(c, "propagated", "uninitialized_undef", 1);
    }
    ir_promotion_erase(m, in);
  }
  for (unsigned d = 0; d < a.definition_count; d++)
    if (aliases[d] != IR_NONE)
      resolve(c, &a, d, aliases, values, path);
  for (unsigned k = 0; k < a.operand_count; k++) {
    const ir_operand *operand = &a.operands[k];
    if (operand->kind == IR_OPERAND_LOCAL && values[operand->definition]) {
      m->tokens[operand->token].replacement = values[operand->definition];
      ir_note(c, "propagated", "load_result_operand", 1);
    }
  }
}
