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

static uint64_t removed;
static void observe(void *context, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  (void)context;
  (void)step;
  if (!strcmp(pass, "dead-instructions") && !strcmp(event, "transformed") &&
      !strcmp(reason, "unused_definition_removed"))
    removed += count;
}
static void oracle(const char *source) {
  const char *opt = badge_test_tool("OPT_TEST_LLVM");
  if (!opt)
    return;
  const char *file = "build/m03-oracle-fixture.ll";
  FILE *f = fopen(file, "wb");
  assert(f && fwrite(source, 1, strlen(source), f) == strlen(source) &&
         !fclose(f));
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    execlp(opt, opt, "-passes=verify", "-disable-output", file, (char *)NULL);
    _exit(127);
  }
  int status;
  assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);
  assert(!unlink(file));
}
static void fixture(const char *source, const char *selection,
                    uint64_t expected, const char *absent,
                    const char *present) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "dead-instruction.ll";
  c->observe = observe;
  assert(!setjmp(c->failure));
  removed = 0;
  ir_pipeline(c, source, strlen(source), selection);
  assert(removed == expected);
  if (absent)
    assert(!strstr(c->output, absent));
  if (present)
    assert(strstr(c->output, present));
  if (!expected)
    assert(c->size == strlen(source) && !strcmp(c->output, source));
  oracle(c->output);
  char *result = strdup(c->output);
  assert(result);
  size_t size = c->size;
  removed = 0;
  ir_pipeline(c, result, size, "dead-instructions");
  assert(!removed && c->size == size && !memcmp(c->output, result, size));
  free(result);
  ir_destroy(c);
  free(c);
}
int main(void) {
  fixture(TARGET "declare i32 @external(i32)\n", "dead-instructions", 0, NULL,
          "@external");
  fixture(TARGET "declare i32 @pure(i32) #0\n" FN(
              "%a = add i32 %x, 1\n%v = call i32 @pure(i32 %a)\n"
              "ret i32 %x") "attributes #0 = { nounwind memory(none) }\n",
          "dead-instructions", 0, NULL, "%v = call");
  fixture(TARGET FN("%c = icmp eq i32 %x, 0\n"
                    "br i1 %c, label %left, label %right\n"
                    "left:\nret i32 1\nright:\nret i32 %x"),
          "dead-instructions", 0, NULL, "%c = icmp");
  fixture(TARGET FN("%a = add i32 %x, 1\n%b = mul i32 %a, %a\n"
                    "%c = xor i32 %b, 7\nret i32 %x"),
          "dead-instructions", 3, "%a =", "ret i32 %x");
  fixture(TARGET FN("%a = add i32 1, 2\n%b = mul i32 %a, 3\nret i32 %b"),
          "constant-fold,dead-instructions", 2, "freeze", "ret i32 9");
  fixture(TARGET FN("%a = freeze i32 poison\n%b = freeze i8 undef\n"
                    "%c = shl i32 %x, 32\n%d = add nsw i32 %x, 1\n"
                    "%e = icmp eq i32 %x, 7\n%f = zext i1 %e to i32\n"
                    "%g = select i1 %e, i32 %f, i32 %x\nret i32 %x"),
          "dead-instructions", 7, "%a =", "ret i32 %x");
  fixture(TARGET FN("%a = freeze ptr null\n%b = getelementptr inbounds i8, ptr "
                    "%a, i32 3\n"
                    "%c = ptrtoint ptr %b to i32\n%d = inttoptr i32 %c to ptr\n"
                    "ret i32 %x"),
          "dead-instructions", 4, "%a =", "ret i32 %x");
  fixture(TARGET FN("%a = insertvalue [2 x i32] poison, i32 %x, 0\n"
                    "%b = extractvalue [2 x i32] %a, 0\nret i32 %x"),
          "dead-instructions", 2, "insertvalue", "ret i32 %x");
  fixture(TARGET FN("%a = add i32 %x, 1\n%b = mul i32 %a, %a\n"
                    "%dead = sub i32 %a, 7\nret i32 %b"),
          "dead-instructions", 1, "%dead =", "%a = add");
  fixture(TARGET "declare i32 @effect(i32)\n" FN(
              "%a = add i32 %x, 1\n%v = call i32 @effect(i32 %a)\n"
              "call void asm sideeffect \"nop\", \"~{memory}\"()\nret i32 %x"),
          "dead-instructions", 0, NULL, "%v = call");
  fixture(TARGET FN("%p = alloca i32, align 4\n%a = add i32 %x, 1\n"
                    "store i32 %a, ptr %p, align 4\n"
                    "%b = load i32, ptr %p, align 4\n"
                    "%c = load volatile i32, ptr %p, align 4\n"
                    "store volatile i32 %x, ptr %p, align 4\nret i32 %x"),
          "dead-instructions", 0, NULL, "%b = load");
  fixture(TARGET FN("%a = sdiv i32 %x, 0\n%b = udiv i32 %x, 3\n"
                    "%c = srem i32 %x, -1\n%d = urem i32 %x, 3\nret i32 %x"),
          "dead-instructions", 0, NULL, "%a = sdiv");
  fixture(
      TARGET FN("%a = add i32 %x, 1, !tag !0\nret i32 %x") "!0 = !{i32 7}\n",
      "dead-instructions", 0, NULL, "!tag !0");
  fixture(
      TARGET FN("%a = add i32 %x, 1\nbr i1 true, label %left, label %right\n"
                "left:\n%b = add i32 %a, 2\nbr label %join\n"
                "right:\n%c = mul i32 %a, 3\nbr label %join\n"
                "join:\n%q = phi i32 [%b, %left], [%c, %right]\nret i32 %x"),
      "dead-instructions", 4, "phi i32", "br i1 true");
  fixture(
      TARGET FN("br label %loop\nloop:\n"
                "%p = phi i32 [%x, %entry], [%next, %loop]\n"
                "%next = add i32 %p, 1\nbr i1 false, label %loop, label %exit\n"
                "exit:\nret i32 %x"),
      "dead-instructions", 0, NULL, "%p = phi");
  fixture(TARGET FN("switch i32 %x, label %exit [\ni32 0, label %exit\n]\n"
                    "exit:\nret i32 %x"),
          "dead-instructions", 0, NULL, "switch");
  fixture(
      TARGET FN("%a = add i32 %x, 1\nbr i1 true, label %join, label %join\n"
                "join:\n%p = phi i32 [%a, %entry], [%a, %entry]\nret i32 %x"),
      "dead-instructions", 2, "%a =", "br i1 true");
  fixture(
      TARGET
      "define i32 @f(i32 %x) {\nentry:\n%a = add i32 %x, 1\nret i32 %x\n}\n"
      "define i32 @g(i32 %x) {\nentry:\n%a = add i32 %x, 1\nret i32 %a\n}\n",
      "dead-instructions", 1, NULL, "ret i32 %a");
  fixture(
      TARGET FN("%\"\\61\" = add i32 %x, 1\n%b = xor i32 %a, %a\nret i32 %x"),
      "dead-instructions", 2, "%b =", "ret i32 %x");
  fixture(TARGET FN("%a = freeze i32 7\nret i32 %a"), "dead-instructions", 0,
          NULL, "freeze");
  fixture(TARGET FN("%a = xor i1 true, false\n%b = add i64 1, 2\n"
                    "%c = lshr i64 %b, 7\n%d = trunc i64 %c to i8\n"
                    "%e = sext i8 %d to i32\nret i32 %x"),
          "dead-instructions", 5, "%b =", "ret i32 %x");
  fixture(TARGET FN("%a = add i32 %x, 1\n%q = udiv i32 %a, 2\nret i32 %x"),
          "dead-instructions", 0, NULL, "%a = add");
  fixture(TARGET FN("%a = add i32 %x, 1\nbr label %join\n"
                    "join:\n%q = phi i32 [%a, %entry]\nret i32 %q"),
          "dead-instructions", 0, NULL, "%a = add");
  fixture(TARGET FN("br label %loop\nloop:\n"
                    "%p = phi i32 [%x, %entry], [%p, %loop]\n"
                    "br i1 false, label %loop, label %exit\nexit:\nret i32 %x"),
          "dead-instructions", 0, NULL, "%p = phi");
  fixture(TARGET FN("ret i32 %x\nunreachable_block:\n"
                    "%a = add i32 %x, 1\nunreachable"),
          "dead-instructions", 1, "%a =", "unreachable_block:");
  char *chain = malloc(1024 * 1024);
  assert(chain);
  size_t used =
      (size_t)sprintf(chain, TARGET "define i32 @f(i32 %%x) {\nentry:\n"
                                    "%%d0 = add i32 %%x, 1\n");
  for (unsigned k = 1; k < 10000; k++) {
    int n = snprintf(chain + used, 1024 * 1024 - used,
                     "%%d%u = xor i32 %%d%u, %%d%u\n", k, k - 1, k - 1);
    assert(n > 0 && (size_t)n < 1024 * 1024 - used);
    used += (size_t)n;
  }
  assert(used + 16 < 1024 * 1024);
  strcpy(chain + used, "ret i32 %x\n}\n");
  fixture(chain, "dead-instructions,dead-instructions", 10000,
          "%d0 =", "ret i32 %x");
  free(chain);
  const char *bad[] = {TARGET FN("%a = add i32 %missing, 1\nret i32 %x"),
                       TARGET FN("%a = add i8 %x, 1\nret i32 %x")};
  for (unsigned k = 0; k < 2; k++) {
    ir_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->file = "invalid-dead-tree.ll";
    if (!setjmp(c->failure)) {
      ir_pipeline(c, bad[k], strlen(bad[k]), "dead-instructions");
      assert(!"invalid IR must not disappear through DCE");
    }
    ir_destroy(c);
    free(c);
  }
  assert(ir_find_pass("dead-instructions", 17)->role == IR_PASS_OPTIMIZATION);
  for (unsigned k = 0; k < badge_ir_default_pass_count; k++)
    assert(strcmp(badge_ir_default_pipeline[k], "dead-instructions"));
  puts("M03: cascading worklist, repeated operands/edges, pure whitelist, "
       "effectful roots, cyclic retention, idempotence and invalid-IR "
       "rejection PASS");
  return 0;
}
