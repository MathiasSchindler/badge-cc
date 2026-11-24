#include "codegen_internal.h"
#include <string.h>

static int hex(unsigned char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}
static char *decode(cg_context *c, const char *text, const unsigned *registers,
                    unsigned register_count) {
  size_t length = strlen(text);
  if (length < 2 || text[0] != '"' || text[length - 1] != '"')
    cg_error(c, "invalid inline assembly string");
  char *decoded = cg_alloc(c, length);
  unsigned count = 0;
  for (size_t k = 1; k + 1 < length; k++) {
    cg_register_work(c);
    unsigned ch = (unsigned char)text[k];
    if (ch == '\\') {
      if (k + 3 >= length || hex(text[k + 1]) < 0 || hex(text[k + 2]) < 0)
        cg_error(c, "invalid inline assembly escape");
      ch = (unsigned)(hex(text[k + 1]) * 16 + hex(text[k + 2]));
      k += 2;
    }
    if (!ch) cg_error(c, "NUL in inline assembly");
    if (ch == '$' && k + 2 < length && text[k + 1] == '$') k++;
    else if (ch == '$' && register_count) {
      if (k + 2 >= length || text[k + 1] < '0' ||
          (unsigned)(text[k + 1] - '0') >= register_count ||
          (text[k + 2] >= '0' && text[k + 2] <= '9'))
        cg_error(c, "unsupported inline assembly operand reference");
      decoded[count++] = 'r';
      ch = '0' + registers[(unsigned)(text[++k] - '0')];
    }
    decoded[count++] = (char)ch;
  }
  decoded[count] = 0;
  return decoded;
}
static int nops(cg_context *c, const char *text) {
  unsigned count = 0;
  const char *line = text;
  while (*line) {
    const char *end = line;
    while (*end && *end != '\n') {
      cg_register_work(c);
      end++;
    }
    const char *first = line, *last = end;
    while (first < last && (*first == ' ' || *first == '\t' || *first == '\r')) first++;
    while (last > first && (last[-1] == ' ' || last[-1] == '\t' || last[-1] == '\r')) last--;
    if (first != last) {
      if (last - first != 3 || memcmp(first, "nop", 3)) return 0;
      count++;
    }
    line = *end ? end + 1 : end;
  }
  return count != 0;
}
cg_assembly *cg_instruction_assembly(cg_context *c, cg_inst *i) {
  if (i->assembly_checked) return i->assembly;
  i->assembly_checked = 1;
  unsigned at = i->begin + (i->result ? 2 : 0);
  if (!strcmp(c->tokens[at].text, "tail") || !strcmp(c->tokens[at].text, "notail")) at++;
  if (strcmp(c->tokens[at].text, "call")) return NULL;
  unsigned k = at + 1;
  while (k < i->end && strcmp(c->tokens[k].text, "asm")) {
    cg_register_work(c);
    k++;
  }
  if (k == i->end) return NULL;
  k++;
  if (k < i->end && !strcmp(c->tokens[k].text, "sideeffect")) k++;
  if (k + 4 >= i->end || strcmp(c->tokens[k + 1].text, ",") ||
      strcmp(c->tokens[k + 3].text, "("))
    cg_error(c, "unsupported inline assembly operands/modifiers");
  cg_assembly *a = cg_alloc(c, sizeof(*a));
  char *constraints = decode(c, c->tokens[k + 2].text, NULL, 0);
  const char *p = constraints;
  unsigned registers[5], register_count = 0, tied = 0;
  int clobbers = 0;
  while (*p) {
    cg_register_work(c);
    if (!strncmp(p, "~{memory}", 9)) {
      clobbers = 1;
      a->memory_barrier = 1;
      p += 9;
    } else if (!strncmp(p, "~{cc}", 5)) {
      clobbers = 1;
      a->flags_write = 1;
      p += 5;
    } else if (!register_count && !strncmp(p, "~{r", 3) && p[3] >= '0' && p[3] <= '3' && p[4] == '}') {
      clobbers = 1;
      a->writes |= 1u << (p[3] - '0');
      p += 5;
    } else if (!register_count && c->fn && c->fn->naked && !strncmp(p, "~{r12}", 6)) {
      clobbers = 1;
      a->writes |= 1u << 12;
      p += 6;
    } else if (*p == '=' && register_count == a->output && a->output < 2 && !clobbers) {
      p++;
      if (*p == '&') p++;
      if (*p != 'r' && *p != 'l')
        cg_error(c, "unsupported inline assembly output constraint");
      p++;
      a->output++;
      registers[register_count] = register_count;
      register_count++;
    } else if (!clobbers && (*p == 'r' || *p == 'l')) {
      if (tied || register_count == 3)
        cg_error(c, "inline assembly register operand limit exceeded");
      p++;
      a->input_registers[a->input_count++] = register_count;
      registers[register_count] = register_count;
      register_count++;
    } else if (!clobbers && *p >= '0' && (unsigned)(*p - '0') < a->output) {
      unsigned reg = (unsigned)(*p++ - '0');
      if (a->input_count == 3 || (tied & (1u << reg)))
        cg_error(c, "invalid tied inline assembly input");
      tied |= 1u << reg;
      a->input_registers[a->input_count++] = reg;
      registers[register_count] = reg;
      register_count++;
    } else cg_error(c, "unsupported inline assembly constraint or clobber");
    if (*p && *p != ',') cg_error(c, "unsupported inline assembly clobber");
    if (*p == ',' && !*++p) cg_error(c, "invalid inline assembly clobber list");
  }
  if ((a->output != 0) != (i->result != NULL))
    cg_error(c, "inline assembly output constraint does not match the result");
  a->text = decode(c, c->tokens[k].text, registers, register_count);
  a->known = !register_count && nops(c, a->text);
  if (!a->known) {
    a->reads = a->writes = 0xffffu;
    a->flags_read = a->flags_write = 1;
    a->memory_read = a->memory_write = a->control = 1;
  }
  i->assembly = a;
  return a;
}
