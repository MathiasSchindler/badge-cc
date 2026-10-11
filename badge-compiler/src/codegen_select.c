#include "codegen_internal.h"
#include "thumb_immediate.h"
#include "target_cost.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

enum { SELECT_SKIP = 1, SELECT_MLA, SELECT_DIVISION, SELECT_COMPARE_SKIP, SELECT_COMPARE_BRANCH, SELECT_COUNT_SKIP, SELECT_ABSOLUTE_STORE, SELECT_WRAPPER_STORE, SELECT_WRAPPER_CALL };
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
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i && i->next; i = i->next) {
      cg_register_work(c);
      cg_inst *next = i->next;
      if (cg_branch_selection(c) &&
          compare_profitable &&
          c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT &&
          (cg_effect_selection(c) || !assembly) && i->result &&
          i->result->type->bits == 1 && i->end - i->begin == 8 &&
          !strcmp(c->tokens[i->begin + 2].text, "icmp") &&
          cg_comparison_condition(c->tokens[i->begin + 3].text) &&
          (!strcmp(c->tokens[i->begin + 4].text, "i32") ||
           (cg_control_selection(c) &&
            (!strcmp(c->tokens[i->begin + 4].text, "i8") ||
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
              !(cg_control_selection(c) && !strcmp(s, "null"))) operands = 0;
        }
        if (operands) {
          *pair(c, i) = (cg_selected_pair){.instruction=i, .kind=SELECT_COMPARE_SKIP};
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
      if (!scalar(c, i) || !scalar(c, next) || pair(c, i)->instruction ||
          pair(c, next)->instruction)
        continue;
      const char *op = c->tokens[i->begin + 2].text;
      const char *following = c->tokens[next->begin + 2].text;
      if (!strcmp(op, "mul") && !strcmp(following, "add") &&
          cg_register_lookup(c, i->result->name)->selection_uses == 1 &&
          (!strcmp(c->tokens[next->begin + 4].text, i->result->name) ||
           !strcmp(c->tokens[next->begin + 6].text, i->result->name))) {
        *pair(c, i) = (cg_selected_pair){.instruction=i, .kind=SELECT_SKIP};
        *pair(c, next) = (cg_selected_pair){.instruction=next, .producer=i, .kind=SELECT_MLA};
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
  return kind == SELECT_SKIP || kind == SELECT_MLA;
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
int cg_selection_compare_operands(cg_context *c, cg_inst *i, cg_type *type) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS ||
      !register_operands(c) ||
      (strcmp(c->tokens[i->begin + 4].text, "i32") &&
       strcmp(c->tokens[i->begin + 4].text, "ptr")))
    return 0;
  unsigned left = typed_operand(c, i, 5, 0, type);
  uint32_t n;
  if (number(c->tokens[i->begin + 7].text, &n) && thumb_modified(n) >= 0) {
    cg_emit(c, "cmp.%s r%u, #%" PRIu32 "\n", left < 8 && n <= 255 ? "n" : "w",
            left, n);
  } else {
    unsigned right = typed_operand(c, i, 7, 1, type);
    cg_emit(c, "cmp.%s r%u, r%u\n", left < 8 && right < 8 ? "n" : "w",
            left, right);
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
  uint32_t n;
  int constant = number(c->tokens[i->begin + 6].text, &n);
  if (!constant && (!strcmp(op, "add") || !strcmp(op, "and") ||
                    !strcmp(op, "or") || !strcmp(op, "xor"))) {
    constant = number(c->tokens[i->begin + 4].text, &n);
    at = 6;
  }
  if (constant && immediate_supported(op, n)) {
    unsigned src = operand(c, i, at, 0);
    if (!immediate(c, op, n, dst, src))
      cg_error(c, "register immediate eligibility mismatch");
    c->selection->immediates++;
  } else {
    unsigned left = operand(c, i, 4, 0), right = operand(c, i, 6, 2);
    if (divide) {
      /* Adjacent div/rem selection retains its quotient in r4. */
      cg_emit(c, "%s r4, r%u, r%u\n", op[0] == 's' ? "sdiv" : "udiv",
              left, right);
      if (strstr(op, "rem"))
        cg_emit(c, "mls r%u, r4, r%u, r%u\n", dst, right, left);
      else
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
        cg_emit(c, "mls r%u, r4, r%u, r%u\n", dst, right, left);
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
int cg_selection_offset(cg_context *c, uint32_t offset) {
  if (!c->selection)
    return 0;
  if (!immediate(c, "add", offset, 0, 0)) {
    cg_immediate(c, 1, offset);
    cg_emit(c, "add.w r0, r0, r1\n");
  }
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
