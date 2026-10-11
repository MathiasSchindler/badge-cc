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
static int immediate(const char *text, const char *op, unsigned *reg,
                     uint32_t *number) {
  char instruction[16];
  int end = 0;
  return sscanf(text, "%15s r%u, #%" SCNu32 "%n", instruction, reg, number, &end) == 3 &&
         !strcmp(op, instruction) && !text[end] && *reg <= 12 && *number <= 65535;
}
static char *symbol(const char *text, const char *op, unsigned *reg) {
  char instruction[16], part[16];
  int start = 0;
  if (sscanf(text, "%15s r%u, #:%15[^:]:%n", instruction, reg, part, &start) != 3 ||
      strcmp(instruction, op) || *reg > 12 ||
      strcmp(part, !strcmp(op, "movw") ? "lower16" : "upper16") ||
      text[start] != '(')
    return NULL;
  return (char *)text + start + 1;
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
  if (!strcmp(op, "str") || !strcmp(op, "str.w") || !strcmp(op, "str.n") ||
      !strcmp(op, "strb") || !strcmp(op, "strb.w") || !strcmp(op, "strb.n") ||
      !strcmp(op, "strh") || !strcmp(op, "strh.w") || !strcmp(op, "strh.n") ||
      !strcmp(op, "cmp") || !strcmp(op, "cmp.w") || !strcmp(op, "cmp.n")) return;
  const char *writes[] = {"mov", "mov.w", "movs.n", "movw", "movt", "add.w", "add.n", "addw",
    "sub.w", "subw", "subs.w", "mul", "mla", "mls", "udiv", "sdiv",
    "and.w", "orr.w", "eor.w", "lsl.w", "lsr.w", "asr.w", "rsb.w",
    "ubfx", "sxtb", "sxth", "uxtb.n", "uxth.n",
    "ldr", "ldr.w", "ldr.n", "ldrb", "ldrb.w", "ldrb.n", "ldrh", "ldrh.w", "ldrh.n"};
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
static int flags_overwritten(cg_context *c, const char *body, const char *text) {
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
        !strcmp(opcode, "cmp.n") || !strcmp(opcode, "cmp.w") ||
        !strcmp(opcode, "adds.n") || !strcmp(opcode, "adds.w") ||
        !strcmp(opcode, "subs.n") || !strcmp(opcode, "subs.w")) return 1;
    instructions++;
    if (!strcmp(opcode, "b") || !strcmp(opcode, "b.n") || !strcmp(opcode, "b.w")) {
      char target[128];
      int consumed = 0;
      if (sscanf(instruction, "%23s %127s%n", opcode, target, &consumed) != 2 ||
          instruction[consumed] || strncmp(target, ".Lcg_", 5))
        return 0;
      text = find_label(c, body, target);
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
      "ubfx", "uxtb.n", "uxth.n", "and.w", "orr.w", "eor.w", "lsl.w", "lsr.w", "asr.w", "nop"};
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
static int short_branch_span(cg_context *c, const char *from, const char *to) {
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
    if (bytes > 240) return 0;
    from = end + 1;
  }
  return 1;
}
void cg_reduce_branches(cg_context *c, size_t body_start) {
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
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
    else if (sscanf(p, "%11s %127s%n", opcode, edge, &consumed) == 2 &&
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
        const char *destination = find_label(c, body, target);
        if ((size_t)(edge_end - edge_line) == strlen(label) &&
            !memcmp(edge_line, label, strlen(label)) && destination &&
            short_branch_span(c, p, destination)) {
          cg_emit(c, "b%s.n %s\n@ branch-selection: saved 2\n",
                  cg_inverse_condition(opcode + 1), target);
          p = edge_line;
          continue;
        }
      }
    }
    cg_emit(c, "%.*s\n", (int)(end - p), p);
    p = end + 1;
  }
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
      if (sscanf(p, "%11s r3, r%u, #%u%n", opcode, &source, &immediate_value, &consumed) == 3 &&
          !p[consumed] && source <= 12 &&
          (!strcmp(opcode, "lsr.w") || !strcmp(opcode, "lsl.w") ||
           !strcmp(opcode, "asr.w") || !strcmp(opcode, "and.w"))) {
        char *move_end = strchr(next, '\n');
        char *call = move_end ? move_end + 1 : NULL;
        char *call_end = call ? strchr(call, '\n') : NULL;
        if (move_end && call_end &&
            (size_t)(move_end - next) == strlen("mov r0, r3") &&
            !memcmp(next, "mov r0, r3", (size_t)(move_end - next)) &&
            !strncmp(call, "bl ", 3) &&
            !strncmp(call_end + 1, "@ scalar-call: arguments 1\n", 27)) {
          cg_emit(c, "%s r0, r%u, #%u\n%.*s\n@ scalar-call-forwarding: saved 2\n",
                  opcode, source, immediate_value, (int)(call_end - call), call);
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
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
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
    else if (sscanf(p, "cmp.n r%u, #0%n", &reg, &consumed) == 1 &&
             p + consumed == end && reg < 8) {
      const char *branch = end + 1, *branch_end = strchr(branch, '\n');
      char opcode[12], target[128];
      consumed = 0;
      if (branch_end &&
          sscanf(branch, "%11s %127s%n", opcode, target, &consumed) == 2 &&
          branch + consumed == branch_end &&
          (!strcmp(opcode, "beq.n") || !strcmp(opcode, "bne.n"))) {
        const char *destination = find_label(c, body, target);
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
            flags_overwritten(c, body, branch_end + 1) &&
            flags_overwritten(c, body, destination)) {
          cg_emit(c, "%s r%u, %s\n@ zero-branch: saved 2\n",
                  opcode[1] == 'e' ? "cbz" : "cbnz", reg, target);
          p = branch_end + 1;
          continue;
        }
      }
    }
    cg_emit(c, "%.*s\n", (int)(end - p), p);
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
           !strcmp(op, "ldr.n") || !strcmp(op, "ldr.w")) &&
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
void cg_fold_memory_addresses(cg_context *c, size_t body_start) {
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
  c->size = body_start;
  unsigned assembly_lines = 0;
  for (const char *p = body; p < body + length;) {
    cg_register_work(c);
    const char *end = strchr(p, '\n');
    if (!end) cg_error(c, "unterminated memory address instruction");
    unsigned dst, base, index, shift;
    int consumed = 0;
    if (assembly_lines) assembly_lines--;
    else if (!strncmp(p, "@ constant-assembly-lines:", 26))
      sscanf(p, "@ constant-assembly-lines: %u", &assembly_lines);
    else if (sscanf(p, "add.w r%u, r%u, r%u, lsl #%u%n",
                    &dst, &base, &index, &shift, &consumed) == 4 &&
             p + consumed == end && dst <= 12 && base <= 12 && index <= 12 &&
             shift <= 3 && dst != base && dst != index) {
      const char *memory = end + 1;
      for (unsigned n = 0; n < 16 && *memory; n++) {
        const char *finish = strchr(memory, '\n');
        if (!finish || finish - memory >= 256) break;
        char instruction[256], op[24];
        memcpy(instruction, memory, (size_t)(finish - memory));
        instruction[finish - memory] = 0;
        unsigned value, address;
        int used = 0;
        if (sscanf(instruction, "%23s r%u, [r%u]%n", op, &value, &address, &used) == 3 &&
            !instruction[used] && address == dst && value <= 12 && value != dst &&
            (!strcmp(op, "str.n") || !strcmp(op, "str.w") ||
             !strcmp(op, "ldr.n") || !strcmp(op, "ldr.w")) &&
            overwritten_before_use(c, finish + 1, dst)) {
          cg_emit(c, "%.*s", (int)(memory - end - 1), end + 1);
          cg_emit(c, "%s.w r%u, [r%u, r%u, lsl #%u]\n@ memory-address-fold: saved %u\n",
                  op[0] == 's' ? "str" : "ldr", value, base, index, shift,
                  !strcmp(op + 3, ".n") ? 2u : 4u);
          p = finish + 1;
          break;
        }
        if (*instruction != '@' && *instruction) {
          cg_constant facts[13] = {{0}};
          facts[base].known = facts[index].known = 1;
          effects(instruction, facts);
          if (mentions(instruction, dst) || !facts[base].known || !facts[index].known)
            break;
        } else if (!strncmp(instruction, "@ assembly-effects:", 19) ||
                   !strncmp(instruction, "@ constant-assembly-lines:", 26) ||
                   !strncmp(instruction, "@ frame-incoming:", 17)) break;
        memory = finish + 1;
      }
      if (p > end) continue;
    }
    cg_emit(c, "%.*s\n", (int)(end - p), p);
    p = end + 1;
  }
}
void cg_reuse_constants(cg_context *c, size_t body_start) {
  int synthesis = c->scalar_selection == BADGE_SELECTION_THUMB_CONSTANTS;
  for (cg_block *b = c->fn->blocks; b; b = b->next)
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_assembly *a = cg_instruction_assembly(c, i);
      if (!synthesis && a && !a->known) return;
    }
  size_t length = c->size - body_start;
  char *body = cg_alloc(c, length + 1);
  memcpy(body, c->out + body_start, length);
  char *original = synthesis ? cg_alloc(c, length + 1) : body;
  if (synthesis) memcpy(original, body, length);
  c->size = body_start;
  cg_constant facts[13] = {{0}};
  unsigned assembly_lines = 0;
  char *current = body;
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
      cg_emit(c, "%s\n", current);
      assembly_lines--;
      current = next_line;
      continue;
    }
    if (synthesis && !strncmp(current, "@ constant-assembly-lines:", 26)) {
      int end = 0;
      if (sscanf(current, "@ constant-assembly-lines: %u%n", &assembly_lines, &end) != 1 ||
          current[end] || !assembly_lines)
        cg_error(c, "invalid constant selection assembly boundary");
      cg_emit(c, "%s\n", current);
      current = next_line;
      continue;
    }
    if (synthesis) {
      char opcode[12];
      unsigned dst, src, number;
      int consumed = 0;
      if (sscanf(current, "%11s r%u, r%u, #%u%n",
                 opcode, &dst, &src, &number, &consumed) == 4 &&
          !current[consumed] && dst < 7 && src < 7) {
        if (!strcmp(opcode, "and.w") && (number == 255 || number == 65535)) {
          cg_emit(c, "%s.n r%u, r%u\n@ scalar-extract: saved 2\n",
                  number == 255 ? "uxtb" : "uxth", dst, src);
          facts[dst].known = 0;
          current = next_line;
          continue;
        }
        if (((!strcmp(opcode, "lsr.w") || !strcmp(opcode, "asr.w")) &&
             number >= 1 && number <= 32) ||
            (!strcmp(opcode, "lsl.w") && number <= 31)) {
          if (flags_overwritten(c, original, original + (next_line - body))) {
            cg_emit(c, "%.*ss.n r%u, r%u, #%u\n@ scalar-shift: saved 2\n",
                    3, opcode, dst, src, number);
            facts[dst].known = 0;
            current = next_line;
            continue;
          }
        }
      }
      consumed = 0;
      /* Frame finalization may replace r7 with sp. */
      unsigned right;
      if (sscanf(current, "%11s r%u, r%u, r%u%n",
                 opcode, &dst, &src, &right, &consumed) == 4 &&
          !current[consumed] && dst < 7 && src < 7 && right < 7 &&
          (!strcmp(opcode, "add.w") || !strcmp(opcode, "sub.w")) &&
          flags_overwritten(c, original, original + (next_line - body))) {
        cg_emit(c, "%ss.n r%u, r%u, r%u\n@ scalar-width: saved 2\n",
                opcode[0] == 'a' ? "add" : "sub", dst, src, right);
        facts[dst].known = 0;
        current = next_line;
        continue;
      }
      consumed = 0;
      if (sscanf(current, "%11s r%u, r%u, #%u%n",
                 opcode, &dst, &src, &number, &consumed) == 4 &&
          !current[consumed] && dst < 7 && src < 7 &&
          (number <= 7 || (dst == src && number <= 255)) &&
          (!strcmp(opcode, "add.w") || !strcmp(opcode, "sub.w") ||
           !strcmp(opcode, "adds.w") || !strcmp(opcode, "subs.w"))) {
        int sets_flags = !strcmp(opcode, "adds.w") || !strcmp(opcode, "subs.w");
        if (sets_flags || flags_overwritten(c, original, original + (next_line - body))) {
          cg_emit(c, "%ss.n r%u, r%u, #%u\n@ scalar-width: saved 2\n",
                  opcode[0] == 'a' ? "add" : "sub", dst, src, number);
          facts[dst].known = 0;
          current = next_line;
          continue;
        }
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
                              flags_overwritten(c, original, original + (after - body));
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
          cg_emit(c, "%s\n", current);
          if (old_bytes == 8)
            cg_emit(c, "%.*s\n", (int)(after - next_line - 1), next_line);
        }
        if (folded_copy) cg_emit(c, "@ constant-copy-fold: saved 2\n");
      }
      facts[dst] = next;
    } else {
      effects(current, facts);
      cg_emit(c, "%s\n", current);
    }
    current = after;
  }
  if (assembly_lines) cg_error(c, "truncated constant selection assembly boundary");
}
