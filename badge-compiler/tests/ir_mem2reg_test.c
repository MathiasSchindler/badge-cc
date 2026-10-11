#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "ir_promotion.h"
#include "target.h"
#include "test_mode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TARGET                                                                 \
  "target triple = " BADGE_IR_TRIPLE "\ntarget datalayout = " BADGE_IR_LAYOUT  \
  "\n"
#define FN(body) "define i32 @f(i32 %x) {\nentry:\n" body "\n}\n"
int badge_opt_main(int, char **);
static uint64_t slots, loads, stores, phis, existing, removed;
static void observe(void *context, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  (void)context;
  (void)step;
  if (strcmp(pass, "mem2reg"))
    return;
  if (!strcmp(event, "retained") && !strcmp(reason, "phi_definition"))
    phis += count;
  if (strcmp(event, "transformed"))
    return;
  if (!strcmp(reason, "promoted_slot"))
    slots += count;
  if (!strcmp(reason, "load_removed"))
    loads += count;
  if (!strcmp(reason, "store_removed"))
    stores += count;
  if (!strcmp(reason, "existing_phi_removed"))
    existing += count;
  if (!strcmp(reason, "generated_phi_removed"))
    removed += count;
}
static void oracle(const char *source) {
  const char *tool = badge_test_tool("OPT_TEST_LLVM");
  if (!tool)
    return;
  const char *file = "build/m05-oracle-fixture.ll";
  FILE *f = fopen(file, "wb");
  assert(f && fwrite(source, 1, strlen(source), f) == strlen(source) &&
         !fclose(f));
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    execlp(tool, tool, "-passes=verify", "-disable-output", file, (char *)NULL);
    _exit(127);
  }
  int status;
  assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);
  assert(!unlink(file));
}
static void fixture(const char *source, unsigned ns, unsigned nl, unsigned nw,
                    unsigned np, unsigned ne, const char *present,
                    const char *absent) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "mem2reg.ll";
  c->observe = observe;
  if (setjmp(c->failure)) {
    fprintf(stderr, "unexpected mem2reg failure:\n%s\n", source);
    abort();
  }
  slots = loads = stores = phis = existing = removed = 0;
  ir_pipeline(c, source, strlen(source), "mem2reg");
  if (slots != ns || loads != nl || stores != nw || phis != np ||
      existing != ne) {
    fprintf(stderr,
            "unexpected mem2reg counts: "
            "%llu/%llu/%llu/%llu/%llu\nsource:\n%s\noutput:\n%s\n",
            (unsigned long long)slots, (unsigned long long)loads,
            (unsigned long long)stores, (unsigned long long)phis,
            (unsigned long long)existing, source, c->output);
    abort();
  }
  if (!ns && !ne)
    assert(c->size == strlen(source) && !strcmp(c->output, source));
  if (present)
    assert(strstr(c->output, present));
  if (absent)
    assert(!strstr(c->output, absent));
  oracle(c->output);
  char *result = strdup(c->output);
  assert(result);
  size_t size = c->size;
  c->observe = NULL;
  ir_pipeline(c, result, size, "mem2reg");
  assert(c->size == size && !memcmp(c->output, result, size));
  free(result);
  ir_destroy(c);
  free(c);
}
static void atomic_failure(void) {
  const char *input = "build/m05-cli-input.ll",
             *output = "build/m05-cli-output.ll";
  const char *invalid =
      TARGET FN("%p = alloca i32\nstore i32 %missing, ptr %p\n"
                "%v = load i32, ptr %p\nret i32 %v");
  FILE *f = fopen(input, "wb");
  assert(f && fwrite(invalid, 1, strlen(invalid), f) == strlen(invalid) &&
         !fclose(f));
  f = fopen(output, "wb");
  assert(f && fwrite("sentinel", 1, 8, f) == 8 && !fclose(f));
  char *args[] = {"badge-opt", "--passes=mem2reg", (char *)input,
                  "-o",        (char *)output,     NULL};
  assert(badge_opt_main(5, args) == 1);
  f = fopen(output, "rb");
  assert(f);
  char bytes[16] = {0};
  assert(fread(bytes, 1, sizeof(bytes), f) == 8 && !strcmp(bytes, "sentinel"));
  assert(!fclose(f) && !unlink(input) && !unlink(output));
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "mem2reg-allocation-limit.ll";
  c->memory = 128u * 1024u * 1024u;
  if (!setjmp(c->failure)) {
    const char *valid = TARGET FN("ret i32 %x");
    ir_pipeline(c, valid, strlen(valid), "mem2reg");
    assert(!"exhausted allocation budget must fail");
  }
  ir_destroy(c);
  free(c);
}
int main(void) {
  fixture(TARGET FN("%p = alloca i32\nbr i1 true, label %left, label %right\n"
                    "left:\nstore i32 %x, ptr %p\nbr label %join\n"
                    "right:\nstore i32 9, ptr %p\nbr label %join\n"
                    "join:\n%v = load i32, ptr %p\nret i32 %v"),
          1, 1, 2, 1, 0, "phi i32", "alloca");
  fixture(TARGET FN("%p = alloca i32\nbr i1 true, label %left, label %right\n"
                    "left:\nstore i32 %x, ptr %p\nbr label %join\n"
                    "right:\nstore i32 %x, ptr %p\nbr label %join\n"
                    "join:\n%v = load i32, ptr %p\nret i32 %v"),
          1, 1, 2, 0, 0, "ret i32 %x", "phi");
  assert(removed == 1);
  fixture(TARGET FN(
              "%p = alloca i32\nbr i1 true, label %a, label %b\n"
              "a:\nstore i32 %x, ptr %p\nbr label %first\n"
              "b:\nstore i32 %x, ptr %p\nbr label %first\n"
              "first:\n%v = load i32, ptr %p\nbr i1 true, label %c, label %d\n"
              "c:\nstore i32 %v, ptr %p\nbr label %last\n"
              "d:\nstore i32 %x, ptr %p\nbr label %last\n"
              "last:\n%r = load i32, ptr %p\nret i32 %r"),
          1, 2, 4, 0, 0, "ret i32 %x", "phi");
  assert(removed == 2);
  fixture(
      TARGET FN(
          "%p = alloca i32\nbr i1 true, label %left, label %right\n"
          "left:\nstore i32 17, ptr %p\nbr label %join\n"
          "right:\nbr label %join\njoin:\n%v = load i32, ptr %p\nret i32 %v"),
      1, 1, 1, 1, 0, "undef", "alloca");
  fixture(TARGET FN("%p = alloca i32\nbr label %body\nbody:\n"
                    "%early = load i32, ptr %p\nstore i32 %x, ptr %p\n"
                    "ret i32 %early"),
          1, 1, 1, 0, 0, "ret i32 undef", "alloca");
  fixture(TARGET FN("%p = alloca i32\nstore i32 0, ptr %p\nbr label %loop\n"
                    "loop:\n%v = load i32, ptr %p\n%next = add i32 %v, 1\n"
                    "store i32 %next, ptr %p\n%c = icmp ult i32 %next, %x\n"
                    "br i1 %c, label %loop, label %exit\n"
                    "exit:\n%r = load i32, ptr %p\nret i32 %r"),
          1, 2, 2, 1, 0, "phi i32", "alloca");
  fixture(TARGET FN(
              "%p = alloca i32\nbr label %loop\nloop:\n"
              "%v = load i32, ptr %p\n%next = add i32 %v, 1\n"
              "store i32 %next, ptr %p\nbr i1 false, label %loop, label %exit\n"
              "exit:\nret i32 %next"),
          1, 1, 1, 1, 0, "undef", "alloca");
  fixture(
      TARGET FN("%p = alloca i32\n%q = alloca i32\n"
                "store i32 13, ptr %p\nstore i32 29, ptr %q\nbr label %loop\n"
                "loop:\n%a = load i32, ptr %p\n%b = load i32, ptr %q\n"
                "store i32 %b, ptr %p\nstore i32 %a, ptr %q\n"
                "br i1 false, label %loop, label %exit\n"
                "exit:\n%c = load i32, ptr %p\n%d = load i32, ptr %q\n"
                "%r = add i32 %c, %d\nret i32 %r"),
      2, 4, 4, 2, 0, "phi i32", "alloca");
  fixture(
      TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                "br i1 true, label %left, label %right\n"
                "left:\n%a = load i32, ptr %p\nbr label %join\n"
                "right:\n%b = load i32, ptr %p\nbr label %join\n"
                "join:\n%r = phi i32 [%a, %left], [%b, %right]\nret i32 %r"),
      1, 2, 1, 0, 1, "ret i32 %x", "phi");
  fixture(
      TARGET FN(
          "%p = alloca i32\nbr i1 true, label %left, label %right\n"
          "left:\nstore i32 1, ptr %p\nbr i1 true, label %join, label %join\n"
          "right:\nstore i32 2, ptr %p\nbr label %join\n"
          "join:\n%v = load i32, ptr %p\nret i32 %v"),
      1, 1, 2, 1, 0, "[1, %left], [1, %left]", "alloca");
  fixture(
      TARGET FN(
          "%p = alloca i32\nbr i1 true, label %left, label %right\n"
          "dead:\nbr label %join\nleft:\nstore i32 1, ptr %p\nbr label %join\n"
          "right:\nstore i32 2, ptr %p\nbr label %join\n"
          "join:\n%v = load i32, ptr %p\nret i32 %v"),
      1, 1, 2, 1, 0, "[undef, %dead]", "alloca");
  fixture(
      TARGET FN("%p = alloca i32\nbr i1 true, label %a, label %b\n"
                "a:\nstore i32 1, ptr %p\nbr i1 true, label %b, label %exit\n"
                "b:\n%v = load i32, ptr %p\n%n = add i32 %v, 1\n"
                "store i32 %n, ptr %p\nbr i1 false, label %a, label %exit\n"
                "exit:\n%r = load i32, ptr %p\nret i32 %r"),
      1, 2, 2, 2, 0, "undef", "alloca");
  fixture(TARGET "define i64 @f(i1 %c) {\nentry:\n%p = alloca i64\n"
                 "br i1 %c, label %left, label %right\n"
                 "left:\nstore i64 4294967297, ptr %p\nbr label %join\n"
                 "right:\nstore i64 -1, ptr %p\nbr label %join\n"
                 "join:\n%v = load i64, ptr %p\nret i64 %v\n}\n",
          1, 1, 2, 1, 0, "phi i64", "alloca");
  fixture(TARGET "@a = global i32 1\n@b = global i32 2\n"
                 "define ptr @f(i1 %c) {\nentry:\n%p = alloca ptr\n"
                 "br i1 %c, label %left, label %right\n"
                 "left:\nstore ptr @a, ptr %p\nbr label %join\n"
                 "right:\nstore ptr @b, ptr %p\nbr label %join\n"
                 "join:\n%v = load ptr, ptr %p\nret ptr %v\n}\n",
          1, 1, 2, 1, 0, "phi ptr", "alloca");
  fixture(TARGET "@a = global i32 1\n"
                 "define ptr @f(i1 %c) {\nentry:\n%p = alloca ptr\n"
                 "br i1 %c, label %left, label %right\n"
                 "left:\nstore ptr @a, ptr %p\nbr label %join\n"
                 "right:\nstore ptr @\"\\61\", ptr %p\nbr label %join\n"
                 "join:\n%v = load ptr, ptr %p\nret ptr %v\n}\n",
          1, 1, 2, 0, 0, "ret ptr @a", "phi");
  fixture(TARGET "define i8 @f(i1 %c) {\nentry:\n%p = alloca i8\n"
                 "br i1 %c, label %left, label %right\n"
                 "left:\nstore i8 -1, ptr %p\nbr label %join\n"
                 "right:\nstore i8 255, ptr %p\nbr label %join\n"
                 "join:\n%v = load i8, ptr %p\nret i8 %v\n}\n",
          1, 1, 2, 0, 0, "ret i8 -1", "phi");
  fixture(TARGET "define i1 @f(i1 %c) {\nentry:\n%p = alloca i1\n"
                 "br i1 %c, label %a, label %b\n"
                 "a:\nstore i1 true, ptr %p\nbr label %join\n"
                 "b:\nstore i1 false, ptr %p\nbr label %join\n"
                 "join:\n%v = load i1, ptr %p\nret i1 %v\n}\n",
          1, 1, 2, 1, 0, "phi i1", "alloca");
  fixture(TARGET "define i16 @f(i1 %c) {\nentry:\n%p = alloca i16\n"
                 "br i1 %c, label %a, label %b\n"
                 "a:\nstore i16 -32768, ptr %p\nbr label %join\n"
                 "b:\nstore i16 32767, ptr %p\nbr label %join\n"
                 "join:\n%v = load i16, ptr %p\nret i16 %v\n}\n",
          1, 1, 2, 1, 0, "phi i16", "alloca");
  fixture(TARGET FN("%p = alloca i32\n%\".badge.opt.1\" = add i32 %x, 1\n"
                    "br i1 true, label %\".badge.opt.2\", label %right\n"
                    "\".badge.opt.2\":\nstore i32 %x, ptr %p\nbr label %join\n"
                    "right:\nstore i32 9, ptr %p\nbr label %join\n"
                    "join:\n%v = load i32, ptr %p\nret i32 %v"),
          1, 1, 2, 1, 0, "%\".badge.opt.3\" = phi", "alloca");
  fixture(TARGET FN("%p = alloca i32\nbr i1 true, label %left, label %right\n"
                    "left:\nstore i32 1, ptr %p\nbr label %join\n"
                    "right:\nstore i32 2, ptr %p\nbr label %join\n"
                    "join:\nret i32 %x"),
          1, 0, 2, 0, 0, "ret i32 %x", "phi");
  fixture(TARGET FN("%p = alloca i32\n%q = getelementptr i32, ptr %p, i32 0\n"
                    "store i32 %x, ptr %q\nret i32 %x"),
          0, 0, 0, 0, 0, "getelementptr", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore volatile i32 %x, ptr %p\n"
                    "%v = load i32, ptr %p\nret i32 %v"),
          0, 0, 0, 0, 0, "volatile", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                    "%v = load volatile i32, ptr %p\nret i32 %v"),
          0, 0, 0, 0, 0, "volatile", NULL);
  fixture(
      TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                "%v = load i32, ptr %p, !tag !0\nret i32 %v") "!0 = !{i32 7}\n",
      0, 0, 0, 0, 0, "!tag", NULL);
  fixture(TARGET "declare void @escape(ptr)\n" FN(
              "%p = alloca i32\ncall void @escape(ptr %p)\nret i32 %x"),
          0, 0, 0, 0, 0, "@escape", NULL);
  fixture(TARGET FN("%p = alloca i32\ncall void asm sideeffect \"\", \"r\"(ptr "
                    "%p)\nret i32 %x"),
          0, 0, 0, 0, 0, "asm", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore i8 7, ptr %p\nret i32 %x"), 0, 0, 0,
          0, 0, "store i8", NULL);
  fixture(TARGET FN("%p = alloca i32, i32 %x\nret i32 %x"), 0, 0, 0, 0, 0,
          "alloca", NULL);
  fixture(TARGET FN("%p = alloca [2 x i32]\nret i32 %x"), 0, 0, 0, 0, 0,
          "alloca", NULL);
  fixture(TARGET FN("br label %body\nbody:\n%p = alloca i32\nret i32 %x"), 0, 0,
          0, 0, 0, "alloca", NULL);
  fixture(TARGET FN("%p = alloca i32\nret i32 %x\n"
                    "dead:\nstore i32 9, ptr %p\nunreachable"),
          0, 0, 0, 0, 0, "dead:", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore i32 poison, ptr %p\n"
                    "%v = load i32, ptr %p\nret i32 %v"),
          1, 1, 1, 0, 0, "ret i32 poison", "alloca");
  fixture(TARGET "declare i32 @f(i32)\n", 0, 0, 0, 0, 0, "@f", NULL);
  fixture(TARGET FN("br label %loop\nloop:\n"
                    "%v = phi i32 [undef, %entry], [%v, %loop]\n"
                    "br i1 false, label %loop, label %exit\nexit:\nret i32 %v"),
          0, 0, 0, 0, 1, "ret i32 undef", "phi");
  fixture(
      TARGET FN("br i1 true, label %a, label %b\n"
                "a:\nbr label %join\nb:\nbr label %join\n"
                "join:\n%v = phi i32 [poison, %a], [undef, %b]\nret i32 %v"),
      0, 0, 0, 0, 0, "phi", NULL);
  fixture(TARGET FN("br i1 true, label %a, label %b\n"
                    "a:\nbr label %join\nb:\nbr label %join\n"
                    "join:\n%v = phi i32 [%x, %a], [%x, %b], !tag !0\nret i32 "
                    "%v") "!0 = !{i32 7}\n",
          0, 0, 0, 0, 0, "!tag", NULL);
  char *chain = malloc(2 * 1024 * 1024);
  assert(chain);
  size_t used =
      (size_t)sprintf(chain, TARGET "define i32 @f(i32 %%x) {\nentry:\n");
  for (unsigned k = 0; k < 1000; k++)
    used += (size_t)sprintf(chain + used, "%%s%u = alloca i32\n", k);
  used += (size_t)sprintf(chain + used, "br label %%b999\n");
  for (unsigned k = 0; k < 1000; k++) {
    used += (size_t)sprintf(chain + used, "b%u:\nstore i32 ", k);
    if (k == 999)
      used += (size_t)sprintf(chain + used, "%%x");
    else
      used += (size_t)sprintf(chain + used, "%%v%u", k + 1);
    used += (size_t)sprintf(
        chain + used, ", ptr %%s%u\n%%v%u = load i32, ptr %%s%u\n", k, k, k);
    if (k)
      used += (size_t)sprintf(chain + used, "br label %%b%u\n", k - 1);
    else
      used += (size_t)sprintf(chain + used, "ret i32 %%v0\n");
  }
  assert(used + 3 < 2 * 1024 * 1024);
  strcpy(chain + used, "}\n");
  fixture(chain, 1000, 1000, 1000, 0, 0, "ret i32 %x", "alloca");
  used = (size_t)sprintf(chain, TARGET "define i32 @f(i32 %%x) {\nentry:\n");
  for (unsigned k = 0; k < 5000; k++)
    used += (size_t)sprintf(chain + used, "%%s%u = alloca i32\n", k);
  used += (size_t)sprintf(
      chain + used, "store i32 %%x, ptr %%s0\n%%v0 = load i32, ptr %%s0\n");
  for (unsigned k = 1; k < 5000; k++)
    used += (size_t)sprintf(
        chain + used,
        "store i32 %%v%u, ptr %%s%u\n%%v%u = load i32, ptr %%s%u\n", k - 1, k,
        k, k);
  assert(used + 32 < 2 * 1024 * 1024);
  strcpy(chain + used, "ret i32 %v4999\n}\n");
  fixture(chain, 5000, 5000, 5000, 0, 0, "ret i32 %x", "alloca");
  free(chain);
  atomic_failure();
  const ir_pass *pass = ir_find_pass("mem2reg", 7);
  assert(pass && pass->role == IR_PASS_OPTIMIZATION);
  puts("M05: pruned SSA, branches/loops/irreducible CFG, parallel/dead "
       "predecessor edges, undef, typed PHIs, cleanup, exclusions, reversed "
       "deep CFG and atomic/resource failures PASS");
  return 0;
}
