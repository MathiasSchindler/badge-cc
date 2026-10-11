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
#define FN(body) "define i32 @f(i32 %x) {\nentry:\n" body "\n}\n"
int badge_opt_main(int, char **);
static unsigned fixtures;
static uint64_t transformed;
static const char *pipeline = "instruction-combine";
static void observe(void *unused, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  (void)unused;
  (void)step;
  if (!strcmp(pass, pipeline) && !strcmp(event, "transformed") &&
      strcmp(reason, "unused_definition_removed"))
    transformed += count;
}
static void oracle(const char *source) {
  const char *tool = badge_test_tool("OPT_TEST_LLVM");
  if (!tool)
    return;
  const char *file = "build/m07-oracle-fixture.ll";
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
                    unsigned changes) {
  oracle(source);
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "instruction-combine.ll";
  c->observe = observe;
  if (setjmp(c->failure)) {
    fprintf(stderr, "unexpected M07 failure:\n%s\n", source);
    abort();
  }
  transformed = 0;
  ir_pipeline(c, source, strlen(source), pipeline);
  assert(c->memory == 0);
  if ((present && !strstr(c->output, present)) ||
      (absent && strstr(c->output, absent)) || transformed != changes) {
    fprintf(stderr, "M07 fixture %u failed: %llu transformations\n%s\n",
            fixtures, (unsigned long long)transformed, c->output);
    abort();
  }
  oracle(c->output);
  char *result = strdup(c->output);
  assert(result);
  size_t size = c->size;
  c->observe = NULL;
  ir_pipeline(c, result, size, pipeline);
  assert(c->size == size && !memcmp(c->output, result, size));
  free(result);
  ir_destroy(c);
  free(c);
  fixtures++;
}
static void arithmetic_tests(void) {
  unsigned widths[] = {1, 8, 16, 32, 64};
  const char *ops[] = {"add", "sub", "mul",  "and", "or",
                       "xor", "shl", "lshr", "ashr"};
  for (unsigned w = 0; w < sizeof(widths) / sizeof(*widths); w++)
    for (unsigned j = 0; j < sizeof(ops) / sizeof(*ops); j++) {
      unsigned bits = widths[w];
      const char *neutral = !strcmp(ops[j], "mul")   ? "1"
                            : !strcmp(ops[j], "and") ? "-1"
                                                     : "0";
      char source[1024], present[80];
      snprintf(source, sizeof(source),
               TARGET "define i%u @f(i%u %%x) {\nentry:\n"
                      "%%v = %s i%u %%x, %s\nret i%u %%v\n}\n",
               bits, bits, ops[j], bits, neutral, bits);
      snprintf(present, sizeof(present), "ret i%u %%x", bits);
      fixture(source, present, "%v =", 1);
      if (!strcmp(ops[j], "sub") || !strcmp(ops[j], "shl") ||
          !strcmp(ops[j], "lshr") || !strcmp(ops[j], "ashr"))
        continue;
      snprintf(source, sizeof(source),
               TARGET "define i%u @f(i%u %%x) {\nentry:\n"
                      "%%v = %s i%u %s, %%x\nret i%u %%v\n}\n",
               bits, bits, ops[j], bits, neutral, bits);
      fixture(source, present, "%v =", 1);
    }
  const char *flags[] = {"add nsw",    "add nuw",     "add nsw nuw",
                         "sub nsw",    "sub nuw",     "mul nsw",
                         "mul nuw",    "or disjoint", "shl nsw nuw",
                         "lshr exact", "ashr exact"};
  for (unsigned j = 0; j < sizeof(flags) / sizeof(*flags); j++) {
    char source[1024];
    snprintf(source, sizeof(source),
             TARGET "define i32 @f(i32 %%x) {\nentry:\n"
                    "%%v = %s i32 %%x, %s\nret i32 %%v\n}\n",
             flags[j], !strncmp(flags[j], "mul", 3) ? "1" : "0");
    fixture(source, "ret i32 %x", "%v =", 1);
  }
  fixture(TARGET FN("%v = add i32 %x, 1\nret i32 %v"), "add i32", NULL, 0);
  fixture(TARGET FN("%v = sub i32 0, %x\nret i32 %v"), "sub i32", NULL, 0);
  fixture(TARGET FN("%v = mul i32 %x, 0\nret i32 %v"), "mul i32", NULL, 0);
  fixture(TARGET FN("%v = and i32 %x, 0\nret i32 %v"), "and i32", NULL, 0);
  fixture(TARGET FN("%v = xor i32 %x, %x\nret i32 %v"), "xor i32", NULL, 0);
  fixture(TARGET FN("%v = sub i32 %x, %x\nret i32 %v"), "sub i32", NULL, 0);
  fixture(TARGET FN("%v = shl i32 %x, 32\nret i32 %v"), "shl i32", NULL, 0);
  fixture(TARGET FN("%v = shl i32 0, %x\nret i32 %v"), "shl i32", NULL, 0);
  fixture(TARGET FN("%v = udiv i32 %x, 1\nret i32 %v"), "udiv i32", NULL, 0);
  fixture(TARGET FN("%v = sdiv i32 %x, 1\nret i32 %v"), "sdiv i32", NULL, 0);
  fixture(TARGET FN("%v = add nsw i32 %x, 1\nret i32 %v"), "add nsw", NULL, 0);
  fixture(TARGET FN("%v = or disjoint i32 %x, 1\nret i32 %v"), "or disjoint",
          NULL, 0);
  fixture(TARGET FN("%v = lshr exact i32 %x, 1\nret i32 %v"), "lshr exact",
          NULL, 0);
  fixture(TARGET FN("%v = add i32 %x, 0, !foo !0\nret i32 %v") "!0 = !{}\n",
          ", !foo !0", NULL, 0);
  fixture(TARGET FN("%v = add i32 undef, 0\nret i32 %v"), "add i32 undef", NULL,
          0);
  fixture(TARGET FN("%v = and i32 poison, -1\nret i32 %v"), "and i32 poison",
          NULL, 0);
  fixture(TARGET FN("%p = freeze i32 poison\n%v = add i32 %p, 0\nret i32 %v"),
          "ret i32 %p", "add i32", 1);
  fixture(TARGET "define i1 @f(i1 %x) {\nentry:\n"
                 "%v = mul nsw i1 %x, 1\nret i1 %v\n}\n",
          "mul nsw i1", NULL, 0);
  fixture(TARGET "define i1 @f(i1 %x) {\nentry:\n"
                 "%v = mul nuw i1 %x, 1\nret i1 %v\n}\n",
          "ret i1 %x", "mul nuw", 1);
}
static void cast_tests(void) {
  const char *extensions[] = {"zext", "sext"};
  for (unsigned j = 0; j < 2; j++) {
    char source[1024];
    snprintf(source, sizeof(source),
             TARGET "define i8 @f(i8 %%x) {\nentry:\n"
                    "%%a = %s i8 %%x to i64\n%%b = trunc i64 %%a to i8\n"
                    "ret i8 %%b\n}\n",
             extensions[j]);
    fixture(source, "ret i8 %x", "to i64", 1);
    snprintf(source, sizeof(source),
             TARGET "define i16 @f(i8 %%x) {\nentry:\n"
                    "%%a = %s i8 %%x to i64\n%%b = trunc i64 %%a to i16\n"
                    "ret i16 %%b\n}\n",
             extensions[j]);
    fixture(source, "i8 %x to i16", "to i64", 1);
    snprintf(source, sizeof(source),
             TARGET "define i8 @f(i16 %%x) {\nentry:\n"
                    "%%a = %s i16 %%x to i64\n%%b = trunc i64 %%a to i8\n"
                    "ret i8 %%b\n}\n",
             extensions[j]);
    fixture(source, "trunc i16 %x to i8", "to i64", 1);
    snprintf(source, sizeof(source),
             TARGET "define i64 @f(i8 %%x) {\nentry:\n"
                    "%%a = %s i8 %%x to i16\n%%b = %s i16 %%a to i64\n"
                    "ret i64 %%b\n}\n",
             extensions[j], extensions[j]);
    fixture(source, "i8 %x to i64", "to i16", 1);
  }
  fixture(TARGET "define i64 @f(i1 %x) {\nentry:\n"
                 "%a = zext i1 %x to i8\n%b = sext i8 %a to i64\n"
                 "ret i64 %b\n}\n",
          "zext i1 %x to i64", "sext", 1);
  fixture(TARGET "define i8 @f(i64 %x) {\nentry:\n"
                 "%a = trunc i64 %x to i16\n%b = trunc i16 %a to i8\n"
                 "ret i8 %b\n}\n",
          "trunc i64 %x to i8", "to i16", 1);
  fixture(TARGET FN("%a = trunc i32 %x to i8\n"
                    "%b = zext i8 %a to i32\nret i32 %b"),
          "zext i8", NULL, 0);
  fixture(TARGET FN("%a = trunc i32 %x to i8\n"
                    "%b = sext i8 %a to i32\nret i32 %b"),
          "sext i8", NULL, 0);
  fixture(TARGET "define i32 @f(i8 %x) {\nentry:\n"
                 "%a = sext i8 %x to i16\n%b = zext i16 %a to i32\n"
                 "ret i32 %b\n}\n",
          "zext i16 %a to i32", NULL, 0);
  fixture(TARGET "define i8 @f(i8 %x) {\nentry:\n"
                 "%a = sext i8 %x to i32, !foo !0\n"
                 "%b = trunc i32 %a to i8\nret i8 %b\n}\n!0 = !{}\n",
          "trunc i32 %a", NULL, 0);
  fixture(TARGET "define i8 @f(i8 %x) {\nentry:\n"
                 "%a = sext i8 %x to i32\n"
                 "%b = trunc i32 %a to i8, !foo !0\n"
                 "ret i8 %b\n}\n!0 = !{}\n",
          ", !foo !0", NULL, 0);
  fixture(TARGET FN("%v = bitcast i32 %x to i32\nret i32 %v"), "ret i32 %x",
          "bitcast", 1);
  fixture(TARGET "define ptr @f(ptr %x) {\nentry:\n"
                 "%v = bitcast ptr %x to ptr\nret ptr %v\n}\n",
          "ret ptr %x", "bitcast", 1);
  fixture(TARGET "@g = global i32 0\n"
                 "define ptr @f() {\nentry:\n"
                 "%v = bitcast ptr @g to ptr\nret ptr %v\n}\n",
          "bitcast ptr @g", NULL, 0);
  fixture(TARGET "declare i32 @callee(i16)\n" FN(
              "%p = bitcast ptr @callee to ptr\n"
              "%v = call i32 %p(i32 %x)\nret i32 %v"),
          "call i32 %p(i32 %x)", NULL, 0);
  fixture(TARGET "define ptr @f() {\nentry:\n"
                 "%v = bitcast ptr null to ptr\nret ptr %v\n}\n",
          "bitcast ptr null", NULL, 0);
  fixture(TARGET FN("%v = inttoptr i32 %x to ptr\n"
                    "%w = ptrtoint ptr %v to i32\nret i32 %w"),
          "ptrtoint", NULL, 0);
  fixture(TARGET FN("%v = freeze i32 %x\nret i32 %v"), "freeze i32 %x", NULL,
          0);
  fixture(TARGET "define i8 @f() {\nentry:\n"
                 "%a = zext i8 undef to i32\n"
                 "%b = trunc i32 %a to i8\nret i8 %b\n}\n",
          "trunc i32 %a", NULL, 0);
  fixture(TARGET "define i32 @f(i8 %x) {\nentry:\n"
                 "%a = zext i8 %x to i16\n%b = add i16 %a, 0\n"
                 "%c = zext i16 %b to i32\nret i32 %c\n}\n",
          "zext i8 %x to i32", "to i16", 2);
  fixture(TARGET "define i8 @f(i8 %x) {\nentry:\n"
                 "br label %later\nfirst:\n"
                 "%c = trunc i64 %b to i8\nret i8 %c\nlater:\n"
                 "%a = sext i8 %x to i16\n"
                 "%b = sext i16 %a to i64\nbr label %first\n}\n",
          "ret i8 %x", "sext", 2);
  fixture(TARGET "define i16 @f(i8 %x) {\nentry:\n"
                 "%a = sext i8 %x to i64\n%b = trunc i64 %a to i16\n"
                 "%c = trunc i64 %a to i8\n"
                 "%d = zext i8 %c to i16\n"
                 "%r = add i16 %b, %d\nret i16 %r\n}\n",
          "sext i8 %x to i16", "to i64", 2);
}
static void flow_tests(void) {
  fixture(TARGET FN("%a = add i32 %x, 0\n"
                    "%b = xor i32 %a, 0\n"
                    "%c = icmp eq i32 %x, 0\n"
                    "br i1 %c, label %left, label %right\n"
                    "left:\nbr label %join\nright:\nbr label %join\n"
                    "join:\n%v = phi i32 [ %a, %left ], [ %b, %right ]\n"
                    "ret i32 %v"),
          "phi i32 [ %x", "%a =", 2);
  fixture(TARGET FN("%p = alloca i32\n"
                    "%v = load volatile i32, ptr %p\n"
                    "%r = add i32 %v, 0\nstore volatile i32 %r, ptr %p\n"
                    "ret i32 %r"),
          "store volatile i32 %v", "add i32", 1);
  fixture(TARGET "declare i32 @effect(i32)\n" FN(
              "%a = add i32 %x, 0\n%v = call i32 @effect(i32 %a)\nret i32 %v"),
          "call i32 @effect(i32 %x)", "%a =", 1);
  fixture(TARGET FN("br label %loop\nloop:\n"
                    "%i = phi i32 [ %x, %entry ], [ %n, %loop ]\n"
                    "%a = add i32 %i, 0\n%n = add i32 %a, 1\n"
                    "%c = icmp ult i32 %n, 7\n"
                    "br i1 %c, label %loop, label %out\nout:\nret i32 %n"),
          "%n = add i32 %i, 1", "%a =", 1);
  fixture(TARGET "define i32 @f(i32 %\"x\\20y\") {\nentry:\n"
                 "%\"v a\" = add i32 %\"x y\", 0\nret i32 %\"v a\"\n}\n",
          "ret i32 %\"x\\20y\"", "%\"v a\" =", 1);
  fixture(TARGET, "target triple", NULL, 0);
}
static void failures_and_scaling(void) {
  const char *input = "build/m07-cli-input.ll",
             *output = "build/m07-cli-output.ll";
  const char *invalid = TARGET FN("%v = add i32 %missing, 0\nret i32 %v");
  FILE *f = fopen(input, "wb");
  assert(f && fwrite(invalid, 1, strlen(invalid), f) == strlen(invalid) &&
         !fclose(f));
  f = fopen(output, "wb");
  assert(f && fwrite("sentinel", 1, 8, f) == 8 && !fclose(f));
  char option[64];
  snprintf(option, sizeof(option), "--passes=%s", pipeline);
  char *args[] = {"badge-opt",    option,
                  (char *)input,  "-o",
                  (char *)output, NULL};
  assert(badge_opt_main(5, args) == 1);
  const char *valid = TARGET FN("%v = add i32 %x, 0\nret i32 %v");
  f = fopen(input, "wb");
  assert(f && fwrite(valid, 1, strlen(valid), f) == strlen(valid) &&
         !fclose(f));
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    execl("build/badge-opt-combine-budget", "badge-opt-combine-budget",
          option, input, "-o", output, (char *)NULL);
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
  c->file = "combine-scaling.ll";
  assert(!setjmp(c->failure));
  ir_emit(c, TARGET "define i32 @f(i32 %%x) {\nentry:\n"
                    "%%v0 = add i32 %%x, 0\n");
  for (unsigned k = 1; k < 8000; k++)
    ir_emit(c, "%%v%u = add i32 %%v%u, 0\n", k, k - 1);
  ir_emit(c, "ret i32 %%v7999\n}\n");
  char *source = strdup(c->output);
  assert(source);
  ir_pipeline(c, source, strlen(source), pipeline);
  assert(strstr(c->output, "ret i32 %x") && !strstr(c->output, "add i32"));
  free(source);
  ir_destroy(c);
  free(c);
  c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "combine-allocation-limit.ll";
  c->memory = 128u * 1024u * 1024u;
  if (!setjmp(c->failure)) {
    ir_pipeline(c, valid, strlen(valid), pipeline);
    assert(!"exhausted allocation budget must fail");
  }
  ir_destroy(c);
  free(c);
}
static void boolean_tests(void) {
  unsigned widths[] = {8, 16, 32, 64};
  for (unsigned w = 0; w < sizeof(widths) / sizeof(*widths); w++)
    for (unsigned sign = 0; sign < 2; sign++)
      for (unsigned equality = 0; equality < 2; equality++)
        for (unsigned side = 0; side < 2; side++) {
          char source[1024];
          const char *constant = equality ? (sign ? "-1" : "1") : "0";
          snprintf(source, sizeof(source),
                   TARGET "define i1 @f(i1 %%x) {\nentry:\n"
                          "%%a = %s i1 %%x to i%u\n"
                          "%%b = icmp %s i%u %s, %s\nret i1 %%b\n}\n",
                   sign ? "sext" : "zext", widths[w], equality ? "eq" : "ne",
                   widths[w], side ? constant : "%a", side ? "%a" : constant);
          fixture(source, "ret i1 %x", "%a =", 1);
        }
  const char *rejected[] = {
      "%a = zext i1 %x to i32\n%b = icmp eq i32 %a, 0",
      "%a = zext i1 %x to i32\n%b = icmp ne i32 %a, 1",
      "%a = sext i1 %x to i32\n%b = icmp eq i32 %a, 1",
      "%a = zext i1 %x to i32\n%b = icmp eq i32 %a, 2",
      "%a = zext i1 undef to i32\n%b = icmp ne i32 %a, 0",
      "%a = zext i1 poison to i32\n%b = icmp ne i32 %a, 0",
      "%a = zext i1 %x to i32, !foo !0\n%b = icmp ne i32 %a, 0",
      "%a = zext i1 %x to i32\n%b = icmp ne i32 %a, 0, !foo !0"};
  for (unsigned k = 0; k < sizeof(rejected) / sizeof(*rejected); k++) {
    char source[1024];
    snprintf(source, sizeof(source),
             TARGET "define i1 @f(i1 %%x) {\nentry:\n%s\nret i1 %%b\n}\n"
                    "!0 = !{}\n", rejected[k]);
    fixture(source, "%b = icmp", NULL, 0);
  }
  fixture(TARGET "define i1 @f(i8 %x) {\nentry:\n"
                 "%a = zext i8 %x to i32\n%b = icmp ne i32 %a, 0\n"
                 "ret i1 %b\n}\n", "icmp ne i32", NULL, 0);
  fixture(TARGET "define i1 @f(i1 %x, ptr %p) {\nentry:\n"
                 "%a = zext i1 %x to i32\n%b = icmp ne i32 %a, 0\n"
                 "store volatile i32 %a, ptr %p\nret i1 %b\n}\n",
          "store volatile i32 %a", "%b =", 1);
  fixture(TARGET "define i1 @f(ptr %p) {\nentry:\n"
                 "%x = load volatile i1, ptr %p\n%a = zext i1 %x to i32\n"
                 "%b = icmp ne i32 %a, 0\nret i1 %b\n}\n",
          "load volatile i1", "%a =", 1);
  fixture(TARGET "define i1 @f(i1 %x) {\nentry:\n"
                 "%a = zext i1 %x to i16\n%b = icmp ne i16 %a, 0\n"
                 "%c = zext i1 %b to i32\n%d = icmp eq i32 %c, 1\n"
                 "ret i1 %d\n}\n", "ret i1 %x", "zext", 2);
}
int main(void) {
  arithmetic_tests();
  cast_tests();
  flow_tests();
  failures_and_scaling();
  pipeline = "scalar-cleanup";
  arithmetic_tests();
  cast_tests();
  flow_tests();
  failures_and_scaling();
  boolean_tests();
  printf("M07: %u LLVM-verified/idempotent positive/negative fixtures, "
         "8000 aliases and atomic invalid-input/work-budget failure PASS\n",
         fixtures);
  return 0;
}
