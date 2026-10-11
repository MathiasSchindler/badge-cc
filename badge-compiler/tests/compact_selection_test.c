#include "codegen_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned cases;
static void finish(cg_context *c, int selected) {
  assert(selected ? c->size > 0 : c->size == 0);
  printf(".global compact_%u\n.type compact_%u, %%function\n.thumb_func\n"
         "compact_%u:\n%s%s.size compact_%u, .-compact_%u\n",
         cases, cases, cases, selected ? c->out : "nop.w\n",
         "bx lr\n", cases, cases);
  cases++;
  free(c->out);
}
int main(void) {
  puts(".syntax unified\n.cpu cortex-m33\n.thumb\n.text");
  const char *ops[] = {"add", "sub", "and", "or", "xor", "mul",
                       "shl", "lshr", "ashr"};
  for (unsigned op = 0; op < sizeof(ops) / sizeof(*ops); op++)
    for (unsigned dst = 0; dst <= 13; dst++)
      for (unsigned left = 0; left <= 13; left++)
        for (unsigned right = 0; right <= 13; right++) {
          cg_context c = {.scalar_selection = BADGE_SELECTION_THUMB_COMPACT};
          int low = dst < 8 && left < 8 && right < 8;
          int same = dst == left || (op >= 2 && op <= 5 && dst == right);
          int expected = op == 0
              ? (dst <= 12 && left <= 12 && right <= 12 &&
                 (dst == left || dst == right || low))
              : low && (op == 1 || same);
          int selected = cg_compact_binary(&c, ops[op], dst, left, right);
          assert(selected == expected);
          finish(&c, selected);
        }
  for (unsigned store = 0; store < 2; store++)
    for (unsigned reg = 0; reg <= 12; reg++)
      for (unsigned offset = 120; offset <= 132; offset += 4) {
        cg_context c = {.scalar_selection = BADGE_SELECTION_THUMB_COMPACT,
                        .frame_addressing = BADGE_FRAME_DIRECT};
        cg_stack(&c, store, reg, offset);
        assert(strstr(c.out, reg < 8 && offset <= 124 ? ".n " : ".w "));
        finish(&c, 1);
      }
  for (unsigned bits = 8; bits <= 16; bits += 8)
    for (unsigned reg = 0; reg <= 12; reg++) {
      cg_context c = {.scalar_selection = BADGE_SELECTION_THUMB_COMPACT};
      cg_type type = {.bits = bits};
      cg_normalize(&c, &type, reg);
      assert(strstr(c.out, reg < 8 ? ".n " : "ubfx "));
      finish(&c, 1);
    }
  cg_context control = {.scalar_selection = BADGE_SELECTION_THUMB_LAYOUT};
  assert(!cg_compact_binary(&control, "add", 0, 0, 2));
  assert(!control.out);
  fprintf(stderr, "compact selection: %u allocated-register/boundary cases passed\n",
          cases);
}
