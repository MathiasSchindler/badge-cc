#ifndef BADGE_COMPILER_C_SEMANTIC_H
#define BADGE_COMPILER_C_SEMANTIC_H
#include "c_frontend.h"

enum { Q_CONST=1,Q_VOLATILE=2,Q_RESTRICT=4 };
typedef enum {
    T_VOID,T_BOOL,T_CHAR,T_SCHAR,T_UCHAR,T_SHORT,T_USHORT,T_INT,T_UINT,
    T_LONG,T_ULONG,T_LLONG,T_ULLONG,T_FLOAT,T_DOUBLE,T_LDOUBLE,
    T_POINTER,T_ARRAY,T_FUNCTION,T_STRUCT,T_UNION,T_ENUM
} type_kind_t;
typedef struct c_type c_type_t;
typedef struct c_field {
    const char *name;
    c_type_t *type;
    uint32_t offset;
    struct c_field *next;
} c_field_t;
struct c_type {
    unsigned id,qualifiers,rank;
    type_kind_t kind;
    uint32_t size,alignment;
    int complete,is_unsigned,prototype,variadic;
    c_type_t *base,*unqualified,*next;
    uint64_t length;
    c_type_t **parameters;
    size_t parameter_count;
    c_field_t *fields,*last_field;
    const char *tag;
};
typedef enum { S_OBJECT,S_FUNCTION,S_TYPEDEF,S_ENUM,S_PARAMETER,S_STRING } symbol_kind_t;
typedef struct symbol {
    unsigned id;
    const char *name;
    symbol_kind_t kind;
    c_type_t *type;
    int defined,tentative,linkage,static_storage,register_storage,noreturn,used;
    uint32_t alignment,requested_alignment;
    const char *section;
    const char *section_name;
    uint64_t value;
    unsigned char *bytes;
    size_t byte_count;
    struct symbol *next;
} symbol_t;
typedef struct scope scope_t;
typedef struct sem_info {
    c_type_t *type,*conversion,*operation_type;
    symbol_t *symbol;
    scope_t *parameters;
    uint64_t value;
    int constant,ice,lvalue,address;
    int64_t addend;
    uint32_t field_offset;
    node_t *selected;
} sem_info_t;
typedef struct sema sema_t;

/* The model and annotations are owned by the parser arena. */
sema_t *badge_sema_analyze(parser_t *,node_t *);
symbol_t *badge_sema_symbols(sema_t *);
unsigned badge_sema_symbol_count(sema_t *);
c_type_t *badge_sema_primitive(sema_t *,type_kind_t);
unsigned char *badge_sema_literal(sema_t *,node_t *,size_t *);
#endif
