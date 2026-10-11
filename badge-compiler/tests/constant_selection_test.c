#include "codegen_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *select(const char *source) {
  cg_function fn = {0};
  cg_context c = {.scalar_selection = BADGE_SELECTION_THUMB_CONSTANTS,
                  .fn = &fn, .file = "constant-selection"};
  assert(!setjmp(c.failure));
  cg_emit(&c, "%s", source);
  cg_fold_memory_addresses(&c, 0);
  cg_forward_byte_arguments(&c, 0);
  cg_reuse_constants(&c, 0);
  cg_reduce_branches(&c, 0);
  while (c.allocations) {
    cg_allocation *next = c.allocations->next;
    free(c.allocations);
    c.allocations = next;
  }
  return c.out;
}
static void expect(const char *source, const char *needle, const char *absent) {
  char *out = select(source);
  assert(strstr(out, needle));
  if (absent) assert(!strstr(out, absent));
  free(out);
}
static void capture_flags(unsigned reg, unsigned id, unsigned phase) {
  const char *conditions[] = {"pl", "ne", "cc", "vc"};
  printf("mov.w r%u, #0\n", reg);
  for (unsigned flag = 0; flag < 4; flag++)
    printf("b%s .Lcg_flags_%u_%u_%u\norr.w r%u, r%u, #%u\n.Lcg_flags_%u_%u_%u:\n",
           conditions[flag], id, phase, flag, reg, reg, 1u << flag, id, phase, flag);
}
int main(int argc, char **argv) {
  int runtime = argc == 2 && !strcmp(argv[1], "--runtime");
  expect("movw r0, #0\nmovt r0, #3072\n", "mov.w r0, #201326592", "movt");
  expect("movw r1, #41\ncmp r0, r1\n", "movs.n r1, #41", "movw");
  expect("movw r1, #41\nbeq somewhere\n", "movw r1, #41", "movs");
  expect("movw r8, #41\ncmp r0, r8\n", "movw r8, #41", "movs");
  expect("movw r1, #256\ncmp r0, r1\n", "movw r1, #256", "movs");
  expect("movw r1, #41\nit eq\nmoveq r0, r1\n", "movw r1, #41", "movs");
  expect("movw r0, #41\nbl command\n", "movs.n r0, #41", "movw");
  expect("movw r0, #1\nmovw r1, #75\nbl command\n", "movs.n r0, #1", NULL);
  expect("movw r0, #41\nadc.w r1, r1, r3\nbl command\n", "movw r0, #41", "movs");
  expect("movw r0, #41\n@ assembly-effects: known 0\nbl command\n",
         "movw r0, #41", "movs");
  expect("movw r0, #41\nstr.n r0, [r5]\n.Lcg_1_next:\ncmp.n r5, #3\n",
         "movs.n r0, #41", "movw");
  expect("movw r0, #41\nstr.n r0, [r5]\nbeq target\ncmp r5, #3\n",
         "movw r0, #41", "movs");
  expect("movw r0, #41\nit eq\ncmp r5, #3\n", "movw r0, #41", "movs");
  expect("movw r0, #41\nb .Lcg_1_test\n.Lcg_1_test:\ncmp r0, #0\n",
         "movs.n r0, #41", "movw");
  expect(".Lcg_1_test:\ncmp r0, #0\n.Lcg_1_body:\n"
         "movw r0, #41\nb .Lcg_1_test\n", "movs.n r0, #41", "movw");
  expect("movw r0, #41\nb .Lcg_1_cycle\n.Lcg_1_cycle:\nb .Lcg_1_cycle\n",
         "movw r0, #41", "movs");
  expect("movw r0, #41\nb .Lcg_1_use\n.Lcg_1_use:\nadc.w r1, r1, r3\ncmp r0, #0\n",
         "movw r0, #41", "movs");
  expect("add.w r0, r1, #7\ncmp r0, r2\n", "adds.n r0, r1, #7", NULL);
  expect("sub.w r5, r5, #255\ncmp r5, #0\n", "subs.n r5, r5, #255", NULL);
  expect("sub.w r5, r5, #256\ncmp r5, #0\n", "sub.w r5, r5, #256", "subs.n");
  expect("add.w r0, r1, #8\ncmp r0, r2\n", "add.w r0, r1, #8", "adds.n");
  expect("sub.w r8, r8, #1\ncmp r8, #0\n", "sub.w r8, r8, #1", "subs.n");
  expect("lsr.w r3, r6, #8\nmov r0, r3\nbl sink\n@ scalar-call: arguments 1\n",
         "lsrs.n r0, r6, #8", "mov r0, r3");
  expect("and.w r3, r6, #255\nmov r0, r3\nbl sink\n@ scalar-call: arguments 1\n",
         "uxtb.n r0, r6", "mov r0, r3");
  expect("lsr.w r3, r6, #8\nmov r0, r3\nbl sink\n", "mov r0, r3", "scalar-call-forwarding");
  expect("lsr.w r3, r6, #8\nmov r0, r3\nbl sink\n@ scalar-call: arguments 2\n",
         "mov r0, r3", "scalar-call-forwarding");
  expect("lsr.w r3, r6, #8\nmov r0, r3\nblx r5\n@ scalar-call: arguments 1\n",
         "mov r0, r3", "scalar-call-forwarding");
  expect("@ constant-assembly-lines: 4\nlsr.w r3, r6, #8\nmov r0, r3\n"
         "bl sink\n@ scalar-call: arguments 1\n", "mov r0, r3", "scalar-call-forwarding");
  expect("lsr.w r0, r1, #32\ncmp r0, #0\n", "lsrs.n r0, r1, #32", NULL);
  expect("lsl.w r0, r1, #0\ncmp r0, #0\n", "lsls.n r0, r1, #0", NULL);
  expect("asr.w r0, r1, #1\ncmp r0, #0\n", "asrs.n r0, r1, #1", NULL);
  expect("lsr.w r0, r1, #8\nadc.w r2, r2, r3\ncmp r0, #0\n",
         "lsr.w r0, r1, #8", "lsrs.n");
  expect("lsl.w r0, r1, #8\nbeq .Lcg_use\n.Lcg_use:\ncmp r0, #0\n",
         "lsl.w r0, r1, #8", "lsls.n");
  expect("asr.w r0, r7, #8\ncmp r0, #0\n", "asr.w r0, r7, #8", "asrs.n");
  expect("and.w r0, r1, #65535\nadc.w r2, r2, r3\n", "uxth.n r0, r1", NULL);
  expect("and.w r0, r7, #255\ncmp r0, #0\n", "and.w r0, r7, #255", "uxtb.n");
  expect("and.w r0, r1, #127\ncmp r0, #0\n", "and.w r0, r1, #127", "uxtb.n");
  expect("sub.w r5, r5, #1\nadc.w r0, r0, r3\ncmp r5, #0\n",
         "sub.w r5, r5, #1", "subs.n");
  expect("subs.w r5, r5, #1\nbne .Lcg_1_head\n", "subs.n r5, r5, #1", NULL);
  expect("add.w r8, r1, r5, lsl #2\nmovw r0, #33\nstr.w r0, [r8]\n"
         "movw r8, #7\n", "str.w r0, [r1, r5, lsl #2]", "add.w r8");
  expect("add.w r8, r1, r5, lsl #3\nmovw r0, #5\nstr.w r0, [r8]\n"
         "mov r0, r8\n", "add.w r8", "memory-address-fold");
  expect("add.w r8, r1, r5, lsl #2\nmovw r1, #33\nstr.w r0, [r8]\n"
         "movw r8, #7\n", "add.w r8", "memory-address-fold");
  expect("add.w r8, r1, r5, lsl #2\nstr.w r8, [r8]\nmovw r8, #7\n",
         "add.w r8", "memory-address-fold");
  expect("add.w r8, r1, r5, lsl #4\nstr.w r0, [r8]\nmovw r8, #7\n",
         "add.w r8", "memory-address-fold");
  expect("add.w r1, r1, r5, lsl #2\nstr.w r0, [r1]\nmovw r1, #7\n",
         "add.w r1", "memory-address-fold");
  expect("add.w r8, r1, r5, lsl #2\nstr.w r0, [r8]\nmovt r8, #7\n",
         "add.w r8", "memory-address-fold");
  expect("add.w r8, r1, r5, lsl #2\nstr.w r0, [r8]\nadd.w r8, r8, #7\n",
         "add.w r8", "memory-address-fold");
  expect("add.w r8, r1, r5, lsl #2\nstr.w r0, [r8]\n.Lcg_1_join:\nmovw r8, #7\n",
         "add.w r8", "memory-address-fold");
  expect("add.w r8, r1, r5, lsl #2\nstr.w r0, [r8]\n"
         "@ assembly-effects: known 0\n@ constant-assembly-lines: 1\nnop\nmovw r8, #7\n",
         "add.w r8", "memory-address-fold");
  expect("bne .Lcg_edge1\nb .Lcg_1_target\n.Lcg_edge1:\nnop\n.Lcg_1_target:\n",
         "beq.n .Lcg_1_target", "b .Lcg_1_target");
  expect("@ assembly-effects: known 0\n@ constant-assembly-lines: 3\n"
         "bne .Lcg_edge1\nb .Lcg_1_target\n.Lcg_edge1:\n.Lcg_1_target:\n",
         "bne .Lcg_edge1\nb .Lcg_1_target", "branch-selection");
  expect("bne .Lcg_edge1\nb .Lcg_1_target\n.Lcg_edge1:\n"
         "@ assembly-effects: known 0\n@ constant-assembly-lines: 1\nnop\n.Lcg_1_target:\n",
         "bne .Lcg_edge1", "branch-selection");
  for (unsigned count = 58; count <= 59; count++) {
    char source[1024] = "bne .Lcg_edge1\nb .Lcg_1_target\n.Lcg_edge1:\n";
    for (unsigned n = 0; n < count; n++) strcat(source, "nop\n");
    strcat(source, ".Lcg_1_target:\n");
    expect(source, count == 58 ? "beq.n .Lcg_1_target" : "bne .Lcg_edge1",
           count == 58 ? "b .Lcg_1_target" : "branch-selection");
  }
  expect("movw r8, #65280\nmovt r8, #65535\n", "mvn.w r8, #255", "movt");
  expect("movw r0, #32\nmovt r0, #53248\nmov r5, r0\nmovw r0, #255\n",
         "movw r5, #32\nmovt r5, #53248", "mov r5, r0");
  expect("movw r0, #32\nmovt r0, #53248\nmov r5, r0\nstr r0, [r5]\n",
         "mov r5, r0", "constant-copy-fold");
  expect("movw r0, #0\nmovt r0, #3072\nmov r5, r0\nmovw r0, #0\nmovt r0, #3072\n",
         "mov r0, r5", NULL);
  expect("movw r1, #32772\nmovt r1, #16387\nmovw r2, #32772\nmovt r2, #16386\n",
         "sub.w r2, r1, #65536", NULL);
  expect("movw r1, #32772\nmovt r1, #16386\nmovw r2, #32772\nmovt r2, #16387\n",
         "add.w r2, r1, #65536", NULL);
  expect("movw r0, #32\nmovt r0, #53248\nmov r5, r0\n"
         "@ assembly-effects: known 0\n@ constant-assembly-lines: 2\n"
         "movw r5, #0\nmovt r5, #3072\nmovw r0, #36\nmovt r0, #53248\n",
         "movw r5, #0\nmovt r5, #3072", "constant-reuse");
  expect("@ assembly-effects: known 0\n@ constant-assembly-lines: 3\n"
         "@ constant-assembly-lines: 1\nmovw r5, #0\nmovt r5, #3072\n",
         "movw r5, #0\nmovt r5, #3072", "constant-synthesis");
  expect("@ assembly-effects: known 1\n@ constant-assembly-lines: 4\n"
         "\nnop\n\nnop\nmovw r0, #0\nmovt r0, #3072\n",
         "\nnop\n\nnop\nmov.w r0, #201326592", NULL);
  expect("movw r5, #32\nmovt r5, #53248\nbl effect\nmovw r0, #36\nmovt r0, #53248\n",
         "movw r0, #36\nmovt r0, #53248", "constant-reuse");
  expect("movw r5, #32\nmovt r5, #53248\njoin:\nmovw r0, #36\nmovt r0, #53248\n",
         "movw r0, #36\nmovt r0, #53248", "constant-reuse");
  puts(".syntax unified\n.cpu cortex-m33\n.thumb\n.text");
  if (runtime)
    puts(".global main\n.thumb_func\nmain:\npush.w {r4-r8, lr}");
  if (runtime) {
    for (unsigned shift = 0; shift <= 3; shift++) {
      char source[256];
      printf("movw r1, #:lower16:memory_test\nmovt r1, #:upper16:memory_test\nmov.w r5, #1\n");
      snprintf(source, sizeof(source),
               "add.w r8, r1, r5, lsl #%u\nmovw r0, #33\nstr.w r0, [r8]\n"
               "movw r8, #7\n", shift);
      char *out = select(source);
      printf("%sldr.w r2, [r1, #%u]\ncmp r2, #33\nbne constant_failed\n", out, 1u << shift);
      free(out);
      snprintf(source, sizeof(source),
               "add.w r8, r1, r5, lsl #%u\nldr.w r0, [r8]\nmovw r8, #7\n", shift);
      out = select(source);
      printf("%scmp r0, #33\nbne constant_failed\n", out);
      free(out);
    }
    const uint32_t inputs[] = {0, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    for (unsigned half = 0; half < 2; half++)
      for (unsigned carry = 0; carry < 2; carry++)
        for (unsigned n = 0; n < 4; n++) {
          unsigned mask = half ? 65535 : 255;
          char source[128];
          snprintf(source, sizeof(source),
                   "and.w r0, r1, #%u\nadc.w r2, r2, r3\n", mask);
          char *out = select(source);
          printf("movw r1, #%u\nmovt r1, #%u\nmov.w r2, #0\nmov.w r3, #0\n"
                 "cmp r2, #%u\n%s"
                 "cmp r2, #%u\nbne constant_failed\nmovw r3, #%u\n"
                 "cmp r0, r3\nbne constant_failed\n",
                 inputs[n] & 65535u, inputs[n] >> 16, carry ? 0 : 1, out,
                 carry, inputs[n] & mask);
          free(out);
        }
    unsigned id = 0;
    for (unsigned sub = 0; sub < 2; sub++)
      for (unsigned immediate = 1; immediate <= 255; immediate += 254)
        for (unsigned n = 0; n < 4; n++, id++) {
          char source[64];
          snprintf(source, sizeof(source), "%ss.w r5, r5, #%u\n",
                   sub ? "sub" : "add", immediate);
          char *out = select(source);
          printf("movw r5, #%u\nmovt r5, #%u\n%s",
                 inputs[n] & 65535u, inputs[n] >> 16, source);
          capture_flags(8, id, 0);
          printf("mov r4, r5\nmovw r5, #%u\nmovt r5, #%u\n%s",
                 inputs[n] & 65535u, inputs[n] >> 16, out);
          capture_flags(6, id, 1);
          puts("cmp r8, r6\nbne constant_failed\ncmp r4, r5\nbne constant_failed");
          free(out);
        }
    const uint32_t bases[] = {0x40038004u, 0x40028004u};
    for (unsigned n = 0; n < 2; n++) {
      uint32_t target = bases[1 - n];
      char source[192];
      snprintf(source, sizeof(source),
               "movw r5, #%u\nmovt r5, #%u\nmovw r8, #%u\nmovt r8, #%u\n",
               bases[n] & 65535u, bases[n] >> 16, target & 65535u, target >> 16);
      char *out = select(source);
      printf("mov.w r2, #0\nmov.w r3, #0\ncmp r2, r2\n%s"
             "adc.w r2, r2, r3\ncmp r2, #1\nbne constant_failed\n"
             "movw r0, #%u\nmovt r0, #%u\ncmp r8, r0\nbne constant_failed\n",
             out, target & 65535u, target >> 16);
      free(out);
    }
  }
  const uint32_t values[] = {0x01010101u, 0x00ff00ffu, 0xff00ff00u,
    0x80000000u, 0xd0000000u, 0x0c000000u, 0xdc000000u,
    0xffffffffu, 0xffff00ffu, 0xffffff00u, 0x12345678u,
    0x00010000u, 0x0000ffffu};
  unsigned index = 0;
  for (unsigned r = 0; r <= 12; r++) {
    if (runtime && r != 5 && r != 8 && r != 12) continue;
    for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++) {
      char source[128];
      snprintf(source, sizeof(source), "movw r%u, #%u\nmovt r%u, #%u\n",
               r, values[i] & 65535u, r, values[i] >> 16);
      char *out = select(source);
      if (runtime) {
        puts("mov.w r2, #0\nmov.w r3, #0\ncmp r3, r3");
        printf("%sadc.w r2, r2, r3\ncmp r2, #1\nbne constant_failed\n"
               "movw r0, #%u\nmovt r0, #%u\ncmp r%u, r0\nbne constant_failed\n",
               out, values[i] & 65535u, values[i] >> 16, r);
        index++;
      } else printf("constant_%u:\n%s", index++, out);
      assert(!strstr(out, "movs") && !strstr(out, "mvns"));
      free(out);
    }
  }
  if (runtime)
    puts("movw r0, #0\nb constant_return\nconstant_failed:\nmovw r0, #1\n"
         "constant_return:\npop.w {r4-r8, pc}\n.data\n.balign 4\nmemory_test:\n.space 32\n");
  fprintf(stderr, "constant selection: %u encodings and boundary assertions passed\n", index);
}
