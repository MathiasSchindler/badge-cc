#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "host_io.h"
#include "ir_pass.h"
#include "target.h"
#include "test_mode.h"
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>

int badge_opt_main(int, char **);

#define TARGET                                                                 \
  "target datalayout = " BADGE_IR_LAYOUT "\ntarget triple = " BADGE_IR_TRIPLE  \
  "\n"
#define FN(body) "define i32 @f(i32 %x) {\nentry:\n" body "\n}\n"
#define LEGACY "constant-fold,aggregate-memory,wide-division"
static unsigned policy_steps;
static void policy_observe(void *context, unsigned step, const char *pass,
                           const char *event, const char *reason,
                           uint64_t count) {
  (void)context;
  (void)count;
  if (!strcmp(event, "pass") && !strcmp(reason, "started")) {
    assert(step == policy_steps++);
    assert(!strcmp(pass, badge_ir_default_pipeline[step]));
  }
}
static void default_policy(void) {
  const char *expected[] = {"mem2reg", "scalar-cleanup", "constant-fold",
                            "aggregate-memory", "wide-division", "local-memory",
                            "loop-scalar", "cross-function"};
  assert(badge_ir_default_pass_count == sizeof(expected) / sizeof(*expected));
  for (unsigned k = 0; k < badge_ir_default_pass_count; ++k)
    assert(!strcmp(expected[k], badge_ir_default_pipeline[k]));
  const char source[] =
      TARGET "@port = global i32 0\ndeclare void @effect()\n" FN(
          "%slot = alloca i32\nstore i32 %x, ptr %slot\n"
          "%loaded = load i32, ptr %slot\n%neutral = add i32 %loaded, 0\n"
          "%cond = icmp eq i32 1, 1\nbr i1 %cond, label %keep, label %dead\n"
          "keep:\n%a = add i32 %neutral, 1\n%b = add i32 %neutral, 1\n"
          "%sum = add i32 %a, %b\n%test = icmp slt i32 %neutral, 10\n"
          "%wide = zext i1 %test to i32\n%bool = icmp ne i32 %wide, 0\n"
          "%chosen = select i1 %bool, i32 %sum, i32 %neutral\n"
          "store volatile i32 %sum, ptr @port\nret i32 %chosen\n"
          "dead:\ncall void @effect()\nret i32 0");
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "default-policy.ll";
  assert(!setjmp(c->failure));
  c->observe = policy_observe;
  ir_pipeline(c, source, strlen(source), NULL);
  assert(policy_steps == badge_ir_default_pass_count && !c->memory);
  assert(!strstr(c->output, "alloca") && !strstr(c->output, "%neutral =") &&
         strstr(c->output, "call void @effect"));
  assert(strstr(c->output, "%a = add i32 %x, 1") &&
         strstr(c->output, "%b = add i32 %x, 1") &&
         strstr(c->output, "store volatile i32 %sum"));
  assert(!strstr(c->output, "%wide =") && !strstr(c->output, "%bool =") &&
         strstr(c->output, "select i1 %test"));
  char *result = strdup(c->output);
  assert(result);
  c->observe = NULL;
  ir_pipeline(c, source, strlen(source),
              "mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-"
              "division,local-memory,loop-scalar,cross-function");
  assert(!strcmp(result, c->output));
  ir_pipeline(c, source, strlen(source),
              "mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-"
              "division");
  assert(strstr(c->output, "%wide =") && strstr(c->output, "%bool ="));
  free(result);
  ir_pipeline(c, source, strlen(source), LEGACY);
  assert(strstr(c->output, "alloca") && strstr(c->output, "%neutral ="));
  ir_destroy(c);
  free(c);
}
static void run(const char *exe, const char *a, const char *b, const char *d,
                const char *e) {
  pid_t pid = fork();
  assert(pid >= 0);
  if (!pid) {
    execlp(exe, exe, a, b, d, e, (char *)NULL);
    _exit(127);
  }
  int status;
  assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);
}
static void save(const char *path, const char *text) {
  FILE *f = fopen(path, "wb");
  assert(f);
  assert(fwrite(text, 1, strlen(text), f) == strlen(text) && fclose(f) == 0);
}
static void fixture(const char *source, const char *fragment, int accepted,
                    const char *path, const char *assembly,
                    const char *object) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "fixture.ll";
  int failed = setjmp(c->failure);
  if (!failed)
    ir_pipeline(c, source, strlen(source), LEGACY);
  if (accepted >= 0 && (failed == 0) != accepted) {
    fprintf(stderr, "unexpected fixture result: %s\n", source);
    abort();
  }
  if (!failed) {
    if (fragment && !strstr(c->output, fragment)) {
      fprintf(stderr, "missing fragment %s in %s\n", fragment, c->output);
      abort();
    }
    assert(host_write_file("badge-opt-test", path, (unsigned char *)c->output,
                           c->size));
    const char *opt = badge_test_tool("OPT_TEST_LLVM");
    if (opt)
      run(opt, "-passes=verify", "-disable-output", path, NULL);
    run("./build/badge-codegen", path, "-o", assembly, NULL);
    run("./build/badge-asm", assembly, "-o", object, NULL);
  }
  ir_destroy(c);
  free(c);
}
int main(void) {
  default_policy();
  char dir[] = "build/opt-test-XXXXXX";
  assert(mkdtemp(dir));
  char path[128], input[128], assembly[128], object[128], alias[128];
  assert(snprintf(path, sizeof(path), "%s/output.ll", dir) > 0);
  assert(snprintf(input, sizeof(input), "%s/input.ll", dir) > 0);
  assert(snprintf(assembly, sizeof(assembly), "%s/output.s", dir) > 0);
  assert(snprintf(object, sizeof(object), "%s/output.o", dir) > 0);
  assert(snprintf(alias, sizeof(alias), "%s/alias.ll", dir) > 0);
  static const struct {
    const char *ir, *fragment;
  } positive[] = {
      {TARGET FN("%a = add i32 2, 3\nret i32 %a"), "ret i32 5"},
      {TARGET FN("%a = sub i8 0, 1\n%b = zext i8 %a to i32\nret i32 %b"),
       "ret i32 255"},
      {TARGET FN("%a = mul i64 4294967295, 4294967295\n%b = trunc i64 %a to "
                 "i32\nret i32 %b"),
       "ret i32 1"},
      {TARGET FN("%a = sdiv i32 -17, 3\nret i32 %a"), "ret i32 4294967291"},
      {TARGET FN("%a = srem i32 -17, 3\nret i32 %a"), "ret i32 4294967294"},
      {TARGET FN("%a = udiv i32 -1, 3\nret i32 %a"), "ret i32 1431655765"},
      {TARGET FN("%a = urem i32 -1, 7\nret i32 %a"), "ret i32 3"},
      {TARGET FN(
           "%a = sext i8 -128 to i64\n%b = trunc i64 %a to i32\nret i32 %b"),
       "ret i32 4294967168"},
      {TARGET FN("%a = shl i16 1, 15\n%b = zext i16 %a to i32\nret i32 %b"),
       "ret i32 32768"},
      {TARGET FN("%a = ashr i32 -2147483648, 31\nret i32 %a"),
       "ret i32 4294967295"},
      {TARGET FN("%a = lshr i64 -1, 63\n%b = trunc i64 %a to i32\nret i32 %b"),
       "ret i32 1"},
      {TARGET FN("%a = icmp slt i32 -1, 1\n%b = zext i1 %a to i32\nret i32 %b"),
       "ret i32 1"},
      {TARGET FN("%a = icmp ugt i64 -1, 1\n%b = zext i1 %a to i32\nret i32 %b"),
       "ret i32 1"},
      {TARGET FN("%a = or i8 128, 127\n%b = and i8 %a, 129\n%c = xor i8 %b, "
                 "1\n%d = zext i8 %c to i32\nret i32 %d"),
       "ret i32 128"},
      {TARGET FN("%a = add nsw i32 2147483647, 1\nret i32 %a"),
       "add nsw i32 2147483647, 1"},
      {TARGET FN("%a = load volatile i32, ptr null, align 4\nret i32 %a"),
       "load volatile i32"},
      {TARGET "define i32 @f(i32 %x){\nentry:\n%a = add i32 1, 2\nret i32 "
              "%a\n}\ndefine i32 @g(i32 %a){\nentry:\nret i32 %a\n}\n",
       "ret i32 %a"},
      {TARGET "define i32 @f([3 x i32] %x, ptr %p) {\nentry:\nstore [3 x i32] "
              "%x, ptr %p, align 4\nret i32 0\n}\n",
       "extractvalue [3 x i32]"},
      {TARGET "define i32 @f(ptr %p) {\nentry:\n%a = load [3 x i32], ptr %p, "
              "align 1\n%b = extractvalue [3 x i32] %a, 2\nret i32 %b\n}\n",
       "insertvalue [3 x i32]"},
      {TARGET FN("%p = alloca [3 x i32], align 4\nstore [3 x i32] "
                 "zeroinitializer, ptr %p, align 4\nret i32 0"),
       "extractvalue [3 x i32] zeroinitializer"},
      {TARGET "define i32 @f(ptr %p) {\nentry:\n%\".badge.opt.1\" = load i32, "
              "ptr %p, align 4\n%a = load [3 x i32], ptr %p, align 4\nret i32 "
              "%\".badge.opt.1\"\n}\n",
       "%\".badge.opt.2\""},
      {TARGET FN("%a = sext i32 %x to i64\n%b = sdiv i64 %a, 11200\n%c = trunc "
                 "i64 %b to i32\nret i32 %c"),
       "define internal i64"},
      {TARGET FN("%a = zext i32 %x to i64\n%b = urem i64 %a, 17\n%c = trunc "
                 "i64 %b to i32\nret i32 %c"),
       "mul i64"},
      {TARGET FN("%a = sext i32 %x to i64\n%b = sdiv i64 %a, 1024\n%c = trunc "
                 "i64 %b to i32\nret i32 %c"),
       "sdiv i64 %a, 1024"},
      {TARGET FN("%a = zext i32 %x to i64\n%b = udiv exact i64 %a, 17\n%c = "
                 "trunc i64 %b to i32\nret i32 %c"),
       "define internal i64"},
      {TARGET "define i64 @.badge.opt.1() {\nentry:\nret i64 7\n}\n" FN(
           "%a = sext i32 %x to i64\n%b = sdiv i64 %a, 17\n%c = trunc i64 %b "
           "to i32\nret i32 %c"),
       "@\".badge.opt.2\""},
      {TARGET "define i32 @f(ptr %p) {\nentry:\n%.badge.opt.1 = load i32, "
              "ptr %p, align 4\n%a = load [3 x i32], ptr %p, align 4\nret i32 "
              "%.badge.opt.1\n}\n",
       "%\".badge.opt.2\""},
  };
  for (size_t k = 0; k < sizeof(positive) / sizeof(*positive); k++)
    fixture(positive[k].ir, positive[k].fragment, 1, path, assembly, object);
  static const char *negative[] = {
      "",
      "target triple = \"x86_64-unknown-linux-gnu\"\n",
      TARGET "define i32 @f(i32 %x) {\nentry:\nret i32 %x\n",
      TARGET "}\n",
      TARGET FN("%a = load [3 x i32], ptr null, align 3\nret i32 0"),
      TARGET FN("%a = load volatile [3 x i32], ptr null, align 4\nret i32 0"),
      TARGET FN("%a = load [3 x i16], ptr null, align 2\nret i32 0"),
      TARGET FN("%a = load [257 x i32], ptr null, align 4\nret i32 0"),
      TARGET FN("%a = load [3 x i32], ptr null, align 4, !tag !1\nret i32 0"),
      TARGET FN("%a = udiv i64 4, poison\nret i32 0"),
      TARGET FN("%a = sdiv i64 2, poison\nret i32 0"),
      TARGET "source_filename = \"bad\\xx\"\n",
  };
  for (size_t k = 0; k < sizeof(negative) / sizeof(*negative); k++)
    fixture(negative[k], NULL, 0, path, assembly, object);
  const char seed[] = TARGET FN("%a = add i32 2, 3\nret i32 %a");
  for (size_t k = 0; k < sizeof(seed) - 1; k++) {
    char mutated[sizeof(seed)];
    memcpy(mutated, seed, sizeof(seed));
    mutated[k] ^= 64;
    ir_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->file = "mutation.ll";
    if (!setjmp(c->failure))
      ir_pipeline(c, mutated, strlen(mutated), NULL);
    ir_destroy(c);
    free(c);
  }
  save(input, TARGET "define i32 @f(");
  save(path, "preserved");
  char *args[] = {"badge-opt", input, "-o", path, NULL};
  assert(badge_opt_main(4, args) == 1);
  char preserved[16] = {0};
  FILE *f = fopen(path, "rb");
  assert(f);
  assert(fread(preserved, 1, sizeof(preserved), f) == 9 && fclose(f) == 0 &&
         !strcmp(preserved, "preserved"));
  save(input, TARGET FN("%a = add i32 2, 3\nret i32 %a"));
  char *selected[] = {"badge-opt", "--passes=constant-fold", input, "-o", path,
                      NULL};
  assert(badge_opt_main(5, selected) == 0);
  selected[1] = "--passes=missing";
  assert(badge_opt_main(5, selected) == 1);
  selected[1] = "--passes=";
  assert(badge_opt_main(5, selected) == 1);
  selected[1] = "--passes=constant-fold,";
  assert(badge_opt_main(5, selected) == 1);
  args[3] = input;
  assert(badge_opt_main(4, args) == 1);
  assert(link(input, alias) == 0);
  args[3] = alias;
  assert(badge_opt_main(4, args) == 1);
  assert(unlink(alias) == 0 && symlink("input.ll", alias) == 0);
  assert(badge_opt_main(4, args) == 1);
  assert(badge_opt_main(1, args) == 2);
  args[3] = path;
  f = fopen(input, "wb");
  assert(f);
  assert(ftruncate(fileno(f), 16 * 1024 * 1024 + 1) == 0 && fclose(f) == 0);
  assert(badge_opt_main(4, args) == 1);
  f = fopen(input, "wb");
  assert(f);
  assert(fwrite("a\0b", 1, 3, f) == 3 && fclose(f) == 0);
  assert(badge_opt_main(4, args) == 1);
  assert(unlink(alias) == 0 && unlink(input) == 0 && unlink(path) == 0 &&
         unlink(assembly) == 0 && unlink(object) == 0 && rmdir(dir) == 0);
  printf("badge-opt: %zu %sbackend-assembled positive, %zu "
         "negative, %zu mutation sanitizer fixtures; pass selection/output "
         "preservation/alias checks PASS\n",
         sizeof(positive) / sizeof(*positive),
         badge_test_oracles_enabled() ? "LLVM-verified/" : "owned ",
         sizeof(negative) / sizeof(*negative), sizeof(seed) - 1);
  return 0;
}
