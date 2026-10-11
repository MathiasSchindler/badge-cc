#define _DARWIN_C_SOURCE
#define main badge_codegen_main
#include "badge_codegen.c"
#undef main
#include "codegen_internal.h"
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>

#define TARGET "target triple = \"thumbv8m.main-unknown-none-eabi\"\n"
#define FN(body) "define i32 @f(i32 %x) {\nentry:\n" body "\n}\n"
static void fixture_mode(const char *source, const char *fragment, int accepted,
                         const char *path, badge_frame_addressing mode,
                         badge_register_residency residency,
                         badge_scalar_selection selection,
                         badge_memory_selection memory) {
  char *assembly = NULL;
  size_t size = 0;
  int failed = !badge_codegen_compile_with_selections(
      source, strlen(source), "fixture.ll",
      (badge_codegen_options){mode, residency}, selection, memory, &assembly,
      &size);
  if (accepted >= 0 && (failed == 0) != accepted) {
    fprintf(stderr, "unexpected fixture result: %s\n", source);
    abort();
  }
  if (!failed) {
    if (fragment && !strstr(assembly, fragment)) {
      fprintf(stderr, "missing assembly fragment '%s' for: %s\n", fragment,
              source);
      abort();
    }
    assert(size == strlen(assembly));
    assert(host_write_file("badge-codegen-test", path,
                           (unsigned char *)assembly, size));
    pid_t pid = fork();
    assert(pid >= 0);
    if (!pid) {
      execl("./build/badge-asm", "badge-asm", path, "-o",
            "build/codegen-test.o", (char *)NULL);
      _exit(127);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0);
  } else {
    assert(assembly == NULL && size == 0);
  }
  free(assembly);
}
static void fixture(const char *source, const char *fragment, int accepted,
                    const char *path) {
  for (unsigned mode = 0; mode < 2; mode++) {
    for (unsigned residency = 0; residency <= BADGE_RESIDENCY_GLOBAL_COMPACT; residency++)
      for (unsigned selection = 0; selection <= BADGE_SELECTION_THUMB_LAYOUT; selection++)
        for (unsigned memory = 0; memory < 2; memory++)
          fixture_mode(source,
                       !residency && !selection && !memory ? fragment : NULL,
                       accepted, path, (badge_frame_addressing)mode,
                       (badge_register_residency)residency,
                       (badge_scalar_selection)selection,
                       (badge_memory_selection)memory);
  }
}
static void memory_tests(const char *path) {
  const unsigned lengths[] = {0, 1,  2,  3,  4,  5,  7, 8,
                              9, 15, 16, 17, 63, 64, 65};
  for (unsigned n = 0; n < sizeof(lengths) / sizeof(*lengths); n++)
    for (unsigned variant = 0; variant < 7; variant++) {
      unsigned length = lengths[n], offset = variant == 1   ? 5
                                             : variant == 4 ? 79
                                                            : 4;
      unsigned align = variant == 2 ? 1 : 4;
      char source[2048];
      snprintf(
          source, sizeof(source),
          "%sdefine void @memory(ptr align 4 %%external, i8 %%fill) {\nentry:\n"
          "%%a = alloca [80 x i8], align %u\n%%b = alloca [80 x i8], align %u\n"
          "%%dst = getelementptr [80 x i8], ptr %%a, i32 0, i32 %u\n"
          "%%src = getelementptr [80 x i8], ptr %%b, i32 0, i32 4\n"
          "call void @llvm.memcpy.p0.p0.i32(ptr %%dst, ptr %s, i32 %u, i1 %s)\n"
          "call void @llvm.memset.p0.i32(ptr %%dst, i8 %%fill, i32 %u, i1 %s)\n"
          "ret void\n}\n",
          TARGET, align, align, offset,
          variant == 6   ? "inttoptr (i32 1073741824 to ptr)"
          : variant == 5 ? "%external"
                         : "%src",
          length, variant == 3 ? "true" : "false", length,
          variant == 3 ? "true" : "false");
      for (unsigned frame = 0; frame < 2; frame++)
        for (unsigned residency = 0; residency <= BADGE_RESIDENCY_GLOBAL_COMPACT; residency++)
          for (unsigned scalar = 0; scalar <= BADGE_SELECTION_THUMB_LAYOUT; scalar++) {
            int fill = length >= 4 && offset == 4 && align == 4 && variant != 3;
            int copy = fill && variant < 5;
            char fragment[100];
            snprintf(fragment, sizeof(fragment),
                     "word-copies %d word-fills %d word-bytes %u tail-bytes %u",
                     copy, fill, (copy + fill) * (length / 4) * 4,
                     (copy + fill) * (length % 4));
            fixture_mode(source, fragment, 1, path,
                         (badge_frame_addressing)frame,
                         (badge_register_residency)residency,
                         (badge_scalar_selection)scalar, BADGE_MEMORY_WORD);
          }
    }
  char *out = NULL;
  size_t size = 0;
  const char source[] = TARGET FN("ret i32 %x");
  assert(!badge_codegen_compile_with_selections(
      source, sizeof(source) - 1, "memory.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL},
      BADGE_SELECTION_THUMB, (badge_memory_selection)99, &out, &size));
  assert(!out && !size);
  const char *offsets[] = {"%index", "-4"};
  for (unsigned k = 0; k < 2; k++) {
    char source[1024];
    snprintf(source, sizeof(source),
             "%sdefine void @f(i32 %%index) {\nentry:\n"
             "%%a = alloca [80 x i8], align 4\n"
             "%%dst = getelementptr [80 x i8], ptr %%a, i32 0, i32 %s\n"
             "call void @llvm.memset.inline.p0.i32(ptr %%dst, i8 0, i32 7, i1 "
             "false)\n"
             "ret void\n}\n",
             TARGET, offsets[k]);
    fixture_mode(source, "word-fills 0", 1, path, BADGE_FRAME_DIRECT,
                 BADGE_RESIDENCY_GLOBAL, BADGE_SELECTION_THUMB,
                 BADGE_MEMORY_WORD);
  }
}
static void selection_tests(void) {
  static const struct {
    const char *body, *match;
  } tests[] = {
      {"%a = add i32 %x, 4095\nret i32 %a", "addw r0, r0, #4095"},
      {"%a = sub i32 %x, -4095\nret i32 %a", "addw r0, r0, #4095"},
      {"%a = and i32 %x, 16711935\nret i32 %a", "and.w r0, r0, #16711935"},
      {"%a = xor i32 %x, 305419896\nret i32 %a", "eor.w r0, r0, r2"},
      {"%a = shl i32 %x, 31\nret i32 %a", "lsl.w r0, r0, #31"},
      {"%a = lshr i32 %x, 32\nret i32 %a", "lsr.w r0, r0, r2"},
      {"%a = add nsw i32 %x, 1\nret i32 %a", "add.w r0, r0, r2"},
      {"%m = mul i32 %x, 7\n%a = add i32 %m, %x\nret i32 %a",
       "mla r0, r0, r2, r1"},
      {"%m = mul nsw i32 %x, 7\n%a = add i32 %m, %x\nret i32 %a",
       "multiply-adds 0"},
      {"%m = mul i32 %x, 7\n%a = add i32 %m, %m\nret i32 %a",
       "multiply-adds 0"},
      {"%q = udiv i32 %x, 7\n%r = urem i32 %x, 7\n%a = add i32 %q, %r\nret i32 "
       "%a",
       "shared-divisions 1"},
      {"%r = srem i32 %x, 7\n%q = sdiv i32 %x, 7\n%a = add i32 %q, %r\nret i32 "
       "%a",
       "shared-divisions 1"},
      {"%q = udiv exact i32 %x, 7\n%r = urem i32 %x, 7\nret i32 %r",
       "shared-divisions 0"},
      {"%q = udiv i32 %x, 7\n%r = urem i32 %x, 9\nret i32 %r",
       "shared-divisions 0"},
      {"%q = udiv i32 %x, 7\ncall void asm \"nop\", \"\"()\n%r = urem i32 %x, "
       "7\nret i32 %r",
       "shared-divisions 0"},
      {"%q = udiv i32 %x, 7\nbr label %out\nout:\n%r = urem i32 %x, 7\nret i32 "
       "%r",
       "shared-divisions 0"},
  };
  for (unsigned residency = 0; residency <= BADGE_RESIDENCY_GLOBAL_COMPACT; residency++)
    for (unsigned k = 0; k < sizeof(tests) / sizeof(*tests); k++) {
      char source[1024], *out = NULL;
      size_t size = 0;
      assert(snprintf(source, sizeof(source), "%s%s\n}\n",
                      TARGET "define i32 @f(i32 %x) {\nentry:\n",
                      tests[k].body) > 0);
      assert(badge_codegen_compile_with_scalar_selection(
          source, strlen(source), "selection.ll",
          (badge_codegen_options){BADGE_FRAME_DIRECT,
                                  (badge_register_residency)residency},
          BADGE_SELECTION_THUMB, &out, &size));
      assert(strstr(out, tests[k].match));
      if (k == 10 || k == 11) {
        const char *division = strstr(out, k == 10 ? "udiv r4" : "sdiv r4");
        assert(division &&
               !strstr(division + 1, k == 10 ? "udiv r4" : "sdiv r4"));
      }
      free(out);
    }
  char *out = NULL;
  size_t size = 0;
  const char source[] = TARGET FN("ret i32 %x");
  assert(!badge_codegen_compile_with_scalar_selection(
      source, sizeof(source) - 1, "bad-selection.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL},
      (badge_scalar_selection)99, &out, &size));
  assert(!out && !size);
}
static unsigned register_moves(const char *assembly) {
  unsigned count = 0;
  for (const char *at = assembly; (at = strstr(at, "\nmov r")) != NULL; at++)
    count++;
  return count;
}
static void register_operand_tests(const char *path) {
  const char *operations[] = {"add", "sub", "mul", "and", "or", "xor",
                              "shl", "lshr", "ashr", "udiv", "sdiv",
                              "urem", "srem"};
  const char *left[] = {"%x", "%x", "7", "%x"};
  const char *right[] = {"%y", "7", "%y", "%x"};
  for (unsigned op = 0; op < sizeof(operations) / sizeof(*operations); op++)
    for (unsigned variant = 0; variant < 4; variant++) {
      char source[512];
      assert(snprintf(source, sizeof(source),
          "%sdefine i32 @f(i32 %%x, i32 %%y) {\nentry:\n"
          "%%a = %s i32 %s, %s\n%%b = xor i32 %%a, %%x\nret i32 %%b\n}\n",
          TARGET, operations[op], left[variant], right[variant]) > 0);
      for (unsigned frame = 0; frame < 2; frame++)
        for (unsigned residency = 0; residency <= BADGE_RESIDENCY_GLOBAL_COMPACT; residency++)
          for (unsigned memory = 0; memory < 2; memory++)
            fixture_mode(source,
                residency >= BADGE_RESIDENCY_GLOBAL
                    ? "register-operands: operations 2"
                    : "register-operands: operations 0",
                1, path, (badge_frame_addressing)frame,
                (badge_register_residency)residency,
                BADGE_SELECTION_THUMB_REGISTERS, (badge_memory_selection)memory);
    }
  const char source[] = TARGET
      "define i32 @f(i32 %x, i32 %y) {\nentry:\n"
      "%a = add i32 %x, %y\nret i32 %a\n}\n";
  char *control = NULL, *candidate = NULL;
  size_t control_size = 0, candidate_size = 0;
  assert(badge_codegen_compile_with_scalar_selection(
      source, sizeof(source) - 1, "registers.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL},
      BADGE_SELECTION_THUMB, &control, &control_size));
  assert(badge_codegen_compile_with_scalar_selection(
      source, sizeof(source) - 1, "registers.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL},
      BADGE_SELECTION_THUMB_REGISTERS, &candidate, &candidate_size));
  assert(register_moves(control) - register_moves(candidate) == 3);
  assert(!strstr(candidate, "add.w r0, r0, r2"));
  free(control);
  free(candidate);
}
static void flexible_tests(const char *path) {
  for (unsigned size = 1; size <= 8; size++)
    for (unsigned align = 1; align <= 8; align *= 2) {
      cg_type type = {.size = size};
      unsigned expected = CG_SCRATCH_R4;
      for (unsigned offset = 0; offset < size; offset += 4) {
        unsigned bytes = size - offset > 4 ? 4 : size - offset;
        if ((bytes == 4 && align < 4) || (bytes == 2 && align < 2) ||
            bytes == 3)
          expected |= CG_SCRATCH_R5;
      }
      assert(cg_memory_scratch(&type, align) == expected);
    }
  const char source[] = TARGET FN(
      "%a = freeze i32 %x\n%b = inttoptr i32 %a to ptr\n"
      "%c = ptrtoint ptr %b to i32\nret i32 %c");
  for (unsigned frame = 0; frame < 2; frame++)
    for (unsigned scalar = 0; scalar <= BADGE_SELECTION_THUMB_LAYOUT; scalar++)
      fixture_mode(source, "allocation-copies: direct 3", 1, path,
                   (badge_frame_addressing)frame, BADGE_RESIDENCY_GLOBAL_FLEX,
                   (badge_scalar_selection)scalar, BADGE_MEMORY_WORD);
}
static void compact_tests(const char *path) {
  const unsigned widths[] = {1, 8, 16};
  const char *operations[] = {"add", "sub", "mul", "and", "or", "xor",
                              "shl", "lshr", "ashr", "udiv", "sdiv",
                              "urem", "srem"};
  for (unsigned w = 0; w < sizeof(widths) / sizeof(*widths); w++)
    for (unsigned op = 0; op < sizeof(operations) / sizeof(*operations); op++) {
      char source[512];
      assert(snprintf(source, sizeof(source),
          "%sdefine i32 @narrow(i%u %%x, i%u %%y) {\nentry:\n"
          "%%a = %s i%u %%x, %%y\n%%b = xor i%u %%a, %%x\n"
          "%%r = sext i%u %%b to i32\nret i32 %%r\n}\n",
          TARGET, widths[w], widths[w], operations[op], widths[w],
          widths[w], widths[w]) > 0);
      fixture(source, NULL, 1, path);
    }
  char source[8192] = TARGET "define i32 @chain(i32 %v0) {\nentry:\n";
  size_t used = strlen(source);
  for (unsigned n = 1; n <= 80; n++)
    used += (size_t)snprintf(source + used, sizeof(source) - used,
                            "%%v%u = add i32 %%v%u, %u\n", n, n - 1, n);
  assert(used < sizeof(source));
  strcat(source, "ret i32 %v80\n}\n");
  for (unsigned frame = 0; frame < 2; frame++)
    for (unsigned scalar = 0; scalar <= BADGE_SELECTION_THUMB_LAYOUT; scalar++) {
      unsigned sizes[2];
      for (unsigned compact = 0; compact < 2; compact++) {
        char *out = NULL;
        size_t size = 0;
        assert(badge_codegen_compile_with_selections(
            source, strlen(source), "chain.ll",
            (badge_codegen_options){(badge_frame_addressing)frame,
                compact ? BADGE_RESIDENCY_GLOBAL_COMPACT : BADGE_RESIDENCY_GLOBAL_FLEX},
            (badge_scalar_selection)scalar, BADGE_MEMORY_WORD, &out, &size));
        const char *annotation = strstr(out, "@ stack-frame: ");
        assert(annotation && sscanf(annotation, "@ stack-frame: %u", &sizes[compact]) == 1);
        free(out);
      }
      assert(sizes[0] >= 320 && sizes[1] <= 16);
    }
}
static void global_tests(void) {
  const char cycle[] =
      TARGET "define i32 @f() {\nentry:\nbr label %loop\nloop:\n"
             "%a = phi i32 [1, %entry], [%b, %loop]\n"
             "%b = phi i32 [2, %entry], [%c, %loop]\n"
             "%c = phi i32 [3, %entry], [%a, %loop]\n"
             "%ticks = phi i64 [0, %entry], [%next, %loop]\n"
             "%next = add i64 %ticks, 1\n"
             "%more = icmp ult i64 %next, 7\n"
             "br i1 %more, label %loop, label %exit\nexit:\n"
             "%ab = mul i32 %a, 100\n%bc = mul i32 %b, 10\n"
             "%sum = add i32 %ab, %bc\n%result = add i32 %sum, %c\n"
             "ret i32 %result\n}\n";
  char *out = NULL;
  size_t size = 0;
  const char all_phi[] =
      TARGET "define void @f() {\nentry:\nbr label %loop\nloop:\n"
             "%a = phi i32 [1, %entry], [%b, %loop]\n"
             "%b = phi i32 [2, %entry], [%c, %loop]\n"
             "%c = phi i32 [3, %entry], [%a, %loop]\n"
             "br label %loop\n}\n";
  assert(badge_codegen_compile_with_options(
      all_phi, sizeof(all_phi) - 1, "all-phi.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL}, &out,
      &size));
  assert(strstr(out, "ldr.w r8, [r7,") && strstr(out, "ldr.w r10, [r7,") &&
         strstr(out, "ldr.w r11, [r7,") && strstr(out, "mov r6, r"));
  assert(strstr(out, "avoided-loads 3 avoided-stores 6 phi-register-copies 3"));
  free(out);
  for (unsigned mode = 0; mode < 2; mode++) {
    assert(badge_codegen_compile_with_options(
        cycle, sizeof(cycle) - 1, "cycle.ll",
        (badge_codegen_options){(badge_frame_addressing)mode,
                                BADGE_RESIDENCY_GLOBAL},
        &out, &size));
    assert(strstr(out, "mov r6, r") && strstr(out, "phi-register-copies 3"));
    assert(strstr(out, "push.w {r4-r8, r10, r11, lr}"));
    assert(strstr(out, "pop.w {r4-r8, r10, r11, pc}"));
    assert(!strstr(out, "r9"));
    free(out);
    const char indirect[] =
        TARGET "define i32 @f(ptr %fp) {\nentry:\n%r = call i32 %fp()\n"
               "ret i32 %r\n}\n";
    assert(badge_codegen_compile_with_options(
        indirect, sizeof(indirect) - 1, "global-indirect.ll",
        (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL},
        &out, &size));
    assert(strstr(out, "mov r12, r8\nblx r12"));
    free(out);
  }
  const char pressure[] =
      TARGET "define i32 @f(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {\n"
             "entry:\ncall void asm sideeffect \"nop\", \"~{memory}\"()\n"
             "%ab = add i32 %a, %b\n%cd = add i32 %c, %d\n"
             "%abcd = add i32 %ab, %cd\n%all = add i32 %abcd, %e\n"
             "ret i32 %all\n}\n";
  assert(badge_codegen_compile_with_options(
      pressure, sizeof(pressure) - 1, "pressure.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL}, &out,
      &size));
  const char *allocation = strstr(out, "@ global-allocation:");
  unsigned allocated = 0, spilled = 0;
  assert(allocation && sscanf(allocation,
                              "@ global-allocation: allocated %u "
                              "spilled %u",
                              &allocated, &spilled) == 2);
  assert(allocated && spilled >= 2);
  assert(strstr(out, "asm-spills 3"));
  assert(strstr(out, "str.w r8, [r7,") && strstr(out, "ldr.w r8, [r7,"));
  free(out);
}
static void residency_tests(void) {
  const char chain[] = TARGET FN("%a = add i32 %x, 1\n%b = mul i32 %a, %a\n%c "
                                 "= xor i32 %b, 7\nret i32 %c");
  char *out = NULL;
  size_t size;
  assert(badge_codegen_compile_with_options(
      chain, sizeof(chain) - 1, "residency.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_LOCAL}, &out,
      &size));
  assert(strstr(out, "mov r2, r0\n") && !strstr(out, "mov r8, r0\n"));
  assert(strstr(out, "avoided-loads 3 avoided-stores 2"));
  assert(strstr(out, "push.w {r4-r8, lr}") && !strstr(out, "r9"));
  free(out);
  const char live_out[] =
      TARGET "define i32 @f(i32 %x) {\nentry:\n%a = add i32 %x, 1\n"
             "%b = mul i32 %a, %a\nbr label %exit\nexit:\n"
             "%c = add i32 %a, %b\nret i32 %c\n}\n";
  assert(badge_codegen_compile_with_options(
      live_out, sizeof(live_out) - 1, "live-out.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_LOCAL}, &out,
      &size));
  assert(strstr(out, "str.w r0, [r7, #4]\nmov r2, r0\nmul"));
  free(out);
  assert(!badge_codegen_compile_with_options(
      chain, sizeof(chain) - 1, "bad-residency.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, (badge_register_residency)99},
      &out, &size));
  assert(!out && !size);
  cg_type t = {.size = 4, .align = 4, .bits = 32};
  cg_value v = {.name = "%x", .type = &t};
  cg_function f = {.values = &v};
  cg_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "register-work-budget";
  c->fn = &f;
  c->register_residency = BADGE_RESIDENCY_LOCAL;
  c->register_work = UINT32_MAX - 1;
  if (!setjmp(c->failure)) {
    cg_register_prepare(c);
    assert(!"work budget must fail");
  }
  while (c->allocations) {
    cg_allocation *a = c->allocations;
    c->allocations = a->next;
    free(a);
  }
  free(c);
}
static void frame_tests(const char *path) {
  const unsigned offsets[] = {0,    1,    2,    3,     4,     124,
                              128,  252,  256,  1020,  1024,  4088,
                              4092, 4095, 4096, 65532, 65536, 131116};
  cg_context c = {0};
  c.file = "frame-tests";
  assert(!setjmp(c.failure));
  for (unsigned mode = 0; mode < 2; mode++) {
    c.frame_addressing = (badge_frame_addressing)mode;
    for (unsigned store = 0; store < 2; store++) {
      const unsigned registers[] = {0, 5, 12};
      for (unsigned k = 0; k < sizeof(offsets) / sizeof(*offsets); k++) {
        for (unsigned j = 0; j < sizeof(registers) / sizeof(*registers); j++) {
          unsigned r = registers[j], offset = offsets[k];
          if (store && r == 12)
            continue; /* r12 is the materialized-address scratch. */
          c.size = 0;
          cg_stack(&c, (int)store, r, offset);
          char expected[128];
          if (mode == BADGE_FRAME_DIRECT && offset <= 4095 && offset % 4 == 0)
            snprintf(expected, sizeof(expected), "%s.w r%u, [r7, #%u]\n",
                     store ? "str" : "ldr", r, offset);
          else if (offset >> 16)
            snprintf(expected, sizeof(expected),
                     "movw r12, #%u\nmovt r12, #%u\nadd.w r12, r7, r12\n%s.w "
                     "r%u, [r12]\n",
                     offset & 65535, offset >> 16, store ? "str" : "ldr", r);
          else
            snprintf(expected, sizeof(expected),
                     "movw r12, #%u\nadd.w r12, r7, r12\n%s.w r%u, [r12]\n",
                     offset, store ? "str" : "ldr", r);
          assert(!strcmp(c.out, expected));
        }
      }
    }
  }
  c.frame_addressing = BADGE_FRAME_DIRECT;
  cg_type wide = {.size = 8, .align = 8, .bits = 64};
  cg_value value = {.type = &wide, .slot = 4088};
  c.size = 0;
  cg_save(&c, &value, 0, 0);
  cg_save(&c, &value, 1, 1);
  assert(!strcmp(c.out, "str.w r0, [r7, #4088]\nstr.w r1, [r7, #4092]\n"));
  value.slot = 4096;
  c.size = 0;
  cg_save(&c, &value, 0, 0);
  cg_save(&c, &value, 1, 1);
  assert(strstr(c.out, "movw r12, #4096\n") &&
         strstr(c.out, "movw r12, #4100\n"));
  free(c.out);
  fixture(
      TARGET
      "define i32 @f(ptr %fp) {\nentry:\n%r = call i32 %fp()\nret i32 %r\n}\n",
      "blx r12", 1, path);
  fixture(TARGET
          "define i64 @f(i64 %x) {\nentry:\n%p = alloca [8192 x i8], align 8\n"
          "%a = add i64 %x, 1\nret i64 %a\n}\n",
          "movw r12, #", 1, path);
}
static void save(const char *path, const char *text) {
  FILE *f = fopen(path, "wb");
  assert(f);
  assert(fwrite(text, 1, strlen(text), f) == strlen(text) && fclose(f) == 0);
}
static void selection_cli_tests(const char *input, const char *path) {
  const char branch[] = TARGET
      "define i32 @f(i32 %x) {\nentry:\n%c = icmp slt i32 %x, 0\n"
      "br i1 %c, label %negative, label %positive\n"
      "negative:\nret i32 1\npositive:\nret i32 2\n}\n";
  fixture_mode(branch, "@ compare-branch: fused 1", 1, path,
      BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL_COMPACT,
      BADGE_SELECTION_THUMB_BRANCH, BADGE_MEMORY_WORD);
  fixture_mode(branch, "@ scalar-homes: omitted", 1, path,
      BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL_COMPACT,
      BADGE_SELECTION_THUMB_HOMES, BADGE_MEMORY_WORD);
  char *branchOutput = NULL;
  size_t branchSize = 0;
  assert(!badge_codegen_compile_with_selections(
      branch, sizeof(branch) - 1, "branch.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL_COMPACT},
      (badge_scalar_selection)99, BADGE_MEMORY_WORD, &branchOutput, &branchSize));
  assert(!branchOutput && !branchSize);
  const char source[] = TARGET FN("%a = add i32 %x, 1\nret i32 %a");
  save(input, source);
  char *args[] = {"badge-codegen",
                  "--scalar-selection=thumb",
                  "--register-residency=global",
                  "--frame-addressing=direct",
                  "--memory-selection=word",
                  (char *)input,
                  "-o",
                  (char *)path,
                  NULL};
  assert(badge_codegen_main(8, args) == 0);
  char *expected = NULL;
  size_t size = 0;
  assert(badge_codegen_compile_with_selections(
      source, sizeof(source) - 1, "selection-cli.ll",
      (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL},
      BADGE_SELECTION_THUMB, BADGE_MEMORY_WORD, &expected, &size));
  args[3] = "--scalar-selection=legacy";
  assert(badge_codegen_main(8, args) == 2);
  args[1] = "--scalar-selection=invalid";
  assert(badge_codegen_main(8, args) == 2);
  args[1] = "--scalar-selection=thumb";
  args[3] = "--memory-selection=byte";
  assert(badge_codegen_main(8, args) == 2);
  args[3] = "--frame-addressing=direct";
  args[4] = "--memory-selection=invalid";
  assert(badge_codegen_main(8, args) == 2);
  FILE *f = fopen(path, "rb");
  char *actual = malloc(size);
  assert(f && actual && fread(actual, 1, size, f) == size);
  assert(fgetc(f) == EOF && !ferror(f) && fclose(f) == 0);
  assert(!memcmp(actual, expected, size));
  free(actual);
  free(expected);
  for (unsigned selection = BADGE_SELECTION_THUMB_REGISTERS;
       selection <= BADGE_SELECTION_THUMB_LAYOUT; selection++) {
    args[1] = selection == BADGE_SELECTION_THUMB_LAYOUT
                  ? "--scalar-selection=thumb-layout"
                  : "--scalar-selection=thumb-registers";
    args[4] = "--memory-selection=word";
    assert(badge_codegen_main(8, args) == 0);
    assert(badge_codegen_compile_with_selections(
        source, sizeof(source) - 1, "selection-cli.ll",
        (badge_codegen_options){BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL},
        (badge_scalar_selection)selection, BADGE_MEMORY_WORD, &expected, &size));
    f = fopen(path, "rb");
    actual = malloc(size);
    assert(f && actual && fread(actual, 1, size, f) == size);
    assert(fgetc(f) == EOF && !ferror(f) && fclose(f) == 0);
    assert(!memcmp(actual, expected, size));
    free(actual);
    free(expected);
  }
}
static void promoted_api_tests(void) {
  const char source[] = TARGET
      "declare i32 @callee(i32, i32, i32, i32, i32)\n"
      "define i32 @caller(i32 %a, i32 %b) {\nentry:\n"
      "%c = icmp slt i32 %a, %b\nbr i1 %c, label %yes, label %no\n"
      "yes:\n%r = call i32 @callee(i32 %a, i32 %b, i32 1, i32 2, i32 3)\n"
      "ret i32 %r\nno:\nret i32 %a\n}\n";
  badge_codegen_options options = {BADGE_FRAME_DIRECT,
                                   BADGE_RESIDENCY_GLOBAL_COMPACT};
  char *expected = NULL;
  size_t expected_size = 0;
  assert(badge_codegen_compile_with_selections(
      source, sizeof(source) - 1, "default.ll", options,
      BADGE_SELECTION_THUMB_WIDTHS, BADGE_MEMORY_WORD, &expected, &expected_size));
  for (unsigned mode = 0; mode < 4; mode++) {
    char *actual = NULL;
    size_t size = 0;
    int ok;
    if (mode == 0)
      ok = badge_codegen_compile(source, sizeof(source) - 1, "default.ll",
                                 &actual, &size);
    else if (mode == 1)
      ok = badge_codegen_compile_with_options(
          source, sizeof(source) - 1, "default.ll", options, &actual, &size);
    else if (mode == 2)
      ok = badge_codegen_compile_with_frame_addressing(
          source, sizeof(source) - 1, "default.ll", BADGE_FRAME_DIRECT,
          &actual, &size);
    else
      ok = badge_codegen_compile_with_scalar_selection(
          source, sizeof(source) - 1, "default.ll", options,
          BADGE_SELECTION_THUMB_WIDTHS, &actual, &size);
    assert(ok && size == expected_size && !memcmp(actual, expected, size + 1));
    free(actual);
  }
  free(expected);
}
static void api_tests(const char *input, const char *path) {
  promoted_api_tests();
  const char source[] = TARGET
      "@g = global i32 7\n" FN("%p = ptrtoint ptr @g to i32\nret i32 %p");
  char unchanged[sizeof(source)];
  memcpy(unchanged, source, sizeof(source));
  char *expected = NULL;
  size_t expected_size = 0;
  assert(badge_codegen_compile(source, sizeof(source) - 1, "api.ll", &expected,
                               &expected_size));
  assert(strstr(expected,
                "@ branch-layout: active 1 fallthroughs 1 inversions 0"));
  assert(expected_size == strlen(expected));
  assert(!memcmp(source, unchanged, sizeof(source)));
  save(input, source);
  char *args[] = {"badge-codegen", (char *)input, "-o", (char *)path, NULL};
  assert(badge_codegen_main(4, args) == 0);
  FILE *f = fopen(path, "rb");
  assert(f);
  char *actual = malloc(expected_size + 1);
  assert(actual);
  assert(fread(actual, 1, expected_size, f) == expected_size);
  assert(fgetc(f) == EOF && !ferror(f) && fclose(f) == 0);
  assert(!memcmp(actual, expected, expected_size));
  free(actual);
  for (unsigned k = 0; k < 3; k++) {
    char *assembly = NULL;
    size_t size = 17;
    const char invalid[] = TARGET FN("ret i32 %missing");
    assert(!badge_codegen_compile(invalid, sizeof(invalid) - 1, "bad.ll",
                                  &assembly, &size));
    assert(assembly == NULL && size == 0);
    assert(badge_codegen_compile(source, sizeof(source) - 1, "again.ll",
                                 &assembly, &size));
    assert(size == expected_size && !memcmp(assembly, expected, size + 1));
    free(assembly);
  }
  char *assembly = NULL;
  size_t size = 17;
  assert(!badge_codegen_compile(source, sizeof(source), "nul.ll", &assembly,
                                &size));
  assert(assembly == NULL && size == 0);
  const size_t limit = 16u * 1024u * 1024u;
  assert(
      !badge_codegen_compile(source, limit + 1, "large.ll", &assembly, &size));
  assert(assembly == NULL && size == 0);
  char *bounded = malloc(limit);
  assert(bounded);
  memcpy(bounded, source, sizeof(source) - 1);
  bounded[sizeof(source) - 1] = ';';
  memset(bounded + sizeof(source), ' ', limit - sizeof(source));
  assert(
      badge_codegen_compile(bounded, limit, "boundary.ll", &assembly, &size));
  assert(size == expected_size && !memcmp(assembly, expected, size + 1));
  free(bounded);
  free(assembly);
  assembly = NULL;
  assert(!badge_codegen_compile(NULL, 0, "null.ll", &assembly, &size));
  assert(assembly == NULL && size == 0);
  assert(!badge_codegen_compile(source, sizeof(source) - 1, NULL, &assembly,
                                &size));
  assert(!badge_codegen_compile(source, sizeof(source) - 1, "api.ll", NULL,
                                &size));
  assert(!badge_codegen_compile(source, sizeof(source) - 1, "api.ll", &assembly,
                                NULL));
  assert(assembly == NULL && size == 0);
  free(expected);
  assembly = NULL;
  assert(!badge_codegen_compile_with_frame_addressing(
      source, sizeof(source) - 1, "bad-mode.ll", (badge_frame_addressing)99,
      &assembly, &size));
  assert(!assembly && !size);
  char *direct[] = {"badge-codegen", "--frame-addressing=direct",
                    (char *)input,   "-o",
                    (char *)path,    NULL};
  assert(badge_codegen_main(5, direct) == 0);
  direct[1] = "--frame-addressing=invalid";
  assert(badge_codegen_main(5, direct) == 2);
  char *local[] = {"badge-codegen",
                   "--register-residency=local",
                   "--frame-addressing=direct",
                   (char *)input,
                   "-o",
                   (char *)path,
                   NULL};
  for (unsigned residency = BADGE_RESIDENCY_LOCAL;
       residency <= BADGE_RESIDENCY_GLOBAL_COMPACT; residency++) {
    local[1] = residency == BADGE_RESIDENCY_LOCAL
                   ? "--register-residency=local"
                   : residency == BADGE_RESIDENCY_GLOBAL_COMPACT
                   ? "--register-residency=global-compact"
                   : residency == BADGE_RESIDENCY_GLOBAL_FLEX
                   ? "--register-residency=global-flex"
                   : "--register-residency=global";
    assert(badge_codegen_main(6, local) == 0);
    assert(badge_codegen_compile_with_options(
        source, sizeof(source) - 1, "api-options.ll",
        (badge_codegen_options){BADGE_FRAME_DIRECT,
                                (badge_register_residency)residency},
        &assembly, &size));
    char *duplicate[] = {"badge-codegen",
                         local[1],
                         "--register-residency=stack",
                         (char *)input,
                         "-o",
                         (char *)path,
                         NULL};
    assert(badge_codegen_main(6, duplicate) == 2);
    local[1] = "--register-residency=invalid";
    assert(badge_codegen_main(6, local) == 2);
    f = fopen(path, "rb");
    assert(f);
    actual = malloc(size);
    assert(actual && fread(actual, 1, size, f) == size);
    assert(fgetc(f) == EOF && fclose(f) == 0 &&
           !memcmp(actual, assembly, size));
    free(actual);
    free(assembly);
    assembly = NULL;
  }
}
static unsigned layout_branch_count(const char *assembly) {
  unsigned count = 0;
  for (const char *p = assembly; (p = strstr(p, "\nb.w ")); p++)
    count++;
  return count;
}
static void layout_tests(const char *path) {
  const struct {
    const char *source;
    unsigned fallthroughs, inversions;
  } cases[] = {
      {TARGET FN("br label %body\nbody:\nret i32 %x"), 2, 0},
      {TARGET FN("%c = icmp ne i32 %x, 0\n"
                 "br i1 %c, label %yes, label %no\n"
                 "yes:\nret i32 %x\nno:\nret i32 0"), 2, 1},
      {TARGET FN("%c = icmp ne i32 %x, 0\n"
                 "br i1 %c, label %yes, label %no\n"
                 "no:\nret i32 0\nyes:\nret i32 %x"), 2, 0},
      {TARGET FN("%c = icmp ne i32 %x, 0\n"
                 "br i1 %c, label %yes, label %no\n"
                 "yes:\n%y = phi i32 [%x, %entry]\nret i32 %y\n"
                 "no:\n%n = phi i32 [0, %entry]\nret i32 %n"), 1, 0},
      {TARGET FN("%c = icmp ne i32 %x, 0\n"
                 "br i1 %c, label %body, label %body\n"
                 "body:\n%v = phi i32 [%x, %entry], [%x, %entry]\n"
                 "ret i32 %v"), 2, 0},
      {TARGET FN("%wide = zext i32 %x to i64\nbr label %body\n"
                 "body:\n%v = phi i64 [%wide, %entry]\n"
                 "%n = trunc i64 %v to i32\nret i32 %n"), 2, 0},
      {TARGET FN("switch i32 %x, label %fallback [\ni32 1, label %one\n]\n"
                 "fallback:\nret i32 0\none:\nret i32 1"), 2, 0},
      {TARGET FN("br label %header\nheader:\n"
                 "%i = phi i32 [0, %entry], [%next, %body]\n"
                 "%go = icmp ult i32 %i, %x\n"
                 "br i1 %go, label %body, label %exit\n"
                 "body:\n%next = add i32 %i, 1\nbr label %header\n"
                 "exit:\nret i32 %i"), 3, 1}};
  for (unsigned k = 0; k < sizeof(cases) / sizeof(*cases); k++)
    for (unsigned frame = 0; frame < 2; frame++)
      for (unsigned residency = 0; residency <= BADGE_RESIDENCY_GLOBAL_COMPACT; residency++)
        for (unsigned memory = 0; memory < 2; memory++) {
          int active = residency >= BADGE_RESIDENCY_GLOBAL;
          char expected[100];
          snprintf(expected, sizeof(expected),
                   "@ branch-layout: active %d fallthroughs %u inversions %u",
                   active, active ? cases[k].fallthroughs : 0,
                   active ? cases[k].inversions : 0);
          fixture_mode(cases[k].source, expected, 1, path,
                       (badge_frame_addressing)frame,
                       (badge_register_residency)residency,
                       BADGE_SELECTION_THUMB_LAYOUT,
                       (badge_memory_selection)memory);
          char *control = NULL, *candidate = NULL;
          size_t a = 0, b = 0;
          badge_codegen_options options = {
              (badge_frame_addressing)frame, (badge_register_residency)residency};
          assert(badge_codegen_compile_with_selections(
              cases[k].source, strlen(cases[k].source), "layout.ll", options,
              BADGE_SELECTION_THUMB_REGISTERS, (badge_memory_selection)memory,
              &control, &a));
          assert(badge_codegen_compile_with_selections(
              cases[k].source, strlen(cases[k].source), "layout.ll", options,
              BADGE_SELECTION_THUMB_LAYOUT, (badge_memory_selection)memory,
              &candidate, &b));
          assert(layout_branch_count(control) ==
                 layout_branch_count(candidate) +
                     (active ? cases[k].fallthroughs : 0));
          free(control);
          free(candidate);
        }
  const char *excluded[] = {
      TARGET "@port = global i32 0\n" FN(
          "%v = load volatile i32, ptr @port\nbr label %body\nbody:\nret i32 %v"),
      TARGET "declare i32 @effect(i32)\n" FN(
          "%v = call i32 @effect(i32 %x)\nbr label %body\nbody:\nret i32 %v"),
      TARGET FN("call void asm sideeffect \"nop\", \"~{memory}\"()\n"
                "br label %body\nbody:\nret i32 %x"),
      TARGET FN("%p = inttoptr i32 %x to ptr\n"
                "%v = load i32, ptr %p\nbr label %body\nbody:\nret i32 %v")};
  for (unsigned k = 0; k < sizeof(excluded) / sizeof(*excluded); k++) {
    char *control = NULL, *candidate = NULL;
    size_t a = 0, b = 0;
    badge_codegen_options options = {BADGE_FRAME_DIRECT, BADGE_RESIDENCY_GLOBAL_COMPACT};
    assert(badge_codegen_compile_with_scalar_selection(
        excluded[k], strlen(excluded[k]), "layout-control.ll", options,
        BADGE_SELECTION_THUMB_REGISTERS, &control, &a));
    assert(badge_codegen_compile_with_scalar_selection(
        excluded[k], strlen(excluded[k]), "layout-candidate.ll", options,
        BADGE_SELECTION_THUMB_LAYOUT, &candidate, &b));
    const char *annotation =
        "@ branch-layout: active 0 fallthroughs 0 inversions 0\n";
    char *at = strstr(candidate, annotation);
    assert(at && b == a + strlen(annotation));
    memmove(at, at + strlen(annotation), strlen(at + strlen(annotation)) + 1);
    assert(!strcmp(control, candidate));
    free(control);
    free(candidate);
  }
}
static void host_tests(void) {
  cg_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "host-core";
  c->fn = cg_alloc(c, sizeof(*c->fn));
  cg_type t = {.size = 4, .align = 4, .bits = 32};
  char **names = cg_alloc(c, 1024 * sizeof(*names));
  for (unsigned i = 0; i < 1024; i++) {
    names[i] = cg_alloc(c, 32);
    snprintf(names[i], 32, "%%value_%u", i);
    cg_value *v = cg_define(c, names[i], &t);
    assert(cg_find(c, names[i]) == v && v->slot == i * 4);
  }
  for (unsigned i = 0; i < 1024; i++)
    assert(!strcmp(cg_find(c, names[i])->name, names[i]));
  if (!setjmp(c->failure)) {
    cg_define(c, names[0], &t);
    assert(!"duplicate must fail after table growth");
  }
  if (!setjmp(c->failure)) {
    cg_find(c, "%missing");
    assert(!"undefined lookup must fail");
  }
  cg_emit(c, "");
  assert(c->size == 0 && !strcmp(c->out, ""));
  cg_emit(c, "head %u %%\n", 17u);
  const char *prefix = "head 17 %\n";
  char *body = cg_alloc(c, 20001);
  memset(body, 'x', 20000);
  cg_emit(c, "%s:%08x", body, 0xabcdefu);
  assert(c->size == strlen(prefix) + 20000 + strlen(":00abcdef"));
  assert(!memcmp(c->out, prefix, strlen(prefix)));
  assert(!memcmp(c->out + strlen(prefix), body, 20000));
  assert(!strcmp(c->out + strlen(prefix) + 20000, ":00abcdef"));
  cg_emit(c, "%s", body);
  assert(strlen(c->out) == c->size && c->capacity > 32768);
  if (!setjmp(c->failure)) {
    cg_emit(c, "%*s", 64 * 1024 * 1024 + 1, "");
    assert(!"output limit must fail before growing the buffer");
  }
  c->memory = 128u * 1024u * 1024u;
  if (!setjmp(c->failure)) {
    cg_define(c, names[0], &t);
    assert(!"duplicate must fail without allocating at the budget");
  }
  if (!setjmp(c->failure)) {
    cg_alloc(c, 1);
    assert(!"allocation limit must fail");
  }
  while (c->allocations) {
    cg_allocation *a = c->allocations;
    c->allocations = a->next;
    free(a);
  }
  free(c->out);
  free(c);
}
int main(int argc, char **argv) {
  if (argc > 2 || (argc == 2 && strcmp(argv[1], "--layout") &&
                  strcmp(argv[1], "--api") && strcmp(argv[1], "--host"))) {
    fprintf(stderr, "Usage: badge_codegen_test [--layout|--api|--host]\n");
    return 2;
  }
  if (argc == 1 || !strcmp(argv[1], "--host"))
    host_tests();
  if (argc == 2 && !strcmp(argv[1], "--host")) {
    puts("badge-codegen: indexed lookups, buffered formatting and resource limits PASS");
    return 0;
  }
  char dir[] = "build/codegen-test-XXXXXX";
  assert(mkdtemp(dir));
  char path[128], input[128], alias[128];
  assert(snprintf(path, sizeof(path), "%s/output.s", dir) > 0);
  assert(snprintf(input, sizeof(input), "%s/input.ll", dir) > 0);
  assert(snprintf(alias, sizeof(alias), "%s/alias.ll", dir) > 0);
  if (argc == 2) {
    if (!strcmp(argv[1], "--layout"))
      layout_tests(path);
    else {
      api_tests(input, path);
      selection_cli_tests(input, path);
      assert(unlink(input) == 0);
    }
    assert(unlink(path) == 0 && rmdir(dir) == 0);
    printf("badge-codegen: %s focused checks PASS\n", argv[1]);
    return 0;
  }
  api_tests(input, path);
  selection_cli_tests(input, path);
  layout_tests(path);
  frame_tests(path);
  residency_tests();
  global_tests();
  flexible_tests(path);
  compact_tests(path);
  selection_tests();
  register_operand_tests(path);
  memory_tests(path);
  static const struct {
    const char *ir, *assembly;
  } positive[] = {
      {TARGET FN("ret i32 %x"), ".thumb_func"},
      {TARGET FN("%a = add nsw i32 %x, 17\nret i32 %a"), "add.w"},
      {TARGET FN("%a = sub i32 %x, 17\nret i32 %a"), "sub.w"},
      {TARGET FN("%a = mul i32 %x, 17\nret i32 %a"), "mul"},
      {TARGET FN("%a = sdiv i32 %x, 7\nret i32 %a"), "sdiv"},
      {TARGET FN("%a = urem i32 %x, 7\nret i32 %a"), "mls"},
      {TARGET FN("%a = and i32 %x, 7\nret i32 %a"), "and.w"},
      {TARGET FN("%a = or disjoint i32 %x, 8\nret i32 %a"), "orr.w"},
      {TARGET FN("%a = xor i32 %x, -1\nret i32 %a"), "eor.w"},
      {TARGET FN("%a = shl nuw i32 %x, 2\nret i32 %a"), "lsl.w"},
      {TARGET FN("%a = lshr i32 %x, 2\nret i32 %a"), "lsr.w"},
      {TARGET FN("%a = ashr i32 %x, 2\nret i32 %a"), "asr.w"},
      {TARGET FN("%a = trunc i32 %x to i8\n%b = sext i8 %a to i32\nret i32 %b"),
       "sxtb"},
      {TARGET FN("%a = trunc i32 %x to i16\n%b = zext nneg i16 %a to i32\nret "
                 "i32 %b"),
       "#16"},
      {TARGET FN("%a = icmp samesign slt i32 %x, 3\n%b = select i1 %a, i32 7, "
                 "i32 9\nret i32 %b"),
       "movlt"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = mul i64 %a, 65537\n%c = trunc "
                 "i64 %b to i32\nret i32 %c"),
       "umull"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = add i64 %a, 4294967295\n%c = "
                 "sub i64 %b, 7\n%d = trunc i64 %c to i32\nret i32 %d"),
       "sbc.w"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = sdiv i64 %a, 1024\n%c = trunc "
                 "i64 %b to i32\nret i32 %c"),
       "adc r1"},
      {TARGET FN("%a = zext i32 %x to i64\n%b = icmp uge i64 %a, "
                 "4294967295\n%c = zext i1 %b to i32\nret i32 %c"),
       "cmpyes"},
      {TARGET FN("%a = zext i32 %x to i64\n%b = icmp eq i64 %a, 17\n%c = zext "
                 "i1 %b to i32\nret i32 %c"),
       "eor.w"},
      {TARGET FN("%p = alloca i32, align 4\nstore i32 %x, ptr %p, align "
                 "1\n%a = load i32, ptr %p, align 1\nret i32 %a"),
       "strb.w"},
      {TARGET FN("%p = alloca [8192 x i8], align 8\nstore i32 %x, ptr %p, "
                 "align 4\n%a = load i32, ptr %p, align 4\nret i32 %a"),
       "stack-frame: 8208"},
      {TARGET FN("%a = freeze i32 %x\nret i32 %a"), "ldr.w"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = shl i64 %a, 1\n%c = trunc i64 "
                 "%b to i32\nret i32 %c"),
       "lsl.w r1"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = lshr i64 %a, 63\n%c = trunc "
                 "i64 %b to i32\nret i32 %c"),
       "lsr.w r0, r1, #31"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = ashr i64 %a, 32\n%c = trunc "
                 "i64 %b to i32\nret i32 %c"),
       "asr.w r1, r1, #31"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = sdiv i64 %a, 1\n%c = trunc i64 "
                 "%b to i32\nret i32 %c"),
       "ldr.w"},
      {TARGET
       "@g = constant [1 x i8] zeroinitializer, align 1\n" FN("ret i32 0"),
       ".rodata.g"},
      {TARGET FN(
           "%p = alloca i32, align 4\nstore volatile i32 %x, ptr %p, align "
           "4\n%a = load volatile i32, ptr %p, align 4\nret i32 %a"),
       "str.w r0, [r4, #0]"},
      {TARGET "@g = constant [3 x i8] c\"a\\00b\", section "
              "\".rodata.fixture\", align 1\n" FN("ret i32 0"),
       ".byte 98"},
      {TARGET "@g = global i32 3, align 4\n@p = global ptr @g, align 4\n" FN(
           "ret i32 0"),
       ".word g"},
      {TARGET
       "define void @f() section \".ramfunc\" align 8 {\nentry:\nret void\n}\n",
       ".section \".ramfunc\""},
      {TARGET
       "define i32 @f(i32 %x) {\nentry:\nbr label %loop\nloop:\n%a = phi i32 [ "
       "%x, %entry ], [ %b, %loop ]\n%b = sub i32 %a, 1\n%c = icmp sgt i32 %b, "
       "0\nbr i1 %c, label %loop, label %exit\nexit:\nret i32 %b\n}\n",
       ".Lcg_edge"},
  };
  for (size_t k = 0; k < sizeof(positive) / sizeof(*positive); k++)
    fixture(positive[k].ir, positive[k].assembly, 1, path);
  static const char *negative[] = {
      "",
      "target triple = \"x86_64-unknown-linux-gnu\"\n",
      TARGET "module asm \"nop\"\n",
      TARGET FN("%s = zext i32 %x to i64\n%a = shl i64 1, %s\nret i32 0"),
      TARGET FN("%a = udiv i64 4, 2\nret i32 0"),
      TARGET FN("%a = sdiv i64 4, 3\nret i32 0"),
      TARGET FN("%a = fadd float 1.0, 2.0\nret i32 0"),
      TARGET FN(
          "%a = load atomic i32, ptr null monotonic, align 4\nret i32 %a"),
      TARGET FN("%a = load volatile i32, ptr null, align 1\nret i32 %a"),
      TARGET FN("%a = call i32 @llvm.smin.i32(i32 %x)\nret i32 %a"),
      TARGET "define i32 @f({ [2 x i32], i32 } %x) {\nentry:\n%a = "
             "extractvalue { [2 x i32], i32 } %x, 0, 1\nret i32 %a\n}\n",
      TARGET "@llvm.used = appending global [1 x ptr] [ptr @f]\n",
      TARGET FN("%p = alloca i32, i32 %x, align 4\nret i32 0"),
      TARGET FN("%p = alloca i32, align 16\nret i32 0"),
      TARGET FN("%p = alloca [65537 x i8], align 1\nret i32 0"),
      TARGET FN("%a = load [12 x i8], ptr null, align 4\nret i32 0"),
      TARGET FN("br label %missing"),
      TARGET FN("ret i32 %missing"),
      TARGET FN("%a = add i32 %x, 1\n%a = add i32 %x, 2\nret i32 %a"),
      TARGET "define fastcc i32 @f() {\nentry:\nret i32 0\n}\n",
      TARGET "define i32 @f(ptr inalloca(i32) %p) {\nentry:\nret i32 0\n}\n",
      TARGET FN("%a = call fastcc i32 @g()\nret i32 %a"),
      TARGET FN("%a = call i32 @g(ptr inreg null)\nret i32 %a"),
      TARGET "@g = global i4294967328 0\n",
      TARGET "@g = global [16777217 x i8] zeroinitializer\n",
      TARGET "@g = global i32 0, section x\n",
      TARGET "define i32 @f(",
  };
  for (size_t k = 0; k < sizeof(negative) / sizeof(*negative); k++)
    fixture(negative[k], NULL, 0, path);
  const char seed[] = TARGET FN("%a = add i32 %x, 3\nret i32 %a");
  for (size_t k = 0; k < sizeof(seed) - 1; k++) {
    char mutation[sizeof(seed)];
    memcpy(mutation, seed, sizeof(seed));
    mutation[k] ^= 64;
    fixture(mutation, NULL, -1, path);
  }
  save(input, TARGET FN("ret i32 %missing"));
  save(path, "preserved");
  char *args[] = {"badge-codegen", input, "-o", path, NULL};
  assert(badge_codegen_main(4, args) == 1);
  char preserved[16] = {0};
  FILE *f = fopen(path, "rb");
  assert(f);
  assert(fread(preserved, 1, sizeof(preserved), f) == 9 && fclose(f) == 0 &&
         !strcmp(preserved, "preserved"));
  save(input, TARGET FN("ret i32 %x"));
  args[3] = input;
  assert(badge_codegen_main(4, args) == 1);
  assert(link(input, alias) == 0);
  args[3] = alias;
  assert(badge_codegen_main(4, args) == 1);
  assert(unlink(alias) == 0 && symlink("input.ll", alias) == 0);
  assert(badge_codegen_main(4, args) == 1);
  assert(badge_codegen_main(1, args) == 2);
  assert(unlink(alias) == 0 && unlink(input) == 0 && unlink(path) == 0 &&
         rmdir(dir) == 0);
  assert(unlink("build/codegen-test.o") == 0);
  printf("badge-codegen: %zu positive, %zu negative, %zu mutation sanitizer "
         "fixtures; API/CLI equivalence, repeated calls, input bounds and "
         "output preservation/alias checks PASS\n",
         sizeof(positive) / sizeof(*positive),
         sizeof(negative) / sizeof(*negative), sizeof(seed) - 1);
  return 0;
}
