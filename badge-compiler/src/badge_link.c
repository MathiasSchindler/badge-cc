#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "badge_object.h"
#include "host_io.h"
#include <ctype.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>

#define LK_FILES 128
#define LK_SECTIONS 16384
#define LK_SYMBOLS 262144
#define LK_GLOBALS 65536
#define LK_HASH 131071
#define LK_COMMANDS 8192
#define LK_OUTPUTS 64
#define LK_REGIONS 32
#define LK_MEMORY (192u * 1024u * 1024u)

typedef struct lk_expr lk_expr;
struct lk_expr { int op; uint64_t number; const char *name; lk_expr *left,*right; };
typedef struct { const char *name; uint32_t origin,length,permissions; uint64_t cursor; } lk_region;
typedef struct { const char *name; uint32_t address,load,size,alignment,flags,offset; int region,load_region,noload,fill,laid; } lk_output;
typedef struct { int kind,out,keep,provide; const char *name; lk_expr *expr; } lk_command;
typedef struct { const char *path; bo_object_t *object; struct lk_section *sections; uint32_t *globals,*next; } lk_file;
typedef struct lk_section { lk_file *file; uint32_t index,head,offset; int out,live,discard,placed; } lk_section;
typedef struct {
    const char *name; lk_file *file; uint32_t symbol,value,size,alignment,visibility,next;
    int binding,type,script,provide,defined,live,out,common,placed;
} lk_global;
typedef struct lk_allocation { struct lk_allocation *next; } lk_allocation;
typedef struct {
    jmp_buf failure;
    lk_allocation *allocations; size_t allocated,models;
    lk_file files[LK_FILES]; unsigned file_count,section_count,symbol_count;
    lk_region regions[LK_REGIONS]; unsigned region_count;
    lk_output outputs[LK_OUTPUTS]; unsigned output_count;
    lk_command commands[LK_COMMANDS]; unsigned command_count;
    lk_global *globals; uint32_t *hash; unsigned global_count;
    const char *script,*output,*map,*entry,*cursor,*token; unsigned line;
    int quoted,gc,active; uint64_t dot;
    FILE *reading; unsigned char *temporary;
    unsigned char *image,*map_bytes; size_t image_size,map_size;
} linker_t;

enum { E_NUMBER=1,E_NAME,E_DOT,E_ADD,E_SUB,E_EQ,E_LE,E_GE,E_LT,E_GT,E_ALIGN,E_ORIGIN,E_LENGTH,E_LOAD };
enum { C_SELECT=1,C_ASSIGN,C_DOT,C_ASSERT,C_OUTPUT };

static _Noreturn void lk_error(linker_t *l,const char *format,...) {
    fprintf(stderr,"badge-link: "); va_list ap; va_start(ap,format); vfprintf(stderr,format,ap); va_end(ap);
    if(l->cursor!=NULL) fprintf(stderr," (%s:%u)",l->script,l->line);
    fputc('\n',stderr); longjmp(l->failure,1);
}
static void *lk_allocate(linker_t *l,size_t size) {
    if(size>LK_MEMORY-l->allocated || l->models>LK_MEMORY-l->allocated-size)
        lk_error(l,"aggregate memory limit exceeded");
    lk_allocation *a=calloc(1,sizeof(*a)+size);
    if(a==NULL) lk_error(l,"out of memory");
    a->next=l->allocations; l->allocations=a; l->allocated+=size; return a+1;
}
static const char *lk_copy(linker_t *l,const char *p,size_t n) {
    if(n>4096) lk_error(l,"name/token exceeds 4096 bytes");
    char *s=lk_allocate(l,n+1); memcpy(s,p,n); return s;
}
static uint32_t lk_u32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned lk_u16(const unsigned char *p) { return (unsigned)p[0]|(unsigned)p[1]<<8; }
static void lk_put32(unsigned char *p,uint32_t v) { for(unsigned k=0;k<4;++k) p[k]=(unsigned char)(v>>(k*8)); }
static void lk_put16(unsigned char *p,uint32_t v) { p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); }
static uint64_t lk_align(linker_t *l,uint64_t n,uint64_t align) {
    if(align==0 || align>65536 || (align&(align-1))!=0) lk_error(l,"invalid alignment");
    if(n>UINT32_MAX || n+align-1>UINT32_MAX) lk_error(l,"address/alignment overflow");
    return (n+align-1)&~(align-1);
}
static uint32_t lk_bound(linker_t *l,uint64_t n) {
    if(n>UINT32_MAX) lk_error(l,"value outside uint32 range");
    return (uint32_t)n;
}
static void lk_destroy(linker_t *l) {
    if(l->reading!=NULL) fclose(l->reading);
    free(l->temporary);
    for(unsigned k=0;k<l->file_count;++k) badge_object_free(l->files[k].object);
    lk_allocation *a=l->allocations;
    while(a!=NULL) { lk_allocation *next=a->next; free(a); a=next; }
    free(l);
}
static void lk_alias(linker_t *l,const char *input,const char *output) {
    if(output==NULL) return;
    struct stat a,b;
    if(stat(input,&a)!=0) lk_error(l,"cannot inspect %s: %s",input,strerror(errno));
    if(lstat(output,&b)==0) {
        if(!S_ISREG(b.st_mode)) lk_error(l,"output must be a regular file: %s",output);
        if(a.st_dev==b.st_dev && a.st_ino==b.st_ino) lk_error(l,"input/output alias: %s",input);
    } else if(errno!=ENOENT) lk_error(l,"cannot inspect %s: %s",output,strerror(errno));
}
static const char *lk_destination(linker_t *l,const char *path) {
    size_t length=strlen(path); if(length>4096) lk_error(l,"output path exceeds limit");
    char *work=lk_allocate(l,length+1); memcpy(work,path,length+1);
    char *slash=strrchr(work,'/'); const char *base=work,*parent=".";
    if(slash!=NULL) { *slash=0; base=slash+1; parent=slash==work?"/":work; }
    if(!*base || strcmp(base,".")==0 || strcmp(base,"..")==0) lk_error(l,"invalid output filename");
    l->temporary=(unsigned char *)realpath(parent,NULL);
    if(l->temporary==NULL) lk_error(l,"cannot resolve output directory: %s",strerror(errno));
    size_t n=strlen((char *)l->temporary);
    char *result=lk_allocate(l,n+strlen(base)+2);
    memcpy(result,l->temporary,n); result[n]='/'; strcpy(result+n+1,base);
    free(l->temporary); l->temporary=NULL; return result;
}
static unsigned char *lk_read(linker_t *l,const char *path,size_t limit,size_t *size) {
    lk_alias(l,path,l->output); lk_alias(l,path,l->map);
    l->reading=fopen(path,"rb"); if(l->reading==NULL) lk_error(l,"cannot open %s: %s",path,strerror(errno));
    struct stat st;
    if(fstat(fileno(l->reading),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0 ||
       (uint64_t)st.st_size>limit) lk_error(l,"invalid/nonregular/oversized input: %s",path);
    *size=(size_t)st.st_size;
    if(*size>LK_MEMORY-l->allocated-l->models) lk_error(l,"input memory limit exceeded");
    l->temporary=malloc(*size+1); if(l->temporary==NULL) lk_error(l,"out of memory reading %s",path);
    int ok=fread(l->temporary,1,*size,l->reading)==*size;
    if(fclose(l->reading)!=0) ok=0; l->reading=NULL;
    if(!ok) lk_error(l,"cannot read/close %s",path);
    l->temporary[*size]=0; return l->temporary;
}
static uint32_t lk_global_id(linker_t *l,const char *name) {
    uint32_t hash=2166136261u;
    for(const unsigned char *p=(const unsigned char *)name;*p;++p) hash=(hash^*p)*16777619u;
    unsigned slot=hash%LK_HASH;
    for(uint32_t id=l->hash[slot];id!=0;id=l->globals[id].next)
        if(strcmp(l->globals[id].name,name)==0) return id;
    if(l->global_count==LK_GLOBALS) lk_error(l,"global symbol limit exceeded");
    uint32_t id=++l->global_count;
    l->globals[id]=(lk_global){.name=name,.next=l->hash[slot],.out=-1}; l->hash[slot]=id;
    return id;
}
static int lk_region_id(linker_t *l,const char *name) {
    for(unsigned k=0;k<l->region_count;++k) if(strcmp(l->regions[k].name,name)==0) return (int)k;
    lk_error(l,"unknown memory region '%s'",name);
}
static int lk_output_id(linker_t *l,const char *name) {
    for(unsigned k=0;k<l->output_count;++k) if(strcmp(l->outputs[k].name,name)==0) return (int)k;
    lk_error(l,"unknown output section '%s'",name);
}

static void lk_token(linker_t *l) {
    const char *p=l->cursor;
    for(;;) {
        while(isspace((unsigned char)*p)) { if(*p=='\n') ++l->line; ++p; }
        if(p[0]=='/' && p[1]=='*') {
            p+=2; while(*p && !(p[0]=='*' && p[1]=='/')) { if(*p=='\n') ++l->line; ++p; }
            if(!*p) lk_error(l,"unterminated script comment"); p+=2; continue;
        }
        break;
    }
    const char *start=p; l->quoted=0;
    if(*p=='"') {
        l->quoted=1; start=++p;
        while(*p && *p!='"') { if(*p=='\\' || *p=='\n') lk_error(l,"unsupported script string escape/newline"); ++p; }
        if(*p!='"') lk_error(l,"unterminated script string");
        l->token=lk_copy(l,start,(size_t)(p-start)); l->cursor=p+1; return;
    }
    if(strncmp(p,"/DISCARD/",9)==0) p+=9;
    else if(isalnum((unsigned char)*p) || *p=='_' || *p=='.' || *p=='$') {
        while(isalnum((unsigned char)*p) || *p=='_' || *p=='.' || *p=='$') ++p;
        if(*p=='*' && (isalpha((unsigned char)*start) || *start=='.')) ++p;
    } else if(*p) {
        ++p; if((*start=='=' || *start=='<' || *start=='>' || *start=='+') && *p=='=') ++p;
    }
    l->token=lk_copy(l,start,(size_t)(p-start)); l->cursor=p;
}
static int lk_take(linker_t *l,const char *token) {
    if(!l->quoted && strcmp(l->token,token)==0) { lk_token(l); return 1; } return 0;
}
static void lk_expect(linker_t *l,const char *token) {
    if(!lk_take(l,token)) lk_error(l,"expected '%s', found '%s'",token,l->token);
}
static const char *lk_name(linker_t *l) {
    const char *s=l->token;
    if(l->quoted || !(isalpha((unsigned char)*s) || *s=='_' || *s=='.' || *s=='$') || strchr(s,'*'))
        lk_error(l,"expected name, found '%s'",s);
    lk_token(l); return s;
}
static lk_expr *lk_node(linker_t *l,int op,lk_expr *left,lk_expr *right) {
    lk_expr *e=lk_allocate(l,sizeof(*e)); e->op=op; e->left=left; e->right=right; return e;
}
static lk_expr *lk_expression(linker_t *,unsigned,unsigned);
static lk_expr *lk_atom(linker_t *l,unsigned depth) {
    if(depth>64) lk_error(l,"script expression nesting limit exceeded");
    if(lk_take(l,"(")) { lk_expr *e=lk_expression(l,0,depth+1); lk_expect(l,")"); return e; }
    if(isdigit((unsigned char)*l->token) && !l->quoted) {
        errno=0; char *end; uint64_t n=strtoull(l->token,&end,0);
        if(*end=='k' || *end=='K') { if(n>UINT32_MAX/1024) lk_error(l,"number overflow"); n*=1024; ++end; }
        if(errno || *end || n>UINT32_MAX) lk_error(l,"invalid script integer");
        lk_token(l); lk_expr *e=lk_node(l,E_NUMBER,NULL,NULL); e->number=n; return e;
    }
    const char *name=lk_name(l);
    if(strcmp(name,".")==0) return lk_node(l,E_DOT,NULL,NULL);
    if(lk_take(l,"(")) {
        int op=strcmp(name,"ALIGN")==0?E_ALIGN:strcmp(name,"ORIGIN")==0?E_ORIGIN:
            strcmp(name,"LENGTH")==0?E_LENGTH:strcmp(name,"LOADADDR")==0?E_LOAD:0;
        if(!op) lk_error(l,"unsupported script function '%s'",name);
        lk_expr *e=lk_node(l,op,NULL,NULL);
        if(op==E_ALIGN) e->left=lk_expression(l,0,depth+1);
        else e->name=lk_name(l);
        lk_expect(l,")"); return e;
    }
    lk_expr *e=lk_node(l,E_NAME,NULL,NULL); e->name=name; return e;
}
static lk_expr *lk_expression(linker_t *l,unsigned level,unsigned depth) {
    if(level==2) return lk_atom(l,depth);
    lk_expr *left=lk_expression(l,level+1,depth);
    for(;;) {
        int op=0;
        if(level==1) { if(lk_take(l,"+")) op=E_ADD; else if(lk_take(l,"-")) op=E_SUB; }
        else if(lk_take(l,"==")) op=E_EQ; else if(lk_take(l,"<=")) op=E_LE;
        else if(lk_take(l,">=")) op=E_GE; else if(lk_take(l,"<")) op=E_LT; else if(lk_take(l,">")) op=E_GT;
        if(!op) return left;
        left=lk_node(l,op,left,lk_expression(l,level+1,depth));
    }
}
static uint32_t lk_symbol_value(linker_t *,lk_file *,uint32_t);
static uint64_t lk_eval(linker_t *l,lk_expr *e,unsigned depth) {
    if(depth>64) lk_error(l,"script expression evaluation depth exceeded");
    uint64_t a,b;
    switch(e->op) {
        case E_NUMBER:return e->number;
        case E_DOT:if(l->active<0) lk_error(l,"location counter outside output section"); return l->dot;
        case E_NAME: {
            lk_global *g=&l->globals[lk_global_id(l,e->name)];
            if(!g->defined) lk_error(l,"undefined script symbol '%s'",e->name);
            if(g->common && !g->placed) lk_error(l,"forward COMMON value in script");
            if(g->file!=NULL && !g->common && !g->script) {
                const bo_symbol_t *s=&g->file->object->symbols[g->symbol-1];
                if(s->section!=BO_ABS && !g->file->sections[s->section].placed) lk_error(l,"forward input-symbol value in script");
                return lk_symbol_value(l,g->file,g->symbol);
            }
            return g->value;
        }
        case E_ORIGIN:return l->regions[lk_region_id(l,e->name)].origin;
        case E_LENGTH:return l->regions[lk_region_id(l,e->name)].length;
        case E_LOAD: {
            lk_output *o=&l->outputs[lk_output_id(l,e->name)];
            if(!o->laid) lk_error(l,"forward LOADADDR in script"); return o->load;
        }
        case E_ALIGN:
            if(l->active<0) lk_error(l,"ALIGN requires an active output section");
            return lk_align(l,l->dot,lk_eval(l,e->left,depth+1));
        default:break;
    }
    a=lk_eval(l,e->left,depth+1); b=lk_eval(l,e->right,depth+1);
    switch(e->op) {
        case E_ADD:if(a>UINT32_MAX || b>UINT32_MAX-a) lk_error(l,"script addition overflow"); return a+b;
        case E_SUB:if(a<b) lk_error(l,"script subtraction underflow"); return a-b;
        case E_EQ:return a==b; case E_LE:return a<=b; case E_GE:return a>=b;
        case E_LT:return a<b; case E_GT:return a>b;
        default:lk_error(l,"invalid expression operator");
    }
}
static uint64_t lk_evaluate(linker_t *l,lk_expr *e) { return lk_eval(l,e,0); }
static lk_command *lk_command_add(linker_t *l,int kind,int out) {
    if(l->command_count==LK_COMMANDS) lk_error(l,"script command limit exceeded");
    lk_command *c=&l->commands[l->command_count++]; c->kind=kind; c->out=out; return c;
}
static void lk_statement(linker_t *l,int out) {
    if(lk_take(l,"ASSERT")) {
        if(out==-2) lk_error(l,"DISCARD supports input selectors only");
        lk_expect(l,"("); lk_command *c=lk_command_add(l,C_ASSERT,out); c->expr=lk_expression(l,0,0);
        lk_expect(l,","); if(!l->quoted) lk_error(l,"ASSERT requires quoted diagnostic"); c->name=l->token;
        lk_token(l); lk_expect(l,")"); lk_take(l,";"); return;
    }
    int keep=lk_take(l,"KEEP");
    if(keep) lk_expect(l,"(");
    if(lk_take(l,"*")) {
        if(out==-1) lk_error(l,"input selector outside output section"); lk_expect(l,"(");
        unsigned count=0;
        while(!lk_take(l,")")) {
            const char *p=l->token; size_t n=strlen(p);
            if(l->quoted || n==0 || !(isalpha((unsigned char)*p) || *p=='.' || strcmp(p,"*")==0) ||
               (strchr(p,'*') && strchr(p,'*')!=p+n-1)) lk_error(l,"unsupported input pattern '%s'",p);
            lk_command *c=lk_command_add(l,C_SELECT,out); c->name=p; c->keep=keep; lk_token(l); ++count;
        }
        if(count==0) lk_error(l,"empty selector");
        if(keep) lk_expect(l,")"); lk_take(l,";"); return;
    }
    if(keep) lk_error(l,"KEEP requires input selector");
    if(out==-2) lk_error(l,"DISCARD supports input selectors only");
    int provide=lk_take(l,"PROVIDE"); if(provide) lk_expect(l,"(");
    const char *name=lk_name(l); int dot=strcmp(name,".")==0;
    int increment=lk_take(l,"+="); if(!increment) lk_expect(l,"=");
    if((dot && out<0) || (increment && !dot) || (provide && dot)) lk_error(l,"unsupported location assignment");
    lk_command *c=lk_command_add(l,dot?C_DOT:C_ASSIGN,out); c->name=name; c->provide=provide;
    c->expr=lk_expression(l,0,0);
    if(increment) c->expr=lk_node(l,E_ADD,lk_node(l,E_DOT,NULL,NULL),c->expr);
    if(provide) lk_expect(l,")");
    lk_expect(l,";");
    if(!dot) {
        lk_global *g=&l->globals[lk_global_id(l,name)];
        if(g->script) lk_error(l,"duplicate script symbol '%s'",name);
        g->script=1; g->provide=provide; g->binding=1;
    }
}
static void lk_parse(linker_t *l,const char *text) {
    l->cursor=text; l->line=1; lk_token(l); lk_expect(l,"MEMORY"); lk_expect(l,"{");
    while(!lk_take(l,"}")) {
        if(l->region_count==LK_REGIONS) lk_error(l,"memory region limit exceeded");
        const char *name=lk_name(l);
        for(unsigned k=0;k<l->region_count;++k) if(strcmp(l->regions[k].name,name)==0) lk_error(l,"duplicate memory region");
        lk_expect(l,"("); const char *flags=lk_name(l); uint32_t permissions=0;
        for(const char *p=flags;*p;++p) {
            unsigned bit=*p=='r'?4:*p=='w'?2:*p=='x'?1:0;
            if(!bit || (permissions&bit)) lk_error(l,"unsupported memory permissions"); permissions|=bit;
        }
        lk_expect(l,")"); lk_expect(l,":"); lk_expect(l,"ORIGIN"); lk_expect(l,"=");
        uint32_t origin=lk_bound(l,lk_evaluate(l,lk_expression(l,0,0)));
        lk_expect(l,","); lk_expect(l,"LENGTH"); lk_expect(l,"=");
        uint32_t length=lk_bound(l,lk_evaluate(l,lk_expression(l,0,0)));
        if(!length || (uint64_t)origin+length>UINT32_MAX) lk_error(l,"invalid memory region range");
        for(unsigned k=0;k<l->region_count;++k) {
            lk_region *r=&l->regions[k];
            if(origin<(uint64_t)r->origin+r->length && r->origin<(uint64_t)origin+length) lk_error(l,"overlapping memory regions");
        }
        l->regions[l->region_count++]=(lk_region){name,origin,length,permissions,origin}; lk_take(l,";");
    }
    lk_expect(l,"ENTRY"); lk_expect(l,"("); l->entry=lk_name(l); lk_expect(l,")");
    lk_expect(l,"SECTIONS"); lk_expect(l,"{");
    while(!lk_take(l,"}")) {
        if(l->token[0]=='.' || strcmp(l->token,"/DISCARD/")==0) {
            const char *name=l->token; lk_token(l);
            int discard=strcmp(name,"/DISCARD/")==0,noload=0,out=-2;
            if(lk_take(l,"(")) { lk_expect(l,"NOLOAD"); lk_expect(l,")"); noload=1; }
            lk_expect(l,":");
            if(!discard) {
                if(l->output_count==LK_OUTPUTS) lk_error(l,"output section limit exceeded");
                for(unsigned k=0;k<l->output_count;++k) if(strcmp(l->outputs[k].name,name)==0) lk_error(l,"duplicate output section");
                out=(int)l->output_count++; l->outputs[out]=(lk_output){.name=name,.noload=noload,.alignment=1,.fill=-1};
                lk_command_add(l,C_OUTPUT,out);
            }
            lk_expect(l,"{"); while(!lk_take(l,"}")) lk_statement(l,out);
            if(!discard) {
                lk_expect(l,">"); l->outputs[out].region=lk_region_id(l,lk_name(l));
                l->outputs[out].load_region=l->outputs[out].region;
                if(lk_take(l,"AT")) { lk_expect(l,">"); l->outputs[out].load_region=lk_region_id(l,lk_name(l)); }
                if(lk_take(l,"=")) {
                    uint64_t fill=lk_evaluate(l,lk_expression(l,0,0));
                    if(fill>255) lk_error(l,"only one-byte section fill supported"); l->outputs[out].fill=(int)fill;
                }
            }
            lk_take(l,";");
        } else lk_statement(l,-1);
    }
    if(*l->token) lk_error(l,"unsupported trailing script input '%s'",l->token);
    if(!l->output_count) lk_error(l,"script has no output sections"); l->cursor=NULL;
}

static unsigned lk_uleb(linker_t *l,const unsigned char *p,size_t end,size_t *at) {
    uint32_t v=0;
    for(unsigned k=0;k<5;++k) {
        if(*at==end) lk_error(l,"truncated Arm attributes");
        unsigned b=p[(*at)++]; if(k==4 && (b&0xf0)) lk_error(l,"overflow in Arm attributes");
        v|=(b&127)<<(k*7); if(!(b&128)) return v;
    }
    lk_error(l,"invalid Arm attribute ULEB128");
}
static void lk_attributes(linker_t *l,const bo_section_t *s) {
    const unsigned char *p=s->data; size_t end=s->size,at=1;
    if(!end || p[0]!='A') lk_error(l,"unsupported Arm attribute format");
    while(at<end) {
        if(end-at<10) lk_error(l,"truncated Arm attribute vendor");
        size_t stop=at+lk_u32(p+at); at+=4;
        if(stop>end || stop<at+6 || memcmp(p+at,"aeabi\0",6)!=0) lk_error(l,"unsupported Arm attribute vendor");
        at+=6;
        while(at<stop) {
            size_t start=at; unsigned tag=lk_uleb(l,p,stop,&at);
            if(tag!=1 || stop-at<4) lk_error(l,"only file-level Arm attributes supported");
            uint32_t size=lk_u32(p+at); at+=4; size_t finish=start+size;
            if(finish>stop || finish<at) lk_error(l,"invalid Arm attribute record length");
            while(at<finish) {
                tag=lk_uleb(l,p,finish,&at);
                unsigned value=0;
                if(tag!=4 && tag!=5 && !(tag>=32 && (tag&1))) value=lk_uleb(l,p,finish,&at);
                if(tag==4 || tag==5 || tag==32 || (tag>=32 && (tag&1))) {
                    const unsigned char *zero=memchr(p+at,0,finish-at);
                    if(zero==NULL) lk_error(l,"unterminated Arm attribute string"); at=(size_t)(zero-p)+1;
                }
                if((tag==6 && value!=17) || (tag==7 && value!=77) || (tag==8 && value!=0) ||
                   (tag==9 && value!=3) || (tag==18 && value!=4) || (tag==26 && value!=2) ||
                   (tag==28 && value!=0)) lk_error(l,"incompatible Cortex-M33/soft-float ABI attribute %u",tag);
            }
        }
    }
}
static int lk_relocation_order(const void *a,const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return (x>y)-(x<y);
}
static void lk_load(linker_t *l,const char *path) {
    if(l->file_count==LK_FILES) lk_error(l,"object count limit exceeded");
    size_t size; unsigned char *input=lk_read(l,path,BO_MAX_FILE,&size);
    lk_file *f=&l->files[l->file_count++]; f->path=path;
    f->object=badge_object_read(input,size); free(l->temporary); l->temporary=NULL;
    if(f->object==NULL) lk_error(l,"cannot decode object %s",path);
    const bo_object_t *o=f->object;
    if(o->section_count>LK_SECTIONS-l->section_count || o->symbol_count>LK_SYMBOLS-l->symbol_count)
        lk_error(l,"aggregate section/symbol limit exceeded");
    l->section_count+=(unsigned)o->section_count; l->symbol_count+=(unsigned)o->symbol_count;
    size_t model=(o->section_count+1)*sizeof(bo_section_t)+(o->symbol_count+1)*sizeof(bo_symbol_t)+
        (o->relocation_count+1)*sizeof(bo_relocation_t);
    for(size_t k=0;k<o->section_count;++k) {
        const bo_section_t *s=&o->sections[k]; model+=strlen(s->name)+1;
        if(s->type!=BO_NOBITS) model+=s->size;
        if(s->type==BO_ARM_ATTRIBUTES) lk_attributes(l,s);
        if(!(s->flags&2) && s->type!=BO_ARM_ATTRIBUTES && s->type!=BO_LLVM_ADDRSIG &&
           strcmp(s->name,".comment")!=0 && !(strcmp(s->name,".note.GNU-stack")==0 && !s->size && !s->flags))
            lk_error(l,"unsupported nonallocated input section '%s'",s->name);
    }
    for(size_t k=0;k<o->symbol_count;++k) model+=strlen(o->symbols[k].name)+1;
    if(model>LK_MEMORY-l->models || l->allocated>LK_MEMORY-l->models-model) lk_error(l,"object model memory limit exceeded");
    l->models+=model;
    f->sections=lk_allocate(l,(o->section_count+1)*sizeof(*f->sections));
    f->globals=lk_allocate(l,(o->symbol_count+1)*sizeof(*f->globals));
    f->next=lk_allocate(l,(o->relocation_count+1)*sizeof(*f->next));
    uint64_t *places=lk_allocate(l,(o->relocation_count+1)*sizeof(*places));
    for(size_t k=0;k<o->relocation_count;++k)
        places[k]=((uint64_t)o->relocations[k].section<<32)|o->relocations[k].offset;
    qsort(places,o->relocation_count,sizeof(*places),lk_relocation_order);
    for(size_t k=1;k<o->relocation_count;++k)
        if((places[k]>>32)==(places[k-1]>>32) && places[k]-places[k-1]<4) lk_error(l,"overlapping relocation places");
    for(uint32_t k=1;k<=o->section_count;++k) f->sections[k]=(lk_section){.file=f,.index=k,.head=UINT32_MAX,.out=-1};
    for(uint32_t k=0;k<o->relocation_count;++k) {
        lk_section *s=&f->sections[o->relocations[k].section]; f->next[k]=s->head; s->head=k;
    }
    for(uint32_t k=1;k<=o->symbol_count;++k) {
        const bo_symbol_t *s=&o->symbols[k-1];
        if(!s->binding) { if(s->section==BO_COMMON) lk_error(l,"local COMMON is unsupported"); continue; }
        if(!*s->name) lk_error(l,"global symbol requires a name");
        uint32_t id=lk_global_id(l,s->name); f->globals[k]=id; lk_global *g=&l->globals[id];
        if(s->visibility==1 || s->visibility==2) g->visibility=2;
        else if(!g->visibility) g->visibility=s->visibility;
        if(!s->section) {
            if(!g->defined && (s->binding==1 || !g->binding)) g->binding=(int)s->binding;
            continue;
        }
        if(g->script && !g->provide) lk_error(l,"object/script symbol collision '%s'",s->name);
        int common=s->section==BO_COMMON;
        if(g->defined && g->common && common) {
            if(g->binding==1 && s->binding==2) continue;
            if(g->binding==2 && s->binding==1) {
                g->size=s->size; g->alignment=s->value; g->binding=1; g->file=f; g->symbol=k; continue;
            }
            if(s->size>g->size) g->size=s->size;
            if(s->value>g->alignment) g->alignment=s->value;
            if(s->binding==1) g->binding=1;
            continue;
        }
        if(g->defined && g->binding==1 && s->binding==1 && !common && !g->common)
            lk_error(l,"duplicate strong definition '%s' in %s and %s",s->name,g->file->path,path);
        int replace=!g->defined || (s->binding==1 && g->binding==2) ||
            (s->binding==1 && g->common && !common);
        if(!replace) continue;
        if(g->provide) g->script=0;
        g->file=f; g->symbol=k; g->defined=1; g->binding=(int)s->binding; g->type=(int)s->type;
        g->common=common; g->size=s->size; g->alignment=common?s->value:1;
        g->value=s->value;
    }
}
static int lk_matches(const char *pattern,const char *name) {
    size_t n=strlen(pattern);
    return n && pattern[n-1]=='*' ? strncmp(pattern,name,n-1)==0 : strcmp(pattern,name)==0;
}
static void lk_select(linker_t *l) {
    for(unsigned c=0;c<l->command_count;++c) {
        lk_command *command=&l->commands[c]; if(command->kind!=C_SELECT) continue;
        if(strcmp(command->name,"COMMON")==0) {
            if(command->keep && command->out>=0)
                for(unsigned k=1;k<=l->global_count;++k) if(l->globals[k].common) l->globals[k].live=1;
            continue;
        }
        for(unsigned k=0;k<l->file_count;++k) {
            lk_file *f=&l->files[k];
            for(uint32_t j=1;j<=f->object->section_count;++j) {
                lk_section *s=&f->sections[j]; const bo_section_t *input=&f->object->sections[j-1];
                if(s->out!=-1 || s->discard || !(input->flags&2) || !lk_matches(command->name,input->name)) continue;
                s->out=command->out; s->discard=command->out==-2; if(command->keep && !s->discard) s->live=1;
            }
        }
    }
}
static lk_section *lk_symbol_section(linker_t *l,lk_file *f,uint32_t id, int mark) {
    const bo_symbol_t *s=&f->object->symbols[id-1];
    if(s->binding) {
        lk_global *g=&l->globals[f->globals[id]];
        if(mark) g->live=1;
        if(g->script && (!g->defined || !g->provide)) return NULL;
        if(!g->defined) {
            if(g->binding==2) return NULL;
            lk_error(l,"undefined symbol '%s' referenced by %s",s->name,f->path);
        }
        if(g->common) return NULL;
        f=g->file; s=&f->object->symbols[g->symbol-1];
    }
    if(s->section==BO_ABS) return NULL;
    if(!s->section || s->section>f->object->section_count) lk_error(l,"undefined/local invalid symbol '%s'",s->name);
    return &f->sections[s->section];
}
static void lk_gc(linker_t *l) {
    lk_global *entry=&l->globals[lk_global_id(l,l->entry)];
    if(!entry->defined || entry->script || entry->common) lk_error(l,"missing executable entry '%s'",l->entry);
    entry->live=1; lk_section *root=lk_symbol_section(l,entry->file,entry->symbol,1);
    if(root==NULL || !(root->file->object->sections[root->index-1].flags&4)) lk_error(l,"entry is not executable");
    const bo_section_t *code=&root->file->object->sections[root->index-1];
    const bo_symbol_t *start=&entry->file->object->symbols[entry->symbol-1];
    if(code->type!=BO_PROGBITS || (start->value&~1u)>=code->size ||
       (root->out>=0 && l->outputs[root->out].noload)) lk_error(l,"entry requires file-backed executable code");
    root->live=1;
    lk_section **queue=lk_allocate(l,(l->section_count+1)*sizeof(*queue)); unsigned head=0,tail=0;
    for(unsigned k=0;k<l->file_count;++k) {
        lk_file *f=&l->files[k];
        for(uint32_t j=1;j<=f->object->section_count;++j) {
            lk_section *s=&f->sections[j];
            if(!l->gc && (f->object->sections[j-1].flags&2) && !s->discard) s->live=1;
            if(s->live) queue[tail++]=s;
        }
    }
    if(!l->gc) for(unsigned k=1;k<=l->global_count;++k) if(l->globals[k].common) l->globals[k].live=1;
    while(head<tail) {
        lk_section *s=queue[head++]; lk_file *f=s->file;
        if(s->discard || s->out<0) lk_error(l,"live discarded/orphan section '%s' in %s",f->object->sections[s->index-1].name,f->path);
        for(uint32_t at=s->head;at!=UINT32_MAX;at=f->next[at]) {
            lk_section *target=lk_symbol_section(l,f,f->object->relocations[at].symbol,1);
            if(target!=NULL && !target->live) { target->live=1; queue[tail++]=target; }
        }
    }
}
static void lk_close_output(linker_t *l) {
    if(l->active<0) return;
    lk_output *o=&l->outputs[l->active]; o->size=lk_bound(l,l->dot-o->address);
    lk_region *r=&l->regions[o->region];
    if(l->dot>(uint64_t)r->origin+r->length) lk_error(l,"output section %s exceeds region %s",o->name,r->name);
    r->cursor=l->dot;
    lk_region *load=&l->regions[o->load_region];
    if(!o->noload && o->load_region!=o->region) {
        uint64_t end=(uint64_t)o->load+o->size;
        if(end>(uint64_t)load->origin+load->length) lk_error(l,"load image exceeds region %s",load->name);
        load->cursor=end;
    }
    l->active=-1;
}
static void lk_place(linker_t *l,lk_section *s) {
    lk_output *o=&l->outputs[l->active]; const bo_section_t *input=&s->file->object->sections[s->index-1];
    if(input->type!=BO_PROGBITS && input->type!=BO_NOBITS) lk_error(l,"unsupported allocated section type in %s",input->name);
    if(input->type==BO_NOBITS && !o->noload) lk_error(l,"NOBITS input requires NOLOAD output");
    l->dot=lk_align(l,l->dot,input->alignment); s->offset=lk_bound(l,l->dot-o->address);
    l->dot+=input->size; lk_bound(l,l->dot);
}
static void lk_layout(linker_t *l) {
    for(unsigned k=0;k<l->file_count;++k) {
        lk_file *f=&l->files[k];
        for(uint32_t j=1;j<=f->object->section_count;++j) {
            lk_section *s=&f->sections[j]; if(!s->live) continue;
            lk_output *o=&l->outputs[s->out]; const bo_section_t *input=&f->object->sections[j-1];
            if(input->alignment>o->alignment) o->alignment=input->alignment;
            o->flags|=input->flags&5;
        }
    }
    for(unsigned k=1;k<=l->global_count;++k) {
        lk_global *g=&l->globals[k]; if(!g->common || !g->live) continue;
        int selected=-1;
        for(unsigned c=0;c<l->command_count;++c) if(l->commands[c].kind==C_SELECT &&
            strcmp(l->commands[c].name,"COMMON")==0) { selected=l->commands[c].out; break; }
        if(selected<0) lk_error(l,"COMMON symbol '%s' has no output section",g->name);
        g->out=selected; lk_output *o=&l->outputs[selected];
        if(!o->noload) lk_error(l,"COMMON requires NOLOAD output");
        if(g->alignment>o->alignment) o->alignment=g->alignment;
    }
    for(unsigned c=0;c<l->command_count;++c) {
        lk_command *command=&l->commands[c];
        if(command->kind==C_OUTPUT) {
            lk_close_output(l); l->active=command->out; lk_output *o=&l->outputs[l->active];
            lk_region *r=&l->regions[o->region]; l->dot=lk_align(l,r->cursor,o->alignment);
            o->address=lk_bound(l,l->dot);
            o->laid=1;
            o->load=lk_bound(l,o->load_region==o->region?l->dot:lk_align(l,l->regions[o->load_region].cursor,o->alignment));
            o->flags|=2|(o->noload?1:0); continue;
        }
        if(command->out==-2) continue;
        if(command->out==-1) lk_close_output(l);
        if(command->kind==C_SELECT) {
            if(strcmp(command->name,"COMMON")==0) {
                for(unsigned k=1;k<=l->global_count;++k) {
                    lk_global *g=&l->globals[k]; if(!g->common || !g->live || g->out!=l->active || g->placed) continue;
                    l->dot=lk_align(l,l->dot,g->alignment); g->value=lk_bound(l,l->dot); l->dot+=g->size;
                    g->placed=1;
                }
            } else {
                for(unsigned k=0;k<l->file_count;++k) {
                    lk_file *f=&l->files[k];
                    for(uint32_t j=1;j<=f->object->section_count;++j) {
                        lk_section *s=&f->sections[j];
                        if(s->live && s->out==l->active && lk_matches(command->name,f->object->sections[j-1].name)) {
                            if(s->placed) continue;
                            lk_place(l,s);
                            s->placed=1;
                        }
                    }
                }
            }
        } else if(command->kind==C_DOT) {
            uint64_t value=lk_evaluate(l,command->expr);
            if(value<l->dot) lk_error(l,"location counter cannot move backward"); l->dot=value;
        } else if(command->kind==C_ASSIGN) {
            lk_global *g=&l->globals[lk_global_id(l,command->name)];
            if(command->provide && (g->defined || !g->live)) continue;
            g->value=lk_bound(l,lk_evaluate(l,command->expr)); g->defined=1; g->out=l->active;
        } else if(command->kind==C_ASSERT && !lk_evaluate(l,command->expr)) lk_error(l,"script ASSERT: %s",command->name);
    }
    lk_close_output(l);
}

static uint32_t lk_symbol_value(linker_t *l,lk_file *f,uint32_t id) {
    const bo_symbol_t *s=&f->object->symbols[id-1];
    if(s->binding) {
        lk_global *g=&l->globals[f->globals[id]];
        if(g->script || g->common) { if(!g->defined) lk_error(l,"unresolved script symbol '%s'",g->name); return g->value; }
        if(!g->defined) { if(g->binding==2) return 0; lk_error(l,"undefined symbol '%s'",s->name); }
        f=g->file; s=&f->object->symbols[g->symbol-1];
    }
    if(s->section==BO_ABS) return s->value;
    lk_section *section=&f->sections[s->section];
    if(!section->live || section->out<0) lk_error(l,"reference to discarded symbol '%s'",s->name);
    return lk_bound(l,(uint64_t)l->outputs[section->out].address+section->offset+s->value);
}
static int64_t lk_sign(uint32_t value,unsigned bits) {
    return (value&(UINT32_C(1)<<(bits-1)))?(int64_t)value-(INT64_C(1)<<bits):(int64_t)value;
}
static void lk_mov_fixup(linker_t *l,unsigned char *p,uint32_t type,uint32_t value) {
    unsigned h=lk_u16(p),low=lk_u16(p+2);
    if((h&0xfbf0)!=(type==47?0xf240:0xf2c0) || (low&0x8000) ||
       ((low>>8)&15)==13 || ((low>>8)&15)==15)
        lk_error(l,"invalid Thumb MOVW/MOVT relocation opcode");
    unsigned imm=((h&15)<<12)|((h>>10&1)<<11)|((low>>12&7)<<8)|(low&255);
    /* Both REL fields contain the same signed addend, not its high/low half. */
    uint32_t full=value+(uint32_t)lk_sign(imm,16);
    imm=type==47?(full&65535):(full>>16);
    lk_put16(p,(h&0xfbf0)|((imm>>11&1)<<10)|(imm>>12));
    lk_put16(p+2,(low&0x0f00)|((imm>>8&7)<<12)|(imm&255));
}
static void lk_fixup(linker_t *l,unsigned char *p,uint32_t type,uint32_t value,uint32_t place,int weak) {
    if(type==2) { lk_put32(p,value+lk_u32(p)); return; }
    if(weak && (type==3 || type==42)) value=place+(type==42?4u:0u);
    if(type==3) { lk_put32(p,value+lk_u32(p)-place); return; }
    if(type==42) {
        int64_t x=(int64_t)value+lk_sign(lk_u32(p)&UINT32_C(0x7fffffff),31)-place;
        if(x<-(INT64_C(1)<<30) || x>=(INT64_C(1)<<30)) lk_error(l,"PREL31 relocation out of range");
        lk_put32(p,(lk_u32(p)&UINT32_C(0x80000000))|((uint32_t)x&UINT32_C(0x7fffffff))); return;
    }
    if(type==10 || type==30) {
        unsigned h=lk_u16(p),low=lk_u16(p+2),sign=(h>>10)&1;
        if((h&0xf800)!=0xf000 || (type==10?(low&0xd000)!=0xd000:(low&0xd000)!=0x9000))
            lk_error(l,"invalid Thumb branch relocation opcode");
        unsigned i1=(~((low>>13)^sign))&1,i2=(~((low>>11)^sign))&1;
        int64_t add=lk_sign((sign<<24)|(i1<<23)|(i2<<22)|((h&1023)<<12)|((low&2047)<<1),25);
        int64_t x=(int64_t)(value&~1u)+add-place;
        if(weak) x=add+4;
        if((x&1) || x<-(INT64_C(1)<<24) || x>=(INT64_C(1)<<24)) lk_error(l,"Thumb branch relocation out of range/alignment (no veneers)");
        uint32_t bits=(uint32_t)x; sign=(bits>>24)&1;
        unsigned j1=(~((bits>>23)^sign))&1,j2=(~((bits>>22)^sign))&1;
        lk_put16(p,0xf000|(sign<<10)|((bits>>12)&1023));
        lk_put16(p+2,(low&0xd000)|(j1<<13)|(j2<<11)|((bits>>1)&2047)); return;
    }
    lk_error(l,"unsupported relocation %u",type);
}
static void lk_relocate(linker_t *l) {
    for(unsigned k=0;k<l->file_count;++k) {
        lk_file *f=&l->files[k]; const bo_object_t *o=f->object;
        for(size_t j=0;j<o->relocation_count;++j) {
            const bo_relocation_t *r=&o->relocations[j]; lk_section *s=&f->sections[r->section];
            if(!s->live) continue;
            lk_output *out=&l->outputs[s->out];
            if(out->noload) lk_error(l,"relocation in NOLOAD section");
            uint32_t value=lk_symbol_value(l,f,r->symbol),place=out->address+s->offset+r->offset;
            const bo_symbol_t *symbol=&o->symbols[r->symbol-1];
            int weak=symbol->binding!=0 && l->globals[f->globals[r->symbol]].binding==2 && !l->globals[f->globals[r->symbol]].defined;
            if((r->type==10 || r->type==30) && !weak) {
                lk_section *target=lk_symbol_section(l,f,r->symbol,0);
                if(target==NULL || !(target->file->object->sections[target->index-1].flags&4) ||
                   target->file->object->sections[target->index-1].type!=BO_PROGBITS || l->outputs[target->out].noload)
                    lk_error(l,"branch target '%s' is not Thumb code",symbol->name);
            }
            unsigned char *p=l->image+out->offset+s->offset+r->offset;
            if(r->type==47 || r->type==48)
                lk_mov_fixup(l,p,r->type,value);
            else lk_fixup(l,p,r->type,value,place,weak);
        }
    }
}

static void lk_emit(linker_t *l) {
    size_t max_symbols=(size_t)l->symbol_count+l->global_count+1;
    bo_symbol_t *symbols=lk_allocate(l,max_symbols*sizeof(*symbols)); size_t ns=0,locals=1,strings=1;
    for(unsigned k=0;k<l->file_count;++k) {
        lk_file *f=&l->files[k];
        for(uint32_t j=1;j<=f->object->symbol_count;++j) {
            const bo_symbol_t *s=&f->object->symbols[j-1]; if(s->binding || !s->section) continue;
            bo_symbol_t v=*s;
            if(s->section!=BO_ABS) {
                lk_section *section=&f->sections[s->section]; if(!section->live) continue;
                v.section=(uint32_t)section->out+1; v.value=lk_symbol_value(l,f,j);
            }
            symbols[ns++]=v; ++locals; strings+=strlen(v.name)+1;
        }
    }
    for(unsigned pass=0;pass<2;++pass) for(unsigned k=1;k<=l->global_count;++k) {
        lk_global *g=&l->globals[k]; if(!g->defined) continue;
        if((g->visibility==2)!=(pass==0)) continue;
        bo_symbol_t v={.name=g->name,.binding=(uint32_t)g->binding,.type=(uint32_t)g->type,.visibility=g->visibility,.size=g->size};
        if(g->script || g->common) { if(g->provide && !g->live) continue; v.section=g->out<0?BO_ABS:(uint32_t)g->out+1; v.value=g->value; }
        else {
            const bo_symbol_t *s=&g->file->object->symbols[g->symbol-1];
            if(s->section!=BO_ABS && !g->file->sections[s->section].live) continue;
            v.section=s->section==BO_ABS?BO_ABS:(uint32_t)g->file->sections[s->section].out+1;
            v.value=lk_symbol_value(l,g->file,g->symbol);
        }
        if(pass==0) { v.binding=0; ++locals; }
        symbols[ns++]=v; strings+=strlen(v.name)+1;
    }
    const bo_section_t *attributes=NULL;
    for(unsigned k=0;k<l->file_count && attributes==NULL;++k)
        for(size_t j=0;j<l->files[k].object->section_count;++j)
            if(l->files[k].object->sections[j].type==BO_ARM_ATTRIBUTES) { attributes=&l->files[k].object->sections[j]; break; }
    unsigned attr=attributes?l->output_count+1:0;
    unsigned symtab=l->output_count+(attributes?2:1),strtab=symtab+1,names=strtab+1,count=names+1;
    const char **section_names=lk_allocate(l,count*sizeof(*section_names));
    uint32_t *name_offsets=lk_allocate(l,count*sizeof(*name_offsets)); size_t shnames=1;
    for(unsigned k=1;k<=l->output_count;++k) section_names[k]=l->outputs[k-1].name;
    if(attr) section_names[attr]=".ARM.attributes";
    section_names[symtab]=".symtab"; section_names[strtab]=".strtab"; section_names[names]=".shstrtab";
    for(unsigned k=1;k<count;++k) { name_offsets[k]=lk_bound(l,shnames); shnames+=strlen(section_names[k])+1; }
    uint64_t end=52+l->output_count*32;
    for(unsigned k=0;k<l->output_count;++k) {
        lk_output *o=&l->outputs[k]; end=lk_align(l,end,o->alignment); o->offset=lk_bound(l,end);
        if(!o->noload) end+=o->size;
    }
    uint32_t attr_offset=lk_bound(l,end); if(attributes) end+=attributes->size;
    end=lk_align(l,end,4); uint32_t sym_offset=lk_bound(l,end); end+=(ns+1)*16;
    uint32_t str_offset=lk_bound(l,end); end+=strings;
    uint32_t names_offset=lk_bound(l,end); end+=shnames;
    end=lk_align(l,end,4); uint32_t shoff=lk_bound(l,end); end+=count*40;
    if(end>BO_MAX_FILE) lk_error(l,"executable exceeds 64 MiB");
    l->image=lk_allocate(l,(size_t)end); l->image_size=(size_t)end;
    unsigned char *elf=l->image; memcpy(elf,"\177ELF\1\1\1",7);
    lk_put16(elf+16,2); lk_put16(elf+18,40); lk_put32(elf+20,1);
    lk_global *entry=&l->globals[lk_global_id(l,l->entry)];
    uint32_t address=lk_symbol_value(l,entry->file,entry->symbol);
    if(!(address&1)) lk_error(l,"entry must have Thumb function marking");
    lk_put32(elf+24,address); lk_put32(elf+28,52); lk_put32(elf+32,shoff);
    lk_put32(elf+36,UINT32_C(0x05000200)); lk_put16(elf+40,52); lk_put16(elf+42,32);
    lk_put16(elf+44,l->output_count); lk_put16(elf+46,40); lk_put16(elf+48,count); lk_put16(elf+50,names);
    for(unsigned k=0;k<l->output_count;++k) {
        lk_output *o=&l->outputs[k]; unsigned char *h=elf+shoff+(k+1)*40,*ph=elf+52+k*32;
        lk_put32(h+4,o->noload?8:1); lk_put32(h+8,o->flags); lk_put32(h+12,o->address);
        lk_put32(h+16,o->offset); lk_put32(h+20,o->size); lk_put32(h+32,o->alignment);
        lk_put32(ph,1); lk_put32(ph+4,o->offset); lk_put32(ph+8,o->address); lk_put32(ph+12,o->load);
        lk_put32(ph+16,o->noload?0:o->size); lk_put32(ph+20,o->size);
        lk_put32(ph+24,4|((o->flags&1)?2:0)|((o->flags&4)?1:0)); lk_put32(ph+28,1);
        if(!o->noload) memset(elf+o->offset,o->fill>=0?o->fill:((o->flags&4)?0xd4:0),o->size);
    }
    for(unsigned k=0;k<l->file_count;++k) {
        lk_file *f=&l->files[k];
        for(uint32_t j=1;j<=f->object->section_count;++j) {
            lk_section *s=&f->sections[j]; if(!s->live) continue; lk_output *out=&l->outputs[s->out];
            const bo_section_t *input=&f->object->sections[j-1];
            if(!out->noload && input->size) memcpy(elf+out->offset+s->offset,input->data,input->size);
        }
    }
    if(attributes) {
        unsigned char *h=elf+shoff+attr*40; lk_put32(h+4,BO_ARM_ATTRIBUTES);
        lk_put32(h+16,attr_offset); lk_put32(h+20,attributes->size); lk_put32(h+32,1);
        memcpy(elf+attr_offset,attributes->data,attributes->size);
    }
    unsigned indices[]={symtab,strtab,names}; uint32_t offsets[]={sym_offset,str_offset,names_offset};
    uint32_t sizes[]={lk_bound(l,(ns+1)*16),lk_bound(l,strings),lk_bound(l,shnames)};
    for(unsigned k=0;k<3;++k) {
        unsigned char *h=elf+shoff+indices[k]*40;
        lk_put32(h+4,k?3:2); lk_put32(h+16,offsets[k]); lk_put32(h+20,sizes[k]); lk_put32(h+32,k?1:4);
    }
    unsigned char *symh=elf+shoff+symtab*40; lk_put32(symh+24,strtab); lk_put32(symh+28,(uint32_t)locals); lk_put32(symh+36,16);
    size_t at=1;
    for(size_t k=0;k<ns;++k) {
        bo_symbol_t *s=&symbols[k]; unsigned char *p=elf+sym_offset+(k+1)*16;
        lk_put32(p,lk_bound(l,at)); lk_put32(p+4,s->value); lk_put32(p+8,s->size);
        p[12]=(unsigned char)((s->binding<<4)|s->type); p[13]=(unsigned char)s->visibility; lk_put16(p+14,s->section);
        size_t n=strlen(s->name)+1; memcpy(elf+str_offset+at,s->name,n); at+=n;
    }
    for(unsigned k=1;k<count;++k) {
        lk_put32(elf+shoff+k*40,name_offsets[k]); memcpy(elf+names_offset+name_offsets[k],section_names[k],strlen(section_names[k])+1);
    }
    lk_relocate(l);
    if(l->map==NULL) return;
    size_t capacity=1024;
    for(unsigned k=0;k<l->output_count;++k) capacity+=strlen(l->outputs[k].name)+128;
    for(unsigned k=0;k<l->file_count;++k) for(uint32_t j=1;j<=l->files[k].object->section_count;++j)
        if(l->files[k].sections[j].live) capacity+=strlen(l->files[k].path)+strlen(l->files[k].object->sections[j-1].name)+128;
    for(size_t k=locals-1;k<ns;++k) capacity+=strlen(symbols[k].name)+32;
    if(capacity>BO_MAX_FILE) lk_error(l,"map exceeds output limit");
    l->map_bytes=lk_allocate(l,capacity); char *text=(char *)l->map_bytes; size_t used=0;
    int n=snprintf(text,capacity,"     VMA      LMA     Size Align Output / Input / Symbol\n"); if(n<0) lk_error(l,"cannot format map"); used=(size_t)n;
    for(unsigned k=0;k<l->output_count;++k) {
        lk_output *o=&l->outputs[k];
        n=snprintf(text+used,capacity-used,"%08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %5" PRIu32 " %s\n",o->address,o->load,o->size,o->alignment,o->name);
        if(n<0 || (size_t)n>=capacity-used) lk_error(l,"map exceeds bound"); used+=(size_t)n;
        for(unsigned f=0;f<l->file_count;++f) for(uint32_t j=1;j<=l->files[f].object->section_count;++j) {
            lk_section *s=&l->files[f].sections[j]; if(!s->live || s->out!=(int)k) continue;
            const bo_section_t *input=&s->file->object->sections[j-1];
            n=snprintf(text+used,capacity-used,"%08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %5" PRIu32 "   %s:(%s)\n",o->address+s->offset,o->load+s->offset,input->size,input->alignment,s->file->path,input->name);
            if(n<0 || (size_t)n>=capacity-used) lk_error(l,"map exceeds bound"); used+=(size_t)n;
        }
    }
    for(size_t k=locals-1;k<ns;++k) {
        n=snprintf(text+used,capacity-used,"%08" PRIx32 " %s\n",symbols[k].value,symbols[k].name);
        if(n<0 || (size_t)n>=capacity-used) lk_error(l,"map exceeds bound"); used+=(size_t)n;
    }
    l->map_size=used;
}

int main(int argc,char **argv) {
    linker_t *l=calloc(1,sizeof(*l)); if(l==NULL) { fprintf(stderr,"badge-link: out of memory\n"); return 1; }
    l->active=-1; l->gc=1;
    if(setjmp(l->failure)) { lk_destroy(l); return 1; }
    const char *inputs[LK_FILES]; unsigned count=0;
    for(int k=1;k<argc;++k) {
        const char *p=argv[k];
        if((strcmp(p,"-o")==0 || strcmp(p,"-T")==0 || strcmp(p,"-Map")==0) && k+1<argc) {
            const char **dest=strcmp(p,"-o")==0?&l->output:strcmp(p,"-T")==0?&l->script:&l->map;
            if(*dest) lk_error(l,"duplicate option '%s'",p); *dest=argv[++k];
        } else if(strcmp(p,"--gc-sections")==0) l->gc=1;
        else if(strcmp(p,"--no-gc-sections")==0) l->gc=0;
        else if(strcmp(p,"--help")==0 && argc==2) {
            printf("usage: badge-link -T script.ld [--gc-sections|--no-gc-sections] [-Map output.map] -o output.elf input.o ...\n");
            lk_destroy(l); return 0;
        } else if(p[0]=='-') lk_error(l,"unsupported option '%s'",p);
        else {
            if(count==LK_FILES) lk_error(l,"object count limit exceeded"); inputs[count++]=p;
        }
    }
    if(!l->script || !l->output || !count) {
        fprintf(stderr,"badge-link: requires -T script, -o output and input objects (see --help)\n"); lk_destroy(l); return 2;
    }
    if(l->map!=NULL) {
        const char *a_path=lk_destination(l,l->output),*b_path=lk_destination(l,l->map);
        if(strcmp(a_path,b_path)==0) lk_error(l,"ELF/map output alias");
        struct stat a,b;
        if(stat(l->output,&a)==0 && stat(l->map,&b)==0 && a.st_dev==b.st_dev && a.st_ino==b.st_ino) lk_error(l,"ELF/map output alias");
    }
    l->globals=lk_allocate(l,(LK_GLOBALS+1)*sizeof(*l->globals)); l->hash=lk_allocate(l,LK_HASH*sizeof(*l->hash));
    size_t size; unsigned char *script=lk_read(l,l->script,65536,&size);
    if(memchr(script,0,size)!=NULL) lk_error(l,"NUL byte in linker script");
    char *text=lk_allocate(l,size+1); memcpy(text,script,size+1); free(l->temporary); l->temporary=NULL;
    lk_parse(l,text);
    for(unsigned k=0;k<count;++k) lk_load(l,inputs[k]);
    lk_select(l); lk_gc(l); lk_layout(l); lk_emit(l);
    if(l->map && !host_write_file("badge-link",l->map,l->map_bytes,l->map_size)) { lk_destroy(l); return 1; }
    if(!host_write_file("badge-link",l->output,l->image,l->image_size)) { lk_destroy(l); return 1; }
    printf("%s: %zu bytes, %u objects, %u output sections, entry %s\n",l->output,l->image_size,l->file_count,l->output_count,l->entry);
    lk_destroy(l); return 0;
}
