#define _POSIX_C_SOURCE 200809L
#include "host_io.h"
#include "ir_pass.h"
#include "target_cost.h"
#include <inttypes.h>

typedef struct {
  unsigned step;
  char pass[64], event[32], reason[96];
  uint64_t count;
} observation;
typedef struct {
  ir_context *ir, *document;
  observation rows[512];
  unsigned count;
  int target_costs;
  unsigned target_work;
} metrics;

static void cost_work(metrics *m) {
  if (++m->target_work > BADGE_TARGET_COST_WORK_LIMIT)
    ir_error(m->ir, "target-cost analysis work limit exceeded");
}
static badge_cost_kind instruction_kind(metrics *state, const ir_module *m,
                                       const ir_line *line) {
  const char *op = ir_text(m, line->op);
  if (!strcmp(op, "br"))
    return BADGE_COST_BRANCH;
  unsigned bits = 0;
  for (unsigned k = line->op + 1; k < line->first + line->count; k++) {
    cost_work(state);
    bits = ir_bits(ir_text(m, k));
    if (bits)
      break;
  }
  if (bits && bits <= 32) {
    if (!strcmp(op, "load")) return BADGE_COST_LOAD;
    if (!strcmp(op, "store")) return BADGE_COST_STORE;
  }
  if (bits != 32)
    return BADGE_COST_UNKNOWN;
  if (!strcmp(op, "mul")) return BADGE_COST_MUL;
  if (!strcmp(op, "sdiv") || !strcmp(op, "udiv") ||
      !strcmp(op, "srem") || !strcmp(op, "urem"))
    return BADGE_COST_DIVIDE;
  if (!strcmp(op, "add") || !strcmp(op, "sub") || !strcmp(op, "and") ||
      !strcmp(op, "or") || !strcmp(op, "xor") || !strcmp(op, "icmp") ||
      !strcmp(op, "shl") || !strcmp(op, "lshr") || !strcmp(op, "ashr"))
    return BADGE_COST_ALU;
  return BADGE_COST_UNKNOWN;
}
static void range(ir_context *out, badge_cost_range r) {
  if (r.known)
    ir_emit(out, "{\"min\":%u,\"max\":%u}", r.min, r.max);
  else
    ir_append(out, "null", 4);
}
static void cost_rows(ir_context *out, const uint64_t *counts) {
  ir_append(out, ",\"target_costs\":[", 17);
  unsigned emitted = 0;
  for (unsigned kind = 0; kind < BADGE_COST_KIND_COUNT; kind++) {
    if (!counts[kind])
      continue;
    const badge_cost_rule *rule = &badge_cost_rules[kind];
    badge_cost_estimate estimate =
        badge_target_cost((badge_cost_request){.kind = kind});
    if (emitted++) ir_append(out, ",", 1);
    ir_emit(out, "{\"rule\":\"%s\",\"fact\":\"%s\",\"experiment\":\"%s\","
                 "\"condition\":\"%s\",\"occurrences\":%" PRIu64 ",\"status\":\"%s\","
                 "\"single_selected_instruction_baseline_issue\":",
            rule->name, rule->fact, rule->experiment, rule->condition,
            counts[kind], estimate.status);
    range(out, estimate.baseline_issue);
    ir_append(out, ",\"instruction_bytes\":", 21);
    range(out, estimate.code_bytes);
    ir_append(out, ",\"result_ready\":", 16);
    range(out, estimate.result_ready);
    ir_append(out, ",\"resource_occupancy\":", 22);
    range(out, estimate.resource_occupancy);
    ir_emit(out, ",\"resources\":%u,\"memory_wait_unknown\":%s,"
                 "\"extra_temporaries\":null}",
            estimate.resources, estimate.memory_wait_unknown ? "true" : "false");
  }
  ir_append(out, "]", 1);
}

static void quote(ir_context *c, const char *text) {
  ir_append(c, "\"", 1);
  for (; *text; text++) {
    unsigned char ch = (unsigned char)*text;
    if (ch == '"' || ch == '\\')
      ir_emit(c, "\\%c", ch);
    else if (ch < 32 || ch >= 127)
      ir_emit(c, "\\u%04x", ch);
    else
      ir_append(c, text, 1);
  }
  ir_append(c, "\"", 1);
}
static void observe(void *data, unsigned step, const char *pass,
                    const char *event, const char *reason, uint64_t count) {
  metrics *m = data;
  for (unsigned k = 0; k < m->count; k++) {
    observation *r = &m->rows[k];
    if (r->step == step && !strcmp(r->pass, pass) && !strcmp(r->event, event) &&
        !strcmp(r->reason, reason)) {
      if (count > UINT64_MAX - r->count)
        ir_error(m->ir, "metrics counter overflow");
      r->count += count;
      return;
    }
  }
  if (m->count == 512 || strlen(pass) >= sizeof(m->rows[0].pass) ||
      strlen(event) >= sizeof(m->rows[0].event) ||
      strlen(reason) >= sizeof(m->rows[0].reason))
    ir_error(m->ir, "metrics observation limit exceeded");
  observation *r = &m->rows[m->count++];
  r->step = step;
  strcpy(r->pass, pass);
  strcpy(r->event, event);
  strcpy(r->reason, reason);
  r->count = count;
}
static void functions(metrics *state, ir_context *c, ir_context *out,
                      const char *source, size_t size) {
  ir_module m = {0};
  ir_parse(c, &m, source, size);
  ir_append(out, "[", 1);
  unsigned emitted = 0;
  for (unsigned l = 0; l < m.line_count; l++) {
    ir_line *header = &m.lines[l];
    if (!header->count || strcmp(ir_text(&m, header->first), "define"))
      continue;
    const char *name = NULL;
    for (unsigned k = header->first; k < header->first + header->count; k++)
      if (*ir_text(&m, k) == '@') {
        name = ir_text(&m, k);
        break;
      }
    if (!name)
      ir_error(c, "metrics requires a named function");
    const char *opcodes[128];
    uint64_t counts[128] = {0}, instructions = 0, volatile_loads = 0,
             volatile_stores = 0;
    unsigned count = 0;
    uint64_t cost_counts[BADGE_COST_KIND_COUNT] = {0};
    while (++l < m.line_count) {
      ir_line *line = &m.lines[l];
      if (line->count && !strcmp(ir_text(&m, line->first), "}"))
        break;
      if (!line->instruction)
        continue;
      const char *op = ir_text(&m, line->op);
      unsigned k = 0;
      while (k < count && strcmp(opcodes[k], op))
        k++;
      if (k == count) {
        if (count == 128)
          ir_error(c, "metrics opcode limit exceeded");
        opcodes[count++] = op;
      }
      counts[k]++;
      instructions++;
      if (state->target_costs) {
        cost_work(state);
        cost_counts[instruction_kind(state, &m, line)]++;
      }
      if (!strcmp(ir_text(&m, line->op + 1), "volatile")) {
        volatile_loads += !strcmp(op, "load");
        volatile_stores += !strcmp(op, "store");
      }
    }
    if (emitted++)
      ir_append(out, ",", 1);
    ir_append(out, "{\"function\":", 12);
    quote(out, name);
    ir_emit(out,
            ",\"instructions\":%" PRIu64 ",\"volatile_loads\":%" PRIu64
            ",\"volatile_stores\":%" PRIu64 ",\"opcodes\":{",
            instructions, volatile_loads, volatile_stores);
    for (unsigned k = 0; k < count; k++) {
      if (k)
        ir_append(out, ",", 1);
      quote(out, opcodes[k]);
      ir_emit(out, ":%" PRIu64, counts[k]);
    }
    ir_append(out, "}", 1);
    if (state->target_costs)
      cost_rows(out, cost_counts);
    ir_append(out, "}", 1);
  }
  ir_append(out, "]", 1);
}
static int analyze(metrics *m, const char *source, size_t size,
                   const char *selection, const char *checked, const char *emitted) {
  ir_context *c = m->ir, *out = m->document;
  if (setjmp(c->failure))
    return 0;
  if (setjmp(out->failure))
    return 0;
  ir_append(out, "{\"schema\":1,\"before\":", 21);
  functions(m, c, out, source, size);
  ir_destroy(c);
  memset(c, 0, sizeof(*c));
  c->file = out->file;
  if (setjmp(c->failure))
    return 0;
  c->observe = observe;
  c->observation_context = m;
  ir_pipeline(c, source, size, selection);
  if (checked) {
    FILE *f = fopen(checked, "rb");
    if (!f)
      ir_error(c, "cannot open checked IR");
    int equal = 1;
    for (size_t k = 0; k < c->size; k++)
      if (fgetc(f) != (unsigned char)c->output[k]) {
        equal = 0;
        break;
      }
    if (fgetc(f) != EOF || ferror(f))
      equal = 0;
    if (fclose(f))
      equal = 0;
    if (!equal)
      ir_error(c, "metrics pipeline differs from checked backend-ready IR");
  }
  if (emitted && !host_write_file("badge-ir-metrics", emitted,
                                 (unsigned char *)c->output, c->size))
    ir_error(c, "cannot write measured backend-ready IR");
  ir_append(out, ",\"after\":", 9);
  functions(m, c, out, c->output, c->size);
  ir_append(out, ",\"events\":[", 11);
  for (unsigned k = 0; k < m->count; k++) {
    observation *r = &m->rows[k];
    if (k)
      ir_append(out, ",", 1);
    ir_emit(out, "{\"step\":%u,\"pass\":", r->step);
    quote(out, r->pass);
    ir_append(out, ",\"event\":", 9);
    quote(out, r->event);
    ir_append(out, ",\"reason\":", 10);
    quote(out, r->reason);
    ir_emit(out, ",\"count\":%" PRIu64 "}", r->count);
  }
  ir_append(out, "]", 1);
  if (m->target_costs)
    ir_emit(out, ",\"target_model\":{\"version\":%u,\"stage\":\"IR-conservative\","
                 "\"runtime_prediction\":false,\"work\":%u,\"work_limit\":%u,"
                 "\"limits\":\"occurrences are not machine instructions; "
                 "memory waits, exact operands/layout and allocation remain unknown\"}",
            BADGE_TARGET_COST_VERSION, m->target_work, BADGE_TARGET_COST_WORK_LIMIT);
  ir_append(out, "}\n", 2);
  return 1;
}
int main(int argc, char **argv) {
  const char *selection = NULL, *checked = NULL, *emitted = NULL;
  int target_costs = 0;
  while (argc > 4) {
    if (!selection && !strncmp(argv[1], "--passes=", 9))
      selection = argv[1] + 9;
    else if (!checked && !strncmp(argv[1], "--check=", 8))
      checked = argv[1] + 8;
    else if (!emitted && !strncmp(argv[1], "--emit-ir=", 10))
      emitted = argv[1] + 10;
    else if (!target_costs && !strcmp(argv[1], "--target-costs"))
      target_costs = 1;
    else
      break;
    argc--;
    argv++;
  }
  if (argc != 4 || strcmp(argv[2], "-o")) {
    fputs("usage: badge-ir-metrics [--passes=name,...] [--check=ready.ll] [--emit-ir=ready.ll] [--target-costs] "
          "input.ll -o report.json\n",
          stderr);
    return 2;
  }
  struct stat a, b;
  if (stat(argv[1], &a) || !S_ISREG(a.st_mode) || a.st_size < 0 ||
      a.st_size > 16 * 1024 * 1024) {
    fputs("badge-ir-metrics: input must be a regular file within 16 MiB\n",
          stderr);
    return 1;
  }
  if (!stat(argv[3], &b)) {
    if (a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
      fputs("badge-ir-metrics: output aliases input\n", stderr);
      return 1;
    }
  } else if (errno != ENOENT) {
    fputs("badge-ir-metrics: cannot inspect output\n", stderr);
    return 1;
  }
  if (checked) {
    struct stat check;
    if (stat(checked, &check) || !S_ISREG(check.st_mode) || check.st_size < 0 ||
        check.st_size > 16 * 1024 * 1024) {
      fputs(
          "badge-ir-metrics: checked IR must be a regular file within 16 MiB\n",
          stderr);
      return 1;
    }
    if (!stat(argv[3], &b) && b.st_dev == check.st_dev &&
        b.st_ino == check.st_ino) {
      fputs("badge-ir-metrics: output aliases checked IR\n", stderr);
      return 1;
    }
  }
    if (emitted) {
      struct stat output, report, check;
      int exists = stat(emitted, &output) == 0;
      if ((!exists && errno != ENOENT) ||
          (exists && (output.st_dev == a.st_dev && output.st_ino == a.st_ino)) ||
          !strcmp(emitted, argv[3]) ||
          (exists && !stat(argv[3], &report) && output.st_dev == report.st_dev &&
           output.st_ino == report.st_ino) ||
          (checked && (!strcmp(emitted, checked) ||
           (exists && !stat(checked, &check) && output.st_dev == check.st_dev &&
            output.st_ino == check.st_ino)))) {
        fputs("badge-ir-metrics: emitted IR aliases an input/report or cannot be inspected\n", stderr);
        return 1;
      }
    }
  metrics *m = calloc(1, sizeof(*m));
  ir_context *c = calloc(1, sizeof(*c)), *out = calloc(1, sizeof(*out));
  size_t size = (size_t)a.st_size;
  char *source = malloc(size ? size : 1);
  FILE *f = fopen(argv[1], "rb");
  if (!m || !c || !out || !source || !f) {
    fputs("badge-ir-metrics: cannot allocate/open input\n", stderr);
    free(m);
    free(c);
    free(out);
    free(source);
    if (f)
      fclose(f);
    return 1;
  }
  int ok = fread(source, 1, size, f) == size;
  if (fgetc(f) != EOF || ferror(f))
    ok = 0;
  if (fclose(f))
    ok = 0;
  if (!ok)
    fputs("badge-ir-metrics: input read failed\n", stderr);
  c->file = out->file = argv[1];
  m->ir = c;
  m->document = out;
  m->target_costs = target_costs;
  if (ok)
    ok = analyze(m, source, size, selection, checked, emitted);
  if (ok)
    ok = host_write_file("badge-ir-metrics", argv[3],
                         (unsigned char *)out->output, out->size);
  ir_destroy(c);
  ir_destroy(out);
  free(c);
  free(out);
  free(source);
  free(m);
  return ok ? 0 : 1;
}
