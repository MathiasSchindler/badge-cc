#include "ir_analysis.h"
#include "ir_loop_admission.h"
#include <inttypes.h>
#include <string.h>

void ir_mem2reg(ir_context *, ir_module *);

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

static int buffer_offset(const ir_analysis *a, const ir_operand *address,
                         unsigned buffer, uint64_t *offset) {
  if (address->kind != IR_OPERAND_LOCAL)
    return 0;
  if (address->definition == buffer) {
    *offset = 0;
    return 1;
  }
  unsigned def = a->definitions[address->definition].instruction;
  if (def == IR_NONE)
    return 0;
  const ir_instruction *gep = &a->instructions[def];
  if (gep->opcode != IR_OP_GEP || gep->operand_count != 2)
    return 0;
  const ir_line *line = &a->text->lines[gep->line - 1];
  const ir_operand *base = &a->operands[gep->first_operand];
  return gep->line == gep->end_line && line->count == 10 &&
         !strcmp(ir_text(a->text, line->op + 1), "i8") &&
         base->kind == IR_OPERAND_LOCAL && base->definition == buffer &&
         base[1].kind == IR_OPERAND_LITERAL &&
         a->types[base[1].type].kind == IR_INT &&
         a->types[base[1].type].bits == 32 &&
         ir_integer(ir_text(a->text, base[1].token), offset);
}

static int remove_overwritten_fills(ir_context *c, ir_analysis *a) {
  enum { MAX_BYTES = 32, WINDOW = 24 };
  ir_module *m = a->text;
  int changed = 0;
  for (unsigned i = 0; i < a->instruction_count; ++i) {
    const ir_instruction *fill = &a->instructions[i];
    if (fill->opcode != IR_OP_CALL || fill->callee == IR_NONE ||
        fill->operand_count != 5 || fill->line != fill->end_line ||
        strcmp(ir_text(m, a->operands[fill->callee].token),
               "@llvm.memset.inline.p0.i32"))
      continue;
    const ir_operand *buffer = &a->operands[fill->first_operand + 1];
    uint64_t size;
    if (buffer->kind != IR_OPERAND_LOCAL ||
        buffer[2].kind != IR_OPERAND_LITERAL ||
        !ir_integer(ir_text(m, buffer[2].token), &size) ||
        !size || size > MAX_BYTES ||
        byte_buffer(a, &a->definitions[buffer->definition]) != size ||
        strcmp(ir_text(m, buffer[3].token), "false"))
      continue;
    uint64_t remaining = (UINT64_C(1) << size) - 1;
    for (unsigned j = i + 1; j <= i + WINDOW && j < a->instruction_count; ++j) {
      const ir_instruction *next = &a->instructions[j];
      if (next->block != fill->block ||
          (next->effects & (IR_EFFECT_VOLATILE | IR_EFFECT_OPAQUE | IR_EFFECT_CALL |
                            IR_EFFECT_CONTROL)))
        break;
      if (!(next->effects & (IR_EFFECT_READ | IR_EFFECT_WRITE)))
        continue;
      const ir_operand *op = &a->operands[next->first_operand];
      if (next->opcode == IR_OP_LOAD && op->kind == IR_OPERAND_GLOBAL)
        continue;
      if (next->opcode != IR_OP_STORE || next->line != next->end_line)
        break;
      uint64_t offset;
      const ir_type *type = &a->types[op->type];
      if (type->kind != IR_INT ||
          (type->bits != 8 && type->bits != 16 && type->bits != 32) ||
          !buffer_offset(a, op + 1, buffer->definition, &offset) ||
          offset >= size || type->bits / 8 > size - offset)
        break;
      remaining &= ~(((UINT64_C(1) << (type->bits / 8)) - 1) << offset);
      if (!remaining) {
        m->lines[fill->line - 1].replacement = "";
        ir_note(c, "candidate", "overwritten_private_fill", 1);
        changed = 1;
        break;
      }
    }
  }
  return changed;
}

static const ir_operand *swap_byte(const ir_analysis *a, const ir_operand *value,
                                   int high) {
  if (value->kind != IR_OPERAND_LOCAL)
    return NULL;
  unsigned i = a->definitions[value->definition].instruction;
  if (i == IR_NONE)
    return NULL;
  const ir_instruction *cast = &a->instructions[i];
  const ir_module *m = a->text;
  if (cast->opcode != IR_OP_CAST || strcmp(cast->operation, "trunc") ||
      cast->operand_count != 1 || a->types[cast->type].bits != 8 ||
      cast->line != cast->end_line || m->lines[cast->line - 1].count != 7)
    return NULL;
  const ir_operand *word = &a->operands[cast->first_operand];
  if (word->kind != IR_OPERAND_LOCAL || a->types[word->type].bits != 32)
    return NULL;
  if (!high)
    return word;
  i = a->definitions[word->definition].instruction;
  if (i == IR_NONE)
    return NULL;
  const ir_instruction *shift = &a->instructions[i];
  const ir_operand *source = &a->operands[shift->first_operand];
  if (shift->opcode != IR_OP_BINARY || strcmp(shift->operation, "lshr") ||
      shift->operand_count != 2 || shift->line != shift->end_line ||
      m->lines[shift->line - 1].count != 7 ||
      source->kind != IR_OPERAND_LOCAL ||
      strcmp(ir_text(m, source[1].token), "8"))
    return NULL;
  return source;
}

static int pack_byte_pairs(ir_context *c, ir_analysis *a) {
  enum { WINDOW = 6 };
  ir_module *m = a->text;
  int little = 0;
  for (unsigned n = 0; n < m->line_count; ++n) {
    const ir_line *line = &m->lines[n];
    if (line->count == 4 && !strcmp(ir_text(m, line->first), "target") &&
        !strcmp(ir_text(m, line->first + 1), "datalayout"))
      little = !strncmp(ir_text(m, line->first + 3), "\"e-", 3);
  }
  if (!little)
    return 0;
  int changed = 0;
  for (unsigned i = 0; i < a->instruction_count; ++i) {
    const ir_instruction *store = &a->instructions[i];
    ir_line *row = &m->lines[store->line - 1];
    if (store->opcode != IR_OP_STORE || store->effects != (IR_EFFECT_WRITE | IR_EFFECT_TRAP) ||
        store->line != store->end_line || row->replacement || row->count != 9)
      continue;
    const ir_operand *first = &a->operands[store->first_operand], *addr = first + 1;
    if (addr->kind != IR_OPERAND_LOCAL || a->types[first->type].bits != 8)
      continue;
    unsigned base = addr->definition, def = a->definitions[base].instruction;
    if (def == IR_NONE)
      continue;
    const ir_instruction *where = &a->instructions[def];
    if (where->opcode == IR_OP_GEP && where->operand_count == 2) {
      const ir_operand *operand = &a->operands[where->first_operand];
      if (operand->kind != IR_OPERAND_LOCAL)
        continue;
      base = operand->definition;
    }
    const ir_definition *object = &a->definitions[base];
    unsigned capacity = byte_buffer(a, object);
    uint64_t offset;
    if (capacity < 2 || capacity > 32 || !buffer_offset(a, addr, base, &offset) ||
        offset % 2 || offset > capacity - 2)
      continue;
    ir_line *allocation = &m->lines[a->instructions[object->instruction].line - 1];
    unsigned align = result_buffer_align(m, allocation->op, allocation->first + allocation->count);
    if (!align || (align < 2 && allocation->count != 11))
      continue;
    const ir_operand *source = swap_byte(a, first, 1);
    if (!source)
      continue;
    for (unsigned j = i + 1; j <= i + WINDOW && j < a->instruction_count; ++j) {
      const ir_instruction *second = &a->instructions[j];
      if (second->block != store->block)
        break;
      if (!second->effects)
        continue;
      ir_line *next = &m->lines[second->line - 1];
      if (second->opcode != IR_OP_STORE || second->effects != store->effects ||
          second->line != second->end_line || next->count != 9 || next->replacement)
        break;
      const ir_operand *low = &a->operands[second->first_operand];
      uint64_t following;
      const ir_operand *same = swap_byte(a, low, 0);
      if (a->types[low->type].bits == 8 && same &&
          same->definition == source->definition &&
          buffer_offset(a, low + 1, base, &following) && following == offset + 1) {
        const char *half = ir_fresh(c, m, '%'), *swapped = ir_fresh(c, m, '%');
        row->replacement = ir_format(c,
            "  %s = trunc i32 %s to i16\n  %s = call i16 @llvm.bswap.i16(i16 %s)\n"
            "  store i16 %s, ptr %s, align 2\n", half, ir_text(m, source->token),
            swapped, half, swapped, ir_text(m, addr->token));
        next->replacement = "";
        if (a->definitions[first->definition].use_count == 1) {
          const ir_instruction *high = &a->instructions[a->definitions[first->definition].instruction];
          m->lines[high->line - 1].replacement = "";
          const ir_operand *shift = &a->operands[high->first_operand];
          if (a->definitions[shift->definition].use_count == 1)
            m->lines[a->instructions[a->definitions[shift->definition].instruction].line - 1].replacement = "";
        }
        if (a->definitions[low->definition].use_count == 1)
          m->lines[a->instructions[a->definitions[low->definition].instruction].line - 1].replacement = "";
        const ir_operand *tail = low + 1;
        if (tail->definition != base && a->definitions[tail->definition].use_count == 1)
          m->lines[a->instructions[a->definitions[tail->definition].instruction].line - 1].replacement = "";
        if (align < 2) {
          const char *name = object->name;
          allocation->replacement = ir_format(c, "  %s = alloca [%u x i8], align 2\n",
                                               name, capacity);
        }
        ir_note(c, "candidate", "private_big_endian_halfword_store", 1);
        changed = 1;
      }
      break;
    }
  }
  return changed;
}

static const ir_operand *plain_copy(const ir_analysis *a, const ir_instruction *in,
                                     uint64_t *bytes) {
  if (in->opcode != IR_OP_CALL || in->callee == IR_NONE || in->operand_count != 5 ||
      in->line != in->end_line ||
      strcmp(ir_text(a->text, a->operands[in->callee].token), "@llvm.memcpy.inline.p0.p0.i32"))
    return NULL;
  const ir_operand *dest = &a->operands[in->first_operand + 1];
  const ir_line *line = &a->text->lines[in->line - 1];
  // Replacing the source cannot carry over align/dereferenceable metadata.
  if (line->count != 16 || dest[2].kind != IR_OPERAND_LITERAL ||
      !ir_integer(ir_text(a->text, dest[2].token), bytes) || !*bytes ||
      strcmp(ir_text(a->text, dest[3].token), "false"))
    return NULL;
  return dest;
}

void ir_copy_chains(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  ir_analysis a;
  ir_analyze(c, m, &a);
  for (unsigned i = 1; i < a.instruction_count; i++) {
    const ir_instruction *first = &a.instructions[i - 1], *second = &a.instructions[i];
    if (first->block != second->block || m->lines[first->line - 1].replacement ||
        m->lines[second->line - 1].replacement)
      continue;
    uint64_t bytes0, bytes1;
    const ir_operand *a0 = plain_copy(&a, first, &bytes0);
    const ir_operand *a1 = plain_copy(&a, second, &bytes1);
    if (!a0 || !a1 || bytes0 != bytes1 || a0->kind != IR_OPERAND_LOCAL ||
        a0[1].kind != IR_OPERAND_LOCAL || a1[1].kind != IR_OPERAND_LOCAL ||
        a0->definition != a1[1].definition || a0->definition == a0[1].definition ||
        m->tokens[a0[1].token].replacement || m->tokens[a1[1].token].replacement)
      continue;
    const ir_definition *temporary = &a.definitions[a0->definition];
    const ir_definition *source = &a.definitions[a0[1].definition];
    // Both originals must be distinct addressable stack objects. The copy-out
    // destination must be another allocation or the caller's result pointer.
    if (a1->kind != IR_OPERAND_LOCAL || a1->definition == a0->definition ||
        a1->definition == a0[1].definition ||
        temporary->use_count != 2 || byte_buffer(&a, temporary) != bytes0 ||
        byte_buffer(&a, source) != bytes0)
      continue;
    const ir_definition *out = &a.definitions[a1->definition];
    if (!byte_buffer(&a, out)) {
      unsigned function = a.blocks[second->block].function;
      const ir_function *fn = &a.functions[function];
      const ir_line *header = &m->lines[fn->line - 1];
      int sret = 0;
      if (out->instruction != IR_NONE || a1->definition != fn->first_definition)
        continue;
      for (unsigned k = header->first; k < header->first + header->count; k++) {
        const char *s = ir_text(m, k);
        if (!strcmp(s, "sret")) sret = 1;
        if (*s == '%') break;
      }
      if (!sret) continue;
    }
    m->lines[a.instructions[temporary->instruction].line - 1].replacement = "";
    m->lines[first->line - 1].replacement = "";
    m->tokens[a1[1].token].replacement = source->name;
    ir_note(c, "transformed", "private_copy_chain", 1);
  }
  ir_reparse(c, m, mark);
}

typedef struct {
  const ir_operand *pointer;
  unsigned lane, words;
} record_use;

static unsigned i32_words(const ir_analysis *a, unsigned type) {
  const ir_type *t = &a->types[type];
  if (t->kind == IR_INT && t->bits == 32)
    return 1;
  return t->kind == IR_ARRAY && t->count >= 2 && t->count <= 4 &&
                 a->types[t->element].kind == IR_INT &&
                 a->types[t->element].bits == 32
             ? t->count : 0;
}

static int record_access(ir_analysis *a, unsigned use, unsigned lane,
                         unsigned lanes, record_use *out) {
  const ir_operand *p = &a->operands[use];
  const ir_instruction *in = &a->instructions[p->instruction];
  const ir_line *line = &a->text->lines[in->line - 1];
  if (in->line != in->end_line || line->replacement)
    return 0;
  unsigned words = 0;
  if (in->opcode == IR_OP_CALL && !lane && in->callee != IR_NONE &&
      in->operand_count == 5 && use == in->first_operand + 1 &&
      !strcmp(ir_text(a->text, a->operands[in->callee].token),
              "@llvm.memset.inline.p0.i32")) {
    uint64_t n;
    if (strcmp(ir_text(a->text, (p + 1)->token), "0") ||
        strcmp(ir_text(a->text, (p + 3)->token), "false") ||
        !ir_integer(ir_text(a->text, (p + 2)->token), &n) || n != 4 * lanes)
      return 0;
    words = lanes;
  } else {
    int store = in->opcode == IR_OP_STORE;
    if ((!store && in->opcode != IR_OP_LOAD) ||
        in->effects != ((store ? IR_EFFECT_WRITE : IR_EFFECT_READ) | IR_EFFECT_TRAP) ||
        use != in->first_operand + (unsigned)store ||
        p->token + 4 != line->first + line->count ||
        strcmp(ir_text(a->text, p->token + 1), ",") ||
        strcmp(ir_text(a->text, p->token + 2), "align") ||
        strcmp(ir_text(a->text, p->token + 3), "4"))
      return 0;
    words = i32_words(a, store ? (p - 1)->type : in->type);
    if (!words || words > lanes - lane)
      return 0;
    if (store && words > 1) {
      const char *v = ir_text(a->text, (p - 1)->token);
      if (*v != '%' && strcmp(v, "zeroinitializer") && strcmp(v, "poison") &&
          strcmp(v, "undef"))
        return 0;
    }
  }
  *out = (record_use){p, lane, words};
  return 1;
}

void ir_small_aggregate(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  ir_analysis a;
  ir_analyze(c, m, &a);
  int fills = remove_overwritten_fills(c, &a);
  int packed = pack_byte_pairs(c, &a);
  if (fills || packed) {
    ir_function_admission(c, m, &a, BADGE_SELECTION_THUMB_CONSTANTS);
    int admitted = 0, declared = 0;
    for (unsigned i = 0; i < m->line_count; ++i)
      admitted |= m->lines[i].replacement &&
                  strstr(m->lines[i].replacement, "call i16 @llvm.bswap.i16(") != NULL;
    for (unsigned i = 0; i < a.global_count; ++i) {
      const char *name = a.globals[i].name;
      if (!strcmp(name, "@llvm.bswap.i16") || !strcmp(name, "@\"llvm.bswap.i16\""))
        declared = 1;
    }
    if (admitted && !declared)
      m->suffix = "declare i16 @llvm.bswap.i16(i16)\n";
    ir_reparse(c, m, mark);
    ir_analyze(c, m, &a);
  }
  unsigned changed = 0;
  for (unsigned i = 0; i < a.instruction_count; i++) {
    const ir_instruction *allocation = &a.instructions[i];
    if (allocation->opcode != IR_OP_ALLOCA)
      continue;
    const ir_definition *slot = &a.definitions[allocation->definition];
    unsigned size = byte_buffer(&a, slot);
    ir_line *decl = &m->lines[allocation->line - 1];
    if (size < 8 || size > 16 || size % 4 ||
        allocation->block != a.functions[a.blocks[allocation->block].function].first_block ||
        result_buffer_align(m, decl->op, decl->first + decl->count) < 4)
      continue;
    unsigned lanes = size / 4, use_count = 0, gep_count = 0;
    record_use uses[128];
    unsigned geps[64];
    int valid = 1;
    for (unsigned u = slot->first_use; u != IR_NONE && valid;
         u = a.operands[u].next_use) {
      const ir_instruction *in = &a.instructions[a.operands[u].instruction];
      if (in->opcode != IR_OP_GEP) {
        if (use_count == 128 || !record_access(&a, u, 0, lanes, &uses[use_count]))
          valid = 0;
        else
          use_count++;
        continue;
      }
      const ir_line *line = &m->lines[in->line - 1];
      uint64_t offset;
      if (gep_count == 64 || in->end_line != in->line || line->replacement ||
          line->first + line->count != line->op + 8 ||
          strcmp(ir_text(m, line->op + 1), "i8") || in->operand_count != 2 ||
          u != in->first_operand ||
          !ir_integer(ir_text(m, a.operands[u + 1].token), &offset) ||
          offset >= size || offset % 4) {
        valid = 0;
        break;
      }
      geps[gep_count++] = in->line;
      for (unsigned v = a.definitions[in->definition].first_use; v != IR_NONE;
           v = a.operands[v].next_use) {
        if (use_count == 128 || !record_access(&a, v, (unsigned)offset / 4,
                                               lanes, &uses[use_count])) {
          valid = 0;
          break;
        }
        use_count++;
      }
    }
    if (!valid || !use_count)
      continue;
    size_t start = c->size;
    const char *fields[4];
    for (unsigned k = 0; k < lanes; k++) {
      fields[k] = ir_fresh(c, m, '%');
      ir_emit(c, "  %s = alloca i32, align 4\n", fields[k]);
    }
    decl->replacement = ir_copy(c, c->output + start, c->size - start);
    c->size = start;
    for (unsigned g = 0; g < gep_count; g++)
      m->lines[geps[g] - 1].replacement = "";
    for (unsigned j = 0; j < use_count; j++) {
      record_use u = uses[j];
      const ir_instruction *in = &a.instructions[u.pointer->instruction];
      if (in->opcode != IR_OP_CALL && u.words == 1) {
        m->tokens[u.pointer->token].replacement = fields[u.lane];
        continue;
      }
      const char *previous = "poison";
      for (unsigned k = 0; k < u.words; k++) {
        const char *address = fields[u.lane + k];
        if (in->opcode == IR_OP_CALL)
          ir_emit(c, "  store i32 0, ptr %s, align 4\n", address);
        else if (in->opcode == IR_OP_STORE) {
          const char *part = ir_fresh(c, m, '%');
          ir_emit(c, "  %s = extractvalue [%u x i32] %s, %u\n"
                     "  store i32 %s, ptr %s, align 4\n",
                  part, u.words, ir_text(m, (u.pointer - 1)->token), k, part, address);
        } else {
          const char *part = ir_fresh(c, m, '%');
          const char *next = k + 1 == u.words ? a.definitions[in->definition].name
                                             : ir_fresh(c, m, '%');
          ir_emit(c, "  %s = load i32, ptr %s, align 4\n"
                     "  %s = insertvalue [%u x i32] %s, i32 %s, %u\n",
                  part, address, next, u.words, previous, part, k);
          previous = next;
        }
      }
      m->lines[in->line - 1].replacement = ir_copy(c, c->output + start, c->size - start);
      c->size = start;
    }
    changed++;
    ir_note(c, "transformed", "private_word_record", 1);
  }
  ir_reparse(c, m, mark);
  if (changed)
    ir_mem2reg(c, m);
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
  ir_copy_chains(c, m);
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
