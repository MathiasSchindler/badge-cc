#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include "c_frontend.h"
#include "host_io.h"

static token_t *peek(parser_t *p, size_t ahead) {
    size_t index = p->position + ahead;
    return &p->tokens.items[index < p->tokens.count ? index : p->tokens.count];
}

_Noreturn void parse_error(parser_t *p, const char *format, ...) {
    const char *file = p->file;
    unsigned line = p->line;
    if (p->tokens.items != NULL) {
        file = peek(p, 0)->file;
        line = peek(p, 0)->line;
    }
    fprintf(stderr, "%s: %s:%u: ", p->tool == NULL ? "badge-parse" : p->tool, file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    longjmp(p->failure, 1);
}

void *parse_allocate(void *context, size_t size) {
    parser_t *p = context;
    if (size > PARSE_MAX_ARENA - sizeof(allocation_t) ||
        size + sizeof(allocation_t) > PARSE_MAX_ARENA - p->allocated)
        parse_error(p, "parser memory limit exceeded");
    allocation_t *a = calloc(1, sizeof(*a) + size);
    if (a == NULL) parse_error(p, "out of memory");
    a->next = p->allocations;
    p->allocations = a;
    p->allocated += sizeof(*a) + size;
    return a->bytes;
}

void parse_destroy(parser_t *p) {
    while (p->allocations != NULL) {
        allocation_t *next = p->allocations->next;
        free(p->allocations);
        p->allocations = next;
    }
    free(p->output);
}

static void parse_lexer_error(void *context, const char *file, unsigned line, const char *message) {
    parser_t *p = context;
    p->file = file;
    p->line = line;
    parse_error(p, "%s", message);
}

int word_in(const char *word, const char *list) {
    size_t length = strlen(word);
    for (const char *start = list; *start != '\0';) {
        const char *end = strchr(start, ' ');
        size_t size = end == NULL ? strlen(start) : (size_t)(end - start);
        if (length == size && memcmp(start, word, size) == 0) return 1;
        if (end == NULL) break;
        start = end + 1;
    }
    return 0;
}

static int keyword(const char *s) {
    return word_in(s, "auto break case char const continue default do double else enum extern "
        "float for goto if inline int long register restrict return short signed sizeof static "
        "struct switch typedef union unsigned void volatile while _Alignas _Alignof _Atomic "
        "_Bool _Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local "
        "__attribute__ __asm__ __asm asm __volatile__");
}

static int at(parser_t *p, const char *s) { return strcmp(peek(p, 0)->text, s) == 0; }
static int accept(parser_t *p, const char *s) {
    if (!at(p, s)) return 0;
    ++p->position;
    return 1;
}
static void expect(parser_t *p, const char *s) {
    if (!accept(p, s)) parse_error(p, "expected '%s', found '%s'", s, peek(p, 0)->text);
}
static const char *identifier(parser_t *p) {
    token_t *t = peek(p, 0);
    if (t->kind != IDENT || keyword(t->text)) parse_error(p, "expected identifier, found '%s'", t->text);
    ++p->position;
    return t->text;
}
static void enter(parser_t *p) {
    if (++p->depth > PARSE_MAX_DEPTH) parse_error(p, "parser nesting limit exceeded");
}
static node_t *node_at(parser_t *p, const char *kind, const char *text, size_t begin) {
    if (++p->node_count > C_LEXER_MAX_TOKENS * 4u) parse_error(p, "AST node limit exceeded");
    node_t *n = parse_allocate(p, sizeof(*n));
    n->kind = kind; n->text = text; n->begin = begin; n->height = 1;
    return n;
}
static node_t *node(parser_t *p, const char *kind, const char *text) {
    return node_at(p, kind, text, p->position);
}
static void child(parser_t *p, node_t *parent, node_t *n) {
    if (n == NULL) return;
    if (n->height + 1 > PARSE_MAX_DEPTH) parse_error(p, "AST nesting limit exceeded");
    if (parent->height <= n->height) parent->height = n->height + 1;
    if (parent->last == NULL) parent->first = n;
    else parent->last->next = n;
    parent->last = n;
}
static node_t *finish(parser_t *p, node_t *n) { n->end = p->position; return n; }
static node_t *leaf(parser_t *p, const char *kind) {
    node_t *n = node(p, kind, peek(p, 0)->text);
    ++p->position;
    return finish(p, n);
}
static int is_typedef(parser_t *p, const char *name) {
    for (binding_t *b = p->bindings; b != NULL; b = b->next)
        if (strcmp(b->name, name) == 0) return b->is_typedef;
    return 0;
}
static void bind(parser_t *p, const char *name, int is_type) {
    binding_t *b = parse_allocate(p, sizeof(*b));
    b->name = name; b->is_typedef = is_type; b->next = p->bindings;
    p->bindings = b;
}
static int qualifier(const char *s) { return word_in(s, "const restrict volatile _Atomic"); }
static int storage(const char *s) { return word_in(s, "typedef extern static auto register _Thread_local"); }
static int type_word(const char *s) {
    return word_in(s, "void char short int long float double signed unsigned _Bool _Complex _Imaginary");
}
static int declaration_start(parser_t *p, size_t ahead) {
    const char *s = peek(p, ahead)->text;
    return qualifier(s) || storage(s) || type_word(s) || is_typedef(p, s) ||
        word_in(s, "struct union enum inline _Noreturn _Alignas _Static_assert __attribute__");
}
static int string_token(token_t *t) {
    if (t->kind != LITERAL) return 0;
    const char *quote = strpbrk(t->text, "\"'");
    return quote != NULL && *quote == '"';
}

static node_t *expression(parser_t *, int);
static node_t *unary(parser_t *);
static node_t *initializer(parser_t *);
static node_t *declaration(parser_t *, int, int);
static node_t *specifiers(parser_t *, int, int *);
static node_t *declarator(parser_t *, int);
static node_t *type_name(parser_t *);
static node_t *statement(parser_t *);
static node_t *block(parser_t *);
static node_t *assembly(parser_t *);

static node_t *strings(parser_t *p) {
    node_t *n = node(p, "string_literal", NULL);
    if (!string_token(peek(p, 0))) parse_error(p, "expected string literal");
    while (string_token(peek(p, 0))) child(p, n, leaf(p, "string_segment"));
    return finish(p, n);
}

static node_t *attributes(parser_t *p) {
    node_t *n = node(p, "attributes", NULL);
    expect(p, "__attribute__"); expect(p, "("); expect(p, "(");
    if (!at(p, ")")) do {
        node_t *a = node(p, "attribute", peek(p, 0)->text);
        if (peek(p, 0)->kind != IDENT) parse_error(p, "expected attribute name");
        ++p->position;
        if (accept(p, "(")) {
            if (!at(p, ")")) do { child(p, a, expression(p, 2)); } while (accept(p, ","));
            expect(p, ")");
        }
        child(p, n, finish(p, a));
    } while (accept(p, ","));
    expect(p, ")"); expect(p, ")");
    return finish(p, n);
}

static node_t *static_assertion(parser_t *p) {
    node_t *n = node(p, "static_assert", NULL);
    expect(p, "_Static_assert"); expect(p, "(");
    child(p, n, expression(p, 3)); expect(p, ",");
    child(p, n, strings(p)); expect(p, ")"); expect(p, ";");
    return finish(p, n);
}

static node_t *record(parser_t *p) {
    enter(p);
    const char *kind = peek(p, 0)->text;
    node_t *n = node(p, kind, NULL);
    ++p->position;
    while (at(p, "__attribute__")) child(p, n, attributes(p));
    if (peek(p, 0)->kind == IDENT && !keyword(peek(p, 0)->text)) n->text = identifier(p);
    if (accept(p, "{")) {
        node_t *body = node_at(p, strcmp(kind, "enum") == 0 ? "enum_definition" : "record_definition",
                              NULL, p->position - 1);
        if (strcmp(kind, "enum") == 0) {
            if (at(p, "}")) parse_error(p, "empty enum is unsupported");
            do {
                node_t *e = node(p, "enumerator", peek(p, 0)->text);
                const char *name = identifier(p);
                if (accept(p, "=")) child(p, e, expression(p, 3));
                bind(p, name, 0);
                child(p, body, finish(p, e));
            } while (accept(p, ",") && !at(p, "}"));
        } else {
            while (!at(p, "}")) child(p, body, declaration(p, 1, 0));
        }
        expect(p, "}");
        child(p, n, finish(p, body));
    } else if (n->text == NULL) parse_error(p, "record/enum needs a tag or definition");
    while (at(p, "__attribute__")) child(p, n, attributes(p));
    --p->depth;
    return finish(p, n);
}

static node_t *specifiers(parser_t *p, int allow_storage, int *is_type) {
    enter(p);
    node_t *n = node(p, "specifiers", NULL);
    int has_type = 0;
    for (;;) {
        const char *s = peek(p, 0)->text;
        if (at(p, "__attribute__")) child(p, n, attributes(p));
        else if (word_in(s, "struct union enum")) {
            child(p, n, record(p)); has_type = 1;
        } else if (at(p, "_Atomic") && strcmp(peek(p, 1)->text, "(") == 0) {
            node_t *a = node(p, "atomic_type", NULL);
            ++p->position; expect(p, "(");
            child(p, a, type_name(p)); expect(p, ")");
            child(p, n, finish(p, a)); has_type = 1;
        } else if (allow_storage && at(p, "_Alignas")) {
            node_t *a = node(p, "alignment", NULL);
            ++p->position; expect(p, "(");
            child(p, a, declaration_start(p, 0) ? type_name(p) : expression(p, 3));
            expect(p, ")"); child(p, n, finish(p, a));
        } else if (type_word(s)) {
            child(p, n, leaf(p, "type_specifier")); has_type = 1;
        } else if (qualifier(s)) child(p, n, leaf(p, "type_qualifier"));
        else if (allow_storage && storage(s)) {
            if (at(p, "typedef")) *is_type = 1;
            child(p, n, leaf(p, "storage_class"));
        } else if (allow_storage && word_in(s, "inline _Noreturn")) {
            child(p, n, leaf(p, "function_specifier"));
        } else if (!has_type && is_typedef(p, s)) {
            child(p, n, leaf(p, "typedef_name")); has_type = 1;
        } else break;
    }
    if (!has_type) parse_error(p, "expected type specifier; implicit int/unknown types are unsupported");
    --p->depth;
    return finish(p, n);
}

static const char *declarator_name(node_t *n) {
    while (n != NULL) {
        if (strcmp(n->kind, "name") == 0) return n->text;
        if (strcmp(n->kind, "abstract_declarator") == 0) return NULL;
        n = n->first;
    }
    return NULL;
}

static node_t *function_declarator(node_t *n) {
    node_t *closest = NULL;
    while (n != NULL && strcmp(n->kind, "name") != 0 &&
           strcmp(n->kind, "abstract_declarator") != 0) {
        if (strcmp(n->kind, "parenthesized_declarator") != 0) closest = n;
        n = n->first;
    }
    return closest != NULL && strcmp(closest->kind, "function_declarator") == 0 ? closest : NULL;
}

static node_t *declarator(parser_t *p, int abstract_ok) {
    enter(p);
    size_t start = p->position;
    node_t *pointers[PARSE_MAX_DEPTH], *qualities[PARSE_MAX_DEPTH];
    size_t count = 0;
    while (at(p, "*")) {
        if (count == PARSE_MAX_DEPTH) parse_error(p, "pointer nesting limit exceeded");
        pointers[count] = node(p, "pointer_declarator", NULL);
        ++p->position;
        qualities[count] = node(p, "pointer_qualifiers", NULL);
        while (qualifier(peek(p, 0)->text) || at(p, "__attribute__"))
            child(p, qualities[count], at(p, "__attribute__") ? attributes(p) : leaf(p, "type_qualifier"));
        finish(p, qualities[count]);
        ++count;
    }
    node_t *n;
    int grouped = at(p, "(") && (!abstract_ok ||
        strcmp(peek(p, 1)->text, "*") == 0 || strcmp(peek(p, 1)->text, "(") == 0 ||
        (peek(p, 1)->kind == IDENT && !declaration_start(p, 1)));
    if (grouped) {
        n = node(p, "parenthesized_declarator", NULL);
        ++p->position; child(p, n, declarator(p, abstract_ok)); expect(p, ")");
        finish(p, n);
    } else if (peek(p, 0)->kind == IDENT && !keyword(peek(p, 0)->text)) {
        n = node(p, "name", peek(p, 0)->text); identifier(p); finish(p, n);
    } else {
        if (!abstract_ok) parse_error(p, "expected declarator");
        n = finish(p, node(p, "abstract_declarator", NULL));
    }
    for (;;) {
        if (accept(p, "[")) {
            node_t *a = node_at(p, "array_declarator", NULL, start);
            child(p, a, n);
            node_t *q = node(p, "array_qualifiers", NULL);
            while (qualifier(peek(p, 0)->text) || at(p, "static"))
                child(p, q, leaf(p, "array_qualifier"));
            child(p, a, finish(p, q));
            if (at(p, "*") && strcmp(peek(p, 1)->text, "]") == 0) {
                ++p->position; a->text = "*";
            }
            else if (!at(p, "]")) child(p, a, expression(p, 2));
            expect(p, "]"); n = finish(p, a);
        } else if (accept(p, "(")) {
            node_t *f = node_at(p, "function_declarator", NULL, start);
            child(p, f, n);
            node_t *parameters = node(p, "parameters", at(p, ")") ? "unspecified" : "prototype");
            binding_t *saved = p->bindings;
            if (!at(p, ")")) do {
                if (accept(p, "...")) {
                    if (parameters->first == NULL) parse_error(p, "ellipsis needs a preceding parameter");
                    child(p, parameters, finish(p, node_at(p, "ellipsis", NULL, p->position - 1)));
                    break;
                }
                node_t *parameter = node(p, "parameter", NULL);
                int is_type = 0;
                child(p, parameter, specifiers(p, 1, &is_type));
                node_t *d = declarator(p, 1);
                child(p, parameter, d);
                const char *name = declarator_name(d);
                if (name != NULL) bind(p, name, 0);
                child(p, parameters, finish(p, parameter));
            } while (accept(p, ","));
            expect(p, ")");
            child(p, f, finish(p, parameters));
            f->parameters = p->bindings; f->parameter_base = saved;
            p->bindings = saved;
            n = finish(p, f);
        } else if (at(p, "__attribute__")) {
            child(p, n, attributes(p)); finish(p, n);
        } else break;
    }
    while (count != 0) {
        --count;
        child(p, pointers[count], n); child(p, pointers[count], qualities[count]);
        n = finish(p, pointers[count]);
    }
    --p->depth;
    return n;
}

static node_t *type_name(parser_t *p) {
    node_t *n = node(p, "type_name", NULL);
    int unused = 0;
    child(p, n, specifiers(p, 0, &unused));
    node_t *d = declarator(p, 1);
    if (declarator_name(d) != NULL) parse_error(p, "identifier is not allowed in a type name");
    child(p, n, d);
    return finish(p, n);
}

static node_t *postfix(parser_t *p, node_t *n) {
    for (;;) {
        if (accept(p, "[")) {
            node_t *a = node_at(p, "subscript", NULL, n->begin);
            child(p, a, n); child(p, a, expression(p, 1)); expect(p, "]");
            n = finish(p, a);
        } else if (accept(p, "(")) {
            node_t *call = node_at(p, "call", NULL, n->begin);
            child(p, call, n);
            if (!at(p, ")")) do { child(p, call, expression(p, 2)); } while (accept(p, ","));
            expect(p, ")"); n = finish(p, call);
        } else if (at(p, ".") || at(p, "->")) {
            node_t *m = node_at(p, "member", peek(p, 0)->text, n->begin);
            ++p->position; child(p, m, n);
            node_t *name = node(p, "member_name", peek(p, 0)->text);
            identifier(p); child(p, m, finish(p, name)); n = finish(p, m);
        } else if (at(p, "++") || at(p, "--")) {
            node_t *u = node_at(p, "postfix", peek(p, 0)->text, n->begin);
            ++p->position; child(p, u, n); n = finish(p, u);
        } else return n;
    }
}

static node_t *unary(parser_t *p) {
    enter(p);
    node_t *n;
    if (word_in(peek(p, 0)->text, "++ -- & * + - ~ !")) {
        n = node(p, "unary", peek(p, 0)->text); ++p->position;
        child(p, n, unary(p)); finish(p, n);
    } else if (at(p, "sizeof") || at(p, "_Alignof")) {
        int alignment = at(p, "_Alignof");
        n = node(p, alignment ? "alignof_type" : "sizeof_expression", NULL);
        ++p->position;
        if (at(p, "(") && declaration_start(p, 1)) {
            ++p->position; child(p, n, type_name(p)); expect(p, ")");
            if (!alignment) n->kind = "sizeof_type";
        } else {
            if (alignment) parse_error(p, "_Alignof requires a parenthesized type name");
            child(p, n, unary(p));
        }
        finish(p, n);
    } else if (at(p, "(") && declaration_start(p, 1)) {
        n = node(p, "cast", NULL);
        ++p->position; child(p, n, type_name(p)); expect(p, ")");
        if (at(p, "{")) {
            n->kind = "compound_literal"; child(p, n, initializer(p));
            n = postfix(p, finish(p, n));
        } else { child(p, n, unary(p)); finish(p, n); }
    } else {
        if (accept(p, "(")) {
            n = node_at(p, "parenthesized_expression", NULL, p->position - 1);
            child(p, n, expression(p, 1)); expect(p, ")"); finish(p, n);
        } else if (accept(p, "_Generic")) {
            n = node_at(p, "generic_selection", NULL, p->position - 1);
            expect(p, "("); child(p, n, expression(p, 2)); expect(p, ",");
            do {
                node_t *a = node(p, "generic_association", NULL);
                if (accept(p, "default")) a->text = "default";
                else child(p, a, type_name(p));
                expect(p, ":"); child(p, a, expression(p, 2));
                child(p, n, finish(p, a));
            } while (accept(p, ","));
            expect(p, ")"); finish(p, n);
        } else if (string_token(peek(p, 0))) n = strings(p);
        else if (peek(p, 0)->kind == NUMBER) n = leaf(p, "number");
        else if (peek(p, 0)->kind == LITERAL) n = leaf(p, "character");
        else {
            n = node(p, "identifier", peek(p, 0)->text);
            if (is_typedef(p, peek(p, 0)->text)) parse_error(p, "typedef name is not an expression");
            identifier(p); finish(p, n);
        }
        n = postfix(p, n);
    }
    --p->depth;
    return n;
}

static int precedence(const char *s) {
    if (strcmp(s, ",") == 0) return 1;
    if (word_in(s, "= += -= *= /= %= <<= >>= &= ^= |=")) return 2;
    if (strcmp(s, "?") == 0) return 3;
    if (strcmp(s, "||") == 0) return 4;
    if (strcmp(s, "&&") == 0) return 5;
    if (strcmp(s, "|") == 0) return 6;
    if (strcmp(s, "^") == 0) return 7;
    if (strcmp(s, "&") == 0) return 8;
    if (word_in(s, "== !=")) return 9;
    if (word_in(s, "< > <= >=")) return 10;
    if (word_in(s, "<< >>")) return 11;
    if (word_in(s, "+ -")) return 12;
    if (word_in(s, "* / %")) return 13;
    return 0;
}

static node_t *expression(parser_t *p, int minimum) {
    enter(p);
    node_t *left = unary(p);
    for (;;) {
        int level = precedence(peek(p, 0)->text);
        if (level < minimum) break;
        if (level == 2 && word_in(left->kind, "binary conditional assignment cast"))
            parse_error(p, "assignment left operand must be a unary expression");
        node_t *n = node_at(p, level == 3 ? "conditional" : level == 2 ? "assignment" : "binary",
                            peek(p, 0)->text, left->begin);
        ++p->position; child(p, n, left);
        if (level == 3) {
            child(p, n, expression(p, 1)); expect(p, ":");
            child(p, n, expression(p, 3));
        } else child(p, n, expression(p, level == 2 ? 2 : level + 1));
        left = finish(p, n);
    }
    --p->depth;
    return left;
}

static node_t *initializer(parser_t *p) {
    enter(p);
    node_t *n;
    if (accept(p, "{")) {
        n = node_at(p, "initializer_list", NULL, p->position - 1);
        if (!at(p, "}")) do {
            node_t *item = node(p, "initializer_item", NULL);
            if (at(p, ".") || at(p, "[")) {
                do {
                    node_t *d = node(p, "field_designator", NULL);
                    if (accept(p, ".")) d->text = identifier(p);
                    else {
                        d->kind = "index_designator"; expect(p, "[");
                        child(p, d, expression(p, 3)); expect(p, "]");
                    }
                    child(p, item, finish(p, d));
                } while (at(p, ".") || at(p, "["));
                expect(p, "=");
            }
            child(p, item, initializer(p)); child(p, n, finish(p, item));
        } while (accept(p, ",") && !at(p, "}"));
        expect(p, "}"); finish(p, n);
    } else n = expression(p, 2);
    --p->depth;
    return n;
}

static node_t *assembly(parser_t *p) {
    node_t *n = node(p, "assembly", peek(p, 0)->text);
    ++p->position;
    while (word_in(peek(p, 0)->text, "volatile __volatile__ inline goto"))
        child(p, n, leaf(p, "asm_qualifier"));
    expect(p, "("); child(p, n, strings(p));
    unsigned group = 0;
    static const char *const groups[] = {"asm_outputs", "asm_inputs", "asm_clobbers", "asm_labels"};
    while (accept(p, ":")) {
        if (group == 4) parse_error(p, "too many inline assembly operand groups");
        node_t *g = node(p, groups[group], NULL);
        if (!at(p, ":") && !at(p, ")")) do {
            node_t *operand = node(p, "asm_operand", NULL);
            if (group < 2) {
                if (accept(p, "[")) { operand->text = identifier(p); expect(p, "]"); }
                child(p, operand, strings(p)); expect(p, "(");
                child(p, operand, expression(p, 1)); expect(p, ")");
            } else if (group == 2) child(p, operand, strings(p));
            else operand->text = identifier(p);
            child(p, g, finish(p, operand));
        } while (accept(p, ","));
        child(p, n, finish(p, g)); ++group;
    }
    expect(p, ")");
    return finish(p, n);
}

static void import_parameters(parser_t *p, binding_t *b, binding_t *base) {
    enter(p);
    if (b != base) {
        import_parameters(p, b->next, base);
        bind(p, b->name, b->is_typedef);
    }
    --p->depth;
}

static node_t *declaration(parser_t *p, int member, int external) {
    enter(p);
    if (at(p, "_Static_assert")) { --p->depth; return static_assertion(p); }
    node_t *n = node(p, member ? "member_declaration" : "declaration", NULL);
    int is_type = 0;
    child(p, n, specifiers(p, !member, &is_type));
    if (!at(p, ";")) do {
        node_t *item = node(p, "init_declarator", NULL);
        node_t *d = declarator(p, member);
        const char *name = declarator_name(d);
        child(p, item, d);
        if (!member && name != NULL) bind(p, name, is_type);
        if (member && accept(p, ":")) {
            node_t *width = node_at(p, "bitfield_width", NULL, p->position - 1);
            child(p, width, expression(p, 3)); child(p, item, finish(p, width));
        } else if (member && name == NULL) parse_error(p, "unnamed member needs a bitfield width");
        if (!member && word_in(peek(p, 0)->text, "__asm__ __asm asm"))
            child(p, item, assembly(p));
        while (at(p, "__attribute__")) child(p, item, attributes(p));
        if (at(p, "{") && external) {
            node_t *f = function_declarator(d);
            if (f == NULL || n->first->next != NULL || is_type)
                parse_error(p, "function definition needs a single function declarator");
            n->kind = "function_definition";
            child(p, n, finish(p, item));
            binding_t *saved = p->bindings;
            import_parameters(p, f->parameters, f->parameter_base);
            child(p, n, block(p)); p->bindings = saved;
            --p->depth; return finish(p, n);
        }
        if (accept(p, "=")) {
            if (member) parse_error(p, "member initializer is unsupported");
            child(p, item, initializer(p));
        }
        child(p, n, finish(p, item));
    } while (accept(p, ","));
    expect(p, ";"); --p->depth;
    return finish(p, n);
}

static node_t *block(parser_t *p) {
    enter(p);
    node_t *n = node(p, "block", NULL);
    binding_t *saved = p->bindings;
    expect(p, "{");
    while (!at(p, "}"))
        child(p, n, declaration_start(p, 0) &&
              strcmp(peek(p, 1)->text, ":") != 0 ? declaration(p, 0, 0) : statement(p));
    expect(p, "}"); p->bindings = saved; --p->depth;
    return finish(p, n);
}

static node_t *statement(parser_t *p) {
    enter(p);
    node_t *n;
    if (at(p, "{")) n = block(p);
    else if (at(p, "if") || at(p, "while") || at(p, "switch")) {
        n = node(p, peek(p, 0)->text, NULL); ++p->position;
        expect(p, "("); child(p, n, expression(p, 1)); expect(p, ")");
        child(p, n, statement(p));
        if (strcmp(n->kind, "if") == 0 && accept(p, "else")) child(p, n, statement(p));
        finish(p, n);
    } else if (accept(p, "do")) {
        n = node_at(p, "do", NULL, p->position - 1);
        child(p, n, statement(p)); expect(p, "while"); expect(p, "(");
        child(p, n, expression(p, 1)); expect(p, ")"); expect(p, ";"); finish(p, n);
    } else if (accept(p, "for")) {
        n = node_at(p, "for", NULL, p->position - 1);
        binding_t *saved = p->bindings;
        expect(p, "(");
        if (declaration_start(p, 0)) child(p, n, declaration(p, 0, 0));
        else {
            child(p, n, at(p, ";") ? finish(p, node(p, "empty", NULL)) : expression(p, 1));
            expect(p, ";");
        }
        child(p, n, at(p, ";") ? finish(p, node(p, "empty", NULL)) : expression(p, 1));
        expect(p, ";");
        child(p, n, at(p, ")") ? finish(p, node(p, "empty", NULL)) : expression(p, 1));
        expect(p, ")"); child(p, n, statement(p));
        p->bindings = saved; finish(p, n);
    } else if (at(p, "case") || at(p, "default")) {
        int is_case = at(p, "case");
        n = node(p, peek(p, 0)->text, NULL); ++p->position;
        if (is_case) child(p, n, expression(p, 3));
        expect(p, ":"); child(p, n, statement(p)); finish(p, n);
    } else if (peek(p, 0)->kind == IDENT && strcmp(peek(p, 1)->text, ":") == 0) {
        n = node(p, "label", peek(p, 0)->text);
        identifier(p); expect(p, ":"); child(p, n, statement(p)); finish(p, n);
    } else if (word_in(peek(p, 0)->text, "return goto break continue")) {
        n = node(p, peek(p, 0)->text, NULL); ++p->position;
        if (strcmp(n->kind, "goto") == 0) n->text = identifier(p);
        else if (strcmp(n->kind, "return") == 0 && !at(p, ";")) child(p, n, expression(p, 1));
        expect(p, ";"); finish(p, n);
    } else if (word_in(peek(p, 0)->text, "__asm__ __asm asm")) {
        n = assembly(p); expect(p, ";"); finish(p, n);
    } else {
        n = node(p, "expression_statement", NULL);
        if (!at(p, ";")) child(p, n, expression(p, 1));
        expect(p, ";"); finish(p, n);
    }
    --p->depth;
    return n;
}

static void validate_number(parser_t *p, const char *s) {
    int hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    if (strchr(s, '.') != NULL || strpbrk(s, hex ? "pP" : "eE") != NULL) {
        char *end;
        (void)strtod(s, &end);
        if (end == s || (hex && strpbrk(s, "pP") == NULL) ||
            (*end != '\0' && !(word_in(end, "f F l L"))))
            parse_error(p, "invalid floating constant '%s'", s);
        return;
    }
    const char *digits = s + (hex ? 2 : 0), *end = digits;
    unsigned base = hex ? 16 : s[0] == '0' ? 8 : 10;
    while (*end != '\0') {
        unsigned digit = *end >= '0' && *end <= '9' ? (unsigned)(*end - '0') :
            *end >= 'a' && *end <= 'f' ? (unsigned)(*end - 'a' + 10) :
            *end >= 'A' && *end <= 'F' ? (unsigned)(*end - 'A' + 10) : 16;
        if (digit >= base) break;
        ++end;
    }
    if (end == digits || (*end != '\0' && !word_in(end,
        "u U l L ul uL Ul UL lu lU Lu LU ll LL ull uLL Ull ULL llu llU LLu LLU")))
        parse_error(p, "invalid integer constant '%s'", s);
}

static unsigned marker_number(parser_t *p, token_t *t) {
    if (t->kind != NUMBER || t->text[0] == '\0') parse_error(p, "invalid line marker");
    unsigned value = 0;
    for (const char *s = t->text; *s != '\0'; ++s) {
        if (*s < '0' || *s > '9' || value > (UINT_MAX - (unsigned)(*s - '0')) / 10)
            parse_error(p, "invalid line marker number");
        value = value * 10 + (unsigned)(*s - '0');
    }
    return value;
}

static void validate_utf8(parser_t *p, const char *source, size_t length, int track_lines) {
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)source[i];
        if (track_lines && ch == '\n') ++p->line;
        if (ch < 128) continue;
        unsigned remaining = ch >= 0xc2 && ch <= 0xdf ? 1 :
            ch >= 0xe0 && ch <= 0xef ? 2 : ch >= 0xf0 && ch <= 0xf4 ? 3 : 0;
        if (remaining == 0 || remaining >= length - i) parse_error(p, "invalid UTF-8 source/filename");
        unsigned value = ch & (remaining == 1 ? 31u : remaining == 2 ? 15u : 7u);
        for (unsigned j = 0; j < remaining; ++j) {
            unsigned char next = (unsigned char)source[++i];
            if ((next & 0xc0) != 0x80) parse_error(p, "invalid UTF-8 source/filename");
            value = value * 64 + (next & 63u);
        }
        if (value < (remaining == 1 ? 0x80u : remaining == 2 ? 0x800u : 0x10000u) ||
            (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff)
            parse_error(p, "invalid UTF-8 source/filename");
    }
}

static const char *marker_file(parser_t *p, token_t *t) {
    size_t length = strlen(t->text);
    if (t->kind != LITERAL || length < 2 || t->text[0] != '"' || t->text[length - 1] != '"')
        parse_error(p, "invalid line marker filename");
    char *path = parse_allocate(p, length);
    size_t out = 0;
    for (size_t i = 1; i < length - 1; ++i) {
        unsigned char ch = (unsigned char)t->text[i];
        if (ch == '\\') {
            ch = (unsigned char)t->text[++i];
            if (ch >= '0' && ch <= '7') {
                unsigned value = ch - '0';
                for (unsigned n = 1; n < 3 && i + 1 < length - 1 &&
                     t->text[i + 1] >= '0' && t->text[i + 1] <= '7'; ++n)
                    value = value * 8 + (unsigned)(t->text[++i] - '0');
                if (value == 0 || value > UCHAR_MAX) parse_error(p, "invalid filename escape");
                ch = (unsigned char)value;
            } else if (ch != '\\' && ch != '"') parse_error(p, "unsupported filename escape");
        }
        path[out++] = (char)ch;
    }
    validate_utf8(p, path, out, 0);
    return path;
}

node_t *parse_source(parser_t *p, const char *source, size_t length, const char *file) {
    p->file = file; p->line = 1;
    if (length > PARSE_MAX_BYTES) parse_error(p, "input size limit exceeded");
    validate_utf8(p, file, strlen(file), 0);
    validate_utf8(p, source, length, 1);
    c_lexer_t lexer = {.context = p, .allocate = parse_allocate, .error = parse_lexer_error};
    tokens_t raw = c_lex(&lexer, source, length, file);
    tokens_t result = {.items = parse_allocate(p, (raw.count + 1) * sizeof(token_t))};
    const char *mapped_file = file;
    unsigned anchor = 1, mapped_line = 1;
    int beginning = 1;
    for (size_t i = 0; i < raw.count;) {
        token_t t = raw.items[i];
        p->file = file; p->line = t.line;
        if (t.kind == NEWLINE) { beginning = 1; ++i; continue; }
        if (beginning && strcmp(t.text, "#") == 0) {
            size_t end = i + 1;
            while (end < raw.count && raw.items[end].kind != NEWLINE) ++end;
            size_t number = i + 1;
            if (number < end && strcmp(raw.items[number].text, "line") == 0) ++number;
            if (end == number) parse_error(p, "expected preprocessing line marker");
            mapped_line = marker_number(p, &raw.items[number]);
            size_t flags = number + 1;
            if (flags < end) mapped_file = marker_file(p, &raw.items[flags++]);
            for (size_t flag = flags; flag < end; ++flag) {
                unsigned value = marker_number(p, &raw.items[flag]);
                if (value < 1 || value > 4) parse_error(p, "invalid line marker flag");
            }
            anchor = (end < raw.count ? raw.items[end].line : t.line) + 1;
            i = end; continue;
        }
        beginning = 0;
        unsigned delta = t.line - anchor;
        if (delta > UINT_MAX - mapped_line) parse_error(p, "source line number overflow");
        t.file = mapped_file; t.line = mapped_line + delta;
        p->file = t.file; p->line = t.line;
        if (t.kind == NUMBER) validate_number(p, t.text);
        if (t.kind == LITERAL && (strcmp(t.text, "''") == 0 || strcmp(t.text, "L''") == 0 ||
            strcmp(t.text, "u''") == 0 || strcmp(t.text, "U''") == 0))
            parse_error(p, "empty character constant");
        result.items[result.count++] = t;
        ++i;
    }
    unsigned end_line = lexer.end_line < anchor ? anchor : lexer.end_line;
    if (end_line - anchor > UINT_MAX - mapped_line) parse_error(p, "source line number overflow");
    result.items[result.count] = (token_t){.text = "", .file = mapped_file,
        .line = mapped_line + end_line - anchor, .kind = PUNCT};
    p->tokens = result;
    node_t *root = node(p, "translation_unit", NULL);
    while (p->position < p->tokens.count) {
        if (word_in(peek(p, 0)->text, "__asm__ __asm asm")) {
            node_t *n = assembly(p); expect(p, ";"); child(p, root, finish(p, n));
        } else child(p, root, declaration(p, 0, 1));
    }
    return finish(p, root);
}

void output_bytes(parser_t *p, const char *bytes, size_t size) {
    if (size > PARSE_MAX_OUTPUT - p->output_size) parse_error(p, "AST output size limit exceeded");
    size_t needed = p->output_size + size + 1;
    if (needed > p->output_capacity) {
        size_t capacity = p->output_capacity == 0 ? 4096 : p->output_capacity;
        while (capacity < needed) capacity *= 2;
        char *output = realloc(p->output, capacity);
        if (output == NULL) parse_error(p, "out of memory serializing AST");
        p->output = output; p->output_capacity = capacity;
    }
    memcpy(p->output + p->output_size, bytes, size);
    p->output_size += size; p->output[p->output_size] = '\0';
}
void output_text(parser_t *p, const char *text) { output_bytes(p, text, strlen(text)); }
void output_string(parser_t *p, const char *text) {
    output_text(p, "\"");
    for (const unsigned char *s = (const unsigned char *)text; *s != '\0'; ++s) {
        if (*s == '"' || *s == '\\') {
            output_text(p, "\\"); output_bytes(p, (const char *)s, 1);
        } else if (*s < 32 || *s == 127) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", *s);
            output_text(p, escaped);
        } else output_bytes(p, (const char *)s, 1);
    }
    output_text(p, "\"");
}
void output_node(parser_t *p, node_t *n) {
    output_text(p, "{\"kind\":"); output_string(p, n->kind);
    if (n->text != NULL) { output_text(p, ",\"text\":"); output_string(p, n->text); }
    char numbers[128];
    if (!p->no_locations) {
        output_text(p, ",\"location\":{\"file\":");
        token_t *t = &p->tokens.items[n->begin];
        output_string(p, t->file);
        snprintf(numbers, sizeof(numbers), ",\"line\":%u}", t->line);
        output_text(p, numbers);
    }
    snprintf(numbers, sizeof(numbers), ",\"tokens\":[%zu,%zu],\"children\":[", n->begin, n->end);
    output_text(p, numbers);
    for (node_t *c = n->first; c != NULL; c = c->next) {
        if (c != n->first) output_text(p, ",");
        output_node(p, c);
    }
    output_text(p, "]");
    if (p->node_metadata != NULL) p->node_metadata(p, n);
    output_text(p, "}");
}
void serialize(parser_t *p, node_t *root) {
    output_text(p, "{\"schema\":\"badge-c-ast-v1\",\"locations\":");
    output_text(p, p->no_locations ? "false" : "true");
    output_text(p, ",\"ast\":");
    output_node(p, root);
    output_text(p, "}\n");
}

int frontend_run(int argc, char **argv, const char *tool, void (*process)(parser_t *, node_t *)) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("usage: %s [--no-locations] input.i -o output.ast.json\n", tool);
        return 0;
    }
    int no_locations = argc > 1 && strcmp(argv[1], "--no-locations") == 0;
    int first = no_locations ? 2 : 1;
    if (argc != first + 3 || strcmp(argv[first + 1], "-o") != 0 ||
        argv[first][0] == '-' || argv[first + 2][0] == '\0') {
        fprintf(stderr, "usage: %s [--no-locations] input.i -o output.ast.json\n", tool);
        return 2;
    }
    const char *input = argv[first], *output = argv[first + 2];
    parser_t *p = calloc(1, sizeof(*p));
    if (p == NULL) { fprintf(stderr, "%s: out of memory\n", tool); return 1; }
    p->file = input; p->line = 1; p->no_locations = no_locations; p->tool = tool;
    int result = 1;
    if (setjmp(p->failure) == 0) {
        struct stat source_stat, output_stat;
        if (stat(input, &source_stat) != 0)
            parse_error(p, "cannot inspect input: %s", strerror(errno));
        if (!S_ISREG(source_stat.st_mode) || source_stat.st_size < 0 ||
            (uintmax_t)source_stat.st_size > PARSE_MAX_BYTES)
            parse_error(p, "input must be a regular file within the size limit");
        if (stat(output, &output_stat) == 0) {
            if (source_stat.st_dev == output_stat.st_dev && source_stat.st_ino == output_stat.st_ino)
                parse_error(p, "output aliases input");
        } else if (errno != ENOENT) parse_error(p, "cannot inspect output: %s", strerror(errno));
        size_t length = (size_t)source_stat.st_size;
        char *source = parse_allocate(p, length + 1);
        FILE *file = fopen(input, "rb");
        if (file == NULL) parse_error(p, "cannot open input: %s", strerror(errno));
        int ok = fread(source, 1, length, file) == length;
        if (fgetc(file) != EOF || ferror(file)) ok = 0;
        if (fclose(file) != 0) ok = 0;
        if (!ok) parse_error(p, "failed to read/close input or input changed size");
        process(p, parse_source(p, source, length, input));
        result = host_write_file(tool, output, (const unsigned char *)p->output, p->output_size) ? 0 : 1;
    }

    parse_destroy(p); free(p);
    return result;
}

#ifndef BADGE_PARSE_LIBRARY
int main(int argc, char **argv) { return frontend_run(argc, argv, "badge-parse", serialize); }
#endif
