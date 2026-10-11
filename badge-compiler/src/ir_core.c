#define _POSIX_C_SOURCE 200809L
#include "ir_pass.h"
#include "ir_analysis.h"
#include "target.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IR_MEMORY_LIMIT (128u * 1024u * 1024u)
#define IR_CHUNK_SIZE (64u * 1024u)
struct ir_chunk {
  struct ir_chunk *next;
  size_t capacity, used;
  _Alignas(max_align_t) unsigned char bytes[];
};
struct ir_allocation {
  struct ir_allocation *next;
  ir_chunk *chunk;
  size_t memory, end;
  _Alignas(max_align_t) unsigned char bytes[];
};
_Static_assert(sizeof(ir_allocation) % _Alignof(max_align_t) == 0,
               "IR allocation header must preserve alignment");
_Static_assert(sizeof(ir_chunk) % _Alignof(max_align_t) == 0,
               "IR chunk header must preserve alignment");
_Noreturn void ir_error(ir_context *c, const char *message) {
  fprintf(stderr, "badge-opt: %s:%u: %s\n", c->file, c->line, message);
  longjmp(c->failure, 1);
}
void *ir_allocate(ir_context *c, size_t size) {
  if (c->memory > IR_MEMORY_LIMIT || size > IR_MEMORY_LIMIT - c->memory)
    ir_error(c, "IR allocation limit exceeded");
  size_t alignment = _Alignof(max_align_t);
  size_t span = sizeof(ir_allocation) + size;
  span += (alignment - span % alignment) % alignment;
  ir_chunk *chunk = c->chunks;
  if (!chunk || span > chunk->capacity - chunk->used) {
    size_t capacity = span > IR_CHUNK_SIZE ? span : IR_CHUNK_SIZE;
    chunk = malloc(sizeof(*chunk) + capacity);
    if (!chunk)
      ir_error(c, "out of memory");
    *chunk = (ir_chunk){.next = c->chunks, .capacity = capacity};
    c->chunks = chunk;
  }
  ir_allocation *a = (void *)(chunk->bytes + chunk->used);
  chunk->used += span;
  a->next = c->allocations;
  a->chunk = chunk;
  a->memory = c->memory + size;
  a->end = chunk->used;
  c->allocations = a;
  c->memory = a->memory;
  memset(a->bytes, 0, size);
  return a->bytes;
}
char *ir_copy(ir_context *c, const char *source, size_t size) {
  char *copy = ir_allocate(c, size + 1);
  memcpy(copy, source, size);
  return copy;
}
char *ir_format(ir_context *c, const char *format, ...) {
  va_list ap, copy;
  char buffer[256];
  va_start(ap, format);
  va_copy(copy, ap);
  int n = vsnprintf(buffer, sizeof(buffer), format, copy);
  va_end(copy);
  if (n < 0 || n > 16 * 1024 * 1024)
    ir_error(c, "formatted IR limit exceeded");
  char *text;
  if ((size_t)n < sizeof(buffer))
    text = ir_copy(c, buffer, (size_t)n);
  else {
    text = ir_allocate(c, (size_t)n + 1);
    vsnprintf(text, (size_t)n + 1, format, ap);
  }
  va_end(ap);
  return text;
}
void ir_append(ir_context *c, const char *bytes, size_t size) {
  if (size > 16u * 1024u * 1024u - c->size)
    ir_error(c, "IR output limit exceeded");
  size_t needed = c->size + size + 1;
  if (needed > c->capacity) {
    size_t cap = c->capacity ? c->capacity : 4096;
    while (cap < needed)
      cap *= 2;
    char *p = realloc(c->output, cap);
    if (!p)
      ir_error(c, "out of memory");
    c->output = p;
    c->capacity = cap;
  }
  memcpy(c->output + c->size, bytes, size);
  c->size += size;
  c->output[c->size] = 0;
}
void ir_emit(ir_context *c, const char *format, ...) {
  va_list ap, copy;
  va_start(ap, format);
  va_copy(copy, ap);
  int n = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (n < 0 || n > 16 * 1024 * 1024)
    ir_error(c, "formatted IR limit exceeded");
  char *text = ir_allocate(c, (size_t)n + 1);
  vsnprintf(text, (size_t)n + 1, format, ap);
  va_end(ap);
  ir_append(c, text, (size_t)n);
}
const char *ir_text(const ir_module *m, unsigned index) {
  if (index >= m->count)
    return "";
  return m->tokens[index].replacement ? m->tokens[index].replacement
                                      : m->tokens[index].text;
}
int ir_integer(const char *text, uint64_t *value) {
  if (!strcmp(text, "true")) {
    *value = 1;
    return 1;
  }
  if (!strcmp(text, "false")) {
    *value = 0;
    return 1;
  }
  if (!isdigit((unsigned char)*text) && *text != '-')
    return 0;
  char *end;
  errno = 0;
  uint64_t n = strtoull(text, &end, 10);
  if (end == text || *end || errno == ERANGE)
    return 0;
  *value = n;
  return 1;
}
unsigned ir_bits(const char *text) {
  uint64_t n;
  if (*text != 'i' || !ir_integer(text + 1, &n))
    return 0;
  return n == 1 || n == 8 || n == 16 || n == 32 || n == 64 ? (unsigned)n : 0;
}
unsigned ir_hash(const char *name) {
  unsigned hash = 2166136261u;
  for (; *name; name++)
    hash = (hash ^ (unsigned char)*name) * 16777619u;
  return hash;
}
void ir_map_init(ir_context *c, ir_map *map, unsigned expected) {
  unsigned capacity = 8;
  if (expected > 1000000)
    ir_error(c, "IR name index limit exceeded");
  while (capacity < expected * 2u)
    capacity *= 2;
  map->capacity = capacity;
  map->entries = ir_allocate(c, (size_t)capacity * sizeof(*map->entries));
}
void ir_map_clear(ir_map *map) {
  memset(map->entries, 0, (size_t)map->capacity * sizeof(*map->entries));
  map->count = 0;
}
const char *ir_map_get(const ir_map *map, const char *name) {
  if (!map->capacity)
    return NULL;
  unsigned slot = ir_hash(name) & (map->capacity - 1);
  while (map->entries[slot].name) {
    if (!strcmp(map->entries[slot].name, name))
      return map->entries[slot].value;
    slot = (slot + 1) & (map->capacity - 1);
  }
  return NULL;
}
void ir_map_put(ir_context *c, ir_map *map, const char *name,
                const char *value) {
  if (!map->capacity)
    ir_error(c, "uninitialized IR name index");
  unsigned slot = ir_hash(name) & (map->capacity - 1);
  while (map->entries[slot].name) {
    if (!strcmp(map->entries[slot].name, name)) {
      map->entries[slot].value = value;
      return;
    }
    slot = (slot + 1) & (map->capacity - 1);
  }
  if (map->count == map->capacity / 2)
    ir_error(c, "IR name index capacity exceeded");
  map->entries[slot] = (ir_map_entry){name, value};
  map->count++;
}
char *ir_name_key(ir_context *c, const char *name) {
  static const char hex[] = "0123456789abcdef";
  char *key = ir_allocate(c, strlen(name) * 2 + 2), *out = key;
  *out++ = *name++;
  int quoted = *name == '"';
  if (quoted)
    name++;
  while (*name && (!quoted || *name != '"')) {
    unsigned char ch = (unsigned char)*name++;
    if (quoted && ch == '\\') {
      char escape[3] = {name[0], name[1], 0};
      ch = (unsigned char)strtoul(escape, NULL, 16);
      name += 2;
    }
    *out++ = hex[ch >> 4];
    *out++ = hex[ch & 15];
  }
  *out = 0;
  return key;
}
char *ir_fresh(ir_context *c, ir_module *m, char prefix) {
  if (prefix != '@' && prefix != '%')
    ir_error(c, "fresh IR name requires a global/SSA prefix");
  if (!m->names.capacity) {
    unsigned count = 0;
    for (unsigned k = 0; k < m->count; k++)
      if (*m->tokens[k].text == '@' || *m->tokens[k].text == '%')
        count++;
    ir_map_init(c, &m->names, count);
    for (unsigned k = 0; k < m->count; k++) {
      const char *text = m->tokens[k].text;
      if (*text == '@' || *text == '%')
        ir_map_put(c, &m->names, ir_name_key(c, text), text);
    }
  }
  for (;;) {
    char *name = ir_format(c, "%c\".badge.opt.%u\"", prefix, ++c->unique);
    if (!ir_map_get(&m->names, ir_name_key(c, name)))
      return name;
  }
}
void ir_parse(ir_context *c, ir_module *m, const char *source, size_t size) {
  if (size > 16u * 1024u * 1024u || memchr(source, 0, size))
    ir_error(c, "invalid/oversized textual IR");
  m->source = source;
  m->size = size;
  unsigned count = 0, lines = 1;
  for (size_t k = 0; k < size; k++) {
    if (source[k] == '\n')
      lines++;
    if (!isspace((unsigned char)source[k]))
      count++;
  }
  if (count > 1000000)
    count = 1000000;
  m->tokens = ir_allocate(c, ((size_t)count + 1) * sizeof(*m->tokens));
  m->lines = ir_allocate(c, (size_t)lines * sizeof(*m->lines));
  size_t k = 0;
  int in_function = 0, target = 0, layout = 0;
  while (k < size) {
    ir_line *line = &m->lines[m->line_count++];
    c->line = m->line_count;
    line->begin = k;
    line->first = m->count;
    while (k < size && source[k] != '\n') {
      if (isspace((unsigned char)source[k])) {
        k++;
        continue;
      }
      if (source[k] == ';') {
        while (k < size && source[k] != '\n')
          k++;
        break;
      }
      size_t start = k;
      if (source[k] == '"' || ((source[k] == '@' || source[k] == '%') &&
                               k + 1 < size && source[k + 1] == '"')) {
        if (source[k] != '"')
          k++;
        k++;
        while (k < size && source[k] != '"') {
          if (source[k] == '\n')
            ir_error(c, "newline in IR string");
          if (source[k] == '\\') {
            if (k + 2 >= size || !isxdigit((unsigned char)source[k + 1]) ||
                !isxdigit((unsigned char)source[k + 2]))
              ir_error(c, "invalid IR string escape");
            k += 3;
          } else
            k++;
        }
        if (k >= size)
          ir_error(c, "unterminated IR string");
        k++;
      } else if (strchr("()[]{},=<>:*", source[k]))
        k++;
      else
        while (k < size && !isspace((unsigned char)source[k]) &&
               !strchr("()[]{},=<>:*;\"", source[k]))
          k++;
      if (k == start)
        ir_error(c, "invalid IR character");
      if (m->count == 1000000)
        ir_error(c, "IR token limit exceeded");
      m->tokens[m->count++] =
          (ir_token){.text = ir_copy(c, source + start, k - start),
                     .begin = start,
                     .end = k};
    }
    if (k < size)
      k++;
    line->end = k;
    line->count = m->count - line->first;
    line->op = line->first;
    if (!line->count)
      continue;
    const char *first = ir_text(m, line->first);
    if (!strcmp(first, "target")) {
      if (line->count != 4 || strcmp(ir_text(m, line->first + 2), "="))
        ir_error(c, "invalid target declaration");
      const char *kind = ir_text(m, line->first + 1),
                 *value = ir_text(m, line->first + 3);
      if (!strcmp(kind, "triple")) {
        if (target++ || strcmp(value, BADGE_IR_TRIPLE))
          ir_error(c, "unsupported/duplicate target triple");
      } else if (!strcmp(kind, "datalayout")) {
        if (layout++ || strcmp(value, BADGE_IR_LAYOUT))
          ir_error(c, "unsupported/duplicate data layout");
      } else
        ir_error(c, "unsupported target declaration");
    } else if (!strcmp(first, "define")) {
      if (in_function || strcmp(ir_text(m, m->count - 1), "{"))
        ir_error(c, "expected one-line function header");
      in_function = 1;
    } else if (!strcmp(first, "}")) {
      if (!in_function || line->count != 1)
        ir_error(c, "unexpected function end");
      in_function = 0;
    } else if (in_function) {
      if (line->count >= 2 && !strcmp(ir_text(m, line->first + 1), ":"))
        continue;
      if (*first == '%' && line->count >= 4 &&
          !strcmp(ir_text(m, line->first + 1), "="))
        line->op += 2;
      const char *op = ir_text(m, line->op);
      if (strcmp(op, "]") && strncmp(op, "i", 1))
        line->instruction = 1;
      else if (line->op != line->first)
        line->instruction = 1;
      if (!strcmp(op, "insertvalue") || !strcmp(op, "inttoptr") ||
          !strcmp(op, "icmp"))
        line->instruction = 1;
    }
  }
  if (in_function)
    ir_error(c, "unterminated function");
  if (!target || !layout)
    ir_error(c, "missing target triple/data layout");
}
void ir_write(ir_context *c, const ir_module *m) {
  for (unsigned n = 0; n < m->line_count; n++) {
    const ir_line *line = &m->lines[n];
    if (line->replacement) {
      ir_append(c, line->replacement, strlen(line->replacement));
      continue;
    }
    size_t at = line->begin;
    for (unsigned j = line->first; j < line->first + line->count; j++) {
      const ir_token *t = &m->tokens[j];
      if (!t->replacement)
        continue;
      ir_append(c, m->source + at, t->begin - at);
      ir_append(c, t->replacement, strlen(t->replacement));
      at = t->end;
    }
    ir_append(c, m->source + at, line->end - at);
  }
  if (m->suffix) {
    ir_append(c, "\n", 1);
    ir_append(c, m->suffix, strlen(m->suffix));
  }
}
void ir_release_to(ir_context *c, ir_allocation *mark) {
  if (c->allocations == mark)
    return;
  ir_chunk *retained = mark ? mark->chunk : NULL;
  while (c->chunks != retained) {
    ir_chunk *chunk = c->chunks;
    /* Keep one small empty chunk for the next verification/pass. */
    if (!mark && !chunk->next && chunk->capacity == IR_CHUNK_SIZE) {
      chunk->used = 0;
      break;
    }
    c->chunks = chunk->next;
    free(chunk);
  }
  if (mark)
    retained->used = mark->end;
  c->allocations = mark;
  c->memory = mark ? mark->memory : 0;
}
void ir_destroy(ir_context *c) {
  ir_release_to(c, NULL);
  while (c->chunks) {
    ir_chunk *chunk = c->chunks;
    c->chunks = chunk->next;
    free(chunk);
  }
  free(c->output);
  free(c->pipeline_source);
}
static void ir_source_copy(ir_context *c, const char *source, size_t size) {
  if (size > 16u * 1024u * 1024u)
    ir_error(c, "IR input limit exceeded");
  if (size + 1 > c->pipeline_capacity) {
    char *p = realloc(c->pipeline_source, size + 1);
    if (!p)
      ir_error(c, "out of memory");
    c->pipeline_source = p;
    c->pipeline_capacity = size + 1;
  }
  memcpy(c->pipeline_source, source, size);
  c->pipeline_source[size] = 0;
}
void ir_reparse(ir_context *c, ir_module *m, ir_allocation *mark) {
  c->size = 0;
  ir_write(c, m);
  size_t size = c->size;
  ir_release_to(c, mark);
  const char *source = ir_copy(c, c->output, size);
  *m = (ir_module){0};
  ir_parse(c, m, source, size);
  ir_verify(c, m);
  c->size = 0;
}
const ir_pass *ir_find_pass(const char *name, size_t length) {
  for (unsigned j = 0; j < badge_ir_pass_count; j++)
    if (strlen(badge_ir_passes[j].name) == length &&
        !strncmp(name, badge_ir_passes[j].name, length))
      return &badge_ir_passes[j];
  return NULL;
}
void ir_note(ir_context *c, const char *event, const char *reason,
             uint64_t count) {
  if (c->observe) {
    if (!c->current_pass)
      ir_error(c, "IR observation outside a pass");
    c->observe(c->observation_context, c->current_step, c->current_pass, event,
               reason, count);
  }
}
void ir_pipeline(ir_context *c, const char *source, size_t size,
                 const char *selection) {
  const char *next = selection;
  if (selection && !*selection)
    ir_error(c, "empty IR pass selection");
  ir_source_copy(c, source, size);
  for (unsigned pass = 0; pass < (selection ? 16 : badge_ir_default_pass_count);
       pass++) {
    const ir_pass *p = NULL;
    if (selection) {
      if (!*next)
        break;
      const char *end = strchr(next, ',');
      size_t length = end ? (size_t)(end - next) : strlen(next);
      p = ir_find_pass(next, length);
      if (!p)
        ir_error(c, "unknown/empty IR pass");
      next = end ? end + 1 : next + length;
      if (end && !*next)
        ir_error(c, "empty IR pass");
    } else {
      const char *name = badge_ir_default_pipeline[pass];
      p = ir_find_pass(name, strlen(name));
      if (!p)
        ir_error(c, "unregistered default IR pass");
    }
    ir_allocation *mark = c->allocations;
    ir_module m = {0};
    ir_parse(c, &m, c->pipeline_source, size);
    ir_verify(c, &m);
    c->size = 0;
    c->current_step = pass;
    c->current_pass = p->name;
    ir_note(c, "pass", "started", 1);
    p->run(c, &m);
    ir_note(c, "pass", "completed", 1);
    ir_write(c, &m);
    ir_source_copy(c, c->output, c->size);
    size = c->size;
    ir_release_to(c, mark);
  }
  if (selection && *next)
    ir_error(c, "pass selection exceeds pipeline bound");
  c->current_pass = NULL;
  ir_allocation *mark = c->allocations;
  ir_module final = {0};
  ir_parse(c, &final, c->pipeline_source, size);
  ir_verify(c, &final);
  ir_release_to(c, mark);
}
