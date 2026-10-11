#ifndef BADGE_COMPILER_C_FRONTEND_H
#define BADGE_COMPILER_C_FRONTEND_H
#include <setjmp.h>
#include <stddef.h>
#include "c_lexer.h"

#define PARSE_MAX_DEPTH 256u
#define PARSE_MAX_BYTES (16u * 1024u * 1024u)
#define PARSE_MAX_ARENA (128u * 1024u * 1024u)
#define PARSE_MAX_OUTPUT (64u * 1024u * 1024u)

typedef struct allocation {
    struct allocation *next;
    max_align_t alignment;
    unsigned char bytes[];
} allocation_t;
typedef struct binding {
    const char *name;
    int is_typedef;
    struct binding *next;
} binding_t;
typedef struct node {
    const char *kind, *text;
    size_t begin, end;
    unsigned height;
    struct node *first, *last, *next;
    binding_t *parameters, *parameter_base;
    void *semantic;
} node_t;
typedef struct parser {
    jmp_buf failure;
    allocation_t *allocations;
    size_t allocated, position, node_count;
    unsigned depth, line;
    const char *file, *tool;
    tokens_t tokens;
    binding_t *bindings;
    char *output;
    size_t output_size, output_capacity;
    int no_locations;
    void *semantic_context;
    void (*node_metadata)(struct parser *, node_t *);
} parser_t;

int word_in(const char *, const char *);
_Noreturn void parse_error(parser_t *, const char *, ...);
void *parse_allocate(void *, size_t);
void parse_destroy(parser_t *);
node_t *parse_source(parser_t *, const char *, size_t, const char *);
void output_bytes(parser_t *, const char *, size_t);
void output_text(parser_t *, const char *);
void output_string(parser_t *, const char *);
void output_node(parser_t *, node_t *);
void serialize(parser_t *, node_t *);
int frontend_run(int, char **, const char *, void (*)(parser_t *, node_t *));
#endif
