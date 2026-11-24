#include "codegen_internal.h"
#include "target_cost.h"
#include "thumb_immediate.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int known;
  uint32_t number;
  char *symbol;
  int64_t addend;
} cg_constant;

static char *line(char *text) {
  char *end = strchr(text, '\n');
  if (end) *end = 0;
  return end ? end + 1 : text + strlen(text);
}
static const char *first_nonspace(const char *text) {
  while (*text != '\n' && isspace((unsigned char)*text))
    text++;
  return text;
}
static int opcode_prefix(const char *p, const char *opcode, size_t n) {
  return !strncmp(p, opcode, n) && isspace((unsigned char)p[n]);
}
static int immediate(const char *text, const char *op, unsigned *reg,
                     uint32_t *number) {
  const char *p = first_nonspace(text);
  int end = 0;
  if (!opcode_prefix(p, op, 4))
    return 0;
  return sscanf(p + 4, " r%u, #%" SCNu32 "%n", reg, number, &end) == 2 &&
         !p[4 + end] && *reg <= 12 && *number <= 65535;
}
static char *symbol(const char *text, const char *op, unsigned *reg) {
  const char *p = first_nonspace(text);
  char part[16];
  int start = 0;
  if (!opcode_prefix(p, op, 4) ||
      sscanf(p + 4, " r%u, #:%15[^:]:%n", reg, part, &start) != 2 ||
      *reg > 12 ||
      strcmp(part, !strcmp(op, "movw") ? "lower16" : "upper16") ||
      p[4 + start] != '(')
    return NULL;
  return (char *)p + 4 + start + 1;
}
static int same_base(cg_constant a, cg_constant b) {
  return a.known && b.known && ((!a.symbol && !b.symbol) ||
         (a.symbol && b.symbol && !strcmp(a.symbol, b.symbol)));
}
static int64_t value(cg_constant fact) {
  return fact.symbol ? fact.addend : (int64_t)fact.number;
}
static unsigned reuse(cg_context *c, cg_constant facts[13], unsigned dst,
                      cg_constant next, unsigned old_bytes) {
  unsigned best = 13, bytes = old_bytes;
  int64_t best_delta = 0;
  for (unsigned src = 0; src <= 12; src++) {
    cg_register_work(c);
    if (!same_base(facts[src], next)) continue;
    int64_t delta = value(next) - value(facts[src]);
    unsigned candidate = 0;
    if (!delta) candidate = src == dst ? 0 : 2;
    else if ((delta >= -4095 && delta <= 4095) ||
             c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS) {
      badge_cost_range cost = badge_target_add_bytes(dst, src,
          (unsigned)(delta < 0 ? -delta : delta), 0, 0);
      if (cost.known) candidate = cost.max;
      else continue;
    } else continue;
    if (candidate < bytes) {
      bytes = candidate;
      best = src;
      best_delta = delta;
    }
  }
  if (best == 13) return 0;
  if (!best_delta) {
    if (best != dst) cg_emit(c, "mov r%u, r%u\n", dst, best);
  } else
    cg_emit(c, "%s r%u, r%u, #%" PRId64 "\n",
            best_delta < -4095 ? "sub.w" : best_delta > 4095 ? "add.w" :
            best_delta < 0 ? "subw" : "addw",
            dst, best, best_delta < 0 ? -best_delta : best_delta);
  cg_emit(c, "@ constant-reuse: saved %u\n", old_bytes - bytes);
  return 1;
}
static void effects(const char *text, cg_constant facts[13]) {
  char op[24];
  unsigned dst = 0, src = 0;
  int end = 0;
  if (!*text || (text[0] == '@' && strncmp(text, "@ frame-incoming:", 17))) return;
  if (!strcmp(text, "nop")) return;
  if (strchr(text, '!') || sscanf(text, "%23s", op) != 1) {
    memset(facts, 0, 13 * sizeof(*facts));
    return;
  }
  if (sscanf(text, "mov r%u, r%u%n", &dst, &src, &end) == 2 &&
      !text[end] && dst <= 12 && src <= 12) {
    facts[dst] = facts[src];
    return;
  }
  if ((!strcmp(op, "ldrd") || !strcmp(op, "strd")) &&
      !strstr(text, "],") &&
      sscanf(text + strlen(op), " r%u, r%u, [", &dst, &src) == 2 &&
      dst <= 12 && src <= 12) {
    if (op[0] == 'l')
      facts[dst].known = facts[src].known = 0;
    return;
  }
  if (!strcmp(op, "str") || !strcmp(op, "str.w") || !strcmp(op, "str.n") ||
      !strcmp(op, "strb") || !strcmp(op, "strb.w") || !strcmp(op, "strb.n") ||
      !strcmp(op, "strh") || !strcmp(op, "strh.w") || !strcmp(op, "strh.n") ||
      !strcmp(op, "cmp") || !strcmp(op, "cmp.w") || !strcmp(op, "cmp.n") ||
      !strcmp(op, "tst.w") || !strcmp(op, "tst.n") || !strcmp(op, "cmn.w")) return;
  const char *writes[] = {"mov", "mov.w", "movs.n", "movw", "movt", "orn.w", "add.w", "add.n", "addw",
    "sub.w", "subw", "subs.w", "adds.n", "subs.n", "mul", "mla", "mls", "udiv", "sdiv",
    "and.w", "bic.w", "orr.w", "eor.w", "lsl.w", "lsr.w", "asr.w", "lsls.n", "lsrs.n", "asrs.n", "rsb.w",
    "ubfx", "clz", "sxtb", "sxth", "sxtb.n", "sxtb.w", "sxth.n", "sxth.w",
    "uxtb.n", "uxtb.w", "uxth.n", "uxth.w",
    "ldr", "ldr.w", "ldr.n", "ldrb", "ldrb.w", "ldrb.n", "ldrh", "ldrh.w", "ldrh.n",
    "ldrsb", "ldrsh", "ldrsb.n", "ldrsh.n", "ldrsb.w", "ldrsh.w"};
  for (unsigned n = 0; n < sizeof(writes) / sizeof(*writes); n++)
    if (!strcmp(op, writes[n]) && sscanf(text + strlen(op), " r%u,", &dst) == 1 &&
        dst <= 12) {
      facts[dst].known = 0;
      return;
    }
  /* Labels, IT, calls, directives and unknown effects end availability. */
  memset(facts, 0, 13 * sizeof(*facts));
}
static const char *find_label(cg_context *c, const char *body, const char *name);
static int sole_label_entry(cg_context *c, const char *body, const char *name,
                            const char *label, const char *jump);
static int label_character(unsigned char ch);
typedef struct {
  const char *name, *definition;
  size_t length;
  unsigned mentions;
} emitted_label;
typedef struct {
  emitted_label *slots;
  unsigned capacity;
} emitted_labels;
static emitted_label *emitted_lookup(cg_context *c, emitted_labels labels,
                                     const char *name, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t k = 0; k < length; ++k)
    hash = (hash ^ (unsigned char)name[k]) * 16777619u;
  unsigned slot = hash & (labels.capacity - 1);
  while (labels.slots[slot].name &&
         (labels.slots[slot].length != length || memcmp(labels.slots[slot].name, name, length))) {
    cg_register_work(c);
    slot = (slot + 1) & (labels.capacity - 1);
  }
  return &labels.slots[slot];
}
static emitted_labels index_emitted_labels(cg_context *c, const char *body, int mentions) {
  unsigned capacity = 8, count = 0;
  for (const char *p = body; (p = strstr(p, "\n.Lcg_")); ++p) {
    cg_register_work(c);
    count++;
  }
  while (capacity < 2 * (count + 1)) capacity *= 2;
  emitted_labels index = {cg_alloc(c, capacity * sizeof(emitted_label)), capacity};
  unsigned assembly_lines = 0;
  for (const char *p = body; *p;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end) cg_error(c, "unterminated instruction in emitted label index");
    if (assembly_lines) assembly_lines--;
    else if (!strncmp(p, "@ constant-assembly-lines:", 26))
      sscanf(p, "@ constant-assembly-lines: %u", &assembly_lines);
    else if (!strncmp(p, ".Lcg_", 5) && end[-1] == ':') {
      size_t length = (size_t)(end - p - 1);
      emitted_label *slot = emitted_lookup(c, index, p, length);
      if (!slot->name) *slot = (emitted_label){p, p, length, 0};
    }
    p = end + 1;
  }
  if (mentions)
    for (const char *p = body; (p = strstr(p, ".Lcg_"));) {
      cg_register_work(c);
      const char *end = p + 5;
      while (label_character((unsigned char)*end)) ++end;
      if (p == body || !label_character((unsigned char)p[-1])) {
        emitted_label *slot = emitted_lookup(c, index, p, (size_t)(end - p));
        if (slot->name) slot->mentions++;
      }
      p = end;
    }
  return index;
}
static int flags_overwritten(cg_context *c, const char *body, const char *text,
                             const emitted_labels *labels) {
  /* Follow only neutral instructions and unconditional edges to an NZCV kill. */
  unsigned instructions = 0;
  for (unsigned lines = 0; lines < 64 && instructions < 8 && *text; lines++) {
    cg_register_work(c);
    const char *end = strchr(text, '\n');
    size_t length = end ? (size_t)(end - text) : strlen(text);
    if (length >= 128) return 0;
    char instruction[128];
    memcpy(instruction, text, length);
    instruction[length] = 0;
    if (!strncmp(instruction, "@ assembly-effects:", 19) ||
        !strncmp(instruction, "@ constant-assembly-lines:", 26) ||
        !strncmp(instruction, "@ frame-incoming:", 17)) return 0;
    if (*instruction == '@' || !*instruction ||
        (!strncmp(instruction, ".Lcg_", 5) && length && instruction[length - 1] == ':')) {
      if (!end) return 0;
      text = end + 1;
      continue;
    }
    char opcode[24];
    if (sscanf(instruction, "%23s", opcode) != 1) return 0;
    /* AAPCS calls do not consume incoming NZCV. */
    if (!strcmp(opcode, "bl") || !strcmp(opcode, "cmp") ||
        !strcmp(opcode, "cmp.n") || !strcmp(opcode, "cmp.w") || !strcmp(opcode, "cmn.w") ||
        !strcmp(opcode, "adds.n") || !strcmp(opcode, "adds.w") ||
        !strcmp(opcode, "subs.n") || !strcmp(opcode, "subs.w")) return 1;
    instructions++;
    if (!strcmp(opcode, "b") || !strcmp(opcode, "b.n") || !strcmp(opcode, "b.w")) {
      char target[128];
      int consumed = 0;
      if (sscanf(instruction, "%23s %127s%n", opcode, target, &consumed) != 2 ||
          instruction[consumed] || strncmp(target, ".Lcg_", 5))
        return 0;
      text = labels ? emitted_lookup(c, *labels, target, strlen(target))->definition
                    : find_label(c, body, target);
      if (!text) return 0;
      continue;
    }
    unsigned reg, source;
    uint32_t number;
    int consumed = 0;
    int neutral = 0;
    const char *ops[] = {"mov.w", "addw", "subw", "add.w", "sub.w",
      "str", "str.n", "str.w", "strb", "strb.n", "strb.w", "strh", "strh.n", "strh.w",
      "ldr", "ldr.n", "ldr.w", "ldrb", "ldrb.n", "ldrb.w", "ldrh", "ldrh.n", "ldrh.w",
      "ubfx", "clz", "uxtb.n", "uxtb.w", "uxth.n", "uxth.w", "and.w", "bic.w", "orr.w", "orn.w",
      "eor.w", "lsl.w", "lsr.w", "asr.w", "nop", "udiv", "sdiv", "mls", "mla",
      "movs.n"};
    for (unsigned n = 0; n < sizeof(ops) / sizeof(*ops); n++)
      if (!strcmp(opcode, ops[n])) neutral = 1;
    if (!neutral && !immediate(instruction, "movw", &reg, &number) &&
        !immediate(instruction, "movt", &reg, &number) &&
        !symbol(instruction, "movw", &reg) && !symbol(instruction, "movt", &reg) &&
        !(sscanf(instruction, "mov r%u, r%u%n", &reg, &source, &consumed) == 2 &&
          !instruction[consumed] && reg <= 12 && source <= 12))
      return 0;
    if (!end) return 0;
    text = end + 1;
  }
  return 0;
}
int cg_flags_overwritten(cg_context *c, const char *body, const char *text) {
  return flags_overwritten(c, body, text, NULL);
}
static const char *find_label(cg_context *c, const char *body, const char *name) {
  size_t length = strlen(name);
  unsigned assembly_lines = 0;
  for (const char *p = body; *p;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end) return NULL;
    if (assembly_lines) assembly_lines--;
    else if (!strncmp(p, "@ constant-assembly-lines:", 26))
      sscanf(p, "@ constant-assembly-lines: %u", &assembly_lines);
    else if ((size_t)(end - p) == length + 1 && !memcmp(p, name, length) &&
             p[length] == ':') return p;
    p = end + 1;
  }
  return NULL;
}
static int branch_span(cg_context *c, const char *from, const char *to, unsigned limit) {
  if (from > to) {
    const char *swap = from;
    from = to;
    to = swap;
  }
  unsigned bytes = 0;
  while (from < to) {
    cg_register_work(c);
    const char *end = strchr(from, '\n');
    if (!end || !strncmp(from, "@ constant-assembly-lines:", 26) ||
        !strncmp(from, "@ frame-incoming:", 17))
      return 0;
    if (*from && *from != '@' && *from != '\n') {
      if (*from == '.') {
        if (strncmp(from, ".Lcg_", 5) || end[-1] != ':') return 0;
      } else bytes += 4;
    }
    if (bytes > limit) return 0;
    from = end + 1;
  }
  return 1;
}
static const char *edge_body(const char *line) {
  while (*line == '@' || !strncmp(line, ".Lcg_edge", 9)) {
    const char *end = strchr(line, '\n');
    if (!end || (*line == '.' && end[-1] != ':'))
      break;
    line = end + 1;
  }
  return line;
}

static const char *only_label(const char *line, char label[128]) {
  const char *end = strchr(line, '\n');
  if (strncmp(line, ".Lcg_", 5) || !end || end - line <= 1 ||
      end[-1] != ':' || end - line > 128)
    return NULL;
  size_t n = (size_t)(end - line - 1);
  memcpy(label, line, n);
  label[n] = 0;
  return end + 1;
}

static const char *fold_unsigned_distance(cg_context *c, const char *body,
                                          const char *p, const char *end) {
  unsigned a, b, dst, x, y;
  int used = 0;
  if (strncmp(p, "cmp.n ", 6) ||
      sscanf(p, "cmp.n r%u, r%u%n", &a, &b, &used) != 2 || p + used != end ||
      a >= 8 || b >= 8)
    return NULL;
  const char *branch = end + 1, *branch_end = strchr(branch, '\n');
  char target[128], join[128], label[128];
  used = 0;
  if (!branch_end || sscanf(branch, "bls.n %127s%n", target, &used) != 1 ||
      branch + used != branch_end)
    return NULL;
  const char *first_label = edge_body(branch_end + 1);
  const char *first = only_label(first_label, label);
  if (!first || !sole_label_entry(c, body, label, first_label, NULL))
    return NULL;
  for (const char *q = branch_end + 1; q < first_label;) {
    if (*q == '.' && (!only_label(q, label) ||
                     !sole_label_entry(c, body, label, q, NULL)))
      return NULL;
    q = strchr(q, '\n') + 1;
  }
  const char *sub_end = strchr(first, '\n');
  used = 0;
  if (!sub_end || sscanf(first, "sub.w r%u, r%u, r%u%n", &dst, &x, &y, &used) != 3 ||
      first + used != sub_end || dst >= 8 || x != a || y != b)
    return NULL;
  const char *jump = sub_end + 1, *jump_end = strchr(jump, '\n');
  used = 0;
  if (!jump_end || sscanf(jump, "b %127s%n", join, &used) != 1 || jump + used != jump_end)
    return NULL;
  const char *other = jump_end + 1, *second = only_label(other, label);
  if (!second || strcmp(label, target) ||
      !sole_label_entry(c, body, label, other, branch + 6))
    return NULL;
  const char *second_end = strchr(second, '\n');
  unsigned result;
  used = 0;
  if (!second_end || sscanf(second, "sub.w r%u, r%u, r%u%n", &result, &x, &y, &used) != 3 ||
      second + used != second_end || result != dst || x != b || y != a ||
      !only_label(second_end + 1, label) || strcmp(label, join))
    return NULL;
  /* SUBS sets the same NZCV as CMP; the predicated RSB leaves it intact. */
  cg_emit(c, "subs.n r%u, r%u, r%u\nit lo\nrsblo.n r%u, r%u, #0\n"
             "@ unsigned-distance: predicated reverse subtract\n", dst, a, b, dst, dst);
  return second_end + 1;
}

void cg_fold_distances(cg_context *c, size_t start) {
  const char *input = c->out + start;
  if (!strstr(input, "\nbls.n .Lcg_") || strstr(input, "@ constant-assembly-lines:"))
    return;
  size_t length = c->size - start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, input, length);
  c->size = start;
  for (const char *p = body; *p;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end)
      cg_error(c, "unterminated distance selection instruction");
    const char *joined = fold_unsigned_distance(c, body, p, end);
    if (joined)
      p = joined;
    else {
      cg_append(c, p, (size_t)(end + 1 - p));
      p = end + 1;
    }
  }
}

void cg_reduce_branches(cg_context *c, size_t body_start) {
  if (!strstr(c->out + body_start, ".Lcg_edge")) return;
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
  emitted_labels labels = index_emitted_labels(c, body, 1);
  c->size = body_start;
  unsigned assembly_lines = 0;
  for (const char *p = body; p < body + length;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end) cg_error(c, "unterminated branch selection instruction");
    char opcode[12], edge[128], target[128], label[129];
    int consumed = 0;
    if (assembly_lines) assembly_lines--;
    else if (!strncmp(p, "@ constant-assembly-lines:", 26))
      sscanf(p, "@ constant-assembly-lines: %u", &assembly_lines);
    else if (*first_nonspace(p) == 'b' &&
             sscanf(p, "%11s %127s%n", opcode, edge, &consumed) == 2 &&
             p + consumed == end && opcode[0] == 'b' &&
             cg_inverse_condition(opcode + 1) && !strncmp(edge, ".Lcg_edge", 9)) {
      const char *jump = end + 1, *jump_end = strchr(jump, '\n');
      const char *edge_line = jump_end ? jump_end + 1 : NULL;
      const char *edge_end = edge_line ? strchr(edge_line, '\n') : NULL;
      consumed = 0;
      if (jump_end && edge_end &&
          sscanf(jump, "b %127s%n", target, &consumed) == 1 &&
          jump + consumed == jump_end && !strncmp(target, ".Lcg_", 5)) {
        snprintf(label, sizeof(label), "%s:", edge);
        const char *destination = emitted_lookup(c, labels, target, strlen(target))->definition;
        const emitted_label *original = emitted_lookup(c, labels, edge, strlen(edge));
        if ((size_t)(edge_end - edge_line) == strlen(label) &&
            !memcmp(edge_line, label, strlen(label)) && destination &&
            original->definition == edge_line && original->mentions == 2) {
          int narrow = branch_span(c, p, destination, 240);
          if (narrow || branch_span(c, p, destination, 32760)) {
            cg_emit(c, "b%s.%c %s\n@ branch-selection: folded edge\n",
                    cg_inverse_condition(opcode + 1), narrow ? 'n' : 'w', target);
            p = edge_line;
            continue;
          }
        }
      }
    }
    cg_append(c, p, (size_t)(end + 1 - p));
    p = end + 1;
  }
}
void cg_share_return_tails(cg_context *c, size_t body_start) {
  const char *original = c->out + body_start;
  if (strstr(original, "@ constant-assembly-lines:"))
    return;
  const char *last = NULL, *last_end = NULL;
  for (const char *p = original; *p;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end)
      cg_error(c, "unterminated return-tail instruction");
    if (*p != '@' && *p != '\n' && end[-1] != ':') {
      last = p;
      last_end = end;
    }
    p = end + 1;
  }
  unsigned value;
  int at = 0;
  if (!last || !((sscanf(last, "mov r0, r%u%n", &value, &at) == 1 ||
                  sscanf(last, "movw r0, #%u%n", &value, &at) == 1 ||
                  sscanf(last, "movs.n r0, #%u%n", &value, &at) == 1) &&
                 at && last + at == last_end))
    return;
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, original, length + 1);
  last_end = body + (last_end - original);
  last = body + (last - original);
  unsigned label = 0;
  c->size = body_start;
  for (const char *p = body; *p;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end)
      cg_error(c, "unterminated return-tail instruction");
    unsigned b;
    int move = p < last && end - p == last_end - last &&
               !memcmp(p, last, (size_t)(end - p));
    const char *jump = end + 1, *finish = move ? strchr(jump, '\n') : NULL;
    at = 0;
    if (finish &&
        (sscanf(jump, "b .Lcg_return%u%n", &b, &at) == 1 ||
         sscanf(jump, "b.w .Lcg_return%u%n", &b, &at) == 1) &&
        at && jump + at == finish && b == c->fn->id &&
        branch_span(c, p, last, 2000)) {
      if (!label)
        label = ++c->unique;
      cg_emit(c, "b.n .Lcg_shared_return%u\n", label);
      p = finish + 1;
      continue;
    }
    if (p == last && label)
      cg_emit(c, ".Lcg_shared_return%u:\n", label);
    cg_append(c, p, (size_t)(end + 1 - p));
    p = end + 1;
  }
  c->out[c->size] = 0;
}
void cg_forward_byte_arguments(cg_context *c, size_t body_start) {
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
  c->size = body_start;
  unsigned assembly_lines = 0;
  for (char *p = body; p < body + length;) {
    char *next = line(p);
    if (assembly_lines) assembly_lines--;
    else if (!strncmp(p, "@ constant-assembly-lines:", 26))
      sscanf(p, "@ constant-assembly-lines: %u", &assembly_lines);
    else {
      char opcode[12];
      unsigned base;
      int consumed = 0;
      unsigned source, immediate_value;
      int shifted = sscanf(p, "%11s r3, r%u, #%u%n", opcode, &source, &immediate_value, &consumed) == 3 &&
          !p[consumed] && source <= 12 &&
          (!strcmp(opcode, "lsr.w") || !strcmp(opcode, "lsl.w") ||
           !strcmp(opcode, "asr.w") || !strcmp(opcode, "and.w"));
      consumed = 0;
      int extended = !shifted && sscanf(p, "%11s r3, r%u%n", opcode, &source, &consumed) == 2 &&
          consumed && !p[consumed] && source <= 12 &&
          (!strcmp(opcode, "uxtb.n") || !strcmp(opcode, "uxth.n") ||
           !strcmp(opcode, "uxtb.w") || !strcmp(opcode, "uxth.w"));
      if (shifted || extended) {
        char *move_end = strchr(next, '\n');
        char *call = move_end ? move_end + 1 : NULL;
        char *call_end = call ? strchr(call, '\n') : NULL;
        if (move_end && call_end &&
            (size_t)(move_end - next) == strlen("mov r0, r3") &&
            !memcmp(next, "mov r0, r3", (size_t)(move_end - next)) &&
            !strncmp(call, "bl ", 3) &&
            !strncmp(call_end + 1, "@ scalar-call: arguments 1\n", 27)) {
          if (extended)
            cg_emit(c, "%s r0, r%u\n", opcode, source);
          else
            cg_emit(c, "%s r0, r%u, #%u\n", opcode, source, immediate_value);
          cg_emit(c, "%.*s\n@ scalar-call-forwarding: saved 2\n", (int)(call_end - call), call);
          p = call_end + 1;
          continue;
        }
      }
      consumed = 0;
      if (sscanf(p, "%11s r3, [r%u]%n", opcode, &base, &consumed) == 2 &&
          !p[consumed] && base != 3 &&
          (!strcmp(opcode, "ldrb.n") || !strcmp(opcode, "ldrb.w"))) {
        char *memory = next, *memory_end = strchr(memory, '\n');
        char *add = memory_end ? memory_end + 1 : NULL;
        char *add_end = add ? strchr(add, '\n') : NULL;
        char *move = add_end ? add_end + 1 : NULL;
        char *move_end = move ? strchr(move, '\n') : NULL;
        char *call = move_end ? move_end + 1 : NULL;
        char *call_end = call ? strchr(call, '\n') : NULL;
        unsigned dst, src, number;
        consumed = 0;
        if (memory_end && add_end && move_end && call_end &&
            (size_t)(memory_end - memory) == strlen("@ operand-memory: direct 1") &&
            !memcmp(memory, "@ operand-memory: direct 1", (size_t)(memory_end - memory)) &&
            sscanf(add, "add.w r%u, r%u, #%u%n", &dst, &src, &number, &consumed) == 3 &&
            add + consumed == add_end && dst != 3 && src != 3 &&
            (size_t)(move_end - move) == strlen("mov r0, r3") &&
            !memcmp(move, "mov r0, r3", (size_t)(move_end - move)) &&
            !strncmp(call, "bl ", 3) &&
            !strncmp(call_end + 1, "@ scalar-call: arguments 1\n", 27)) {
          cg_emit(c, "%s r0, [r%u]\n@ operand-memory: direct 1\n", opcode, base);
          cg_emit(c, "%.*s\n%.*s\n@ byte-call-forwarding: saved 2\n",
                  (int)(add_end - add), add, (int)(call_end - call), call);
          p = call_end + 1;
          continue;
        }
      }
    }
    cg_emit(c, "%s\n", p);
    p = next;
  }
}
void cg_fold_zero_branches(cg_context *c, size_t body_start) {
  if (!strstr(c->out + body_start, "cmp.n ")) return;
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
  emitted_labels labels = index_emitted_labels(c, body, 0);
  c->size = body_start;
  unsigned assembly_lines = 0;
  for (const char *p = body; p < body + length;) {
    const char *end = strchr(p, '\n');
    if (!end) cg_error(c, "unterminated zero branch instruction");
    unsigned reg;
    int consumed = 0;
    if (assembly_lines) assembly_lines--;
    else if (!strncmp(p, "@ constant-assembly-lines:", 26))
      sscanf(p, "@ constant-assembly-lines: %u", &assembly_lines);
    else if (!strncmp(p, "cmp.n", 5) &&
             sscanf(p, "cmp.n r%u, #0%n", &reg, &consumed) == 1 &&
             p + consumed == end && reg < 8) {
      const char *branch = end + 1, *branch_end = strchr(branch, '\n');
      char opcode[12], target[128];
      consumed = 0;
      if (branch_end &&
          sscanf(branch, "%11s %127s%n", opcode, target, &consumed) == 2 &&
          branch + consumed == branch_end &&
          (!strcmp(opcode, "beq.n") || !strcmp(opcode, "bne.n"))) {
        const char *destination = emitted_lookup(c, labels, target, strlen(target))->definition;
        unsigned bytes = 0, instructions = 0;
        int bounded = destination && destination > branch_end;
        for (const char *q = branch_end + 1; bounded && q < destination;) {
          const char *finish = strchr(q, '\n');
          if (!finish || !strncmp(q, "@ constant-assembly-lines:", 26) ||
              !strncmp(q, "@ frame-incoming:", 17)) bounded = 0;
          else {
            if (*q && *q != '@' && *q != '\n') {
              if (*q == '.') {
                if (strncmp(q, ".Lcg_", 5) || finish[-1] != ':') bounded = 0;
              } else { bytes += 4; instructions++; }
            }
            if (bytes > 120) bounded = 0;
            q = finish + 1;
          }
          cg_register_work(c);
        }
        if (bounded && instructions &&
            flags_overwritten(c, body, branch_end + 1, &labels) &&
            flags_overwritten(c, body, destination, &labels)) {
          cg_emit(c, "%s r%u, %s\n@ zero-branch: saved 2\n",
                  opcode[1] == 'e' ? "cbz" : "cbnz", reg, target);
          p = branch_end + 1;
          continue;
        }
      }
    }
    cg_append(c, p, (size_t)(end + 1 - p));
    p = end + 1;
  }
}
static int mentions(const char *text, unsigned reg) {
  for (const char *p = text; *p; p++)
    if (*p == 'r' && (p == text || !isalnum((unsigned char)p[-1]))) {
      unsigned n;
      int end = 0;
      if (sscanf(p, "r%u%n", &n, &end) == 1 && n == reg &&
          !isalnum((unsigned char)p[end]) && p[end] != '_') return 1;
    }
  return 0;
}
static int overwritten_before_use(cg_context *c, const char *text, unsigned reg) {
  for (unsigned n = 0; n < 64 && *text; n++) {
    cg_register_work(c);
    const char *end = strchr(text, '\n');
    size_t length = end ? (size_t)(end - text) : strlen(text);
    if (length >= 256) return 0;
    char instruction[256], op[24];
    memcpy(instruction, text, length);
    instruction[length] = 0;
    if (!strncmp(instruction, "@ assembly-effects:", 19) ||
        !strncmp(instruction, "@ frame-incoming:", 17) ||
        !strncmp(instruction, "@ constant-assembly-lines:", 26)) return 0;
    if (*instruction && *instruction != '@') {
      unsigned dst;
      int operand = 0;
      if (sscanf(instruction, "%23s r%u,%n", op, &dst, &operand) == 2 &&
          operand && dst == reg &&
          (!strcmp(op, "movw") || !strcmp(op, "mov") || !strcmp(op, "mov.w") ||
           !strcmp(op, "movs.n") || !strcmp(op, "add.w") || !strcmp(op, "sub.w") ||
           !strcmp(op, "ldr.n") || !strcmp(op, "ldr.w") ||
           !strcmp(op, "ldrb.n") || !strcmp(op, "ldrb.w") ||
           !strcmp(op, "ldrh.n") || !strcmp(op, "ldrh.w") ||
           !strcmp(op, "uxtb.n") || !strcmp(op, "uxtb.w") ||
           !strcmp(op, "uxth.n") || !strcmp(op, "uxth.w")) &&
          !mentions(instruction + operand, reg))
        return 1;
      if (mentions(instruction, reg) || sscanf(instruction, "%23s", op) != 1)
        return 0;
      cg_constant facts[13] = {{0}};
      for (unsigned r = 0; r < 13; r++) facts[r].known = 1;
      effects(instruction, facts);
      if (!facts[reg].known) return 0;
    }
    if (!end) return 0;
    text = end + 1;
  }
  return 0;
}
typedef struct {
  char op[24];
  unsigned reg, base, bytes, shift;
  int offset, index, narrow, sign;
} scalar_transfer;

static int parse_transfer(const char *p, const char *end, scalar_transfer *m) {
  const char *opcode = first_nonspace(p);
  if (opcode >= end || (strncmp(opcode, "ldr", 3) && strncmp(opcode, "str", 3)))
    return 0;
  char text[128];
  size_t length = (size_t)(end - p);
  if (length < sizeof(text)) {
    memcpy(text, p, length);
    text[length] = 0;
    p = text;
    end = text + length;
  }
  int at = 0, used = 0;
  char base[8];
  *m = (scalar_transfer){.index = -1};
  if (sscanf(p, "%23s r%u, [%7[a-z0-9]%n", m->op, &m->reg, base, &at) != 3 ||
      !at || p + at >= end || m->reg > 12 ||
      (strncmp(m->op, "ldr", 3) && strncmp(m->op, "str", 3)))
    return 0;
  const char *suffix = m->op + 3;
  m->sign = *suffix == 's' && m->op[0] == 'l';
  if (m->sign)
    suffix++;
  m->bytes = 4;
  if (*suffix == 'b' || *suffix == 'h')
    m->bytes = *suffix++ == 'b' ? 1 : 2;
  m->narrow = !strcmp(suffix, ".n");
  if (!(m->narrow || !strcmp(suffix, ".w")) || (m->sign && m->bytes == 4))
    return 0;
  if (!strcmp(base, "sp"))
    m->base = 13;
  else if (sscanf(base, "r%u%n", &m->base, &used) != 1 ||
           !used || base[used] || m->base > 12)
    return 0;
  p += at;
  if (p + 1 == end && *p == ']')
    return 1;
  used = 0;
  if (sscanf(p, ", #%d]%n", &m->offset, &used) == 1 && used && p + used == end)
    return m->offset >= 0 && m->offset <= 4095;
  unsigned index;
  used = 0;
  if ((sscanf(p, ", r%u, lsl #%u]%n", &index, &m->shift, &used) != 2 ||
       !used || p + used != end) &&
      (sscanf(p, ", r%u]%n", &index, &used) != 1 || !used || p + used != end))
    return 0;
  if (index > 12 || m->shift > 3)
    return 0;
  m->index = (int)index;
  return 1;
}

static int narrow_transfer(const scalar_transfer *m) {
  if (m->reg >= 8)
    return 0;
  if (m->index >= 0)
    return m->base < 8 && m->index < 8 && !m->shift;
  return !m->sign && m->offset >= 0 && !(m->offset % (int)m->bytes) &&
      (m->base < 8 ? m->offset < (int)m->bytes * 32 :
       m->base == 13 && m->bytes == 4 && m->offset <= 1020);
}

static void emit_transfer(cg_context *c, const scalar_transfer *m, int narrow) {
  const char *dot = strchr(m->op, '.');
  cg_emit(c, "%.*s.%c r%u, [", (int)(dot - m->op), m->op,
          narrow ? 'n' : 'w', m->reg);
  if (m->base == 13)
    cg_emit(c, "sp");
  else
    cg_emit(c, "r%u", m->base);
  if (m->index >= 0) {
    cg_emit(c, ", r%d", m->index);
    if (m->shift)
      cg_emit(c, ", lsl #%u", m->shift);
  } else if (m->offset)
    cg_emit(c, ", #%d", m->offset);
  cg_emit(c, "]\n");
}

static int parse_move(const char *p, const char *end, unsigned *dst,
                      unsigned *src) {
  int used = 0;
  return !strncmp(p, "mov", 3) &&
         sscanf(p, "mov r%u, r%u%n", dst, src, &used) == 2 &&
         p + used == end && *dst <= 12 && *src <= 12 && *dst != *src;
}

static const char *fold_transfer_move(cg_context *c, const char *p,
                                       const char *end) {
  const char *next = end + 1, *finish = strchr(next, '\n');
  if (!finish)
    return NULL;
  scalar_transfer m;
  unsigned dst, src, reg;
  int replaced = 0;
  if (parse_move(p, end, &dst, &src) && parse_transfer(next, finish, &m)) {
    reg = dst;
    if (m.op[0] == 's' && m.reg == dst && m.base != dst && m.index != (int)dst)
      m.reg = src;
    else if (m.base == dst && m.index != (int)dst && (m.op[0] == 'l' || m.reg != dst)) {
      m.base = src;
      replaced = m.op[0] == 'l' && m.reg == dst;
    } else
      return NULL;
  } else if (parse_transfer(p, end, &m) && m.op[0] == 'l' &&
             parse_move(next, finish, &dst, &src) && m.reg == src) {
    reg = src;
    m.reg = dst;
  } else
    return NULL;
  if (!replaced && !overwritten_before_use(c, finish + 1, reg))
    return NULL;
  int narrow = narrow_transfer(&m);
  emit_transfer(c, &m, narrow);
  cg_emit(c, "@ memory-move-fold: saved %u\n", (m.narrow ? 4u : 6u) - (narrow ? 2u : 4u));
  return finish + 1;
}

static const char *fold_stack_pair(cg_context *c, const char *p,
                                   const char *end) {
  if (c->frame_addressing != BADGE_FRAME_DIRECT)
    return NULL;
  const char *next = end + 1, *finish = strchr(next, '\n');
  scalar_transfer a, b;
  if (!finish || !parse_transfer(p, end, &a) ||
      !parse_transfer(next, finish, &b) || a.op[0] != b.op[0] ||
      a.bytes != 4 || b.bytes != 4 || a.base != b.base ||
      (a.base != 7 && a.base != 13) || a.index >= 0 || b.index >= 0 ||
      a.offset % 4 || a.offset > 1020 || b.offset != a.offset + 4 ||
      (a.narrow && b.narrow))
    return NULL;
  int load = a.op[0] == 'l';
  if (load && (a.reg == b.reg || a.reg == a.base || b.reg == a.base))
    return NULL;
  cg_emit(c, "%s r%u, r%u, [", load ? "ldrd" : "strd", a.reg, b.reg);
  if (a.base == 13)
    cg_emit(c, "sp");
  else
    cg_emit(c, "r7");
  cg_emit(c, ", #%d]\n@ stack-pair-fold: saved %u\n",
          a.offset, a.narrow || b.narrow ? 2u : 4u);
  return finish + 1;
}

static const char *spill_notes(const char *p) {
  while (*p == '@' && strncmp(p, "@ constant-assembly-lines:", 26) &&
         strncmp(p, "@ frame-incoming:", 17) && strncmp(p, "@ assembly-effects:", 19)) {
    const char *end = strchr(p, '\n');
    if (!end)
      break;
    p = end + 1;
  }
  return p;
}

static const char *fold_large_spill_reload(cg_context *c, const char *p,
                                          const char *end) {
  scalar_transfer store;
  if (c->frame_addressing != BADGE_FRAME_DIRECT ||
      strncmp(p, "str.", 4) || !parse_transfer(p, end, &store) ||
      store.base != 7 || store.index >= 0 || store.offset % 4 || store.reg == 7)
    return NULL;
  const char *line = spill_notes(end + 1);
  for (unsigned n = 0; n < 2; n++) {
    const char *finish = strchr(line, '\n');
    scalar_transfer load;
    if (!finish || !parse_transfer(line, finish, &load) ||
        load.op[0] != 'l' || load.bytes != 4 || load.base != 7 ||
        load.index >= 0 || load.reg == 7)
      break;
    if (load.reg == store.reg) {
      if (load.offset != store.offset)
        break;
      cg_append(c, p, (size_t)(line - p));
      cg_emit(c, "@ spill-reload: value already in the destination register\n");
      return finish + 1;
    }
    line = spill_notes(finish + 1);
  }
  return NULL;
}

static const char *fold_spill_reload(cg_context *c, const char *p, const char *end) {
  scalar_transfer store;
  if (strncmp(p, "str.", 4) || !parse_transfer(p, end, &store) ||
      store.bytes != 4 || store.index >= 0 || store.base != 7 || store.reg != 0 || !store.narrow)
    return NULL;
  const char *line = spill_notes(end + 1);
  const char *finish = strchr(line, '\n');
  scalar_transfer address;
  if (!finish || !parse_transfer(line, finish, &address) || address.op[0] != 'l' ||
      address.bytes != 4 || address.base != 7 || address.index >= 0 ||
      address.reg != 4 || address.offset == store.offset || !address.narrow)
    return NULL;
  line = spill_notes(finish + 1);
  finish = strchr(line, '\n');
  scalar_transfer reload;
  if (!finish || !parse_transfer(line, finish, &reload) || reload.op[0] != 'l' ||
      reload.bytes != 4 || reload.base != 7 || reload.index >= 0 || reload.reg != 0 ||
      reload.offset != store.offset || !reload.narrow)
    return NULL;
  const char *transfer = spill_notes(finish + 1), *last = strchr(transfer, '\n');
  scalar_transfer output;
  if (!last || !parse_transfer(transfer, last, &output) || output.op[0] != 's' ||
      output.bytes == 1 || output.reg != 0 || output.base != 4 ||
      output.index >= 0 || output.offset)
    return NULL;
  cg_append(c, p, (size_t)(line - p));
  cg_emit(c, "@ spill-reload: value already in the destination register\n");
  return finish + 1;
}

static const char *fold_shifted_load(cg_context *c, const char *p, const char *end) {
  if (strncmp(p, "lsl.w ", 6))
    return NULL;
  unsigned dst, index, shift;
  int used = 0;
  if (sscanf(p, "lsl.w r%u, r%u, #%u%n", &dst, &index, &shift, &used) != 3 ||
      p + used != end || dst > 12 || index > 12 || dst == index || !shift || shift > 3)
    return NULL;
  const char *line = end + 1;
  for (unsigned n = 0; n < 4; n++) {
    line = spill_notes(line);
    const char *finish = strchr(line, '\n');
    if (!finish)
      return NULL;
    scalar_transfer load;
    if (parse_transfer(line, finish, &load) && load.op[0] == 'l' && load.reg == dst &&
        load.index == (int)dst && load.base != dst && load.base != 13 &&
        !load.shift && !load.offset) {
      load.index = (int)index;
      load.shift = shift;
      cg_append(c, end + 1, (size_t)(line - end - 1));
      emit_transfer(c, &load, 0);
      cg_emit(c, "@ memory-index-fold: shifted load\n");
      return finish + 1;
    }
    /* Only address materialization may occur between the shift and load. */
    if ((strncmp(line, "movw r", 6) && strncmp(line, "movt r", 6)))
      return NULL;
    char materialization[256];
    size_t length = (size_t)(finish - line);
    if (length >= sizeof(materialization))
      return NULL;
    memcpy(materialization, line, length);
    materialization[length] = 0;
    if (mentions(materialization, dst) || mentions(materialization, index))
      return NULL;
    line = finish + 1;
  }
  return NULL;
}

static const char *fold_shifted_logic(cg_context *c, const char *p, const char *end) {
  unsigned tmp, src, shift, dst, a, b;
  int used = 0;
  if (strncmp(p, "lsr.w ", 6) ||
      sscanf(p, "lsr.w r%u, r%u, #%u%n", &tmp, &src, &shift, &used) != 3 ||
      p + used != end || tmp > 12 || src > 12 || !shift || shift >= 32)
    return NULL;
  const char *line = spill_notes(end + 1), *finish = strchr(line, '\n');
  char op[8];
  used = 0;
  if (!finish || sscanf(line, "%7[a-z].w r%u, r%u, r%u%n", op, &dst, &a, &b, &used) != 4 ||
      line + used != finish || dst != tmp || a > 12 || b > 12 ||
      (a == tmp) == (b == tmp) ||
      (strcmp(op, "and") && strcmp(op, "eor") && strcmp(op, "orr")))
    return NULL;
  cg_append(c, end + 1, (size_t)(line - end - 1));
  cg_emit(c, "%s.w r%u, r%u, r%u, lsr #%u\n@ shifted-logic: folded in-place result\n",
          op, dst, a == tmp ? b : a, src, shift);
  return finish + 1;
}

static const char *fold_word_argument(cg_context *c, const char *p, const char *end) {
  scalar_transfer spill, reload;
  if (!parse_transfer(p, end, &spill) || spill.op[0] != 's' || spill.bytes != 4 ||
      spill.reg || spill.base != 7 || spill.index >= 0 || !spill.narrow)
    return NULL;
  const char *line = spill_notes(end + 1), *finish = strchr(line, '\n');
  if (!finish || !parse_transfer(line, finish, &reload) || reload.op[0] != 'l' ||
      reload.bytes != 4 || reload.reg || reload.base != 7 || reload.index >= 0 ||
      reload.offset != spill.offset || !reload.narrow)
    return NULL;
  const char *move = spill_notes(finish + 1), *last = strchr(move, '\n');
  unsigned dst, src;
  if (!last || !parse_move(move, last, &dst, &src) || dst != 1 || src < 4 ||
      strncmp(last + 1, "bl ", 3))
    return NULL;
  cg_append(c, p, (size_t)(line - p));
  cg_emit(c, "@ word-argument: r0 still holds the spilled argument\n");
  return finish + 1;
}

void cg_fold_memory_addresses(cg_context *c, size_t body_start) {
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
  c->size = body_start;
  unsigned assembly_lines = 0;
  int large_cfg = c->register_residency == BADGE_RESIDENCY_GLOBAL_COMPACT &&
                  c->fn->block_count > CG_LOOP_ALLOCATION_BLOCK_LIMIT;
  for (const char *p = body; p < body + length;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end) cg_error(c, "unterminated memory address instruction");
    unsigned dst, base, index = 0, shift = 0, offset = 0;
    int consumed = 0;
    int indexed = 0, immediate = 0;
    if (assembly_lines) assembly_lines--;
    else if (!strncmp(p, "@ constant-assembly-lines:", 26))
      sscanf(p, "@ constant-assembly-lines: %u", &assembly_lines);
    else {
      const char *after = large_cfg ? fold_large_spill_reload(c, p, end) : NULL;
      if (!after)
        after = fold_spill_reload(c, p, end);
      if (!after)
        after = fold_word_argument(c, p, end);
      if (!after)
        after = fold_shifted_logic(c, p, end);
      if (!after)
        after = fold_shifted_load(c, p, end);
      if (!after)
        after = fold_stack_pair(c, p, end);
      if (!after)
        after = fold_transfer_move(c, p, end);
      if (after) {
        p = after;
        continue;
      }
      if (!strncmp(p, "add.w", 5)) {
        indexed = (sscanf(p, "add.w r%u, r%u, r%u, lsl #%u%n",
                          &dst, &base, &index, &shift, &consumed) == 4 &&
                   p + consumed == end) ||
                  (sscanf(p, "add.w r%u, r%u, r%u%n",
                          &dst, &base, &index, &consumed) == 3 &&
                   p + consumed == end);
        if (!indexed)
          immediate = sscanf(p, "add.w r%u, r%u, #%u%n",
                             &dst, &base, &offset, &consumed) == 3 &&
                      p + consumed == end && offset <= 4095;
      } else if (!strncmp(p, "addw", 4))
        immediate = sscanf(p, "addw r%u, r%u, #%u%n",
                           &dst, &base, &offset, &consumed) == 3 &&
                    p + consumed == end && offset <= 4095;
    }
    if ((indexed || immediate) && dst <= 12 && base <= 12 && (!indexed || dst != base) &&
        (!indexed || (index <= 12 && shift <= 3 && dst != index))) {
      const char *memory = end + 1;
      for (unsigned n = 0; n < 16 && *memory; n++) {
        const char *finish = strchr(memory, '\n');
        if (!finish || finish - memory >= 256) break;
        char instruction[256];
        memcpy(instruction, memory, (size_t)(finish - memory));
        instruction[finish - memory] = 0;
        scalar_transfer m;
        if (parse_transfer(instruction, instruction + (finish - memory), &m) &&
            m.base == dst && m.index < 0 && !m.offset &&
            (m.reg == dst ? m.op[0] == 'l' : overwritten_before_use(c, finish + 1, dst))) {
          m.base = base;
          m.index = indexed ? (int)index : -1;
          m.shift = shift;
          m.offset = (int)offset;
          int short_form = !m.sign && narrow_transfer(&m);
          cg_append(c, end + 1, (size_t)(memory - end - 1));
          emit_transfer(c, &m, short_form);
          cg_emit(c, "@ memory-address-fold: saved %u\n",
                  4u + (m.narrow ? 2u : 4u) - (short_form ? 2u : 4u));
          p = finish + 1;
          break;
        }
        if (*instruction != '@' && *instruction) {
          cg_constant facts[13] = {{0}};
          facts[base].known = 1;
          if (indexed)
            facts[index].known = 1;
          effects(instruction, facts);
          if (mentions(instruction, dst) || !facts[base].known ||
              (indexed && !facts[index].known))
            break;
        } else if (!strncmp(instruction, "@ assembly-effects:", 19) ||
                   !strncmp(instruction, "@ constant-assembly-lines:", 26) ||
                   !strncmp(instruction, "@ frame-incoming:", 17)) break;
        memory = finish + 1;
      }
      if (p > end) continue;
    }
    cg_append(c, p, (size_t)(end + 1 - p));
    p = end + 1;
  }
}
static int label_character(unsigned char ch) {
  return isalnum(ch) || ch == '_' || ch == '.' || ch == '$';
}

static int sole_label_entry(cg_context *c, const char *body, const char *name,
                            const char *label, const char *jump) {
  size_t length = strlen(name);
  for (const char *p = body; (p = strstr(p, name)); p += length) {
    cg_register_work(c);
    if (p != label && p != jump && (p == body || !label_character(p[-1])) &&
        !label_character(p[length]))
      return 0;
  }
  return 1;
}

static void copy_line(cg_context *c, char *text, char *next) {
  if (next > text && !next[-1]) {
    next[-1] = '\n';
    cg_append(c, text, (size_t)(next - text));
    next[-1] = 0;
  } else
    cg_emit(c, "%s\n", text);
}

void cg_reuse_constants(cg_context *c, size_t body_start) {
  int synthesis = c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS;
  int cfg = synthesis && c->size - body_start <= 32768;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_assembly *a = cg_instruction_assembly(c, i);
      if (!synthesis && a && !a->known) return;
      if (a) cfg = 0;
    }
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
  char *original = synthesis ? cg_alloc(c, length + 1) : body;
  if (synthesis) memcpy(original, body, length);
  c->size = body_start;
  cg_constant facts[13] = {{0}};
  unsigned assembly_lines = 0;
  char *current = body, *carried_label = NULL;
  while (current < body + length) {
    cg_register_work(c);
    char *next_line = line(current);
    if (synthesis && !assembly_lines &&
        !strncmp(current, "@ assembly-effects:", 19)) {
      if (!strncmp(current, "@ assembly-effects: known 0", 27))
        memset(facts, 0, sizeof(facts));
    }
    if (assembly_lines) {
      effects(current, facts);
      copy_line(c, current, next_line);
      assembly_lines--;
      current = next_line;
      continue;
    }
    if (synthesis && !strncmp(current, "@ constant-assembly-lines:", 26)) {
      int end = 0;
      if (sscanf(current, "@ constant-assembly-lines: %u%n", &assembly_lines, &end) != 1 ||
          current[end] || !assembly_lines)
        cg_error(c, "invalid constant selection assembly boundary");
      copy_line(c, current, next_line);
      current = next_line;
      continue;
    }
    if (cfg && !assembly_lines) {
      char name[128];
      int consumed = 0, operand = 0;
      if (current == carried_label) {
        copy_line(c, current, next_line);
        carried_label = NULL;
        current = next_line;
        continue;
      }
      if (!strncmp(current, ".Lcg_", 5) &&
          sscanf(current, "%127[.a-zA-Z0-9_]:%n", name, &consumed) == 1 &&
          consumed && !current[consumed] && !strncmp(name, ".Lcg_", 5) &&
          sole_label_entry(c, original, name, original + (current - body), NULL)) {
        copy_line(c, current, next_line);
        current = next_line;
        continue;
      }
      consumed = 0;
      if (opcode_prefix(current, "b", 1) &&
          sscanf(current, "b %n%127s%n", &operand, name, &consumed) == 1 &&
          consumed && !current[consumed] && !strncmp(name, ".Lcg_", 5)) {
        size_t n = strlen(name);
        if (!strncmp(next_line, name, n) && next_line[n] == ':' &&
            next_line[n + 1] == '\n' &&
            sole_label_entry(c, original, name, original + (next_line - body),
                             original + (current - body) + operand)) {
          carried_label = next_line;
          copy_line(c, current, next_line);
          current = next_line;
          continue;
        }
      }
    }
    if (synthesis) {
      const char *op = first_nonspace(current);
      int extract = opcode_prefix(op, "and.w", 5);
      int shift = opcode_prefix(op, "lsl.w", 5) || opcode_prefix(op, "lsr.w", 5) ||
                  opcode_prefix(op, "asr.w", 5);
      int arithmetic = opcode_prefix(op, "add.w", 5) || opcode_prefix(op, "sub.w", 5);
      int sets_flags = opcode_prefix(op, "adds.w", 6) || opcode_prefix(op, "subs.w", 6);
      unsigned dst, src, number;
      int consumed = 0;
      if ((extract || shift || arithmetic || sets_flags) &&
          sscanf(op + (sets_flags ? 6 : 5), " r%u, r%u, #%u%n",
                 &dst, &src, &number, &consumed) == 3 &&
          !op[(sets_flags ? 6 : 5) + consumed] && dst < 7 && src < 7) {
        if (extract && (number == 255 || number == 65535)) {
          cg_emit(c, "%s.n r%u, r%u\n@ scalar-extract: saved 2\n",
                  number == 255 ? "uxtb" : "uxth", dst, src);
          facts[dst].known = 0;
          current = next_line;
          continue;
        }
        if (shift && (op[2] == 'l' ? number <= 31 : number >= 1 && number <= 32)) {
          if (cg_flags_overwritten(c, original, original + (next_line - body))) {
            cg_emit(c, "%.*ss.n r%u, r%u, #%u\n@ scalar-shift: saved 2\n",
                    3, op, dst, src, number);
            facts[dst].known = 0;
            current = next_line;
            continue;
          }
        }
        if ((arithmetic || sets_flags) && (number <= 7 || (dst == src && number <= 255)) &&
            (sets_flags || cg_flags_overwritten(c, original, original + (next_line - body)))) {
          cg_emit(c, "%ss.n r%u, r%u, #%u\n@ scalar-width: saved 2\n",
                  op[0] == 'a' ? "add" : "sub", dst, src, number);
          facts[dst].known = 0;
          current = next_line;
          continue;
        }
      }
      consumed = 0;
      /* Frame finalization may replace r7 with sp. */
      unsigned right;
      if (arithmetic &&
          sscanf(op + 5, " r%u, r%u, r%u%n", &dst, &src, &right, &consumed) == 3 &&
          !op[5 + consumed] && dst < 7 && src < 7 && right < 7 &&
          cg_flags_overwritten(c, original, original + (next_line - body))) {
        cg_emit(c, "%ss.n r%u, r%u, r%u\n@ scalar-width: saved 2\n",
                op[0] == 'a' ? "add" : "sub", dst, src, right);
        facts[dst].known = 0;
        current = next_line;
        continue;
      }
    }
    unsigned dst, upper_dst;
    uint32_t lower, upper;
    cg_constant next = {0};
    unsigned old_bytes = 0;
    char *after = next_line;
    if (immediate(current, "movw", &dst, &lower)) {
      next.known = 1;
      next.number = lower;
      old_bytes = 4;
      char *end = strchr(next_line, '\n');
      if (end) {
        *end = 0;
        if (immediate(next_line, "movt", &upper_dst, &upper) && upper_dst == dst) {
          next.number |= upper << 16;
          old_bytes = 8;
          after = end + 1;
        }
        *end = '\n';
      }
    } else {
      char *expr = symbol(current, "movw", &dst);
      char *end = strchr(next_line, '\n');
      if (expr && end) {
        *end = 0;
        char *high = symbol(next_line, "movt", &upper_dst);
        if (high && upper_dst == dst && !strcmp(expr, high)) {
          char *sign = NULL;
          for (char *p = expr; *p; p++)
            if (*p == '+' || *p == '-') sign = p;
          char *finish = NULL;
          int64_t offset = sign ? strtoll(sign, &finish, 10) : 0;
          if (finish && !strcmp(finish, ")") && offset >= -32768 && offset <= 32767) {
            next.known = 1;
            next.addend = offset;
            next.symbol = cg_alloc(c, (size_t)(sign - expr) + 1);
            memcpy(next.symbol, expr, (size_t)(sign - expr));
            old_bytes = 8;
            after = end + 1;
          }
        }
        *end = '\n';
      }
    }
    if (next.known) {
      int folded_copy = 0;
      if (synthesis && !next.symbol) {
        char *end = strchr(after, '\n');
        unsigned copy_dst, copy_src, overwrite;
        uint32_t number;
        int consumed = 0;
        if (end) {
          *end = 0;
          if (sscanf(after, "mov r%u, r%u%n", &copy_dst, &copy_src, &consumed) == 2 &&
              !after[consumed] && copy_src == dst && copy_dst <= 12 &&
              copy_dst != dst) {
            char *following = end + 1, *finish = strchr(following, '\n');
            if (finish) {
              *finish = 0;
              if (immediate(following, "movw", &overwrite, &number) &&
                  overwrite == dst) {
                dst = copy_dst;
                folded_copy = 1;
              }
              *finish = '\n';
            }
          }
          *end = '\n';
          if (folded_copy) after = end + 1;
        }
      }
      int modified = synthesis && !next.symbol && old_bytes > 4 &&
                     thumb_modified(next.number) >= 0;
      int complemented = synthesis && !next.symbol && old_bytes > 4 &&
                         !modified && thumb_modified(~next.number) >= 0;
      int narrow_immediate = synthesis && !next.symbol && dst < 8 &&
                              next.number <= 255 &&
                              cg_flags_overwritten(c, original, original + (after - body));
      unsigned materialization_bytes = narrow_immediate ? 2 :
                                       modified || complemented ? 4 : old_bytes;
      if (!reuse(c, facts, dst, next, materialization_bytes)) {
        if (narrow_immediate) {
          cg_emit(c, "movs.n r%u, #%" PRIu32 "\n", dst, next.number);
          cg_emit(c, "@ constant-synthesis: saved %u\n", old_bytes - 2);
        } else if (modified || complemented) {
          cg_emit(c, "%s.w r%u, #%" PRIu32 "\n",
                  complemented ? "mvn" : "mov", dst,
                  complemented ? ~next.number : next.number);
          cg_emit(c, "@ constant-synthesis: saved %u\n", old_bytes - 4);
        } else if (folded_copy) {
          cg_emit(c, "movw r%u, #%" PRIu32 "\n", dst, next.number & 65535u);
          if (old_bytes == 8)
            cg_emit(c, "movt r%u, #%" PRIu32 "\n", dst, next.number >> 16);
        } else {
          copy_line(c, current, next_line);
          if (old_bytes == 8)
            cg_append(c, next_line, (size_t)(after - next_line));
        }
        if (folded_copy) cg_emit(c, "@ constant-copy-fold: saved 2\n");
      }
      facts[dst] = next;
    } else {
      if (synthesis && (!strncmp(current, "bl ", 3) || !strncmp(current, "blx ", 4))) {
        memset(facts, 0, 4 * sizeof(*facts));
        facts[12] = (cg_constant){0};
      } else
        effects(current, facts);
      copy_line(c, current, next_line);
    }
    current = after;
  }
  if (assembly_lines) cg_error(c, "truncated constant selection assembly boundary");
}

static int pool_label(const char *line, const char *name) {
  size_t n = strlen(name);
  return !strncmp(line, name, n) && !strcmp(line + n, ":");
}

static int pool_exits_without_value(char *const *lines, unsigned exit,
                                    unsigned count, unsigned reg) {
  for (unsigned i = exit + 1; i < count; i++) {
    const char *p = lines[i];
    if (!*p || *p == '@' || *p == '.')
      continue;
    if (!strcmp(p, "bx lr"))
      return reg != 0;
    if (!strncmp(p, "pop.", 4) && mentions(p, reg))
      return 1;
    if (!mentions(p, reg))
      continue;
    char op[24];
    unsigned dst;
    int operands = 0;
    if (sscanf(p, "%23s r%u,%n", op, &dst, &operands) != 2 ||
        !operands || dst != reg || mentions(p + operands, reg))
      return 0;
    /* movt, writeback, and other partial writes do not kill the old value. */
    return !strcmp(op, "mov") || !strcmp(op, "mov.w") ||
           !strcmp(op, "movw") || !strcmp(op, "movs.n");
  }
  return 0;
}

typedef struct {
  unsigned header, guard, latch, exit;
  char head_name[128];
} closed_loop;

static int find_closed_loop(cg_context *c, char *const *lines, unsigned count,
                            closed_loop *loop) {
  unsigned latch = count, guard = count;
  char head[128] = "", exit[128] = "";
  for (unsigned i = 0; i < count; i++) {
    const char *p = lines[i];
    if (!*p || *p == '@' || *p == '.' || p[strlen(p) - 1] == ':' || !strcmp(p, "bx lr") ||
        !strncmp(p, "push.", 5) || !strncmp(p, "pop.", 4))
      continue;
    cg_register_work(c);
    char op[24], target[128];
    int end = 0;
    if (sscanf(p, "%23s %127s%n", op, target, &end) == 2 && end && !p[end] &&
        !strncmp(target, ".Lcg_", 5)) {
      char *width = strchr(op, '.');
      if (width) *width = 0;
      if (!strcmp(op, "b") && latch == count) {
        strcpy(head, target);
        latch = i;
        continue;
      }
      if (op[0] == 'b' && cg_inverse_condition(op + 1) && guard == count) {
        strcpy(exit, target);
        guard = i;
        continue;
      }
    }
    cg_constant facts[13] = {{0}};
    for (unsigned r = 0; r < 13; r++) facts[r].known = 1;
    effects(p, facts);
    unsigned known = 0;
    for (unsigned r = 0; r < 13; r++) known += facts[r].known != 0;
    if (!known)
      return 0;
  }
  if (guard >= latch || latch == count)
    return 0;
  unsigned header = 0, done = latch + 1;
  while (header < guard && !pool_label(lines[header], head)) header++;
  while (done < count && !pool_label(lines[done], exit)) done++;
  if (header == guard || done == count)
    return 0;
  for (unsigned i = header + 1; i < latch; i++) {
    const char *p = lines[i];
    if (!strcmp(p, "bx lr") || !strncmp(p, "push.", 5) || !strncmp(p, "pop.", 4))
      return 0;
  }
  *loop = (closed_loop){header, guard, latch, done, ""};
  strcpy(loop->head_name, head);
  return 1;
}

static void hoist_pool_loads(cg_context *c, char *const *lines, unsigned count,
                             const closed_loop *loop, const unsigned *site,
                             const unsigned *regs, unsigned char *hoisted) {
  for (unsigned i = loop->guard + 1; i + 1 < loop->latch; i++) {
    if (!site[i] || regs[i] > 3)
      continue;
    unsigned reg = regs[i];
    unsigned j = loop->header + 1;
    for (; j < i; j++) {
      cg_register_work(c);
      if (*lines[j] != '@' && *lines[j] != '.' && mentions(lines[j], reg))
        break;
    }
    if (j != i || !pool_exits_without_value(lines, loop->exit, count, reg))
      continue;
    /* The only incoming edges were verified above: fallthrough into the
       header and the latch. No iteration may redefine the hoisted register. */
    for (j = i + 2; j < loop->latch; j++) {
      cg_register_work(c);
      const char *p = lines[j];
      if (!*p || *p == '@' || *p == '.')
        continue;
      cg_constant facts[13] = {{0}};
      facts[reg].known = 1;
      effects(p, facts);
      if (!facts[reg].known)
        break;
    }
    if (j == loop->latch)
      hoisted[i] = 1;
  }
}

static int loop_metadata(const char *line) {
  return !*line || *line == '@' || *line == '.';
}

static int countdown_loop(cg_context *c, char *const *lines, unsigned count,
                          const closed_loop *loop, unsigned *init, unsigned *compare,
                          unsigned *step, unsigned *reg, unsigned *trips) {
  *compare = loop->header + 1;
  while (*compare < loop->guard && loop_metadata(lines[*compare])) (*compare)++;
  if (*compare + 1 != loop->guard || strncmp(lines[loop->guard], "bhs.n ", 6))
    return 0;
  int end = 0;
  if (sscanf(lines[*compare], "cmp.n r%u, #%u%n", reg, trips, &end) != 2 ||
      !end || lines[*compare][end] || *reg >= 8 || !*trips || *trips > 255)
    return 0;
  *step = loop->latch - 1;
  while (*step > loop->guard && loop_metadata(lines[*step])) (*step)--;
  unsigned dst, src, increment;
  end = 0;
  if (sscanf(lines[*step], "adds.n r%u, r%u, #%u%n", &dst, &src, &increment, &end) != 3 ||
      !end || lines[*step][end] || dst != *reg || src != *reg || increment != 1)
    return 0;
  *init = loop->header;
  while (*init && !mentions(lines[*init - 1], *reg)) (*init)--;
  if (!*init)
    return 0;
  (*init)--;
  unsigned seed;
  end = 0;
  if (sscanf(lines[*init], "movs.n r%u, #%u%n", &dst, &seed, &end) != 2 ||
      !end || lines[*init][end] || dst != *reg || seed ||
      !pool_exits_without_value(lines, loop->exit, count, *reg))
    return 0;
  unsigned instructions = 0;
  for (unsigned i = loop->guard + 1; i < loop->exit; i++) {
    cg_register_work(c);
    if (!loop_metadata(lines[i])) {
      if (i > loop->latch || (i != *step && mentions(lines[i], *reg)))
        return 0;
      instructions++;
    }
  }
  return instructions <= 60;
}

static const char *rotate_fixed_loop(cg_context *c, char *const *lines,
                                     const closed_loop *loop, unsigned *compare) {
  *compare = loop->header + 1;
  while (*compare < loop->guard && loop_metadata(lines[*compare])) (*compare)++;
  const char *branch = lines[loop->guard];
  int inclusive = !strncmp(branch, "bhi.n ", 6);
  if (*compare + 1 != loop->guard || (!inclusive && strncmp(branch, "bhs.n ", 6)) ||
      loop->latch - loop->header > 60)
    return NULL;
  unsigned reg, bound;
  int end = 0;
  if (sscanf(lines[*compare], "cmp.n r%u, #%u%n", &reg, &bound, &end) != 2 ||
      !end || lines[*compare][end] || reg >= 8 || !bound || bound > 254)
    return NULL;
  unsigned init = loop->header, step = loop->latch - 1;
  while (init && !mentions(lines[init - 1], reg)) init--;
  while (step > loop->guard && loop_metadata(lines[step])) step--;
  unsigned dst, src, seed, increment;
  end = 0;
  if (!init || sscanf(lines[init - 1], "movs.n r%u, #%u%n", &dst, &seed, &end) != 2 ||
      !end || lines[init - 1][end] || dst != reg || seed > bound ||
      (!inclusive && seed == bound))
    return NULL;
  end = 0;
  if (sscanf(lines[step], "adds.n r%u, r%u, #%u%n", &dst, &src, &increment, &end) != 3 ||
      !end || lines[step][end] || dst != reg || src != reg || increment != 1)
    return NULL;
  for (unsigned i = loop->guard + 1; i < step; i++) {
    cg_register_work(c);
    if (!loop_metadata(lines[i]) && mentions(lines[i], reg)) {
      cg_constant facts[13] = {{0}};
      facts[reg].known = 1;
      effects(lines[i], facts);
      if (!facts[reg].known)
        return NULL;
    }
  }
  for (unsigned i = loop->latch + 1; i < loop->exit; i++)
    if (!loop_metadata(lines[i]))
      return NULL;
  return inclusive ? "ls" : "lo";
}

static int loop_uses_register(char *const *lines, unsigned from, unsigned to,
                              unsigned reg) {
  for (unsigned i = from; i < to; i++)
    if (!loop_metadata(lines[i]) && mentions(lines[i], reg))
      return 1;
  return 0;
}

typedef struct { unsigned at, last, state, input, polynomial, scratch, dead; } bit_feedback;

static int plan_bit_feedback(cg_context *c, char *const *lines, unsigned count,
                              const closed_loop *loop, const unsigned char *hoisted,
                              bit_feedback *out) {
  for (unsigned i = loop->guard + 1; i + 6 < loop->latch; i++) {
    cg_register_work(c);
    if (strncmp(lines[i], "eor.w ", 6))
      continue;
    const char *row[6];
    unsigned n = 0, last = i;
    for (; last < loop->latch && n < 6; last++) {
      if (hoisted[last])
        last++;
      else
        row[n++] = lines[last];
    }
    if (n != 6)
      continue;
    unsigned bit, state, input, tmp, polynomial, a, b, d;
    int end[6] = {0};
    if (sscanf(row[0], "eor.w r%u, r%u, r%u%n", &bit, &state, &input, &end[0]) != 3 ||
        sscanf(row[1], "and.w r%u, r%u, #1%n", &a, &b, &end[1]) != 2 ||
        a != bit || b != bit ||
        sscanf(row[2], "lsr.w r%u, r%u, #1%n", &tmp, &b, &end[2]) != 2 ||
        b != state ||
        sscanf(row[3], "rsb.w r%u, r%u, #0%n", &d, &b, &end[3]) != 2 ||
        d != state || b != bit ||
        sscanf(row[4], "and.w r%u, r%u, r%u%n", &d, &b, &polynomial, &end[4]) != 3 ||
        d != state || b != state ||
        sscanf(row[5], "eor.w r%u, r%u, r%u%n", &d, &a, &b, &end[5]) != 3 ||
        d != state || a != tmp || b != state ||
        bit > 12 || state > 12 || input > 12 || tmp > 12 || polynomial > 12)
      continue;
    int complete = 1;
    for (unsigned j = 0; j < 6; j++)
      complete &= end[j] && !row[j][end[j]];
    unsigned regs = (1u << bit) | (1u << state) | (1u << input) |
                    (1u << tmp) | (1u << polynomial);
    if (!complete || __builtin_popcount(regs) != 5)
      continue;
    unsigned overwritten[] = {bit, tmp};
    for (unsigned j = 0; j < 2 && complete; j++) {
      unsigned r = overwritten[j];
      complete = !loop_uses_register(lines, loop->header + 1, i, r) &&
                 !loop_uses_register(lines, last, loop->latch, r) &&
                 pool_exits_without_value(lines, loop->exit, count, r);
    }
    if (!complete)
      continue;
    unsigned scratch = bit < 8 ? bit : tmp < 8 ? tmp : 0;
    if (bit >= 8 && tmp >= 8 &&
        ((regs & 1) || loop_uses_register(lines, loop->header + 1, i, 0) ||
         loop_uses_register(lines, last, loop->latch, 0) ||
         !pool_exits_without_value(lines, loop->exit, count, 0)))
      continue;
    *out = (bit_feedback){i, last - 1, state, input, polynomial, scratch,
                          ((1u << bit) | (1u << tmp)) & ~(1u << scratch)};
    return 1;
  }
  return 0;
}

typedef struct { unsigned frame, metadata, push, pop, mask; } feedback_frame;

static int trim_feedback_frame(cg_context *c, char *const *lines, unsigned count,
                               const bit_feedback *feedback, feedback_frame *out) {
  if (c->fn->frame || c->fn->argc > 4 || !(feedback->dead & 0x0ff0u))
    return 0;
  for (unsigned k = 0; k < c->fn->argc; k++)
    if (c->fn->byval[k] || c->fn->args[k]->type->size > 4)
      return 0;
  feedback_frame f = {count, count, count, count, 0};
  unsigned mask = 0, saved;
  for (unsigned i = 0; i < count; i++) {
    const char *p = lines[i];
    int end = 0;
    if (!strncmp(p, "@ stack-frame: ", 15) &&
        sscanf(p, "@ stack-frame: 0 + %u saved-register bytes%n", &saved, &end) == 1 &&
        end && !p[end] && saved == c->register_saved_bytes)
      f.frame = i;
    else if (!strncmp(p, "@ lightweight-frame: ", 21) &&
             sscanf(p, "@ lightweight-frame: mask %u base sp calls 0%n", &mask, &end) == 1 &&
             end && !p[end])
      f.metadata = i;
    else if (!strncmp(p, "push.", 5) && f.push == count && !strstr(p, "lr"))
      f.push = i;
    else if (!strncmp(p, "pop.", 4) && f.pop == count && !strstr(p, "pc"))
      f.pop = i;
    else if (!loop_metadata(p) && strstr(p, "sp"))
      return 0;
  }
  if (f.frame == count || f.metadata >= f.push || f.push >= feedback->at ||
      f.pop <= feedback->last || f.pop == count || (mask & ~0x0ff0u) ||
      (unsigned)__builtin_popcount(mask) * 4 != c->register_saved_bytes)
    return 0;
  unsigned dead = mask & feedback->dead;
  for (unsigned i = 0; dead && i < count; i++) {
    if (i == f.push || i == f.pop || (i >= feedback->at && i <= feedback->last) ||
        loop_metadata(lines[i]))
      continue;
    for (unsigned r = 4; r < 12; r++)
      if ((dead & (1u << r)) && mentions(lines[i], r))
        dead &= ~(1u << r);
  }
  if (!dead)
    return 0;
  f.mask = mask & ~dead;
  *out = f;
  return 1;
}

static unsigned before_instruction(char *const *lines, unsigned i) {
  while (i && loop_metadata(lines[--i])) {}
  return i;
}

static int plan_feedback_return(cg_context *c, char *const *lines, unsigned count,
                                unsigned exit, unsigned *invert, unsigned *move,
                                unsigned *source) {
  if (c->fn->ret->bits != 32)
    return 0;
  unsigned last = before_instruction(lines, count);
  if (last <= exit || strcmp(lines[last], "bx lr"))
    return 0;
  *move = before_instruction(lines, last);
  if (!strncmp(lines[*move], "pop.", 4))
    *move = before_instruction(lines, *move);
  *invert = before_instruction(lines, *move);
  int end = 0;
  if (*invert <= exit ||
      sscanf(lines[*move], "mov r0, r%u%n", source, &end) != 1 ||
      !end || lines[*move][end] || *source >= 8)
    return 0;
  unsigned dst, value;
  end = 0;
  return sscanf(lines[*invert], "eor.w r%u, r%u, #4294967295%n",
                &dst, &value, &end) == 2 && end && !lines[*invert][end] &&
         dst == *source && value == *source;
}

typedef struct { unsigned compare, guard, shift; } tail_shift;

static int plan_tail_shift(cg_context *c, const char *source, const char *body,
                            char *const *lines, unsigned count, tail_shift *out) {
  for (unsigned guard = 2; guard < count; guard++) {
    if (strncmp(lines[guard], "bhs.n .Lcg_", 11))
      continue;
    unsigned compare = guard - 1, header = compare - 1;
    /* Work backwards over comments, but retain the actual header label. */
    while (header && *lines[header] == '@') header--;
    if (strncmp(lines[header], ".Lcg_", 5))
      continue;
    unsigned reg, bound;
    int end = 0;
    if (sscanf(lines[compare], "cmp.n r%u, #%u%n", &reg, &bound, &end) != 2 ||
        !end || lines[compare][end] || reg >= 8 || !bound || bound > 254)
      continue;
    unsigned exit = guard + 1;
    while (exit < count && !pool_label(lines[exit], lines[guard] + 6)) exit++;
    if (exit == count || exit - header > 60)
      continue;
    unsigned latch = before_instruction(lines, exit);
    unsigned shift = before_instruction(lines, latch), step = before_instruction(lines, shift);
    if (step <= guard || strncmp(lines[latch], "b .Lcg_", 7) ||
        !pool_label(lines[header], lines[latch] + 2) ||
        !sole_label_entry(c, source, lines[latch] + 2,
                          source + (lines[header] - body), source + (lines[latch] - body) + 2))
      continue;
    unsigned dst, src, n, shifted;
    end = 0;
    if (sscanf(lines[step], "adds.n r%u, r%u, #%u%n", &dst, &src, &n, &end) != 3 ||
        !end || lines[step][end] || dst != reg || src != reg || n != 1)
      continue;
    end = 0;
    if (sscanf(lines[shift], "lsrs.n r%u, r%u, #%u%n", &shifted, &src, &n, &end) != 3 ||
        !end || lines[shift][end] || shifted >= 8 || src != shifted || shifted == reg ||
        !n || n > 32 || !pool_exits_without_value(lines, exit, count, shifted))
      continue;
    int safe = 1;
    for (unsigned i = step + 1; i < exit; i++)
      if (i != shift && i != latch && *lines[i] && *lines[i] != '@')
        safe = 0;
    for (unsigned i = exit + 1; i < count; i++)
      if (!loop_metadata(lines[i]) &&
          ((lines[i][0] == 'b' && strcmp(lines[i], "bx lr")) ||
           !strncmp(lines[i], "cb", 2)))
        safe = 0;
    unsigned init = header;
    while (init && !mentions(lines[init - 1], reg)) init--;
    end = 0;
    if (!safe || !init ||
        sscanf(lines[init - 1], "movs.n r%u, #%u%n", &dst, &n, &end) != 2 ||
        !end || lines[init - 1][end] || dst != reg || n >= bound ||
        !cg_flags_overwritten(c, source, source + (lines[guard + 1] - body)))
      continue;
    for (unsigned i = 0; i < header; i++)
      if (!loop_metadata(lines[i]) && (lines[i][0] == 'b' || !strncmp(lines[i], "cb", 2)))
        safe = 0;
    for (unsigned i = guard + 1; safe && i < step; i++) {
      cg_register_work(c);
      const char *p = lines[i];
      if (loop_metadata(p))
        continue;
      if (*p == 'b' || !strncmp(p, "cb", 2)) {
        const char *target = strrchr(p, ' ');
        unsigned j = i + 1;
        while (target && j < step && !pool_label(lines[j], target + 1)) j++;
        if (j == step || !target)
          safe = 0;
      } else {
        cg_constant facts[13] = {{0}};
        facts[reg].known = 1;
        effects(p, facts);
        if (!facts[reg].known)
          safe = 0;
      }
    }
    if (safe) {
      *out = (tail_shift){compare, guard, shift};
      return 1;
    }
  }
  return 0;
}

void cg_pool_literals(cg_context *c, size_t start) {
  enum { MAX_LINES = 920, MAX_NARROW_LINES = 220, MAX_LITERALS = 32 };
  const char *source = c->out + start;
  if (strstr(source, "@ constant-assembly-lines:") || strstr(source, "\nit"))
    return;
  char *body = cg_alloc(c, c->size - start + 1);
  memcpy(body, source, c->size - start);
  char *lines[MAX_LINES];
  unsigned count = 0;
  for (char *p = body; *p;) {
    cg_register_work(c);
    if (count == MAX_LINES)
      return;
    lines[count++] = p;
    p = line(p);
  }
  /* At most four bytes per line, plus the aligned pool: 220*4+32*4+2
     fits Thumb-16 literal reach even for the earliest load. */
  int narrow_reach = count <= MAX_NARROW_LINES;
  struct { const char *expression; unsigned uses, low_uses; } entries[MAX_LITERALS] = {{0}};
  unsigned entry_count = 0;
  unsigned site[MAX_LINES] = {0}, regs[MAX_LINES];
  for (unsigned i = 0; i + 1 < count; i++) {
    const char *p = lines[i];
    if (*p == '.' && strncmp(p, ".type ", 6) && strncmp(p, ".global ", 8) &&
        strcmp(p, ".thumb_func") && (i || strncmp(p, ".balign ", 8)) &&
        p[strlen(p) - 1] != ':')
      return;
    unsigned lower, upper;
    const char *expr = symbol(p, "movw", &lower);
    if (expr) {
      const char *high = symbol(lines[i + 1], "movt", &upper);
      if (!high || lower != upper || strcmp(expr, high))
        continue;
    } else {
      uint32_t lo, hi;
      if (!narrow_reach || !immediate(p, "movw", &lower, &lo) || lower >= 8 ||
          !immediate(lines[i + 1], "movt", &upper, &hi) || lower != upper)
        continue;
      char *decimal = cg_alloc(c, 12);
      snprintf(decimal, 12, "%" PRIu32 ")", (hi << 16) | lo);
      expr = decimal;
    }
    unsigned k = 0;
    while (k < entry_count && strcmp(expr, entries[k].expression))
      k++;
    if (k == entry_count) {
      if (k == MAX_LITERALS)
        continue;
      entries[k].expression = expr;
      entry_count++;
    }
    entries[k].uses++;
    entries[k].low_uses += narrow_reach && lower < 8;
    site[i] = k + 1;
    regs[i++] = lower;
  }
  unsigned pooled = 0;
  for (unsigned k = 0; k < entry_count; k++)
    pooled += entries[k].uses > 1 || entries[k].low_uses;
  unsigned char hoisted[MAX_LINES] = {0};
  closed_loop loop = {0};
  int closed = narrow_reach && find_closed_loop(c, lines, count, &loop);
  unsigned init, compare, step, reg, trips;
  int countdown = closed && countdown_loop(c, lines, count, &loop, &init, &compare,
                                            &step, &reg, &trips);
  const char *rotate = closed && !countdown ? rotate_fixed_loop(c, lines, &loop, &compare) : NULL;
  if (closed && pooled)
    hoist_pool_loads(c, lines, count, &loop, site, regs, hoisted);
  bit_feedback feedback = {0};
  /* The countdown kills the shift flags before any control transfer. */
  int fuse_feedback = countdown && plan_bit_feedback(c, lines, count, &loop, hoisted, &feedback);
  feedback_frame frame = {0};
  int trim = fuse_feedback && trim_feedback_frame(c, lines, count, &feedback, &frame);
  unsigned invert = 0, move = 0, complement = 0;
  int direct_feedback_return = fuse_feedback &&
      plan_feedback_return(c, lines, count, loop.exit, &invert, &move, &complement);
  tail_shift tail = {0};
  int fold_tail = narrow_reach && !closed && plan_tail_shift(c, source, body, lines, count, &tail);
  if (!pooled && !countdown && !rotate && !fold_tail)
    return;
  c->size = start;
  for (unsigned i = 0; i < count; i++) {
    if (trim) {
      if (i == frame.frame) {
        cg_emit(c, "@ stack-frame: 0 + %u saved-register bytes\n",
                (unsigned)__builtin_popcount(frame.mask) * 4);
        continue;
      }
      if (i == frame.metadata) {
        cg_emit(c, "@ lightweight-frame: mask %u base sp calls 0\n", frame.mask);
        continue;
      }
      if (i == frame.push || i == frame.pop) {
        if (frame.mask)
          cg_saved_register_list(c, frame.mask, i == frame.pop);
        continue;
      }
    }
    if (closed && i == loop.header)
      for (unsigned j = loop.header + 1; j < count; j++)
        if (hoisted[j])
          cg_emit(c, "ldr.n r%u, .Lcg_shared_%u_%u\n@ loop-pool: materialized before header\n",
                  regs[j], c->fn->id, site[j]);
    if (countdown) {
      if (i == init) {
        cg_emit(c, "movs.n r%u, #%u\n", reg, trips);
        continue;
      }
      if (i == compare || i == loop.guard)
        continue;
      if (i == step) {
        cg_emit(c, "subs.n r%u, r%u, #1\n", reg, reg);
        continue;
      }
      if (i == loop.latch) {
        cg_emit(c, "bne.n %s\n@ loop-countdown: fixed positive trip count\n", loop.head_name);
        continue;
      }
    }
    if (rotate) {
      if (i == compare || i == loop.guard)
        continue;
      if (i == loop.latch) {
        cg_emit(c, "%s\nb%s.n %s\n@ loop-rotation: positive trip count\n",
                lines[compare], rotate, loop.head_name);
        continue;
      }
    }
    if (fuse_feedback && i == feedback.at) {
      unsigned scratch = feedback.scratch, state = feedback.state;
      cg_emit(c, "eor.w r%u, r%u, r%u\nlsls.n r%u, r%u, #31\n"
              "asrs.n r%u, r%u, #31\nand.w r%u, r%u, r%u\n"
              "eor.w r%u, r%u, r%u, lsr #1\n@ bit-feedback: branchless shifted xor\n",
              scratch, state, feedback.input, scratch, scratch, scratch, scratch,
              scratch, scratch, feedback.polynomial, state, scratch, state);
      i = feedback.last;
      continue;
    }
    if (direct_feedback_return) {
      if (i == invert) {
        cg_emit(c, "mvns.n r0, r%u\n@ bit-feedback: complement into return register\n",
                complement);
        continue;
      }
      if (i == move)
        continue;
    }
    if (fold_tail) {
      if (i == tail.compare || i == tail.guard)
        continue;
      if (i == tail.shift)
        cg_emit(c, "%s\n%s\n@ loop-tail-shift: skipped on exit\n",
                lines[tail.compare], lines[tail.guard]);
    }
    unsigned k = site[i];
    if (k && (entries[k - 1].uses > 1 || entries[k - 1].low_uses)) {
      char width = narrow_reach && regs[i] < 8 ? 'n' : 'w';
      if (!hoisted[i])
        cg_emit(c, "ldr.%c r%u, .Lcg_shared_%u_%u\n", width, regs[i], c->fn->id, k);
      i++;
    } else
      cg_emit(c, "%s\n", lines[i]);
  }
  if (pooled) cg_emit(c, ".p2align 2\n");
  for (unsigned k = 0; k < entry_count; k++)
    if (entries[k].uses > 1 || entries[k].low_uses) {
      const char *expr = entries[k].expression;
      cg_emit(c, ".Lcg_shared_%u_%u:\n.word %.*s\n", c->fn->id, k + 1,
              (int)strlen(expr) - 1, expr);
    }
}
