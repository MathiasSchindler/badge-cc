#include "ir_pass.h"
#include "target.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned calls[3], order;
static void optimization(ir_context *c, ir_module *m) {
  (void)m;
  if (!order)
    order = 1;
  calls[0]++;
  unsigned char *scratch = ir_allocate(c, 16u * 1024u * 1024u);
  scratch[0] = 1;
}
static void legalization(ir_context *c, ir_module *m) {
  (void)c;
  (void)m;
  assert(order == 1);
  order = 2;
  calls[1]++;
}
static void experimental(ir_context *c, ir_module *m) {
  (void)c;
  (void)m;
  calls[2]++;
}
static void invalid_output(ir_context *c, ir_module *m) {
  for (unsigned k = 0; k < m->line_count; k++)
    if (!strcmp(ir_text(m, m->lines[k].op), "ret"))
      m->lines[k].replacement = ir_copy(c, "ret i32 %missing\n", 17);
}
const ir_pass badge_ir_passes[] = {
    {"optimization", "test", optimization, IR_PASS_OPTIMIZATION},
    {"legalization", "test", legalization, IR_PASS_LEGALIZATION},
    {"experimental", "test", experimental, IR_PASS_OPTIMIZATION},
    {"invalid-output", "test", invalid_output, IR_PASS_OPTIMIZATION}};
const unsigned badge_ir_pass_count = 4;
const char *const badge_ir_default_pipeline[] = {"optimization",
                                                 "legalization"};
const unsigned badge_ir_default_pass_count = 2;

static void arena(void) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "arena-fixture.ll";
  assert(!setjmp(c->failure));
  unsigned char *persistent = ir_allocate(c, 17);
  memset(persistent, 42, 17);
  ir_allocation *outer = c->allocations;
  ir_chunk *first = c->chunks;
  size_t bytes = c->memory;
  for (unsigned size = 0; size < 257; size++) {
    unsigned char *p = ir_allocate(c, size);
    assert((uintptr_t)p % _Alignof(max_align_t) == 0);
    for (unsigned k = 0; k < size; k++)
      assert(p[k] == 0);
    memset(p, 0xff, size);
    bytes += size;
  }
  assert(c->memory == bytes && c->chunks == first);
  ir_allocation *inner = c->allocations;
  unsigned char *large = ir_allocate(c, 200000);
  assert(c->chunks != first);
  memset(large, 0xff, 200000);
  ir_allocation *grown = c->allocations;
  ir_chunk *large_chunk = c->chunks;
  for (unsigned k = 0; k < 10000; k++)
    ir_allocate(c, 7);
  ir_release_to(c, grown);
  assert(c->allocations == grown && c->chunks == large_chunk &&
         c->memory == bytes + 200000);
  for (unsigned k = 0; k < 200000; k++)
    assert(large[k] == 0xff);
  ir_release_to(c, inner);
  assert(c->allocations == inner && c->memory == bytes);
  ir_release_to(c, inner);
  assert(c->memory == bytes);
  ir_release_to(c, outer);
  assert(c->chunks == first && c->memory == 17);
  for (unsigned k = 0; k < 17; k++)
    assert(persistent[k] == 42);
  unsigned char *reused = ir_allocate(c, 257);
  for (unsigned k = 0; k < 257; k++)
    assert(reused[k] == 0);
  max_align_t *aligned = ir_allocate(c, sizeof(*aligned));
  *aligned = (max_align_t){0};
  char long_text[513];
  memset(long_text, 'x', sizeof(long_text) - 1);
  long_text[sizeof(long_text) - 1] = 0;
  assert(!strcmp(ir_format(c, "%s:%u", "short", 17), "short:17"));
  assert(!strcmp(ir_format(c, "%s", long_text), long_text));
  ir_release_to(c, NULL);
  assert(c->memory == 0 && !c->allocations && c->chunks == first);
  reused = ir_allocate(c, 257);
  assert(c->chunks == first);
  for (unsigned k = 0; k < 257; k++)
    assert(reused[k] == 0);
  ir_release_to(c, NULL);
  ir_allocate(c, 128u * 1024u * 1024u);
  assert(c->memory == 128u * 1024u * 1024u);
  ir_allocate(c, 0);
  assert(c->memory == 128u * 1024u * 1024u);
  if (!setjmp(c->failure)) {
    ir_allocate(c, 1);
    assert(!"cumulative allocation limit must be diagnosed");
  }
  assert(c->memory == 128u * 1024u * 1024u);
  ir_release_to(c, NULL);
  assert(!c->allocations && c->memory == 0);
  if (!setjmp(c->failure)) {
    ir_allocate(c, SIZE_MAX);
    assert(!"oversized allocation must fail before size arithmetic");
  }
  assert(!c->allocations && c->memory == 0);
  ir_destroy(c);
  assert(!c->chunks);
  free(c);
}

int main(void) {
  arena();
  static const char source[] = "target triple = " BADGE_IR_TRIPLE
                               "\ntarget datalayout = " BADGE_IR_LAYOUT
                               "\ndefine i32 @f() {\nentry:\nret i32 0\n}\n";
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "pipeline-fixture.ll";
  assert(setjmp(c->failure) == 0);
  unsigned char *persistent = ir_allocate(c, 64);
  persistent[0] = 42;
  ir_pipeline(c, source, strlen(source), NULL);
  assert(!strcmp(c->output, source));
  assert(calls[0] == 1 && calls[1] == 1 && calls[2] == 0 && order == 2);
  assert(c->memory == 64 && persistent[0] == 42);
  char selected[256] = {0};
  for (unsigned k = 0; k < 16; k++)
    strcat(selected, k ? ",optimization" : "optimization");
  ir_pipeline(c, source, strlen(source), selected);
  assert(calls[0] == 17 && c->memory == 64 && !strcmp(c->output, source));
  ir_pipeline(c, source, strlen(source), "experimental");
  assert(calls[2] == 1 && c->memory == 64);
  assert(ir_find_pass("experimental", 12)->role == IR_PASS_OPTIMIZATION);
  assert(ir_find_pass("legalization", 12)->role == IR_PASS_LEGALIZATION);
  assert(!ir_find_pass("missing", 7));
  ir_map map = {0};
  ir_map_init(c, &map, 3);
  ir_map_put(c, &map, "a", "first");
  ir_map_put(c, &map, "i", "second");
  ir_map_put(c, &map, "q", "third");
  assert(!strcmp(ir_map_get(&map, "a"), "first"));
  assert(!strcmp(ir_map_get(&map, "i"), "second"));
  assert(!strcmp(ir_map_get(&map, "q"), "third"));
  assert(!ir_map_get(&map, "y"));
  ir_map_put(c, &map, "i", "updated");
  assert(!strcmp(ir_map_get(&map, "i"), "updated"));
  ir_map_clear(&map);
  assert(map.count == 0 && !ir_map_get(&map, "a"));
  static const char names[] = "target triple = " BADGE_IR_TRIPLE
                              "\ntarget datalayout = " BADGE_IR_LAYOUT
                              "\n@\".badge\\2eopt\\2e1\" = constant i32 0\n"
                              "define i32 @names() {\nentry:\n"
                              "%\".badge\\2eopt\\2e3\" = freeze i32 0\n"
                              "%.badge.opt.5 = freeze i32 0\nret i32 0\n}\n";
  ir_module module = {0};
  ir_parse(c, &module, names, strlen(names));
  assert(!strcmp(ir_fresh(c, &module, '@'), "@\".badge.opt.2\""));
  assert(!strcmp(ir_fresh(c, &module, '%'), "%\".badge.opt.4\""));
  assert(!strcmp(ir_fresh(c, &module, '%'), "%\".badge.opt.6\""));
  assert(badge_direct_i64_sdiv(1) && badge_direct_i64_sdiv(UINT64_C(1) << 31));
  assert(!badge_direct_i64_sdiv(0) && !badge_direct_i64_sdiv(3) &&
         !badge_direct_i64_sdiv(UINT64_C(1) << 32));
  ir_destroy(c);
  free(c);
  ir_context *bad = calloc(1, sizeof(*bad));
  assert(bad);
  bad->file = "invalid-output.ll";
  if (!setjmp(bad->failure)) {
    ir_pipeline(bad, source, strlen(source), "invalid-output");
    assert(!"invalid pass output must fail verification");
  }
  ir_destroy(bad);
  free(bad);
  puts("badge-opt: independent registry/default policy, roles, 16-pass scratch "
       "reclamation, aligned zeroed bounded arena, hash collisions, "
       "escaped/unquoted fresh names and target legality PASS");
  return 0;
}
