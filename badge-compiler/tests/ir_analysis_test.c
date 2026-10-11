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

static const char graph[] =
    TARGET "; untouched metadata and attributes\n"
           "declare i32 @external(i32)\n"
           "define i32 @f(i32 %x) #0 {\n"
           "entry:\n%p = alloca i32, align 4\n"
           "store volatile i32 %x, ptr %p, align 4\n"
           "%v = load volatile i32, ptr %p, align 4\n"
           "%condition = icmp slt i32 %v, 0\n"
           "br i1 %condition, label %left, label %right\n"
           "left:\n%a = add nsw i32 %v, 1\nbr label %merge\n"
           "right:\n%b = call i32 @external(i32 %v)\nbr label %merge\n"
           "merge:\n%q = phi i32 [ %a, %left ], [ %b, %right ]\n"
           "br label %loop\n"
           "loop:\n%i = phi i32 [ %q, %merge ], [ %next, %loop ]\n"
           "%next = sub i32 %i, 1\n%c = icmp eq i32 %next, 0\n"
           "br i1 %c, label %exit, label %loop\n"
           "exit:\nret i32 %next\n}\n"
           "attributes #0 = { nounwind \"uninterpreted\"=\"preserved\" }\n";

static void oracle(const char *source) {
  const char *tool = badge_test_tool("OPT_TEST_LLVM");
  if (!tool)
    return;
  const char *file = "build/m02-oracle-fixture.ll";
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
static void fixture(const char *source, int valid) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "analysis-fixture.ll";
  int failed = setjmp(c->failure);
  if (!failed) {
    ir_module m = {0};
    ir_analysis a;
    ir_parse(c, &m, source, strlen(source));
    ir_analyze(c, &m, &a);
    ir_write(c, &m);
    assert(c->size == strlen(source) && !memcmp(c->output, source, c->size));
  }
  if ((!failed) != valid) {
    fprintf(stderr, "unexpected analysis result for:\n%s\n", source);
    abort();
  }
  ir_destroy(c);
  free(c);
  if (valid)
    oracle(source);
}
static unsigned named_definition(const ir_analysis *a, const char *name) {
  for (unsigned k = 0; k < a->definition_count; k++)
    if (!strcmp(a->definitions[k].name, name))
      return k;
  abort();
}
static unsigned operation(const ir_analysis *a, const char *op) {
  for (unsigned k = 0; k < a->instruction_count; k++)
    if (!strcmp(a->instructions[k].operation, op))
      return k;
  abort();
}
static void colliding_names(ir_context *c, char prefix, const char *stem,
                            char names[2][32]) {
  ir_allocation *mark = c->allocations;
  snprintf(names[0], 32, "%c%s0", prefix, stem);
  unsigned hash = ir_hash(ir_name_key(c, names[0])) & 4095;
  ir_release_to(c, mark);
  for (unsigned k = 1; k < 1000000; k++) {
    snprintf(names[1], 32, "%c%s%u", prefix, stem, k);
    unsigned candidate = ir_hash(ir_name_key(c, names[1])) & 4095;
    ir_release_to(c, mark);
    if (candidate == hash)
      return;
  }
  assert(!"expected bounded hash collision");
}
static unsigned stored_index(ir_context *c, const ir_index *index,
                             const char *name) {
  const char *key =
      *name == '@' || *name == '%' ? ir_name_key(c, name) : name;
  for (unsigned k = 0; k < index->capacity; k++)
    if (index->entries[k].name && !strcmp(index->entries[k].name, key))
      return index->entries[k].value;
  abort();
}
static void typed_collisions(void) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "typed-index-collisions.ll";
  assert(!setjmp(c->failure));
  char globals[2][32], locals[2][32], labels[2][32], source[2048];
  colliding_names(c, '@', "g", globals);
  colliding_names(c, '%', "v", locals);
  colliding_names(c, '%', "b", labels);
  int size = snprintf(
      source, sizeof(source),
      TARGET "%s = global i32 1\n%s = global ptr %s\n"
             "define i32 @f(i32 %s) {\n%s:\n%s = add i32 %s, 1\n"
             "br label %s\n%s:\nret i32 %s\n}\n",
      globals[0], globals[1], globals[0], locals[0], labels[0] + 1,
      locals[1], locals[0], labels[1], labels[1] + 1, locals[1]);
  assert(size > 0 && (size_t)size < sizeof(source));
  ir_module m = {0};
  ir_analysis a;
  ir_parse(c, &m, source, (size_t)size);
  ir_analyze(c, &m, &a);
  assert(a.global_index.capacity <= 4096 &&
         a.functions[0].locals.capacity <= 4096 &&
         a.functions[0].labels.capacity <= 4096);
  assert(stored_index(c, &a.global_index, globals[0]) == 0);
  assert(stored_index(c, &a.global_index, globals[1]) == 1);
  assert(stored_index(c, &a.functions[0].locals, locals[0]) == 0);
  assert(stored_index(c, &a.functions[0].locals, locals[1]) == 1);
  assert(stored_index(c, &a.functions[0].labels, labels[0]) == 0);
  assert(stored_index(c, &a.functions[0].labels, labels[1]) == 1);
  assert(stored_index(c, &a.type_index, "i32") == 0);
  ir_write(c, &m);
  assert(!strcmp(c->output, source));
  ir_destroy(c);
  free(c);
}
static void indexes(void) {
  ir_context *c = calloc(1, sizeof(*c));
  assert(c);
  c->file = "indexes.ll";
  assert(!setjmp(c->failure));
  ir_module m = {0};
  ir_analysis a;
  ir_parse(c, &m, graph, strlen(graph));
  ir_analyze(c, &m, &a);
  assert(a.function_count == 2 && a.functions[0].declaration);
  assert(a.functions[1].parameter_count == 1 &&
         a.functions[1].block_count == 6);
  assert(a.block_count == 6 && a.successor_count == 7 &&
         a.predecessor_count == 7);
  assert(a.blocks[3].predecessor_count == 2 &&
         a.blocks[4].predecessor_count == 2);
  assert(ir_dominates(&a, 0, 5) && ir_dominates(&a, 3, 5) &&
         ir_dominates(&a, 4, 5));
  assert(!ir_dominates(&a, 1, 3) && !ir_dominates(&a, IR_NONE, 3));
  assert(!ir_dominates(&a, IR_NONE, IR_NONE));
  unsigned value = named_definition(&a, "%v"), uses = 0;
  assert(a.definitions[value].use_count == 3);
  for (unsigned k = a.definitions[value].first_use; k != IR_NONE;
       k = a.operands[k].next_use) {
    assert(a.operands[k].kind == IR_OPERAND_LOCAL &&
           a.operands[k].definition == value);
    assert(a.operands[k].type == a.definitions[value].type);
    uses++;
  }
  assert(uses == 3);
  unsigned load = a.instructions[operation(&a, "load")].effects;
  assert((load & (IR_EFFECT_READ | IR_EFFECT_VOLATILE | IR_EFFECT_TRAP)) ==
         (IR_EFFECT_READ | IR_EFFECT_VOLATILE | IR_EFFECT_TRAP));
  assert(a.instructions[operation(&a, "call")].effects & IR_EFFECT_CALL);
  assert(a.instructions[operation(&a, "alloca")].effects & IR_EFFECT_ALLOCATE);
  assert(!strcmp(
      a.types[a.instructions[operation(&a, "alloca")].allocated_type].key,
      "i32"));
  assert(a.instructions[operation(&a, "add")].effects == 0);
  assert(a.instructions[operation(&a, "br")].effects & IR_EFFECT_CONTROL);
  size_t retained = c->memory;
  ir_verify(c, &m);
  assert(c->memory == retained);
  ir_write(c, &m);
  assert(!strcmp(c->output, graph));
  ir_destroy(c);
  free(c);
}
static void atomic_cli(void) {
  const char *input = "build/m02-cli-input.ll",
             *output = "build/m02-cli-output.ll";
  FILE *f = fopen(input, "wb");
  assert(f && fwrite(graph, 1, strlen(graph), f) == strlen(graph) &&
         !fclose(f));
  char *args[] = {"badge-opt", "--verify-only", (char *)input,
                  "-o",        (char *)output,  NULL};
  assert(badge_opt_main(5, args) == 0);
  f = fopen(output, "rb");
  assert(f);
  for (size_t k = 0; k < strlen(graph); k++)
    assert(fgetc(f) == graph[k]);
  assert(fgetc(f) == EOF && !fclose(f));
  f = fopen(input, "wb");
  assert(f && fputs(TARGET FN("ret i32 %missing"), f) >= 0 && !fclose(f));
  assert(badge_opt_main(5, args) == 1);
  f = fopen(output, "rb");
  assert(f);
  for (size_t k = 0; k < strlen(graph); k++)
    assert(fgetc(f) == graph[k]);
  assert(fgetc(f) == EOF && !fclose(f));
  assert(!unlink(input) && !unlink(output));
}
int main(void) {
  indexes();
  typed_collisions();
  static const char *positive[] = {
      graph,
      TARGET FN("%v = freeze i32 undef\nret i32 %v"),
      TARGET FN("%v = sdiv exact i32 %x, 0\nret i32 %v"),
      TARGET FN("%v = shl i32 %x, 32\nret i32 %v"),
      TARGET FN("%p = inttoptr i32 %x to ptr\n%v = load i8, ptr %p, align "
                "1\n%r = zext i8 %v to i32\nret i32 %r"),
      TARGET FN(
          "%a = alloca [2 x { i8, i32 }], align 4\n"
          "%p = getelementptr [2 x { i8, i32 }], ptr %a, i32 0, i32 1, i32 1\n"
          "store i32 %x, ptr %p, align 4\nret i32 %x"),
      TARGET FN("%a = insertvalue { [2 x i32], i32 } poison, i32 %x, 0, 1\n"
                "%v = extractvalue { [2 x i32], i32 } %a, 0, 1\nret i32 %v"),
      TARGET FN("%v = add i32 %x, 1, !tag !0\nret i32 %v") "!0 = !{i32 7}\n",
      TARGET "declare i32 @g(i32, ...)\n" FN(
          "%v = call i32 (i32, ...) @g(i32 %x, i64 7)\nret i32 %v"),
      TARGET "declare i32 @g(i32)\n" FN(
          "%v = call i32 @g(i32 %x) nounwind memory(none)\nret i32 %v"),
      TARGET "define i32 @f(ptr %p, i32 %x) {\nentry:\n%v = call i32 (i32) "
             "%p(i32 %x)\nret i32 %v\n}\n",
      TARGET FN(
          "call void asm sideeffect \"nop\", \"~{memory}\"()\nret i32 %x"),
      TARGET FN("switch i32 %x, label %default [\n i32 0, label %zero\n i32 1, "
                "label %one\n]\n"
                "zero:\nret i32 3\none:\nret i32 4\ndefault:\nret i32 5"),
      TARGET FN("br i1 true, label %join, label %join\njoin:\n"
                "%v = phi i32 [ %x, %entry ], [ %x, %entry ]\nret i32 %v"),
      TARGET FN(
          "br i1 true, label %join, label %join\njoin:\n"
          "%v = phi i32 [ -1, %entry ], [ 4294967295, %entry ]\nret i32 %v"),
      TARGET
      "@pointer = global ptr @data\n@data = global i32 7\n" FN("ret i32 %x"),
      TARGET "@pointer = global <{ptr}> <{ptr @f}>\n" FN("ret i32 %x"),
      TARGET FN(
          "br label %join\ndead:\n%v = add i32 %x, 1\nbr label %join\n"
          "join:\n%p = phi i32 [ %x, %entry ], [ %v, %dead ]\nret i32 %p"),
      TARGET
      "define void @f(ptr sret([8 x i8]) align 4 %out, [3 x i32] %in) {\n"
      "entry:\nstore [3 x i32] %in, ptr %out, align 4\nret void\n}\n",
      TARGET FN("%\"\\61\" = add i32 %x, 1\nret i32 %a"),
  };
  for (unsigned k = 0; k < sizeof(positive) / sizeof(*positive); k++)
    fixture(positive[k], 1);
  static const char *negative[] = {
      TARGET FN("ret i32 %missing"),
      TARGET FN("%a = add i32 %later, 1\n%later = add i32 %x, 1\nret i32 %a"),
      TARGET FN("%x = add i32 1, 2\nret i32 %x"),
      TARGET FN("%a = add i32 %x, 1\n%\"\\61\" = add i32 %x, 2\nret i32 %a"),
      TARGET FN("br label %missing"),
      TARGET FN("br label %a\na:\nret i32 0\na:\nret i32 1"),
      TARGET FN("br label %entry"),
      TARGET FN("%entry = add i32 %x, 1\nret i32 %entry"),
      TARGET FN("%a = add i32 %x, 1"),
      TARGET FN("ret i32 %x\n%a = add i32 %x, 1"),
      TARGET FN("ret i32 %x\nret i32 0"),
      TARGET FN("br label %empty\nempty:"),
      TARGET FN("%a = trunc i32 %x to i8\n%b = add i32 %a, 1\nret i32 %b"),
      TARGET FN("ret i8 0"),
      TARGET FN("%a = zext i32 %x to i8\nret i32 %x"),
      TARGET FN("%a = trunc i32 %x to i64\nret i32 %x"),
      TARGET FN("%a = bitcast i32 %x to i64\nret i32 %x"),
      TARGET FN("%a = inttoptr ptr null to ptr\nret i32 %x"),
      TARGET FN("%a = add ptr null, null\nret i32 %x"),
      TARGET FN("%a = add i8 256, 0\nret i32 %x"),
      TARGET FN("%a = add i32 %x, 1, align 4\nret i32 %a"),
      TARGET FN("%a = add exact i32 %x, 1\nret i32 %a"),
      TARGET FN("%a = add nsw nsw i32 %x, 1\nret i32 %a"),
      TARGET FN("%a = icmp invalid i32 %x, 0\nret i32 %x"),
      TARGET FN("%a = select i32 %x, i32 1, i32 2\nret i32 %a"),
      TARGET FN("%a = select i1 true, i32 1, i8 2\nret i32 %a"),
      TARGET FN("%a = load i32, i32 %x\nret i32 %a"),
      TARGET FN("%a = load i32, ptr null, align 3\nret i32 %a"),
      TARGET FN("store i32 %x, ptr @missing\nret i32 %x"),
      TARGET "@pointer = global ptr @missing\n" FN("ret i32 %x"),
      TARGET "@data = global void 0\n" FN("ret i32 %x"),
      TARGET FN("%a = extractvalue [2 x i32] poison, 2\nret i32 %a"),
      TARGET FN("%a = insertvalue [2 x i32] poison, i8 0, 0\nret i32 %x"),
      TARGET FN("%a = getelementptr i32, ptr null, i32 0, i32 1\nret i32 %x"),
      TARGET FN(
          "%a = getelementptr {i32}, ptr null, i32 0, i32 %x\nret i32 %x"),
      TARGET FN("br i32 %x, label %left, label %right\nleft:\nret i32 "
                "0\nright:\nret i32 1"),
      TARGET FN("switch i32 %x, label %last [i32 1, label %last i32 1, label "
                "%last]\nlast:\nret i32 %x"),
      TARGET FN(
          "switch i32 %x, label %last [i8 0, label %last]\nlast:\nret i32 %x"),
      TARGET FN("br i1 true, label %left, label %right\nleft:\n%a = add i32 "
                "%x, 1\nbr label %last\n"
                "right:\nbr label %last\nlast:\nret i32 %a"),
      TARGET FN("br i1 true, label %left, label %right\nleft:\n%a = add i32 "
                "%x, 1\nbr label %last\n"
                "right:\nbr label %last\nlast:\n%p = phi i32 [%a, %right], "
                "[%x, %left]\nret i32 %p"),
      TARGET FN("br label %last\nlast:\n%a = add i32 %x, 1\n%p = phi i32 [%x, "
                "%entry]\nret i32 %p"),
      TARGET FN("br label %last\nlast:\n%p = phi i32 [%x, %last]\nret i32 %p"),
      TARGET FN("br i1 true, label %last, label %last\nlast:\n%p = phi i32 "
                "[%x, %entry]\nret i32 %p"),
      TARGET FN("br i1 true, label %last, label %last\nlast:\n"
                "%p = phi i32 [1, %entry], [2, %entry]\nret i32 %p"),
      TARGET "declare i32 @g(i8)\n" FN("%v = call i32 @g(i32 %x)\nret i32 %v"),
      TARGET "declare i32 @g(i32)\n" FN("%v = call i64 @g(i32 %x)\nret i32 %x"),
      TARGET "declare i32 @g(i32)\n" FN("%v = call i32 @g()\nret i32 %v"),
      TARGET "declare i32 @g(i32)\n" FN(
          "%v = call i32 @g(i32 %x, i32 %x)\nret i32 %v"),
      TARGET "@g = global i32 0\n" FN("%v = call i32 @g(i32 %x)\nret i32 %v"),
      TARGET FN("%v = call i32 @missing(i32 %x)\nret i32 %v"),
      TARGET "define i32 @f(ptr %p) {\nentry:\n%v = call i32 (i32) %p()\nret "
             "i32 %v\n}\n",
      TARGET FN("%a = call void asm sideeffect \"nop\", \"\"()\nret i32 %x"),
      TARGET FN("call i32 asm \"\", \"=r\"()\nret i32 %missing"),
      TARGET FN("%a = fadd i32 %x, 1\nret i32 %a"),
      TARGET FN("%a = add i32 %x, 1, !tag\nret i32 %a"),
  };
  for (unsigned k = 0; k < sizeof(negative) / sizeof(*negative); k++)
    fixture(negative[k], 0);
  const char seed[] = TARGET FN("%a = add i32 %x, 1\nret i32 %a");
  for (size_t k = 0; k < sizeof(seed) - 1; k++) {
    char mutated[sizeof(seed)];
    memcpy(mutated, seed, sizeof(seed));
    mutated[k] ^= 64;
    ir_context *c = calloc(1, sizeof(*c));
    assert(c);
    c->file = "analysis-mutation.ll";
    if (!setjmp(c->failure)) {
      ir_module m = {0};
      ir_parse(c, &m, mutated, strlen(mutated));
      ir_verify(c, &m);
    }
    ir_destroy(c);
    free(c);
  }
  char nested[4096] = TARGET "define void @f() {\nentry:\n%a = alloca ";
  for (unsigned k = 0; k < 33; k++)
    strcat(nested, "[1 x ");
  strcat(nested, "i8");
  for (unsigned k = 0; k < 33; k++)
    strcat(nested, "]");
  strcat(nested, "\nret void\n}\n");
  fixture(nested, 0);
  ir_context *limited = calloc(1, sizeof(*limited));
  assert(limited);
  limited->file = "allocation-limit.ll";
  if (!setjmp(limited->failure)) {
    ir_allocate(limited, 128u * 1024u * 1024u + 1);
    assert(!"allocation limit must be diagnosed");
  }
  ir_destroy(limited);
  free(limited);
  atomic_cli();
  puts("M02: typed views, indexed defs/uses/CFG, effects, dominance/PHIs, "
       "opaque text, malformed IR, mutations and atomic verify-only PASS");
  return 0;
}
