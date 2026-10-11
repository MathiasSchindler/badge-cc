#include "ir_analysis.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ir_context *c;
  ir_analysis *a;
  unsigned p, end, function, instruction, depth;
  uint64_t work;
} reader;

static const char *text(reader *r) { return ir_text(r->a->text, r->p); }
static int accept(reader *r, const char *s) {
  if (r->p < r->end && !strcmp(text(r), s)) {
    r->p++;
    return 1;
  }
  return 0;
}
static void expect(reader *r, const char *s) {
  if (!accept(r, s))
    ir_error(r->c, ir_format(r->c, "expected '%s' in supported IR", s));
}
static unsigned number(reader *r) {
  uint64_t n;
  if (r->p >= r->end || !ir_integer(text(r), &n) || *text(r) == '-' ||
      n > UINT_MAX)
    ir_error(r->c, "expected bounded unsigned integer");
  r->p++;
  return (unsigned)n;
}
static void index_init(ir_context *c, ir_index *map, unsigned expected) {
  unsigned capacity = 8;
  if (expected > 1000000)
    ir_error(c, "IR name index limit exceeded");
  while (capacity < expected * 2u)
    capacity *= 2;
  map->capacity = capacity;
  map->entries = ir_allocate(c, (size_t)capacity * sizeof(*map->entries));
}
static unsigned index_get(const ir_index *map, const char *key) {
  if (!map->capacity)
    return IR_NONE;
  unsigned slot = ir_hash(key) & (map->capacity - 1);
  while (map->entries[slot].name) {
    if (!strcmp(map->entries[slot].name, key))
      return map->entries[slot].value;
    slot = (slot + 1) & (map->capacity - 1);
  }
  return IR_NONE;
}
static void index_put(ir_context *c, ir_index *map, const char *key,
                      unsigned index) {
  if (!map->capacity)
    ir_error(c, "uninitialized IR name index");
  unsigned slot = ir_hash(key) & (map->capacity - 1);
  while (map->entries[slot].name) {
    if (!strcmp(map->entries[slot].name, key))
      ir_error(c, ir_format(c, "duplicate IR name %s", key));
    slot = (slot + 1) & (map->capacity - 1);
  }
  if (map->count == map->capacity / 2)
    ir_error(c, "IR name index capacity exceeded");
  map->entries[slot] = (ir_index_entry){key, index};
  map->count++;
}
static unsigned intern(reader *r, ir_type type) {
  unsigned found = index_get(&r->a->type_index, type.key);
  if (found != IR_NONE)
    return found;
  if (r->a->type_count == r->a->capacity)
    ir_error(r->c, "IR type limit exceeded");
  unsigned id = r->a->type_count++;
  r->a->types[id] = type;
  index_put(r->c, &r->a->type_index, type.key, id);
  return id;
}
static unsigned scalar(reader *r, const char *name) {
  ir_type t = {.key = name};
  if (!strcmp(name, "void"))
    t.kind = IR_VOID;
  else if (!strcmp(name, "ptr"))
    t.kind = IR_PTR;
  else if ((t.bits = ir_bits(name)))
    t.kind = IR_INT;
  else
    ir_error(r->c, "unsupported IR scalar type");
  return intern(r, t);
}
static unsigned type(reader *r) {
  if (r->p == r->end || ++r->depth > 32)
    ir_error(r->c, "IR type nesting/length limit exceeded");
  unsigned id;
  int packed = accept(r, "<");
  if (packed && strcmp(text(r), "{"))
    ir_error(r->c, "unsupported vector type");
  if (accept(r, "[")) {
    unsigned n = number(r);
    expect(r, "x");
    unsigned element = type(r);
    if (r->a->types[element].kind == IR_VOID)
      ir_error(r->c, "void array element");
    expect(r, "]");
    ir_type t = {.kind = IR_ARRAY, .count = n, .element = element};
    t.key = ir_format(r->c, "[%u x %s]", n, r->a->types[element].key);
    id = intern(r, t);
  } else if (accept(r, "{")) {
    unsigned fields[256], n = 0;
    if (!accept(r, "}")) {
      do {
        if (n == 256)
          ir_error(r->c, "IR struct field limit exceeded");
        fields[n] = type(r);
        if (r->a->types[fields[n++]].kind == IR_VOID)
          ir_error(r->c, "void struct field");
      } while (accept(r, ","));
      expect(r, "}");
    }
    if (packed)
      expect(r, ">");
    const char *key = packed ? "<{" : "{";
    for (unsigned k = 0; k < n; k++)
      key = ir_format(r->c, "%s%s%s", key, k ? "," : "",
                      r->a->types[fields[k]].key);
    key = ir_format(r->c, "%s}%s", key, packed ? ">" : "");
    unsigned old = index_get(&r->a->type_index, key);
    if (old != IR_NONE)
      id = old;
    else {
      if (n > r->a->capacity - r->a->field_count)
        ir_error(r->c, "IR field limit exceeded");
      ir_type t = {.kind = IR_STRUCT,
                   .count = n,
                   .first_field = r->a->field_count,
                   .packed = (unsigned)packed,
                   .key = key};
      memcpy(r->a->fields + r->a->field_count, fields, n * sizeof(*fields));
      r->a->field_count += n;
      id = intern(r, t);
    }
  } else {
    id = scalar(r, text(r));
    r->p++;
  }
  r->depth--;
  return id;
}
int ir_type_equal(const ir_analysis *a, unsigned x, unsigned y) {
  return x < a->type_count && y < a->type_count && x == y;
}
static void same(reader *r, unsigned x, unsigned y) {
  if (!ir_type_equal(r->a, x, y))
    ir_error(r->c, "IR operand/result type mismatch");
}
static void kind(reader *r, unsigned t, ir_type_kind k) {
  if (r->a->types[t].kind != k)
    ir_error(r->c, "invalid instruction operand type");
}
static int type_start(const char *s) {
  return ir_bits(s) || !strcmp(s, "ptr") || !strcmp(s, "void") ||
         !strcmp(s, "[") || !strcmp(s, "{") || !strcmp(s, "<");
}
static void parameter_attributes(reader *r) {
  while (r->p < r->end) {
    const char *s = text(r);
    if (!strcmp(s, "zeroext") || !strcmp(s, "signext") ||
        !strcmp(s, "noundef") || !strcmp(s, "nonnull") ||
        !strcmp(s, "nocapture") || !strcmp(s, "readonly") ||
        !strcmp(s, "writeonly") || !strcmp(s, "noalias") ||
        !strcmp(s, "immarg") || !strcmp(s, "returned") || !strcmp(s, "inreg"))
      r->p++;
    else if (accept(r, "align")) {
      unsigned n = number(r);
      if (!n || (n & (n - 1)))
        ir_error(r->c, "invalid parameter alignment");
    } else if (accept(r, "sret") || accept(r, "byval") || accept(r, "byref") ||
               accept(r, "elementtype")) {
      expect(r, "(");
      type(r);
      expect(r, ")");
    } else if (accept(r, "dereferenceable") ||
               accept(r, "dereferenceable_or_null")) {
      expect(r, "(");
      number(r);
      expect(r, ")");
    } else
      break;
  }
}
static unsigned definition(reader *r, const char *name, unsigned t,
                           unsigned block, unsigned instruction) {
  ir_function *f = &r->a->functions[r->function];
  unsigned id = r->a->definition_count++;
  if (id >= r->a->capacity)
    ir_error(r->c, "IR definition limit exceeded");
  const char *key = ir_name_key(r->c, name);
  if (index_get(&f->labels, key) != IR_NONE)
    ir_error(r->c, "SSA name collides with block label");
  index_put(r->c, &f->locals, key, id);
  r->a->definitions[id] =
      (ir_definition){name, t, instruction, block, IR_NONE, 0};
  f->definition_count++;
  return id;
}
static unsigned operand(reader *r, unsigned t, ir_operand_kind forced) {
  if (r->p == r->end)
    ir_error(r->c, "missing IR operand");
  unsigned token = r->p++, id = r->a->operand_count++;
  if (id >= r->a->capacity)
    ir_error(r->c, "IR operand limit exceeded");
  const char *s = ir_text(r->a->text, token);
  ir_operand_kind k = forced;
  if (forced != IR_OPERAND_BLOCK) {
    if (*s == '%')
      k = IR_OPERAND_LOCAL;
    else if (*s == '@') {
      k = IR_OPERAND_GLOBAL;
      kind(r, t, IR_PTR);
    } else {
      uint64_t n;
      k = IR_OPERAND_LITERAL;
      if (!strcmp(s, "poison") || !strcmp(s, "undef") ||
          !strcmp(s, "zeroinitializer")) {
        if (r->a->types[t].kind == IR_VOID)
          ir_error(r->c, "void literal");
      } else if (!strcmp(s, "null"))
        kind(r, t, IR_PTR);
      else if (ir_integer(s, &n)) {
        kind(r, t, IR_INT);
        unsigned bits = r->a->types[t].bits;
        if (bits < 64) {
          uint64_t mask = (UINT64_C(1) << bits) - 1;
          if (*s == '-') {
            if ((uint64_t)(0 - n) > (UINT64_C(1) << (bits - 1)))
              ir_error(r->c, "integer constant outside type range");
          } else if (n > mask)
            ir_error(r->c, "integer constant outside type range");
        }
      } else
        ir_error(r->c, "unsupported IR value/constant expression");
    }
  } else if (*s != '%')
    ir_error(r->c, "expected block reference");
  r->a->operands[id] =
      (ir_operand){k, t, token, IR_NONE, IR_NONE, IR_NONE, r->instruction};
  r->a->instructions[r->instruction].operand_count++;
  return id;
}
static unsigned typed_operand(reader *r) {
  unsigned t = type(r);
  parameter_attributes(r);
  operand(r, t, IR_OPERAND_LITERAL);
  return t;
}
static void label_operand(reader *r) {
  expect(r, "label");
  operand(r, IR_NONE, IR_OPERAND_BLOCK);
}
static void integer_binary(reader *r, ir_instruction *in) {
  unsigned flags = 0;
  for (;;) {
    unsigned bit = 0;
    if (accept(r, "nsw"))
      bit = 1;
    else if (accept(r, "nuw"))
      bit = 2;
    else if (accept(r, "exact"))
      bit = 4;
    else if (accept(r, "disjoint"))
      bit = 8;
    else
      break;
    int arithmetic =
        !strcmp(in->operation, "add") || !strcmp(in->operation, "sub") ||
        !strcmp(in->operation, "mul") || !strcmp(in->operation, "shl");
    int exact =
        !strcmp(in->operation, "udiv") || !strcmp(in->operation, "sdiv") ||
        !strcmp(in->operation, "lshr") || !strcmp(in->operation, "ashr");
    if ((flags & bit) || ((bit & 3) && !arithmetic) || (bit == 4 && !exact) ||
        (bit == 8 && strcmp(in->operation, "or")))
      ir_error(r->c, "invalid integer instruction flag");
    flags |= bit;
  }
  in->type = type(r);
  kind(r, in->type, IR_INT);
  operand(r, in->type, IR_OPERAND_LITERAL);
  expect(r, ",");
  operand(r, in->type, IR_OPERAND_LITERAL);
  if (strstr(in->operation, "div") || strstr(in->operation, "rem"))
    in->effects = IR_EFFECT_TRAP;
}
static int one_of(const char *s, const char *const *choices) {
  for (; *choices; choices++)
    if (!strcmp(s, *choices))
      return 1;
  return 0;
}
static unsigned aggregate_index(reader *r, unsigned t) {
  unsigned n = number(r);
  ir_type *a = &r->a->types[t];
  if ((a->kind != IR_ARRAY && a->kind != IR_STRUCT) || n >= a->count)
    ir_error(r->c, "aggregate index outside type");
  return a->kind == IR_ARRAY ? a->element : r->a->fields[a->first_field + n];
}
static void suffix(reader *r, ir_instruction *in) {
  while (r->p < r->end) {
    if (accept(r, ",")) {
      if (accept(r, "align")) {
        if (in->opcode != IR_OP_ALLOCA && in->opcode != IR_OP_LOAD &&
            in->opcode != IR_OP_STORE)
          ir_error(r->c, "alignment on nonmemory instruction");
        unsigned n = number(r);
        if (!n || (n & (n - 1)) || n > 65536)
          ir_error(r->c, "invalid memory alignment");
      } else if (*text(r) == '!') {
        in->effects |= IR_EFFECT_OPAQUE | IR_EFFECT_READ | IR_EFFECT_WRITE |
                       IR_EFFECT_TRAP;
        /* Metadata is retained verbatim, never a license to remove effects. */
        while (r->p < r->end) {
          if (*text(r) != '!')
            ir_error(r->c, "invalid metadata attachment name");
          r->p++;
          if (r->p == r->end || *text(r) != '!')
            ir_error(r->c, "invalid metadata attachment value");
          r->p++;
          if (r->p < r->end)
            expect(r, ",");
        }
      } else
        ir_error(r->c, "unsupported instruction suffix");
    } else if (in->opcode == IR_OP_CALL &&
               (*text(r) == '#' || *text(r) == '"' ||
                isalpha((unsigned char)*text(r)))) {
      in->effects |= IR_EFFECT_OPAQUE;
      r->p++;
      if (accept(r, "=")) {
        if (*text(r) != '"')
          ir_error(r->c, "unsupported string call attribute");
        r->p++;
      }
      if (accept(r, "(")) {
        unsigned depth = 1;
        while (r->p < r->end && depth) {
          if (*text(r) == '%' || *text(r) == '@')
            ir_error(r->c,
                     "opaque call attribute contains untracked reference");
          if (accept(r, "("))
            depth++;
          else if (accept(r, ")"))
            depth--;
          else
            r->p++;
        }
        if (depth)
          ir_error(r->c, "unterminated opaque call attribute");
      }
    } else
      ir_error(r->c, "unexpected trailing IR tokens");
  }
}
static void call(reader *r, ir_instruction *in) {
  parameter_attributes(r);
  in->type = type(r);
  /* Explicit call-site function types are retained through typed arguments. */
  if (accept(r, "(")) {
    in->explicit_signature = 1;
    in->first_call_parameter = r->a->parameter_count;
    while (!accept(r, ")")) {
      if (accept(r, "...")) {
        in->variadic_call = 1;
        expect(r, ")");
        break;
      }
      unsigned t = type(r);
      if (r->a->types[t].kind == IR_VOID)
        ir_error(r->c, "void call parameter");
      parameter_attributes(r);
      if (r->a->parameter_count == r->a->capacity)
        ir_error(r->c, "IR parameter limit exceeded");
      r->a->parameters[r->a->parameter_count++] = t;
      in->call_parameter_count++;
      if (!accept(r, ",")) {
        expect(r, ")");
        break;
      }
    }
  }
  if (accept(r, "asm")) {
    in->effects |= IR_EFFECT_OPAQUE;
    while (accept(r, "sideeffect") || accept(r, "alignstack") ||
           accept(r, "inteldialect")) {
    }
    if (*text(r) != '"')
      ir_error(r->c, "invalid inline assembly body");
    r->p++;
    expect(r, ",");
    if (*text(r) != '"')
      ir_error(r->c, "invalid inline assembly constraints");
    r->p++;
  } else
    in->callee = operand(r, scalar(r, "ptr"), IR_OPERAND_LITERAL);
  expect(r, "(");
  if (!accept(r, ")")) {
    do {
      typed_operand(r);
    } while (accept(r, ","));
    expect(r, ")");
  }
  if (in->explicit_signature) {
    unsigned arguments = in->operand_count - (in->callee != IR_NONE);
    if (arguments < in->call_parameter_count ||
        (!in->variadic_call && arguments != in->call_parameter_count))
      ir_error(r->c, "explicit call argument count mismatch");
    for (unsigned k = 0; k < in->call_parameter_count; k++)
      same(r,
           r->a->operands[in->first_operand + (in->callee != IR_NONE) + k].type,
           r->a->parameters[in->first_call_parameter + k]);
  }
}
static void instruction(reader *r, unsigned line, unsigned end_line,
                        unsigned block) {
  ir_analysis *a = r->a;
  unsigned id = a->instruction_count++;
  if (id >= a->capacity)
    ir_error(r->c, "IR instruction limit exceeded");
  r->instruction = id;
  ir_instruction *in = &a->instructions[id];
  *in = (ir_instruction){.line = line + 1,
                         .end_line = end_line + 1,
                         .block = block,
                         .type = scalar(r, "void"),
                         .allocated_type = IR_NONE,
                         .definition = IR_NONE,
                         .first_operand = a->operand_count,
                         .callee = IR_NONE};
  const char *result = NULL;
  if (*text(r) == '%') {
    result = text(r);
    r->p++;
    expect(r, "=");
  }
  if (accept(r, "tail") || accept(r, "musttail") || accept(r, "notail")) {
    in->effects |= IR_EFFECT_OPAQUE;
  }
  in->operation = text(r);
  r->p++;
  static const char *const binary[] = {"add",  "sub",  "mul", "udiv", "sdiv",
                                       "urem", "srem", "shl", "lshr", "ashr",
                                       "and",  "or",   "xor", NULL};
  static const char *const casts[] = {"trunc",    "zext",    "sext", "ptrtoint",
                                      "inttoptr", "bitcast", NULL};
  if (one_of(in->operation, binary)) {
    in->opcode = IR_OP_BINARY;
    integer_binary(r, in);
  } else if (!strcmp(in->operation, "icmp")) {
    in->opcode = IR_OP_COMPARE;
    static const char *const predicates[] = {"eq",  "ne",  "ugt", "uge",
                                             "ult", "ule", "sgt", "sge",
                                             "slt", "sle", NULL};
    if (!one_of(text(r), predicates))
      ir_error(r->c, "invalid integer comparison predicate");
    r->p++;
    unsigned t = type(r);
    if (a->types[t].kind != IR_INT && a->types[t].kind != IR_PTR)
      ir_error(r->c, "unsupported comparison type");
    operand(r, t, IR_OPERAND_LITERAL);
    expect(r, ",");
    operand(r, t, IR_OPERAND_LITERAL);
    in->type = scalar(r, "i1");
  } else if (one_of(in->operation, casts)) {
    in->opcode = IR_OP_CAST;
    unsigned from = typed_operand(r);
    expect(r, "to");
    in->type = type(r);
    ir_type *x = &a->types[from], *y = &a->types[in->type];
    if (!strcmp(in->operation, "inttoptr")) {
      kind(r, from, IR_INT);
      kind(r, in->type, IR_PTR);
    } else if (!strcmp(in->operation, "ptrtoint")) {
      kind(r, from, IR_PTR);
      kind(r, in->type, IR_INT);
    } else if (!strcmp(in->operation, "bitcast")) {
      if (!((x->kind == IR_PTR && y->kind == IR_PTR) ||
            (x->kind == IR_INT && y->kind == IR_INT && x->bits == y->bits)))
        ir_error(r->c, "unsupported bitcast type");
    } else {
      kind(r, from, IR_INT);
      kind(r, in->type, IR_INT);
      if (!strcmp(in->operation, "trunc") ? x->bits <= y->bits
                                          : x->bits >= y->bits)
        ir_error(r->c, "invalid integer cast widths");
    }
  } else if (!strcmp(in->operation, "freeze")) {
    in->opcode = IR_OP_FREEZE;
    in->type = typed_operand(r);
  } else if (!strcmp(in->operation, "select")) {
    in->opcode = IR_OP_SELECT;
    same(r, typed_operand(r), scalar(r, "i1"));
    expect(r, ",");
    in->type = typed_operand(r);
    expect(r, ",");
    same(r, typed_operand(r), in->type);
  } else if (!strcmp(in->operation, "alloca")) {
    in->opcode = IR_OP_ALLOCA;
    in->effects = IR_EFFECT_ALLOCATE;
    unsigned t = type(r);
    if (a->types[t].kind == IR_VOID)
      ir_error(r->c, "alloca void");
    in->allocated_type = t;
    in->type = scalar(r, "ptr");
    if (r->p + 1 < r->end && !strcmp(text(r), ",") &&
        type_start(ir_text(a->text, r->p + 1))) {
      r->p++;
      kind(r, typed_operand(r), IR_INT);
    }
  } else if (!strcmp(in->operation, "load") ||
             !strcmp(in->operation, "store")) {
    int store = !strcmp(in->operation, "store");
    in->opcode = store ? IR_OP_STORE : IR_OP_LOAD;
    in->effects = (store ? IR_EFFECT_WRITE : IR_EFFECT_READ) | IR_EFFECT_TRAP;
    if (accept(r, "volatile"))
      in->effects |= IR_EFFECT_VOLATILE;
    unsigned t = store ? typed_operand(r) : type(r);
    if (a->types[t].kind == IR_VOID)
      ir_error(r->c, "void memory access");
    expect(r, ",");
    kind(r, typed_operand(r), IR_PTR);
    if (!store)
      in->type = t;
  } else if (!strcmp(in->operation, "getelementptr")) {
    in->opcode = IR_OP_GEP;
    accept(r, "inbounds");
    unsigned element = type(r);
    if (a->types[element].kind == IR_VOID)
      ir_error(r->c, "void GEP element");
    expect(r, ",");
    kind(r, typed_operand(r), IR_PTR);
    unsigned count = 0;
    while (accept(r, ",")) {
      if (r->p == r->end || *text(r) == '!') {
        r->p--;
        break;
      }
      unsigned index_type = type(r);
      kind(r, index_type, IR_INT);
      if (count) {
        ir_type *t = &a->types[element];
        if (t->kind == IR_STRUCT) {
          same(r, index_type, scalar(r, "i32"));
          unsigned token = r->p;
          element = aggregate_index(r, element);
          r->p = token;
          operand(r, index_type, IR_OPERAND_LITERAL);
        } else if (t->kind == IR_ARRAY) {
          operand(r, index_type, IR_OPERAND_LITERAL);
          element = t->element;
        } else
          ir_error(r->c, "GEP indexes nonaggregate element");
      } else
        operand(r, index_type, IR_OPERAND_LITERAL);
      count++;
    }
    if (!count)
      ir_error(r->c, "missing GEP index");
    in->type = scalar(r, "ptr");
  } else if (!strcmp(in->operation, "extractvalue") ||
             !strcmp(in->operation, "insertvalue")) {
    int insert = !strcmp(in->operation, "insertvalue");
    in->opcode = insert ? IR_OP_INSERT : IR_OP_EXTRACT;
    unsigned aggregate = typed_operand(r), value_type = IR_NONE,
             selected = aggregate;
    expect(r, ",");
    if (insert) {
      value_type = typed_operand(r);
      expect(r, ",");
    }
    selected = aggregate_index(r, selected);
    while (r->p + 1 < r->end && !strcmp(text(r), ",") &&
           *ir_text(a->text, r->p + 1) != '!') {
      r->p++;
      selected = aggregate_index(r, selected);
    }
    if (insert) {
      same(r, value_type, selected);
      in->type = aggregate;
    } else
      in->type = selected;
  } else if (!strcmp(in->operation, "phi")) {
    in->opcode = IR_OP_PHI;
    in->type = type(r);
    do {
      expect(r, "[");
      operand(r, in->type, IR_OPERAND_LITERAL);
      expect(r, ",");
      operand(r, IR_NONE, IR_OPERAND_BLOCK);
      expect(r, "]");
    } while (r->p + 1 < r->end && !strcmp(text(r), ",") &&
             !strcmp(ir_text(a->text, r->p + 1), "[") && (++r->p, 1));
  } else if (!strcmp(in->operation, "call")) {
    in->opcode = IR_OP_CALL;
    in->effects |=
        IR_EFFECT_CALL | IR_EFFECT_READ | IR_EFFECT_WRITE | IR_EFFECT_TRAP;
    call(r, in);
  } else if (!strcmp(in->operation, "ret")) {
    in->opcode = IR_OP_RET;
    in->effects = IR_EFFECT_CONTROL;
    unsigned t = type(r);
    same(r, t, a->functions[r->function].return_type);
    if (a->types[t].kind != IR_VOID)
      operand(r, t, IR_OPERAND_LITERAL);
  } else if (!strcmp(in->operation, "br")) {
    in->opcode = IR_OP_BR;
    in->effects = IR_EFFECT_CONTROL;
    if (!strcmp(text(r), "label"))
      label_operand(r);
    else {
      same(r, typed_operand(r), scalar(r, "i1"));
      expect(r, ",");
      label_operand(r);
      expect(r, ",");
      label_operand(r);
    }
  } else if (!strcmp(in->operation, "switch")) {
    in->opcode = IR_OP_SWITCH;
    in->effects = IR_EFFECT_CONTROL;
    unsigned t = typed_operand(r);
    kind(r, t, IR_INT);
    expect(r, ",");
    label_operand(r);
    expect(r, "[");
    while (!accept(r, "]")) {
      same(r, type(r), t);
      unsigned v = operand(r, t, IR_OPERAND_LITERAL);
      if (a->operands[v].kind != IR_OPERAND_LITERAL)
        ir_error(r->c, "nonconstant switch case");
      uint64_t x;
      if (!ir_integer(ir_text(a->text, a->operands[v].token), &x))
        ir_error(r->c, "invalid switch constant");
      for (unsigned k = in->first_operand + 2; k < v; k += 2) {
        if (!r->work--)
          ir_error(r->c, "switch verification work limit exceeded");
        uint64_t y;
        ir_integer(ir_text(a->text, a->operands[k].token), &y);
        unsigned bits = a->types[t].bits;
        uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
        if ((x & mask) == (y & mask))
          ir_error(r->c, "duplicate switch case");
      }
      expect(r, ",");
      label_operand(r);
    }
  } else if (!strcmp(in->operation, "unreachable")) {
    in->opcode = IR_OP_UNREACHABLE;
    in->effects = IR_EFFECT_CONTROL | IR_EFFECT_TRAP;
  } else
    ir_error(r->c,
             ir_format(r->c, "unsupported instruction '%s'", in->operation));
  suffix(r, in);
  if (result) {
    if (a->types[in->type].kind == IR_VOID)
      ir_error(r->c, "void instruction has SSA result");
    in->definition = definition(r, result, in->type, block, id);
  } else if (a->types[in->type].kind != IR_VOID && in->opcode != IR_OP_CALL)
    ir_error(r->c, "value instruction requires SSA result");
  a->blocks[block].instruction_count++;
}

static void signature(reader *r, unsigned line) {
  ir_analysis *a = r->a;
  int declaration = accept(r, "declare");
  if (!declaration)
    expect(r, "define");
  while (r->p < r->end && !type_start(text(r)))
    r->p++;
  unsigned ret = type(r);
  const char *name = text(r);
  if (*name != '@')
    ir_error(r->c, "expected function name");
  r->p++;
  unsigned fn = a->function_count++;
  if (fn >= a->capacity)
    ir_error(r->c, "IR function limit exceeded");
  r->function = fn;
  ir_function *f = &a->functions[fn];
  *f = (ir_function){.name = name,
                     .line = line + 1,
                     .return_type = ret,
                     .first_parameter = a->parameter_count,
                     .declaration = declaration};
  expect(r, "(");
  if (!accept(r, ")")) {
    do {
      if (accept(r, "...")) {
        f->variadic = 1;
        break;
      }
      unsigned t = type(r);
      parameter_attributes(r);
      if (a->types[t].kind == IR_VOID)
        ir_error(r->c, "void parameter");
      if (a->parameter_count == a->capacity)
        ir_error(r->c, "IR parameter limit exceeded");
      a->parameters[a->parameter_count++] = t;
      f->parameter_count++;
      if (r->p < r->end && *text(r) == '%')
        r->p++;
      else if (!declaration)
        ir_error(r->c, "defined function requires named parameters");
    } while (accept(r, ","));
    expect(r, ")");
  }
  unsigned global = index_get(&a->global_index, ir_name_key(r->c, name));
  if (global != IR_NONE)
    ir_error(r->c, "duplicate function/global declaration");
  global = a->global_count++;
  a->globals[global] = (ir_global){name, fn, IR_NONE, line + 1};
  index_put(r->c, &a->global_index, ir_name_key(r->c, name), global);
}
static void add_block(reader *r, unsigned fn, const char *name, unsigned line) {
  ir_analysis *a = r->a;
  ir_function *f = &a->functions[fn];
  unsigned id = a->block_count++;
  if (id >= a->capacity)
    ir_error(r->c, "IR block limit exceeded");
  const char *key = ir_name_key(r->c, ir_format(r->c, "%%%s", name));
  if (index_get(&f->locals, key) != IR_NONE)
    ir_error(r->c, "block label collides with SSA name");
  index_put(r->c, &f->labels, key, id);
  a->blocks[id] = (ir_block){.name = name,
                             .line = line + 1,
                             .function = fn,
                             .first_instruction = a->instruction_count,
                             .rpo = IR_NONE,
                             .idom = IR_NONE};
  f->block_count++;
}
static void bodies(reader *r) {
  ir_analysis *a = r->a;
  ir_module *m = a->text;
  for (unsigned fn = 0; fn < a->function_count; fn++) {
    ir_function *f = &a->functions[fn];
    if (f->declaration)
      continue;
    r->c->line = f->line;
    unsigned header = f->line - 1, end_line = header + 1;
    while (end_line < m->line_count &&
           (!m->lines[end_line].count ||
            strcmp(ir_text(m, m->lines[end_line].first), "}")))
      end_line++;
    unsigned tokens = 0;
    for (unsigned l = header; l <= end_line; l++)
      tokens += m->lines[l].count;
    index_init(r->c, &f->locals, tokens);
    index_init(r->c, &f->labels, end_line - header);
    f->first_definition = a->definition_count;
    f->first_block = a->block_count;
    r->function = fn;
    r->p = m->lines[header].first;
    r->end = r->p + m->lines[header].count;
    while (r->p < r->end && *text(r) != '@')
      r->p++;
    r->p++;
    expect(r, "(");
    if (!accept(r, ")")) {
      do {
        if (accept(r, "..."))
          break;
        unsigned t = type(r);
        parameter_attributes(r);
        const char *name = text(r);
        r->p++;
        definition(r, name, t, IR_NONE, IR_NONE);
      } while (accept(r, ","));
      expect(r, ")");
    }
    unsigned block = IR_NONE;
    for (unsigned l = header + 1; l < end_line; l++) {
      ir_line *line = &m->lines[l];
      if (!line->count)
        continue;
      r->c->line = l + 1;
      r->p = line->first;
      r->end = line->first + line->count;
      if (line->count == 2 && !strcmp(ir_text(m, line->first + 1), ":")) {
        add_block(r, fn, text(r), l);
        block = a->block_count - 1;
        continue;
      }
      if (block == IR_NONE)
        ir_error(r->c, "supported IR requires explicit entry block label");
      unsigned last = l;
      unsigned op = line->op;
      if (!strcmp(ir_text(m, op), "switch")) {
        unsigned at = op, depth = 0;
        int opened = 0, closed = 0;
        for (;;) {
          for (; at < r->end; at++) {
            const char *token = ir_text(m, at);
            if (!strcmp(token, "[")) {
              depth++;
              opened = 1;
            }
            if (!strcmp(token, "]")) {
              if (!depth)
                ir_error(r->c, "unmatched switch bracket");
              if (!--depth && opened) {
                closed = 1;
                break;
              }
            }
          }
          if (closed)
            break;
          if (++last >= end_line)
            ir_error(r->c, "unterminated switch");
          r->end = m->lines[last].first + m->lines[last].count;
        }
      }
      instruction(r, l, last, block);
      l = last;
    }
    if (!f->block_count)
      ir_error(r->c, "function has no blocks");
  }
}
static void resolve(reader *r) {
  ir_analysis *a = r->a;
  for (unsigned i = 0; i < a->instruction_count; i++) {
    ir_instruction *in = &a->instructions[i];
    ir_block *b = &a->blocks[in->block];
    ir_function *f = &a->functions[b->function];
    r->c->line = in->line;
    for (unsigned k = in->first_operand;
         k < in->first_operand + in->operand_count; k++) {
      ir_operand *op = &a->operands[k];
      const char *name = ir_text(a->text, op->token);
      if (op->kind == IR_OPERAND_LITERAL)
        continue;
      const char *key = ir_name_key(r->c, name);
      if (op->kind == IR_OPERAND_LOCAL) {
        unsigned d = index_get(&f->locals, key);
        if (d == IR_NONE)
          ir_error(r->c, ir_format(r->c, "undefined SSA operand %s", name));
        same(r, op->type, a->definitions[d].type);
        op->definition = d;
        op->next_use = a->definitions[d].first_use;
        a->definitions[d].first_use = k;
        a->definitions[d].use_count++;
      } else if (op->kind == IR_OPERAND_BLOCK) {
        op->block = index_get(&f->labels, key);
        if (op->block == IR_NONE)
          ir_error(r->c, ir_format(r->c, "undefined block %s", name));
      } else {
        op->definition = index_get(&a->global_index, key);
        if (op->definition == IR_NONE)
          ir_error(r->c, ir_format(r->c, "undefined global %s", name));
      }
    }
    if (in->opcode == IR_OP_CALL && in->callee != IR_NONE) {
      ir_operand *callee = &a->operands[in->callee];
      if (callee->kind != IR_OPERAND_GLOBAL)
        continue;
      unsigned target = a->globals[callee->definition].function;
      if (target == IR_NONE)
        ir_error(r->c, "direct call targets data, not function");
      ir_function *fn = &a->functions[target];
      same(r, in->type, fn->return_type);
      unsigned count = in->operand_count - 1;
      if (count < fn->parameter_count ||
          (!fn->variadic && count != fn->parameter_count))
        ir_error(r->c, "call argument count mismatch");
      for (unsigned k = 0; k < fn->parameter_count; k++)
        same(r, a->operands[in->first_operand + 1 + k].type,
             a->parameters[fn->first_parameter + k]);
    }
  }
}
static int terminator(const ir_instruction *in) {
  return in->opcode == IR_OP_RET || in->opcode == IR_OP_BR ||
         in->opcode == IR_OP_SWITCH || in->opcode == IR_OP_UNREACHABLE;
}
static void cfg(reader *r) {
  ir_analysis *a = r->a;
  for (unsigned k = 0; k < a->block_count; k++) {
    ir_block *b = &a->blocks[k];
    r->c->line = b->line;
    if (!b->instruction_count)
      ir_error(r->c, "empty/unterminated block");
    int nonphi = 0;
    for (unsigned i = b->first_instruction;
         i < b->first_instruction + b->instruction_count; i++) {
      ir_instruction *in = &a->instructions[i];
      r->c->line = in->line;
      if (in->opcode == IR_OP_PHI && nonphi)
        ir_error(r->c, "PHI after non-PHI instruction");
      if (in->opcode != IR_OP_PHI)
        nonphi = 1;
      if (terminator(in) !=
          (i + 1 == b->first_instruction + b->instruction_count))
        ir_error(r->c, "block requires exactly one final terminator");
    }
    ir_instruction *last =
        &a->instructions[b->first_instruction + b->instruction_count - 1];
    b->first_successor = a->successor_count;
    for (unsigned i = last->first_operand;
         i < last->first_operand + last->operand_count; i++) {
      ir_operand *op = &a->operands[i];
      if (op->kind != IR_OPERAND_BLOCK)
        continue;
      if (a->successor_count == a->capacity)
        ir_error(r->c, "CFG edge limit exceeded");
      a->successors[a->successor_count++] = op->block;
      b->successor_count++;
      a->blocks[op->block].predecessor_count++;
    }
  }
  unsigned total = 0;
  for (unsigned b = 0; b < a->block_count; b++) {
    a->blocks[b].first_predecessor = total;
    total += a->blocks[b].predecessor_count;
    a->blocks[b].predecessor_count = 0;
  }
  a->predecessor_count = total;
  for (unsigned b = 0; b < a->block_count; b++) {
    ir_block *from = &a->blocks[b];
    for (unsigned k = 0; k < from->successor_count; k++) {
      ir_block *to = &a->blocks[a->successors[from->first_successor + k]];
      a->predecessors[to->first_predecessor + to->predecessor_count++] = b;
    }
  }
  for (unsigned fn = 0; fn < a->function_count; fn++) {
    ir_function *f = &a->functions[fn];
    if (!f->declaration && a->blocks[f->first_block].predecessor_count)
      ir_error(r->c, "entry block has predecessors");
  }
}

static unsigned intersect(ir_analysis *a, unsigned x, unsigned y,
                          uint64_t *budget, ir_context *c) {
  while (x != y) {
    if (!(*budget)--)
      ir_error(c, "dominator work limit exceeded");
    if (a->blocks[x].rpo > a->blocks[y].rpo)
      x = a->blocks[x].idom;
    else
      y = a->blocks[y].idom;
  }
  return x;
}
static void dominators(reader *r) {
  ir_analysis *a = r->a;
  unsigned *stack = ir_allocate(r->c, (size_t)a->block_count * sizeof(*stack)),
           *next = ir_allocate(r->c, (size_t)a->block_count * sizeof(*next)),
           *post = ir_allocate(r->c, (size_t)a->block_count * sizeof(*post));
  uint8_t *seen = ir_allocate(r->c, a->block_count);
  uint64_t budget = 20000000;
  for (unsigned fn = 0; fn < a->function_count; fn++) {
    ir_function *f = &a->functions[fn];
    if (f->declaration)
      continue;
    unsigned depth = 1, count = 0, entry = f->first_block;
    stack[0] = entry;
    seen[entry] = 1;
    while (depth) {
      unsigned b = stack[depth - 1];
      ir_block *block = &a->blocks[b];
      if (next[b] < block->successor_count) {
        unsigned to = a->successors[block->first_successor + next[b]++];
        if (!seen[to]) {
          seen[to] = 1;
          stack[depth++] = to;
        }
      } else {
        post[count++] = b;
        depth--;
      }
    }
    for (unsigned k = 0; k < count; k++)
      a->blocks[post[count - k - 1]].rpo = k;
    a->blocks[entry].idom = entry;
    int changed;
    do {
      changed = 0;
      for (unsigned k = count - 1; k-- > 0;) {
        unsigned b = post[k], idom = IR_NONE;
        ir_block *block = &a->blocks[b];
        for (unsigned j = 0; j < block->predecessor_count; j++) {
          if (!budget--)
            ir_error(r->c, "dominator work limit exceeded");
          unsigned pred = a->predecessors[block->first_predecessor + j];
          if (a->blocks[pred].idom == IR_NONE)
            continue;
          idom =
              idom == IR_NONE ? pred : intersect(a, idom, pred, &budget, r->c);
        }
        if (block->idom != idom) {
          block->idom = idom;
          changed = 1;
        }
      }
    } while (changed);
  }
}
int ir_dominates(const ir_analysis *a, unsigned definition, unsigned use) {
  if (definition >= a->block_count || use >= a->block_count ||
      a->blocks[definition].function != a->blocks[use].function)
    return 0;
  if (definition == use)
    return 1;
  if (a->blocks[use].idom == IR_NONE)
    return 0;
  while (a->blocks[use].idom != use) {
    use = a->blocks[use].idom;
    if (use == definition)
      return 1;
  }
  return 0;
}
static void ssa(reader *r) {
  ir_analysis *a = r->a;
  uint64_t budget = 20000000;
  for (unsigned i = 0; i < a->instruction_count; i++) {
    ir_instruction *in = &a->instructions[i];
    ir_block *b = &a->blocks[in->block];
    r->c->line = in->line;
    if (in->opcode == IR_OP_PHI) {
      if (in->operand_count / 2 != b->predecessor_count)
        ir_error(r->c, "PHI predecessor count mismatch");
      for (unsigned j = 1; j < in->operand_count; j += 2) {
        unsigned pred = a->operands[in->first_operand + j].block;
        unsigned expected = 0, actual = 0;
        for (unsigned k = 0; k < b->predecessor_count; k++)
          if (a->predecessors[b->first_predecessor + k] == pred)
            expected++;
        for (unsigned k = 1; k < in->operand_count; k += 2) {
          if (!budget--)
            ir_error(r->c, "PHI work limit exceeded");
          if (a->operands[in->first_operand + k].block != pred)
            continue;
          actual++;
          ir_operand *x = &a->operands[in->first_operand + j - 1],
                     *y = &a->operands[in->first_operand + k - 1];
          int equal = x->kind == y->kind && x->definition == y->definition;
          if (equal && x->kind == IR_OPERAND_LITERAL) {
            const char *sx = ir_text(a->text, x->token),
                       *sy = ir_text(a->text, y->token);
            equal = !strcmp(sx, sy);
            uint64_t nx, ny;
            if (!equal && ir_integer(sx, &nx) && ir_integer(sy, &ny)) {
              unsigned bits = a->types[x->type].bits;
              uint64_t mask =
                  bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
              equal = (nx & mask) == (ny & mask);
            }
          }
          if (!equal)
            ir_error(r->c, "parallel PHI edges have different values");
        }
        if (!expected || actual != expected)
          ir_error(r->c, "PHI incoming edge mismatch");
      }
    }
    for (unsigned k = 0; k < in->operand_count; k++) {
      ir_operand *op = &a->operands[in->first_operand + k];
      if (op->kind != IR_OPERAND_LOCAL)
        continue;
      ir_definition *def = &a->definitions[op->definition];
      if (def->instruction == IR_NONE)
        continue;
      unsigned use_block = in->block, use_instruction = i;
      if (in->opcode == IR_OP_PHI) {
        use_block = a->operands[in->first_operand + k + 1].block;
        ir_block *pred = &a->blocks[use_block];
        use_instruction = pred->first_instruction + pred->instruction_count;
      }
      if (def->block == use_block) {
        if (def->instruction >= use_instruction)
          ir_error(r->c, "SSA use precedes definition");
      } else {
        unsigned node = use_block;
        int dominates = 0;
        while (node != IR_NONE) {
          if (!budget--)
            ir_error(r->c, "SSA dominance work limit exceeded");
          if (node == def->block) {
            dominates = 1;
            break;
          }
          if (a->blocks[node].idom == node)
            break;
          node = a->blocks[node].idom;
        }
        /* LLVM allows arbitrary dominance in blocks unreachable from entry. */
        if (!dominates && a->blocks[use_block].rpo != IR_NONE)
          ir_error(r->c, "SSA definition does not dominate use");
      }
    }
  }
}
void ir_analyze(ir_context *c, ir_module *m, ir_analysis *a) {
  *a = (ir_analysis){.text = m, .capacity = m->count + 1};
  size_t n = a->capacity;
#define ALLOC(member) a->member = ir_allocate(c, n * sizeof(*a->member))
  ALLOC(types);
  ALLOC(fields);
  ALLOC(functions);
  ALLOC(globals);
  ALLOC(definitions);
  ALLOC(instructions);
  ALLOC(operands);
  ALLOC(blocks);
  ALLOC(successors);
  ALLOC(predecessors);
  ALLOC(parameters);
#undef ALLOC
  index_init(c, &a->type_index, a->capacity);
  index_init(c, &a->global_index, a->capacity);
  reader r = {.c = c, .a = a, .work = 20000000};
  for (unsigned l = 0; l < m->line_count; l++) {
    ir_line *line = &m->lines[l];
    if (!line->count)
      continue;
    c->line = l + 1;
    r.p = line->first;
    r.end = r.p + line->count;
    const char *first = text(&r);
    if (!strcmp(first, "define") || !strcmp(first, "declare")) {
      signature(&r, l);
      if (!a->functions[a->function_count - 1].declaration)
        while (++l < m->line_count &&
               (!m->lines[l].count ||
                strcmp(ir_text(m, m->lines[l].first), "}"))) {
        }
    } else if (*first == '@') {
      if (line->count < 4 || strcmp(ir_text(m, r.p + 1), "="))
        ir_error(c, "invalid global declaration");
      unsigned id = a->global_count++;
      r.p += 2;
      while (r.p < r.end && strcmp(text(&r), "global") &&
             strcmp(text(&r), "constant"))
        r.p++;
      if (r.p == r.end)
        ir_error(c, "unsupported global declaration");
      r.p++;
      unsigned storage = type(&r);
      if (a->types[storage].kind == IR_VOID)
        ir_error(c, "void global storage type");
      a->globals[id] = (ir_global){first, IR_NONE, storage, l + 1};
      index_put(c, &a->global_index, ir_name_key(c, first), id);
    }
  }
  for (unsigned k = 0; k < a->global_count; k++) {
    ir_global *g = &a->globals[k];
    if (g->function != IR_NONE)
      continue;
    ir_line *line = &m->lines[g->line - 1];
    c->line = g->line;
    for (unsigned token = line->first + 2; token < line->first + line->count;
         token++) {
      const char *name = ir_text(m, token);
      if (*name == '@' &&
          index_get(&a->global_index, ir_name_key(c, name)) == IR_NONE)
        ir_error(
            c, ir_format(c, "undefined global initializer reference %s", name));
    }
  }
  bodies(&r);
  resolve(&r);
  cfg(&r);
  dominators(&r);
  ssa(&r);
}
void ir_verify(ir_context *c, ir_module *m) {
  ir_allocation *mark = c->allocations;
  ir_analysis a;
  ir_analyze(c, m, &a);
  ir_release_to(c, mark);
}
