#include "codegen_internal.h"
#include <string.h>

static int hex(unsigned char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}
static char *decode(cg_context *c, const char *text) {
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
  if (i->result || k + 4 >= i->end || strcmp(c->tokens[k + 1].text, ",") ||
      strcmp(c->tokens[k + 3].text, "(") || strcmp(c->tokens[k + 4].text, ")"))
    cg_error(c, "unsupported inline assembly operands/modifiers");
  cg_assembly *a = cg_alloc(c, sizeof(*a));
  a->text = decode(c, c->tokens[k].text);
  char *constraints = decode(c, c->tokens[k + 2].text);
  const char *p = constraints;
  while (*p) {
    cg_register_work(c);
    if (!strncmp(p, "~{memory}", 9)) {
      a->memory_barrier = 1;
      p += 9;
    } else if (!strncmp(p, "~{cc}", 5)) {
      a->flags_write = 1;
      p += 5;
    } else
      cg_error(c, "unsupported inline assembly clobber");
    if (*p && *p != ',') cg_error(c, "unsupported inline assembly clobber");
    if (*p == ',' && !*++p) cg_error(c, "invalid inline assembly clobber list");
  }
  a->known = nops(c, a->text);
  if (!a->known) {
    a->reads = a->writes = 0xffffu;
    a->flags_read = a->flags_write = 1;
    a->memory_read = a->memory_write = a->control = 1;
  }
  i->assembly = a;
  return a;
}
