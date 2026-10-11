#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "ir_pass.h"
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
#define FN(body) "define i32 @f(i32 %x, i32 %y) {\nentry:\n" body "\n}\n"
#ifdef CSE_COLLISION_TEST
#define PREFIX "build/m08-collision-"
#else
#define PREFIX "build/m08-"
#endif
int badge_opt_main(int, char **);
static unsigned fixtures;
static uint64_t reused;
static void observe(void *unused, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  (void)unused;
  (void)step;
  if (!strcmp(pass, "local-cse") && !strcmp(event, "transformed") &&
      strcmp(reason, "unused_definition_removed"))
    reused += count;
}
static void oracle(const char *source) {
  const char *tool = badge_test_tool("OPT_TEST_LLVM");
  if (!tool)
    return;
  const char *file = PREFIX "oracle.ll";
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
static void fixture(const char *source, const char *present, const char *absent,
                    unsigned count) {
  oracle(source);
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "local-cse.ll";
  c->observe = observe;
  if (setjmp(c->failure)) {
    fprintf(stderr, "unexpected CSE failure:\n%s\n", source);
    abort();
  }
  reused = 0;
  ir_pipeline(c, source, strlen(source), "local-cse");
  assert(c->memory == 0);
  if ((present && !strstr(c->output, present)) ||
      (absent && strstr(c->output, absent)) || reused != count) {
    fprintf(stderr, "M08 fixture %u failed: %llu reused\n%s\n", fixtures,
            (unsigned long long)reused, c->output);
    abort();
  }
  oracle(c->output);
  char *result = strdup(c->output);
  assert(result);
  size_t size = c->size;
  c->observe = NULL;
  ir_pipeline(c, result, size, "local-cse");
  assert(c->size == size && !memcmp(c->output, result, size));
  free(result);
  ir_destroy(c);
  free(c);
  fixtures++;
}
static void scalars(void) {
  const char *ops[] = {"add", "sub", "mul",  "and", "or",
                       "xor", "shl", "lshr", "ashr"};
  unsigned widths[] = {1, 8, 16, 32, 64};
  for (unsigned w = 0; w < sizeof(widths) / sizeof(*widths); w++)
    for (unsigned j = 0; j < sizeof(ops) / sizeof(*ops); j++) {
      char source[1024], present[80];
      unsigned bits = widths[w];
      snprintf(source, sizeof(source),
               TARGET "define i%u @f(i%u %%x, i%u %%y) {\nentry:\n"
                      "%%a = %s i%u %%x, %%y\n%%b = %s i%u %%x, %%y\n"
                      "%%r = xor i%u %%a, %%b\nret i%u %%r\n}\n",
               bits, bits, bits, ops[j], bits, ops[j], bits, bits, bits);
      snprintf(present, sizeof(present), "xor i%u %%a, %%a", bits);
      fixture(source, present, "%b =", 1);
    }
  const char *flags[] = {"add nsw", "add nuw",    "add nsw nuw",
                         "mul nsw", "mul nuw",    "or disjoint",
                         "shl nuw", "lshr exact", "ashr exact"};
  for (unsigned j = 0; j < sizeof(flags) / sizeof(*flags); j++) {
    char source[1024];
    snprintf(source, sizeof(source),
             TARGET "define i32 @f(i32 %%x, i32 %%y) {\nentry:\n"
                    "%%a = %s i32 %%x, %%y\n%%b = %s i32 %%x, %%y\n"
                    "%%r = add i32 %%a, %%b\nret i32 %%r\n}\n",
             flags[j], flags[j]);
    fixture(source, "add i32 %a, %a", "%b =", 1);
  }
  fixture(TARGET FN("%a = add i32 %x, %y\n%b = add nsw i32 %x, %y\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "add nsw", NULL, 0);
  fixture(TARGET FN("%a = add nuw i32 %x, %y\n%b = add nsw i32 %x, %y\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "add nsw", NULL, 0);
  fixture(TARGET FN("%a = add i32 %x, %y\n%b = add i32 %y, %x\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "%b = add", NULL, 0);
  fixture(TARGET FN("%a = add i32 %x, 1\n%b = sub i32 %x, 1\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "%b = sub", NULL, 0);
  fixture(TARGET "define i8 @f(i8 %x) {\nentry:\n"
                 "%a = and i8 %x, -1\n%b = and i8 %x, 255\n"
                 "%r = add i8 %a, %b\nret i8 %r\n}\n",
          "add i8 %a, %a", "%b =", 1);
  fixture(TARGET FN("%a = add i32 %x, 0\n"
                    "%b = add i32 %x, zeroinitializer\n"
                    "%r = add i32 %a, %b\nret i32 %r"),
          "add i32 %a, %a", "%b =", 1);
  fixture(TARGET FN("%a = add i32 %x, undef\n%b = add i32 %x, undef\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "%b = add", NULL, 0);
  fixture(TARGET FN("%a = add i32 %x, poison\n%b = add i32 %x, poison\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "%b = add", NULL, 0);
  fixture(TARGET FN("%a = freeze i32 %x\n%b = freeze i32 %x\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "%b = freeze", NULL, 0);
  fixture(TARGET FN("%a = add i32 %x, %y, !foo !0\n"
                    "%b = add i32 %x, %y, !foo !0\n"
                    "%r = xor i32 %a, %b\nret i32 %r") "!0 = !{}\n",
          "%b = add", NULL, 0);
  fixture(TARGET FN("%a = udiv i32 %x, %y\n%b = udiv i32 %x, %y\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "%b = udiv", NULL, 0);
  fixture(TARGET FN("%a = srem i32 %x, %y\n%b = srem i32 %x, %y\n"
                    "%r = xor i32 %a, %b\nret i32 %r"),
          "%b = srem", NULL, 0);
}
static void comparisons_casts_addresses(void) {
  fixture(TARGET FN("%a = icmp slt i32 %x, %y\n%b = icmp slt i32 %x, %y\n"
                    "%r = and i1 %a, %b\n%v = zext i1 %r to i32\nret i32 %v"),
          "and i1 %a, %a", "%b =", 1);
  fixture(TARGET FN("%a = icmp slt i32 %x, %y\n%b = icmp ult i32 %x, %y\n"
                    "%r = and i1 %a, %b\n%v = zext i1 %r to i32\nret i32 %v"),
          "%b = icmp ult", NULL, 0);
  fixture(TARGET "define i64 @f(i8 %x) {\nentry:\n"
                 "%a = sext i8 %x to i64\n%b = sext i8 %x to i64\n"
                 "%r = add i64 %a, %b\nret i64 %r\n}\n",
          "add i64 %a, %a", "%b =", 1);
  fixture(TARGET "define i64 @f(i8 %x) {\nentry:\n"
                 "%a = sext i8 %x to i64\n%b = zext i8 %x to i64\n"
                 "%r = add i64 %a, %b\nret i64 %r\n}\n",
          "%b = zext", NULL, 0);
  fixture(TARGET "define i64 @f(i32 %x) {\nentry:\n"
                 "%a = zext i32 %x to i64\n%b = zext i32 %x to i64\n"
                 "%r = sub i64 %a, %b\nret i64 %r\n}\n",
          "sub i64 %a, %a", "%b =", 1);
  fixture(TARGET "define i1 @f(ptr %p, i32 %i) {\nentry:\n"
                 "%a = getelementptr [8 x i32], ptr %p, i32 0, i32 %i\n"
                 "%b = getelementptr [8 x i32], ptr %p, i32 0, i32 %i\n"
                 "%r = icmp eq ptr %a, %b\nret i1 %r\n}\n",
          "icmp eq ptr %a, %a", "%b =", 1);
  fixture(TARGET "define i1 @f(ptr %p) {\nentry:\n"
                 "%a = getelementptr i8, ptr %p, i32 1\n"
                 "%b = getelementptr i32, ptr %p, i32 1\n"
                 "%r = icmp eq ptr %a, %b\nret i1 %r\n}\n",
          "%b = getelementptr i32", NULL, 0);
  fixture(TARGET "define i1 @f(ptr %p) {\nentry:\n"
                 "%a = getelementptr i8, ptr %p, i32 1\n"
                 "%b = getelementptr inbounds i8, ptr %p, i32 1\n"
                 "%r = icmp eq ptr %a, %b\nret i1 %r\n}\n",
          "getelementptr inbounds", NULL, 0);
  fixture(TARGET "define i1 @f(ptr %p) {\nentry:\n"
                 "%a = getelementptr inbounds i8, ptr %p, i32 1\n"
                 "%b = getelementptr inbounds i8, ptr %p, i32 1\n"
                 "%r = icmp eq ptr %a, %b\nret i1 %r\n}\n",
          "icmp eq ptr %a, %a", "%b =", 1);
  fixture(TARGET "@\"g\\20x\" = global i32 0\n"
                 "define i1 @f() {\nentry:\n"
                 "%a = getelementptr i32, ptr @\"g x\", i32 0\n"
                 "%b = getelementptr i32, ptr @\"g\\20x\", i32 0\n"
                 "%r = icmp eq ptr %a, %b\nret i1 %r\n}\n",
          "icmp eq ptr %a, %a", "%b =", 1);
  fixture(TARGET "define i1 @f(ptr %p) {\nentry:\n"
                 "%a = getelementptr i8, ptr %p, i32 undef\n"
                 "%b = getelementptr i8, ptr %p, i32 undef\n"
                 "%r = icmp eq ptr %a, %b\nret i1 %r\n}\n",
          "%b = getelementptr", NULL, 0);
  fixture(TARGET "define i1 @f(ptr %p) {\nentry:\n"
                 "%a = getelementptr {i32, i32}, ptr %p, i32 0, i32 0\n"
                 "%b = getelementptr {i32, i32}, ptr %p, i32 0, i32 1\n"
                 "%r = icmp eq ptr %a, %b\nret i1 %r\n}\n",
          "%b = getelementptr", NULL, 0);
}
static void flow_effects(void) {
  fixture(TARGET FN("%a = add i32 %x, %y\n%b = add i32 %x, %y\n"
                    "%c = mul i32 %a, 3\n%d = mul i32 %b, 3\n"
                    "%r = add i32 %c, %d\nret i32 %r"),
          "add i32 %c, %c", "%d =", 2);
  fixture(TARGET FN("%a = add i32 %x, %y\nbr label %child\nchild:\n"
                    "%b = add i32 %x, %y\n%r = xor i32 %a, %b\nret i32 %r"),
          "%b = add", NULL, 0);
  fixture(TARGET FN("br label %later\nfirst:\n"
                    "%c = mul i32 %a, 3\n%d = mul i32 %b, 3\n"
                    "%r = add i32 %c, %d\nret i32 %r\n"
                    "later:\n%a = add i32 %x, %y\n%b = add i32 %x, %y\n"
                    "br label %first"),
          "add i32 %c, %c", "%d =", 2);
  fixture(TARGET FN("%a = add i32 %x, %y\n%b = add i32 %x, %y\n"
                    "br i1 true, label %left, label %right\n"
                    "left:\nbr label %join\nright:\nbr label %join\n"
                    "join:\n%v = phi i32 [ %a, %left ], [ %b, %right ]\n"
                    "ret i32 %v"),
          "phi i32 [ %a, %left ], [ %a, %right ]", "%b =", 1);
  fixture(TARGET FN("br label %loop\nloop:\n"
                    "%i = phi i32 [ %x, %entry ], [ %next, %loop ]\n"
                    "%a = add i32 %i, %y\n%b = add i32 %i, %y\n"
                    "%next = add i32 %a, %b\n"
                    "%c = icmp ult i32 %next, 7\n"
                    "br i1 %c, label %loop, label %out\nout:\nret i32 %next"),
          "add i32 %a, %a", "%b =", 1);
  fixture(TARGET "declare void @effect()\n" FN(
              "%a = add i32 %x, %y\ncall void @effect()\n"
              "%b = add i32 %x, %y\n%r = add i32 %a, %b\nret i32 %r"),
          "call void @effect()", "%b =", 1);
  fixture(TARGET "define i32 @f(ptr %p) {\nentry:\n"
                 "%a = load i32, ptr %p\n%b = load i32, ptr %p\n"
                 "%c = add i32 %a, 1\n%d = add i32 %b, 1\n"
                 "%r = add i32 %c, %d\nret i32 %r\n}\n",
          "%b = load", NULL, 0);
  fixture(TARGET "define i32 @f(ptr %p) {\nentry:\n"
                 "%a = load volatile i32, ptr %p\n"
                 "%b = load volatile i32, ptr %p\n"
                 "store volatile i32 %a, ptr %p\n"
                 "store volatile i32 %b, ptr %p\n"
                 "%r = add i32 %a, %b\nret i32 %r\n}\n",
          "store volatile i32 %b", NULL, 0);
  fixture(TARGET "declare i32 @effect()\n" FN(
              "%a = call i32 @effect()\n%b = call i32 @effect()\n"
              "%r = add i32 %a, %b\nret i32 %r"),
          "%b = call", NULL, 0);
  fixture(TARGET FN("ret i32 %x\ndead:\n"
                    "%a = add i32 %x, %y\n%b = add i32 %x, %y\n"
                    "%r = add i32 %a, %b\nret i32 %r"),
          "%b = add", NULL, 0);
  fixture(TARGET "define i32 @f(i32 %\"x\\20y\") {\nentry:\n"
                 "%a = add i32 %\"x y\", 1\n%b = add i32 %\"x\\20y\", 1\n"
                 "%r = add i32 %a, %b\nret i32 %r\n}\n",
          "add i32 %a, %a", "%b =", 1);
  fixture(TARGET, "target triple", NULL, 0);
}
static void collisions_and_scaling(void) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "cse-collisions.ll";
  assert(!setjmp(c->failure));
  ir_emit(c, TARGET "define i32 @f(i32 %%x) {\nentry:\n");
  for (unsigned k = 0; k < 64; k++)
    ir_emit(c, "%%a%u = add i32 %%x, %u\n", k, k);
  for (unsigned k = 0; k < 64; k++)
    ir_emit(c,
            "%%b%u = add i32 %%x, %u\n"
            "%%c%u = xor i32 %%a%u, %%b%u\n",
            k, k, k, k, k);
  ir_emit(c, "%%s0 = add i32 %%c0, %%c1\n");
  for (unsigned k = 1; k < 63; k++)
    ir_emit(c, "%%s%u = add i32 %%s%u, %%c%u\n", k, k - 1, k + 1);
  ir_emit(c, "ret i32 %%s62\n}\n");
  char *source = strdup(c->output);
  assert(source);
  fixture(source, "xor i32 %a63, %a63", "%b63 =", 64);
  free(source);
  c->size = 0;
  ir_emit(c, TARGET "define i32 @f(i32 %%x) {\nentry:\n"
                    "%%a = add i32 %%x, 1\n");
  for (unsigned k = 0; k < 8000; k++)
    ir_emit(c, "%%b%u = add i32 %%x, 1\n", k);
  ir_emit(c, "%%r = add i32 %%a, %%b7999\nret i32 %%r\n}\n");
  source = strdup(c->output);
  assert(source);
  fixture(source, "add i32 %a, %a", "%b7999 =", 8000);
  free(source);
  ir_destroy(c);
  free(c);
}
static void failures(void) {
  const char *input = PREFIX "input.ll", *output = PREFIX "output.ll";
  const char *invalid = TARGET FN(
      "%a = add i32 %missing, %y\n%b = add i32 %missing, %y\nret i32 %b");
  FILE *f = fopen(input, "wb");
  assert(f && fwrite(invalid, 1, strlen(invalid), f) == strlen(invalid) &&
         !fclose(f));
  f = fopen(output, "wb");
  assert(f && fwrite("sentinel", 1, 8, f) == 8 && !fclose(f));
  char *args[] = {"badge-opt", "--passes=local-cse", (char *)input,
                  "-o",        (char *)output,       NULL};
  assert(badge_opt_main(5, args) == 1);
  const char *valid = TARGET FN("%a = add i32 %x, %y\n%b = add i32 %x, %y\n%r "
                                "= add i32 %a, %b\nret i32 %r");
  f = fopen(input, "wb");
  assert(f && fwrite(valid, 1, strlen(valid), f) == strlen(valid) &&
         !fclose(f));
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    execl("build/badge-opt-cse-budget", "badge-opt-cse-budget",
          "--passes=local-cse", input, "-o", output, (char *)NULL);
    _exit(127);
  }
  int status;
  assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 1);
  f = fopen(output, "rb");
  char bytes[16] = {0};
  assert(f && fread(bytes, 1, sizeof(bytes), f) == 8 &&
         !strcmp(bytes, "sentinel") && !fclose(f));
  assert(!unlink(input) && !unlink(output));
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "cse-allocation-limit.ll";
  c->memory = 128u * 1024u * 1024u;
  if (!setjmp(c->failure)) {
    ir_pipeline(c, valid, strlen(valid), "local-cse");
    assert(!"exhausted allocation budget must fail");
  }
  ir_destroy(c);
  free(c);
}
int main(void) {
  scalars();
  comparisons_casts_addresses();
  flow_effects();
  collisions_and_scaling();
  failures();
  printf("M08: %u LLVM-verified/idempotent fixtures, exact collision keys, "
         "8000 repetitions and atomic resource/input failures PASS\n",
         fixtures);
  return 0;
}
