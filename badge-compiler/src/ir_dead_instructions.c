#include "ir_analysis.h"

static int eligible(const ir_instruction *in) {
  if (in->definition == IR_NONE || in->effects)
    return 0;
  switch (in->opcode) {
  case IR_OP_BINARY:
  case IR_OP_COMPARE:
  case IR_OP_CAST:
  case IR_OP_FREEZE:
  case IR_OP_SELECT:
  case IR_OP_GEP:
  case IR_OP_EXTRACT:
  case IR_OP_INSERT:
  case IR_OP_PHI:
    return 1;
  default:
    return 0;
  }
}

void ir_dead_instructions(ir_context *c, ir_module *m) {
  ir_analysis a;
  ir_analyze(c, m, &a);
  unsigned *uses = ir_allocate(c, (size_t)a.definition_count * sizeof(*uses));
  unsigned *work = ir_allocate(c, (size_t)a.instruction_count * sizeof(*work));
  unsigned count = 0;
  for (unsigned d = 0; d < a.definition_count; d++)
    uses[d] = a.definitions[d].use_count;
  for (unsigned i = 0; i < a.instruction_count; i++) {
    const ir_instruction *in = &a.instructions[i];
    if (!eligible(in)) {
      ir_note(c, "skipped", "effectful_or_not_whitelisted", 1);
      continue;
    }
    ir_note(c, "candidate", "pure_nontrapping_definition", 1);
    if (!uses[in->definition])
      work[count++] = i;
  }
  while (count) {
    const ir_instruction *in = &a.instructions[work[--count]];
    c->line = in->line;
    for (unsigned line = in->line; line <= in->end_line; line++)
      m->lines[line - 1].replacement = "";
    ir_note(c, "transformed", "unused_definition_removed", 1);
    for (unsigned k = in->first_operand;
         k < in->first_operand + in->operand_count; k++) {
      const ir_operand *operand = &a.operands[k];
      if (operand->kind != IR_OPERAND_LOCAL)
        continue;
      unsigned d = operand->definition;
      if (!uses[d])
        ir_error(c, "dead-instruction use-count underflow");
      if (--uses[d] || a.definitions[d].instruction == IR_NONE)
        continue;
      unsigned producer = a.definitions[d].instruction;
      if (eligible(&a.instructions[producer])) {
        if (count == a.instruction_count)
          ir_error(c, "dead-instruction worklist limit exceeded");
        work[count++] = producer;
      }
    }
  }
  for (unsigned i = 0; i < a.instruction_count; i++)
    if (eligible(&a.instructions[i]) && uses[a.instructions[i].definition])
      ir_note(c, "skipped", "still_referenced_or_cyclic", 1);
}
