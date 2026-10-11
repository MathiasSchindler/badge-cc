#define _POSIX_C_SOURCE 200809L
#include "badge_object.h"
#ifdef BADGE_ASM_LIBRARY
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include "host_io.h"
#endif
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>

#define AS_MAX_SOURCE (16u*1024u*1024u)
#define AS_MAX_LINES 500000u
#define AS_MAX_MEMORY (192u*1024u*1024u)
#ifndef AS_MAX_LAYOUT_PASSES
#define AS_MAX_LAYOUT_PASSES 128u
#endif

typedef struct as_memory { void *data; struct as_memory *next; } as_memory_t;
typedef struct {
    int op;
    union { int64_t value; uint32_t symbol; } arg;
} as_expression_code_t;
typedef struct as_expression_cache {
    const char *text;
    as_expression_code_t *code;
    size_t count, capacity, stack, stack_max;
    struct as_expression_cache *next;
} as_expression_cache_t;
enum {
    AS_D_NONE, AS_D_UNKNOWN, AS_D_LABEL, AS_D_SECTION, AS_D_TEXT, AS_D_DATA, AS_D_BSS,
    AS_D_P2ALIGN, AS_D_BALIGN, AS_D_WORD, AS_D_LONG, AS_D_HWORD, AS_D_SHORT, AS_D_BYTE,
    AS_D_ASCII, AS_D_ASCIZ, AS_D_ZERO, AS_D_SPACE, AS_D_INCBIN, AS_D_GLOBAL, AS_D_GLOBL,
    AS_D_WEAK, AS_D_HIDDEN, AS_D_TYPE, AS_D_SIZE, AS_D_EQU, AS_D_SET, AS_D_COMM,
    AS_D_SYNTAX, AS_D_CPU, AS_D_FPU, AS_D_THUMB, AS_D_CODE, AS_D_THUMB_FUNC,
    AS_D_FNSTART, AS_D_CANTUNWIND, AS_D_PAD, AS_D_SAVE, AS_D_FNEND, AS_D_EABI_ATTRIBUTE,
    AS_D_FILE, AS_D_IDENT, AS_D_ADDRSIG, AS_D_ADDRSIG_SYM, AS_D_IF, AS_D_ELSE,
    AS_D_ENDIF, AS_D_ERROR, AS_D_ARCH, AS_D_LTORG, AS_D_POOL
};
static unsigned as_directive_code(const char *op) {
    static const char *const names[]={
        "label",".section",".text",".data",".bss",".p2align",".balign",
        ".word",".long",".hword",".short",".byte",".ascii",".asciz",".zero",".space",
        ".incbin",".global",".globl",".weak",".hidden",".type",".size",".equ",".set",
        ".comm",".syntax",".cpu",".fpu",".thumb",".code",".thumb_func",".fnstart",
        ".cantunwind",".pad",".save",".fnend",".eabi_attribute",".file",".ident",
        ".addrsig",".addrsig_sym",".if",".else",".endif",".error",".arch",".ltorg",".pool"
    };
    if(op[0]!='.' && strcmp(op,"label")!=0) return AS_D_NONE;
    for(size_t k=0;k<sizeof(names)/sizeof(*names);++k)
        if(strcmp(op,names[k])==0) return (unsigned)k+AS_D_LABEL;
    return AS_D_UNKNOWN;
}
typedef struct { int64_t value; uint32_t positive, negative; int unknown; } as_value_t;
typedef struct {
    char *op, *arg[16];
    unsigned count, line, width;
    const char *mnemonic;
    int condition;
    unsigned encoding, directive;
    uint32_t layout_section, layout_offset, layout_end_section, layout_end_offset;
    unsigned char *binary;
    size_t binary_size;
    unsigned origin_line;
    const char *decision;
    int64_t displacement, reach_min, reach_max;
    unsigned pc_base, has_reach, alignment, padding, padding_skipped;
    uint32_t label_symbol, numeric, label_number, literal_symbol;
    size_t pool_next, pool_match, pool_hash_next;
    unsigned pool_generation, pool_section;
    as_value_t pool_value;
} as_line_t;
typedef struct { uint32_t number, symbol; size_t record; } as_numeric_t;
typedef struct as_listing_range {
    uint32_t section, offset, size;
    size_t record;
    unsigned pool_padding;
    struct as_listing_range *next;
} as_listing_range_t;
typedef struct {
    bo_section_t out;
    size_t capacity;
    uint32_t section_symbol;
    int mapping;
    unsigned fallthrough;
    size_t pool_first, pool_last;
    unsigned pool_generation;
} as_section_t;
typedef struct {
    bo_symbol_t out;
    uint32_t next, raw_value;
    unsigned definition, thumb, referenced, declared, busy;
    const char *equ;
    uint32_t equ_section, equ_offset;
    size_t equ_record;
} as_symbol_t;
typedef struct {
    const char *input, *output;
    jmp_buf failure;
    as_memory_t *memory;
    size_t allocated;
    as_line_t *lines;
    size_t line_count, line_capacity;
    as_numeric_t *numeric;
    size_t numeric_count, record;
    as_section_t *sections;
    as_symbol_t *symbols;
    bo_relocation_t *relocations;
    uint32_t buckets[8192];
    as_expression_cache_t *expressions[8192];
    size_t pools[8192];
    uint32_t section_count, symbol_count, relocation_count, current, line;
    unsigned final, changed, pending_thumb, it_count, it_at, it_conditions[4];
    unsigned unwind_section, unwind_offset, cantunwind;
    unsigned attributes[128], attr_set[128], cpu, fpu, addrsig;
    const char *attr_strings[128];
    uint32_t *significant;
    size_t significant_count;
    unsigned alias_depth;
    FILE *reading;
    unsigned char *result;
    size_t result_size;
    const char *listing, *ir_source;
    unsigned char *original;
    size_t original_size, listing_record;
    as_listing_range_t *listing_first, *listing_last;
    char *listing_bytes;
    size_t listing_size;
    unsigned ir_lines;
    unsigned pool_padding;
} assembler_t;

static _Noreturn void as_error(assembler_t *s, const char *format, ...) {
    fprintf(stderr,"badge-asm: %s:%u: ",s->input,s->line);
    va_list args; va_start(args,format); vfprintf(stderr,format,args); va_end(args);
    fputc('\n',stderr); longjmp(s->failure,1);
}

static void *as_allocate(assembler_t *s, size_t size) {
    if(size>AS_MAX_MEMORY-s->allocated) as_error(s,"192 MiB allocation limit exceeded");
    void *data=calloc(size==0?1:size,1);
    as_memory_t *m=malloc(sizeof(*m));
    if(data==NULL || m==NULL) { free(data); free(m); as_error(s,"out of memory"); }
    m->data=data; m->next=s->memory; s->memory=m; s->allocated+=size; return data;
}

static char *as_copy(assembler_t *s, const char *text) {
    size_t n=strlen(text);
    if(n>4096) as_error(s,"name exceeds 4096 bytes");
    char *copy=as_allocate(s,n+1); memcpy(copy,text,n+1); return copy;
}

static void as_destroy(assembler_t *s) {
    if(s->reading!=NULL && fclose(s->reading)!=0) fprintf(stderr,"badge-asm: failed to close incbin input\n");
    while(s->memory!=NULL) {
        as_memory_t *m=s->memory; s->memory=m->next; free(m->data); free(m);
    }
    free(s);
}

static char *as_trim(char *p) {
    while(isspace((unsigned char)*p)) ++p;
    char *end=p+strlen(p);
    while(end>p && isspace((unsigned char)end[-1])) *--end=0;
    return p;
}

static uint32_t as_hash(const char *name) {
    uint32_t h=2166136261u;
    for(const unsigned char *p=(const unsigned char *)name;*p;++p) h=(h^*p)*16777619u;
    return h%8192;
}

static uint32_t as_new_symbol(assembler_t *s, const char *name, int ordinary) {
    if(s->symbol_count==BO_MAX_SYMBOLS) as_error(s,"symbol count limit exceeded");
    uint32_t id=++s->symbol_count;
    as_symbol_t *v=&s->symbols[id]; v->out.name=as_copy(s,name);
    if(ordinary) {
        uint32_t h=as_hash(name); v->next=s->buckets[h]; s->buckets[h]=id;
    }
    return id;
}

static uint32_t as_symbol(assembler_t *s, const char *name) {
    if(isdigit((unsigned char)*name)) as_error(s,"numeric local labels cannot be named symbols");
    for(uint32_t id=s->buckets[as_hash(name)];id;id=s->symbols[id].next)
        if(strcmp(s->symbols[id].out.name,name)==0) return id;
    return as_new_symbol(s,name,1);
}

static uint32_t as_section(assembler_t *s, const char *name, uint32_t type,
                           uint32_t flags, uint32_t entry) {
    for(uint32_t k=1;k<=s->section_count;++k) {
        as_section_t *v=&s->sections[k];
        if(strcmp(v->out.name,name)!=0) continue;
        if(v->out.type!=type || v->out.flags!=flags || v->out.entry_size!=entry)
            as_error(s,"conflicting section declaration '%s'",name);
        return k;
    }
    if(s->section_count==BO_MAX_SECTIONS) as_error(s,"section count limit exceeded");
    uint32_t id=++s->section_count;
    s->sections[id].out=(bo_section_t){.name=as_copy(s,name),.type=type,.flags=flags,
        .alignment=1,.entry_size=entry};
    return id;
}

static uint32_t as_section_symbol(assembler_t *s, uint32_t section) {
    as_section_t *v=&s->sections[section];
    if(v->section_symbol==0) {
        uint32_t id=as_new_symbol(s,"",0);
        s->symbols[id].out.type=3; s->symbols[id].out.section=section;
        s->symbols[id].definition=UINT_MAX; v->section_symbol=id;
    }
    return v->section_symbol;
}

static void as_emit(assembler_t *s, const unsigned char *bytes, uint32_t size) {
    as_section_t *v=&s->sections[s->current];
    if(size>BO_MAX_FILE-v->out.size) as_error(s,"section exceeds 64 MiB");
    if(s->final && v->out.type==BO_NOBITS && bytes!=NULL)
        for(uint32_t k=0;k<size;++k) if(bytes[k]!=0) as_error(s,"nonzero initialized data in NOBITS");
    if(s->final && v->out.type!=BO_NOBITS && size!=0) {
        size_t needed=(size_t)v->out.size+size;
        if(needed>v->capacity) {
            size_t capacity=v->capacity==0?64:v->capacity;
            while(capacity<needed) capacity*=2;
            unsigned char *data=as_allocate(s,capacity);
            if(v->out.size!=0) memcpy(data,v->out.data,v->out.size);
            v->out.data=data; v->capacity=capacity;
        }
        if(bytes!=NULL) memcpy((unsigned char *)v->out.data+v->out.size,bytes,size);
        else memset((unsigned char *)v->out.data+v->out.size,0,size);
    }
    if(s->final && s->listing && size) {
        as_listing_range_t *r=s->listing_last;
        if(r && r->record==s->listing_record && r->section==s->current && r->pool_padding==s->pool_padding &&
           r->offset+r->size==v->out.size) r->size+=size;
        else {
            r=as_allocate(s,sizeof(*r)); r->record=s->listing_record;
            r->section=s->current; r->offset=v->out.size; r->size=size;
            r->pool_padding=s->pool_padding;
            if(s->listing_last) s->listing_last->next=r; else s->listing_first=r;
            s->listing_last=r;
        }
    }
    v->out.size+=size;
}

static void as_word(assembler_t *s, uint32_t value, unsigned width) {
    unsigned char bytes[4];
    for(unsigned k=0;k<width;++k) bytes[k]=(unsigned char)(value>>(k*8));
    as_emit(s,bytes,width);
}

static void as_mapping(assembler_t *s, int code) {
    as_section_t *v=&s->sections[s->current];
    if(!s->final || !(v->out.flags&4) || v->mapping==code+1) return;
    char name[32]; snprintf(name,sizeof(name),"$%c.%u",code?'t':'d',s->symbol_count);
    uint32_t id=as_new_symbol(s,name,0);
    s->symbols[id].out.section=s->current; s->symbols[id].raw_value=v->out.size;
    s->symbols[id].definition=UINT_MAX; v->mapping=code+1;
}

static void as_reloc(assembler_t *s, uint32_t offset, uint32_t type, uint32_t symbol) {
    if(!s->final) return;
    if(s->relocation_count==BO_MAX_RELOCATIONS) as_error(s,"relocation count limit exceeded");
    s->symbols[symbol].referenced=1;
    s->relocations[s->relocation_count++]=(bo_relocation_t){s->current,offset,type,symbol};
}

static unsigned as_split(assembler_t *s, char *text, char **args) {
    unsigned n=0, depth=0; int quote=0, escaped=0; char *start=text;
    for(char *p=text;;++p) {
        unsigned char c=(unsigned char)*p;
        if(quote) {
            if(c==0) as_error(s,"unterminated string");
            if(escaped) escaped=0; else if(c=='\\') escaped=1; else if(c=='"') quote=0;
        } else {
            if(c=='"') quote=1;
            else if(c=='[' || c=='{' || c=='(') ++depth;
            else if(c==']' || c=='}' || c==')') {
                if(depth==0) as_error(s,"unbalanced operand delimiters"); --depth;
            } else if((c==',' && depth==0) || c==0) {
                if(c==0 && depth!=0) as_error(s,"unbalanced operand delimiters");
                if(c==',') *p=0;
                char *arg=as_trim(start);
                if(*arg!=0) {
                    if(n==16) as_error(s,"too many operands"); args[n++]=arg;
                } else if(c==',' || n!=0) as_error(s,"empty operand");
                if(c==0) return n;
                start=p+1;
            }
        }
    }
}

static unsigned char *as_string(assembler_t *s, const char *p, size_t *length) {
    if(*p++!='"') as_error(s,"expected quoted string");
    unsigned char *out=as_allocate(s,strlen(p)+1); size_t n=0;
    while(*p!=0 && *p!='"') {
        unsigned c=(unsigned char)*p++;
        if(c=='\\') {
            c=(unsigned char)*p++;
            if(c>='0' && c<='7') {
                unsigned value=c-'0';
                for(unsigned k=1;k<3 && *p>='0' && *p<='7';++k) value=value*8+(unsigned)(*p++-'0');
                if(value>255) as_error(s,"octal escape exceeds one byte"); c=value;
            } else if(c=='n') c='\n'; else if(c=='r') c='\r'; else if(c=='t') c='\t';
            else if(c=='b') c='\b'; else if(c=='f') c='\f'; else if(c=='v') c='\v';
            else if(c!='"' && c!='\\') as_error(s,"unsupported string escape");
        }
        out[n++]=(unsigned char)c;
    }
    if(*p!='"' || p[1]!=0) as_error(s,"invalid quoted string");
    *length=n; return out;
}

enum { AS_LITERAL=256, AS_SYMBOL, AS_DOT, AS_RESOLVE, AS_NEGATE, AS_COMPLEMENT,
    AS_LOCAL_FORWARD, AS_LOCAL_BACKWARD };
static as_value_t as_expression_level(assembler_t *, const char **, unsigned, unsigned, as_expression_cache_t *);
static as_value_t as_expression(assembler_t *, const char *);

static void as_expression_code(assembler_t *s, as_expression_cache_t *c, int op, int64_t arg) {
    /* Resolution is idempotent; precedence levels often request it consecutively. */
    if(op==AS_RESOLVE && c->count && c->code[c->count-1].op==AS_RESOLVE) return;
    if(c->count==c->capacity) {
        size_t capacity=c->capacity?c->capacity*2:8;
        as_expression_code_t *code=as_allocate(s,capacity*sizeof(*code));
        if(c->count) memcpy(code,c->code,c->count*sizeof(*code));
        c->code=code; c->capacity=capacity;
    }
    as_expression_code_t *code=&c->code[c->count++]; code->op=op;
    if(op==AS_SYMBOL) code->arg.symbol=(uint32_t)arg; else code->arg.value=arg;
    if(op==AS_LITERAL || op==AS_SYMBOL || op==AS_DOT || op==AS_LOCAL_FORWARD || op==AS_LOCAL_BACKWARD) {
        if(++c->stack>c->stack_max) c->stack_max=c->stack;
        if(c->stack_max>512) as_error(s,"expression nesting limit exceeded");
    } else if(op<256) --c->stack;
}

static as_value_t as_resolve(assembler_t *s, as_value_t v) {
    uint32_t ids[2]={v.positive,v.negative};
    for(unsigned k=0;k<2;++k) {
        uint32_t id=ids[k]; if(id==0) continue;
        as_symbol_t *symbol=&s->symbols[id];
        if(symbol->equ!=NULL) {
            if(symbol->busy || ++s->alias_depth>64) as_error(s,"cyclic/deep symbol assignment");
            uint32_t current=s->current,size=s->sections[symbol->equ_section].out.size;
            size_t record=s->record;
            s->current=symbol->equ_section; s->sections[s->current].out.size=symbol->equ_offset;
            s->record=symbol->equ_record;
            symbol->busy=1; as_value_t alias=as_expression(s,symbol->equ); symbol->busy=0;
            s->sections[s->current].out.size=size; s->current=current; s->record=record; --s->alias_depth;
            if(alias.negative!=0) as_error(s,"unsupported non-affine symbol assignment");
            if(k!=0 && alias.value==INT64_MIN) as_error(s,"expression overflow");
            int64_t add=k==0?alias.value:-alias.value;
            if(__builtin_add_overflow(v.value,add,&v.value)) as_error(s,"expression overflow");
            if(k==0) v.positive=alias.positive; else v.negative=alias.positive;
            v.unknown|=alias.unknown;
        } else if(symbol->out.section==BO_ABS) {
            int64_t add=k==0?(int64_t)symbol->raw_value:-(int64_t)symbol->raw_value;
            if(__builtin_add_overflow(v.value,add,&v.value)) as_error(s,"expression overflow");
            if(k==0) v.positive=0; else v.negative=0;
        }
    }
    if(v.positive!=0 && v.negative!=0) {
        as_symbol_t *a=&s->symbols[v.positive], *b=&s->symbols[v.negative];
        if(a->definition!=0 && b->definition!=0 && a->out.section==b->out.section) {
            int64_t delta=(int64_t)a->raw_value-b->raw_value;
            if(__builtin_add_overflow(v.value,delta,&v.value)) as_error(s,"expression overflow");
            v.positive=v.negative=0;
        }
    }
    return v;
}

static void as_skip(const char **p) { while(isspace((unsigned char)**p)) ++*p; }
static uint32_t as_local_reference(assembler_t *s, uint32_t number, int forward) {
    size_t lo=0,hi=s->numeric_count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2; as_numeric_t *v=&s->numeric[mid];
        if(v->number<number || (v->number==number && v->record<s->record)) lo=mid+1;
        else hi=mid;
    }
    if(forward) {
        if(lo<s->numeric_count && s->numeric[lo].number==number && s->numeric[lo].record==s->record) ++lo;
        if(lo<s->numeric_count && s->numeric[lo].number==number) return s->numeric[lo].symbol;
    } else if(lo && s->numeric[lo-1].number==number) return s->numeric[lo-1].symbol;
    as_error(s,"unresolved numeric local reference '%" PRIu32 "%c'",number,forward?'f':'b');
}
static uint32_t as_decimal_label(assembler_t *s, const char *p, const char *end) {
    uint32_t number=0;
    for(;p<end;++p) {
        unsigned digit=(unsigned char)*p-'0';
        if(digit>9 || number>(UINT32_MAX-digit)/10) as_error(s,"numeric label outside uint32 decimal range");
        number=number*10+digit;
    }
    return number;
}
static as_value_t as_expression_atom(assembler_t *s, const char **p, unsigned depth, as_expression_cache_t *c) {
    if(depth>64) as_error(s,"expression nesting limit exceeded");
    as_skip(p); as_value_t v={0};
    if(**p=='#') { ++*p; return as_expression_atom(s,p,depth+1,c); }
    if(**p=='+' || **p=='-' || **p=='~') {
        char op=*(*p)++; v=as_resolve(s,as_expression_atom(s,p,depth+1,c));
        as_expression_code(s,c,AS_RESOLVE,0);
        if(op!='+') as_expression_code(s,c,op=='-'?AS_NEGATE:AS_COMPLEMENT,0);
        if(op=='-') {
            if(v.value==INT64_MIN) as_error(s,"expression overflow");
            v.value=-v.value; uint32_t id=v.positive; v.positive=v.negative; v.negative=id;
        } else if(op=='~') {
            if(v.positive || v.negative || v.unknown) {
                if(s->final) as_error(s,"bitwise operation requires an absolute expression");
                v=(as_value_t){.unknown=1};
            } else v.value=~v.value;
        }
        return v;
    }
    if(**p=='(') {
        ++*p; v=as_expression_level(s,p,0,depth+1,c); as_skip(p);
        if(*(*p)++!=')') as_error(s,"missing expression parenthesis"); return v;
    }
    if(isdigit((unsigned char)**p)) {
        const char *numeric_end=*p;
        while(isdigit((unsigned char)*numeric_end)) ++numeric_end;
        if((*numeric_end=='f' || *numeric_end=='b') && !isalnum((unsigned char)numeric_end[1]) &&
           numeric_end[1]!='_' && numeric_end[1]!='.' && numeric_end[1]!='$') {
            uint32_t number=as_decimal_label(s,*p,numeric_end); int forward=*numeric_end=='f';
            *p=numeric_end+1; v.positive=as_local_reference(s,number,forward);
            as_expression_code(s,c,forward?AS_LOCAL_FORWARD:AS_LOCAL_BACKWARD,number);
            as_expression_code(s,c,AS_RESOLVE,0); return as_resolve(s,v);
        }
        errno=0; char *end; unsigned long long n=strtoull(*p,&end,0);
        if(errno!=0 || end==*p || n>INT64_MAX) as_error(s,"integer outside signed 64-bit expression range");
        *p=end; v.value=(int64_t)n; as_expression_code(s,c,AS_LITERAL,v.value); return v;
    }
    if(isalpha((unsigned char)**p) || **p=='_' || **p=='.' || **p=='$') {
        char name[4097]; size_t n=0;
        while(isalnum((unsigned char)**p) || **p=='_' || **p=='.' || **p=='$') {
            if(n==4096) as_error(s,"symbol name limit exceeded"); name[n++]=*(*p)++;
        }
        name[n]=0;
        int dot=strcmp(name,".")==0;
        if(dot) {
            snprintf(name,sizeof(name),".asm.dot.%u.%u",s->current,s->sections[s->current].out.size);
            uint32_t id=as_symbol(s,name); s->symbols[id].out.section=s->current;
            s->symbols[id].raw_value=s->sections[s->current].out.size; s->symbols[id].definition=UINT_MAX;
            v.positive=id;
        } else v.positive=as_symbol(s,name);
        as_expression_code(s,c,dot?AS_DOT:AS_SYMBOL,v.positive);
        as_expression_code(s,c,AS_RESOLVE,0); return as_resolve(s,v);
    }
    as_error(s,"invalid expression near '%s'",*p);
}

static as_value_t as_expression_binary(assembler_t *s, as_value_t left, as_value_t right, int op) {
    left=as_resolve(s,left); right=as_resolve(s,right);
    if(op=='+' || op=='-') {
        if(op=='-') {
            if(right.value==INT64_MIN) as_error(s,"expression overflow");
            right.value=-right.value; uint32_t id=right.positive; right.positive=right.negative; right.negative=id;
        }
        if(left.positive && right.negative && left.positive==right.negative) left.positive=right.negative=0;
        if(left.negative && right.positive && left.negative==right.positive) left.negative=right.positive=0;
        if((left.positive && right.positive) || (left.negative && right.negative)) as_error(s,"non-affine expression");
        if(__builtin_add_overflow(left.value,right.value,&left.value)) as_error(s,"expression overflow");
        if(right.positive) left.positive=right.positive; if(right.negative) left.negative=right.negative;
        left.unknown|=right.unknown; return left;
    }
    if(left.positive || left.negative || right.positive || right.negative || left.unknown || right.unknown) {
        if(s->final) as_error(s,"operator requires absolute resolved expressions");
        return (as_value_t){.unknown=1};
    }
    int64_t a=left.value,b=right.value;
    if(op=='*') { if(__builtin_mul_overflow(a,b,&left.value)) as_error(s,"expression overflow"); }
    else if(op=='/' || op=='%') {
        if(b==0 || (a==INT64_MIN && b==-1)) as_error(s,"invalid division");
        left.value=op=='/'?a/b:a%b;
    } else if(op=='<') {
        if(b<0 || b>63) as_error(s,"invalid shift"); left.value=(int64_t)((uint64_t)a<<(unsigned)b);
    } else if(op=='>') {
        if(b<0 || b>63) as_error(s,"invalid shift"); left.value=a>>(unsigned)b;
    } else if(op=='&') left.value=a&b; else if(op=='|') left.value=a|b;
    else if(op=='^') left.value=a^b; else if(op=='=') left.value=a==b;
    else left.value=a!=b;
    return left;
}

static as_value_t as_expression_level(assembler_t *s, const char **p, unsigned level, unsigned depth, as_expression_cache_t *c) {
    static const char *const operators[]={"|","^","&","== !=","<< >>","+ -","* / %"};
    if(level==7) return as_expression_atom(s,p,depth,c);
    as_value_t left=as_expression_level(s,p,level+1,depth,c);
    for(;;) {
        as_skip(p); char op[3]={0};
        if(level==3 && ((**p=='=' && (*p)[1]=='=') || (**p=='!' && (*p)[1]=='='))) { op[0]=**p; op[1]='='; }
        else if(level==4 && ((**p=='<' && (*p)[1]=='<') || (**p=='>' && (*p)[1]=='>'))) { op[0]=**p; op[1]=**p; }
        else if(level!=3 && level!=4 && **p!=0 && strchr(operators[level],**p)!=NULL && **p!=' ') op[0]=**p;
        else { as_expression_code(s,c,AS_RESOLVE,0); return as_resolve(s,left); }
        *p+=strlen(op);
        as_value_t right=as_expression_level(s,p,level+1,depth,c);
        left=as_expression_binary(s,left,right,op[0]); as_expression_code(s,c,op[0],0);
    }
}

static as_value_t as_expression(assembler_t *s, const char *text) {
    uint32_t bucket=as_hash(text);
    as_expression_cache_t *c=s->expressions[bucket];
    for(;c;c=c->next) if(strcmp(c->text,text)==0) break;
    if(c==NULL) {
        c=as_allocate(s,sizeof(*c));
        /* Operands in memory/shift parsers may point into temporary local buffers. */
        size_t length=strlen(text); char *copy=as_allocate(s,length+1); memcpy(copy,text,length+1); c->text=copy;
        const char *p=text; as_value_t v=as_expression_level(s,&p,0,0,c); as_skip(&p);
        if(*p!=0) as_error(s,"unexpected expression suffix '%s'",p);
        c->next=s->expressions[bucket]; s->expressions[bucket]=c;
        return as_resolve(s,v);
    }
    as_value_t values[c->stack_max]; size_t count=0;
    for(size_t k=0;k<c->count;++k) {
        as_expression_code_t *code=&c->code[k];
        if(code->op==AS_LITERAL) values[count++]=(as_value_t){.value=code->arg.value};
        else if(code->op==AS_SYMBOL) values[count++]=(as_value_t){.positive=code->arg.symbol};
        /* Cached directional references must still use the current source record. */
        else if(code->op==AS_LOCAL_FORWARD || code->op==AS_LOCAL_BACKWARD)
            values[count++]=(as_value_t){.positive=as_local_reference(s,(uint32_t)code->arg.value,
                code->op==AS_LOCAL_FORWARD)};
        else if(code->op==AS_DOT) {
            char name[64]; snprintf(name,sizeof(name),".asm.dot.%u.%u",s->current,s->sections[s->current].out.size);
            uint32_t id=as_symbol(s,name); s->symbols[id].out.section=s->current;
            s->symbols[id].raw_value=s->sections[s->current].out.size; s->symbols[id].definition=UINT_MAX;
            values[count++]=(as_value_t){.positive=id};
        } else if(code->op==AS_RESOLVE) values[count-1]=as_resolve(s,values[count-1]);
        else if(code->op==AS_NEGATE) {
            as_value_t *v=&values[count-1];
            if(v->value==INT64_MIN) as_error(s,"expression overflow");
            v->value=-v->value; uint32_t id=v->positive; v->positive=v->negative; v->negative=id;
        } else if(code->op==AS_COMPLEMENT) {
            as_value_t *v=&values[count-1];
            if(v->positive || v->negative || v->unknown) {
                if(s->final) as_error(s,"bitwise operation requires an absolute expression");
                *v=(as_value_t){.unknown=1};
            } else v->value=~v->value;
        } else {
            as_value_t right=values[--count];
            values[count-1]=as_expression_binary(s,values[count-1],right,code->op);
        }
    }
    return as_resolve(s,values[0]);
}

static int64_t as_absolute(assembler_t *s, const char *text) {
    as_value_t v=as_expression(s,text);
    if(v.positive || v.negative || v.unknown) as_error(s,"expected an absolute expression");
    return v.value;
}

static uint32_t as_bounded(assembler_t *s, const char *text, uint32_t max) {
    int64_t n=as_absolute(s,text);
    if(n<0 || (uint64_t)n>max) as_error(s,"operand outside range 0..%" PRIu32,max);
    return (uint32_t)n;
}

static int as_register(assembler_t *s, const char *text) {
    if(strcmp(text,"sp")==0) return 13; if(strcmp(text,"lr")==0) return 14;
    if(strcmp(text,"pc")==0) return 15; if(strcmp(text,"ip")==0) return 12;
    if(text[0]=='r' && isdigit((unsigned char)text[1])) {
        char *end; long n=strtol(text+1,&end,10); if(*end==0 && n>=0 && n<=15) return (int)n;
    }
    as_error(s,"invalid register '%s'",text);
}

static uint32_t as_register_list(assembler_t *s, const char *text) {
    size_t n=strlen(text);
    if(n<3 || text[0]!='{' || text[n-1]!='}') as_error(s,"expected register list");
    char buffer[256]; if(n>=sizeof(buffer)) as_error(s,"register list too long");
    memcpy(buffer,text+1,n-2); buffer[n-2]=0;
    char *args[16]; unsigned count=as_split(s,buffer,args); uint32_t mask=0;
    for(unsigned k=0;k<count;++k) {
        char *dash=strchr(args[k],'-'); int first,last;
        if(dash) { *dash=0; first=as_register(s,as_trim(args[k])); last=as_register(s,as_trim(dash+1)); }
        else first=last=as_register(s,args[k]);
        if(last<first) as_error(s,"reversed register range");
        for(int r=first;r<=last;++r) {
            if(mask&(1u<<r)) as_error(s,"duplicate register in list"); mask|=1u<<r;
        }
    }
    if(mask==0) as_error(s,"empty register list"); return mask;
}

static int as_condition(const char *text) {
    static const char *const names[]={"eq","ne","hs","lo","mi","pl","vs","vc","hi","ls","ge","lt","gt","le","al"};
    if(strcmp(text,"cs")==0) return 2; if(strcmp(text,"cc")==0) return 3;
    for(unsigned k=0;k<15;++k) if(strcmp(text,names[k])==0) return (int)k;
    return -1;
}

static unsigned as_instruction(assembler_t *, as_line_t *);

static as_line_t *as_append_line(assembler_t *s) {
    if(s->line_count==AS_MAX_LINES) as_error(s,"source record limit exceeded");
    if(s->line_count==s->line_capacity) {
        size_t capacity=s->line_capacity*2;
        if(capacity>AS_MAX_LINES) capacity=AS_MAX_LINES;
        as_line_t *lines=as_allocate(s,capacity*sizeof(*lines));
        memcpy(lines,s->lines,s->line_count*sizeof(*lines)); s->lines=lines; s->line_capacity=capacity;
    }
    return &s->lines[s->line_count++];
}
static int as_numeric_compare(const void *a, const void *b) {
    const as_numeric_t *x=a,*y=b;
    if(x->number!=y->number) return x->number<y->number?-1:1;
    return x->record<y->record?-1:x->record>y->record;
}
static void as_parse(assembler_t *s, unsigned char *input, size_t length) {
    if(memchr(input,0,length)!=NULL) as_error(s,"NUL byte in assembly");
    size_t capacity=2;
    for(size_t k=0;k<length;++k) if(input[k]=='\n') capacity+=2;
    if(capacity>AS_MAX_LINES) capacity=AS_MAX_LINES;
    s->lines=as_allocate(s,capacity*sizeof(*s->lines)); s->line_capacity=capacity;
    char *line=(char *)input; unsigned number=0,origin=0;
    while(line!=NULL) {
        ++number; s->line=number;
        char *next=strchr(line,'\n'); if(next!=NULL) *next++=0;
        if(s->ir_source) {
            unsigned value; char tail;
            if(sscanf(line," @ attribution-ir %u %c",&value,&tail)==1) {
                if(!value || value>s->ir_lines) as_error(s,"invalid attribution IR line");
                origin=value;
            } else if(!strncmp(as_trim(line),"@ attribution-ir",16)) {
                as_error(s,"malformed attribution IR marker");
            } else if(strstr(line,"@ attribution-function ")!=NULL ||
                      strstr(line,".type ")!=NULL || strstr(line,".type\t")!=NULL) origin=0;
        }
        int quoted=0,escaped=0;
        for(char *p=line;*p;++p) {
            if(quoted) { if(escaped) escaped=0; else if(*p=='\\') escaped=1; else if(*p=='"') quoted=0; }
            else if(*p=='"') quoted=1;
            else if(*p=='@' || (*p=='/' && p[1]=='/')) { *p=0; break; }
        }
        char *p=as_trim(line);
        if(*p=='#') {
            char *marker=as_trim(p+1);
            if(!isdigit((unsigned char)*marker)) as_error(s,"unprocessed directive");
            line=next; continue;
        }
        if(*p==0) { line=next; continue; }
        char *end=p;
        while(isalnum((unsigned char)*end) || *end=='.' || *end=='_' || *end=='$') ++end;
        while(*end==':') {
            if(end==p) as_error(s,"empty label");
            *end=0; uint32_t id;
            as_line_t *record=as_append_line(s);
            if(isdigit((unsigned char)*p)) {
                record->numeric=1; record->label_number=as_decimal_label(s,p,end);
                char name[64]; snprintf(name,sizeof(name),".Lasm.numeric.%zu",s->line_count);
                id=as_new_symbol(s,name,0); ++s->numeric_count;
            } else id=as_symbol(s,p);
            if(s->symbols[id].definition || s->symbols[id].equ) as_error(s,"duplicate label '%s'",p);
            s->symbols[id].definition=(unsigned)s->line_count;
            record->op="label"; record->arg[0]=p; record->label_symbol=id;
            record->count=1; record->line=number; p=as_trim(end+1);
            record->origin_line=origin;
            if(*p==0) break;
            end=p;
            while(isalnum((unsigned char)*end) || *end=='.' || *end=='_' || *end=='$') ++end;
        }
        if(*p==0) { line=next; continue; }
        as_line_t *record=as_append_line(s); record->line=number;
        record->origin_line=origin;
        char *assignment=as_trim(end);
        if(end!=p && *assignment=='=') {
            if(isdigit((unsigned char)*p)) as_error(s,"numeric label cannot be assigned");
            char *value=as_trim(assignment+1); *end=0;
            record->op=".set"; record->arg[0]=p; record->arg[1]=value; record->count=2;
        } else {
            end=p; while(*end && !isspace((unsigned char)*end)) ++end;
            if(*end) *end++=0; record->op=p; record->count=as_split(s,as_trim(end),record->arg);
        }
        line=next;
    }
    for(size_t k=0;k<s->line_count;++k)
        s->lines[k].directive=as_directive_code(s->lines[k].op);
    s->numeric=as_allocate(s,s->numeric_count*sizeof(*s->numeric));
    size_t at=0;
    for(size_t k=0;k<s->line_count;++k) if(s->lines[k].numeric)
        s->numeric[at++]=(as_numeric_t){s->lines[k].label_number,s->lines[k].label_symbol,k};
    qsort(s->numeric,s->numeric_count,sizeof(*s->numeric),as_numeric_compare);
}

static void as_alignment(assembler_t *s, as_line_t *l, int power) {
    if(l->count<1 || l->count>3) as_error(s,"invalid alignment operands");
    uint32_t alignment=as_bounded(s,l->arg[0],power?16:65536);
    if(power) alignment=1u<<alignment;
    if(alignment==0 || (alignment&(alignment-1))) as_error(s,"alignment must be a power of two");
    as_section_t *v=&s->sections[s->current]; if(v->out.alignment<alignment) v->out.alignment=alignment;
    uint32_t pad=(0u-v->out.size)&(alignment-1);
    l->alignment=alignment; l->padding=pad; l->padding_skipped=0;
    if(l->count==3 && pad>as_bounded(s,l->arg[2],BO_MAX_FILE)) { l->padding_skipped=1; return; }
    if(l->count>=2) {
        unsigned char fill=(unsigned char)as_bounded(s,l->arg[1],255);
        for(uint32_t k=0;k<pad;++k) as_emit(s,&fill,1);
    } else if(v->out.flags&4) {
        if(pad&1) { as_word(s,0,1); --pad; }
        while(pad) { as_word(s,0xbf00,2); pad-=2; }
    } else as_emit(s,NULL,pad);
}

static void as_data_value(assembler_t *s, as_value_t v, unsigned width) {
    uint32_t place=s->sections[s->current].out.size;
    if(s->sections[s->current].out.type==BO_NOBITS &&
        (v.value!=0 || v.positive || v.negative || v.unknown))
        as_error(s,"nonzero initialized data in NOBITS");
    if(s->final && (v.positive || v.negative || v.unknown)) {
        if(width!=4 || v.positive==0 || v.unknown) as_error(s,"unsupported data relocation expression");
        if(v.negative) {
            as_symbol_t *neg=&s->symbols[v.negative];
            if(neg->definition==0 || neg->out.section!=s->current) as_error(s,"subtracted symbol must belong to the relocation section");
            if(__builtin_add_overflow(v.value,(int64_t)place-neg->raw_value,&v.value)) as_error(s,"relocation addend overflow");
            as_reloc(s,place,3,v.positive);
        } else as_reloc(s,place,2,v.positive);
    }
    if(s->final && (v.value<-(INT64_C(1)<<(width*8-1)) ||
        (v.value>=0 && (uint64_t)v.value>(UINT64_C(1)<<(width*8))-1)))
        as_error(s,"data value does not fit its width");
    as_word(s,(uint32_t)v.value,width);
}
static void as_data(assembler_t *s, as_line_t *l, unsigned width) {
    if(l->count==0) as_error(s,"missing data operand");
    as_mapping(s,0);
    for(unsigned k=0;k<l->count;++k) as_data_value(s,as_expression(s,l->arg[k]),width);
}

static uint32_t as_literal_request(assembler_t *s, as_line_t *l, const char *text) {
    as_value_t value=as_expression(s,text);
    if(!value.positive && !value.negative && !value.unknown &&
       value.value>=INT32_MIN && value.value<=UINT32_MAX) value.value=(uint32_t)value.value;
    if(l->literal_symbol==0) {
        char name[64]; snprintf(name,sizeof(name),".Lasm.pool.%zu",s->record);
        l->literal_symbol=as_new_symbol(s,name,0);
        s->symbols[l->literal_symbol].definition=UINT_MAX;
    }
    as_section_t *section=&s->sections[s->current]; size_t record=s->record+1;
    l->pool_value=value; l->pool_next=0; l->pool_match=0;
    l->pool_generation=section->pool_generation;
    l->pool_section=s->current;
    uint64_t bits=(uint64_t)value.value;
    uint32_t hash=(uint32_t)bits^(uint32_t)(bits>>32);
    hash=(hash*16777619u)^value.positive; hash=(hash*16777619u)^value.negative;
    hash=(hash*16777619u)^s->current; hash=(hash*16777619u)^section->pool_generation;
    hash%=8192;
    for(size_t k=s->pools[hash];k;k=s->lines[k-1].pool_hash_next) {
        as_line_t *other=&s->lines[k-1]; as_value_t v=other->pool_value;
        if(other->pool_section==s->current && other->pool_generation==section->pool_generation &&
           v.value==value.value && v.positive==value.positive && v.negative==value.negative &&
           v.unknown==value.unknown) { l->pool_match=k; break; }
    }
    if(!l->pool_match) { l->pool_hash_next=s->pools[hash]; s->pools[hash]=record; }
    if(section->pool_last) s->lines[section->pool_last-1].pool_next=record;
    else section->pool_first=record;
    section->pool_last=record;
    return l->literal_symbol;
}

static void as_literal_pool(assembler_t *s, as_line_t *l) {
    if(l->count || s->it_count) as_error(s,"literal pool requires no operands and must be outside IT");
    as_section_t *section=&s->sections[s->current];
    if(!section->pool_first) return;
    if(section->fallthrough) as_error(s,"literal pool could be executed by fallthrough; branch or return before .ltorg/.pool");
    as_mapping(s,0);
    l->alignment=4; l->padding=(0u-section->out.size)&3; l->padding_skipped=0;
    if(section->out.alignment<4) section->out.alignment=4;
    s->pool_padding=1; as_emit(s,NULL,l->padding); s->pool_padding=0;
    for(size_t k=section->pool_first;k;k=s->lines[k-1].pool_next) {
        as_line_t *load=&s->lines[k-1]; as_symbol_t *symbol=&s->symbols[load->literal_symbol];
        symbol->out.section=s->current;
        if(load->pool_match) symbol->raw_value=s->symbols[s->lines[load->pool_match-1].literal_symbol].raw_value;
        else {
            symbol->raw_value=section->out.size;
            as_data_value(s,as_resolve(s,load->pool_value),4);
        }
    }
    section->pool_first=section->pool_last=0; ++section->pool_generation;
}

static void as_incbin(assembler_t *s, as_line_t *l) {
    if(l->count<1 || l->count>3) as_error(s,"invalid incbin operands");
    if(l->binary==NULL) {
        size_t name_size; unsigned char *name=as_string(s,l->arg[0],&name_size);
        if(memchr(name,0,name_size)!=NULL) as_error(s,"NUL in incbin filename");
        FILE *f=fopen((char *)name,"rb"); if(f==NULL) as_error(s,"cannot open incbin: %s",strerror(errno));
        s->reading=f;
        struct stat st,dest;
        int ok=fstat(fileno(f),&st)==0 && S_ISREG(st.st_mode) && st.st_size>=0 && (uint64_t)st.st_size<=BO_MAX_FILE;
        if(!ok) { fclose(f); s->reading=NULL; as_error(s,"incbin must be a regular file of at most 64 MiB"); }
        if(stat(s->output,&dest)==0 && st.st_dev==dest.st_dev && st.st_ino==dest.st_ino) {
            fclose(f); s->reading=NULL; as_error(s,"output aliases incbin input");
        }
        if(s->listing && stat(s->listing,&dest)==0 && st.st_dev==dest.st_dev && st.st_ino==dest.st_ino) {
            fclose(f); s->reading=NULL; as_error(s,"listing aliases incbin input");
        }
        uint32_t start=l->count>=2?as_bounded(s,l->arg[1],(uint32_t)st.st_size):0;
        uint32_t count=l->count==3?as_bounded(s,l->arg[2],(uint32_t)st.st_size-start):(uint32_t)st.st_size-start;
        unsigned char *data=as_allocate(s,(size_t)count+1);
        ok=fseek(f,(long)start,SEEK_SET)==0 && fread(data,1,count,f)==count && !ferror(f);
        if(fclose(f)!=0) ok=0;
        s->reading=NULL;
        if(!ok) as_error(s,"failed to read incbin"); l->binary=data; l->binary_size=count;
    }
    as_mapping(s,0); as_emit(s,l->binary,(uint32_t)l->binary_size);
}

static void as_directive(assembler_t *s, as_line_t *l) {
    const char *op=l->op; unsigned n=l->count, d=l->directive;
    if(d==AS_D_LABEL) {
        as_symbol_t *v=&s->symbols[l->label_symbol];
        v->out.section=s->current; v->raw_value=s->sections[s->current].out.size;
        s->sections[s->current].fallthrough=1;
        if(s->pending_thumb) { v->thumb=1; v->out.type=2; s->pending_thumb=0; }
    } else if(d==AS_D_SECTION) {
        if(n<1 || n>4 || s->it_count) as_error(s,"invalid section operands or interrupted IT");
        uint32_t flags=0,type=BO_PROGBITS,entry=0;
        if(n>=2) {
            size_t len; unsigned char *f=as_string(s,l->arg[1],&len);
            for(size_t k=0;k<len;++k) {
                if(f[k]=='a') flags|=2; else if(f[k]=='w') flags|=1; else if(f[k]=='x') flags|=4;
                else if(f[k]=='M') flags|=16; else if(f[k]=='S') flags|=32;
                else if(f[k]=='e') flags|=0x80000000u; else as_error(s,"unsupported section flag");
            }
        }
        if(n>=3) {
            if(strcmp(l->arg[2],"%nobits")==0) type=BO_NOBITS;
            else if(strcmp(l->arg[2],"%progbits")!=0) as_error(s,"unsupported section type");
        }
        if(n==4) entry=as_bounded(s,l->arg[3],BO_MAX_FILE);
        const char *name=l->arg[0];
        if(*name=='"') {
            size_t length; unsigned char *decoded=as_string(s,name,&length);
            if(memchr(decoded,0,length)!=NULL) as_error(s,"NUL in section name");
            name=(char *)decoded;
        }
        s->current=as_section(s,name,type,flags,entry);
    } else if(d==AS_D_TEXT || d==AS_D_DATA || d==AS_D_BSS) {
        if(n!=0 || s->it_count) as_error(s,"invalid section switch");
        uint32_t type=d==AS_D_BSS?BO_NOBITS:BO_PROGBITS;
        s->current=as_section(s,op,type,d==AS_D_TEXT?6:3,0);
    } else if(d==AS_D_P2ALIGN || d==AS_D_BALIGN) as_alignment(s,l,d==AS_D_P2ALIGN);
    else if(d==AS_D_LTORG || d==AS_D_POOL) as_literal_pool(s,l);
    else if(d==AS_D_WORD || d==AS_D_LONG) as_data(s,l,4);
    else if(d==AS_D_HWORD || d==AS_D_SHORT) as_data(s,l,2);
    else if(d==AS_D_BYTE) as_data(s,l,1);
    else if(d==AS_D_ASCII || d==AS_D_ASCIZ) {
        if(n==0) as_error(s,"missing string operand"); as_mapping(s,0);
        if(l->binary==NULL) {
            size_t total=0; unsigned char *strings[16]; size_t lengths[16];
            for(unsigned k=0;k<n;++k) { strings[k]=as_string(s,l->arg[k],&lengths[k]); total+=lengths[k]+(d==AS_D_ASCIZ); }
            l->binary=as_allocate(s,total+1); l->binary_size=total; size_t at=0;
            for(unsigned k=0;k<n;++k) { memcpy(l->binary+at,strings[k],lengths[k]); at+=lengths[k]; if(d==AS_D_ASCIZ) ++at; }
        }
        as_emit(s,l->binary,(uint32_t)l->binary_size);
    } else if(d==AS_D_ZERO || d==AS_D_SPACE) {
        if(n<1 || n>2) as_error(s,"invalid zero/space operands");
        uint32_t count=as_bounded(s,l->arg[0],BO_MAX_FILE); as_mapping(s,0);
        if(n==2) {
            unsigned char fill=(unsigned char)as_bounded(s,l->arg[1],255);
            if(s->sections[s->current].out.type==BO_NOBITS && fill!=0) as_error(s,"nonzero fill in NOBITS");
            for(uint32_t k=0;k<count;++k) as_emit(s,&fill,1);
        } else as_emit(s,NULL,count);
    } else if(d==AS_D_INCBIN) as_incbin(s,l);
    else if(d==AS_D_GLOBAL || d==AS_D_GLOBL || d==AS_D_WEAK || d==AS_D_HIDDEN) {
        if(n==0) as_error(s,"missing symbol");
        for(unsigned k=0;k<n;++k) {
            as_symbol_t *v=&s->symbols[as_symbol(s,l->arg[k])]; v->declared=1;
            if(d==AS_D_HIDDEN) v->out.visibility=2;
            else v->out.binding=d==AS_D_WEAK?2:1;
        }
    } else if(d==AS_D_TYPE) {
        if(n!=2) as_error(s,"invalid type operands");
        as_symbol_t *v=&s->symbols[as_symbol(s,l->arg[0])];
        if(strcmp(l->arg[1],"%function")==0) v->out.type=2;
        else if(strcmp(l->arg[1],"%object")==0) v->out.type=1; else as_error(s,"unsupported symbol type");
    } else if(d==AS_D_SIZE) {
        if(n!=2) as_error(s,"invalid size operands");
        if(s->final) s->symbols[as_symbol(s,l->arg[0])].out.size=as_bounded(s,l->arg[1],UINT32_MAX);
    } else if(d==AS_D_EQU || d==AS_D_SET) {
        if(n!=2) as_error(s,"invalid symbol assignment");
        as_symbol_t *v=&s->symbols[as_symbol(s,l->arg[0])];
        if(v->definition && v->equ==NULL) as_error(s,"symbol redefinition");
        if(v->equ!=NULL && v->equ!=l->arg[1]) as_error(s,"symbol redefinition");
        v->equ=l->arg[1]; v->equ_section=s->current; v->equ_offset=s->sections[s->current].out.size;
        v->equ_record=s->record;
    } else if(d==AS_D_COMM) {
        if(n<2 || n>3) as_error(s,"invalid common declaration");
        as_symbol_t *v=&s->symbols[as_symbol(s,l->arg[0])]; v->declared=1; v->definition=UINT_MAX;
        v->out.binding=1; v->out.type=1; v->out.section=BO_COMMON;
        v->out.size=as_bounded(s,l->arg[1],UINT32_MAX); v->raw_value=n==3?as_bounded(s,l->arg[2],65536):4;
    } else if(d==AS_D_SYNTAX) {
        if(n!=1 || strcmp(l->arg[0],"unified")!=0) as_error(s,"only unified syntax is supported");
    } else if(d==AS_D_CPU) {
        if(n!=1 || strcmp(l->arg[0],"cortex-m33")!=0) as_error(s,"only Cortex-M33 is supported"); s->cpu=1;
    } else if(d==AS_D_ARCH) {
        if(n!=1 || strcmp(l->arg[0],"armv8-m.main")!=0)
            as_error(s,"only .arch armv8-m.main is supported for RP2350 Cortex-M33");
        s->cpu=1;
    } else if(d==AS_D_FPU) {
        if(n!=1 || strcmp(l->arg[0],"fpv5-sp-d16")!=0) as_error(s,"unsupported FPU"); s->fpu=1;
    } else if(d==AS_D_THUMB || d==AS_D_CODE) {
        if((d==AS_D_THUMB && n!=0) || (d==AS_D_CODE && (n!=1 || as_absolute(s,l->arg[0])!=16)))
            as_error(s,"only Thumb mode is supported");
    } else if(d==AS_D_THUMB_FUNC) {
        if(n>1) as_error(s,"invalid thumb_func operands");
        if(n==1) { as_symbol_t *v=&s->symbols[as_symbol(s,l->arg[0])]; v->thumb=1; v->out.type=2; }
        else s->pending_thumb=1;
    } else if(d==AS_D_FNSTART) {
        if(n!=0 || s->unwind_section || !(s->sections[s->current].out.flags&4)) as_error(s,"invalid fnstart");
        s->unwind_section=s->current; s->unwind_offset=s->sections[s->current].out.size; s->cantunwind=0;
    } else if(d==AS_D_CANTUNWIND) {
        if(n!=0 || !s->unwind_section) as_error(s,"cantunwind outside function"); s->cantunwind=1;
    } else if(d==AS_D_PAD || d==AS_D_SAVE) {
        if(n!=1 || !s->unwind_section) as_error(s,"unwind annotation outside function");
        if(d==AS_D_PAD) { if(as_bounded(s,l->arg[0],BO_MAX_FILE)%4) as_error(s,"unaligned stack pad"); }
        else (void)as_register_list(s,l->arg[0]);
    } else if(d==AS_D_FNEND) {
        if(n!=0 || !s->unwind_section || !s->cantunwind || s->current!=s->unwind_section)
            as_error(s,"only cantunwind function records are supported");
        uint32_t code=s->current,offset=s->unwind_offset; char name[4097];
        if(snprintf(name,sizeof(name),".ARM.exidx%s",s->sections[code].out.name)>=(int)sizeof(name)) as_error(s,"unwind section name too long");
        s->current=as_section(s,name,BO_ARM_EXIDX,130,0); s->sections[s->current].out.alignment=4;
        s->sections[s->current].out.link=code;
        as_reloc(s,s->sections[s->current].out.size,42,as_section_symbol(s,code));
        as_word(s,offset,4); as_word(s,1,4); s->current=code; s->unwind_section=0;
    } else if(d==AS_D_EABI_ATTRIBUTE) {
        if(n!=2) as_error(s,"invalid attribute operands"); unsigned tag=as_bounded(s,l->arg[0],127);
        if(tag==5 || tag==67) {
            if(s->attr_strings[tag]==NULL) { size_t len; unsigned char *str=as_string(s,l->arg[1],&len);
                if(memchr(str,0,len)!=NULL) as_error(s,"NUL in attribute string"); s->attr_strings[tag]=(char *)str; }
        } else {
            static const unsigned supported[]={6,7,8,9,10,14,17,18,20,21,23,24,25,26,27,28,30,34,36,38,46};
            int found=0; for(size_t k=0;k<sizeof(supported)/sizeof(*supported);++k) if(tag==supported[k]) found=1;
            if(!found) as_error(s,"unsupported EABI attribute");
            s->attributes[tag]=as_bounded(s,l->arg[1],UINT32_MAX);
            if((tag==6 && s->attributes[tag]!=17) || (tag==7 && s->attributes[tag]!=77) ||
               (tag==8 && s->attributes[tag]!=0) || (tag==9 && s->attributes[tag]!=3) ||
               (tag==10 && s->attributes[tag]!=8) || (tag==18 && s->attributes[tag]!=4) ||
               (tag==26 && s->attributes[tag]!=2) || (tag==28 && s->attributes[tag]!=0))
                as_error(s,"attribute conflicts with Cortex-M33 soft-float target");
        }
        s->attr_set[tag]=1;
    } else if(d==AS_D_FILE) {
        if(n!=1) as_error(s,"numbered/debug file directives are unsupported");
        if(s->final) {
            size_t len; unsigned char *name=as_string(s,l->arg[0],&len);
            if(memchr(name,0,len)!=NULL) as_error(s,"NUL in filename");
            uint32_t id=as_new_symbol(s,(char *)name,0); s->symbols[id].out.type=4;
            s->symbols[id].out.section=BO_ABS; s->symbols[id].definition=UINT_MAX;
        }
    } else if(d==AS_D_IDENT) {
        if(n!=1) as_error(s,"invalid ident operands");
        uint32_t current=s->current; s->current=as_section(s,".comment",BO_PROGBITS,48,1);
        if(s->sections[s->current].out.size==0) as_word(s,0,1);
        if(l->binary==NULL) l->binary=as_string(s,l->arg[0],&l->binary_size);
        as_emit(s,l->binary,(uint32_t)l->binary_size); as_word(s,0,1); s->current=current;
    } else if(d==AS_D_ADDRSIG) {
        if(n!=0) as_error(s,"invalid addrsig"); s->addrsig=1;
    } else if(d==AS_D_ADDRSIG_SYM) {
        if(n!=1) as_error(s,"invalid addrsig_sym"); uint32_t id=as_symbol(s,l->arg[0]);
        s->symbols[id].referenced=1;
        if(s->final) {
            if(s->significant_count==BO_MAX_SYMBOLS) as_error(s,"address-significance count limit exceeded");
            s->significant[s->significant_count++]=id;
        }
    } else as_error(s,"unsupported directive '%s'",op);
}

static void as_uleb(assembler_t *s, uint32_t value) {
    do { unsigned byte=value&127; value>>=7; as_word(s,byte|(value?128:0),1); } while(value);
}

static void as_attributes(assembler_t *s) {
    if(!s->cpu) return;
    const unsigned tags[]={6,7,8,9,46},values[]={17,77,0,3,1};
    for(unsigned k=0;k<5;++k) if(!s->attr_set[tags[k]]) { s->attr_set[tags[k]]=1; s->attributes[tags[k]]=values[k]; }
    if(s->fpu && !s->attr_set[10]) { s->attr_set[10]=1; s->attributes[10]=8; }
    s->current=as_section(s,".ARM.attributes",BO_ARM_ATTRIBUTES,0,0);
    as_word(s,'A',1); as_word(s,0,4); as_emit(s,(const unsigned char *)"aeabi",6);
    as_word(s,1,1); as_word(s,0,4);
    if(s->attr_set[67]) { as_uleb(s,67); as_emit(s,(const unsigned char *)s->attr_strings[67],(uint32_t)strlen(s->attr_strings[67])+1); }
    as_uleb(s,5); const char *cpu=s->attr_strings[5]?s->attr_strings[5]:"cortex-m33";
    as_emit(s,(const unsigned char *)cpu,(uint32_t)strlen(cpu)+1);
    for(unsigned k=0;k<128;++k) if(s->attr_set[k] && k!=5 && k!=67) { as_uleb(s,k); as_uleb(s,s->attributes[k]); }
    as_section_t *v=&s->sections[s->current]; unsigned char *p=(unsigned char *)v->out.data;
    uint32_t n=v->out.size-1,sub=v->out.size-11;
    for(unsigned k=0;k<4;++k) { p[1+k]=(unsigned char)(n>>(8*k)); p[12+k]=(unsigned char)(sub>>(8*k)); }
}

static void as_pass(assembler_t *s) {
    for(uint32_t k=1;k<=s->section_count;++k) {
        as_section_t *section=&s->sections[k]; section->out.size=0; section->mapping=0;
        section->fallthrough=1; section->pool_first=section->pool_last=0; section->pool_generation=0;
    }
    memset(s->pools,0,sizeof(s->pools));
    s->current=1; s->pending_thumb=s->it_count=s->it_at=s->unwind_section=0;
    s->changed=0; unsigned conditional[64],depth=0; int active=1;
    for(size_t k=0;k<s->line_count;++k) {
        as_line_t *l=&s->lines[k]; s->line=l->line;
        s->record=k;
        s->listing_record=k+1;
        if(l->directive==AS_D_IF) {
            if(l->count!=1 || depth==64) as_error(s,"invalid conditional assembly");
            conditional[depth++]=(unsigned)active; if(active) active=as_absolute(s,l->arg[0])!=0; continue;
        }
        if(l->directive==AS_D_ENDIF) {
            if(l->count!=0 || depth==0) as_error(s,"unmatched endif"); active=(int)conditional[--depth]; continue;
        }
        if(depth && l->directive!=AS_D_ERROR) as_error(s,"conditional assembly is restricted to assertions");
        if(l->directive==AS_D_ERROR) {
            if(l->count!=1) as_error(s,"invalid error directive");
            if(l->binary==NULL) l->binary=as_string(s,l->arg[0],&l->binary_size);
            if(active) as_error(s,"assembly assertion failed: %s",l->binary);
            continue;
        }
        if(!active) continue;
        uint32_t before_section=s->current,before_offset=s->sections[s->current].out.size;
        if(l->directive!=AS_D_NONE) as_directive(s,l);
        else {
            if(!(s->sections[s->current].out.flags&4)) as_error(s,"instruction in non-executable section");
            if(s->sections[s->current].out.size&1) as_error(s,"unaligned Thumb instruction");
            int in_it=s->it_count!=0;
            as_mapping(s,1); unsigned width=as_instruction(s,l);
            int stops=!in_it && l->condition==14 && !(l->encoding&8) &&
                (strcmp(l->mnemonic,"b")==0 || strcmp(l->mnemonic,"bx")==0 ||
                 strcmp(l->mnemonic,"tbb")==0 || strcmp(l->mnemonic,"tbh")==0 ||
                 ((strcmp(l->mnemonic,"pop")==0 || strcmp(l->mnemonic,"ldm")==0) &&
                  (as_register_list(s,l->arg[l->count-1])&(1u<<15))));
            s->sections[s->current].fallthrough=!stops;
            if(l->width!=width) { l->width=width; s->changed=1; }
        }
        uint32_t after_offset=s->sections[s->current].out.size;
        if(l->layout_section!=before_section || l->layout_offset!=before_offset ||
           l->layout_end_section!=s->current || l->layout_end_offset!=after_offset)
            s->changed=1;
        l->layout_section=before_section; l->layout_offset=before_offset;
        l->layout_end_section=s->current; l->layout_end_offset=after_offset;
    }
    for(uint32_t k=1;k<=s->section_count;++k) if(s->sections[k].pool_first) {
        as_line_t *load=&s->lines[s->sections[k].pool_first-1]; s->line=load->line;
        as_error(s,"pending literal pool in '%s'; explicit .ltorg/.pool required",s->sections[k].out.name);
    }
    if(depth || s->it_count || s->pending_thumb || s->unwind_section) as_error(s,"unfinished conditional, IT or function annotation");
}

static void as_assemble(assembler_t *s, unsigned char *input, size_t length,
                        unsigned char **output, size_t *output_size) {
    s->sections=as_allocate(s,(BO_MAX_SECTIONS+1)*sizeof(*s->sections));
    s->symbols=as_allocate(s,(BO_MAX_SYMBOLS+1)*sizeof(*s->symbols));
    s->relocations=as_allocate(s,BO_MAX_RELOCATIONS*sizeof(*s->relocations));
    s->significant=as_allocate(s,BO_MAX_SYMBOLS*sizeof(*s->significant));
    s->current=as_section(s,".text",BO_PROGBITS,6,0); s->sections[1].out.alignment=4;
    s->cpu=1;
    as_parse(s,input,length);
    unsigned pass;
    for(pass=0;pass<AS_MAX_LAYOUT_PASSES;++pass) { as_pass(s); if(!s->changed && pass!=0) break; }
    if(pass==AS_MAX_LAYOUT_PASSES)
        as_error(s,"layout relaxation did not converge in %u passes",AS_MAX_LAYOUT_PASSES);
    s->final=1; as_pass(s);
    if(s->changed) as_error(s,"layout changed during final encoding");
    s->listing_record=0;
    as_attributes(s);
    if(s->addrsig) {
        s->current=as_section(s,".llvm_addrsig",BO_LLVM_ADDRSIG,0x80000000u,0);
        s->sections[s->current].out.link=BO_LINK_SYMTAB;
        for(size_t k=0;k<s->significant_count;++k) as_uleb(s,s->significant[k]);
    }
    for(uint32_t k=1;k<=s->symbol_count;++k) {
        as_symbol_t *v=&s->symbols[k];
        if(v->equ!=NULL) {
            as_value_t value=as_resolve(s,(as_value_t){.positive=k});
            if(value.negative || value.unknown) as_error(s,"unresolved symbol assignment");
            if(value.positive) {
                as_symbol_t *base=&s->symbols[value.positive];
                if(!base->definition) as_error(s,"alias of undefined symbol");
                v->out.section=base->out.section; value.value+=base->raw_value;
            } else v->out.section=BO_ABS;
            if(value.value<0 || (uint64_t)value.value>UINT32_MAX) as_error(s,"symbol assignment outside uint32");
            v->raw_value=(uint32_t)value.value; v->definition=UINT_MAX;
        }
        if(!v->definition) {
            if(v->out.name[0]=='.') as_error(s,"undefined local symbol '%s'",v->out.name);
            v->out.binding=v->out.binding==2?2:1;
        }
        v->out.value=v->raw_value;
        if(v->out.type==2 && v->definition && v->out.section!=BO_ABS) {
            if(!v->thumb) as_error(s,"function lacks thumb_func marking"); v->out.value|=1;
        }
    }
    bo_symbol_t *symbols=as_allocate(s,s->symbol_count*sizeof(*symbols));
    for(uint32_t k=1;k<=s->symbol_count;++k) symbols[k-1]=s->symbols[k].out;
    bo_section_t *sections=as_allocate(s,s->section_count*sizeof(*sections));
    for(uint32_t k=1;k<=s->section_count;++k) sections[k-1]=s->sections[k].out;
    bo_object_t object={sections,s->section_count,symbols,s->symbol_count,s->relocations,s->relocation_count};
    if(!badge_object_write(&object,output,output_size)) as_error(s,"object serialization failed");
}

#include "badge_thumb.h"

#ifndef BADGE_ASM_LIBRARY
static int as_listing_same_path(assembler_t *s, const char *left, const char *right) {
    char *a=as_copy(s,left),*b=as_copy(s,right);
    char *ap=strrchr(a,'/'),*bp=strrchr(b,'/');
    const char *an=ap?ap+1:a,*bn=bp?bp+1:b;
    if(strcmp(an,bn)) return 0;
    const char *ad=".",*bd=".";
    if(ap) { *ap=0; ad=ap==a?"/":a; }
    if(bp) { *bp=0; bd=bp==b?"/":b; }
    struct stat x,y;
    return !stat(ad,&x) && !stat(bd,&y) && x.st_dev==y.st_dev && x.st_ino==y.st_ino;
}
static uint32_t as_listing_u32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void as_listing_text(assembler_t *s, const char *format, ...) {
    va_list args; va_start(args,format);
    int n=vsnprintf(s->listing_bytes+s->listing_size,BO_MAX_FILE-s->listing_size,format,args);
    va_end(args);
    if(n<0 || (size_t)n>=BO_MAX_FILE-s->listing_size) as_error(s,"listing exceeds 64 MiB");
    s->listing_size+=(size_t)n;
}
static void as_listing_string(assembler_t *s, const char *text) {
    as_listing_text(s,"\"");
    for(const unsigned char *p=(const unsigned char *)text;*p;++p)
        if(*p=='"' || *p=='\\') as_listing_text(s,"\\%c",*p);
        else if(*p<32 || *p>=127) as_listing_text(s,"\\u%04x",*p);
        else as_listing_text(s,"%c",*p);
    as_listing_text(s,"\"");
}
static void as_listing_write(assembler_t *s) {
    /* Object serialization remaps addrsig symbol indices, possibly changing ULEB widths. */
    uint32_t table=as_listing_u32(s->result+32);
    for(uint32_t k=1;k<=s->section_count;++k) {
        if((uint64_t)table+(k+1)*40>s->result_size) as_error(s,"invalid serialized section table");
        const unsigned char *h=s->result+table+k*40;
        uint32_t offset=as_listing_u32(h+16),size=as_listing_u32(h+20);
        as_section_t *section=&s->sections[k];
        if(section->out.type!=BO_NOBITS) {
            if((uint64_t)offset+size>s->result_size) as_error(s,"invalid serialized section bounds");
            section->out.data=s->result+offset;
        }
        if(section->out.type==BO_LLVM_ADDRSIG) {
            for(as_listing_range_t *r=s->listing_first;r;r=r->next)
                if(r->section==k && r->record==0) r->size=size;
        } else if(section->out.size!=size) as_error(s,"serialized section layout changed");
    }
    s->listing_bytes=as_allocate(s,BO_MAX_FILE);
    as_listing_text(s,"{\"schema\":1,\"input\":"); as_listing_string(s,s->input);
    as_listing_text(s,",\"input_bytes\":%zu,\"source_encoding\":\"byte-codepoints\",\"source\":",s->original_size);
    as_listing_string(s,(const char *)s->original);
    as_listing_text(s,",\"ir_source\":");
    if(s->ir_source) as_listing_string(s,s->ir_source); else as_listing_text(s,"null");
    as_listing_text(s,",\"ranges\":[");
    int first=1;
    for(as_listing_range_t *r=s->listing_first;r;r=r->next) {
        as_line_t *l=r->record?&s->lines[r->record-1]:NULL;
        as_section_t *section=&s->sections[r->section];
        const char *kind=r->pool_padding?"alignment":l==NULL?"generated-metadata":l->directive==AS_D_NONE?"instruction":
            (l->directive==AS_D_P2ALIGN || l->directive==AS_D_BALIGN)?"alignment":
            section->out.type==BO_NOBITS?"nobits":"data";
        as_listing_text(s,"%s{\"section\":",first?"":","); first=0;
        as_listing_string(s,section->out.name);
        as_listing_text(s,",\"offset\":%u,\"size\":%u,\"line\":%u,\"kind\":",
            r->offset,r->size,l?l->line:0); as_listing_string(s,kind);
        as_listing_text(s,",\"operation\":"); as_listing_string(s,l?l->op:"generated");
        as_listing_text(s,",\"width\":%u,\"origin_line\":%u,\"hex\":",
            l && l->directive==AS_D_NONE?l->width:0,l && l->directive==AS_D_NONE?l->origin_line:0);
        if(section->out.type==BO_NOBITS) as_listing_text(s,"null");
        else {
            as_listing_text(s,"\"");
            for(uint32_t i=0;i<r->size;++i) as_listing_text(s,"%02x",section->out.data[r->offset+i]);
            as_listing_text(s,"\"");
        }
        as_listing_text(s,",\"relocations\":["); unsigned n=0;
        for(uint32_t i=0;i<s->relocation_count;++i) {
            bo_relocation_t *rel=&s->relocations[i];
            if(rel->section!=r->section || rel->offset<r->offset || rel->offset>=r->offset+r->size) continue;
            as_listing_text(s,"%s{\"offset\":%u,\"type\":%u,\"symbol\":",n++?",":"",rel->offset,rel->type);
            as_listing_string(s,s->symbols[rel->symbol].out.name); as_listing_text(s,"}");
        }
        as_listing_text(s,"],\"decision\":");
        const char *decision="exact directive bytes";
        if(l && l->directive==AS_D_NONE) {
            if(l->encoding&4) decision="explicit narrow encoding";
            else if(l->encoding&2) decision="explicit wide encoding";
            else if(l->decision) decision=l->decision;
            else decision="operand-selected encoding";
        } else if(l && (l->directive==AS_D_P2ALIGN || l->directive==AS_D_BALIGN))
            decision="exact alignment fill; executable default uses Thumb NOPs";
        else if(l && (l->directive==AS_D_LTORG || l->directive==AS_D_POOL))
            decision=r->pool_padding?"literal pool word alignment, zero fill":
                "explicit literal pool, first-use ordering and deduplication";
        as_listing_string(s,decision); as_listing_text(s,"}");
    }
    as_listing_text(s,"],\"decisions\":["); first=1;
    for(size_t k=0;k<s->line_count;++k) {
        as_line_t *l=&s->lines[k];
        if(!l->has_reach && !l->alignment) continue;
        as_listing_text(s,"%s{\"line\":%u,\"section\":",first?"":",",l->line); first=0;
        as_listing_string(s,s->sections[l->layout_section].out.name);
        as_listing_text(s,",\"offset\":%u,\"width\":%u,\"explicit_width\":%u",
            l->layout_offset,l->width,(l->encoding&4)?2:(l->encoding&2)?4:0);
        if(l->has_reach)
            as_listing_text(s,",\"pc_base\":%u,\"displacement\":%lld,\"reach_min\":%lld,\"reach_max\":%lld",
                l->pc_base,(long long)l->displacement,(long long)l->reach_min,(long long)l->reach_max);
        if(l->alignment)
            as_listing_text(s,",\"alignment\":%u,\"required_padding\":%u,\"max_skip_exceeded\":%s",
                l->alignment,l->padding,l->padding_skipped?"true":"false");
        as_listing_text(s,"}");
    }
    as_listing_text(s,"]}\n");
}
int main(int argc, char **argv) {
    const char *listing=NULL,*ir_source=NULL;
    while(argc>2 && (!strcmp(argv[1],"--listing") || !strcmp(argv[1],"--ir-source"))) {
        const char **dest=!strcmp(argv[1],"--listing")?&listing:&ir_source;
        if(*dest) { fprintf(stderr,"badge-asm: duplicate listing option\n"); return 2; }
        *dest=argv[2]; argv+=2; argc-=2;
    }
    if(ir_source && !listing) { fprintf(stderr,"badge-asm: ir-source requires listing\n"); return 2; }
    if(argc!=4 || strcmp(argv[2],"-o")!=0) {
        fprintf(stderr,"usage: badge-asm [--listing output.json] [--ir-source input.ll] input.s -o output.o\n");
        return argc==2 && strcmp(argv[1],"--help")==0?0:2;
    }
    FILE *f=fopen(argv[1],"rb");
    if(f==NULL) { fprintf(stderr,"badge-asm: cannot open input: %s\n",strerror(errno)); return 1; }
    struct stat st,dest;
    int ok=fstat(fileno(f),&st)==0 && S_ISREG(st.st_mode) && st.st_size>=0 && (uint64_t)st.st_size<=AS_MAX_SOURCE;
    if(!ok) { fclose(f); fprintf(stderr,"badge-asm: input must be a regular file of at most 16 MiB\n"); return 1; }
    if(stat(argv[3],&dest)==0 && st.st_dev==dest.st_dev && st.st_ino==dest.st_ino) {
        fclose(f); fprintf(stderr,"badge-asm: output aliases input\n"); return 1;
    }
    if(listing) {
        struct stat a,b;
        if(!strcmp(listing,argv[1]) || !strcmp(listing,argv[3]) ||
           (!stat(listing,&a) && ((a.st_dev==st.st_dev && a.st_ino==st.st_ino) ||
            (!stat(argv[3],&b) && a.st_dev==b.st_dev && a.st_ino==b.st_ino)))) {
            fclose(f); fprintf(stderr,"badge-asm: listing aliases input/output\n"); return 1;
        }
    }
    size_t length=(size_t)st.st_size; unsigned char *input=malloc(length+1);
    if(input==NULL) { fclose(f); fprintf(stderr,"badge-asm: out of memory reading input\n"); return 1; }
    ok=fread(input,1,length,f)==length && fgetc(f)==EOF && !ferror(f); if(fclose(f)!=0) ok=0;
    if(!ok) { free(input); fprintf(stderr,"badge-asm: failed to read input\n"); return 1; } input[length]=0;
    assembler_t *s=calloc(1,sizeof(*s));
    if(s==NULL) { free(input); fprintf(stderr,"badge-asm: out of memory creating assembler\n"); return 1; }
    s->input=argv[1]; s->output=argv[3];
    s->listing=listing; s->ir_source=ir_source;
    if(setjmp(s->failure)==0) {
        if(listing) {
            if(as_listing_same_path(s,listing,argv[3]) || as_listing_same_path(s,listing,argv[1]) ||
               (ir_source && as_listing_same_path(s,listing,ir_source)))
                as_error(s,"listing aliases an input or object output");
            if(memchr(input,0,length)) as_error(s,"NUL in listing input");
            s->original=as_allocate(s,length+1); memcpy(s->original,input,length); s->original_size=length;
        }
        if(ir_source) {
            FILE *ir=fopen(ir_source,"rb"); struct stat ir_stat;
            if(!ir) as_error(s,"cannot open IR source");
            if(fstat(fileno(ir),&ir_stat) || !S_ISREG(ir_stat.st_mode) ||
               ir_stat.st_size<0 || ir_stat.st_size>AS_MAX_SOURCE) {
                fclose(ir); as_error(s,"IR source exceeds input limit");
            }
            struct stat listing_stat;
            if(stat(listing,&listing_stat)==0 && ir_stat.st_dev==listing_stat.st_dev &&
               ir_stat.st_ino==listing_stat.st_ino) {
                fclose(ir); as_error(s,"listing aliases IR source");
            }
            if((stat(s->output,&listing_stat)==0 && ir_stat.st_dev==listing_stat.st_dev &&
                ir_stat.st_ino==listing_stat.st_ino) || as_listing_same_path(s,s->output,ir_source)) {
                fclose(ir); as_error(s,"object output aliases IR source");
            }
            s->ir_lines=1;
            for(off_t at=0;at<ir_stat.st_size;++at) {
                int byte=fgetc(ir);
                if(byte==EOF) { fclose(ir); as_error(s,"short IR source read"); }
                if(byte=='\n') ++s->ir_lines;
            }
            int failed=ferror(ir); if(fclose(ir)) failed=1;
            if(failed) as_error(s,"cannot read IR source");
        }
        as_assemble(s,input,length,&s->result,&s->result_size);
        if(listing) as_listing_write(s);
        ok=(!listing || host_write_file("badge-asm",listing,(unsigned char *)s->listing_bytes,s->listing_size)) &&
            host_write_file("badge-asm",argv[3],s->result,s->result_size);
    }
    else ok=0;
    if(ok) printf("%s: %zu bytes, %u sections, %u symbols, %u relocations\n",argv[3],s->result_size,s->section_count,s->symbol_count,s->relocation_count);
    free(s->result); as_destroy(s); free(input); return ok?0:1;
}
#endif
