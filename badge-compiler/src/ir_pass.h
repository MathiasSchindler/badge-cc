#ifndef BADGE_IR_PASS_H
#define BADGE_IR_PASS_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ir_allocation ir_allocation;
typedef struct ir_chunk ir_chunk;
typedef struct {
  const char *name, *value;
} ir_map_entry;
typedef struct {
  ir_map_entry *entries;
  unsigned capacity, count;
} ir_map;
typedef struct {
  char *text;
  const char *replacement;
  size_t begin, end;
} ir_token;
typedef struct {
  size_t begin, end;
  unsigned first, count, op;
  int instruction;
  char *replacement;
} ir_line;
typedef struct {
  const char *source;
  size_t size;
  ir_token *tokens;
  unsigned count;
  ir_line *lines;
  unsigned line_count;
  char *suffix;
  ir_map names;
} ir_module;
typedef struct {
  jmp_buf failure;
  ir_allocation *allocations;
  ir_chunk *chunks;
  size_t memory;
  const char *file;
  unsigned line, unique;
  char *output;
  size_t size, capacity;
  char *pipeline_source;
  size_t pipeline_capacity;
  void (*observe)(void *, unsigned, const char *, const char *, const char *,
                  uint64_t);
  void *observation_context;
  unsigned current_step;
  const char *current_pass;
} ir_context;
typedef enum { IR_PASS_OPTIMIZATION, IR_PASS_LEGALIZATION } ir_pass_role;
typedef struct {
  const char *name, *description;
  void (*run)(ir_context *, ir_module *);
  ir_pass_role role;
} ir_pass;

extern const ir_pass badge_ir_passes[];
extern const unsigned badge_ir_pass_count;
extern const char *const badge_ir_default_pipeline[];
extern const unsigned badge_ir_default_pass_count;
const ir_pass *ir_find_pass(const char *name, size_t length);
void ir_pipeline(ir_context *, const char *, size_t, const char *);
void ir_note(ir_context *, const char *, const char *, uint64_t);
void ir_map_init(ir_context *, ir_map *, unsigned);
unsigned ir_hash(const char *);
void ir_map_clear(ir_map *);
const char *ir_map_get(const ir_map *, const char *);
void ir_map_put(ir_context *, ir_map *, const char *, const char *);
_Noreturn void ir_error(ir_context *c, const char *message);
/* Zeroed, max_align_t-aligned storage; memory counts live requested bytes.
   allocations remains a logical last-allocation mark, not a chunk mark. */
void *ir_allocate(ir_context *c, size_t size);
char *ir_copy(ir_context *c, const char *source, size_t size);
char *ir_format(ir_context *c, const char *format, ...);
char *ir_fresh(ir_context *c, ir_module *m, char prefix);
char *ir_name_key(ir_context *, const char *);
void ir_append(ir_context *c, const char *bytes, size_t size);
void ir_emit(ir_context *c, const char *format, ...);
void ir_parse(ir_context *c, ir_module *m, const char *source, size_t size);
void ir_write(ir_context *c, const ir_module *m);
const char *ir_text(const ir_module *m, unsigned index);
int ir_integer(const char *text, uint64_t *value);
unsigned ir_bits(const char *text);
void ir_destroy(ir_context *c);
void ir_release_to(ir_context *, ir_allocation *);
void ir_reparse(ir_context *, ir_module *, ir_allocation *);

#endif
