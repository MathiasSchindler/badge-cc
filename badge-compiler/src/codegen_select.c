#include "codegen_internal.h"
#include "thumb_immediate.h"
#include "target_cost.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

enum { SELECT_SKIP = 1, SELECT_MLA, SELECT_DIVISION, SELECT_COMPARE_SKIP,
       SELECT_COMPARE_BRANCH, SELECT_COUNT_SKIP, SELECT_ABSOLUTE_STORE,
       SELECT_WRAPPER_STORE, SELECT_WRAPPER_CALL, SELECT_PRODUCT_CAST,
       SELECT_SIGNED_PRODUCT, SELECT_EXTENDED_LOAD, SELECT_SHIFTED_BINARY,
       SELECT_BITFIELD, SELECT_MLS, SELECT_MASK_TEST, SELECT_INVERTED_BINARY,
       SELECT_ROTATE, SELECT_ROTATED_XOR, SELECT_BITFIELD_INSERT,
       SELECT_SIGNED_FIELD, SELECT_DUAL_PRODUCT };
typedef struct {
  cg_inst *instruction, *producer, *countdown;
  unsigned kind;
} cg_selected_pair;
struct cg_selection {
  cg_selected_pair *pairs;
  unsigned capacity;
  unsigned immediates, shifted_addresses, constant_addresses, multiply_adds;
  unsigned shared_divisions;
  unsigned register_operations;
  unsigned compare_branches;
  uint32_t absolute_base;
  int absolute_base_emitted;
  int wrapper;
  unsigned wrapper_value_register;
  int wrapper_value_emitted;
};

static int number(const char *s, uint32_t *value);

static int power_of_two(uint32_t value) {
  return value && !(value & (value - 1));
}
static unsigned low_mask_width(uint32_t value) {
  return value && !(value & (value + 1)) ? 32 - (unsigned)__builtin_clz(value) : 0;
}
static int commutative_literal(cg_context *c, cg_inst *i, unsigned *at,
                               uint32_t *value) {
  *at = 4;
  if (number(c->tokens[i->begin + 6].text, value))
    return 1;
  *at = 6;
  return number(c->tokens[i->begin + 4].text, value);
}
static const char *shifted_binary(cg_context *c, cg_inst *shift, cg_inst *use,
                                  uint32_t *amount, unsigned *input,
                                  unsigned *other) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      !cg_global_residency(c) || shift->result->type->bits != 32 ||
      use->result->type->bits != 32 ||
      cg_register_lookup(c, shift->result->name)->selection_uses != 1)
    return NULL;
  const char *operation = c->tokens[shift->begin + 2].text;
  const char *user = c->tokens[use->begin + 2].text;
  if (strcmp(user, "xor") && strcmp(user, "or") && strcmp(user, "and") &&
      strcmp(user, "add") && strcmp(user, "sub"))
    return NULL;
  int scaled_sub = !strcmp(operation, "mul") && !strcmp(user, "sub");
  *input = 4;
  if (scaled_sub) {
    uint32_t factor;
    if (!commutative_literal(c, shift, input, &factor) ||
        factor < 2 || !power_of_two(factor))
      return NULL;
    *amount = (unsigned)__builtin_ctz(factor);
  } else if ((strcmp(operation, "shl") && strcmp(operation, "lshr") &&
              strcmp(operation, "ashr")) ||
             !number(c->tokens[shift->begin + 6].text, amount) ||
             !*amount || *amount >= 32)
    return NULL;
  if (!strcmp(c->tokens[use->begin + 6].text, shift->result->name))
    *other = 4;
  else if ((scaled_sub || strcmp(user, "sub")) &&
           !strcmp(c->tokens[use->begin + 4].text, shift->result->name))
    *other = 6;
  else
    return NULL;
  if (c->tokens[use->begin + *other].text[0] != '%')
    return NULL;
  return scaled_sub || !strcmp(operation, "shl") ? "lsl" :
         !strcmp(operation, "lshr") ? "lsr" : "asr";
}

static int number(const char *s, uint32_t *value) {
  char *end;
  if (!*s || (*s != '-' && (*s < '0' || *s > '9')))
    return 0;
  errno = 0;
  uint64_t n = strtoull(s, &end, 10);
  if (*end || errno == ERANGE ||
      (*s == '-' ? n < UINT64_MAX - INT32_MAX : n > UINT32_MAX))
    return 0;
  *value = (uint32_t)n;
  return 1;
}
static int scalar(cg_context *c, cg_inst *i) {
  if (!i || !i->result || i->end - i->begin != 7 ||
      strcmp(c->tokens[i->begin + 5].text, ","))
    return 0;
  if (strcmp(c->tokens[i->begin + 3].text, "i32")) {
    if (!cg_operand_selection(c) ||
        (strcmp(c->tokens[i->begin + 3].text, "i8") &&
         strcmp(c->tokens[i->begin + 3].text, "i16")))
      return 0;
    const char *op = c->tokens[i->begin + 2].text;
    if (strcmp(op, "add") && strcmp(op, "sub") && strcmp(op, "mul") &&
        strcmp(op, "and") && strcmp(op, "or") && strcmp(op, "xor") &&
        strcmp(op, "shl") && strcmp(op, "lshr"))
      return 0;
  }
  for (unsigned k = 4; k <= 6; k += 2) {
    const char *s = c->tokens[i->begin + k].text;
    uint32_t unused;
    if (*s != '%' && !number(s, &unused))
      return 0;
  }
  return 1;
}
static int scalar32(cg_context *c, cg_inst *i, const char *op) {
  return i && i->result && i->end - i->begin == 7 && i->result->type->bits == 32 &&
         !strcmp(c->tokens[i->begin + 2].text, op) && scalar(c, i);
}
static int single_use(cg_context *c, cg_inst *i) {
  return cg_register_lookup(c, i->result->name)->selection_uses == 1;
}
static unsigned binary_use_of(cg_context *c, cg_inst *i, cg_inst *definition) {
  if (!strcmp(c->tokens[i->begin + 4].text, definition->result->name))
    return 4;
  return !strcmp(c->tokens[i->begin + 6].text, definition->result->name) ? 6 : 0;
}
static int rotate_amount(cg_context *c, cg_inst *left, cg_inst *right,
                          cg_inst *combine, uint32_t *amount) {
  if (!scalar32(c, left, "shl")) {
    cg_inst *swap = left;
    left = right;
    right = swap;
  }
  uint32_t lsl;
  unsigned a, b;
  return scalar32(c, left, "shl") && scalar32(c, right, "lshr") &&
         scalar32(c, combine, "or") && single_use(c, left) && single_use(c, right) &&
         (a = binary_use_of(c, combine, left)) &&
         (b = binary_use_of(c, combine, right)) && a != b &&
         !strcmp(c->tokens[left->begin + 4].text, c->tokens[right->begin + 4].text) &&
         number(c->tokens[left->begin + 6].text, &lsl) &&
         number(c->tokens[right->begin + 6].text, amount) &&
         lsl > 0 && lsl < 32 && *amount == 32 - lsl;
}
static int inserted_field(cg_context *c, cg_inst *clear, cg_inst *shift,
                           cg_inst *clip, cg_inst *combine, unsigned *base,
                           uint32_t *lsb, unsigned *width) {
  uint32_t mask, cleared;
  unsigned data, a, b;
  if (!scalar32(c, clear, "and") || !scalar32(c, shift, "shl") ||
      !scalar32(c, clip, "and") || !scalar32(c, combine, "or") ||
      !single_use(c, clear) || !single_use(c, shift) || !single_use(c, clip) ||
      !(a = binary_use_of(c, combine, clear)) ||
      !(b = binary_use_of(c, combine, clip)) || a == b ||
      !(data = binary_use_of(c, clip, shift)) ||
      !number(c->tokens[clip->begin + (10 - data)].text, &mask) ||
      !commutative_literal(c, clear, base, &cleared) || cleared != ~mask ||
      !number(c->tokens[shift->begin + 6].text, lsb) || *lsb >= 32)
    return 0;
  *width = low_mask_width(mask >> *lsb);
  return *width && (mask >> *lsb << *lsb) == mask;
}
static int cast_from(cg_context *c, cg_inst *cast, cg_inst *source,
                     const char *op, const char *from, const char *to) {
  return cast && cast->result && cast->end - cast->begin == 7 &&
         !strcmp(c->tokens[cast->begin + 2].text, op) &&
         !strcmp(c->tokens[cast->begin + 3].text, from) &&
         !strcmp(c->tokens[cast->begin + 4].text, source->result->name) &&
         !strcmp(c->tokens[cast->begin + 5].text, "to") &&
         !strcmp(c->tokens[cast->begin + 6].text, to);
}
static int signed_halfword(cg_context *c, cg_inst *trunc, cg_inst *extend,
                            const char *source) {
  return trunc && trunc->result && trunc->end - trunc->begin == 7 &&
         !strcmp(c->tokens[trunc->begin + 2].text, "trunc") &&
         !strcmp(c->tokens[trunc->begin + 3].text, "i32") &&
         !strcmp(c->tokens[trunc->begin + 4].text, source) &&
         !strcmp(c->tokens[trunc->begin + 5].text, "to") &&
         !strcmp(c->tokens[trunc->begin + 6].text, "i16") &&
         cast_from(c, extend, trunc, "sext", "i16", "i32");
}
static int ordered_binary(cg_context *c, cg_inst *i, const char *op,
                          cg_inst *left, cg_inst *right) {
  return scalar32(c, i, op) && binary_use_of(c, i, left) == 4 &&
         binary_use_of(c, i, right) == 6;
}
static int high_half(cg_context *c, cg_inst *shift, const char *source) {
  return scalar32(c, shift, "lshr") &&
         !strcmp(c->tokens[shift->begin + 4].text, source) &&
         !strcmp(c->tokens[shift->begin + 6].text, "16");
}
static int bitfield(cg_context *c, cg_inst *shift, cg_inst *use,
                    uint32_t *lsb, unsigned *width) {
  if (shift->result->type->bits != 32 || use->result->type->bits != 32 ||
      strcmp(c->tokens[shift->begin + 2].text, "lshr") ||
      strcmp(c->tokens[use->begin + 2].text, "and") ||
      cg_register_lookup(c, shift->result->name)->selection_uses != 1 ||
      !number(c->tokens[shift->begin + 6].text, lsb) || !*lsb || *lsb >= 32)
    return 0;
  uint32_t mask;
  for (unsigned k = 4; k <= 6; k += 2)
    if (!strcmp(c->tokens[use->begin + k].text, shift->result->name) &&
        number(c->tokens[use->begin + (k == 4 ? 6 : 4)].text, &mask)) {
      *width = low_mask_width(mask);
      return *width && *width <= 32 - *lsb;
    }
  return 0;
}
static const char *inverted_binary(cg_context *c, cg_inst *invert, cg_inst *use,
                                   unsigned *source, unsigned *other) {
  if (invert->result->type->bits != 32 || use->result->type->bits != 32 ||
      strcmp(c->tokens[invert->begin + 2].text, "xor") ||
      cg_register_lookup(c, invert->result->name)->selection_uses != 1)
    return NULL;
  const char *op = c->tokens[use->begin + 2].text;
  if (strcmp(op, "and") && strcmp(op, "or"))
    return NULL;
  uint32_t mask;
  if (!commutative_literal(c, invert, source, &mask) || mask != UINT32_MAX ||
      c->tokens[invert->begin + *source].text[0] != '%')
    return NULL;
  for (unsigned at = 4; at <= 6; at += 2)
    if (!strcmp(c->tokens[use->begin + at].text, invert->result->name)) {
      *other = at == 4 ? 6 : 4;
      if (c->tokens[use->begin + *other].text[0] == '%')
        return !strcmp(op, "and") ? "bic" : "orn";
    }
  return NULL;
}
static int mask_test(cg_context *c, cg_inst *mask, cg_inst *compare) {
  if (!scalar(c, mask) || mask->result->type->bits != 32 ||
      strcmp(c->tokens[mask->begin + 2].text, "and") ||
      cg_register_lookup(c, mask->result->name)->selection_uses != 1 ||
      !compare->result || compare->end - compare->begin != 8 ||
      strcmp(c->tokens[compare->begin + 2].text, "icmp") ||
      strcmp(c->tokens[compare->begin + 4].text, "i32") ||
      strcmp(c->tokens[compare->begin + 6].text, ","))
    return 0;
  const char *pred = c->tokens[compare->begin + 3].text;
  if (strcmp(pred, "eq") && strcmp(pred, "ne"))
    return 0;
  const char *left = c->tokens[compare->begin + 5].text;
  const char *right = c->tokens[compare->begin + 7].text;
  uint32_t value;
  if ((!strcmp(left, mask->result->name) && number(right, &value) && !value) ||
      (!strcmp(right, mask->result->name) && number(left, &value) && !value)) {
    const char *a = c->tokens[mask->begin + 4].text;
    const char *b = c->tokens[mask->begin + 6].text;
    if (number(b, &value) || number(a, &value))
      return value && thumb_modified(value) >= 0;
    return 1;
  }
  return 0;
}
static cg_block *counted_block(cg_context *c, const char *token) {
  const char *name = cg_name(c, token);
  for (cg_block *b = c->fn->blocks; b; b = b->next) {
    cg_register_work(c);
    if (!strcmp(b->name, name)) return b;
  }
  return NULL;
}
static int counted_jump(cg_context *c, cg_inst *i) {
  return i && !i->result && i->end - i->begin == 3 &&
      !strcmp(c->tokens[i->begin].text, "br") &&
      !strcmp(c->tokens[i->begin + 1].text, "label");
}
int cg_emit_counted_leaf(cg_context *c) {
  /* A closed scalar loop can keep its sole state in the incoming ABI register. */
  cg_function *f = c->fn;
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      c->register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT ||
      f->ret->size || f->argc != 1 || f->byval[0] ||
      f->args[0]->type->bits != 32 || f->args[0]->type->size != 4 ||
      f->arg_align[0] >= 8)
    return 0;
  cg_block *entry = f->blocks, *header = entry;
  unsigned entries = 0;
  while (header && header->first == header->last &&
         counted_jump(c, header->first) && entries < 8) {
    entry = header;
    header = counted_block(c, c->tokens[header->last->begin + 2].text);
    entries++;
  }
  if (!entries || !header || header == entry) return 0;
  cg_inst *phi = header->first;
  if (!phi || !phi->result || phi->end - phi->begin != 15 ||
      strcmp(c->tokens[phi->begin + 2].text, "phi") ||
      strcmp(c->tokens[phi->begin + 3].text, "i32") ||
      strcmp(c->tokens[phi->begin + 4].text, "[") ||
      strcmp(c->tokens[phi->begin + 6].text, ",") ||
      strcmp(c->tokens[phi->begin + 8].text, "]") ||
      strcmp(c->tokens[phi->begin + 9].text, ",") ||
      strcmp(c->tokens[phi->begin + 10].text, "[") ||
      strcmp(c->tokens[phi->begin + 12].text, ",") ||
      strcmp(c->tokens[phi->begin + 14].text, "]"))
    return 0;
  cg_inst *decrement = phi->next, *compare = decrement ? decrement->next : NULL;
  cg_inst *branch = compare ? compare->next : NULL;
  if (!scalar(c, decrement) ||
      strcmp(c->tokens[decrement->begin + 3].text, "i32") ||
      strcmp(c->tokens[decrement->begin + 2].text, "sub") ||
      strcmp(c->tokens[decrement->begin + 4].text, phi->result->name) ||
      strcmp(c->tokens[decrement->begin + 6].text, "1") ||
      !compare || !compare->result || compare->end - compare->begin != 8 ||
      strcmp(c->tokens[compare->begin + 2].text, "icmp") ||
      (strcmp(c->tokens[compare->begin + 3].text, "ne") &&
       strcmp(c->tokens[compare->begin + 3].text, "eq")) ||
      strcmp(c->tokens[compare->begin + 4].text, "i32") ||
      strcmp(c->tokens[compare->begin + 5].text, phi->result->name) ||
      strcmp(c->tokens[compare->begin + 6].text, ",") ||
      strcmp(c->tokens[compare->begin + 7].text, "0") ||
      !branch || branch != header->last || branch->result ||
      branch->end - branch->begin != 9 ||
      strcmp(c->tokens[branch->begin].text, "br") ||
      strcmp(c->tokens[branch->begin + 1].text, "i1") ||
      strcmp(c->tokens[branch->begin + 2].text, compare->result->name) ||
      strcmp(c->tokens[branch->begin + 3].text, ",") ||
      strcmp(c->tokens[branch->begin + 4].text, "label") ||
      strcmp(c->tokens[branch->begin + 6].text, ",") ||
      strcmp(c->tokens[branch->begin + 7].text, "label"))
    return 0;
  int ne = !strcmp(c->tokens[compare->begin + 3].text, "ne");
  cg_block *body = counted_block(c, c->tokens[branch->begin + (ne ? 5 : 8)].text);
  cg_block *exit = counted_block(c, c->tokens[branch->begin + (ne ? 8 : 5)].text);
  if (!body || !exit || body == header || exit == header || body == exit ||
      !counted_jump(c, body->last) ||
      counted_block(c, c->tokens[body->last->begin + 2].text) != header ||
      !exit->first || exit->first != exit->last || exit->first->result ||
      exit->first->end - exit->first->begin != 2 ||
      strcmp(c->tokens[exit->first->begin].text, "ret") ||
      strcmp(c->tokens[exit->first->begin + 1].text, "void"))
    return 0;
  for (unsigned k = 5; k <= 11; k += 6) {
    cg_block *from = counted_block(c, c->tokens[phi->begin + k + 2].text);
    const char *value = c->tokens[phi->begin + k].text;
    if (from == entry) {
      if (strcmp(value, f->args[0]->name)) return 0;
    } else if (from == body) {
      if (strcmp(value, decrement->result->name)) return 0;
    } else return 0;
  }
  if (counted_block(c, c->tokens[phi->begin + 7].text) ==
      counted_block(c, c->tokens[phi->begin + 13].text)) return 0;
  unsigned blocks = 0, nops = 0;
  for (cg_block *b = f->blocks; b; b = b->next) blocks++;
  if (blocks != entries + 3) return 0;
  for (cg_inst *i = body->first; i && i != body->last; i = i->next) {
    cg_assembly *a = cg_instruction_assembly(c, i);
    if (!a || !a->known || a->reads || a->writes || a->flags_read ||
        a->memory_read || a->memory_write || a->control)
      return 0;
    for (const char *p = a->text; *p; p++)
      if (*p == 'n' && !strncmp(p, "nop", 3)) nops++;
  }
  /* CBZ's unsigned forward displacement must remain independently encodable. */
  if (!nops || nops > 60) return 0;
  f->frame = 0;
  cg_emit(c, "@ counted-leaf: unsigned i32 iterations, %u explicit nops\n"
             "cbz r0, .Lcg_return%u\n.Lcg_counted%u:\n", nops, f->id, f->id);
  for (cg_inst *i = body->first; i != body->last; i = i->next) {
    cg_assembly *a = cg_instruction_assembly(c, i);
    unsigned count = 0;
    for (const char *p = a->text; *p; p++)
      if (*p == 'n' && !strncmp(p, "nop", 3)) count++;
    cg_emit(c, "@ assembly-effects: known 1 reads 0 writes 0 flags 0/%u "
               "memory 0/0 barrier %u control 0\n@ constant-assembly-lines: %u\n",
            a->flags_write, a->memory_barrier, count);
    for (unsigned k = 0; k < count; k++) cg_emit(c, "nop\n");
  }
  cg_emit(c, "subs.n r0, r0, #1\nbne.n .Lcg_counted%u\n", f->id);
  return 1;
}
static cg_selected_pair *pair(cg_context *c, cg_inst *i) {
  unsigned index = (i->begin * 2654435761u) & (c->selection->capacity - 1);
  for (;;) {
    cg_register_work(c);
    cg_selected_pair *p = &c->selection->pairs[index];
    if (!p->instruction || p->instruction == i)
      return p;
    index = (index + 1) & (c->selection->capacity - 1);
  }
}
static cg_inst *absolute_pointer(cg_context *c, const char *name, uint32_t *address) {
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      if (i->result && !strcmp(i->result->name, name) && i->end - i->begin == 7 &&
          !strcmp(c->tokens[i->begin + 2].text, "inttoptr") &&
          !strcmp(c->tokens[i->begin + 3].text, "i32") &&
          !strcmp(c->tokens[i->begin + 5].text, "to") &&
          !strcmp(c->tokens[i->begin + 6].text, "ptr") &&
          cg_register_lookup(c, name)->selection_uses == 1 &&
          number(c->tokens[i->begin + 4].text, address))
        return i;
    }
  return NULL;
}
static int absolute_store(cg_context *c, cg_inst *i, uint32_t *address,
                          const char **source) {
  unsigned type = i->begin + 1;
  if (!strcmp(c->tokens[type].text, "volatile"))
    type++;
  if (i->result || strcmp(c->tokens[i->begin].text, "store") ||
      i->end - type != 8 || strcmp(c->tokens[type].text, "i32") ||
      strcmp(c->tokens[type + 2].text, ",") || strcmp(c->tokens[type + 3].text, "ptr") ||
      strcmp(c->tokens[type + 5].text, ",") || strcmp(c->tokens[type + 6].text, "align") ||
      strcmp(c->tokens[type + 7].text, "4") ||
      !absolute_pointer(c, c->tokens[type + 4].text, address) || (*address & 3))
    return 0;
  *source = c->tokens[type + 1].text;
  return 1;
}
static void absolute_stores(cg_context *c) {
  /* This linear leaf shape leaves r1 scratch-free and never consumes NZCV. */
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      c->register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT || c->fn->argc > 1 ||
      c->fn->ret->size || c->fn->byval[0] || (c->fn->argc && c->fn->args[0]->type->bits != 32))
    return;
  uint32_t low = UINT32_MAX, high = 0;
  unsigned stores = 0;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      unsigned at = i->begin + (i->result ? 2 : 0);
      const char *op = c->tokens[at].text;
      uint32_t address;
      if (!strcmp(op, "inttoptr")) {
        if (!i->result || !absolute_pointer(c, i->result->name, &address))
          return;
      } else if (!strcmp(op, "store")) {
        const char *source;
        if (!absolute_store(c, i, &address, &source))
          return;
        uint32_t value;
        if (!number(source, &value) && (!c->fn->argc || strcmp(source, c->fn->args[0]->name)))
          return;
        if (address < low)
          low = address;
        if (address > high)
          high = address;
        stores++;
      } else if (!strcmp(op, "call")) {
        cg_assembly *a = cg_instruction_assembly(c, i);
        if (!a || !a->known || a->reads || a->writes || a->flags_read || a->control)
          return;
      } else if (!strcmp(op, "br")) {
        if (i->end - at != 3 || strcmp(c->tokens[at + 1].text, "label") || !b->next ||
            strcmp(cg_name(c, c->tokens[at + 2].text), b->next->name))
          return;
      } else if (!strcmp(op, "ret")) {
        if (b->next || i->end - at != 2 || strcmp(c->tokens[at + 1].text, "void"))
          return;
      } else
        return;
    }
  if (stores < 2 || low <= 65535 || high - low > 124)
    return;
  uint32_t base = low & ~UINT32_C(127);
  if (high - base > 124 || thumb_modified(base) < 0)
    base = low;
  c->selection->absolute_base = base;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      unsigned at = i->begin + (i->result ? 2 : 0);
      if (!strcmp(c->tokens[at].text, "inttoptr"))
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SKIP};
      else if (!strcmp(c->tokens[at].text, "store"))
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_ABSOLUTE_STORE};
    }
}
static void absolute_wrapper(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      c->register_residency != BADGE_RESIDENCY_GLOBAL_COMPACT || c->fn->frame ||
      c->fn->ret->size || c->fn->argc != 1 || c->fn->byval[0] ||
      c->fn->args[0]->type->bits != 32 ||
      cg_register_lookup(c, c->fn->args[0]->name)->selection_uses != 1)
    return;
  uint32_t low = UINT32_MAX, high = 0, common = 0;
  unsigned stores = 0, before = 0, after = 0;
  cg_inst *call = NULL;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      unsigned at = i->begin + (i->result ? 2 : 0);
      const char *op = c->tokens[at].text;
      uint32_t address, value;
      if (!strcmp(op, "inttoptr")) {
        if (!i->result || !absolute_pointer(c, i->result->name, &address))
          return;
      } else if (!strcmp(op, "store")) {
        const char *source;
        if (!absolute_store(c, i, &address, &source) || !number(source, &value) ||
            (stores && value != common))
          return;
        common = value;
        stores++;
        if (call) after++;
        else before++;
        if (address < low) low = address;
        if (address > high) high = address;
      } else if (!strcmp(op, "call")) {
        if (call || i->result || i->end - at != 7 ||
            strcmp(c->tokens[at + 1].text, "void") ||
            c->tokens[at + 2].text[0] != '@' ||
            !strncmp(c->tokens[at + 2].text, "@llvm.", 6) ||
            strcmp(c->tokens[at + 3].text, "(") ||
            strcmp(c->tokens[at + 4].text, "i32") ||
            strcmp(c->tokens[at + 5].text, c->fn->args[0]->name) ||
            strcmp(c->tokens[at + 6].text, ")"))
          return;
        call = i;
      } else if (!strcmp(op, "br")) {
        if (i->end - at != 3 || strcmp(c->tokens[at + 1].text, "label") || !b->next ||
            strcmp(cg_name(c, c->tokens[at + 2].text), b->next->name))
          return;
      } else if (!strcmp(op, "ret")) {
        if (b->next || i->end - at != 2 || strcmp(c->tokens[at + 1].text, "void"))
          return;
      } else
        return;
    }
  if (!before || !after || low <= 65535 || high - low > 124)
    return;
  uint32_t base = low & ~UINT32_C(127);
  if (high - base > 124 || thumb_modified(base) < 0)
    base = low;
  /* Unavailable, replaceable and recursive callees retain AAPCS fallback. */
  unsigned mentions = 0;
  c->selection->wrapper_value_register =
      cg_internal_call_mentions(c, cg_name(c, c->tokens[call->begin + 2].text), &mentions) &&
      !(mentions & (1u << 2)) ? 2 : 1;
  /* r0 is forwarded untouched; r4 always preserves the shared address. */
  c->selection->wrapper = 1;
  c->selection->absolute_base = base;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      unsigned at = i->begin + (i->result ? 2 : 0);
      if (!strcmp(c->tokens[at].text, "inttoptr"))
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SKIP};
      else if (!strcmp(c->tokens[at].text, "store"))
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_WRAPPER_STORE};
      else if (i == call)
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_WRAPPER_CALL};
    }
}
int cg_selection_forward_argument(cg_context *c) {
  return c->selection && c->selection->wrapper;
}
static void signed_products(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS)
    return;
  for (cg_block *b = c->fn->blocks; b; b = b->next) {
    cg_inst *recent[8] = {0};
    unsigned cursor = 0;
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      if (i->result && i->end - i->begin == 7 && !pair(c, i)->instruction &&
          !strcmp(c->tokens[i->begin + 2].text, "mul") &&
          !strcmp(c->tokens[i->begin + 5].text, ",")) {
        const char *to = c->tokens[i->begin + 3].text;
        const char *from = !strcmp(to, "i64") ? "i32" :
                           !strcmp(to, "i32") ? "i16" : NULL;
        cg_inst *cast[2] = {0};
        if (from)
          for (unsigned k = 0; k < 8; k++) {
            cg_register_work(c);
            cg_inst *p = recent[k];
            if (!p || !p->result || p->end - p->begin != 7 ||
                pair(c, p)->instruction ||
                strcmp(c->tokens[p->begin + 2].text, "sext") ||
                strcmp(c->tokens[p->begin + 3].text, from) ||
                strcmp(c->tokens[p->begin + 5].text, "to") ||
                strcmp(c->tokens[p->begin + 6].text, to))
              continue;
            for (unsigned n = 0; n < 2; n++)
              if (!strcmp(p->result->name, c->tokens[i->begin + 4 + 2 * n].text))
                cast[n] = p;
          }
        if (cast[0] && cast[1]) {
          *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SIGNED_PRODUCT};
          for (unsigned n = 0; n < 2; n++) {
            cg_inst *p = cast[n];
            if (cg_register_lookup(c, p->result->name)->selection_uses ==
                (cast[0] == cast[1] ? 2u : 1u))
              *pair(c, p) = (cg_selected_pair){.instruction = p, .kind = SELECT_PRODUCT_CAST};
          }
        }
      }
      recent[cursor++ % 8] = i;
    }
  }
}
static int extended_load(cg_context *c, cg_inst *i, cg_inst *next) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      !cg_global_residency(c) ||
      !i->result || !next->result || strcmp(c->tokens[i->begin + 2].text, "load") ||
      next->end - next->begin != 7 ||
      (strcmp(c->tokens[next->begin + 2].text, "sext") &&
       strcmp(c->tokens[next->begin + 2].text, "zext")) ||
      strcmp(c->tokens[next->begin + 4].text, i->result->name) ||
      strcmp(c->tokens[next->begin + 5].text, "to") ||
      strcmp(c->tokens[next->begin + 6].text, "i32") ||
      cg_register_lookup(c, i->result->name)->selection_uses != 1 ||
      pair(c, i)->instruction || pair(c, next)->instruction)
    return 0;
  unsigned at = i->begin + 3;
  if (!strcmp(c->tokens[at].text, "volatile")) at++;
  unsigned count = i->end - at;
  if ((count != 4 && count != 7) ||
      strcmp(c->tokens[at].text, c->tokens[next->begin + 3].text) ||
      (strcmp(c->tokens[at].text, "i8") && strcmp(c->tokens[at].text, "i16")) ||
      strcmp(c->tokens[at + 1].text, ",") || strcmp(c->tokens[at + 2].text, "ptr"))
    return 0;
  const char *ptr = c->tokens[at + 3].text;
  if (*ptr != '%' && *ptr != '@')
    return 0;
  uint32_t align;
  return count == 4 ||
    (!strcmp(c->tokens[at + 4].text, ",") &&
     !strcmp(c->tokens[at + 5].text, "align") &&
     c->tokens[at + 6].text[0] != '-' &&
     number(c->tokens[at + 6].text, &align) && align >= i->result->type->size);
}

static int select_dual_product(cg_context *c, cg_inst *xor) {
  unsigned base;
  uint32_t difference;
  if (!scalar32(c, xor, "xor") ||
      !commutative_literal(c, xor, &base, &difference) || !difference)
    return 0;
  const char *a = c->tokens[xor->begin + base].text, *b = xor->result->name;
  if (*a != '%')
    return 0;
  cg_inst *v[13], *p = xor->next;
  for (unsigned n = 0; n < 13; n++, p = p->next) {
    if (!p || !p->result || pair(c, p)->instruction || (n < 12 && !single_use(c, p)))
      return 0;
    v[n] = p;
  }
  if (!signed_halfword(c, v[0], v[1], a) || !signed_halfword(c, v[2], v[3], b) ||
      !ordered_binary(c, v[4], "mul", v[1], v[3]) ||
      !high_half(c, v[5], a) || !signed_halfword(c, v[6], v[7], v[5]->result->name) ||
      !high_half(c, v[8], b) || !signed_halfword(c, v[9], v[10], v[8]->result->name) ||
      !ordered_binary(c, v[11], "mul", v[7], v[10]) ||
      !ordered_binary(c, v[12], "add", v[4], v[11]))
    return 0;
  /* Two signed halfword products overflow only when both packed inputs
     are 0x80008000. A nonzero XOR proves that SMUAD leaves sticky Q alone. */
  for (unsigned n = 0; n < 12; n++)
    *pair(c, v[n]) = (cg_selected_pair){.instruction=v[n], .kind=SELECT_SKIP};
  *pair(c, v[12]) = (cg_selected_pair){.instruction=v[12], .producer=xor,
                                      .kind=SELECT_DUAL_PRODUCT};
  return 1;
}

static void forward_adjacent_address(cg_context *c, cg_inst *i, cg_inst *next) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      !cg_global_residency(c) || !i->result ||
      strcmp(c->tokens[i->begin + 2].text, "getelementptr") ||
      cg_register_lookup(c, i->result->name)->selection_uses != 1)
    return;
  unsigned at = next->begin + (next->result ? 2 : 0);
  int store = !strcmp(c->tokens[at].text, "store");
  if (!store && strcmp(c->tokens[at].text, "load"))
    return;
  if (!strcmp(c->tokens[++at].text, "volatile")) at++;
  const char *type = c->tokens[at].text;
  if (strcmp(type, "i8") && strcmp(type, "i16") && strcmp(type, "i32"))
    return;
  unsigned ptr = at + (store ? 4 : 3);
  unsigned end = next->end - ptr;
  if (ptr >= next->end || (end != 1 && end != 4) ||
      strcmp(c->tokens[ptr - 2].text, ",") ||
      strcmp(c->tokens[ptr - 1].text, "ptr") ||
      strcmp(c->tokens[ptr].text, i->result->name))
    return;
  const char *base = c->tokens[i->begin + 6].text;
  uint32_t align, bytes = !strcmp(type, "i8") ? 1 : !strcmp(type, "i16") ? 2 : 4;
  if (!store && !pair(c, i)->instruction && i->end - i->begin == 10 &&
      !strcmp(c->tokens[i->begin + 3].text, "i8") &&
      !strcmp(c->tokens[i->begin + 4].text, ",") &&
      !strcmp(c->tokens[i->begin + 5].text, "ptr") && (*base == '%' || *base == '@') &&
      !strcmp(c->tokens[i->begin + 7].text, ",") &&
      !strcmp(c->tokens[i->begin + 8].text, "i32") &&
      c->tokens[i->begin + 9].text[0] == '%' &&
      (end == 1 || (!strcmp(c->tokens[ptr + 1].text, ",") &&
                    !strcmp(c->tokens[ptr + 2].text, "align") &&
                    number(c->tokens[ptr + 3].text, &align) && align >= bytes))) {
    *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SKIP};
    next->indexed_address = i;
  } else if (!cg_global_register(c, i->result->name))
    i->ephemeral_address = next->ephemeral_address = 1;
}

void cg_selection_prepare(cg_context *c) {
  c->selection = NULL;
  if (c->scalar_selection == BADGE_SELECTION_LEGACY)
    return;
  c->selection = cg_alloc(c, sizeof(*c->selection));
  badge_cost_estimate compare_cost = badge_target_cost((badge_cost_request){
      .kind = BADGE_COST_ALU, .encoding_bytes = 2, .sets_flags = 1});
  int compare_profitable = compare_cost.code_bytes.known &&
      compare_cost.code_bytes.min > 0 && compare_cost.baseline_issue.known &&
      compare_cost.baseline_issue.min > 0;
  unsigned instructions = 0;
  int assembly = 0;
  for (unsigned n = 0; n < c->register_capacity; n++)
    c->register_table[n].selection_uses = 0;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      instructions++;
      for (unsigned k = i->begin + (i->result ? 3 : 1); k < i->end; k++) {
        cg_register_work(c);
        if (!strcmp(c->tokens[k].text, "asm")) assembly = 1;
        if (c->tokens[k].text[0] == '%') {
          cg_register_value *v = cg_register_lookup(c, c->tokens[k].text);
          if (v->value)
            v->selection_uses++;
        }
      }
    }
  c->selection->capacity = 2;
  while (c->selection->capacity < instructions * 2)
    c->selection->capacity *= 2;
  c->selection->pairs =
      cg_alloc(c, c->selection->capacity * sizeof(*c->selection->pairs));
  absolute_stores(c);
  absolute_wrapper(c);
  int constants = c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
                  cg_global_residency(c);
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i && i->next; i = i->next) {
      cg_register_work(c);
      cg_inst *next = i->next;
      forward_adjacent_address(c, i, next);
      if (extended_load(c, i, next)) {
        *pair(c, i) = (cg_selected_pair){.instruction = i, .producer = next, .kind = SELECT_EXTENDED_LOAD};
        *pair(c, next) = (cg_selected_pair){.instruction = next, .kind = SELECT_SKIP};
        continue;
      }
      if (constants && !assembly && !pair(c, i)->instruction &&
          !pair(c, next)->instruction && mask_test(c, i, next)) {
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction = next, .producer = i,
                                           .kind = SELECT_MASK_TEST};
        continue;
      }
      if (cg_branch_selection(c) &&
          compare_profitable &&
          c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT &&
          (cg_effect_selection(c) || !assembly) && i->result &&
          i->result->type->bits == 1 && i->end - i->begin == 8 &&
          !strcmp(c->tokens[i->begin + 2].text, "icmp") &&
          cg_comparison_condition(c->tokens[i->begin + 3].text) &&
          (!strcmp(c->tokens[i->begin + 4].text, "i32") ||
           (cg_control_selection(c) &&
            ((constants && !strcmp(c->tokens[i->begin + 4].text, "i64")) ||
             !strcmp(c->tokens[i->begin + 4].text, "i8") ||
             !strcmp(c->tokens[i->begin + 4].text, "i16") ||
             (!strcmp(c->tokens[i->begin + 4].text, "ptr") &&
              c->tokens[i->begin + 3].text[0] != 's')))) &&
          !strcmp(c->tokens[i->begin + 6].text, ",") &&
          cg_register_lookup(c, i->result->name)->selection_uses == 1 &&
          !next->result && next == b->last && next->end - next->begin == 9 &&
          !strcmp(c->tokens[next->begin].text, "br") &&
          !strcmp(c->tokens[next->begin + 1].text, "i1") &&
          !strcmp(c->tokens[next->begin + 2].text, i->result->name) &&
          !strcmp(c->tokens[next->begin + 3].text, ",") &&
          !strcmp(c->tokens[next->begin + 4].text, "label") &&
          !strcmp(c->tokens[next->begin + 6].text, ",") &&
          !strcmp(c->tokens[next->begin + 7].text, "label")) {
        int operands = 1;
        for (unsigned k = 5; k <= 7; k += 2) {
          const char *s = c->tokens[i->begin + k].text;
          uint32_t unused;
          if (*s != '%' && !number(s, &unused) &&
              !(constants && !strcmp(c->tokens[i->begin + 4].text, "i64") &&
                (*s == '-' || (*s >= '0' && *s <= '9'))) &&
              !(cg_control_selection(c) && !strcmp(s, "null"))) operands = 0;
        }
        if (operands) {
          cg_selected_pair *cmp = pair(c, i);
          *cmp = (cg_selected_pair){.instruction=i, .kind=SELECT_COMPARE_SKIP,
            .producer=cmp->kind == SELECT_MASK_TEST ? cmp->producer : NULL};
          *pair(c, next) = (cg_selected_pair){.instruction=next, .producer=i, .kind=SELECT_COMPARE_BRANCH};
          if (cg_control_selection(c) && !strcmp(c->tokens[i->begin + 4].text, "i32") &&
              (!strcmp(c->tokens[i->begin + 3].text, "ne") ||
               !strcmp(c->tokens[i->begin + 3].text, "eq")) &&
              !strcmp(c->tokens[i->begin + 7].text, "0")) {
            cg_inst *previous = NULL;
            for (cg_inst *p = b->first; p && p != i; p = p->next) {
              cg_register_work(c);
              previous = p;
            }
            if (scalar(c, previous) && previous->result->type->bits == 32 &&
                !strcmp(c->tokens[previous->begin + 2].text, "sub") &&
                !strcmp(c->tokens[previous->begin + 6].text, "1") &&
                !strcmp(previous->result->name, c->tokens[i->begin + 5].text) &&
                cg_global_register(c, previous->result->name)) {
              *pair(c, previous) = (cg_selected_pair){.instruction=previous, .kind=SELECT_COUNT_SKIP};
              pair(c, next)->countdown = previous;
            }
          }
          c->selection->compare_branches++;
          continue;
        }
      }
      if (constants && select_dual_product(c, i))
        continue;
      cg_inst *extend = next->next;
      uint32_t offset;
      if (constants && scalar32(c, i, "lshr") && !pair(c, i)->instruction &&
          signed_halfword(c, next, extend, i->result->name) &&
          !pair(c, next)->instruction && !pair(c, extend)->instruction &&
          single_use(c, i) && single_use(c, next) &&
          number(c->tokens[i->begin + 6].text, &offset) && offset > 0 && offset <= 16) {
        *pair(c, i) = (cg_selected_pair){.instruction=i, .kind=SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction=next, .kind=SELECT_SKIP};
        *pair(c, extend) = (cg_selected_pair){.instruction=extend, .producer=i,
                                              .kind=SELECT_SIGNED_FIELD};
        continue;
      }
      if (!scalar(c, i) || !scalar(c, next) || pair(c, i)->instruction ||
          pair(c, next)->instruction)
        continue;
      uint32_t amount;
      unsigned width;
      cg_inst *third = next->next, *fourth = third ? third->next : NULL;
      if (constants && third && !pair(c, third)->instruction &&
          rotate_amount(c, i, next, third, &amount)) {
        *pair(c, i) = (cg_selected_pair){.instruction=i, .kind=SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction=next, .kind=SELECT_SKIP};
        *pair(c, third) = (cg_selected_pair){.instruction=third, .producer=i,
                                            .kind=SELECT_ROTATE};
        if (fourth && !pair(c, fourth)->instruction && scalar32(c, fourth, "xor") &&
            single_use(c, third) && binary_use_of(c, fourth, third)) {
          pair(c, third)->kind = SELECT_SKIP;
          *pair(c, fourth) = (cg_selected_pair){.instruction=fourth, .producer=i,
                                               .kind=SELECT_ROTATED_XOR};
        }
        continue;
      }
      unsigned base;
      if (constants && third && fourth && !pair(c, third)->instruction &&
          !pair(c, fourth)->instruction &&
          inserted_field(c, i, next, third, fourth, &base, &amount, &width)) {
        *pair(c, i) = (cg_selected_pair){.instruction=i, .kind=SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction=next, .kind=SELECT_SKIP};
        *pair(c, third) = (cg_selected_pair){.instruction=third, .kind=SELECT_SKIP};
        *pair(c, fourth) = (cg_selected_pair){.instruction=fourth, .producer=i,
                                             .kind=SELECT_BITFIELD_INSERT};
        continue;
      }
      if (constants && bitfield(c, i, next, &amount, &width)) {
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction = next, .producer = i,
                                           .kind = SELECT_BITFIELD};
        continue;
      }
      unsigned source, other;
      if (constants && inverted_binary(c, i, next, &source, &other)) {
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction = next, .producer = i,
                                           .kind = SELECT_INVERTED_BINARY};
        continue;
      }
      if (shifted_binary(c, i, next, &amount, &source, &other)) {
        *pair(c, i) = (cg_selected_pair){.instruction = i, .kind = SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction = next, .producer = i,
                                          .kind = SELECT_SHIFTED_BINARY};
        continue;
      }
      const char *op = c->tokens[i->begin + 2].text;
      const char *following = c->tokens[next->begin + 2].text;
      int subtract_product = constants && i->result->type->bits == 32 &&
          next->result->type->bits == 32 && !strcmp(following, "sub") &&
          !strcmp(c->tokens[next->begin + 6].text, i->result->name);
      if (!strcmp(op, "mul") && (!strcmp(following, "add") || subtract_product) &&
          cg_register_lookup(c, i->result->name)->selection_uses == 1 &&
          (!strcmp(c->tokens[next->begin + 4].text, i->result->name) ||
           !strcmp(c->tokens[next->begin + 6].text, i->result->name))) {
        *pair(c, i) = (cg_selected_pair){.instruction=i, .kind=SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction=next, .producer=i,
          .kind=subtract_product ? SELECT_MLS : SELECT_MLA};
      } else if (((!strcmp(op, "udiv") && !strcmp(following, "urem")) ||
                  (!strcmp(op, "urem") && !strcmp(following, "udiv")) ||
                  (!strcmp(op, "sdiv") && !strcmp(following, "srem")) ||
                  (!strcmp(op, "srem") && !strcmp(following, "sdiv"))) &&
                 !strcmp(c->tokens[i->begin + 4].text,
                         c->tokens[next->begin + 4].text) &&
                 !strcmp(c->tokens[i->begin + 6].text,
                         c->tokens[next->begin + 6].text))
        *pair(c, next) = (cg_selected_pair){.instruction=next, .producer=i, .kind=SELECT_DIVISION};
    }
  signed_products(c);
}
cg_inst *cg_selection_extended_load(cg_context *c, cg_inst *i) {
  if (!c->selection) return NULL;
  cg_selected_pair *selected = pair(c, i);
  return selected->kind == SELECT_EXTENDED_LOAD ? selected->producer : NULL;
}
cg_inst *cg_selection_branch_compare(cg_context *c, cg_inst *i) {
  if (!c->selection) return NULL;
  cg_selected_pair *selected = pair(c, i);
  return selected->kind == SELECT_COMPARE_BRANCH ? selected->producer : NULL;
}
cg_inst *cg_selection_countdown(cg_context *c, cg_inst *i) {
  return c->selection ? pair(c, i)->countdown : NULL;
}
int cg_selection_barrier(cg_context *c, cg_inst *i) {
  if (!c->selection)
    return 0;
  unsigned kind = pair(c, i)->kind;
  return kind == SELECT_SKIP || kind == SELECT_MLA || kind == SELECT_MLS;
}
static void read(cg_context *c, cg_inst *i, unsigned operand, unsigned reg) {
  c->pos = i->begin + operand;
  cg_value_read(c, i->result->type, reg, 0);
}
static int immediate_supported(const char *op, uint32_t n) {
  int arithmetic = !strcmp(op, "add") || !strcmp(op, "sub");
  int logical = !strcmp(op, "and") || !strcmp(op, "or") || !strcmp(op, "xor");
  int shift = !strcmp(op, "shl") || !strcmp(op, "lshr") || !strcmp(op, "ashr");
  return (arithmetic && (!n || n <= 4095 || thumb_modified(n) >= 0 ||
                        ((n & 0x80000000u) &&
                         (0u - n <= 4095 || thumb_modified(0u - n) >= 0)))) ||
         (logical && thumb_modified(n) >= 0) || (shift && n < 32);
}
static int immediate(cg_context *c, const char *op, uint32_t n,
                     unsigned dst, unsigned src) {
  if (!strcmp(op, "add") || !strcmp(op, "sub")) {
    if (!n) {
      if (dst != src)
        cg_move(c, dst, src);
      return 1;
    }
    if (cg_compact_selection(c)) {
      uint32_t magnitude = n;
      const char *compact_op = op;
      if (n & 0x80000000u) {
        magnitude = 0u - n;
        compact_op = !strcmp(op, "add") ? "sub" : "add";
      }
      if (magnitude <= 255) {
        badge_cost_range bytes =
            badge_target_add_bytes(dst, src, magnitude, 1, 1);
        if (bytes.known && bytes.min == 2) {
          cg_emit(c, "%ss.n r%u, r%u, #%" PRIu32 "\n",
                  compact_op, dst, src, magnitude);
          return 1;
        }
      }
    }
    if (thumb_modified(n) >= 0)
      cg_machine(c, !strcmp(op, "add") ? CG_M_ADD_IMM : CG_M_SUB_IMM,
                 dst, src, 0, 0, n);
    else if (n <= 4095)
      cg_machine(c, !strcmp(op, "add") ? CG_M_ADDW : CG_M_SUBW,
                 dst, src, 0, 0, n);
    else if ((n & 0x80000000u) && thumb_modified(0u - n) >= 0)
      cg_machine(c, !strcmp(op, "add") ? CG_M_SUB_IMM : CG_M_ADD_IMM,
                 dst, src, 0, 0, 0u - n);
    else if ((n & 0x80000000u) && 0u - n <= 4095)
      cg_machine(c, !strcmp(op, "add") ? CG_M_SUBW : CG_M_ADDW,
                 dst, src, 0, 0, 0u - n);
    else
      return 0;
    return 1;
  }
  const char *ins = !strcmp(op, "and")   ? "and"
                    : !strcmp(op, "or")  ? "orr"
                    : !strcmp(op, "xor") ? "eor"
                                         : NULL;
  if (ins && thumb_modified(n) >= 0) {
    cg_machine(c, !strcmp(op, "and") ? CG_M_AND_IMM :
                  !strcmp(op, "or") ? CG_M_ORR_IMM : CG_M_EOR_IMM,
               dst, src, 0, 0, n);
    return 1;
  }
  ins = !strcmp(op, "shl")    ? "lsl"
        : !strcmp(op, "lshr") ? "lsr"
        : !strcmp(op, "ashr") ? "asr"
                              : NULL;
  if (ins && n < 32) {
    if (n && cg_schedule_selection(c))
      cg_machine(c, !strcmp(op, "shl") ? CG_M_LSL_IMM :
                    !strcmp(op, "lshr") ? CG_M_LSR_IMM : CG_M_ASR_IMM,
                 dst, src, 0, 0, n);
    else if (n)
      cg_emit(c, "%s%s.%s r%u, r%u, #%" PRIu32 "\n", ins,
              cg_compact_selection(c) && dst < 8 && src < 8 ? "s" : "",
              cg_compact_selection(c) && dst < 8 && src < 8 ? "n" : "w",
              dst, src, n);
    else if (dst != src)
      cg_move(c, dst, src);
    return 1;
  }
  return 0;
}
static int register_operands(cg_context *c) {
  return cg_register_selection(c) &&
         cg_global_residency(c);
}
static unsigned typed_operand(cg_context *c, cg_inst *i, unsigned at,
                              unsigned scratch, cg_type *type) {
  unsigned reg = cg_global_register(c, c->tokens[i->begin + at].text);
  if (reg) {
    c->avoided_loads++;
    return reg;
  }
  c->pos = i->begin + at;
  cg_value_read(c, type, scratch, 0);
  return scratch;
}
static unsigned operand(cg_context *c, cg_inst *i, unsigned at,
                        unsigned scratch) {
  return typed_operand(c, i, at, scratch, i->result->type);
}
static unsigned destination(cg_context *c, cg_inst *i) {
  unsigned reg = cg_global_register(c, i->result->name);
  return reg ? reg : 0;
}
int cg_selection_boolean(cg_context *c, cg_inst *i, cg_type *type) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      !register_operands(c) || i->end - i->begin != 8 ||
      strcmp(c->tokens[i->begin + 4].text, "i32") ||
      strcmp(c->tokens[i->begin + 6].text, ",") ||
      pair(c, i)->kind == SELECT_MASK_TEST)
    return 0;
  const char *pred = c->tokens[i->begin + 3].text;
  const char *a = c->tokens[i->begin + 5].text, *b = c->tokens[i->begin + 7].text;
  unsigned at = !strcmp(b, "0") && *a == '%' ? 5 :
                !strcmp(a, "0") && *b == '%' ? 7 : 0;
  if (!at)
    return 0;
  if (strcmp(pred, "eq"))
    return 0;
  unsigned src = typed_operand(c, i, at, 0, type), dst = destination(c, i);
  cg_emit(c, "clz r%u, r%u\nlsr.w r%u, r%u, #5\n", dst, src, dst, dst);
  cg_save_normalized(c, i->result, dst);
  return 1;
}
int cg_selection_compare_operands(cg_context *c, cg_inst *i, cg_type *type) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      !register_operands(c) || i->end - i->begin != 8 ||
      strcmp(c->tokens[i->begin + 6].text, ","))
    return 0;
  cg_selected_pair *selected = pair(c, i);
  cg_inst *mask = selected->kind == SELECT_MASK_TEST ||
                  selected->kind == SELECT_COMPARE_SKIP ? selected->producer : NULL;
  if (mask) {
    uint32_t value;
    const char *a = c->tokens[mask->begin + 4].text;
    const char *b = c->tokens[mask->begin + 6].text;
    if (number(b, &value))
      cg_emit(c, "tst.w r%u, #%" PRIu32 "\n", operand(c, mask, 4, 0), value);
    else if (number(a, &value))
      cg_emit(c, "tst.w r%u, #%" PRIu32 "\n", operand(c, mask, 6, 0), value);
    else {
      unsigned left = operand(c, mask, 4, 0), right = operand(c, mask, 6, 1);
      cg_emit(c, "tst.%c r%u, r%u\n", left < 8 && right < 8 ? 'n' : 'w', left, right);
    }
    return 1;
  }
  const char *kind = c->tokens[i->begin + 4].text;
  const char *pred = c->tokens[i->begin + 3].text;
  int narrow = !strcmp(kind, "i8") || !strcmp(kind, "i16");
  if (narrow ? pred[0] == 's' : strcmp(kind, "i32") && strcmp(kind, "ptr"))
    return 0;
  unsigned a = 5, b = 7;
  uint32_t unused;
  if ((!strcmp(pred, "eq") || !strcmp(pred, "ne")) &&
      c->tokens[i->begin + b].text[0] == '%' &&
      (number(c->tokens[i->begin + a].text, &unused) ||
       !strcmp(c->tokens[i->begin + a].text, "null"))) {
    a = 7;
    b = 5;
  }
  unsigned left = typed_operand(c, i, a, 0, type);
  const char *token = c->tokens[i->begin + b].text;
  uint32_t n;
  int constant = number(token, &n);
  if (!strcmp(token, "null")) {
    n = 0;
    constant = 1;
  }
  if (constant && narrow)
    n &= (UINT32_C(1) << type->bits) - 1;
  /* CMP x, -k and CMN x, k have the same NZCV unless negation overflows. */
  if (constant && thumb_modified(n) >= 0) {
    cg_emit(c, "cmp.%s r%u, #%" PRIu32 "\n", left < 8 && n <= 255 ? "n" : "w",
            left, n);
  } else if (constant && !strcmp(kind, "i32") && (n & UINT32_C(0x80000000)) &&
             (n != UINT32_C(0x80000000) || !strcmp(pred, "eq") || !strcmp(pred, "ne")) &&
             thumb_modified(0u - n) >= 0) {
    cg_emit(c, "cmn.w r%u, #%" PRIu32 "\n", left, 0u - n);
  } else {
    unsigned right = typed_operand(c, i, b, 1, type);
    cg_emit(c, "cmp.n r%u, r%u\n", left, right);
  }
  return 1;
}
static int register_instruction(cg_context *c, cg_inst *i) {
  if (!register_operands(c) || !scalar(c, i))
    return 0;
  const char *op = c->tokens[i->begin + 2].text;
  const char *ins = !strcmp(op, "add")    ? "add.w"
                    : !strcmp(op, "sub")  ? "sub.w"
                    : !strcmp(op, "mul")  ? "mul"
                    : !strcmp(op, "and")  ? "and.w"
                    : !strcmp(op, "or")   ? "orr.w"
                    : !strcmp(op, "xor")  ? "eor.w"
                    : !strcmp(op, "shl")  ? "lsl.w"
                    : !strcmp(op, "lshr") ? "lsr.w"
                    : !strcmp(op, "ashr") ? "asr.w"
                                          : NULL;
  int divide = !strcmp(op, "sdiv") || !strcmp(op, "udiv") ||
               !strcmp(op, "srem") || !strcmp(op, "urem");
  if (!ins && !divide)
    return 0;
  unsigned dst = destination(c, i), at = 4;
  uint32_t n, initial;
  int shift_multiply = c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
      !strcmp(op, "mul") && i->result->type->bits == 32;
  int constant = number(c->tokens[i->begin + 6].text, &n);
  if (!constant && (!strcmp(op, "add") || !strcmp(op, "and") ||
                    !strcmp(op, "or") || !strcmp(op, "xor") || shift_multiply)) {
    constant = number(c->tokens[i->begin + 4].text, &n);
    at = 6;
  }
  unsigned width = constant ? low_mask_width(n) : 0;
  if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
      i->result->type->bits == 32 && constant && !strcmp(op, "and") &&
      (n == 255 || n == 65535)) {
    unsigned src = operand(c, i, at, 0);
    cg_emit(c, "uxt%c.%c r%u, r%u\n", n == 255 ? 'b' : 'h',
            dst < 8 && src < 8 ? 'n' : 'w', dst, src);
  } else if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
      i->result->type->bits == 32 && !strcmp(op, "sub") &&
      number(c->tokens[i->begin + 4].text, &initial) && thumb_modified(initial) >= 0) {
    unsigned src = operand(c, i, 6, 0);
    cg_emit(c, "rsb.w r%u, r%u, #%" PRIu32 "\n", dst, src, initial);
  } else if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
      i->result->type->bits == 32 && constant && !strcmp(op, "and") &&
      thumb_modified(n) < 0 && thumb_modified(~n) >= 0) {
    unsigned src = operand(c, i, at, 0);
    cg_emit(c, "bic.w r%u, r%u, #%" PRIu32 "\n", dst, src, ~n);
  } else if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
      i->result->type->bits == 32 && width && !strcmp(op, "and") &&
      thumb_modified(n) < 0) {
    unsigned src = operand(c, i, at, 0);
    cg_emit(c, "ubfx r%u, r%u, #0, #%u\n", dst, src, width);
  } else if (shift_multiply && constant && power_of_two(n)) {
    unsigned src = operand(c, i, at, 0);
    if (n == 1) cg_move(c, dst, src);
    else cg_emit(c, "lsl.w r%u, r%u, #%u\n", dst, src, (unsigned)__builtin_ctz(n));
    c->selection->immediates++;
  } else if (shift_multiply && constant && power_of_two(n - 1)) {
    unsigned src = operand(c, i, at, 0);
    cg_emit(c, "add.w r%u, r%u, r%u, lsl #%u\n", dst, src, src,
            (unsigned)__builtin_ctz(n - 1));
  } else if (shift_multiply && constant && n > 2 && power_of_two(n + 1)) {
    unsigned src = operand(c, i, at, 0);
    cg_emit(c, "rsb.w r%u, r%u, r%u, lsl #%u\n", dst, src, src,
            (unsigned)__builtin_ctz(n + 1));
  } else if (constant && immediate_supported(op, n)) {
    unsigned src = operand(c, i, at, 0);
    if (!immediate(c, op, n, dst, src))
      cg_error(c, "register immediate eligibility mismatch");
    c->selection->immediates++;
  } else {
    unsigned left = operand(c, i, 4, 0), right = operand(c, i, 6, 2);
    if (divide) {
      int remainder = strstr(op, "rem") != NULL;
      const cg_selected_pair *consumer = i->next ? pair(c, i->next) : NULL;
      int shared = consumer && consumer->kind == SELECT_DIVISION && consumer->producer == i;
      unsigned quotient = c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
          !remainder && (!shared || cg_global_register(c, i->result->name)) ? dst : 4;
      cg_emit(c, "%s r%u, r%u, r%u\n", op[0] == 's' ? "sdiv" : "udiv",
              quotient, left, right);
      if (remainder)
        cg_emit(c, "mls r%u, r4, r%u, r%u\n", dst, right, left);
      else if (quotient != dst)
        cg_emit(c, "mov r%u, r4\n", dst);
    } else if (!cg_compact_binary(c, op, dst, left, right)) {
      cg_machine_opcode machine = !strcmp(op, "add") ? CG_M_ADD :
          !strcmp(op, "sub") ? CG_M_SUB : !strcmp(op, "mul") ? CG_M_MUL :
          !strcmp(op, "and") ? CG_M_AND : !strcmp(op, "or") ? CG_M_ORR :
          !strcmp(op, "xor") ? CG_M_EOR : !strcmp(op, "shl") ? CG_M_LSL :
          !strcmp(op, "lshr") ? CG_M_LSR : CG_M_ASR;
      cg_machine(c, machine, dst, left, right, 0, 0);
    }
  }
  cg_save(c, i->result, dst, 0);
  c->selection->register_operations++;
  return 1;
}
int cg_selection_instruction(cg_context *c, cg_inst *i) {
  if (!c->selection)
    return 0;
  cg_selected_pair *selected = pair(c, i);
  if (selected->kind == SELECT_PRODUCT_CAST) {
    c->pos = i->begin + 4;
    cg_value_read(c, NULL, 0, 0);
    cg_save_normalized(c, i->result, 0);
    return 1;
  }
  if (selected->kind == SELECT_SIGNED_PRODUCT) {
    if (i->result->type->bits == 64) {
      read(c, i, 4, 0);
      read(c, i, 6, 2);
      cg_emit(c, "smull r0, r1, r0, r2\n");
      cg_save(c, i->result, 0, 0);
      cg_save(c, i->result, 1, 1);
    } else {
      unsigned a = operand(c, i, 4, 0), b = operand(c, i, 6, 2);
      unsigned dst = destination(c, i);
      cg_emit(c, "smulbb r%u, r%u, r%u\n", dst, a, b);
      cg_save_normalized(c, i->result, dst);
    }
    return 1;
  }
  if (selected->kind == SELECT_WRAPPER_CALL) {
    cg_emit(c, "bl %s\n@ ordered-wrapper: incoming r0 forwarded\n",
            cg_name(c, c->tokens[i->begin + 2].text));
    if (c->selection->wrapper_value_register == 2)
      cg_emit(c, "@ ordered-wrapper: r2 retained through proven internal call\n");
    return 1;
  }
  if (selected->kind == SELECT_WRAPPER_STORE) {
    uint32_t address, value;
    const char *source;
    if (!absolute_store(c, i, &address, &source) || !number(source, &value))
      cg_error(c, "ordered wrapper store eligibility changed");
    if (!c->selection->absolute_base_emitted) {
      cg_immediate(c, 4, c->selection->absolute_base);
      c->selection->absolute_base_emitted = 1;
    }
    unsigned reg = c->selection->wrapper_value_register;
    if (reg != 2 || !c->selection->wrapper_value_emitted) {
      if (value <= 255) cg_emit(c, "movs.n r%u, #%" PRIu32 "\n", reg, value);
      else cg_immediate(c, reg, value);
      c->selection->wrapper_value_emitted = 1;
    }
    cg_emit(c, "str.n r%u, [r4, #%" PRIu32 "]\n@ ordered-wrapper: shared preserved base\n",
            reg, address - c->selection->absolute_base);
    return 1;
  }
  if (selected->kind == SELECT_ABSOLUTE_STORE) {
    unsigned type = i->begin + 1;
    if (!strcmp(c->tokens[type].text, "volatile")) type++;
    uint32_t address, value;
    if (!absolute_pointer(c, c->tokens[type + 4].text, &address))
      cg_error(c, "absolute store eligibility changed");
    if (!c->selection->absolute_base_emitted) {
      cg_immediate(c, 1, c->selection->absolute_base);
      c->selection->absolute_base_emitted = 1;
    }
    unsigned reg = cg_global_register(c, c->tokens[type + 1].text);
    if (!reg) {
      reg = 0;
      if (!number(c->tokens[type + 1].text, &value))
        cg_error(c, "absolute store value is not resident");
      if (value <= 255) cg_emit(c, "movs.n r0, #%" PRIu32 "\n", value);
      else cg_immediate(c, 0, value);
    }
    cg_emit(c, "str.%s r%u, [r1, #%" PRIu32 "]\n@ absolute-store: shared base\n",
            reg < 8 ? "n" : "w", reg, address - c->selection->absolute_base);
    return 1;
  }
  if (selected->kind == SELECT_SKIP || selected->kind == SELECT_COMPARE_SKIP ||
      selected->kind == SELECT_COUNT_SKIP)
    return 1;
  if (selected->kind == SELECT_SIGNED_FIELD) {
    cg_inst *shift = selected->producer;
    uint32_t offset;
    if (!number(c->tokens[shift->begin + 6].text, &offset))
      cg_error(c, "signed field eligibility changed");
    unsigned src = operand(c, shift, 4, 0), dst = destination(c, i);
    cg_emit(c, "sbfx r%u, r%u, #%" PRIu32 ", #16\n", dst, src, offset);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_DUAL_PRODUCT) {
    cg_inst *xor = selected->producer;
    unsigned at;
    uint32_t difference;
    if (!commutative_literal(c, xor, &at, &difference) || !difference)
      cg_error(c, "dual product overflow proof changed");
    unsigned left = operand(c, xor, at, 0);
    unsigned right = operand(c, xor->next->next->next, 4, 1);
    unsigned dst = destination(c, i);
    cg_emit(c, "smuad r%u, r%u, r%u\n", dst, left, right);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_ROTATE || selected->kind == SELECT_ROTATED_XOR) {
    cg_inst *first = selected->producer, *combine = first->next->next;
    uint32_t amount;
    if (!rotate_amount(c, first, first->next, combine, &amount))
      cg_error(c, "rotate eligibility changed");
    unsigned source = operand(c, first, 4, 0), dst = destination(c, i);
    if (selected->kind == SELECT_ROTATED_XOR) {
      unsigned base = operand(c, i, 10 - binary_use_of(c, i, combine), 1);
      cg_emit(c, "eor.w r%u, r%u, r%u, ror #%" PRIu32 "\n", dst, base, source, amount);
    } else
      cg_emit(c, "ror.w r%u, r%u, #%" PRIu32 "\n", dst, source, amount);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_BITFIELD_INSERT) {
    cg_inst *clear = selected->producer, *shift = clear->next;
    uint32_t lsb;
    unsigned base, width;
    if (!inserted_field(c, clear, shift, shift->next, i, &base, &lsb, &width))
      cg_error(c, "bitfield insertion eligibility changed");
    unsigned field = operand(c, shift, 4, 0);
    unsigned original = operand(c, clear, base, 1), dst = destination(c, i);
    if (dst == field && dst != original) {
      cg_move(c, 12, field);
      field = 12;
    }
    if (dst != original)
      cg_move(c, dst, original);
    cg_emit(c, "bfi r%u, r%u, #%" PRIu32 ", #%u\n", dst, field, lsb, width);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_BITFIELD) {
    uint32_t lsb;
    unsigned width;
    if (!bitfield(c, selected->producer, i, &lsb, &width))
      cg_error(c, "bitfield eligibility changed");
    unsigned src = operand(c, selected->producer, 4, 0);
    unsigned dst = destination(c, i);
    cg_emit(c, "ubfx r%u, r%u, #%" PRIu32 ", #%u\n", dst, src, lsb, width);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_INVERTED_BINARY) {
    unsigned source, other;
    cg_inst *producer = selected->producer;
    const char *mnemonic = inverted_binary(c, producer, i, &source, &other);
    if (!mnemonic)
      cg_error(c, "inverted operand eligibility changed");
    unsigned right = operand(c, producer, source, 0);
    unsigned left = operand(c, i, other, 1), dst = destination(c, i);
    cg_emit(c, "%s.w r%u, r%u, r%u\n", mnemonic, dst, left, right);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_SHIFTED_BINARY) {
    uint32_t amount;
    unsigned source, other;
    cg_inst *producer = selected->producer;
    const char *shift = shifted_binary(c, producer, i, &amount, &source, &other);
    if (!shift)
      cg_error(c, "shifted operand eligibility changed");
    const char *op = c->tokens[i->begin + 2].text;
    const char *mnemonic = !strcmp(op, "xor") ? "eor" : !strcmp(op, "or") ? "orr" :
                           !strcmp(op, "sub") && other == 6 ? "rsb" : op;
    unsigned src = operand(c, producer, source, 0);
    unsigned base = operand(c, i, other, 1);
    unsigned dst = destination(c, i);
    cg_emit(c, "%s.w r%u, r%u, r%u, %s #%" PRIu32 "\n",
            mnemonic, dst, base, src, shift, amount);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_MLS) {
    unsigned left = operand(c, selected->producer, 4, 0);
    unsigned right = operand(c, selected->producer, 6, 2);
    unsigned base = operand(c, i, 4, 1), dst = destination(c, i);
    cg_emit(c, "mls r%u, r%u, r%u, r%u\n", dst, left, right, base);
    cg_save_normalized(c, i->result, dst);
    return 1;
  }
  if (selected->kind == SELECT_MLA) {
    if (register_operands(c)) {
      unsigned at =
          !strcmp(c->tokens[i->begin + 4].text, selected->producer->result->name)
              ? 6
              : 4;
      if (c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS &&
          !strcmp(c->tokens[selected->producer->begin + 3].text, "i32")) {
        uint32_t scale;
        unsigned variable = 4;
        int constant = number(c->tokens[selected->producer->begin + 6].text, &scale);
        if (!constant) {
          constant = number(c->tokens[selected->producer->begin + 4].text, &scale);
          variable = 6;
        }
        if (constant && scale && !(scale & (scale - 1))) {
          unsigned shift = 0;
          while ((scale >>= 1)) shift++;
          unsigned src = operand(c, selected->producer, variable, 0);
          unsigned base = operand(c, i, at, 1), dst = destination(c, i);
          cg_emit(c, "add.w r%u, r%u, r%u, lsl #%u\n", dst, base, src, shift);
          cg_save(c, i->result, dst, 0);
          c->selection->shifted_addresses++;
          c->selection->register_operations++;
          return 1;
        }
      }
      unsigned left = operand(c, selected->producer, 4, 0);
      unsigned right = operand(c, selected->producer, 6, 2);
      unsigned addend = operand(c, i, at, 1), dst = destination(c, i);
      cg_machine(c, CG_M_MLA, dst, left, right, addend, 0);
      cg_save(c, i->result, dst, 0);
      c->selection->multiply_adds++;
      c->selection->register_operations++;
      return 1;
    }
    read(c, selected->producer, 4, 0);
    read(c, selected->producer, 6, 2);
    unsigned addend =
        !strcmp(c->tokens[i->begin + 4].text, selected->producer->result->name)
            ? 6
            : 4;
    read(c, i, addend, 1);
    cg_machine(c, CG_M_MLA, 0, 0, 2, 1, 0);
    cg_save(c, i->result, 0, 0);
    c->selection->multiply_adds++;
    return 1;
  }
  if (selected->kind == SELECT_DIVISION) {
    const char *op = c->tokens[i->begin + 2].text;
    if (register_operands(c)) {
      unsigned dst = destination(c, i);
      if (strstr(op, "rem")) {
        unsigned left = operand(c, i, 4, 0), right = operand(c, i, 6, 2);
        unsigned quotient = c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS ?
            cg_global_register(c, selected->producer->result->name) : 0;
        cg_emit(c, "mls r%u, r%u, r%u, r%u\n", dst, quotient ? quotient : 4, right, left);
      } else
        cg_emit(c, "mov r%u, r4\n", dst);
      cg_save(c, i->result, dst, 0);
      c->selection->shared_divisions++;
      c->selection->register_operations++;
      return 1;
    }
    if (strstr(op, "rem")) {
      read(c, i, 4, 0);
      read(c, i, 6, 2);
      cg_emit(c, "mls r0, r4, r2, r0\n");
    } else
      cg_emit(c, "mov r0, r4\n");
    cg_save(c, i->result, 0, 0);
    c->selection->shared_divisions++;
    return 1;
  }
  if (register_instruction(c, i))
    return 1;
  if (!scalar(c, i))
    return 0;
  const char *op = c->tokens[i->begin + 2].text;
  unsigned operand = 4;
  uint32_t n;
  if (!number(c->tokens[i->begin + 6].text, &n)) {
    if ((strcmp(op, "add") && strcmp(op, "mul") && strcmp(op, "and") &&
         strcmp(op, "or") && strcmp(op, "xor")) ||
        !number(c->tokens[i->begin + 4].text, &n))
      return 0;
    operand = 6;
  }
  /* Probe before reading: an ineligible pattern must preserve cache state. */
  if (!immediate_supported(op, n))
    return 0;
  read(c, i, operand, 0);
  if (!immediate(c, op, n, 0, 0))
    cg_error(c, "scalar immediate eligibility mismatch");
  cg_save(c, i->result, 0, 0);
  c->selection->immediates++;
  return 1;
}
void cg_add_offset(cg_context *c, unsigned dst, unsigned base, uint32_t offset) {
  if (!immediate(c, "add", offset, dst, base)) {
    unsigned scratch = base == 0 ? 1 : 0;
    cg_immediate(c, scratch, offset);
    cg_emit(c, "add.w r%u, r%u, r%u\n", dst, base, scratch);
  }
}
int cg_selection_offset(cg_context *c, uint32_t offset) {
  if (!c->selection)
    return 0;
  cg_add_offset(c, 0, 0, offset);
  c->selection->constant_addresses++;
  return 1;
}
int cg_selection_index(cg_context *c, cg_type *type, unsigned stride) {
  if (!c->selection || type->bits != 32 || type->size != 4)
    return 0;
  uint32_t index;
  if (number(c->tokens[c->pos].text, &index)) {
    c->pos++;
    return cg_selection_offset(c, index * stride);
  }
  if (stride > 1 && !(stride & (stride - 1))) {
    unsigned shift = 0;
    while ((stride >> shift) != 1)
      shift++;
    cg_value_read(c, type, 1, 0);
    cg_emit(c, "add.w r0, r0, r1, lsl #%u\n", shift);
    c->selection->shifted_addresses++;
    return 1;
  }
  return 0;
}
void cg_selection_finish(cg_context *c) {
  if (c->selection)
    cg_emit(c,
            "@ scalar-selection: immediates %u shifted-addresses %u "
            "constant-addresses %u multiply-adds %u shared-divisions %u\n",
            c->selection->immediates, c->selection->shifted_addresses,
            c->selection->constant_addresses, c->selection->multiply_adds,
            c->selection->shared_divisions);
  if (c->selection && cg_register_selection(c))
    cg_emit(c, "@ register-operands: operations %u\n",
            c->selection->register_operations);
  if (c->selection && cg_branch_selection(c))
    cg_emit(c, "@ compare-branch: fused %u\n", c->selection->compare_branches);
}
