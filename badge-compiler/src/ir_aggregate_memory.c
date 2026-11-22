#include "ir_analysis.h"
#include <inttypes.h>
#include <string.h>

static unsigned result_buffer_align(ir_module *m, unsigned start,
                                    unsigned end) {
  for (unsigned k = start; k + 1 < end; k++) {
    if (strcmp(ir_text(m, k), "align"))
      continue;
    uint64_t n;
    return ir_integer(ir_text(m, k + 1), &n) && n && n <= 65536 &&
                   !(n & (n - 1))
               ? (unsigned)n
               : 0;
  }
  return 1;
}

static unsigned byte_buffer(const ir_analysis *a, const ir_definition *def) {
  if (def->instruction == IR_NONE)
    return 0;
  const ir_instruction *in = &a->instructions[def->instruction];
  if (in->opcode != IR_OP_ALLOCA || in->operand_count ||
      in->line != in->end_line)
    return 0;
  const ir_type *type = &a->types[in->allocated_type];
  return type->kind == IR_ARRAY && a->types[type->element].kind == IR_INT &&
                 a->types[type->element].bits == 8
             ? type->count
             : 0;
}

static int buffer_used_after_copy(const ir_analysis *a, unsigned definition,
                                  unsigned copy) {
  unsigned queue[32], count = 1, work = 512;
  queue[0] = definition;
  for (unsigned j = 0; j < count; j++) {
    for (unsigned k = a->definitions[queue[j]].first_use; k != IR_NONE;
         k = a->operands[k].next_use) {
      if (!work--)
        return 0;
      unsigned user = a->operands[k].instruction;
      if (user == copy && !j && k == a->instructions[copy].first_operand + 1)
        continue;
      const ir_instruction *in = &a->instructions[user];
      if (k != in->first_operand || (in->effects & IR_EFFECT_VOLATILE) ||
          (in->opcode != IR_OP_LOAD && in->opcode != IR_OP_GEP))
        return 0;
      if (!j) {
        unsigned block = a->instructions[copy].block;
        if (in->block == block ? user <= copy
                               : !ir_dominates(a, block, in->block))
          return 0;
      }
      if (in->opcode == IR_OP_GEP) {
        if (count == sizeof(queue) / sizeof(*queue))
          return 0;
        queue[count++] = in->definition;
      }
    }
  }
  return 1;
}

static int buffer_only_written(const ir_analysis *a, unsigned definition) {
  unsigned queue[32], count = 1, work = 512;
  queue[0] = definition;
  for (unsigned j = 0; j < count; j++)
    for (unsigned k = a->definitions[queue[j]].first_use; k != IR_NONE;
         k = a->operands[k].next_use) {
      if (!work--)
        return 0;
      const ir_instruction *in = &a->instructions[a->operands[k].instruction];
      if (in->opcode == IR_OP_STORE && k == in->first_operand + 1 &&
          !(in->effects & IR_EFFECT_VOLATILE))
        continue;
      if (in->opcode == IR_OP_CALL && in->callee != IR_NONE &&
          k == in->first_operand + 1 && in->operand_count == 5 &&
          !strcmp(ir_text(a->text, a->operands[in->callee].token),
                  "@llvm.memcpy.inline.p0.p0.i32") &&
          !strcmp(ir_text(a->text, a->operands[k + 3].token), "false"))
        continue;
      if (in->opcode != IR_OP_GEP || k != in->first_operand ||
          count == sizeof(queue) / sizeof(*queue))
        return 0;
      queue[count++] = in->definition;
    }
  return 1;
}

void ir_sret_forward(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  ir_analysis a;
  ir_analyze(c, m, &a);
  for (unsigned i = 1; i < a.instruction_count; i++) {
    const ir_instruction *copy = &a.instructions[i], *call = copy - 1;
    if (copy->opcode != IR_OP_CALL || copy->callee == IR_NONE ||
        copy->block != call->block || copy->operand_count != 5 ||
        copy->line != copy->end_line || call->line != call->end_line ||
        strcmp(ir_text(m, a.operands[copy->callee].token),
               "@llvm.memcpy.inline.p0.p0.i32") ||
        call->opcode != IR_OP_CALL || call->callee == IR_NONE ||
        call->operand_count < 2)
      continue;
    const ir_operand *target = &a.operands[call->callee];
    if (target->kind != IR_OPERAND_GLOBAL)
      continue;
    unsigned f = a.globals[target->definition].function;
    if (f == IR_NONE || a.functions[f].declaration ||
        !a.functions[f].parameter_count ||
        !buffer_only_written(&a, a.functions[f].first_definition))
      continue;
    const ir_line *header = &m->lines[a.functions[f].line - 1];
    const char *linkage = ir_text(m, header->first + 1);
    if (strcmp(linkage, "internal") && strcmp(linkage, "private"))
      continue;
    const ir_operand *dst = &a.operands[copy->first_operand + 1],
                     *src = dst + 1;
    const ir_operand *out = &a.operands[call->first_operand + 1];
    uint64_t bytes;
    if (dst->kind != IR_OPERAND_LOCAL || src->kind != IR_OPERAND_LOCAL ||
        dst->definition == src->definition || out->kind != IR_OPERAND_LOCAL ||
        out->definition != src->definition ||
        !ir_integer(ir_text(m, (src + 1)->token), &bytes) || !bytes ||
        bytes > 256 || strcmp(ir_text(m, (src + 2)->token), "false"))
      continue;
    const ir_definition *from = &a.definitions[src->definition],
                        *to = &a.definitions[dst->definition];
    if (from->use_count != 2 || byte_buffer(&a, from) != bytes ||
        byte_buffer(&a, to) != bytes || m->tokens[out->token].replacement ||
        m->lines[a.instructions[from->instruction].line - 1].replacement ||
        m->lines[copy->line - 1].replacement)
      continue;
    unsigned k = target->token + 1;
    uint64_t sret_bytes;
    if (out->token < k + 10 || strcmp(ir_text(m, k), "(") ||
        strcmp(ir_text(m, k + 1), "ptr") || strcmp(ir_text(m, k + 2), "sret") ||
        strcmp(ir_text(m, k + 3), "(") || strcmp(ir_text(m, k + 4), "[") ||
        !ir_integer(ir_text(m, k + 5), &sret_bytes) || sret_bytes != bytes ||
        strcmp(ir_text(m, k + 6), "x") || strcmp(ir_text(m, k + 7), "i8") ||
        strcmp(ir_text(m, k + 8), "]") || strcmp(ir_text(m, k + 9), ")"))
      continue;
    unsigned required = result_buffer_align(m, k + 10, out->token);
    const ir_line *alloca = &m->lines[a.instructions[to->instruction].line - 1];
    if (!required ||
        result_buffer_align(m, alloca->op, alloca->first + alloca->count) <
            required ||
        !buffer_used_after_copy(&a, dst->definition, i))
      continue;
    m->tokens[out->token].replacement = to->name;
    m->lines[a.instructions[from->instruction].line - 1].replacement = "";
    m->lines[copy->line - 1].replacement = "";
    ir_note(c, "transformed", "sret_destination_forwarded", 1);
  }
  ir_reparse(c, m, mark);
}

void ir_aggregate_memory(ir_context *c, ir_module *m) {
  for (unsigned l = 0; l < m->line_count; l++) {
    ir_line *line = &m->lines[l];
    c->line = l + 1;
    if (!line->instruction)
      continue;
    unsigned p = line->op, end = line->first + line->count;
    const char *op = ir_text(m, p++);
    int store = !strcmp(op, "store");
    if (!store && strcmp(op, "load"))
      continue;
    ir_note(c, "candidate", "memory_access", 1);
    int vol = !strcmp(ir_text(m, p), "volatile");
    if (vol)
      p++;
    if (strcmp(ir_text(m, p), "[")) {
      ir_note(c, "skipped", "not_array_carrier", 1);
      continue;
    }
    uint64_t count;
    unsigned bits = ir_bits(ir_text(m, p + 3));
    if (!ir_integer(ir_text(m, p + 1), &count) ||
        strcmp(ir_text(m, p + 2), "x") || !bits ||
        strcmp(ir_text(m, p + 4), "]"))
      ir_error(c, "unsupported aggregate memory type");
    if (bits < 32 || count > 256 || !count)
      ir_error(c, "aggregate memory requires 1..256 i32/i64 carriers");
    unsigned bytes = bits / 8, total = (unsigned)count * bytes;
    if (total <= 8) {
      ir_note(c, "skipped", "backend_supported_carrier", 1);
      continue;
    }
    const char *type = ir_format(c, "[%" PRIu64 " x i%u]", count, bits);
    p += 5;
    const char *value = store ? ir_text(m, p++) : NULL;
    if (strcmp(ir_text(m, p++), ",") || strcmp(ir_text(m, p++), "ptr"))
      ir_error(c, "invalid aggregate memory operands");
    const char *address = ir_text(m, p++);
    if (*address != '%' && *address != '@' && strcmp(address, "null"))
      ir_error(c, "aggregate memory requires a simple address");
    if (store && *value != '%' && strcmp(value, "zeroinitializer") &&
        strcmp(value, "undef") && strcmp(value, "poison"))
      ir_error(c, "aggregate store requires an SSA/zero value");
    unsigned align = bytes;
    if (p < end) {
      uint64_t n;
      if (end - p != 3 || strcmp(ir_text(m, p), ",") ||
          strcmp(ir_text(m, p + 1), "align") ||
          !ir_integer(ir_text(m, p + 2), &n) || !n || (n & (n - 1)) ||
          n > 65536)
        ir_error(c, "unsupported aggregate memory suffix/alignment");
      align = (unsigned)n;
    }
    if (vol)
      ir_error(c, "wide volatile aggregate SSA memory unsupported");
    ir_note(c, "transformed", "wide_carrier_scalarized", 1);
    ir_note(c, "generated", "scalar_memory_access", count);
    size_t output_start = c->size;
    const char *previous = "poison";
    for (unsigned k = 0; k < count; k++) {
      const char *ptr = address;
      if (k) {
        ptr = ir_fresh(c, m, '%');
        ir_emit(c, "  %s = getelementptr i8, ptr %s, i32 %u\n", ptr, address,
                k * bytes);
      }
      unsigned part_align = align < bytes ? align : bytes;
      const char *scalar = ir_fresh(c, m, '%');
      if (store) {
        ir_emit(c, "  %s = extractvalue %s %s, %u\n", scalar, type, value, k);
        ir_emit(c, "  store i%u %s, ptr %s, align %u\n", bits, scalar, ptr,
                part_align);
      } else {
        ir_emit(c, "  %s = load i%u, ptr %s, align %u\n", scalar, bits, ptr,
                part_align);
        const char *next =
            k + 1 == count ? ir_text(m, line->first) : ir_fresh(c, m, '%');
        ir_emit(c, "  %s = insertvalue %s %s, i%u %s, %u\n", next, type,
                previous, bits, scalar, k);
        previous = next;
      }
    }
    line->replacement =
        ir_copy(c, c->output + output_start, c->size - output_start);
    c->size = output_start;
  }
}
