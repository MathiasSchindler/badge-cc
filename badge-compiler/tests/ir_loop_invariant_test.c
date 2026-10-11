#define _DARWIN_C_SOURCE
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
#define LOOP(body)                                                             \
  TARGET "define i32 @f(i32 %x, i32 %n) {\nentry:\nbr label %header\n"         \
         "header:\n%i = phi i32 [0, %entry], [%next, %body]\n"                 \
         "%sum = phi i32 [0, %entry], [%add, %body]\n"                         \
         "%go = icmp ult i32 %i, %n\nbr i1 %go, label %body, label %exit\n"    \
         "body:\n" body "\n%add = add i32 %sum, %v\n%next = add i32 %i, 1\n"   \
         "br label %header\nexit:\nret i32 %sum\n}\n"
int badge_opt_main(int, char **);
static unsigned fixtures;
static uint64_t transformed;
static const char *pipeline = "loop-invariant";
static void observe(void *unused, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  (void)unused;
  (void)step;
  if (!strcmp(pass, pipeline) && !strcmp(event, "transformed") &&
      !strcmp(reason, "invariant_hoisted"))
    transformed += count;
}
static void oracle(const char *source) {
  const char *tool = badge_test_tool("OPT_TEST_LLVM");
  if (!tool)
    return;
  const char *file = "build/p06-oracle-fixture.ll";
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
static void fixture(const char *source, unsigned changes, const char *moved,
                    const char *header) {
  oracle(source);
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "loop-invariant.ll";
  c->observe = observe;
  if (setjmp(c->failure))
    abort();
  transformed = 0;
  ir_pipeline(c, source, strlen(source), pipeline);
  assert(!c->memory);
  if (transformed != changes ||
      (moved && (!strstr(c->output, moved) || !strstr(c->output, header) ||
                 strstr(c->output, moved) > strstr(c->output, header)))) {
    fprintf(stderr, "P06 fixture %u: %llu moves expected %u\n%s\n", fixtures,
            (unsigned long long)transformed, changes, c->output);
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
static void expressions(void) {
  const char *bodies[] = {
      "%v = add i32 %x, 7",   "%v = sub i32 %x, 7",  "%v = mul i32 %x, 7",
      "%v = and i32 %x, 255", "%v = or i32 %x, 256", "%v = xor i32 %x, 255",
      "%v = shl i32 %x, 7",   "%v = lshr i32 %x, 7", "%v = ashr i32 %x, 7",
      "%v = udiv i32 %x, 7",  "%v = urem i32 %x, 7", "%v = sdiv i32 %x, -7",
      "%v = srem i32 %x, -7", "%v = sdiv i32 7, -1"};
  for (unsigned k = 0; k < sizeof(bodies) / sizeof(*bodies); k++) {
    char source[2048];
    snprintf(source, sizeof(source), "%s%s%s",
             TARGET
             "define i32 @f(i32 %x, i32 %n) {\nentry:\nbr label %header\n"
             "header:\n%i = phi i32 [0, %entry], [%next, %body]\n"
             "%sum = phi i32 [0, %entry], [%add, %body]\n"
             "%go = icmp ult i32 %i, %n\nbr i1 %go, label %body, label "
             "%exit\nbody:\n",
             bodies[k],
             "\n%add = add i32 %sum, %v\n%next = add i32 %i, 1\n"
             "br label %header\nexit:\nret i32 %sum\n}\n");
    fixture(source, 1, "%v =", "header:\n");
  }
  fixture(LOOP("%a = mul i32 %x, 7\n%v = add i32 %a, 11"), 2,
          "%v =", "header:\n");
  const char *rejected[] = {"%v = add nsw i32 %x, 7",
                            "%v = mul nuw i32 %x, 7",
                            "%v = udiv exact i32 %x, 7",
                            "%v = sdiv i32 %x, -1",
                            "%v = sdiv i32 -2147483648, -1",
                            "%v = udiv i32 %x, 0",
                            "%v = udiv i32 %x, %n",
                            "%v = shl i32 %x, %n",
                            "%v = shl i32 %x, 32",
                            "%v = add i32 %x, poison",
                            "%v = add i32 %x, undef",
                            "%v = freeze i32 %x",
                            "%v = add i32 %x, %i"};
  for (unsigned k = 0; k < sizeof(rejected) / sizeof(*rejected); k++) {
    char source[2048];
    snprintf(source, sizeof(source), "%s", LOOP("%v = add i32 %x, 7"));
    char *at = strstr(source, "%v =");
    const char *end = strchr(at, '\n');
    char tail[1024];
    snprintf(tail, sizeof(tail), "%s", end);
    snprintf(at, sizeof(source) - (size_t)(at - source), "%s%s", rejected[k],
             tail);
    fixture(source, 0, NULL, NULL);
  }
  fixture(TARGET
          "define i32 @f(ptr %p, i32 %n) {\nentry:\nbr label %header\n"
          "header:\n%i = phi i32 [0, %entry], [%next, %header]\n"
          "%v = load volatile i32, ptr %p, align 4\n"
          "store volatile i32 %v, ptr %p, align 4\ncall void asm sideeffect "
          "\"nop\", \"~{memory}\"()\n"
          "%next = add i32 %i, 1\n%go = icmp ult i32 %next, %n\n"
          "br i1 %go, label %header, label %exit\nexit:\nret i32 %v\n}\n",
          0, NULL, NULL);
  fixture(
      TARGET
      "define i32 @f(ptr %p, i32 %n) {\nentry:\nbr label %header\n"
      "header:\n%i = phi i32 [0, %entry], [%next, %header]\n"
      "%a = getelementptr i32, ptr %p, i32 2\n%v = load i32, ptr %a, align 4\n"
      "%next = add i32 %i, 1\n%go = icmp ult i32 %next, %n\n"
      "br i1 %go, label %header, label %exit\nexit:\nret i32 %v\n}\n",
      1, "%a =", "header:\n");
  fixture(LOOP("%small = trunc i32 %x to i8\n%v = zext i8 %small to i32"), 2,
          "%v =", "header:\n");
}
static void control(void) {
  fixture(
      TARGET
      "define i32 @f(i32 %x, i32 %n) {\nentry:\nbr label %header\n"
      "header:\n%i = phi i32 [0, %entry], [%li, %left], [%ri, %right]\n"
      "%go = icmp ult i32 %i, %n\nbr i1 %go, label %choose, label %exit\n"
      "choose:\n%cond = icmp eq i32 %x, 0\nbr i1 %cond, label %left, label "
      "%right\n"
      "left:\n%li = add i32 %i, 1\nbr label %header\n"
      "right:\n%ri = add i32 %i, 2\nbr label %header\nexit:\nret i32 %i\n}\n",
      1, "%cond =", "header:\n");
  fixture(
      TARGET
      "define i32 @f(i32 %x, i1 %c) {\nentry:\nbr i1 %c, label %a, label %b\n"
      "a:\nbr label %header\nb:\nbr label %header\nheader:\n"
      "%i = phi i32 [0, %a], [0, %b], [%next, %header]\n"
      "%v = add i32 %x, 7\n%next = add i32 %i, 1\n%go = icmp ult i32 %next, 3\n"
      "br i1 %go, label %header, label %exit\nexit:\nret i32 %v\n}\n",
      0, NULL, NULL);
  fixture(TARGET
          "define i32 @f(i32 %x, i1 %c) {\nentry:\nbr i1 %c, label %header, "
          "label %exit\n"
          "header:\n%v = add i32 %x, 7\nbr i1 %c, label %header, label %exit\n"
          "exit:\nret i32 %x\n}\n",
          0, NULL, NULL);
  fixture(
      TARGET
      "define i32 @f(i32 %x, i1 %c) {\nentry:\nbr i1 %c, label %a, label %b\n"
      "a:\n%v = add i32 %x, 7\nbr label %b\n"
      "b:\n%w = mul i32 %x, 3\nbr i1 %c, label %a, label %exit\n"
      "exit:\nret i32 %x\n}\n",
      0, NULL, NULL);
  fixture(TARGET "define i32 @f(i32 %x) {\nentry:\nret i32 %x\n"
                 "dead:\n%v = add i32 %x, 7\nbr label %dead\n}\n",
          0, NULL, NULL);
  fixture(
      TARGET
      "define i32 @f(i32 %x) {\nentry:\nbr label %outer\n"
      "outer:\n%i = phi i32 [0, %entry], [%next, %after]\n"
      "%go = icmp ult i32 %i, 3\nbr i1 %go, label %pre, label %exit\n"
      "pre:\nbr label %inner\ninner:\n%j = phi i32 [0, %pre], [%jn, %inner]\n"
      "%a = mul i32 %x, 7\n%v = add i32 %a, 11\n"
      "%jn = add i32 %j, 1\n%more = icmp ult i32 %jn, 4\n"
      "br i1 %more, label %inner, label %after\n"
      "after:\n%next = add i32 %i, 1\nbr label %outer\nexit:\nret i32 %x\n}\n",
      2, "%v =", "outer:\n");
}
static void failures(void) {
  const char *input = "build/p06-input.ll", *output = "build/p06-output.ll";
  const char *valid = LOOP("%v = add i32 %x, 7");
  FILE *f = fopen(input, "wb");
  assert(f && fwrite(valid, 1, strlen(valid), f) == strlen(valid) &&
         !fclose(f));
  f = fopen(output, "wb");
  assert(f && fwrite("sentinel", 1, 8, f) == 8 && !fclose(f));
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    char option[64];
    snprintf(option, sizeof(option), "--passes=%s", pipeline);
    execl("build/badge-opt-loop-budget", "badge-opt-loop-budget",
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
  char option[64];
  snprintf(option, sizeof(option), "--passes=%s", pipeline);
  char *args[] = {"badge-opt",    option,
                  (char *)input,  "-o",
                  (char *)output, NULL};
  f = fopen(input, "wb");
  const char *invalid = LOOP("%v = add i32 %missing, 7");
  assert(f && fwrite(invalid, 1, strlen(invalid), f) == strlen(invalid) &&
         !fclose(f));
  assert(badge_opt_main(5, args) == 1);
  f = fopen(output, "rb");
  memset(bytes, 0, sizeof(bytes));
  assert(f && fread(bytes, 1, sizeof(bytes), f) == 8 &&
         !strcmp(bytes, "sentinel") && !fclose(f));
  assert(!unlink(input) && !unlink(output));
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "loop-allocation-limit.ll";
  c->memory = 128u * 1024u * 1024u;
  if (!setjmp(c->failure)) {
    ir_pipeline(c, valid, strlen(valid), pipeline);
    assert(!"exhausted allocation budget must fail");
  }
  ir_destroy(c);
  free(c);
}
static void scaling(void) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "loop-scaling.ll";
  if (setjmp(c->failure))
    abort();
  ir_emit(c,
          TARGET "define i32 @f(i32 %%x) {\nentry:\nbr label %%header\n"
                 "header:\n%%i = phi i32 [0, %%entry], [%%next, %%header]\n");
  for (unsigned k = 0; k < 2000; k++)
    ir_emit(c, "%%v%u = add i32 %%x, %u\n", k, k);
  ir_emit(
      c,
      "%%next = add i32 %%i, 1\n%%go = icmp ult i32 %%next, 3\n"
      "br i1 %%go, label %%header, label %%exit\nexit:\nret i32 %%v1999\n}\n");
  char *source = strdup(c->output);
  assert(source);
  fixture(source, 2000, "%v1999 =", "header:\n");
  free(source);
  ir_destroy(c);
  free(c);
}
static void cost_tests(void) {
  pipeline = "loop-cost";
  fixture(LOOP("%v = mul i32 %x, 240"), 1, "%v =", "header:\n");
  fixture(LOOP("%v = mul i32 -7, %x"), 1, "%v =", "header:\n");
  fixture(LOOP("%v = sdiv i32 %x, 3"), 1, "%v =", "header:\n");
  fixture(LOOP("%v = urem i32 %x, 4294967293"), 1, "%v =", "header:\n");
  const char *operations[] = {"mul", "udiv", "urem", "sdiv", "srem"};
  for (unsigned op = 0; op < sizeof(operations) / sizeof(*operations); op++)
    for (unsigned n = 3; n < 32; n += 2) {
      char source[2048];
      snprintf(source, sizeof(source),
               TARGET
               "define i32 @f(i32 %%x, i32 %%n) {\nentry:\nbr label %%header\n"
               "header:\n%%i = phi i32 [0, %%entry], [%%next, %%body]\n"
               "%%sum = phi i32 [0, %%entry], [%%add, %%body]\n"
               "%%go = icmp ult i32 %%i, %%n\n"
               "br i1 %%go, label %%body, label %%exit\nbody:\n"
               "%%v = %s i32 %%x, %u\n%%add = add i32 %%sum, %%v\n"
               "%%next = add i32 %%i, 1\nbr label %%header\n"
               "exit:\nret i32 %%sum\n}\n", operations[op], n);
      fixture(source, 1, "%v =", "header:\n");
    }
  const char *rejected[] = {
      "%v = mul i32 %x, 0", "%v = mul i32 %x, 1", "%v = mul i32 %x, -1",
      "%v = mul i32 %x, 256", "%v = mul i32 %x, -256",
      "%v = mul i32 %x, -2147483648", "%v = mul i32 %x, %n",
      "%v = mul nuw i32 %x, 7", "%v = mul nsw i32 %x, 7",
      "%v = sdiv i32 %x, -1", "%v = sdiv i32 %x, -2",
      "%v = udiv exact i32 %x, 3", "%v = udiv i32 %x, 0",
      "%v = udiv i32 %x, %n", "%v = mul i32 %i, 7",
      "%v = mul i32 %x, poison", "%v = mul i32 %x, undef",
      "%v = add i32 %x, 7",
      "%a = mul i32 %x, 7\n%b = mul i32 %x, 11\n%v = add i32 %a, %b"};
  for (unsigned k = 0; k < sizeof(rejected) / sizeof(*rejected); k++) {
    char source[2048], tail[1024];
    snprintf(source, sizeof(source), "%s", LOOP("%v = mul i32 %x, 7"));
    char *at = strstr(source, "%v =");
    snprintf(tail, sizeof(tail), "%s", strchr(at, '\n'));
    snprintf(at, sizeof(source) - (size_t)(at - source), "%s%s", rejected[k],
             tail);
    fixture(source, 0, NULL, NULL);
  }
  fixture(LOOP("%a = mul i32 %x, 7\n%v = mul i32 %a, 11"),
          1, "%a =", "header:\n");
  fixture(LOOP("%small = trunc i32 %x to i16\n"
               "%product = mul i16 %small, 7\n%v = zext i16 %product to i32"),
          0, NULL, NULL);
  fixture(TARGET
          "define i32 @f(i32 %x) {\nentry:\nbr label %outer\n"
          "outer:\n%i = phi i32 [0, %entry], [%next, %after]\n"
          "br label %pre\npre:\nbr label %inner\n"
          "inner:\n%j = phi i32 [0, %pre], [%jn, %inner]\n"
          "%a = mul i32 %x, 7\n%z = mul i32 %a, 11\n%jn = add i32 %j, %z\n"
          "%more = icmp ult i32 %jn, 4\n"
          "br i1 %more, label %inner, label %after\n"
          "after:\n%next = add i32 %i, 1\n%go = icmp ult i32 %next, 3\n"
          "br i1 %go, label %outer, label %exit\nexit:\nret i32 %z\n}\n",
          1, "%a =", "outer:\n");
  fixture(TARGET
          "define i32 @f(i32 %x, i1 %c) {\nentry:\n"
          "br i1 %c, label %a, label %b\na:\nbr label %header\n"
          "b:\nbr label %header\nheader:\n"
          "%i = phi i32 [0, %a], [0, %b], [%next, %header]\n"
          "%v = mul i32 %x, 7\n%next = add i32 %i, %v\n"
          "%go = icmp ult i32 %next, 3\n"
          "br i1 %go, label %header, label %exit\nexit:\nret i32 %v\n}\n",
          0, NULL, NULL);
  fixture(TARGET
          "define i32 @f(ptr %p, i32 %n) {\nentry:\nbr label %header\n"
          "header:\n%i = phi i32 [0, %entry], [%next, %header]\n"
          "%x = load volatile i32, ptr %p, align 4\n%v = mul i32 %x, 7\n"
          "store volatile i32 %v, ptr %p, align 4\n"
          "%next = add i32 %i, 1\n%go = icmp ult i32 %next, %n\n"
          "br i1 %go, label %header, label %exit\nexit:\nret i32 %v\n}\n",
          0, NULL, NULL);
  failures();
  pipeline = "loop-invariant";
}
int main(void) {
  expressions();
  control();
  scaling();
  failures();
  cost_tests();
  printf("P06: %u LLVM-verified/idempotent loop, flag, trap, dependency, CFG "
         "and atomic failure fixtures PASS\n",
         fixtures);
  return 0;
}
