#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include "c_semantic.h"
typedef struct tag {
    const char *name;
    c_type_t *type;
    struct tag *next;
} tag_t;
typedef struct visible_symbol {
    symbol_t *symbol;
    struct visible_symbol *next;
} visible_symbol_t;
struct scope {
    struct scope *parent;
    visible_symbol_t *symbols;
    tag_t *tags;
};
typedef struct label {
    const char *name;
    node_t *node;
    int defined, referenced;
    struct label *next;
} label_t;
typedef struct case_value {
    uint64_t value;
    struct case_value *next;
} case_value_t;
typedef struct switch_context {
    c_type_t *type;
    case_value_t *cases;
    int has_default;
    struct switch_context *parent;
} switch_context_t;
struct sema {
    parser_t *parser;
    scope_t *scope, *file_scope;
    c_type_t *types, *last_type, *primitive[T_POINTER], *return_type;
    symbol_t *symbols, *last_symbol;
    unsigned type_count, symbol_count, depth, loops;
    label_t *labels;
    switch_context_t *switches;
};
typedef struct {
    int is_typedef, is_extern, is_static, is_auto, is_register, noreturn, used;
    uint32_t alignment;
    const char *section;
    const char *section_name;
} declaration_flags_t;
static void sem_decl_attributes(sema_t *, node_t *, declaration_flags_t *);

static int sem_kind(node_t *n, const char *kind) { return n != NULL && strcmp(n->kind, kind) == 0; }
static void sem_error(sema_t *s, node_t *n, const char *format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    s->parser->position = n == NULL ? 0 : n->begin;
    parse_error(s->parser, "%s", message);
}
static void *sem_alloc(sema_t *s, size_t size) { return parse_allocate(s->parser, size); }
static void sem_enter(sema_t *s, node_t *n) {
    s->parser->position = n->begin;
    if (++s->depth > PARSE_MAX_DEPTH) sem_error(s, n, "semantic nesting limit exceeded");
}
static sem_info_t *sem_info(sema_t *s, node_t *n) {
    if (n->semantic == NULL) n->semantic = sem_alloc(s, sizeof(sem_info_t));
    return n->semantic;
}
static scope_t *sem_scope(sema_t *s, scope_t *parent) {
    scope_t *scope = sem_alloc(s, sizeof(*scope)); scope->parent = parent; return scope;
}
static symbol_t *sem_lookup(scope_t *scope, const char *name, int local) {
    for (; scope != NULL; scope = local ? NULL : scope->parent)
        for (visible_symbol_t *v = scope->symbols; v != NULL; v = v->next)
            if (strcmp(v->symbol->name, name) == 0) return v->symbol;
    return NULL;
}
static symbol_t *sem_symbol(sema_t *s, const char *name, symbol_kind_t kind, c_type_t *type) {
    symbol_t *v = sem_alloc(s, sizeof(*v));
    v->id = ++s->symbol_count; v->name = name; v->kind = kind; v->type = type;
    v->alignment = type->alignment;
    if (s->last_symbol == NULL) s->symbols = v;
    else s->last_symbol->next = v;
    s->last_symbol = v;
    return v;
}
static void sem_bind(sema_t *s, symbol_t *v) {
    visible_symbol_t *b = sem_alloc(s, sizeof(*b));
    b->symbol = v; b->next = s->scope->symbols; s->scope->symbols = b;
}
static c_type_t *sem_new_type(sema_t *s, type_kind_t kind) {
    if (s->type_count == 10000) sem_error(s, NULL, "type count limit exceeded");
    c_type_t *t = sem_alloc(s, sizeof(*t));
    t->id = ++s->type_count; t->kind = kind; t->unqualified = t;
    if (s->last_type == NULL) s->types = t;
    else s->last_type->next = t;
    s->last_type = t;
    return t;
}
static c_type_t *sem_plain(c_type_t *t) { return t->unqualified; }
static int sem_integer(c_type_t *t) { return (t->kind >= T_BOOL && t->kind <= T_ULLONG) || t->kind == T_ENUM; }
static int sem_arithmetic(c_type_t *t) { return (t->kind >= T_BOOL && t->kind <= T_LDOUBLE) || t->kind == T_ENUM; }
static int sem_scalar(c_type_t *t) { return sem_arithmetic(t) || t->kind == T_POINTER; }
static uint64_t sem_mask(c_type_t *t) { return t->size == 8 ? UINT64_MAX : (UINT64_C(1) << (t->size * 8)) - 1; }
static int64_t sem_signed(c_type_t *t, uint64_t v) {
    uint64_t mask = sem_mask(t), sign = UINT64_C(1) << (t->size * 8 - 1);
    v &= mask;
    return v & sign ? -1 - (int64_t)(~v & mask) : (int64_t)v;
}
static uint64_t sem_convert_bits(c_type_t *from, c_type_t *to, uint64_t value) {
    if (to->kind == T_BOOL) return value != 0;
    if (!from->is_unsigned && sem_integer(from)) value = (uint64_t)sem_signed(from, value);
    return value & sem_mask(to);
}
static c_type_t *sem_qualify(sema_t *s, c_type_t *t, unsigned qualifiers) {
    qualifiers |= t->qualifiers;
    if (t->kind == T_ARRAY && qualifiers != 0) {
        c_type_t *q = sem_new_type(s, T_ARRAY);
        q->base = sem_qualify(s, t->base, qualifiers);
        q->length = t->length; q->size = t->size; q->alignment = t->alignment; q->complete = t->complete;
        return q;
    }
    if (qualifiers == t->qualifiers) return t;
    c_type_t *base = sem_plain(t);
    for (c_type_t *q = s->types; q != NULL; q = q->next)
        if (q->unqualified == base && q->qualifiers == qualifiers) return q;
    c_type_t *q = sem_new_type(s, t->kind);
    unsigned id = q->id; c_type_t *next = q->next;
    *q = *t; q->id = id; q->next = next; q->unqualified = base; q->qualifiers = qualifiers;
    return q;
}
static c_type_t *sem_pointer(sema_t *s, c_type_t *base) {
    for (c_type_t *t = s->types; t != NULL; t = t->next)
        if (t->kind == T_POINTER && t->base == base && t->qualifiers == 0) return t;
    c_type_t *t = sem_new_type(s, T_POINTER);
    t->base = base; t->size = t->alignment = 4; t->complete = 1; return t;
}
static c_type_t *sem_array(sema_t *s, c_type_t *base, uint64_t length, node_t *n) {
    if (base->kind == T_VOID || base->kind == T_FUNCTION || !sem_plain(base)->complete)
        sem_error(s, n, "array element must be a complete object type");
    if (length > UINT32_MAX / base->size) sem_error(s, n, "array exceeds 32-bit target size");
    c_type_t *t = sem_new_type(s, T_ARRAY);
    t->base = base; t->length = length; t->alignment = base->alignment;
    t->size = (uint32_t)(length * base->size); t->complete = length != 0; return t;
}
static int sem_compatible(c_type_t *a, c_type_t *b, unsigned depth) {
    if (depth > PARSE_MAX_DEPTH) return 0;
    if (a == b) return 1;
    if (a->qualifiers != b->qualifiers) return 0;
    if (a->kind == T_ENUM && b->kind != T_ENUM)
        return a->complete && b->kind == (a->is_unsigned ? T_UINT : T_INT);
    if (b->kind == T_ENUM && a->kind != T_ENUM)
        return b->complete && a->kind == (b->is_unsigned ? T_UINT : T_INT);
    if (a->kind != b->kind) return 0;
    if (a->kind == T_POINTER) return sem_compatible(a->base, b->base, depth + 1);
    if (a->kind == T_ARRAY)
        return (a->length == 0 || b->length == 0 || a->length == b->length) &&
            sem_compatible(a->base, b->base, depth + 1);
    if (a->kind == T_FUNCTION) {
        if (!sem_compatible(a->base, b->base, depth + 1)) return 0;
        if (!a->prototype || !b->prototype) {
            c_type_t *p = a->prototype ? a : b;
            if (p->variadic) return 0;
            for (size_t i = 0; i < p->parameter_count; ++i)
                if (p->parameters[i]->kind == T_FLOAT || (sem_integer(p->parameters[i]) && p->parameters[i]->rank < 3))
                    return 0;
            return 1;
        }
        if (a->parameter_count != b->parameter_count || a->variadic != b->variadic) return 0;
        for (size_t i = 0; i < a->parameter_count; ++i)
            if (!sem_compatible(sem_plain(a->parameters[i]), sem_plain(b->parameters[i]), depth + 1)) return 0;
        return 1;
    }
    if (a->kind >= T_STRUCT) return sem_plain(a) == sem_plain(b);
    return 1;
}
static c_type_t *sem_promote(sema_t *s, c_type_t *t) {
    if (t->kind == T_ENUM) return s->primitive[t->is_unsigned ? T_UINT : T_INT];
    return sem_integer(t) && t->rank < 3 ? s->primitive[T_INT] : sem_plain(t);
}
static c_type_t *sem_common(sema_t *s, c_type_t *a, c_type_t *b) {
    a = sem_promote(s, a); b = sem_promote(s, b);
    if (a->kind >= T_FLOAT || b->kind >= T_FLOAT) return a->kind > b->kind ? a : b;
    if (a->is_unsigned == b->is_unsigned) return a->rank >= b->rank ? a : b;
    c_type_t *u = a->is_unsigned ? a : b, *i = a->is_unsigned ? b : a;
    if (u->rank >= i->rank) return u;
    if (i->size > u->size) return i;
    return s->primitive[i->kind + 1];
}
static c_type_t *sem_value(sema_t *s, sem_info_t *i) {
    c_type_t *t = i->type;
    if (t->kind == T_ARRAY) t = sem_pointer(s, t->base);
    else if (t->kind == T_FUNCTION) t = sem_pointer(s, t);
    else {
        if (t->kind != T_VOID && !sem_plain(t)->complete)
            parse_error(s->parser, "value conversion requires a complete type");
        t = sem_plain(t);
    }
    i->conversion = t; return t;
}
static sem_info_t *sem_expression(sema_t *, node_t *);
static unsigned char *sem_literal_bytes(sema_t *, node_t *, size_t *);
static c_type_t *sem_specifiers(sema_t *, node_t *, declaration_flags_t *);
static c_type_t *sem_declarator(sema_t *, node_t *, c_type_t *, int);
static void sem_statement(sema_t *, node_t *, int);
static void sem_initialize(sema_t *, c_type_t *, node_t *, int);
static uint64_t sem_constant(sema_t *s, node_t *n) {
    sem_info_t *i = sem_expression(s, n);
    if (!sem_integer(i->type) || !i->constant || !i->ice)
        sem_error(s, n, "expected a valid integer constant expression");
    return i->value;
}
static unsigned sem_qualifier(sema_t *s, node_t *n) {
    if (strcmp(n->text, "const") == 0) return Q_CONST;
    if (strcmp(n->text, "volatile") == 0) return Q_VOLATILE;
    if (strcmp(n->text, "restrict") == 0) return Q_RESTRICT;
    sem_error(s, n, "atomic types are not supported by this C04 subset"); return 0;
}
static void sem_alignment(sema_t *s, node_t *n, declaration_flags_t *f) {
    uint64_t value;
    if (sem_kind(n->first, "type_name")) {
        declaration_flags_t unused = {0};
        c_type_t *base = sem_specifiers(s, n->first->first, &unused);
        c_type_t *t = sem_declarator(s, n->first->first->next, base, 0);
        if (!sem_plain(t)->complete || t->kind == T_FUNCTION) sem_error(s, n, "alignment needs a complete object type");
        value = t->alignment;
    } else value = sem_constant(s, n->first);
    if (value != 0 && ((value & (value - 1)) != 0 || value > 65536))
        sem_error(s, n, "alignment must be a power of two no larger than 65536");
    if (value > f->alignment) f->alignment = (uint32_t)value;
}
static void sem_attributes(sema_t *s, node_t *n, declaration_flags_t *f) {
    for (node_t *a = n->first; a != NULL; a = a->next) {
        if (strcmp(a->text, "noreturn") == 0 || strcmp(a->text, "used") == 0) {
            if (a->first != NULL) sem_error(s, a, "attribute does not take arguments");
            if (strcmp(a->text, "noreturn") == 0) f->noreturn = 1;
            else f->used = 1;
        } else if (strcmp(a->text, "section") == 0) {
            if (!sem_kind(a->first, "string_literal") || a->first->next != NULL ||
                a->first->first->next != NULL) sem_error(s, a, "section requires one string segment");
            size_t length;
            unsigned char *bytes = sem_literal_bytes(s, a->first->first, &length);
            if (memchr(bytes, 0, length) != NULL) sem_error(s, a, "section name cannot contain NUL");
            f->section = a->first->first->text;
            f->section_name = (const char *)bytes;
        } else sem_error(s, a, "unsupported semantic attribute '%s'", a->text);
    }
}
static uint32_t sem_round(sema_t *s, node_t *n, uint64_t size, uint32_t alignment) {
    uint64_t value = (size + alignment - 1) & ~(uint64_t)(alignment - 1);
    if (value > UINT32_MAX) sem_error(s, n, "object layout exceeds 32-bit target size");
    return (uint32_t)value;
}
static c_type_t *sem_record(sema_t *s, node_t *n) {
    type_kind_t kind = sem_kind(n, "struct") ? T_STRUCT : sem_kind(n, "union") ? T_UNION : T_ENUM;
    node_t *definition = NULL;
    declaration_flags_t attrs = {0};
    for (node_t *c = n->first; c != NULL; c = c->next) {
        if (sem_kind(c, "attributes")) sem_attributes(s, c, &attrs);
        else definition = c;
    }
    if (attrs.noreturn || attrs.used || attrs.section != NULL) sem_error(s, n, "attribute does not apply to records/enums");
    c_type_t *t = NULL;
    if (n->text != NULL) {
        for (scope_t *scope = s->scope; scope != NULL; scope = definition != NULL ? NULL : scope->parent) {
            for (tag_t *tag = scope->tags; tag != NULL; tag = tag->next)
                if (strcmp(tag->name, n->text) == 0) { t = tag->type; break; }
            if (t != NULL) break;
        }
    }
    if (t == NULL) {
        t = sem_new_type(s, kind); t->tag = n->text; t->alignment = kind == T_ENUM ? 4 : 1;
        if (n->text != NULL) {
            tag_t *tag = sem_alloc(s, sizeof(*tag));
            tag->name = n->text; tag->type = t; tag->next = s->scope->tags; s->scope->tags = tag;
        }
    } else if (t->kind != kind) sem_error(s, n, "tag redeclared with a different kind");
    if (definition != NULL) {
        if (t->complete) sem_error(s, n, "record/enum redefinition");
        if (kind == T_ENUM) {
            t->is_unsigned = 1;
            uint64_t next = 0;
            for (node_t *e = definition->first; e != NULL; e = e->next) {
                sem_info_t *v = e->first == NULL ? NULL : sem_expression(s, e->first);
                if (v != NULL && (!sem_integer(v->type) || !v->ice || !v->constant))
                    sem_error(s, e, "enumerator needs an integer constant expression");
                int64_t value = v == NULL ? (int64_t)next :
                    v->type->is_unsigned ? (v->value <= INT64_MAX ? (int64_t)v->value : INT64_MAX) :
                    sem_signed(v->type, v->value);
                if (value < INT32_MIN || value > INT32_MAX)
                    sem_error(s, e, "enumerators outside signed int range are unsupported");
                if (value < 0) t->is_unsigned = 0;
                if (sem_lookup(s->scope, e->text, 1) != NULL) sem_error(s, e, "enumerator redeclaration");
                symbol_t *symbol = sem_symbol(s, e->text, S_ENUM, s->primitive[T_INT]);
                symbol->value = (uint64_t)value & UINT32_MAX; sem_bind(s, symbol);
                sem_info_t *i = sem_info(s, e);
                i->type = symbol->type; i->symbol = symbol; i->constant = i->ice = 1; i->value = symbol->value;
                next = (uint64_t)(value + 1);
            }
            t->size = t->alignment = 4; t->rank = 3;
        } else {
            uint64_t size = 0;
            for (node_t *member = definition->first; member != NULL; member = member->next) {
                if (sem_kind(member, "static_assert")) { sem_statement(s, member, 0); continue; }
                declaration_flags_t flags = {0};
                c_type_t *base = sem_specifiers(s, member->first, &flags);
                if (member->first->next == NULL) sem_error(s, member, "anonymous record members are unsupported");
                for (node_t *item = member->first->next; item != NULL; item = item->next) {
                    node_t *d = item->first;
                    if (d->next != NULL) sem_error(s, item, "bit-fields are not supported by this C04 subset");
                    c_type_t *ft = sem_declarator(s, d, base, 0);
                    declaration_flags_t member_attrs = {0};
                    sem_decl_attributes(s, d, &member_attrs);
                    if (member_attrs.noreturn || member_attrs.used || member_attrs.section || member_attrs.alignment)
                        sem_error(s, d, "declarator attributes on record members are unsupported");
                    sem_info_t *di = sem_info(s, d);
                    const char *name = NULL;
                    for (node_t *p = d; p != NULL; p = p->first) if (sem_kind(p, "name")) { name = p->text; break; }
                    if (name == NULL) sem_error(s, d, "record member needs a name");
                    for (c_field_t *f = t->fields; f != NULL; f = f->next)
                        if (strcmp(f->name, name) == 0) sem_error(s, d, "duplicate record member");
                    int flexible = ft->kind == T_ARRAY && ft->length == 0;
                    if (ft->kind == T_VOID || ft->kind == T_FUNCTION ||
                        (!sem_plain(ft)->complete && !flexible))
                        sem_error(s, d, "member needs a complete object type");
                    if (flexible && (kind != T_STRUCT || t->fields == NULL || item->next != NULL || member->next != NULL))
                        sem_error(s, d, "flexible array must be the last member of a nonempty struct");
                    uint32_t align = flags.alignment > ft->alignment ? flags.alignment : ft->alignment;
                    if (flags.alignment != 0 && flags.alignment < ft->alignment) sem_error(s, d, "alignment weakens member alignment");
                    c_field_t *field = sem_alloc(s, sizeof(*field));
                    field->name = name; field->type = ft;
                    field->offset = kind == T_UNION ? 0 : sem_round(s, d, size, align);
                    if (t->last_field == NULL) t->fields = field; else t->last_field->next = field;
                    t->last_field = field;
                    uint64_t end = (uint64_t)field->offset + ft->size;
                    if (end > size) size = end;
                    if (align > t->alignment) t->alignment = align;
                    di->field_offset = field->offset;
                }
            }
            if (t->fields == NULL) sem_error(s, n, "empty record layout is unsupported");
            t->size = sem_round(s, n, size, t->alignment);
        }
        t->complete = 1;
        for (c_type_t *q = s->types; q != NULL; q = q->next) if (q != t && q->unqualified == t) {
            q->size = t->size; q->alignment = t->alignment; q->complete = 1;
            q->fields = t->fields; q->last_field = t->last_field; q->rank = t->rank; q->is_unsigned = t->is_unsigned;
        }
    }
    sem_info(s, n)->type = t; return t;
}
static c_type_t *sem_specifiers(sema_t *s, node_t *n, declaration_flags_t *f) {
    sem_enter(s, n);
    c_type_t *base = NULL;
    unsigned q = 0, longs = 0, shorts = 0, signs = 0, unsigneds = 0, ints = 0;
    type_kind_t primary = T_INT; int explicit_primary = 0, selected = 0, storage_count = 0;
    for (node_t *c = n->first; c != NULL; c = c->next) {
        if (sem_kind(c, "type_qualifier")) q |= sem_qualifier(s, c);
        else if (sem_kind(c, "storage_class")) {
            ++storage_count;
            if (strcmp(c->text, "typedef") == 0) f->is_typedef = 1;
            else if (strcmp(c->text, "extern") == 0) f->is_extern = 1;
            else if (strcmp(c->text, "static") == 0) f->is_static = 1;
            else if (strcmp(c->text, "auto") == 0) f->is_auto = 1;
            else if (strcmp(c->text, "register") == 0) f->is_register = 1;
            else sem_error(s, c, "thread-local storage is unsupported");
        } else if (sem_kind(c, "function_specifier")) {
            if (strcmp(c->text, "_Noreturn") == 0) f->noreturn = 1;
            else sem_error(s, c, "inline linkage semantics are unsupported");
        } else if (sem_kind(c, "attributes")) sem_attributes(s, c, f);
        else if (sem_kind(c, "alignment")) sem_alignment(s, c, f);
        else if (sem_kind(c, "typedef_name")) {
            symbol_t *v = sem_lookup(s->scope, c->text, 0);
            if (v == NULL || v->kind != S_TYPEDEF) sem_error(s, c, "unknown typedef");
            base = v->type; ++selected; sem_info(s, c)->symbol = v;
        } else if (sem_kind(c, "struct") || sem_kind(c, "union") || sem_kind(c, "enum")) {
            base = sem_record(s, c); ++selected;
        } else if (sem_kind(c, "atomic_type")) sem_error(s, c, "atomic types are unsupported");
        else if (sem_kind(c, "type_specifier")) {
            const char *w = c->text;
            if (strcmp(w, "long") == 0) ++longs;
            else if (strcmp(w, "short") == 0) ++shorts;
            else if (strcmp(w, "signed") == 0) ++signs;
            else if (strcmp(w, "unsigned") == 0) ++unsigneds;
            else if (strcmp(w, "int") == 0) ++ints;
            else {
                ++explicit_primary;
                if (strcmp(w, "void") == 0) primary = T_VOID;
                else if (strcmp(w, "_Bool") == 0) primary = T_BOOL;
                else if (strcmp(w, "char") == 0) primary = T_CHAR;
                else if (strcmp(w, "float") == 0) primary = T_FLOAT;
                else if (strcmp(w, "double") == 0) primary = T_DOUBLE;
                else sem_error(s, c, "complex/imaginary types are unsupported");
            }
        } else sem_error(s, c, "unsupported declaration specifier");
    }
    if (storage_count > 1 || selected > 1 || longs > 2 || shorts > 1 || signs > 1 || unsigneds > 1 ||
        ints > 1 || explicit_primary > 1 || (signs && unsigneds) ||
        (selected && (longs || shorts || signs || unsigneds || ints || explicit_primary)))
        sem_error(s, n, "invalid declaration specifier combination");
    if (base == NULL) {
        if (primary == T_CHAR) {
            if (longs || shorts || ints) sem_error(s, n, "invalid char specifiers");
            primary = signs ? T_SCHAR : unsigneds ? T_UCHAR : T_CHAR;
        } else if (primary == T_INT) {
            if (longs && shorts) sem_error(s, n, "short and long cannot be combined");
            primary = longs == 2 ? T_LLONG : longs ? T_LONG : shorts ? T_SHORT : T_INT;
            if (unsigneds) primary = (type_kind_t)(primary + 1);
        } else if (primary == T_DOUBLE) {
            if (shorts || signs || unsigneds || ints || longs > 1) sem_error(s, n, "invalid double specifiers");
            if (longs) primary = T_LDOUBLE;
        } else if (longs || shorts || signs || unsigneds || ints) sem_error(s, n, "invalid type specifiers");
        base = s->primitive[primary];
    }
    if ((q & Q_RESTRICT) && base->kind != T_POINTER) sem_error(s, n, "restrict requires an object pointer type");
    base = sem_qualify(s, base, q);
    sem_info(s, n)->type = base; --s->depth; return base;
}
static c_type_t *sem_type_name(sema_t *s, node_t *n) {
    declaration_flags_t f = {0};
    c_type_t *t = sem_declarator(s, n->first->next, sem_specifiers(s, n->first, &f), 0);
    sem_info(s, n)->type = t; return t;
}
static c_type_t *sem_declarator(sema_t *s, node_t *n, c_type_t *base, int parameter) {
    sem_enter(s, n);
    c_type_t *type = base;
    if (sem_kind(n, "pointer_declarator")) {
        unsigned q = 0;
        for (node_t *c = n->first->next->first; c != NULL; c = c->next) {
            if (!sem_kind(c, "type_qualifier")) sem_error(s, c, "pointer attributes are unsupported");
            q |= sem_qualifier(s, c);
        }

        type = sem_qualify(s, sem_pointer(s, base), q);
        if ((q & Q_RESTRICT) && base->kind == T_FUNCTION) sem_error(s, n, "restrict does not apply to function pointers");
        type = sem_declarator(s, n->first, type, parameter);
    } else if (sem_kind(n, "array_declarator")) {
        node_t *bounds = n->first->next->next;
        uint64_t length = 0;
        if (n->text != NULL) sem_error(s, n, "variable-length arrays are unsupported");
        if (!parameter && n->first->next->first != NULL) sem_error(s, n, "array qualifiers require a parameter declaration");
        if (bounds != NULL) {
            sem_info_t *v = sem_expression(s, bounds);
            if (!sem_integer(v->type)) sem_error(s, bounds, "array bound must have integer type");
            if (!v->constant || !v->ice) sem_error(s, bounds, "nonconstant/invalid array bounds (VLAs) are unsupported");
            length = sem_constant(s, bounds);
            if ((!v->type->is_unsigned && sem_signed(v->type, length) <= 0) || length == 0)
                sem_error(s, bounds, "array bound must be positive");
        }
        type = sem_declarator(s, n->first, sem_array(s, base, length, n), parameter);
    } else if (sem_kind(n, "function_declarator")) {
        if (base->kind == T_ARRAY || base->kind == T_FUNCTION) sem_error(s, n, "function cannot return an array or function");
        c_type_t *fn = sem_new_type(s, T_FUNCTION); fn->base = base;
        node_t *list = n->first->next;
        fn->prototype = strcmp(list->text, "prototype") == 0;
        size_t count = 0;
        for (node_t *p = list->first; p != NULL; p = p->next) ++count;
        fn->parameters = sem_alloc(s, (count + 1) * sizeof(c_type_t *));
        scope_t *saved = s->scope;
        s->scope = sem_scope(s, saved);
        sem_info(s, n)->parameters = s->scope;
        for (node_t *p = list->first; p != NULL; p = p->next) {
            if (sem_kind(p, "ellipsis")) { fn->variadic = 1; continue; }
            declaration_flags_t f = {0};
            c_type_t *pt = sem_declarator(s, p->first->next, sem_specifiers(s, p->first, &f), 1);
            const char *name = NULL;
            for (node_t *d = p->first->next; d != NULL; d = d->first)
                if (sem_kind(d, "name")) { name = d->text; break; }
            if (f.is_typedef || f.is_static || f.is_extern || f.is_auto || f.noreturn || f.used || f.section || f.alignment)
                sem_error(s, p, "invalid parameter specifiers");
            if (pt->kind == T_VOID) {
                if (pt->qualifiers || name != NULL || count != 1) sem_error(s, p, "void must be the sole unnamed parameter");
                continue;
            }
            node_t *outer = NULL;
            for (node_t *d = p->first->next; d != NULL && !sem_kind(d, "name") &&
                 !sem_kind(d, "abstract_declarator"); d = d->first)
                if (!sem_kind(d, "parenthesized_declarator")) outer = d;
            unsigned array_qualifiers = 0;
            for (node_t *d = p->first->next; d != NULL && !sem_kind(d, "name") &&
                 !sem_kind(d, "abstract_declarator"); d = d->first) {
                if (!sem_kind(d, "array_declarator")) continue;
                for (node_t *q = d->first->next->first; q != NULL; q = q->next) {
                    if (d != outer) sem_error(s, q, "array qualifiers/static require outermost parameter array");
                    if (strcmp(q->text, "static") == 0) {
                        if (d->first->next->next == NULL) sem_error(s, q, "static array parameter needs a bound");
                    } else array_qualifiers |= sem_qualifier(s, q);
                }
            }
            if (pt->kind == T_ARRAY) pt = sem_qualify(s, sem_pointer(s, pt->base), array_qualifiers);
            if (pt->kind == T_FUNCTION) pt = sem_pointer(s, pt);
            fn->parameters[fn->parameter_count++] = pt;
            sem_info(s, p)->type = pt;
            if (name != NULL) {
                if (sem_lookup(s->scope, name, 1) != NULL) sem_error(s, p, "duplicate parameter");
                symbol_t *v = sem_symbol(s, name, S_PARAMETER, pt); sem_bind(s, v);
                v->register_storage = f.is_register;
                sem_info(s, p)->symbol = v;
                sem_info(s, p->first->next)->type = pt;
                sem_info(s, p->first->next)->symbol = v;
                for (node_t *d = p->first->next; d != NULL; d = d->first)
                    if (sem_kind(d, "name")) { sem_info(s, d)->type = pt; sem_info(s, d)->symbol = v; break; }
            }
        }
        s->scope = saved;
        type = sem_declarator(s, n->first, fn, parameter);
    } else if (sem_kind(n, "parenthesized_declarator")) type = sem_declarator(s, n->first, base, parameter);
    else if (!sem_kind(n, "name") && !sem_kind(n, "abstract_declarator")) sem_error(s, n, "unsupported declarator");
    else if (base->kind == T_ARRAY) {
        type = sem_new_type(s, T_ARRAY);
        type->base = base->base; type->length = base->length; type->size = base->size;
        type->alignment = base->alignment; type->complete = base->complete;
    }
    sem_info(s, n)->type = type;
    --s->depth; return type;
}

static int sem_const_member(sema_t *s, node_t *n, c_type_t *t, unsigned depth) {
    if (depth > PARSE_MAX_DEPTH) sem_error(s, n, "const-member nesting limit exceeded");
    if (t->qualifiers & Q_CONST) return 1;
    if (t->kind == T_ARRAY) return sem_const_member(s, n, t->base, depth + 1);
    if (t->kind == T_STRUCT || t->kind == T_UNION)
        for (c_field_t *f = sem_plain(t)->fields; f != NULL; f = f->next)
            if (sem_const_member(s, n, f->type, depth + 1)) return 1;
    return 0;
}
static void sem_modifiable(sema_t *s, node_t *n, sem_info_t *i) {
    if (!i->lvalue || (i->type->qualifiers & Q_CONST) || i->type->kind == T_ARRAY ||
        !sem_plain(i->type)->complete) sem_error(s, n, "operand is not a modifiable lvalue");
    if ((i->type->kind == T_STRUCT || i->type->kind == T_UNION) && sem_const_member(s, n, i->type, 0))
        sem_error(s, n, "record has a const member");
}
static int sem_null(sem_info_t *i) {
    return sem_integer(i->type) && i->constant && i->ice && i->value == 0;
}
static int sem_address_value(sem_info_t *i) {
    return i->address && (!i->lvalue || i->type->kind == T_ARRAY || i->type->kind == T_FUNCTION);
}
static int sem_pointer_compatible(c_type_t *a, c_type_t *b, int allow_void) {
    if (a->kind != T_POINTER || b->kind != T_POINTER) return 0;
    if (sem_compatible(sem_plain(a->base), sem_plain(b->base), 0)) return 1;
    return allow_void && (a->base->kind == T_VOID || b->base->kind == T_VOID) &&
        a->base->kind != T_FUNCTION && b->base->kind != T_FUNCTION;
}
static void sem_assign(sema_t *s, node_t *n, c_type_t *target) {
    sem_info_t *i = sem_expression(s, n);
    c_type_t *from = sem_value(s, i);
    if (sem_arithmetic(target) && sem_arithmetic(from)) { i->conversion = sem_plain(target); return; }
    if (target->kind == T_BOOL && sem_scalar(from)) { i->conversion = target; return; }
    if (target->kind == T_POINTER) {
        if (sem_null(i)) { i->conversion = target; return; }
        if (sem_pointer_compatible(target, from, 1) &&
            (from->base->qualifiers & ~target->base->qualifiers) == 0) { i->conversion = target; return; }
    }
    if ((target->kind == T_STRUCT || target->kind == T_UNION) &&
        sem_compatible(sem_plain(target), from, 0)) { i->conversion = target; return; }
    sem_error(s, n, "incompatible assignment/argument/return conversion");
}
static void sem_number(sema_t *s, node_t *n, sem_info_t *i) {
    int hex = n->text[0] == '0' && (n->text[1] == 'x' || n->text[1] == 'X');
    if (strchr(n->text, '.') != NULL || strpbrk(n->text, hex ? "pP" : "eE") != NULL)
        sem_error(s, n, "floating constants are not supported by this C04 subset");
    errno = 0; char *end;
    uint64_t value = strtoull(n->text, &end, 0);
    if (errno == ERANGE) sem_error(s, n, "integer constant exceeds 64 bits");
    int is_unsigned = strchr(end, 'u') != NULL || strchr(end, 'U') != NULL;
    unsigned longs = 0;
    for (const char *p = end; *p != '\0'; ++p) if (*p == 'l' || *p == 'L') ++longs;
    type_kind_t candidates[] = {T_INT, T_UINT, T_LONG, T_ULONG, T_LLONG, T_ULLONG};
    int decimal = n->text[0] != '0';
    for (size_t k = 0; k < sizeof(candidates) / sizeof(candidates[0]); ++k) {
        c_type_t *t = s->primitive[candidates[k]];
        if ((longs == 1 && t->rank < 4) || (longs == 2 && t->rank < 5) ||
            (is_unsigned && !t->is_unsigned) || (!is_unsigned && decimal && t->is_unsigned)) continue;
        uint64_t maximum = sem_mask(t) >> (t->is_unsigned ? 0 : 1);
        if (value <= maximum) { i->type = t; i->value = value; i->constant = i->ice = 1; return; }
    }
    sem_error(s, n, "integer constant has no supported target type");
}
static unsigned sem_hex_digit(unsigned char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
        c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
}
static unsigned char *sem_literal_bytes(sema_t *s, node_t *n, size_t *length) {
    size_t capacity = strlen(n->text) + 1;
    unsigned char *out = sem_alloc(s, capacity);
    const char *p = n->text;
    if (p[0] == 'u' && p[1] == '8') p += 2;
    else if (p[0] == 'L' || p[0] == 'u' || p[0] == 'U') sem_error(s, n, "wide literals are unsupported");
    char quote = *p++;
    *length = 0;
    while (*p != quote) {
        unsigned value = (unsigned char)*p++;
        if (value == '\\') {
            unsigned char escape = (unsigned char)*p++;
            if (escape >= '0' && escape <= '7') {
                value = escape - '0';
                for (unsigned k = 1; k < 3 && *p >= '0' && *p <= '7'; ++k)
                    value = value * 8 + (unsigned)(*p++ - '0');
            } else if (escape == 'x') {
                value = 0;
                if (sem_hex_digit((unsigned char)*p) == 16) sem_error(s, n, "hex escape requires digits");
                while (sem_hex_digit((unsigned char)*p) != 16) {
                    if (value > 255 / 16) sem_error(s, n, "escape exceeds target character range");
                    value = value * 16 + sem_hex_digit((unsigned char)*p++);
                }
            } else {
                const char *escapes = "abfnrtv\\'\"?";
                const unsigned char values[] = {7, 8, 12, 10, 13, 9, 11, '\\', '\'', '"', '?'};
                const char *found = strchr(escapes, escape);
                if (escape == 0 || found == NULL) sem_error(s, n, "unsupported/invalid character escape");
                value = values[found - escapes];
            }
        }
        if (value > 255) sem_error(s, n, "escape exceeds target character range");
        out[(*length)++] = (unsigned char)value;
    }
    return out;
}
static void sem_string(sema_t *s, node_t *n, sem_info_t *i) {
    size_t capacity = 1;
    for (node_t *segment = n->first; segment != NULL; segment = segment->next) capacity += strlen(segment->text);
    unsigned char *bytes = sem_alloc(s, capacity);
    size_t total = 0;
    for (node_t *segment = n->first; segment != NULL; segment = segment->next) {
        size_t length; unsigned char *part = sem_literal_bytes(s, segment, &length);
        memcpy(bytes + total, part, length); total += length;
    }
    c_type_t *t = sem_array(s, s->primitive[T_CHAR], total + 1, n);
    char *name = sem_alloc(s, 40);
    snprintf(name, 40, ".string.%u", s->symbol_count + 1);
    symbol_t *v = sem_symbol(s, name, S_STRING, t);
    v->bytes = bytes; v->byte_count = total + 1; v->defined = v->static_storage = 1;
    i->type = t; i->symbol = v; i->lvalue = i->address = 1;
}
static void sem_fold_binary(sema_t *s, sem_info_t *out, const char *op, sem_info_t *a,
                            sem_info_t *b, c_type_t *t) {
    out->ice = a->ice && b->ice;
    if ((strcmp(op, "&&") == 0 && a->constant && a->value == 0) ||
        (strcmp(op, "||") == 0 && a->constant && a->value != 0)) {
        out->constant = 1; out->value = strcmp(op, "||") == 0; return;
    }
    if (!a->constant || !b->constant || !sem_integer(t)) return;
    uint64_t x = sem_convert_bits(a->type, t, a->value), y = sem_convert_bits(b->type, t, b->value);
    uint64_t mask = sem_mask(t), sign = (mask >> 1) + 1, result = 0;
    int invalid = 0;
    if (strcmp(op, "+") == 0) {
        result = (x + y) & mask;
        invalid = !t->is_unsigned && ((x ^ result) & (y ^ result) & sign) != 0;
    } else if (strcmp(op, "-") == 0) {
        result = (x - y) & mask;
        invalid = !t->is_unsigned && ((x ^ y) & (x ^ result) & sign) != 0;
    } else if (strcmp(op, "*") == 0) {
        result = (x * y) & mask;
        if (!t->is_unsigned) {
            uint64_t ax = x & sign ? ((~x & mask) + 1) : x, ay = y & sign ? ((~y & mask) + 1) : y;
            uint64_t maximum = ((x ^ y) & sign) ? sign : sign - 1;
            invalid = ay != 0 && ax > maximum / ay;
        }
    } else if (strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
        if (y == 0 || (!t->is_unsigned && x == sign && y == mask)) invalid = 1;
        else if (t->is_unsigned) result = op[0] == '/' ? x / y : x % y;
        else result = (uint64_t)(op[0] == '/' ? sem_signed(t, x) / sem_signed(t, y) : sem_signed(t, x) % sem_signed(t, y));
    } else if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
        y = b->value;
        if (y >= t->size * 8 || (!b->type->is_unsigned && sem_signed(b->type, y) < 0)) invalid = 1;
        else if (op[0] == '<') {
            invalid = !t->is_unsigned && ((x & sign) != 0 || x > (sign - 1) >> y);
            result = x << y;
        } else {
            result = x >> y;
            if (!t->is_unsigned && (x & sign) && y != 0) result |= mask ^ (mask >> y);
        }
    } else if (strcmp(op, "&") == 0) result = x & y;
    else if (strcmp(op, "|") == 0) result = x | y;
    else if (strcmp(op, "^") == 0) result = x ^ y;
    else if (strcmp(op, "==") == 0) result = x == y;
    else if (strcmp(op, "!=") == 0) result = x != y;
    else if (strcmp(op, "&&") == 0) result = x != 0 && y != 0;
    else if (strcmp(op, "||") == 0) result = x != 0 || y != 0;
    else if (strcmp(op, "<") == 0) result = t->is_unsigned ? x < y : sem_signed(t, x) < sem_signed(t, y);
    else if (strcmp(op, ">") == 0) result = t->is_unsigned ? x > y : sem_signed(t, x) > sem_signed(t, y);
    else if (strcmp(op, "<=") == 0) result = t->is_unsigned ? x <= y : sem_signed(t, x) <= sem_signed(t, y);
    else if (strcmp(op, ">=") == 0) result = t->is_unsigned ? x >= y : sem_signed(t, x) >= sem_signed(t, y);
    else sem_error(s, NULL, "internal error: unsupported constant operator");
    if (!invalid) { out->constant = 1; out->value = result & sem_mask(out->type); }
}
static void sem_pointer_arithmetic(sema_t *s, node_t *n, sem_info_t *out, sem_info_t *p,
                                   sem_info_t *index, c_type_t *type, int subtract) {
    if (!sem_plain(type->base)->complete || type->base->kind == T_FUNCTION || type->base->kind == T_VOID)
        sem_error(s, n, "pointer arithmetic needs a complete object type");
    out->type = type;
    if (sem_address_value(p) && index->constant && sem_integer(index->type)) {
        int64_t count = index->type->is_unsigned ?
            (index->value <= INT32_MAX ? (int64_t)index->value : INT64_MAX) : sem_signed(index->type, index->value);
        if (count >= INT32_MIN && count <= INT32_MAX) {
            int64_t delta = count * type->base->size;
            if (subtract) delta = -delta;
            if ((delta > 0 && p->addend > INT64_MAX - delta) || (delta < 0 && p->addend < INT64_MIN - delta))
                sem_error(s, n, "address constant addend overflow");
            out->address = 1; out->symbol = p->symbol; out->value = p->value;
            out->addend = p->addend + delta;
        }
    }
}
static sem_info_t *sem_expression(sema_t *s, node_t *n) {
    sem_enter(s, n);
    sem_info_t *i = sem_info(s, n);
    if (i->type != NULL) { --s->depth; return i; }
    node_t *a = n->first, *b = a == NULL ? NULL : a->next;
    if (sem_kind(n, "number")) sem_number(s, n, i);
    else if (sem_kind(n, "character")) {
        size_t count; unsigned char *bytes = sem_literal_bytes(s, n, &count);
        if (count != 1) sem_error(s, n, "multi-byte/multi-character constants are unsupported");
        i->type = s->primitive[T_INT]; i->constant = i->ice = 1; i->value = bytes[0];
    } else if (sem_kind(n, "string_literal")) sem_string(s, n, i);
    else if (sem_kind(n, "identifier")) {
        symbol_t *v = sem_lookup(s->scope, n->text, 0);
        if (v == NULL || v->kind == S_TYPEDEF) sem_error(s, n, "undeclared value identifier '%s'", n->text);
        i->type = v->type; i->symbol = v; i->lvalue = v->kind == S_OBJECT || v->kind == S_PARAMETER;
        if (v->kind == S_ENUM) { i->constant = i->ice = 1; i->value = v->value; }
        if (v->kind == S_FUNCTION || (v->static_storage && v->type->kind == T_ARRAY)) i->address = 1;
    } else if (sem_kind(n, "parenthesized_expression")) *i = *sem_expression(s, a);
    else if (sem_kind(n, "sizeof_type") || sem_kind(n, "alignof_type") || sem_kind(n, "sizeof_expression")) {
        c_type_t *t = sem_kind(n, "sizeof_expression") ? sem_expression(s, a)->type : sem_type_name(s, a);
        if (t->kind == T_VOID || t->kind == T_FUNCTION || !sem_plain(t)->complete)
            sem_error(s, n, "sizeof/alignof needs a complete object type");
        i->type = s->primitive[T_UINT]; i->constant = i->ice = 1;
        i->value = sem_kind(n, "alignof_type") ? t->alignment : t->size;
    } else if (sem_kind(n, "cast")) {
        c_type_t *to = sem_type_name(s, a);
        if (to->kind != T_VOID && !sem_plain(to)->complete) sem_error(s, n, "cast target must be complete");
        sem_info_t *v = sem_expression(s, b);
        c_type_t *from = sem_value(s, v);
        if (to->kind != T_VOID && (!sem_scalar(to) || !sem_scalar(from) ||
            (to->kind == T_POINTER && !sem_integer(from) && from->kind != T_POINTER) ||
            (from->kind == T_POINTER && !sem_integer(to) && to->kind != T_POINTER)))
            sem_error(s, n, "invalid scalar cast");
        i->type = sem_plain(to); v->conversion = to;
        if (sem_integer(to) && sem_integer(from)) {
            i->constant = v->constant; i->ice = v->ice; i->value = sem_convert_bits(from, to, v->value);
        } else if (to->kind == T_POINTER && ((from->kind == T_POINTER && sem_address_value(v)) ||
                   (v->constant && sem_integer(from)))) {
            i->address = 1; i->symbol = v->symbol; i->addend = v->addend; i->value = v->value & UINT32_MAX;
        } else if (sem_integer(to) && from->kind == T_POINTER && sem_address_value(v) && v->symbol == NULL) {
            i->constant = 1;
            i->value = ((v->value + (uint64_t)v->addend) & UINT32_MAX) & sem_mask(to);
            if (to->kind == T_BOOL) i->value = i->value != 0;
        }

    } else if (sem_kind(n, "unary") || sem_kind(n, "postfix")) {
        sem_info_t *v = sem_expression(s, a);
        const char *op = n->text;
        if (strcmp(op, "&") == 0) {
            if ((!v->lvalue && v->type->kind != T_FUNCTION) ||
                (v->symbol != NULL && v->symbol->register_storage)) sem_error(s, n, "address-of needs an addressable lvalue/function");
            i->type = sem_pointer(s, v->type);
            if (v->address || (v->symbol != NULL && v->symbol->static_storage)) {
                i->address = 1; i->symbol = v->symbol; i->value = v->value; i->addend = v->addend;
            }
        } else {
            c_type_t *t = sem_value(s, v);
            if (strcmp(op, "*") == 0) {
                if (t->kind != T_POINTER || t->base->kind == T_VOID) sem_error(s, n, "dereference requires a non-void pointer");
                i->type = t->base; i->lvalue = t->base->kind != T_FUNCTION;
                i->address = sem_address_value(v); i->symbol = i->address ? v->symbol : NULL;
                i->value = v->value; i->addend = v->addend;
            } else if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0) {
                sem_modifiable(s, a, v);
                if (!sem_scalar(t)) sem_error(s, n, "increment requires scalar operand");
                if (t->kind == T_POINTER && (!sem_plain(t->base)->complete || t->base->kind == T_FUNCTION))
                    sem_error(s, n, "increment requires complete pointed-to object");
                i->type = t;
            } else if (strcmp(op, "!") == 0) {
                if (!sem_scalar(t)) sem_error(s, n, "logical not requires scalar operand");
                i->type = s->primitive[T_INT]; i->ice = v->ice;
                if (v->constant) { i->constant = 1; i->value = v->value == 0; }
            } else {
                if (!sem_arithmetic(t) || (strcmp(op, "~") == 0 && !sem_integer(t))) sem_error(s, n, "invalid unary arithmetic operand");
                i->type = sem_promote(s, t); v->conversion = i->type;
                i->ice = v->ice;
                if (v->constant && sem_integer(i->type)) {
                    uint64_t value = sem_convert_bits(v->type, i->type, v->value), mask = sem_mask(i->type);
                    if (strcmp(op, "-") != 0 || i->type->is_unsigned || value != (mask >> 1) + 1) {
                        i->constant = 1;
                        i->value = strcmp(op, "-") == 0 ? (0 - value) & mask :
                            strcmp(op, "~") == 0 ? ~value & mask : value;
                    }
                }
            }
        }
    } else if (sem_kind(n, "subscript")) {
        sem_info_t *left = sem_expression(s, a), *right = sem_expression(s, b);
        c_type_t *lt = sem_value(s, left), *rt = sem_value(s, right);
        if (lt->kind != T_POINTER && rt->kind == T_POINTER) {
            sem_info_t *tmp = left; left = right; right = tmp; c_type_t *tt = lt; lt = rt; rt = tt;
        }
        if (lt->kind != T_POINTER || !sem_integer(rt)) sem_error(s, n, "subscript requires pointer and integer");
        sem_pointer_arithmetic(s, n, i, left, right, lt, 0);
        i->type = lt->base; i->lvalue = 1; right->conversion = sem_promote(s, rt);
    } else if (sem_kind(n, "member")) {
        sem_info_t *v = sem_expression(s, a);
        c_type_t *t = v->type;
        if (strcmp(n->text, "->") == 0) {
            t = sem_value(s, v);
            if (t->kind != T_POINTER) sem_error(s, n, "arrow requires a pointer");
            t = t->base;
        }
        if ((t->kind != T_STRUCT && t->kind != T_UNION) || !sem_plain(t)->complete) sem_error(s, n, "member access requires complete record");
        c_field_t *field = sem_plain(t)->fields;
        while (field != NULL && strcmp(field->name, b->text) != 0) field = field->next;
        if (field == NULL) sem_error(s, b, "unknown record member '%s'", b->text);
        i->type = sem_qualify(s, field->type, t->qualifiers & (Q_CONST | Q_VOLATILE));
        i->lvalue = strcmp(n->text, "->") == 0 || v->lvalue;
        i->field_offset = field->offset;
        int static_location = strcmp(n->text, "->") == 0 ? sem_address_value(v) :
            v->address || (v->symbol != NULL && v->symbol->static_storage && v->lvalue);
        if (static_location) {
            i->address = 1; i->symbol = v->symbol; i->value = v->value; i->addend = v->addend + field->offset;
        }
        sem_info(s, b)->type = i->type; sem_info(s, b)->field_offset = field->offset;
    } else if (sem_kind(n, "call")) {
        sem_info_t *callee = sem_expression(s, a);
        c_type_t *fn = sem_value(s, callee);
        if (fn->kind != T_POINTER || fn->base->kind != T_FUNCTION) sem_error(s, n, "called expression is not a function");
        fn = fn->base; size_t count = 0;
        if (fn->base->kind != T_VOID && !sem_plain(fn->base)->complete)
            sem_error(s, n, "called function returns an incomplete type");
        for (node_t *arg = b; arg != NULL; arg = arg->next, ++count) {
            if (fn->prototype && count < fn->parameter_count) sem_assign(s, arg, fn->parameters[count]);
            else {
                sem_info_t *v = sem_expression(s, arg); c_type_t *t = sem_value(s, v);
                if (t->kind == T_VOID || !sem_plain(t)->complete) sem_error(s, arg, "argument needs a complete value type");
                v->conversion = t->kind == T_FLOAT ? s->primitive[T_DOUBLE] : sem_promote(s, t);
            }
        }
        if (fn->prototype && (count < fn->parameter_count || (!fn->variadic && count > fn->parameter_count)))
            sem_error(s, n, "wrong number of function arguments");
        i->type = fn->base;
    } else if (sem_kind(n, "binary") || sem_kind(n, "assignment")) {
        sem_info_t *left = sem_expression(s, a), *right = sem_expression(s, b);
        c_type_t *lt = sem_kind(n, "assignment") && strcmp(n->text, "=") == 0 ?
            sem_plain(left->type) : sem_value(s, left);
        c_type_t *rt = sem_value(s, right);
        const char *op = n->text;
        char compound[4];
        if (sem_kind(n, "assignment")) {
            sem_modifiable(s, a, left);
            if (strcmp(op, "=") == 0) { sem_assign(s, b, left->type); i->type = lt; --s->depth; return i; }
            size_t length = strlen(op) - 1; memcpy(compound, op, length); compound[length] = '\0'; op = compound;
        }
        if (strcmp(op, ",") == 0) { i->type = rt; }
        else if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
            if (!sem_scalar(lt) || !sem_scalar(rt)) sem_error(s, n, "logical operators require scalar operands");
            i->type = s->primitive[T_INT];
            if (sem_integer(lt) && sem_integer(rt)) {
                i->operation_type = sem_common(s, lt, rt); sem_fold_binary(s, i, op, left, right, i->operation_type);
            }
        } else if ((strcmp(op, "+") == 0 || strcmp(op, "-") == 0) && (lt->kind == T_POINTER || rt->kind == T_POINTER)) {
            if (lt->kind == T_POINTER && sem_integer(rt)) sem_pointer_arithmetic(s, n, i, left, right, lt, op[0] == '-');
            else if (rt->kind == T_POINTER && sem_integer(lt) && op[0] == '+') sem_pointer_arithmetic(s, n, i, right, left, rt, 0);
            else if (op[0] == '-' && sem_pointer_compatible(lt, rt, 0)) {
                if (!sem_plain(lt->base)->complete || lt->base->kind == T_FUNCTION) sem_error(s, n, "pointer difference requires complete objects");
                i->type = s->primitive[T_INT];
            } else sem_error(s, n, "invalid pointer arithmetic operands");
        } else if (word_in(op, "== != < > <= >=") && (lt->kind == T_POINTER || rt->kind == T_POINTER)) {
            int equality = strcmp(op, "==") == 0 || strcmp(op, "!=") == 0;
            if (!sem_pointer_compatible(lt, rt, equality) && !(equality &&
                ((lt->kind == T_POINTER && sem_null(right)) || (rt->kind == T_POINTER && sem_null(left)))))
                sem_error(s, n, "incompatible pointer comparison");
            if (!equality && (lt->base->kind == T_FUNCTION || rt->base->kind == T_FUNCTION))
                sem_error(s, n, "function pointer ordering is unsupported");
            i->type = s->primitive[T_INT];
        } else {
            int integer_only = word_in(op, "% & | ^ << >>");
            if (!sem_arithmetic(lt) || !sem_arithmetic(rt) || (integer_only && (!sem_integer(lt) || !sem_integer(rt))))
                sem_error(s, n, "invalid arithmetic operands");
            int shift = strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0;
            c_type_t *common = shift ? sem_promote(s, lt) : sem_common(s, lt, rt);
            i->operation_type = common;
            left->conversion = common; right->conversion = shift ? sem_promote(s, rt) : common;
            i->type = word_in(op, "== != < > <= >=") ? s->primitive[T_INT] : common;
            if (sem_integer(common)) sem_fold_binary(s, i, op, left, right, common);
        }
        if (sem_kind(n, "assignment")) {
            if (lt->kind == T_POINTER && i->type->kind != T_POINTER) sem_error(s, n, "invalid compound pointer assignment");
            i->conversion = lt; i->type = lt; i->constant = i->ice = i->address = 0;
        }
    } else if (sem_kind(n, "conditional")) {
        sem_info_t *condition = sem_expression(s, a), *yes = sem_expression(s, b), *no = sem_expression(s, b->next);
        if (!sem_scalar(sem_value(s, condition))) sem_error(s, n, "condition needs scalar type");
        c_type_t *yt = sem_value(s, yes), *nt = sem_value(s, no);
        if (sem_arithmetic(yt) && sem_arithmetic(nt)) i->type = sem_common(s, yt, nt);
        else if (yt->kind == T_POINTER && sem_null(no)) i->type = yt;
        else if (nt->kind == T_POINTER && sem_null(yes)) i->type = nt;
        else if (sem_pointer_compatible(yt, nt, 1)) {
            c_type_t *base = yt->base->kind == T_VOID ? yt->base : nt->base->kind == T_VOID ? nt->base : sem_plain(yt->base);
            i->type = sem_pointer(s, sem_qualify(s, base, yt->base->qualifiers | nt->base->qualifiers));
        } else if (sem_compatible(yt, nt, 0)) i->type = yt;
        else sem_error(s, n, "incompatible conditional operands");
        yes->conversion = no->conversion = i->type;
        i->ice = condition->ice && yes->ice && no->ice;
        if (condition->constant) {
            sem_info_t *chosen = condition->value ? yes : no;
            if (chosen->constant && sem_integer(i->type)) {
                i->constant = 1; i->value = sem_convert_bits(chosen->type, i->type, chosen->value);
            }
            if (sem_address_value(chosen) && i->type->kind == T_POINTER) {
                i->address = 1; i->symbol = chosen->symbol; i->value = chosen->value; i->addend = chosen->addend;
            } else if (i->type->kind == T_POINTER && sem_null(chosen)) i->address = 1;
        }
    } else if (sem_kind(n, "compound_literal")) {
        i->type = sem_type_name(s, a); i->lvalue = 1;
        sem_initialize(s, i->type, b, s->scope == s->file_scope);
    } else if (sem_kind(n, "generic_selection")) {
        c_type_t *control = sem_value(s, sem_expression(s, a));
        node_t *chosen = NULL, *fallback = NULL;
        for (node_t *association = b; association != NULL; association = association->next) {
            node_t *value;
            if (association->text != NULL) {
                if (fallback != NULL) sem_error(s, association, "multiple generic defaults");
                fallback = association->first; value = fallback;
            } else {
                c_type_t *match = sem_type_name(s, association->first);
                if (!sem_plain(match)->complete || match->kind == T_FUNCTION) sem_error(s, association, "generic association needs complete object type");
                for (node_t *previous = b; previous != association; previous = previous->next)
                    if (previous->text == NULL && sem_compatible(sem_info(s, previous)->type, match, 0))
                        sem_error(s, association, "duplicate compatible generic type");
                sem_info(s, association)->type = match;
                value = association->first->next;
                if (sem_compatible(control, match, 0)) chosen = value;
            }
            (void)sem_expression(s, value);
        }
        if (chosen == NULL) chosen = fallback;
        if (chosen == NULL) sem_error(s, n, "no matching generic association");
        *i = *sem_expression(s, chosen);
        i->selected = chosen;
    }
    else sem_error(s, n, "unsupported expression '%s'", n->kind);
    --s->depth; return i;
}

static c_field_t *sem_field_at(c_type_t *t, uint64_t index) {
    c_field_t *f = sem_plain(t)->fields;
    while (f != NULL && index-- != 0) f = f->next;
    return f;
}
static void sem_complete_array(sema_t *s, c_type_t *t, uint64_t length, node_t *n) {
    if (length == 0 || length > UINT32_MAX / t->base->size) sem_error(s, n, "invalid inferred array size");
    t->length = length; t->size = (uint32_t)(length * t->base->size); t->complete = 1;
}
static void sem_initialize(sema_t *s, c_type_t *type, node_t *n, int static_storage) {
    sem_enter(s, n);
    sem_info(s, n)->conversion = type;
    if (type->kind == T_ARRAY && sem_kind(n, "string_literal")) {
        sem_info_t *value = sem_expression(s, n);
        if (type->base->kind != T_CHAR && type->base->kind != T_SCHAR && type->base->kind != T_UCHAR)
            sem_error(s, n, "string initializer needs a character array");
        if (type->length == 0) sem_complete_array(s, type, value->type->length, n);
        if (type->length < value->type->length - 1) sem_error(s, n, "string initializer exceeds array bound");
        --s->depth; return;
    }
    if (!sem_kind(n, "initializer_list")) {
        if (type->kind == T_ARRAY) sem_error(s, n, "array initializer needs braces or a string");
        sem_assign(s, n, type);
        sem_info_t *value = sem_info(s, n);
        int address_value = sem_address_value(value);
        if (static_storage && !value->constant && !address_value)
            sem_error(s, n, "static initializer is not a supported constant/address expression");
        --s->depth; return;
    }
    sem_info(s, n)->type = type;
    int aggregate = type->kind == T_ARRAY || type->kind == T_STRUCT || type->kind == T_UNION;
    uint64_t index = 0, maximum = 0;
    for (node_t *item = n->first; item != NULL; item = item->next) {
        c_type_t *target = type;
        node_t *value = item->first;
        uint64_t offset = 0;
        int designated = sem_kind(value, "field_designator") || sem_kind(value, "index_designator");
        if (!aggregate && (index != 0 || designated)) sem_error(s, item, "excess/designated scalar initializer");
        if (designated) {
            int outer = 1;
            while (sem_kind(value, "field_designator") || sem_kind(value, "index_designator")) {
                if (sem_kind(value, "index_designator")) {
                    if (target->kind != T_ARRAY) sem_error(s, value, "index designator needs an array");
                    uint64_t k = sem_constant(s, value->first);
                    sem_info_t *ki = sem_expression(s, value->first);
                    if ((!ki->type->is_unsigned && sem_signed(ki->type, k) < 0) || k >= UINT32_MAX ||
                        (target->length != 0 && k >= target->length)) sem_error(s, value, "initializer designator is out of bounds");
                    if (outer) index = k;
                    offset += k * target->base->size; target = target->base;
                } else {
                    if (target->kind != T_STRUCT && target->kind != T_UNION) sem_error(s, value, "field designator needs a record");
                    c_field_t *field = sem_plain(target)->fields; uint64_t k = 0;
                    while (field != NULL && strcmp(field->name, value->text) != 0) { field = field->next; ++k; }
                    if (field == NULL) sem_error(s, value, "unknown initializer field");
                    if (outer) index = k;
                    offset += field->offset; target = field->type;
                }
                sem_info(s, value)->type = target;
                outer = 0; value = value->next;
            }
        } else if (type->kind == T_ARRAY) {
            if (type->length != 0 && index >= type->length) sem_error(s, item, "excess array initializer");
            offset = index * type->base->size; target = type->base;
        } else if (type->kind == T_STRUCT || type->kind == T_UNION) {
            c_field_t *field = sem_field_at(type, index);
            if (field == NULL || (type->kind == T_UNION && index != 0)) sem_error(s, item, "excess record initializer");
            offset = field->offset; target = field->type;
        }
        if (target->kind == T_ARRAY && target->length == 0) sem_error(s, value, "initializing flexible array members is unsupported");
        if (offset > UINT32_MAX) sem_error(s, item, "initializer offset exceeds target size");
        sem_info(s, item)->type = target; sem_info(s, item)->field_offset = (uint32_t)offset;
        sem_initialize(s, target, value, static_storage);
        ++index; if (index > maximum) maximum = index;
        if (type->kind == T_UNION) index = UINT64_MAX;
    }
    if (type->kind == T_ARRAY && type->length == 0) sem_complete_array(s, type, maximum, n);
    --s->depth;
}
static const char *sem_decl_name(node_t *n) {
    for (; n != NULL; n = n->first) if (sem_kind(n, "name")) return n->text;
    return NULL;
}
static node_t *sem_function_node(node_t *n) {
    node_t *closest = NULL;
    for (; n != NULL && !sem_kind(n, "name"); n = n->first)
        if (!sem_kind(n, "parenthesized_declarator")) closest = n;
    return sem_kind(closest, "function_declarator") ? closest : NULL;
}
static void sem_decl_attributes(sema_t *s, node_t *n, declaration_flags_t *f) {
    if (sem_kind(n, "attributes")) { sem_attributes(s, n, f); return; }
    if (sem_kind(n, "name") || sem_kind(n, "abstract_declarator")) {
        for (node_t *a = n->first; a != NULL; a = a->next)
            if (sem_kind(a, "attributes")) sem_attributes(s, a, f);
        return;
    }
    sem_decl_attributes(s, n->first, f);
    for (node_t *a = n->first->next; a != NULL; a = a->next)
        if (sem_kind(a, "attributes")) sem_attributes(s, a, f);
}
static void sem_declaration(sema_t *s, node_t *n) {
    declaration_flags_t flags = {0};
    if (n->first->next == NULL) {
        for (node_t *r = n->first->first; r != NULL; r = r->next) {
            if ((!sem_kind(r, "struct") && !sem_kind(r, "union") && !sem_kind(r, "enum")) ||
                r->text == NULL || r->first != NULL) continue;
            tag_t *existing = s->scope->tags;
            while (existing != NULL && strcmp(existing->name, r->text) != 0) existing = existing->next;
            if (existing == NULL) {
                c_type_t *t = sem_new_type(s, sem_kind(r, "struct") ? T_STRUCT : sem_kind(r, "union") ? T_UNION : T_ENUM);
                t->tag = r->text; t->alignment = t->kind == T_ENUM ? 4 : 1;
                tag_t *tag = sem_alloc(s, sizeof(*tag));
                tag->name = r->text; tag->type = t; tag->next = s->scope->tags; s->scope->tags = tag;
            }
        }
    }
    c_type_t *base = sem_specifiers(s, n->first, &flags);
    int file = s->scope == s->file_scope;
    if (file && (flags.is_auto || flags.is_register)) sem_error(s, n, "invalid file-scope storage class");
    if (n->first->next == NULL) {
        int tag_declaration = 0;
        for (node_t *c = n->first->first; c != NULL; c = c->next)
            if (sem_kind(c, "struct") || sem_kind(c, "union") || sem_kind(c, "enum")) tag_declaration = 1;
        if (!tag_declaration || flags.is_typedef) sem_error(s, n, "declaration declares no name/tag/enumerator");
        return;
    }
    for (node_t *item = n->first->next; item != NULL && !sem_kind(item, "block"); item = item->next) {
        node_t *d = item->first;
        declaration_flags_t f = flags;
        c_type_t *type = sem_declarator(s, d, base, 0);
        sem_decl_attributes(s, d, &f);
        node_t *init = NULL;
        for (node_t *c = d->next; c != NULL; c = c->next) {
            if (sem_kind(c, "attributes")) sem_attributes(s, c, &f);
            else if (sem_kind(c, "assembly")) sem_error(s, c, "declaration assembly labels are unsupported");
            else init = c;
        }
        const char *name = sem_decl_name(d);
        if (name == NULL) sem_error(s, d, "declaration needs a name");
        symbol_kind_t kind = f.is_typedef ? S_TYPEDEF : type->kind == T_FUNCTION ? S_FUNCTION : S_OBJECT;
        int function_definition = sem_kind(n, "function_definition");
        if (f.noreturn && (kind != S_FUNCTION)) sem_error(s, d, "noreturn requires a function declaration");
        if (((f.is_auto || f.is_register) && kind == S_FUNCTION) || (!file && f.is_static && kind == S_FUNCTION))
            sem_error(s, d, "invalid function storage class");
        if (f.is_register && f.alignment) sem_error(s, d, "register object cannot request alignment");
        if ((f.is_typedef || kind == S_FUNCTION) && (init != NULL || f.alignment != 0))
            sem_error(s, d, "invalid initializer/alignment on typedef/function");
        if (!file && f.is_extern && init != NULL) sem_error(s, d, "block extern cannot have an initializer");
        if (kind == S_OBJECT && type->kind == T_VOID) sem_error(s, d, "object cannot have void type");
        int linkage = kind == S_TYPEDEF ? 0 : file ? (f.is_static ? 1 : 2) :
            (f.is_extern || kind == S_FUNCTION) ? 2 : 0;
        symbol_t *v = sem_lookup(s->scope, name, 1);
        if (file && f.is_static && v == NULL)
            for (symbol_t *other = s->symbols; other != NULL; other = other->next)
                if (other->linkage == 2 && strcmp(other->name, name) == 0)
                    sem_error(s, d, "static declaration follows external linkage");
        if (v == NULL && linkage == 2) {
            symbol_t *visible = sem_lookup(s->scope, name, 0);
            if (visible != NULL && visible->linkage != 0) v = visible;
            if (v == NULL) for (symbol_t *other = s->symbols; other != NULL; other = other->next)
                if (other->linkage == 2 && strcmp(other->name, name) == 0) { v = other; break; }
        }
        if (v != NULL) {
            if (v->kind != kind || !sem_compatible(v->type, type, 0)) sem_error(s, d, "incompatible redeclaration of '%s'", name);
            if (!file && kind == S_OBJECT && !f.is_extern) sem_error(s, d, "duplicate local declaration");
            if (file && f.is_static && v->linkage == 2) sem_error(s, d, "static declaration follows external linkage");
            if (v->defined && (function_definition || init != NULL)) sem_error(s, d, "duplicate definition");
            if ((type->kind == T_ARRAY && type->length != 0) || (type->kind == T_FUNCTION && type->prototype)) v->type = type;
            else type = v->type;
            if (sem_lookup(s->scope, name, 1) == NULL) sem_bind(s, v);
        } else {
            v = sem_symbol(s, name, kind, type); v->linkage = linkage;
            v->static_storage = linkage != 0 || f.is_static; v->register_storage = f.is_register;
            sem_bind(s, v);
        }
        v->noreturn |= f.noreturn;
        if ((f.section != NULL || f.used) && (kind == S_TYPEDEF || (kind == S_OBJECT && !v->static_storage)))
            sem_error(s, d, "section/used attributes require a function or static-storage object");
        v->used |= f.used;
        if (f.section != NULL) {
            if (v->section != NULL && strcmp(v->section, f.section) != 0) sem_error(s, d, "conflicting section attributes");
            v->section = f.section;
            v->section_name = f.section_name;
        }
        if (f.alignment != 0 && f.alignment < type->alignment) sem_error(s, d, "alignment weakens object alignment");
        if (f.alignment != 0 && v->requested_alignment != 0 && v->requested_alignment != f.alignment)
            sem_error(s, d, "conflicting alignment declarations");
        if (f.alignment != 0) v->requested_alignment = f.alignment;
        if (f.alignment > v->alignment) v->alignment = f.alignment;
        if (type->alignment > v->alignment) v->alignment = type->alignment;
        sem_info(s, item)->type = type; sem_info(s, item)->symbol = v;
        sem_info(s, d)->symbol = v;
        if (init != NULL) {
            sem_initialize(s, type, init, v->static_storage);
            v->defined = 1;
        } else if (kind == S_OBJECT && file && !f.is_extern) v->tentative = 1;
        if (kind == S_OBJECT && !sem_plain(type)->complete &&
            !(file || f.is_extern) ) sem_error(s, d, "local object type is incomplete");
        if (function_definition) {
            if (kind != S_FUNCTION || !file) sem_error(s, d, "invalid function definition");
            if (!type->prototype) sem_error(s, d, "non-prototype function definitions are unsupported");
            if (type->base->kind != T_VOID && !sem_plain(type->base)->complete) sem_error(s, d, "function return type is incomplete");
            node_t *fn = sem_function_node(d);
            scope_t *saved = s->scope;
            s->scope = sem_info(s, fn)->parameters;
            for (size_t k = 0; k < type->parameter_count; ++k)
                if (!sem_plain(type->parameters[k])->complete) sem_error(s, d, "definition parameter is incomplete");
            if (type->parameter_count != 0) {
                size_t named = 0;
                for (visible_symbol_t *p = s->scope->symbols; p != NULL; p = p->next)
                    if (p->symbol->kind == S_PARAMETER) ++named;
                if (named != type->parameter_count) sem_error(s, d, "function definition requires parameter names");
            }
            s->return_type = type->base; s->labels = NULL; v->defined = 1;
            sem_statement(s, item->next, 1);
            for (label_t *label = s->labels; label != NULL; label = label->next)
                if (label->referenced && !label->defined) sem_error(s, label->node, "undefined goto label '%s'", label->name);
            s->return_type = NULL; s->scope = saved;
        }
    }
}
static label_t *sem_label(sema_t *s, node_t *n) {
    for (label_t *l = s->labels; l != NULL; l = l->next)
        if (strcmp(l->name, n->text) == 0) return l;
    label_t *l = sem_alloc(s, sizeof(*l));
    l->name = n->text; l->node = n; l->next = s->labels; s->labels = l; return l;
}
static void sem_assembly(sema_t *s, node_t *n) {
    int has_goto = 0;
    for (node_t *q = n->first; q != NULL; q = q->next)
        if (sem_kind(q, "asm_qualifier") && strcmp(q->text, "goto") == 0) has_goto = 1;
    for (node_t *group = n->first; group != NULL; group = group->next) {
        if (sem_kind(group, "asm_qualifier")) continue;
        if (sem_kind(group, "string_literal")) {
            for (node_t *segment = group->first; segment != NULL; segment = segment->next) {
                size_t length; (void)sem_literal_bytes(s, segment, &length);
            }
            continue;
        }
        if (s->return_type == NULL) sem_error(s, group, "extended assembly requires function scope");
        for (node_t *operand = group->first; operand != NULL; operand = operand->next) {
            if (sem_kind(group, "asm_inputs") || sem_kind(group, "asm_outputs")) {
                for (node_t *segment = operand->first->first; segment != NULL; segment = segment->next) {
                    size_t length; (void)sem_literal_bytes(s, segment, &length);
                }
                sem_info_t *v = sem_expression(s, operand->first->next);
                if (sem_kind(group, "asm_outputs")) sem_modifiable(s, operand->first->next, v);
                if (v->type->kind == T_VOID) sem_error(s, operand, "void assembly operand");
            } else if (sem_kind(group, "asm_labels")) {
                if (!has_goto) sem_error(s, operand, "assembly labels require asm goto");
                node_t label = *operand; label.text = operand->text;
                label_t *l = sem_label(s, &label);
                l->node = operand; l->referenced = 1;
            }
        }
    }
}
static void sem_statement(sema_t *s, node_t *n, int reuse_scope) {
    sem_enter(s, n);
    node_t *a = n->first, *b = a == NULL ? NULL : a->next;
    if (sem_kind(n, "declaration") || sem_kind(n, "function_definition")) sem_declaration(s, n);
    else if (sem_kind(n, "block")) {
        scope_t *saved = s->scope;
        if (!reuse_scope) s->scope = sem_scope(s, saved);
        for (node_t *c = a; c != NULL; c = c->next) sem_statement(s, c, 0);
        s->scope = saved;
    } else if (sem_kind(n, "expression_statement")) { if (a != NULL) (void)sem_value(s, sem_expression(s, a)); }
    else if (sem_kind(n, "static_assert")) {
        if (sem_constant(s, a) == 0) sem_error(s, n, "static assertion failed");
        (void)sem_expression(s, b);
    } else if (sem_kind(n, "return")) {
        if (s->return_type == NULL) sem_error(s, n, "return outside function");
        if (a == NULL) {
            if (s->return_type->kind != T_VOID) sem_error(s, n, "non-void function requires a return value");
        } else if (s->return_type->kind == T_VOID) {
            if (sem_expression(s, a)->type->kind != T_VOID) sem_error(s, n, "void function cannot return a value");
        } else sem_assign(s, a, s->return_type);
    } else if (sem_kind(n, "if") || sem_kind(n, "while")) {
        if (!sem_scalar(sem_value(s, sem_expression(s, a)))) sem_error(s, a, "condition must be scalar");
        if (sem_kind(n, "while")) ++s->loops;
        sem_statement(s, b, 0);
        if (sem_kind(n, "while")) --s->loops;
        if (b->next != NULL) sem_statement(s, b->next, 0);
    } else if (sem_kind(n, "do")) {
        ++s->loops; sem_statement(s, a, 0); --s->loops;
        if (!sem_scalar(sem_value(s, sem_expression(s, b)))) sem_error(s, b, "condition must be scalar");
    } else if (sem_kind(n, "for")) {
        scope_t *saved = s->scope; s->scope = sem_scope(s, saved);
        if (sem_kind(a, "declaration")) sem_declaration(s, a);
        else if (!sem_kind(a, "empty")) (void)sem_expression(s, a);
        if (!sem_kind(b, "empty") && !sem_scalar(sem_value(s, sem_expression(s, b)))) sem_error(s, b, "condition must be scalar");
        if (!sem_kind(b->next, "empty")) (void)sem_expression(s, b->next);
        ++s->loops; sem_statement(s, b->next->next, 0); --s->loops;
        s->scope = saved;
    } else if (sem_kind(n, "switch")) {
        c_type_t *t = sem_value(s, sem_expression(s, a));
        if (!sem_integer(t)) sem_error(s, a, "switch requires integer expression");
        switch_context_t context = {.type = sem_promote(s, t), .parent = s->switches};
        s->switches = &context; sem_statement(s, b, 0); s->switches = context.parent;
    } else if (sem_kind(n, "case") || sem_kind(n, "default")) {
        if (s->switches == NULL) sem_error(s, n, "case/default outside switch");
        if (sem_kind(n, "case")) {
            uint64_t value = sem_constant(s, a);
            value = sem_convert_bits(sem_expression(s, a)->type, s->switches->type, value);
            for (case_value_t *c = s->switches->cases; c != NULL; c = c->next)
                if (c->value == value) sem_error(s, n, "duplicate case value");
            case_value_t *c = sem_alloc(s, sizeof(*c)); c->value = value;
            c->next = s->switches->cases; s->switches->cases = c;
            sem_statement(s, b, 0);
        } else {
            if (s->switches->has_default) sem_error(s, n, "duplicate default label");
            s->switches->has_default = 1; sem_statement(s, a, 0);
        }
    } else if (sem_kind(n, "break")) {
        if (s->loops == 0 && s->switches == NULL) sem_error(s, n, "break outside loop/switch");
    } else if (sem_kind(n, "continue")) {
        if (s->loops == 0) sem_error(s, n, "continue outside loop");
    } else if (sem_kind(n, "label")) {
        label_t *l = sem_label(s, n);
        if (l->defined) sem_error(s, n, "duplicate label");
        l->defined = 1; sem_statement(s, a, 0);
    } else if (sem_kind(n, "goto")) sem_label(s, n)->referenced = 1;
    else if (sem_kind(n, "assembly")) sem_assembly(s, n);
    else sem_error(s, n, "unsupported statement '%s'", n->kind);
    --s->depth;
}

#ifndef BADGE_SEMA_LIBRARY
static void sem_output_number(parser_t *p, uint64_t number) {
    char text[32]; snprintf(text, sizeof(text), "%" PRIu64, number); output_text(p, text);
}
static void sem_output_metadata(parser_t *p, node_t *n) {
    sem_info_t *i = n->semantic;
    if (i == NULL) return;
    if (i->type != NULL) { output_text(p, ",\"type\":"); sem_output_number(p, i->type->id); }
    if (i->conversion != NULL) { output_text(p, ",\"conversion\":"); sem_output_number(p, i->conversion->id); }
    if (i->operation_type != NULL) { output_text(p, ",\"operation_type\":"); sem_output_number(p, i->operation_type->id); }
    if (i->symbol != NULL) { output_text(p, ",\"symbol\":"); sem_output_number(p, i->symbol->id); }
    if (i->type != NULL) output_text(p, i->lvalue ? ",\"category\":\"lvalue\"" :
        i->type->kind == T_FUNCTION ? ",\"category\":\"function\"" : ",\"category\":\"rvalue\"");
    if (i->constant) {
        output_text(p, ",\"integer_constant\":\""); sem_output_number(p, i->value); output_text(p, "\"");
        output_text(p, i->ice ? ",\"ice\":true" : ",\"ice\":false");
    }
    if (i->address) {
        output_text(p, ",\"address\":{\"base_bits\":\""); sem_output_number(p, i->value);
        char addend[64]; snprintf(addend, sizeof(addend), "\",\"addend\":\"%" PRId64 "\"}", i->addend);
        output_text(p, addend);
    }
    if (sem_kind(n, "member") || sem_kind(n, "member_name") || sem_kind(n, "initializer_item") ||
        sem_kind(n, "name")) {
        output_text(p, ",\"byte_offset\":"); sem_output_number(p, i->field_offset);
    }
}
static void sem_output_types(sema_t *s) {
    static const char *const names[] = {
        "void", "bool", "char", "signed_char", "unsigned_char", "short", "unsigned_short", "int", "unsigned_int",
        "long", "unsigned_long", "long_long", "unsigned_long_long", "float", "double", "long_double",
        "pointer", "array", "function", "struct", "union", "enum"
    };
    parser_t *p = s->parser; output_text(p, "\"types\":[");
    for (c_type_t *t = s->types; t != NULL; t = t->next) {
        if (t != s->types) output_text(p, ",");
        output_text(p, "{\"id\":"); sem_output_number(p, t->id);
        output_text(p, ",\"kind\":"); output_string(p, names[t->kind]);
        output_text(p, ",\"size\":"); sem_output_number(p, t->size);
        output_text(p, ",\"alignment\":"); sem_output_number(p, t->alignment);
        output_text(p, ",\"qualifiers\":"); sem_output_number(p, t->qualifiers);
        output_text(p, t->complete ? ",\"complete\":true" : ",\"complete\":false");
        output_text(p, t->is_unsigned ? ",\"unsigned\":true" : ",\"unsigned\":false");
        if (t->base != NULL) { output_text(p, ",\"base\":"); sem_output_number(p, t->base->id); }
        if (t->tag != NULL) { output_text(p, ",\"tag\":"); output_string(p, t->tag); }
        if (t->kind == T_ARRAY) { output_text(p, ",\"length\":"); sem_output_number(p, t->length); }
        if (t->kind == T_FUNCTION) {
            output_text(p, t->prototype ? ",\"prototype\":true" : ",\"prototype\":false");
            output_text(p, t->variadic ? ",\"variadic\":true" : ",\"variadic\":false");
            output_text(p, ",\"parameters\":[");
            for (size_t k = 0; k < t->parameter_count; ++k) {
                if (k != 0) output_text(p, ","); sem_output_number(p, t->parameters[k]->id);
            }
            output_text(p, "]");
        }
        if (t->kind == T_STRUCT || t->kind == T_UNION) {
            output_text(p, ",\"fields\":[");
            for (c_field_t *f = t->fields; f != NULL; f = f->next) {
                if (f != t->fields) output_text(p, ",");
                output_text(p, "{\"name\":"); output_string(p, f->name);
                output_text(p, ",\"type\":"); sem_output_number(p, f->type->id);
                output_text(p, ",\"offset\":"); sem_output_number(p, f->offset); output_text(p, "}");
            }
            output_text(p, "]");
        }
        output_text(p, "}");
    }
    output_text(p, "],\"symbols\":[");
    static const char *const symbol_names[] = {"object", "function", "typedef", "enum_constant", "parameter", "string"};
    for (symbol_t *v = s->symbols; v != NULL; v = v->next) {
        if (v->type->alignment > v->alignment) v->alignment = v->type->alignment;
        if (v != s->symbols) output_text(p, ",");
        output_text(p, "{\"id\":"); sem_output_number(p, v->id);
        output_text(p, ",\"name\":"); output_string(p, v->name);
        output_text(p, ",\"kind\":"); output_string(p, symbol_names[v->kind]);
        output_text(p, ",\"type\":"); sem_output_number(p, v->type->id);
        output_text(p, ",\"linkage\":"); sem_output_number(p, (unsigned)v->linkage);
        output_text(p, ",\"alignment\":"); sem_output_number(p, v->alignment);
        output_text(p, v->static_storage ? ",\"static_storage\":true" : ",\"static_storage\":false");
        output_text(p, v->defined ? ",\"defined\":true" : ",\"defined\":false");
        if (v->noreturn) output_text(p, ",\"noreturn\":true");
        if (v->used) output_text(p, ",\"used\":true");
        if (v->section != NULL) { output_text(p, ",\"section_spelling\":"); output_string(p, v->section); }
        if (v->kind == S_ENUM) { output_text(p, ",\"integer_constant\":\""); sem_output_number(p, v->value); output_text(p, "\""); }
        if (v->bytes != NULL) {
            output_text(p, ",\"bytes_hex\":\"");
            for (size_t k = 0; k < v->byte_count; ++k) {
                char byte[3]; snprintf(byte, sizeof(byte), "%02x", v->bytes[k]); output_text(p, byte);
            }
            output_text(p, "\"");
        }
        output_text(p, "}");
    }
    output_text(p, "],");
}
#endif
static sema_t *sem_analyze(parser_t *p, node_t *root) {
    sema_t *s = parse_allocate(p, sizeof(*s)); s->parser = p;
    s->scope = s->file_scope = sem_scope(s, NULL);
    static const unsigned sizes[] = {0,1,1,1,1,2,2,4,4,4,4,8,8,4,8,8};
    static const unsigned ranks[] = {0,0,1,1,1,2,2,3,3,4,4,5,5,6,7,8};
    for (unsigned k = 0; k < T_POINTER; ++k) {
        c_type_t *t = sem_new_type(s, (type_kind_t)k);
        t->size = sizes[k]; t->alignment = sizes[k] == 0 ? 1 : sizes[k];
        t->rank = ranks[k]; t->complete = k != T_VOID;
        t->is_unsigned = k == T_BOOL || k == T_CHAR || k == T_UCHAR || k == T_USHORT ||
            k == T_UINT || k == T_ULONG || k == T_ULLONG;
        s->primitive[k] = t;
    }
    for (node_t *n = root->first; n != NULL; n = n->next) sem_statement(s, n, 0);
    for (symbol_t *v = s->symbols; v != NULL; v = v->next) {
        if (v->kind == S_OBJECT && v->tentative) {
            if (v->type->kind == T_ARRAY && v->type->length == 0) sem_complete_array(s, v->type, 1, root);
            if (!sem_plain(v->type)->complete) sem_error(s, root, "tentative definition has incomplete type");
            v->defined = 1;
        }
    }
    p->semantic_context = s; return s;
}
sema_t *badge_sema_analyze(parser_t *p,node_t *root) { return sem_analyze(p,root); }
symbol_t *badge_sema_symbols(sema_t *s) { return s->symbols; }
unsigned badge_sema_symbol_count(sema_t *s) { return s->symbol_count; }
c_type_t *badge_sema_primitive(sema_t *s,type_kind_t kind) {
    if(kind>=T_POINTER) sem_error(s,NULL,"invalid primitive type request"); return s->primitive[kind];
}
unsigned char *badge_sema_literal(sema_t *s,node_t *n,size_t *size) { return sem_literal_bytes(s,n,size); }
#ifndef BADGE_SEMA_LIBRARY
static void sem_process(parser_t *p, node_t *root) {
    sema_t *s = sem_analyze(p, root);
    p->node_metadata = sem_output_metadata;
    output_text(p, "{\"schema\":\"badge-c-typed-ast-v1\",\"target\":\"thumbv8m.main-unknown-none-eabi\",");
    output_text(p, "\"abi\":\"aapcs-soft-float\",\"byte_order\":\"little\",");
    output_text(p, p->no_locations ? "\"locations\":false," : "\"locations\":true,");
    sem_output_types(s);
    output_text(p, "\"ast\":"); output_node(p, root); output_text(p, "}\n");
}
int main(int argc, char **argv) { return frontend_run(argc, argv, "badge-sema", sem_process); }
#endif
