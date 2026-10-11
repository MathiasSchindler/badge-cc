#include "codegen_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned cases;
static void finish(cg_context *c) {
  printf("width_%u:\n%s", cases++, c->out);
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
          cg_context c = {.scalar_selection = BADGE_SELECTION_THUMB_WIDTHS};
          int expected = op == 0 && dst <= 12 && left <= 12 && right <= 12 &&
                         (dst == left || dst == right);
          assert(cg_compact_binary(&c, ops[op], dst, left, right) == expected);
          if (expected) {
            assert(!strncmp(c.out, "add.n ", 6));
            finish(&c);
          } else assert(!c.out);
        }
  for (unsigned sp = 0; sp < 2; sp++)
    for (unsigned store = 0; store < 2; store++)
      for (unsigned reg = 0; reg <= 12; reg++)
        for (unsigned offset = 0; offset <= 1024; offset += 4) {
          cg_context c = {.scalar_selection = BADGE_SELECTION_THUMB_WIDTHS,
                          .frame_addressing = BADGE_FRAME_DIRECT,
                          .frame_sp_relative = (int)sp};
          cg_stack(&c, store, reg, offset);
          assert(strstr(c.out, reg < 8 && offset <= (sp ? 1020u : 124u)
                                   ? ".n " : ".w "));
          finish(&c);
        }
  for (unsigned bits = 1; bits <= 32; bits *= 2)
    for (unsigned reg = 0; reg <= 12; reg++) {
      cg_context c = {.scalar_selection = BADGE_SELECTION_THUMB_WIDTHS};
      cg_type type = {.bits = bits};
      cg_normalize(&c, &type, reg);
      if (bits < 32) {
        assert(strstr(c.out, reg < 8 && (bits == 8 || bits == 16) ? ".n " : "ubfx "));
        finish(&c);
      } else assert(!c.out);
    }
  fprintf(stderr, "width selection: %u flag-preserving encoding cases passed\n", cases);
}
