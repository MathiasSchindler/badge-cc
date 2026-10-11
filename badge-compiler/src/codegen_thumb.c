#include "codegen_internal.h"
#include <inttypes.h>
#include <string.h>

/* Only callers at an outside-IT scalar boundary with dead APSR may use this. */
int cg_compact_binary(cg_context *c, const char *op, unsigned dst,
                      unsigned left, unsigned right) {
  if (cg_width_selection(c) && !strcmp(op, "add") &&
      dst <= 12 && left <= 12 && right <= 12 &&
      (dst == left || dst == right)) {
    cg_emit(c, "add.n r%u, r%u\n", dst, dst == left ? right : left);
    return 1;
  }
  if (!cg_compact_selection(c) || dst > 12 || left > 12 || right > 12)
    return 0;
  if (!strcmp(op, "add") && (dst == left || dst == right)) {
    cg_emit(c, "add.n r%u, r%u\n", dst, dst == left ? right : left);
    return 1;
  }
  if (dst >= 8 || left >= 8 || right >= 8)
    return 0;
  if (!strcmp(op, "add") || !strcmp(op, "sub")) {
    cg_emit(c, "%ss.n r%u, r%u, r%u\n", op, dst, left, right);
    return 1;
  }
  int commutative = !strcmp(op, "and") || !strcmp(op, "or") ||
                    !strcmp(op, "xor") || !strcmp(op, "mul");
  if (commutative && dst == right) {
    unsigned swap = left;
    left = right;
    right = swap;
  }
  if (dst != left)
    return 0;
  const char *ins = !strcmp(op, "and") ? "ands" :
                    !strcmp(op, "or") ? "orrs" :
                    !strcmp(op, "xor") ? "eors" :
                    !strcmp(op, "mul") ? "muls" :
                    !strcmp(op, "shl") ? "lsls" :
                    !strcmp(op, "lshr") ? "lsrs" :
                    !strcmp(op, "ashr") ? "asrs" : NULL;
  if (!ins)
    return 0;
  cg_emit(c, "%s.n r%u, r%u, r%u\n", ins, dst, left, right);
  return 1;
}

void cg_immediate(cg_context *c, unsigned r, uint32_t n) {
  cg_machine(c, CG_M_MOVW, r, 0, 0, 0, n & 65535);
  if (n >> 16)
    cg_machine(c, CG_M_MOVT, r, 0, 0, 0, n >> 16);
}
void cg_address(cg_context *c, unsigned r, unsigned offset) {
  cg_immediate(c, r, offset);
  cg_emit(c, "add.w r%u, %s, r%u\n", r, c->frame_sp_relative ? "sp" : "r7", r);
}
void cg_stack(cg_context *c, int store, unsigned r, unsigned offset) {
  if (c->frame_addressing == BADGE_FRAME_DIRECT && offset <= 4095 &&
      offset % 4 == 0) {
    if (!store && cg_schedule_selection(c)) {
      cg_machine(c, CG_M_LOAD_FRAME, r, 0, 0, 0, offset);
      return;
    }
    cg_emit(c, "%s.%s r%u, [%s, #%u]\n", store ? "str" : "ldr",
            cg_narrow_memory_selection(c) && r < 8 &&
                offset <= (c->frame_sp_relative ? 1020u : 124u) ? "n" : "w",
            r, c->frame_sp_relative ? "sp" : "r7", offset);
    return;
  }
  cg_address(c, 12, offset);
  cg_emit(c, "%s.w r%u, [r12]\n", store ? "str" : "ldr", r);
}
void cg_symbol(cg_context *c, unsigned r, const char *name, int64_t addend) {
  if (cg_relax_selection(c) || cg_operand_selection(c)) {
    if (addend >= -32768 && addend <= 32767) {
      cg_emit(c, "movw r%u, #:lower16:(%s%+" PRId64 ")\n"
                 "movt r%u, #:upper16:(%s%+" PRId64 ")\n",
              r, name, addend, r, name, addend);
      c->layout_addresses++;
      return;
    }
    c->layout_literal_fallbacks++;
  }
  unsigned id = ++c->unique;
  cg_emit(c,
          "ldr%s r%u, .Lcg_lit%u\nb%s .Lcg_after%u\n.p2align "
          "2\n.Lcg_lit%u:\n.word %s%+" PRId64 "\n.Lcg_after%u:\n",
          cg_width_selection(c) ? "" : ".w", r, id,
          cg_width_selection(c) ? cg_branch_width(c) : ".w",
          id, id, name, addend, id);
}
void cg_normalize(cg_context *c, cg_type *t, unsigned r) {
  if (cg_narrow_memory_selection(c) && r < 8 &&
      (t->bits == 8 || t->bits == 16))
    cg_emit(c, "%s.n r%u, r%u\n", t->bits == 8 ? "uxtb" : "uxth", r, r);
  else if (t->bits && t->bits < 32)
    cg_emit(c, "ubfx r%u, r%u, #0, #%u\n", r, r, t->bits);
}
void cg_save(cg_context *c, cg_value *v, unsigned r, unsigned word) {
  if (!word)
    cg_normalize(c, v->type, r);
  if (cg_register_save(c, v, r, word))
    return;
  if (v->slot == UINT32_MAX)
    cg_error(c, "write requires an omitted scalar home");
  cg_stack(c, 1, r, v->slot + word * 4);
}
void cg_save_normalized(cg_context *c, cg_value *v, unsigned r) {
  if (cg_register_save(c, v, r, 0))
    return;
  if (v->slot == UINT32_MAX)
    cg_error(c, "write requires an omitted scalar home");
  cg_stack(c, 1, r, v->slot);
}
void cg_signed(cg_context *c, cg_type *t, unsigned r) {
  if (t->bits == 1)
    cg_emit(c, "rsb.w r%u, r%u, #0\n", r, r);
  else if (t->bits == 8 || t->bits == 16)
    cg_emit(c, "%s r%u, r%u\n", t->bits == 8 ? "sxtb" : "sxth", r, r);
}
unsigned cg_memory_scratch(cg_type *t, unsigned align) {
  unsigned mask = CG_SCRATCH_R4;
  for (unsigned w = 0; w < cg_round(t->size, 4) / 4; w++) {
    unsigned size = t->size - w * 4;
    if (size > 4)
      size = 4;
    if ((size == 4 && align < 4) || (size == 2 && align < 2) ||
        size == 3)
      mask |= CG_SCRATCH_R5;
  }
  return mask;
}
void cg_memory(cg_context *c, int store, cg_type *t, unsigned align) {
  unsigned words = cg_round(t->size, 4) / 4;
  for (unsigned w = 0; w < words; w++) {
    unsigned size = t->size - w * 4;
    if (size > 4)
      size = 4;
    if (size == 4 && align >= 4)
      cg_emit(c, "%s.%s r%u, [r4, #%u]\n", store ? "str" : "ldr",
              cg_narrow_memory_selection(c) && w < 8 && w * 4 <= 124 ? "n" : "w",
              w, w * 4);
    else if (size == 2 && align >= 2)
      cg_emit(c, "%s r%u, [r4, #%u]\n", store ? "strh" : "ldrh", w, w * 4);
    else if (size == 1)
      cg_emit(c, "%s r%u, [r4, #%u]\n", store ? "strb" : "ldrb", w, w * 4);
    else {
      if (!store)
        cg_immediate(c, w, 0);
      for (unsigned k = 0; k < size; k++) {
        if (store) {
          if (k)
            cg_emit(c, "lsr.w r5, r%u, #%u\n", w, k * 8);
          else
            cg_emit(c, "mov r5, r%u\n", w);
          cg_emit(c, "strb.%s r5, [r4, #%u]\n",
                  cg_narrow_memory_selection(c) && w * 4 + k <= 31 ? "n" : "w",
                  w * 4 + k);
        } else {
          cg_emit(c, "ldrb.%s r5, [r4, #%u]\norr.w r%u, r%u, r5, lsl #%u\n",
                  cg_narrow_memory_selection(c) && w * 4 + k <= 31 ? "n" : "w",
                  w * 4 + k, w, w, k * 8);
        }
      }
    }
  }
}
