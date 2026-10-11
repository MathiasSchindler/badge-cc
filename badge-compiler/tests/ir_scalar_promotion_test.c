#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "ir_analysis.h"
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
static uint64_t slots, loads, stores, skipped;
static void observe(void *context, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  (void)context;
  (void)step;
  if (strcmp(pass, "scalar-promotion"))
    return;
  if (!strcmp(event, "skipped"))
    skipped += count;
  if (strcmp(event, "transformed"))
    return;
  if (!strcmp(reason, "promoted_slot"))
    slots += count;
  if (!strcmp(reason, "load_removed"))
    loads += count;
  if (!strcmp(reason, "store_removed"))
    stores += count;
}
static void oracle(const char *source) {
  const char *tool = badge_test_tool("OPT_TEST_LLVM");
  if (!tool)
    return;
  const char *file = "build/m04-oracle-fixture.ll";
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
static void fixture(const char *source, const char *selection, unsigned ns,
                    unsigned nl, unsigned nw, unsigned nk, const char *present,
                    const char *absent) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "scalar-promotion.ll";
  c->observe = observe;
  if (setjmp(c->failure)) {
    fprintf(stderr, "unexpected scalar-promotion failure for:\n%s\n", source);
    abort();
  }
  slots = loads = stores = skipped = 0;
  ir_pipeline(c, source, strlen(source), selection);
  if (slots != ns || loads != nl || stores != nw || skipped != nk) {
    fprintf(stderr, "unexpected promotion counts for:\n%s\noutput:\n%s\n",
            source, c->output);
    abort();
  }
  if (!ns)
    assert(c->size == strlen(source) && !strcmp(c->output, source));
  if (present)
    assert(strstr(c->output, present));
  if (absent)
    assert(!strstr(c->output, absent));
  oracle(c->output);
  ir_destroy(c);
  free(c);
}
static void atomic_cli(void) {
  const char *input = "build/m04-cli-input.ll",
             *output = "build/m04-cli-output.ll";
  const char *invalid = TARGET FN("%p = alloca i32\n"
                                  "store i32 %missing, ptr %p\n"
                                  "%v = load i32, ptr %p\nret i32 %v");
  FILE *f = fopen(input, "wb");
  assert(f && fwrite(invalid, 1, strlen(invalid), f) == strlen(invalid) &&
         !fclose(f));
  f = fopen(output, "wb");
  assert(f && fwrite("sentinel", 1, 8, f) == 8 && !fclose(f));
  char *args[] = {"badge-opt",    "--passes=scalar-promotion",
                  (char *)input,  "-o",
                  (char *)output, NULL};
  assert(badge_opt_main(5, args) == 1);
  f = fopen(output, "rb");
  assert(f);
  char bytes[16] = {0};
  assert(fread(bytes, 1, sizeof(bytes), f) == 8 && !strcmp(bytes, "sentinel"));
  assert(!fclose(f) && !unlink(input) && !unlink(output));
}
int main(void) {
  fixture(TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                    "%a = load i32, ptr %p\n%b = add i32 %a, %a\n"
                    "store i32 %b, ptr %p\n%c = load i32, ptr %p\nret i32 %c"),
          "scalar-promotion,scalar-promotion", 1, 2, 2, 0, "ret i32 %b",
          "alloca");
  fixture(TARGET FN("%p = alloca i32, i64 2, align 4\nbr label %body\nbody:\n"
                    "store i32 7, ptr %p\n%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 1, 1, 1, 0, "ret i32 7", "alloca");
  fixture(TARGET FN("%p = alloca i32\n%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 1, 1, 0, 0, "ret i32 undef", "alloca");
  fixture(TARGET FN("%p = alloca i32\n%early = load i32, ptr %p\n"
                    "store i32 %x, ptr %p\n%late = load i32, ptr %p\n"
                    "%r = add i32 %early, %late\nret i32 %r"),
          "scalar-promotion", 1, 2, 1, 0, "%r = add i32 undef, %x", "load");
  fixture(TARGET FN("%p = alloca i32\nstore i32 poison, ptr %p\n"
                    "%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 1, 1, 1, 0, "ret i32 poison", "alloca");
  fixture(TARGET "define ptr @f(ptr %x) {\nentry:\n%p = alloca ptr\n"
                 "store ptr %x, ptr %p\n%v = load ptr, ptr %p\nret ptr %v\n}\n",
          "scalar-promotion", 1, 1, 1, 0, "ret ptr %x", "alloca");
  fixture(TARGET
          "@value = global i32 3\n"
          "define ptr @f() {\nentry:\n%p = alloca ptr\n"
          "store ptr @value, ptr %p\n%v = load ptr, ptr %p\nret ptr %v\n}\n",
          "scalar-promotion", 1, 1, 1, 0, "ret ptr @value", "alloca");
  fixture(TARGET "define i64 @f(i64 %x) {\nentry:\n%p = alloca i64, align 8\n"
                 "store i64 %x, ptr %p\n%v = load i64, ptr %p\nret i64 %v\n}\n",
          "scalar-promotion", 1, 1, 1, 0, "ret i64 %x", "alloca");
  fixture(TARGET FN("%p = alloca i8\n%q = alloca i16\n%b = alloca i1\n"
                    "store i8 -1, ptr %p\nstore i16 -32768, ptr %q\n"
                    "store i1 true, ptr %b\n"
                    "%a = load i8, ptr %p\n%c = load i16, ptr %q\n"
                    "%d = load i1, ptr %b\n%e = select i1 %d, i8 %a, i8 0\n"
                    "%z = sext i8 %e to i32\nret i32 %z"),
          "scalar-promotion", 3, 3, 3, 0, "select i1 true, i8 -1, i8 0",
          "alloca");
  fixture(
      TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                "br i1 true, label %left, label %right\n"
                "left:\n%a = load i32, ptr %p\nbr label %join\n"
                "right:\n%b = load i32, ptr %p\nbr label %join\n"
                "join:\n%r = phi i32 [%a, %left], [%b, %right]\nret i32 %r"),
      "scalar-promotion", 1, 2, 1, 0, "%r = phi i32 [%x, %left], [%x, %right]",
      "alloca");
  fixture(
      TARGET FN(
          "%p = alloca i32\nstore i32 %x, ptr %p\n"
          "br label %loop\nloop:\n%i = phi i32 [0, %entry], [%next, %loop]\n"
          "%v = load i32, ptr %p\n%next = add i32 %i, 1\n"
          "br i1 false, label %loop, label %exit\n"
          "exit:\n%w = load i32, ptr %p\nret i32 %w"),
      "scalar-promotion", 1, 2, 1, 0, "ret i32 %x", "alloca");
  fixture(TARGET FN("%p = alloca i32\nbr label %loop\n"
                    "loop:\n%i = phi i32 [%x, %entry], [%v, %loop]\n"
                    "store i32 %i, ptr %p\n%v = load i32, ptr %p\n"
                    "br i1 false, label %loop, label %exit\n"
                    "exit:\nret i32 %v"),
          "scalar-promotion", 1, 1, 1, 0, "phi i32 [%x, %entry], [%i, %loop]",
          "alloca");
  fixture(
      TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                "br label %loop\nloop:\n%v = load i32, ptr %p\n"
                "%next = add i32 %v, 1\nstore i32 %next, ptr %p\n"
                "br i1 false, label %loop, label %exit\nexit:\nret i32 %next"),
      "scalar-promotion", 0, 0, 0, 1, "alloca", NULL);
  fixture(TARGET FN("%p = alloca i32\nbr label %body\nbody:\n"
                    "%early = load i32, ptr %p\nstore i32 %x, ptr %p\n"
                    "ret i32 %early"),
          "scalar-promotion", 0, 0, 0, 1, "load", NULL);
  fixture(TARGET FN("%p = alloca i32\n%early = load i32, ptr %p\n"
                    "store i32 %x, ptr %p\nbr label %exit\n"
                    "exit:\n%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "%early = load", NULL);
  fixture(TARGET FN("%p = alloca i32\nbr label %body\n"
                    "exit:\n%v = load i32, ptr %p\nret i32 %v\n"
                    "body:\nstore i32 %x, ptr %p\nbr label %exit"),
          "scalar-promotion", 0, 0, 0, 1, "store i32 %x", NULL);
  fixture(TARGET FN("%p = alloca i32\nbr label %exit\n"
                    "dead:\nstore i32 %x, ptr %p\n%v = load i32, ptr %p\n"
                    "unreachable\nexit:\nret i32 %x"),
          "scalar-promotion", 0, 0, 0, 1, "dead:", NULL);
  fixture(TARGET FN("%p = alloca i32, i32 %x\nstore i32 7, ptr %p\n"
                    "%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "alloca i32, i32 %x", NULL);
  fixture(TARGET FN("%p = alloca i32, i32 0\nret i32 %x"), "scalar-promotion",
          0, 0, 0, 1, "alloca", NULL);
  fixture(TARGET FN("%p = alloca i32, i32 -1\nret i32 %x"), "scalar-promotion",
          0, 0, 0, 1, "alloca", NULL);
  fixture(TARGET FN("%p = alloca [2 x i32]\nret i32 %x"), "scalar-promotion", 0,
          0, 0, 1, "alloca", NULL);
  fixture(TARGET FN("%p = alloca <{i8, i32}>\nret i32 %x"), "scalar-promotion",
          0, 0, 0, 1, "alloca", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore i8 7, ptr %p\n"
                    "%v = load i8, ptr %p\nret i32 %x"),
          "scalar-promotion", 0, 0, 0, 1, "load i8", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                    "%v = load volatile i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "volatile", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore volatile i32 %x, ptr %p\n"
                    "%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "volatile", NULL);
  fixture(
      TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                "%v = load i32, ptr %p, !tag !0\nret i32 %v") "!0 = !{i32 7}\n",
      "scalar-promotion", 0, 0, 0, 1, "!tag !0", NULL);
  fixture(TARGET FN("%p = alloca i32, !tag !0\nret i32 %x") "!0 = !{i32 7}\n",
          "scalar-promotion", 0, 0, 0, 1, "!tag !0", NULL);
  fixture(TARGET "declare void @escape(ptr)\n" FN(
              "%p = alloca i32\nstore i32 %x, ptr %p\n"
              "call void @escape(ptr %p)\n%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "call void @escape", NULL);
  fixture(TARGET FN("%p = alloca i32\nstore i32 %x, ptr %p\n"
                    "call void asm sideeffect \"\", \"r,~{memory}\"(ptr %p)\n"
                    "%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "asm sideeffect", NULL);
  fixture(TARGET FN("%p = alloca i32\n%q = getelementptr i32, ptr %p, i32 0\n"
                    "store i32 %x, ptr %q\nret i32 %x"),
          "scalar-promotion", 0, 0, 0, 1, "getelementptr", NULL);
  fixture(TARGET "define ptr @f() {\nentry:\n%p = alloca i32\nret ptr %p\n}\n",
          "scalar-promotion", 0, 0, 0, 1, "ret ptr %p", NULL);
  fixture(TARGET FN("%p = alloca ptr\nstore ptr %p, ptr %p\nret i32 %x"),
          "scalar-promotion", 0, 0, 0, 1, "store ptr %p", NULL);
  fixture(TARGET FN("%p = alloca i32\n%v = ptrtoint ptr %p to i32\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "ptrtoint", NULL);
  fixture(TARGET FN("br label %body\nbody:\n%p = alloca i32\n"
                    "store i32 %x, ptr %p\n%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "alloca", NULL);
  fixture(TARGET FN("%p = alloca i32\n%q = alloca i32\n"
                    "store i32 %x, ptr %p\n%a = load i32, ptr %p\n"
                    "store i32 %a, ptr %q\n%b = load i32, ptr %q\n"
                    "store i32 %b, ptr %p\n%c = load i32, ptr %p\nret i32 %c"),
          "scalar-promotion", 2, 3, 3, 0, "ret i32 %x", "alloca");
  fixture(TARGET FN("%p = alloca i32\n%q = alloca ptr\n"
                    "store ptr %p, ptr %q\n%pointer = load ptr, ptr %q\n"
                    "store i32 %x, ptr %pointer\n%v = load i32, ptr %pointer\n"
                    "ret i32 %v"),
          "scalar-promotion,scalar-promotion,scalar-promotion", 2, 2, 2, 1,
          "ret i32 %x", "alloca");
  fixture(TARGET FN("%p = alloca i32\nbr i1 true, label %left, label %right\n"
                    "left:\nstore i32 %x, ptr %p\nbr label %join\n"
                    "right:\nstore i32 9, ptr %p\nbr label %join\n"
                    "join:\n%v = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 0, 0, 0, 1, "load", NULL);
  fixture(TARGET FN("%p = alloca i32\nbr label %body\n"
                    "body:\nstore i32 %x, ptr %p\n%v = load i32, ptr %p\n"
                    "br i1 true, label %join, label %join\n"
                    "join:\n%r = phi i32 [%v, %body], [%v, %body]\nret i32 %r"),
          "scalar-promotion", 1, 1, 1, 0, "phi i32 [%x, %body], [%x, %body]",
          "alloca");
  fixture(TARGET FN("%\"\\70\" = alloca i32\nstore i32 %x, ptr %p\n"
                    "%\"\\76\" = load i32, ptr %p\nret i32 %v"),
          "scalar-promotion", 1, 1, 1, 0, "ret i32 %x", "alloca");
  fixture(
      TARGET FN(
          "%p = alloca i32\nstore i32 %x, ptr %p\n"
          "%v = load i32, ptr %p\nret i32 %v") "define i32 @g(i32 %x) "
                                               "{\nentry:\n%p = alloca i32\n"
                                               "store i32 9, ptr %p\n%v = load "
                                               "i32, ptr %p\nret i32 %v\n}\n",
      "scalar-promotion", 2, 2, 2, 0, "ret i32 9", "alloca");
  fixture(TARGET FN("%p = alloca i32\nstore i32 7, ptr %p\n"
                    "%v = load i32, ptr %p\n%r = add i32 %v, 5\nret i32 %r"),
          "scalar-promotion,constant-fold,dead-instructions", 1, 1, 1, 0,
          "ret i32 12", "alloca");
  fixture(TARGET FN("%p = alloca i32\nret i32 %x"), "scalar-promotion", 1, 0, 0,
          0, "ret i32 %x", "alloca");
  fixture(TARGET "declare i32 @f(i32)\n", "scalar-promotion", 0, 0, 0, 0, "@f",
          NULL);
  char *chain = malloc(2 * 1024 * 1024);
  assert(chain);
  size_t used =
      (size_t)sprintf(chain, TARGET "define i32 @f(i32 %%x) {\nentry:\n");
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
  fixture(chain, "scalar-promotion,scalar-promotion", 5000, 5000, 5000, 0,
          "ret i32 %x", "alloca");
  used = (size_t)sprintf(chain, TARGET "define i32 @f(i32 %%x) {\nentry:\n");
  for (unsigned k = 0; k < 128; k++)
    used += (size_t)sprintf(chain + used, "%%s%u = alloca i32\n", k);
  used += (size_t)sprintf(chain + used, "br label %%b127\n");
  for (unsigned k = 0; k < 128; k++) {
    used += (size_t)sprintf(chain + used, "b%u:\nstore i32 ", k);
    if (k == 127)
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
  fixture(chain, "scalar-promotion", 128, 128, 128, 0, "ret i32 %x", "alloca");
  free(chain);
  atomic_cli();
  const ir_pass *pass =
      ir_find_pass("scalar-promotion", strlen("scalar-promotion"));
  assert(pass && pass->role == IR_PASS_OPTIMIZATION);
  for (unsigned k = 0; k < badge_ir_default_pass_count; k++)
    assert(strcmp(badge_ir_default_pipeline[k], "scalar-promotion"));
  puts("M04: scalar slots, initialization/dominance, load alias chains, "
       "PHI/loop uses, conservative preservation, atomic failure and 5000-slot "
       "cascade PASS");
  return 0;
}
