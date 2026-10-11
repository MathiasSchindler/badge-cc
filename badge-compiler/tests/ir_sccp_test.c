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
static uint64_t blocks, edges;
static void observe(void *context, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  (void)context;
  (void)step;
  if (strcmp(pass, "sccp") || strcmp(event, "removed"))
    return;
  if (!strcmp(reason, "unreachable_block"))
    blocks += count;
  if (!strcmp(reason, "CFG_edge"))
    edges += count;
}
static void oracle(const char *source) {
  const char *tool = badge_test_tool("OPT_TEST_LLVM");
  if (!tool)
    return;
  const char *file = "build/m06-oracle-fixture.ll";
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
                    unsigned nb, unsigned ne) {
  oracle(source);
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "sccp.ll";
  c->observe = observe;
  if (setjmp(c->failure)) {
    fprintf(stderr, "unexpected SCCP failure:\n%s\n", source);
    abort();
  }
  blocks = edges = 0;
  ir_pipeline(c, source, strlen(source), "sccp");
  assert(c->memory == 0);
  if ((present && !strstr(c->output, present)) ||
      (absent && strstr(c->output, absent)) || blocks != nb || edges != ne) {
    fprintf(stderr, "SCCP fixture %u failed: blocks=%llu edges=%llu\n%s\n",
            fixtures, (unsigned long long)blocks, (unsigned long long)edges,
            c->output);
    abort();
  }
  oracle(c->output);
  char *result = strdup(c->output);
  assert(result);
  size_t size = c->size;
  c->observe = NULL;
  ir_pipeline(c, result, size, "sccp");
  assert(c->size == size && !memcmp(c->output, result, size));
  free(result);
  ir_destroy(c);
  free(c);
  fixtures++;
}
static void failure_tests(void) {
  const char *input = "build/m06-cli-input.ll",
             *output = "build/m06-cli-output.ll";
  const char *invalid = TARGET FN("br i1 true, label %live, label %dead\n"
                                  "live:\nret i32 %x\ndead:\nret i32 %missing");
  FILE *f = fopen(input, "wb");
  assert(f && fwrite(invalid, 1, strlen(invalid), f) == strlen(invalid) &&
         !fclose(f));
  f = fopen(output, "wb");
  assert(f && fwrite("sentinel", 1, 8, f) == 8 && !fclose(f));
  char *args[] = {"badge-opt", "--passes=sccp", (char *)input,
                  "-o",        (char *)output,  NULL};
  assert(badge_opt_main(5, args) == 1);
  f = fopen(output, "rb");
  char bytes[16] = {0};
  assert(f && fread(bytes, 1, sizeof(bytes), f) == 8 &&
         !strcmp(bytes, "sentinel") && !fclose(f));
  const char *valid = TARGET FN("%a = add i32 %x, 1\nret i32 %a");
  f = fopen(input, "wb");
  assert(f && fwrite(valid, 1, strlen(valid), f) == strlen(valid) &&
         !fclose(f));
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    execl("build/badge-opt-sccp-budget", "badge-opt-sccp-budget",
          "--passes=sccp", input, "-o", output, (char *)NULL);
    _exit(127);
  }
  int status;
  assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 1);
  f = fopen(output, "rb");
  memset(bytes, 0, sizeof(bytes));
  assert(f && fread(bytes, 1, sizeof(bytes), f) == 8 &&
         !strcmp(bytes, "sentinel") && !fclose(f));
  assert(!unlink(input) && !unlink(output));
}
static void scaling(void) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "sccp-scaling.ll";
  assert(!setjmp(c->failure));
  ir_emit(c, TARGET "define i32 @f(i32 %%x) {\nentry:\n");
  ir_emit(c, "%%v0 = add i32 %%x, 1\n");
  for (unsigned k = 1; k < 8000; k++)
    ir_emit(c, "%%v%u = add i32 %%v%u, 1\n", k, k - 1);
  ir_emit(c, "ret i32 %%v7999\n}\n");
  char *source = strdup(c->output);
  assert(source);
  ir_pipeline(c, source, strlen(source), "sccp");
  assert(strstr(c->output, "ret i32 %v7999"));
  free(source);
  ir_destroy(c);
  free(c);
}
int main(void) {
  fixture(TARGET FN("%c = icmp eq i32 4, 4\n"
                    "br i1 %c, label %a, label %b\n"
                    "a:\nbr label %join\nb:\nbr label %join\n"
                    "join:\n%v = phi i32 [ 7, %a ], [ 9, %b ]\nret i32 %v"),
          "ret i32 7", "phi", 1, 2);
  fixture(TARGET FN("br i1 false, label %a, label %b\n"
                    "a:\nbr label %join\nb:\nbr label %join\n"
                    "join:\n%v = phi i32 [ %x, %a ], [ %x, %b ]\nret i32 %v"),
          "ret i32 %x", "phi", 1, 2);
  fixture(TARGET FN("switch i32 2, label %other [\n"
                    "i32 1, label %one\ni32 2, label %two\n]\n"
                    "other:\nret i32 0\none:\nret i32 1\ntwo:\nret i32 2"),
          "br label %two", "switch", 2, 2);
  fixture(TARGET FN("switch i32 9, label %other [ i32 1, label %one ]\n"
                    "other:\nret i32 0\none:\nret i32 1"),
          "br label %other", "switch", 1, 1);
  fixture(TARGET FN("switch i32 %x, label %other [ i32 1, label %one ]\n"
                    "other:\nret i32 0\none:\nret i32 1"),
          "switch i32 %x", NULL, 0, 0);
  fixture(TARGET FN("switch i32 1, label %join [ i32 1, label %join ]\n"
                    "join:\n%v = phi i32 [ %x, %entry ], [ %x, %entry ]\n"
                    "ret i32 %v"),
          "ret i32 %x", "phi", 0, 1);
  fixture(TARGET FN("switch i32 %x, label %join [ i32 1, label %join ]\n"
                    "join:\n%v = phi i32 [ 8, %entry ], [ 8, %entry ]\n"
                    "ret i32 %v"),
          "ret i32 8", "phi", 0, 0);
  fixture(TARGET FN("br label %loop\nloop:\n"
                    "%i = phi i32 [ 0, %entry ], [ %n, %loop ]\n"
                    "%n = add i32 %i, 1\n%c = icmp ult i32 %i, 3\n"
                    "br i1 %c, label %loop, label %exit\nexit:\nret i32 %i"),
          "br i1 %c", NULL, 0, 0);
  fixture(TARGET FN("br label %loop\nloop:\n"
                    "%i = phi i32 [ 7, %entry ], [ %i, %loop ]\n"
                    "%c = icmp eq i32 %i, 7\n"
                    "br i1 %c, label %loop, label %exit\nexit:\nret i32 %i"),
          "loop:\n  br label %loop", "exit:", 1, 1);
  fixture(TARGET FN("br label %later\n"
                    "first:\n%c = icmp eq i32 %v, 3\n"
                    "br i1 %c, label %yes, label %no\n"
                    "later:\n%v = add i32 1, 2\nbr label %first\n"
                    "yes:\nret i32 3\nno:\nret i32 4"),
          "br label %yes", "no:", 1, 1);
  fixture(TARGET FN("br i1 undef, label %a, label %b\n"
                    "a:\nret i32 1\nb:\nret i32 2"),
          "br i1 undef", NULL, 0, 0);
  fixture(TARGET FN("%c = freeze i1 poison\nbr i1 %c, label %a, label %b\n"
                    "a:\nret i32 1\nb:\nret i32 2"),
          "freeze i1 poison", NULL, 0, 0);
  fixture(TARGET FN("%v = select i1 true, i32 7, i32 poison\nret i32 %v"),
          "ret i32 7", "select", 0, 0);
  fixture(TARGET FN("%v = select i1 poison, i32 7, i32 7\nret i32 %v"),
          "select i1 poison", NULL, 0, 0);
  fixture(TARGET FN("%v = add nsw i32 2147483647, 1\nret i32 %v"), "add nsw",
          NULL, 0, 0);
  fixture(TARGET FN("%v = or disjoint i32 3, 3\nret i32 %v"), "or disjoint",
          NULL, 0, 0);
  fixture(TARGET FN("%v = udiv exact i32 9, 2\nret i32 %v"), "udiv exact", NULL,
          0, 0);
  fixture(TARGET FN("%v = sdiv i32 2147483648, 4294967295\nret i32 %v"),
          "sdiv i32", NULL, 0, 0);
  fixture(TARGET FN("%v = udiv i32 9, 0\nret i32 %v"), "udiv i32 9, 0", NULL, 0,
          0);
  fixture(TARGET FN("%v = shl i32 1, 32\nret i32 %v"), "shl i32 1, 32", NULL, 0,
          0);
  fixture(TARGET FN("%v = sext i8 -128 to i64\n"
                    "%w = ashr i64 %v, 63\n"
                    "%c = icmp eq i64 %w, -1\n"
                    "%r = zext i1 %c to i32\nret i32 %r"),
          "ret i32 1", "sext", 0, 0);
  fixture(TARGET FN("%v = add i8 255, 1\n"
                    "%w = zext i8 %v to i32\nret i32 %w"),
          "ret i32 0", "add", 0, 0);
  fixture(TARGET "declare void @effect()\n" FN(
              "call void @effect()\nbr i1 true, label %a, label %dead\n"
              "a:\n%v = load volatile i32, ptr null\nret i32 %v\n"
              "dead:\ncall void @effect()\nstore volatile i32 7, ptr null\n"
              "ret i32 4"),
          "load volatile", "store volatile", 1, 1);
  fixture(TARGET FN("br i1 true, label %a, label %b, !foo !0\n"
                    "a:\nret i32 1\nb:\nret i32 2") "!0 = !{}\n",
          "label %b, !foo !0", NULL, 0, 0);
  fixture(TARGET FN("br i1 true, label %a, label %b\n"
                    "a:\nbr label %join\nb:\nbr label %join\n"
                    "join:\n%v = phi i32 [ %x, %a ], [ %x, %b ], !foo !0\n"
                    "ret i32 %v") "!0 = !{}\n",
          "phi i32 [ %x, %a ], !foo !0", "%b ]", 1, 2);
  fixture(TARGET "define i32 @quoted() {\n\"en\\74ry\":\n"
                 "br i1 true, label %\"a\\20b\", label %dead\n"
                 "dead:\nret i32 9\n\"a b\":\nret i32 7\n}\n",
          "br label %\"a b\"", "dead:", 1, 1);
  fixture(TARGET "@a = global ptr blockaddress(@f, %gone)\n"
                 "define i32 @f() {\nentry:\n"
                 "br i1 true, label %live, label %gone\n"
                 "live:\nret i32 0\ngone:\nret i32 1\n}\n",
          "gone:", NULL, 0, 0);
  fixture(TARGET FN("br i1 undef, label %a, label %b\n"
                    "a:\n%p = phi i32 [ 4, %entry ], [ %q, %b ]\n"
                    "br label %b\nb:\n"
                    "%q = phi i32 [ 4, %entry ], [ %p, %a ]\n"
                    "%c = icmp eq i32 %q, 4\n"
                    "br i1 %c, label %a, label %out\nout:\nret i32 9"),
          "br label %a", "out:", 1, 1);
  fixture(TARGET FN("br label %a\na:\n"
                    "%p = phi ptr [ null, %entry ], [ %p, %a ]\n"
                    "store i32 %x, ptr %p\n"
                    "%c = icmp eq i32 %x, 0\n"
                    "br i1 %c, label %a, label %out\nout:\nret i32 %x"),
          "store i32 %x, ptr null", "phi ptr", 0, 0);
  fixture(TARGET, "target triple", NULL, 0, 0);
  fixture(TARGET FN("%c = icmp eq i32 %x, 0\n"
                    "br i1 %c, label %a, label %b\n"
                    "a:\n%av = phi i32 [ %x, %entry ]\nbr label %join\n"
                    "b:\n%bv = phi i32 [ %x, %entry ]\nbr label %join\n"
                    "join:\n%v = phi i32 [ %av, %a ], [ %bv, %b ]\nret i32 %v"),
          "ret i32 %x", "phi", 0, 0);
  failure_tests();
  scaling();
  printf("badge-opt: SCCP %u LLVM-verified/idempotent fixtures, sparse 8000-"
         "definition scaling, invalid-dead-code and budget atomicity PASS\n",
         fixtures);
  return 0;
}
