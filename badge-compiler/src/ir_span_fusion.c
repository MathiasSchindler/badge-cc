#include "ir_analysis.h"
#include "ir_loop_admission.h"
#include "target.h"
#include <inttypes.h>
#include <string.h>

typedef struct {
  unsigned head, entry, body, latch, exit;
  const ir_instruction *index;
  const ir_operand *limit;
} span_loop;
typedef struct {
  const ir_instruction *access, *address, *offset;
  const ir_operand *index;
} span_access;

static const ir_operand *inputs(const ir_analysis *a, const ir_instruction *i) {
  return &a->operands[i->first_operand];
}
static const ir_instruction *definition(const ir_analysis *a, const ir_operand *o) {
  if (o->kind != IR_OPERAND_LOCAL)
    return NULL;
  unsigned d = a->definitions[o->definition].instruction;
  return d != IR_NONE ? &a->instructions[d] : NULL;
}
static int literal(const ir_analysis *a, const ir_operand *o, uint64_t *value) {
  return o->kind == IR_OPERAND_LITERAL && ir_integer(ir_text(a->text, o->token), value);
}
static int number(const ir_analysis *a, const ir_operand *o, uint64_t expected) {
  uint64_t v;
  return literal(a, o, &v) && v == expected;
}
static int matches(const ir_operand *o, const ir_instruction *i) {
  return o->kind == IR_OPERAND_LOCAL && o->definition == i->definition;
}
static int same(const ir_analysis *a, const ir_operand *l, const ir_operand *r) {
  uint64_t n;
  return (l->kind == IR_OPERAND_LOCAL && r->kind == IR_OPERAND_LOCAL &&
          l->definition == r->definition) || (literal(a, l, &n) && number(a, r, n));
}
static int once(const ir_analysis *a, const ir_instruction *i) {
  return a->definitions[i->definition].use_count == 1;
}
static const char *name(const ir_analysis *a, const ir_instruction *i) {
  return a->definitions[i->definition].name;
}
static int binary(const ir_analysis *a, const ir_instruction *i, const char *op) {
  return i && i->opcode == IR_OP_BINARY && !strcmp(i->operation, op) &&
         a->types[i->type].bits == 32 && i->end_line == i->line &&
         a->text->lines[i->line - 1].count == 7;
}
static const ir_instruction *first(const ir_analysis *a, unsigned block) {
  return &a->instructions[a->blocks[block].first_instruction];
}
static const ir_instruction *last(const ir_analysis *a, unsigned block) {
  return first(a, block) + a->blocks[block].instruction_count - 1;
}
static int predecessor(const ir_analysis *a, unsigned block, unsigned from) {
  return a->blocks[block].predecessor_count == 1 &&
         a->predecessors[a->blocks[block].first_predecessor] == from;
}
static int jump(const ir_analysis *a, unsigned block, unsigned to) {
  const ir_instruction *i = last(a, block);
  return i->opcode == IR_OP_BR && i->operand_count == 1 && inputs(a, i)->block == to;
}
static const ir_instruction *test(const ir_analysis *a, unsigned block) {
  const ir_instruction *br = last(a, block);
  const ir_instruction *cmp = br->opcode == IR_OP_BR && br->operand_count == 3
                                  ? definition(a, inputs(a, br)) : NULL;
  return cmp && cmp->opcode == IR_OP_COMPARE && cmp->block == block &&
         a->types[inputs(a, cmp)->type].bits == 32 &&
         !strcmp(ir_text(a->text, a->text->lines[cmp->line - 1].op + 1), "ult")
             ? cmp : NULL;
}
static const ir_operand *incoming(const ir_analysis *a, const ir_instruction *phi,
                                  unsigned block) {
  if (!phi || phi->opcode != IR_OP_PHI || phi->operand_count != 4 ||
      a->types[phi->type].bits != 32)
    return NULL;
  const ir_operand *o = inputs(a, phi);
  return o[1].block == block ? o : o[3].block == block ? o + 2 : NULL;
}
static int incremented(const ir_analysis *a, const ir_operand *o,
                       const ir_instruction *index, unsigned amount) {
  const ir_instruction *add = definition(a, o);
  return binary(a, add, "add") && matches(inputs(a, add), index) &&
         number(a, inputs(a, add) + 1, amount);
}

static int loop_shape(const ir_analysis *a, unsigned head, unsigned step, span_loop *out) {
  const ir_instruction *cmp = test(a, head);
  if (!cmp || a->blocks[head].predecessor_count != 2)
    return 0;
  const ir_operand *o = inputs(a, cmp), *br = inputs(a, last(a, head));
  const ir_instruction *index = definition(a, o);
  if (step == 2) {
    if (!binary(a, index, "add") || !number(a, inputs(a, index) + 1, 1))
      return 0;
    index = definition(a, inputs(a, index));
  }
  if (!index || index->opcode != IR_OP_PHI || index->block != head)
    return 0;
  unsigned body = br[1].block, exit = br[2].block;
  if (body == head || body == exit || !predecessor(a, body, head))
    return 0;
  const ir_instruction *back = last(a, body);
  if (back->opcode != IR_OP_BR || back->operand_count != 1)
    return 0;
  unsigned latch = inputs(a, back)->block;
  if (latch == head)
    latch = body;
  else if (latch == exit || !predecessor(a, latch, body) || !jump(a, latch, head))
    return 0;
  unsigned at = a->blocks[head].first_predecessor;
  unsigned entry = a->predecessors[at];
  if (entry == latch) entry = a->predecessors[at + 1];
  if (entry == head || entry == body || entry == latch || entry == exit)
    return 0;
  const ir_operand *start = incoming(a, index, entry), *next = incoming(a, index, latch);
  if (!start || !next || !number(a, start, 0) || !incremented(a, next, index, step))
    return 0;
  *out = (span_loop){head, entry, body, latch, exit, index, o + 1};
  return 1;
}

static int bounded_count(const ir_analysis *a, const ir_operand *o, unsigned capacity) {
  uint64_t value;
  if (literal(a, o, &value))
    return value <= capacity;
  const ir_instruction *expr = definition(a, o);
  uint64_t extra = 0;
  if (binary(a, expr, "add")) {
    if (!literal(a, inputs(a, expr) + 1, &extra) || extra > capacity)
      return 0;
    expr = definition(a, inputs(a, expr));
  }
  return binary(a, expr, "and") && literal(a, inputs(a, expr) + 1, &value) &&
         value <= capacity - extra;
}

static int access_index(const ir_analysis *a, const ir_instruction *address, unsigned width,
                        span_access *access) {
  if (!address || address->opcode != IR_OP_GEP || address->operand_count != 2 ||
      !once(a, address) || a->text->lines[address->line - 1].count != 10 ||
      strcmp(ir_text(a->text, a->text->lines[address->line - 1].op + 1), "i8"))
    return 0;
  access->address = address;
  const ir_operand *o = inputs(a, address) + 1;
  if (a->types[o->type].bits != 32)
    return 0;
  if (width > 1) {
    access->offset = definition(a, o);
    if (!binary(a, access->offset, "mul") || !number(a, inputs(a, access->offset) + 1, width))
      return 0;
    o = inputs(a, access->offset);
  }
  access->index = o;
  return 1;
}

static int private_span(const ir_analysis *a, const ir_instruction *object,
                        const span_loop *write, const span_access *read, unsigned width,
                        unsigned tail, span_access stores[3]) {
  unsigned count = tail == IR_NONE ? 1 : 3;
  if (a->definitions[object->definition].use_count != count + 1)
    return 0;
  for (unsigned u = a->definitions[object->definition].first_use; u != IR_NONE;
       u = a->operands[u].next_use) {
    const ir_instruction *addr = &a->instructions[a->operands[u].instruction];
    if (addr == read->address)
      continue;
    span_access st = {0};
    if (!access_index(a, addr, width, &st) || inputs(a, addr) != &a->operands[u])
      return 0;
    unsigned use = a->definitions[addr->definition].first_use;
    st.access = &a->instructions[a->operands[use].instruction];
    if (st.access->opcode != IR_OP_STORE || st.access->effects != (IR_EFFECT_WRITE | IR_EFFECT_TRAP) ||
        st.access->end_line != st.access->line || inputs(a, st.access) + 1 != &a->operands[use] ||
        a->types[inputs(a, st.access)->type].bits != width * 8)
      return 0;
    unsigned slot;
    if (st.access->block == write->body && matches(st.index, write->index))
      slot = 0;
    else if (count == 3 && st.access->block == write->body &&
             incremented(a, st.index, write->index, 1))
      slot = 1;
    else if (count == 3 && st.access->block == tail && matches(st.index, write->index))
      slot = 2;
    else
      return 0;
    if (stores[slot].access)
      return 0;
    stores[slot] = st;
  }
  return stores[0].access && (count == 1 ||
         (stores[1].access && stores[2].access && stores[0].access < stores[1].access));
}

static int removed_block(const span_loop *read, unsigned b) {
  return b == read->head || b == read->body || b == read->latch || b == read->exit;
}
static int closed_results(const ir_analysis *a, const span_loop *read) {
  const unsigned blocks[] = {read->head, read->body, read->latch, read->exit};
  for (unsigned n = 0; n < sizeof(blocks) / sizeof(*blocks); n++)
    for (const ir_instruction *i = first(a, blocks[n]); i <= last(a, blocks[n]); i++)
      if (i->definition != IR_NONE)
        for (unsigned u = a->definitions[i->definition].first_use; u != IR_NONE;
             u = a->operands[u].next_use)
          if (!removed_block(read, a->instructions[a->operands[u].instruction].block))
            return 0;
  return 1;
}

static int local_results(const ir_analysis *a, const ir_instruction *begin,
                         const ir_instruction *end) {
  for (const ir_instruction *i = begin; i <= end; i++)
    if (i->definition != IR_NONE)
      for (unsigned u = a->definitions[i->definition].first_use; u != IR_NONE;
           u = a->operands[u].next_use) {
        const ir_instruction *use = &a->instructions[a->operands[u].instruction];
        if (use < begin || use > end)
          return 0;
      }
  return 1;
}

static int equal_lanes(const ir_analysis *a, const ir_instruction *index,
                       const ir_instruction *left, const ir_instruction *left_end,
                       const ir_instruction *right, const ir_instruction *right_end,
                       int following) {
  const char *old[24], *equivalent[24], *offset[8];
  unsigned count = 0, offsets = 0;
  const char *iv = name(a, index);
  while (left <= left_end && right <= right_end) {
    if (following && binary(a, right, "add") && matches(inputs(a, right), index) &&
        number(a, inputs(a, right) + 1, 1)) {
      if (offsets == sizeof(offset) / sizeof(*offset))
        return 0;
      offset[offsets++] = name(a, right++);
      continue;
    }
    const ir_line *l = &a->text->lines[left->line - 1], *r = &a->text->lines[right->line - 1];
    int defines = left->definition != IR_NONE;
    if (left->opcode != right->opcode || left->end_line != left->line ||
        right->end_line != right->line || l->count != r->count ||
        (left->effects & (IR_EFFECT_ALLOCATE | IR_EFFECT_OPAQUE)) ||
        (right->effects & (IR_EFFECT_ALLOCATE | IR_EFFECT_OPAQUE)) ||
        defines != (right->definition != IR_NONE))
      return 0;
    for (unsigned k = defines ? 1 : 0; k < l->count; k++) {
      const char *expects = ir_text(a->text, l->first + k);
      const char *actual = ir_text(a->text, r->first + k);
      if (following && *actual == '%') {
        if (!strcmp(actual, iv))
          return 0;
        for (unsigned u = 0; u < offsets; u++)
          if (!strcmp(actual, offset[u])) {
            actual = iv;
            break;
          }
      }
      if (*expects == '%')
        for (unsigned u = 0; u < count; u++)
          if (!strcmp(expects, old[u])) {
            expects = equivalent[u];
            break;
          }
      if (strcmp(expects, actual))
        return 0;
    }
    if (defines) {
      if (count == sizeof(old) / sizeof(*old))
        return 0;
      old[count] = name(a, left);
      equivalent[count++] = name(a, right);
    }
    left++;
    right++;
  }
  return left > left_end && right > right_end;
}

static void remove_block(ir_analysis *a, unsigned block) {
  for (unsigned line = a->blocks[block].line; line <= last(a, block)->end_line; line++)
    a->text->lines[line - 1].replacement = "";
}

static int reroll(ir_context *c, ir_analysis *a, const span_loop *write,
                  const span_access stores[3], unsigned guard, unsigned tail,
                  unsigned exit) {
  const ir_instruction *inc = first(a, write->head) + 1;
  const ir_instruction *cmp = test(a, write->head);
  if (write->body == write->latch || a->blocks[write->head].instruction_count != 4 ||
      first(a, write->head) != write->index || definition(a, inputs(a, cmp)) != inc ||
      !once(a, inc) || a->blocks[write->latch].instruction_count != 2 ||
      stores[1].access + 1 != last(a, write->body) ||
      stores[2].access + 1 != last(a, tail))
    return 0;
  const ir_instruction *one = first(a, write->body);
  const ir_instruction *two = stores[0].access + 1;
  const ir_instruction *extra = first(a, tail);
  if (stores[0].access - one > 23 || stores[1].access - two > 31 ||
      stores[2].access - extra > 23 ||
      !local_results(a, two, stores[1].access) ||
      !local_results(a, extra, stores[2].access) ||
      !local_results(a, first(a, guard), last(a, guard)) ||
      !equal_lanes(a, write->index, one, stores[0].access, two, stores[1].access, 1) ||
      !equal_lanes(a, write->index, one, stores[0].access, extra, stores[2].access, 0))
    return 0;
  const ir_instruction *step = definition(a, incoming(a, write->index, write->latch));
  if (step != first(a, write->latch))
    return 0;
  ir_module *m = a->text;
  m->lines[inc->line - 1].replacement = "";
  m->lines[cmp->line - 1].replacement = ir_format(c,
      "  %s = icmp ult i32 %s, %s\n", name(a, cmp), name(a, write->index),
      ir_text(m, write->limit->token));
  m->lines[last(a, write->head)->line - 1].replacement = ir_format(c,
      "  br i1 %s, label %%%s, label %%%s\n", name(a, cmp),
      a->blocks[write->body].name, a->blocks[exit].name);
  m->lines[step->line - 1].replacement = ir_format(c,
      "  %s = add i32 %s, 1\n", name(a, step), name(a, write->index));
  for (unsigned line = two->line; line <= stores[1].access->end_line; line++)
    m->lines[line - 1].replacement = "";
  remove_block(a, guard);
  remove_block(a, tail);
  ir_note(c, "proposed", "equivalent_span_producer_lanes", 1);
  return 1;
}

static void replace_store(ir_context *c, ir_analysis *a, const span_access *store,
                          const char *operation, unsigned width, uint32_t factor,
                          const char *acc, const char *next) {
  ir_module *m = a->text;
  const char *value = ir_text(m, inputs(a, store->access)->token);
  const char *ext = width < 4 ? ir_fresh(c, m, '%') : value;
  const char *scale = ir_fresh(c, m, '%');
  const char *prefix = width < 4 ? ir_format(c, "  %s = zext i%u %s to i32\n",
                                             ext, width * 8, value) : "";
  m->lines[store->access->line - 1].replacement = ir_format(c,
      "%s  %s = mul i32 %s, %" PRIu32 "\n  %s = %s i32 %s, %s\n",
      prefix, scale, acc, factor, next, operation, scale, ext);
  m->lines[store->address->line - 1].replacement = "";
  if (store->offset && once(a, store->offset))
    m->lines[store->offset->line - 1].replacement = "";
}

static int fuse(ir_context *c, ir_analysis *a, unsigned header) {
  span_loop read, write;
  if (!loop_shape(a, header, 1, &read) ||
      a->blocks[header].instruction_count != 4 ||
      !predecessor(a, read.exit, header) || a->blocks[read.exit].instruction_count != 1 ||
      a->blocks[read.entry].instruction_count != 1 || !jump(a, read.entry, header))
    return 0;
  const ir_instruction *ret = last(a, read.exit);
  if (ret->opcode != IR_OP_RET || ret->operand_count != 1)
    return 0;
  const ir_instruction *carry = definition(a, inputs(a, ret));
  if (!carry || carry == read.index || carry->block != header)
    return 0;
  const ir_operand *start = incoming(a, carry, read.entry), *back = incoming(a, carry, read.latch);
  uint64_t seed, factor;
  if (!start || !back || !literal(a, start, &seed))
    return 0;
  const ir_instruction *update = definition(a, back);
  if ((!binary(a, update, "xor") && !binary(a, update, "add")) ||
      update->block != read.body)
    return 0;
  const ir_operand *combine = inputs(a, update);
  const ir_instruction *scale = definition(a, combine);
  if (!binary(a, scale, "mul") || !matches(inputs(a, scale), carry) ||
      !literal(a, inputs(a, scale) + 1, &factor))
    return 0;
  const ir_instruction *extend = definition(a, combine + 1), *load = extend;
  unsigned extended = 0;
  if (extend && extend->opcode == IR_OP_CAST && !strcmp(extend->operation, "zext")) {
    extended = 1;
    load = definition(a, inputs(a, extend));
  }
  if (!load || load->opcode != IR_OP_LOAD || load->block != read.body ||
      load->effects != (IR_EFFECT_READ | IR_EFFECT_TRAP))
    return 0;
  unsigned bits = a->types[load->type].bits, width = bits / 8;
  if (!(bits == 32 ? !extended : extended && (bits == 8 || bits == 16)))
    return 0;
  span_access access = {.access = load};
  if (!access_index(a, definition(a, inputs(a, load)), width, &access) ||
      !matches(access.index, read.index))
    return 0;
  const ir_instruction *object = definition(a, inputs(a, access.address));
  if (!object || object->opcode != IR_OP_ALLOCA || object->operand_count)
    return 0;
  const ir_type *array = &a->types[object->allocated_type];
  if (array->kind != IR_ARRAY || a->types[array->element].bits != 8 ||
      array->count > 1024 || !bounded_count(a, read.limit, array->count / width))
    return 0;

  const ir_instruction *nodes[] = {update, scale, load, access.address, access.offset,
                                   extended ? extend : NULL};
  unsigned body_count = 1 + (read.body == read.latch);
  for (unsigned n = 0; n < sizeof(nodes) / sizeof(*nodes); n++) {
    if (!nodes[n]) continue;
    if (nodes[n]->block != read.body)
      return 0;
    body_count++;
  }
  const ir_operand *step = incoming(a, read.index, read.latch);
  if (definition(a, step)->block != read.latch ||
      (read.body != read.latch && a->blocks[read.latch].instruction_count != 2) ||
      a->blocks[read.body].instruction_count != body_count || !closed_results(a, &read))
    return 0;

  unsigned guard = IR_NONE, tail = IR_NONE, head;
  const ir_block *entry = &a->blocks[read.entry];
  if (entry->predecessor_count == 1) {
    head = a->predecessors[entry->first_predecessor];
    if (!loop_shape(a, head, 1, &write) || write.exit != read.entry)
      return 0;
  } else if (entry->predecessor_count == 2) {
    guard = a->predecessors[entry->first_predecessor];
    tail = a->predecessors[entry->first_predecessor + 1];
    if (jump(a, guard, read.entry)) {
      unsigned swap = guard;
      guard = tail;
      tail = swap;
    }
    const ir_instruction *cmp = test(a, guard);
    if (!cmp || !jump(a, tail, read.entry) || !predecessor(a, tail, guard) ||
        a->blocks[guard].predecessor_count != 1 ||
        a->blocks[guard].instruction_count != 2)
      return 0;
    head = a->predecessors[a->blocks[guard].first_predecessor];
    const ir_operand *o = inputs(a, last(a, guard));
    if (o[1].block != tail || o[2].block != read.entry ||
        !loop_shape(a, head, 2, &write) || write.exit != guard ||
        !matches(inputs(a, cmp), write.index) || !same(a, inputs(a, cmp) + 1, read.limit))
      return 0;
  } else
    return 0;
  if (!same(a, read.limit, write.limit) || removed_block(&read, write.head) ||
      removed_block(&read, write.body) || removed_block(&read, write.latch) ||
      removed_block(&read, write.entry) || (tail != IR_NONE &&
      (removed_block(&read, tail) || removed_block(&read, guard))))
    return 0;
  if (write.limit->kind == IR_OPERAND_LOCAL) {
    const ir_instruction *limit = definition(a, write.limit);
    if (!limit || !ir_dominates(a, limit->block, write.entry))
      return 0;
  }
  span_access stores[3] = {0};
  if (!private_span(a, object, &write, &access, width, tail, stores))
    return 0;

  c->line = object->line;
  if (tail != IR_NONE && reroll(c, a, &write, stores, guard, tail, read.entry))
    tail = IR_NONE;
  ir_module *m = a->text;
  const char *acc = ir_fresh(c, m, '%'), *one = ir_fresh(c, m, '%');
  const char *two = tail != IR_NONE ? ir_fresh(c, m, '%') : one;
  ir_line *phi = &m->lines[first(a, write.head)->line - 1];
  phi->replacement = ir_format(c, "  %s = phi i32 [%" PRIu32 ", %%%s], [%s, %%%s]\n%s",
      acc, (uint32_t)seed, a->blocks[write.entry].name, two, a->blocks[write.latch].name,
      ir_copy(c, m->source + phi->begin, phi->end - phi->begin));
  replace_store(c, a, &stores[0], update->operation, width, (uint32_t)factor, acc, one);
  char *exit = ir_format(c, "  ret i32 %s\n", acc);
  if (tail != IR_NONE) {
    const char *extra = ir_fresh(c, m, '%'), *result = ir_fresh(c, m, '%');
    replace_store(c, a, &stores[1], update->operation, width, (uint32_t)factor, one, two);
    replace_store(c, a, &stores[2], update->operation, width, (uint32_t)factor, acc, extra);
    exit = ir_format(c, "  %s = phi i32 [%s, %%%s], [%s, %%%s]\n  ret i32 %s\n",
        result, acc, a->blocks[guard].name, extra, a->blocks[tail].name, result);
  }
  m->lines[last(a, read.entry)->line - 1].replacement = exit;
  m->lines[object->line - 1].replacement = "";
  const unsigned blocks[] = {read.head, read.body, read.latch, read.exit};
  for (unsigned n = 0; n < sizeof(blocks) / sizeof(*blocks); n++)
    remove_block(a, blocks[n]);
  ir_note(c, "proposed", "private_span_reduction_fusion", 1);
  return 1;
}

void ir_span_fusion(ir_context *c, ir_module *m) {
  if (!strstr(m->source, "alloca [") || !strstr(m->source, "icmp ult i32"))
    return;
  ir_allocation *mark = c->allocations;
  ir_analysis a;
  ir_analyze(c, m, &a);
  int changed = 0;
  for (unsigned f = 0; f < a.function_count; f++) {
    const ir_function *fn = &a.functions[f];
    if (fn->declaration || fn->block_count > 32 || fn->definition_count > 256)
      continue;
    for (unsigned b = fn->first_block; b < fn->first_block + fn->block_count; b++)
      if (fuse(c, &a, b)) {
        changed = 1;
        break;
      }
  }
  if (changed) {
    ir_function_admission(c, m, &a, BADGE_SELECTION_THUMB_CONSTANTS);
    ir_reparse(c, m, mark);
  } else
    ir_release_to(c, mark);
}
