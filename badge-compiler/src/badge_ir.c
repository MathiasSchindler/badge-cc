#define _POSIX_C_SOURCE 200809L
#include "c_semantic.h"
#include "target.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *data; size_t size,capacity; } ir_buffer;
typedef struct { c_type_t *type; const char *text; } ir_value;
typedef struct ir_label { node_t *node; const char *name; unsigned block; struct ir_label *next; } ir_label;
typedef struct ir_case { node_t *node; unsigned block; struct ir_case *next; } ir_case;
typedef struct ir_switch { ir_case *cases; struct ir_switch *parent; unsigned end; } ir_switch;
typedef struct ir_reloc { uint32_t offset,size; const char *type,*value; struct ir_reloc *next; } ir_reloc;
typedef struct {
    parser_t *p; sema_t *sema; node_t **initializers,**definitions;
    const char **places; unsigned char *allocated;
    ir_buffer body,entry; ir_buffer *buffer;
    unsigned reg,temp,label,block,break_block,continue_block,depth;
    int terminated,ssa_expressions;
    symbol_t *function; ir_label *labels; ir_switch *switches;
} ir_context;

static int ir_kind(node_t *n,const char *kind) { return n!=NULL && strcmp(n->kind,kind)==0; }
static sem_info_t *ir_info(node_t *n) { return n==NULL?NULL:n->semantic; }
static _Noreturn void ir_error(ir_context *c,node_t *n,const char *message) {
    c->p->position=n==NULL?0:n->begin; parse_error(c->p,"%s",message);
}
static void *ir_allocate(ir_context *c,size_t size) { return parse_allocate(c->p,size); }
static const char *ir_format(ir_context *c,const char *format,...) {
    va_list ap,copy; va_start(ap,format); va_copy(copy,ap);
    int n=vsnprintf(NULL,0,format,copy); va_end(copy);
    if(n<0 || n>65536) ir_error(c,NULL,"IR formatting limit exceeded");
    char *text=ir_allocate(c,(size_t)n+1); vsnprintf(text,(size_t)n+1,format,ap); va_end(ap); return text;
}
static void ir_bytes(ir_context *c,const char *text,size_t length) {
    if(c->buffer==NULL) { output_bytes(c->p,text,length); return; }
    ir_buffer *b=c->buffer;
    if(length>PARSE_MAX_OUTPUT-b->size) ir_error(c,NULL,"IR output limit exceeded");
    size_t needed=b->size+length+1;
    if(needed>b->capacity) {
        size_t capacity=b->capacity?b->capacity:4096; while(capacity<needed) capacity*=2;
        char *data=ir_allocate(c,capacity); if(b->size) memcpy(data,b->data,b->size);
        b->data=data; b->capacity=capacity;
    }
    memcpy(b->data+b->size,text,length); b->size+=length; b->data[b->size]=0;
}
static void ir_text(ir_context *c,const char *text) { ir_bytes(c,text,strlen(text)); }
static void ir_emit(ir_context *c,const char *format,...) {
    char text[2048]; va_list ap; va_start(ap,format); int n=vsnprintf(text,sizeof(text),format,ap); va_end(ap);
    if(n<0 || (size_t)n>=sizeof(text)) ir_error(c,NULL,"IR instruction/name exceeds bound");
    ir_bytes(c,text,(size_t)n);
}
static void ir_string(ir_context *c,const unsigned char *text,size_t length) {
    ir_text(c,"\"");
    for(size_t k=0;k<length;++k) {
        unsigned ch=text[k];
        if(ch<32 || ch>=127 || ch=='"' || ch=='\\') ir_emit(c,"\\%02X",ch);
        else ir_bytes(c,(const char *)&text[k],1);
    }
    ir_text(c,"\"");
}
static int ir_integer(c_type_t *t) { return (t->kind>=T_BOOL && t->kind<=T_ULLONG) || t->kind==T_ENUM; }
static int ir_float(c_type_t *t) { return t->kind>=T_FLOAT && t->kind<=T_LDOUBLE; }
static int ir_record(c_type_t *t) { return t->kind==T_STRUCT || t->kind==T_UNION; }
static int ir_aggregate(c_type_t *t) { return ir_record(t) || t->kind==T_ARRAY; }
static unsigned ir_object_qualifiers(c_type_t *t) {
    unsigned qualifiers=t->qualifiers;
    while(t->kind==T_ARRAY) { t=t->base; qualifiers|=t->qualifiers; }
    return qualifiers;
}
static unsigned ir_bits(c_type_t *t) { return t->kind==T_BOOL?1:t->size*8; }
static uint64_t ir_mask(c_type_t *t) { unsigned bits=ir_bits(t); return bits==64?UINT64_MAX:(UINT64_C(1)<<bits)-1; }
static c_type_t *ir_primitive(ir_context *c,type_kind_t kind) { return badge_sema_primitive(c->sema,kind); }
static c_type_t *ir_pointer(ir_context *c,c_type_t *base) {
    c_type_t *t=ir_allocate(c,sizeof(*t)); t->kind=T_POINTER; t->base=base;
    t->size=t->alignment=4; t->complete=1; t->unqualified=t; return t;
}
static c_type_t *ir_promote(ir_context *c,c_type_t *t) {
    return ir_integer(t) && t->size<4?ir_primitive(c,T_INT):t;
}
static const char *ir_type(ir_context *c,c_type_t *t) {
    if(t->kind==T_VOID) return "void";
    if(t->kind==T_POINTER || t->kind==T_FUNCTION) return "ptr";
    if(ir_integer(t)) return ir_format(c,"i%u",ir_bits(t));
    if(ir_float(t)) return t->kind==T_FLOAT?"float":"double";
    if(ir_aggregate(t)) return ir_format(c,"[%" PRIu32 " x i8]",t->size);
    ir_error(c,NULL,"unsupported IR type");
}
static const char *ir_storage_type(ir_context *c,c_type_t *t) {
    return t->kind==T_BOOL?"i8":ir_type(c,t);
}
static unsigned ir_carrier_size(c_type_t *t) { return t->alignment>=8?8:4; }
static int ir_byval(c_type_t *t) { return ir_record(t) && t->size>64; }
static unsigned ir_byval_alignment(c_type_t *t) { return t->alignment>=8?8:4; }
static const char *ir_carrier(ir_context *c,c_type_t *t) {
    if(!t->complete || !t->size) ir_error(c,NULL,"incomplete record ABI is outside the C05 contract");
    unsigned word=ir_carrier_size(t); return ir_format(c,"[%" PRIu32 " x i%u]",(t->size+word-1)/word,word*8);
}
static const char *ir_small_return(c_type_t *t) { return t->size==1?"i8":t->size==2?"i16":"i32"; }
static const char *ir_return_type(ir_context *c,c_type_t *fn) {
    if(ir_record(fn->base) && (!fn->base->complete || !fn->base->size))
        ir_error(c,NULL,"incomplete record ABI is outside the C05 contract");
    c_type_t *t=fn->base; return ir_record(t)?(t->size>4?"void":ir_small_return(t)):ir_type(c,t);
}
static const char *ir_extension(c_type_t *t) {
    return ir_integer(t) && t->size<4?(t->is_unsigned?"zeroext ":"signext "):"";
}
static const char *ir_symbol(ir_context *c,symbol_t *s) {
    if(s->kind==S_STRING) return ir_format(c,"@\".str.%u\"",s->id);
    if(s->static_storage && s->linkage==0)
        return ir_format(c,"@\".b.%u.%s\"",s->id,s->name);
    return ir_format(c,"@\"%s\"",s->name);
}
static const char *ir_reg(ir_context *c) { return ir_format(c,"%%v%u",++c->reg); }
static unsigned ir_new_block(ir_context *c) { return ++c->label; }
static void ir_branch(ir_context *c,unsigned target) {
    if(!c->terminated) ir_emit(c,"  br label %%b%u\n",target); c->terminated=1;
}
static void ir_block(ir_context *c,unsigned block) {
    c->block=block; c->terminated=0; ir_emit(c,"b%u:\n",block);
}
static void ir_live(ir_context *c) { if(c->terminated) ir_block(c,ir_new_block(c)); }
static const char *ir_temp(ir_context *c,c_type_t *t,unsigned extra) {
    const char *name=ir_format(c,"%%t%u",++c->temp);
    ir_buffer *saved=c->buffer; c->buffer=&c->entry;
    if(extra) ir_emit(c,"  %s = alloca [%u x i8], align %u\n",name,extra,t->alignment);
    else ir_emit(c,"  %s = alloca %s, align %u\n",name,ir_storage_type(c,t),t->alignment);
    c->buffer=saved; return name;
}
static const char *ir_place_symbol(ir_context *c,symbol_t *s) {
    if(s->static_storage || s->kind==S_FUNCTION || s->kind==S_STRING) return ir_symbol(c,s);
    if(!c->allocated[s->id]) {
        c->places[s->id]=ir_format(c,"%%s%u",s->id); c->allocated[s->id]=1;
        ir_buffer *saved=c->buffer; c->buffer=&c->entry;
        ir_emit(c,"  %s = alloca %s, align %u\n",c->places[s->id],ir_storage_type(c,s->type),s->alignment);
        c->buffer=saved;
    }
    return c->places[s->id];
}
static const char *ir_offset(ir_context *c,const char *base,const char *offset) {
    if(strcmp(offset,"0")==0) return base;
    const char *v=ir_reg(c); ir_emit(c,"  %s = getelementptr i8, ptr %s, i32 %s\n",v,base,offset); return v;
}
static ir_value ir_cast(ir_context *,ir_value,c_type_t *);
static ir_value ir_expression(ir_context *,node_t *);
static const char *ir_condition(ir_context *,node_t *);
static const char *ir_lvalue(ir_context *,node_t *);
static void ir_initialize(ir_context *,c_type_t *,const char *,node_t *);
static const char *ir_truth(ir_context *c,ir_value v) {
    if(v.type->kind==T_BOOL) return v.text;
    const char *r=ir_reg(c);
    if(ir_float(v.type)) ir_emit(c,"  %s = fcmp une %s %s, 0.0\n",r,ir_type(c,v.type),v.text);
    else ir_emit(c,"  %s = icmp ne %s %s, %s\n",r,ir_type(c,v.type),v.text,v.type->kind==T_POINTER?"null":"0");
    return r;
}
static ir_value ir_cast(ir_context *c,ir_value v,c_type_t *to) {
    if(to==NULL || to==v.type) return v;
    if(to->kind==T_VOID) return (ir_value){to,""};
    if(ir_aggregate(to)) { if(!ir_aggregate(v.type)) ir_error(c,NULL,"invalid aggregate conversion"); v.type=to; return v; }
    if(to->kind==T_BOOL) return (ir_value){to,ir_truth(c,v)};
    const char *op=NULL;
    if(v.type->kind==T_POINTER && to->kind==T_POINTER) { v.type=to; return v; }
    if(ir_integer(v.type) && ir_integer(to)) {
        if(ir_bits(v.type)==ir_bits(to)) { v.type=to; return v; }
        op=ir_bits(v.type)>ir_bits(to)?"trunc":v.type->is_unsigned?"zext":"sext";
    } else if(v.type->kind==T_POINTER && ir_integer(to)) op="ptrtoint";
    else if(ir_integer(v.type) && to->kind==T_POINTER) op="inttoptr";
    else if(ir_integer(v.type) && ir_float(to)) op=v.type->is_unsigned?"uitofp":"sitofp";
    else if(ir_float(v.type) && ir_integer(to)) op=to->is_unsigned?"fptoui":"fptosi";
    else if(ir_float(v.type) && ir_float(to)) {
        if(v.type->size==to->size) { v.type=to; return v; }
        op=v.type->size>to->size?"fptrunc":"fpext";
    }
    if(op==NULL) ir_error(c,NULL,"unsupported scalar conversion");
    const char *r=ir_reg(c); ir_emit(c,"  %s = %s %s %s to %s\n",r,op,ir_type(c,v.type),v.text,ir_type(c,to));
    return (ir_value){to,r};
}
static void ir_copy(ir_context *,const char *,const char *,uint32_t,int);
static ir_value ir_load(ir_context *c,c_type_t *t,const char *place) {
    if(ir_record(t)) {
        const char *copy=ir_temp(c,t,0);
        ir_copy(c,copy,place,t->size,(t->qualifiers&Q_VOLATILE)!=0);
        return (ir_value){t->unqualified,copy};
    }
    if(t->kind==T_ARRAY) return (ir_value){ir_pointer(c,t->base),place};
    if(t->kind==T_FUNCTION) return (ir_value){ir_pointer(c,t),place};
    const char *r=ir_reg(c);
    ir_emit(c,"  %s = load %s%s, ptr %s, align %u\n",r,(t->qualifiers&Q_VOLATILE)?"volatile ":"",ir_storage_type(c,t),place,t->alignment);
    if(t->kind==T_BOOL) {
        const char *b=ir_reg(c); ir_emit(c,"  %s = trunc i8 %s to i1\n",b,r); r=b;
    }
    return (ir_value){t,r};
}
static void ir_copy(ir_context *c,const char *dest,const char *source,uint32_t size,int vol) {
    if(size) ir_emit(c,"  call void @llvm.memcpy.inline.p0.p0.i32(ptr %s, ptr %s, i32 %" PRIu32 ", i1 %s)\n",dest,source,size,vol?"true":"false");
}
static void ir_zero(ir_context *c,const char *dest,uint32_t size,int vol) {
    if(size) ir_emit(c,"  call void @llvm.memset.inline.p0.i32(ptr %s, i8 0, i32 %" PRIu32 ", i1 %s)\n",dest,size,vol?"true":"false");
}
static void ir_store(ir_context *c,c_type_t *t,const char *place,ir_value value) {
    value=ir_cast(c,value,t);
    if(ir_record(t)) { ir_copy(c,place,value.text,t->size,((t->qualifiers|value.type->qualifiers)&Q_VOLATILE)!=0); return; }
    const char *text=value.text;
    if(t->kind==T_BOOL) {
        text=ir_reg(c); ir_emit(c,"  %s = zext i1 %s to i8\n",text,value.text);
    }
    ir_emit(c,"  store %s%s %s, ptr %s, align %u\n",(t->qualifiers&Q_VOLATILE)?"volatile ":"",ir_storage_type(c,t),text,place,t->alignment);
}
static const char *ir_lvalue(ir_context *c,node_t *n) {
    sem_info_t *i=ir_info(n); node_t *a=n->first,*b=a==NULL?NULL:a->next;
    if(ir_kind(n,"identifier")) return ir_place_symbol(c,i->symbol);
    if(ir_kind(n,"parenthesized_expression")) return ir_lvalue(c,a);
    if(ir_kind(n,"generic_selection")) return ir_lvalue(c,i->selected);
    if(ir_kind(n,"unary") && strcmp(n->text,"*")==0) return ir_expression(c,a).text;
    if(ir_kind(n,"member")) {
        const char *base=strcmp(n->text,"->")==0?ir_expression(c,a).text:
            ir_info(a)->lvalue?ir_lvalue(c,a):ir_expression(c,a).text;
        return ir_offset(c,base,ir_format(c,"%" PRIu32,i->field_offset));
    }
    if(ir_kind(n,"subscript")) {
        ir_value left=ir_expression(c,a),right=ir_expression(c,b);
        if(left.type->kind!=T_POINTER) { ir_value swap=left; left=right; right=swap; }
        right=ir_cast(c,right,ir_primitive(c,T_INT));
        const char *offset=right.text;
        if(left.type->base->size!=1) {
            offset=ir_reg(c); ir_emit(c,"  %s = mul i32 %s, %" PRIu32 "\n",offset,right.text,left.type->base->size);
        }
        return ir_offset(c,left.text,offset);
    }
    if(ir_kind(n,"compound_literal")) {
        const char *place=ir_temp(c,i->type,0); ir_initialize(c,i->type,place,b); return place;
    }
    if(ir_kind(n,"string_literal")) return ir_symbol(c,i->symbol);
    ir_error(c,n,"unsupported addressable expression");
}
static ir_value ir_binary(ir_context *c,const char *op,ir_value left,ir_value right,c_type_t *result,c_type_t *operation) {
    if(left.type->kind==T_POINTER || right.type->kind==T_POINTER) {
        if(strcmp(op,"+")==0 || strcmp(op,"-")==0) {
            if(right.type->kind==T_POINTER && left.type->kind!=T_POINTER) { ir_value swap=left; left=right; right=swap; }
            if(right.type->kind==T_POINTER) {
                ir_value x=ir_cast(c,left,ir_primitive(c,T_UINT)),y=ir_cast(c,right,ir_primitive(c,T_UINT));
                const char *difference=ir_reg(c); ir_emit(c,"  %s = sub i32 %s, %s\n",difference,x.text,y.text);
                if(left.type->base->size!=1) {
                    const char *r=ir_reg(c); ir_emit(c,"  %s = sdiv i32 %s, %" PRIu32 "\n",r,difference,left.type->base->size); difference=r;
                }
                return (ir_value){result,difference};
            }
            right=ir_cast(c,right,ir_primitive(c,T_INT)); const char *offset=right.text;
            if(left.type->base->size!=1 || strcmp(op,"-")==0) {
                offset=ir_reg(c);
                ir_emit(c,"  %s = mul i32 %s, %" PRId64 "\n",offset,right.text,
                    (strcmp(op,"-")==0?-1:1)*(int64_t)left.type->base->size);
            }
            return (ir_value){result,ir_offset(c,left.text,offset)};
        }
        if(left.type->kind!=T_POINTER) left=ir_cast(c,left,right.type);
        if(right.type->kind!=T_POINTER) right=ir_cast(c,right,left.type);
        const char *pred=strcmp(op,"==")==0?"eq":strcmp(op,"!=")==0?"ne":strcmp(op,"<")==0?"ult":
            strcmp(op,">")==0?"ugt":strcmp(op,"<=")==0?"ule":"uge";
        const char *r=ir_reg(c); ir_emit(c,"  %s = icmp %s ptr %s, %s\n",r,pred,left.text,right.text);
        return ir_cast(c,(ir_value){ir_primitive(c,T_BOOL),r},result);
    }
    c_type_t *type=operation==NULL?left.type:operation;
    left=ir_cast(c,left,type); right=ir_cast(c,right,type);
    const char *instruction=NULL,*pred=NULL;
    int floating=ir_float(type);
    if(strcmp(op,"+")==0) instruction=floating?"fadd":"add";
    else if(strcmp(op,"-")==0) instruction=floating?"fsub":"sub";
    else if(strcmp(op,"*")==0) instruction=floating?"fmul":"mul";
    else if(strcmp(op,"/")==0) instruction=floating?"fdiv":type->is_unsigned?"udiv":"sdiv";
    else if(strcmp(op,"%")==0) instruction=type->is_unsigned?"urem":"srem";
    else if(strcmp(op,"&")==0) instruction="and";
    else if(strcmp(op,"|")==0) instruction="or";
    else if(strcmp(op,"^")==0) instruction="xor";
    else if(strcmp(op,"<<")==0) instruction="shl";
    else if(strcmp(op,">>")==0) instruction=type->is_unsigned?"lshr":"ashr";
    else if(strcmp(op,"==")==0) pred=floating?"oeq":"eq";
    else if(strcmp(op,"!=")==0) pred=floating?"une":"ne";
    else if(strcmp(op,"<")==0) pred=floating?"olt":type->is_unsigned?"ult":"slt";
    else if(strcmp(op,">")==0) pred=floating?"ogt":type->is_unsigned?"ugt":"sgt";
    else if(strcmp(op,"<=")==0) pred=floating?"ole":type->is_unsigned?"ule":"sle";
    else if(strcmp(op,">=")==0) pred=floating?"oge":type->is_unsigned?"uge":"sge";
    if(instruction==NULL && pred==NULL) ir_error(c,NULL,"unsupported binary operation");
    const char *r=ir_reg(c);
    if(pred) {
        ir_emit(c,"  %s = %s %s %s %s, %s\n",r,floating?"fcmp":"icmp",pred,ir_type(c,type),left.text,right.text);
        return ir_cast(c,(ir_value){ir_primitive(c,T_BOOL),r},result);
    }
    ir_emit(c,"  %s = %s %s %s, %s\n",r,instruction,ir_type(c,type),left.text,right.text);
    return ir_cast(c,(ir_value){type,r},result);
}
static const char *ir_pack(ir_context *c,ir_value value) {
    c_type_t *t=value.type; unsigned word=ir_carrier_size(t),size=(t->size+word-1)/word*word;
    const char *place=ir_temp(c,t,size); ir_zero(c,place,size,0); ir_copy(c,place,value.text,t->size,(t->qualifiers&Q_VOLATILE)!=0);
    const char *r=ir_reg(c); ir_emit(c,"  %s = load %s, ptr %s, align %u\n",r,ir_carrier(c,t),place,t->alignment); return r;
}
static void ir_signature(ir_context *c,c_type_t *fn,int names) {
    int comma=0;
    if(ir_record(fn->base) && fn->base->size>4) {
        ir_emit(c,"ptr sret(%s) align %u%s",ir_type(c,fn->base),fn->base->alignment,names?" %result":""); comma=1;
    }
    for(size_t k=0;k<fn->parameter_count;++k) {
        c_type_t *t=fn->parameters[k]; if(comma) ir_text(c,", "); comma=1;
        if(ir_byval(t)) ir_emit(c,"ptr byval(%s) align %u ",ir_type(c,t),ir_byval_alignment(t));
        else if(ir_record(t)) { ir_text(c,ir_carrier(c,t)); if(names) ir_text(c," "); }
        else ir_emit(c,"%s %s",ir_type(c,t),ir_extension(t));
        if(names) ir_emit(c,"%%arg%zu",k);
    }
    if(fn->variadic || !fn->prototype) { if(comma) ir_text(c,", "); ir_text(c,"..."); }
}
static ir_value ir_call(ir_context *c,node_t *n) {
    node_t *a=n->first; ir_value callee=ir_expression(c,a); c_type_t *fn=callee.type->base;
    size_t count=0; for(node_t *arg=a->next;arg;arg=arg->next) ++count;
    ir_value *values=ir_allocate(c,(count+1)*sizeof(*values)); size_t k=0;
    for(node_t *arg=a->next;arg;arg=arg->next) values[k++]=ir_expression(c,arg);
    const char *result=NULL,*place=NULL;
    int record=ir_record(fn->base),sret=record && fn->base->size>4;
    if(sret) place=ir_temp(c,fn->base,0);
    if(fn->base->kind!=T_VOID && !sret) result=ir_reg(c);
    for(k=0;k<count;++k) if(ir_record(values[k].type)) {
        if(ir_byval(values[k].type)) {
            c_type_t type=*values[k].type; type.alignment=ir_byval_alignment(&type);
            const char *copy=ir_temp(c,&type,0);
            ir_copy(c,copy,values[k].text,type.size,0); values[k].text=copy;
        } else values[k].text=ir_pack(c,values[k]);
    }
    ir_text(c,"  "); if(result) ir_emit(c,"%s = ",result);
    ir_emit(c,"call %s%s ",record?"":ir_extension(fn->base),ir_return_type(c,fn));
    if(fn->variadic || !fn->prototype) { ir_text(c,"("); ir_signature(c,fn,0); ir_text(c,") "); }
    ir_emit(c,"%s(",callee.text); int comma=0;
    if(sret) { ir_emit(c,"ptr sret(%s) align %u %s",ir_type(c,fn->base),fn->base->alignment,place); comma=1; }
    for(k=0;k<count;++k) {
        if(comma) ir_text(c,", "); comma=1;
        c_type_t *t=values[k].type;
        if(ir_byval(t)) ir_emit(c,"ptr byval(%s) align %u %s",ir_type(c,t),ir_byval_alignment(t),values[k].text);
        else if(ir_record(t)) ir_emit(c,"%s %s",ir_carrier(c,t),values[k].text);
        else ir_emit(c,"%s %s%s",ir_type(c,t),ir_extension(t),values[k].text);
    }
    ir_text(c,")\n");
    if(record && !sret) {
        unsigned size=fn->base->size==1?1:fn->base->size==2?2:4;
        place=ir_temp(c,fn->base,size);
        ir_emit(c,"  store %s %s, ptr %s, align %u\n",ir_small_return(fn->base),result,place,fn->base->alignment);
    }
    return (ir_value){fn->base,record?place:result==NULL?"":result};
}
static ir_value ir_conditional(ir_context *c,node_t *n) {
    sem_info_t *i=ir_info(n); node_t *a=n->first,*b=a->next,*d=b->next;
    const char *truth=ir_condition(c,a); unsigned yes=ir_new_block(c),no=ir_new_block(c),end=ir_new_block(c);
    int scalar=c->ssa_expressions && !ir_aggregate(i->type) && i->type->kind!=T_VOID;
    const char *place=i->type->kind==T_VOID || scalar?NULL:ir_temp(c,i->type,0);
    ir_emit(c,"  br i1 %s, label %%b%u, label %%b%u\n",truth,yes,no); c->terminated=1;
    ir_block(c,yes); ir_value x=ir_expression(c,b); if(scalar) x=ir_cast(c,x,i->type);
    if(place) ir_store(c,i->type,place,x); unsigned xblock=c->block; ir_branch(c,end);
    ir_block(c,no); ir_value y=ir_expression(c,d); if(scalar) y=ir_cast(c,y,i->type);
    if(place) ir_store(c,i->type,place,y); unsigned yblock=c->block; ir_branch(c,end);
    ir_block(c,end);
    if(scalar) {
        const char *r=ir_reg(c);
        ir_emit(c,"  %s = phi %s [%s, %%b%u], [%s, %%b%u]\n",r,ir_type(c,i->type),x.text,xblock,y.text,yblock);
        return (ir_value){i->type,r};
    }
    return place?ir_load(c,i->type,place):(ir_value){i->type,""};
}
static ir_value ir_logical(ir_context *c,node_t *n) {
    c_type_t *type=ir_primitive(c,c->ssa_expressions?T_BOOL:T_INT);
    const char *place=c->ssa_expressions?NULL:ir_temp(c,type,0);
    int conjunction=strcmp(n->text,"&&")==0;
    const char *left=ir_condition(c,n->first);
    unsigned evaluate=ir_new_block(c),shorted=ir_new_block(c),end=ir_new_block(c);
    ir_emit(c,"  br i1 %s, label %%b%u, label %%b%u\n",left,conjunction?evaluate:shorted,conjunction?shorted:evaluate);
    c->terminated=1; ir_block(c,shorted);
    if(place) ir_store(c,type,place,(ir_value){type,conjunction?"0":"1"});
    ir_branch(c,end);
    ir_block(c,evaluate);
    const char *right=ir_condition(c,n->first->next);
    if(place) ir_store(c,type,place,ir_cast(c,(ir_value){ir_primitive(c,T_BOOL),right},type));
    unsigned right_block=c->block; ir_branch(c,end);
    ir_block(c,end);
    if(!place) {
        const char *r=ir_reg(c);
        ir_emit(c,"  %s = phi i1 [%s, %%b%u], [%s, %%b%u]\n",r,conjunction?"false":"true",shorted,right,right_block);
        return (ir_value){type,r};
    }
    return ir_load(c,type,place);
}
static const char *ir_condition(ir_context *c,node_t *n) {
    if(!c->ssa_expressions) return ir_truth(c,ir_expression(c,n));
    if(++c->depth>PARSE_MAX_DEPTH) ir_error(c,n,"IR condition depth exceeded");
    sem_info_t *i=ir_info(n); node_t *a=n->first,*b=a==NULL?NULL:a->next;
    if(i==NULL || i->type==NULL) ir_error(c,n,"condition lacks semantic type");
    const char *truth;
    if(i->constant && !i->lvalue && ir_integer(i->type))
        truth=(i->value&ir_mask(i->type))?"true":"false";
    else if(ir_kind(n,"parenthesized_expression"))
        truth=ir_condition(c,a);
    else if(ir_kind(n,"generic_selection"))
        truth=ir_condition(c,i->selected);
    else if(ir_kind(n,"binary") && (!strcmp(n->text,"&&") || !strcmp(n->text,"||")))
        truth=ir_logical(c,n).text;
    else if(ir_kind(n,"binary") && !strcmp(n->text,",")) {
        (void)ir_expression(c,a); truth=ir_condition(c,b);
    } else if(ir_kind(n,"binary") && (!strcmp(n->text,"==") || !strcmp(n->text,"!=") ||
              !strcmp(n->text,"<") || !strcmp(n->text,">") ||
              !strcmp(n->text,"<=") || !strcmp(n->text,">="))) {
        ir_value left=ir_expression(c,a),right=ir_expression(c,b);
        truth=ir_binary(c,n->text,left,right,ir_primitive(c,T_BOOL),i->operation_type).text;
    } else if(ir_kind(n,"unary") && !strcmp(n->text,"!")) {
        const char *value=ir_condition(c,a); truth=ir_reg(c);
        ir_emit(c,"  %s = xor i1 %s, true\n",truth,value);
    } else truth=ir_truth(c,ir_expression(c,n));
    --c->depth; return truth;
}
static ir_value ir_raw_expression(ir_context *c,node_t *n) {
    sem_info_t *i=ir_info(n); node_t *a=n->first,*b=a==NULL?NULL:a->next;
    if(i==NULL || i->type==NULL) ir_error(c,n,"expression lacks semantic type");
    if(i->constant && !i->lvalue && ir_integer(i->type))
        return (ir_value){i->type,ir_format(c,"%" PRIu64,i->value&ir_mask(i->type))};
    if(ir_kind(n,"identifier")) return ir_load(c,i->type,ir_place_symbol(c,i->symbol));
    if(ir_kind(n,"string_literal")) return (ir_value){ir_pointer(c,i->type->base),ir_symbol(c,i->symbol)};
    if(ir_kind(n,"parenthesized_expression")) return ir_raw_expression(c,a);
    if(ir_kind(n,"cast")) return ir_cast(c,ir_expression(c,b),i->type);
    if(ir_kind(n,"member") || ir_kind(n,"subscript") || ir_kind(n,"compound_literal")) return ir_load(c,i->type,ir_lvalue(c,n));
    if(ir_kind(n,"call")) return ir_call(c,n);
    if(ir_kind(n,"conditional")) return ir_conditional(c,n);
    if(ir_kind(n,"binary") || ir_kind(n,"assignment")) {
        if(strcmp(n->text,",")==0) { (void)ir_expression(c,a); return ir_expression(c,b); }
        if(strcmp(n->text,"&&")==0 || strcmp(n->text,"||")==0) return ir_cast(c,ir_logical(c,n),i->type);
        if(ir_kind(n,"assignment")) {
            const char *place=ir_lvalue(c,a); c_type_t *type=ir_info(a)->type;
            ir_value right=ir_expression(c,b);
            if(strcmp(n->text,"=")!=0) {
                char op[4]; size_t length=strlen(n->text)-1; memcpy(op,n->text,length); op[length]=0;
                ir_value left=ir_load(c,type,place);
                right=ir_binary(c,op,left,right,type,i->operation_type);
            }
            right=ir_cast(c,right,type); ir_store(c,type,place,right); return right;
        }
        ir_value left=ir_expression(c,a),right=ir_expression(c,b);
        return ir_binary(c,n->text,left,right,i->type,i->operation_type);
    }
    if(ir_kind(n,"unary") || ir_kind(n,"postfix")) {
        const char *op=n->text;
        if(strcmp(op,"&")==0) return (ir_value){i->type,ir_lvalue(c,a)};
        if(strcmp(op,"*")==0) return ir_load(c,i->type,ir_lvalue(c,n));
        if(strcmp(op,"++")==0 || strcmp(op,"--")==0) {
            const char *place=ir_lvalue(c,a); ir_value old=ir_load(c,ir_info(a)->type,place);
            ir_value next=ir_binary(c,strcmp(op,"++")==0?"+":"-",old,(ir_value){ir_primitive(c,T_INT),"1"},i->type,ir_promote(c,i->type));
            ir_store(c,ir_info(a)->type,place,next); return ir_kind(n,"postfix")?old:next;
        }
        if(strcmp(op,"!")==0) {
            const char *truth=ir_condition(c,a),*r=ir_reg(c); ir_emit(c,"  %s = xor i1 %s, true\n",r,truth);
            return ir_cast(c,(ir_value){ir_primitive(c,T_BOOL),r},i->type);
        }
        ir_value value=ir_expression(c,a);
        value=ir_cast(c,value,i->type);
        if(strcmp(op,"+")==0) return value;
        const char *r=ir_reg(c);
        if(strcmp(op,"-")==0 && ir_float(i->type)) ir_emit(c,"  %s = fneg %s %s\n",r,ir_type(c,i->type),value.text);
        else if(strcmp(op,"-")==0) ir_emit(c,"  %s = sub %s 0, %s\n",r,ir_type(c,i->type),value.text);
        else ir_emit(c,"  %s = xor %s %s, -1\n",r,ir_type(c,i->type),value.text);
        return (ir_value){i->type,r};
    }
    if(ir_kind(n,"generic_selection")) {
        if(i->selected==NULL) ir_error(c,n,"generic selection lacks its semantic choice");
        return ir_expression(c,i->selected);
    }
    ir_error(c,n,"unsupported IR expression");
}
static ir_value ir_expression(ir_context *c,node_t *n) {
    if(++c->depth>PARSE_MAX_DEPTH) ir_error(c,n,"IR expression depth exceeded");
    ir_value value=ir_raw_expression(c,n); sem_info_t *i=ir_info(n);
    if(i->conversion && i->conversion->kind!=T_ARRAY && i->conversion->kind!=T_FUNCTION)
        value=ir_cast(c,value,i->conversion);
    --c->depth; return value;
}

static node_t *ir_initializer_value(node_t *item) {
    node_t *n=item->first;
    while(ir_kind(n,"field_designator") || ir_kind(n,"index_designator")) n=n->next;
    return n;
}
static void ir_initializer_contract(ir_context *c,c_type_t *t,node_t *n) {
    if(!ir_kind(n,"initializer_list")) return;
    if(t->kind==T_UNION && n->first && n->first->next)
        ir_error(c,n,"multiple union initializer items are outside the C05 contract");
    for(node_t *item=n->first;item;item=item->next) {
        c_type_t *parent=t;
        for(node_t *part=item->first;ir_kind(part,"field_designator") || ir_kind(part,"index_designator");part=part->next) {
            if(parent->kind==T_UNION && parent!=t)
                ir_error(c,part,"nested union member designators are outside the C05 contract");
            parent=ir_info(part)->type;
        }
        ir_initializer_contract(c,ir_info(item)->type,ir_initializer_value(item));
    }
}
static void ir_initialize(ir_context *c,c_type_t *t,const char *place,node_t *n) {
    if(n==NULL) return;
    if(ir_kind(n,"initializer_list")) {
        ir_zero(c,place,t->size,(ir_object_qualifiers(t)&Q_VOLATILE)!=0);
        for(node_t *item=n->first;item;item=item->next) {
            sem_info_t *i=ir_info(item);
            const char *target=ir_offset(c,place,ir_format(c,"%" PRIu32,i->field_offset));
            c_type_t *type=i->type;
            if(t->qualifiers&Q_VOLATILE) {
                type=ir_allocate(c,sizeof(*type)); *type=*i->type; type->qualifiers|=Q_VOLATILE;
            }
            ir_initialize(c,type,target,ir_initializer_value(item));
        }
    } else if(t->kind==T_ARRAY && ir_kind(n,"string_literal")) {
        symbol_t *s=ir_info(n)->symbol; uint32_t size=(uint32_t)s->byte_count;
        if(size>t->size) size=t->size;
        ir_zero(c,place,t->size,(ir_object_qualifiers(t)&Q_VOLATILE)!=0);
        ir_copy(c,place,ir_symbol(c,s),size,(ir_object_qualifiers(t)&Q_VOLATILE)!=0);
    } else ir_store(c,t,place,ir_expression(c,n));
}
static const char *ir_constant_pointer(ir_context *c,sem_info_t *i) {
    uint32_t base=(uint32_t)i->value; const char *value;
    if(i->symbol) value=ir_symbol(c,i->symbol);
    else value=base?ir_format(c,"inttoptr (i32 %" PRIu32 " to ptr)",base):"null";
    if(i->addend) value=ir_format(c,"getelementptr (i8, ptr %s, i32 %" PRIu32 ")",value,(uint32_t)i->addend);
    return value;
}
static void ir_global_value(ir_context *c,c_type_t *t,node_t *n,uint32_t offset,
                            unsigned char *bytes,ir_reloc **relocations,uint32_t total) {
    if(t->size>total || offset>total-t->size) ir_error(c,n,"global initializer exceeds object");
    if(n==NULL) return;
    memset(bytes+offset,0,t->size);
    for(ir_reloc *r=*relocations;r;r=r->next)
        if(offset<r->offset+r->size && r->offset<offset+t->size) r->size=0;
    if(ir_kind(n,"initializer_list")) {
        for(node_t *item=n->first;item;item=item->next) {
            sem_info_t *i=ir_info(item); ir_global_value(c,i->type,ir_initializer_value(item),offset+i->field_offset,bytes,relocations,total);
        }
        return;
    }
    sem_info_t *i=ir_info(n);
    if(t->kind==T_ARRAY && ir_kind(n,"string_literal")) {
        symbol_t *s=i->symbol; size_t size=s->byte_count<t->size?s->byte_count:t->size;
        memcpy(bytes+offset,s->bytes,size); return;
    }
    if(ir_aggregate(t)) ir_error(c,n,"nonconstant static aggregate expression is unsupported");
    if(t->kind==T_POINTER) {
        const char *value=ir_constant_pointer(c,i);
        if(!strcmp(value,"null")) return;
        ir_reloc *r=ir_allocate(c,sizeof(*r)); r->offset=offset; r->size=4; r->type="ptr";
        r->value=value; r->next=*relocations; *relocations=r; return;
    }
    uint64_t value=i->value;
    if(ir_integer(t)) {
        if(!i->type->is_unsigned && ir_integer(i->type) && i->type->size<8) {
            unsigned bits=ir_bits(i->type); uint64_t sign=UINT64_C(1)<<(bits-1),mask=ir_mask(i->type);
            if(value&sign) value|=~mask;
        }
        value=t->kind==T_BOOL?value!=0:value&ir_mask(t);
    } else if(ir_float(t)) {
        double number;
        if(i->type->is_unsigned) number=(double)i->value;
        else {
            uint64_t bits=i->value,mask=ir_mask(i->type); unsigned width=ir_bits(i->type);
            int64_t integer=bits&(UINT64_C(1)<<(width-1))?-1-(int64_t)(~bits&mask):(int64_t)bits;
            number=(double)integer;
        }
        if(t->size==4) { float f=(float)number; uint32_t bits; memcpy(&bits,&f,4); value=bits; }
        else memcpy(&value,&number,8);
    } else ir_error(c,n,"unsupported static initializer conversion");
    for(unsigned k=0;k<t->size;++k) bytes[offset+k]=(unsigned char)(value>>(k*8));
}
static int ir_reloc_order(const void *a,const void *b) {
    const ir_reloc *x=*(const ir_reloc *const *)a,*y=*(const ir_reloc *const *)b;
    return (x->offset>y->offset)-(x->offset<y->offset);
}
static void ir_byte_constant(ir_context *c,const unsigned char *bytes,uint32_t size) {
    ir_emit(c,"[%" PRIu32 " x i8] c",size); ir_string(c,bytes,size);
}
static void ir_global(ir_context *c,symbol_t *s) {
    c_type_t *t=s->type; int defined=s->defined || s->tentative || (s->linkage==0 && s->static_storage) || s->kind==S_STRING;
    ir_emit(c,"%s = ",ir_symbol(c,s));
    if(!defined) { ir_emit(c,"external global %s, align %u\n",ir_storage_type(c,t),s->alignment); return; }
    if(t->size>PARSE_MAX_BYTES) ir_error(c,NULL,"global object exceeds C05 size bound");
    unsigned char *bytes=ir_allocate(c,(size_t)t->size+1); ir_reloc *relocations=NULL;
    if(s->kind==S_STRING) memcpy(bytes,s->bytes,s->byte_count);
    else ir_global_value(c,t,c->initializers[s->id],0,bytes,&relocations,t->size);
    size_t count=0; for(ir_reloc *r=relocations;r;r=r->next) if(r->size) ++count;
    ir_reloc **ordered=ir_allocate(c,(count+1)*sizeof(*ordered)); size_t at=0;
    for(ir_reloc *r=relocations;r;r=r->next) if(r->size) ordered[at++]=r;
    qsort(ordered,count,sizeof(*ordered),ir_reloc_order);
    ir_text(c,s->linkage==2?"dso_local ":"internal ");
    ir_text(c,(ir_object_qualifiers(t)&Q_CONST) || s->kind==S_STRING?"constant ":"global ");
    if(count==0) {
        uint32_t nonzero=0;
        while(nonzero<t->size && bytes[nonzero]==0) ++nonzero;
        if(nonzero==t->size) ir_emit(c,"[%" PRIu32 " x i8] zeroinitializer",t->size);
        else ir_byte_constant(c,bytes,t->size);
    }
    else {
        ir_text(c,"<{ "); uint32_t position=0;
        for(size_t k=0;k<count;++k) {
            ir_reloc *r=ordered[k]; if(r->offset<position) ir_error(c,NULL,"overlapping static relocations");
            if(r->offset>position) ir_emit(c,"[%" PRIu32 " x i8], ",r->offset-position);
            ir_text(c,r->type); if(k+1<count || r->offset+r->size<t->size) ir_text(c,", ");
            position=r->offset+r->size;
        }
        if(position<t->size) ir_emit(c,"[%" PRIu32 " x i8]",t->size-position);
        ir_text(c," }> <{ "); position=0;
        for(size_t k=0;k<count;++k) {
            ir_reloc *r=ordered[k];
            if(r->offset>position) { ir_byte_constant(c,bytes+position,r->offset-position); ir_text(c,", "); }
            ir_emit(c,"%s %s",r->type,r->value);
            if(k+1<count || r->offset+r->size<t->size) ir_text(c,", "); position=r->offset+r->size;
        }
        if(position<t->size) ir_byte_constant(c,bytes+position,t->size-position);
        ir_text(c," }>");
    }
    if(s->section_name) { ir_text(c,", section "); ir_string(c,(const unsigned char *)s->section_name,strlen(s->section_name)); }
    ir_emit(c,", align %u\n",s->alignment);
}

static void ir_collect(ir_context *c,node_t *n) {
    if(ir_kind(n,"init_declarator") && ir_info(n) && ir_info(n)->symbol) {
        symbol_t *s=ir_info(n)->symbol;
        for(node_t *child=n->first->next;child;child=child->next)
            if(!ir_kind(child,"attributes")) {
                ir_initializer_contract(c,s->type,child); c->initializers[s->id]=child;
            }
    }
    if(ir_kind(n,"compound_literal")) ir_initializer_contract(c,ir_info(n)->type,n->first->next);
    if(ir_kind(n,"function_definition")) {
        symbol_t *s=ir_info(n->first->next)->symbol; c->definitions[s->id]=n;
    }
    for(node_t *child=n->first;child;child=child->next) ir_collect(c,child);
}
static node_t *ir_function_declarator(node_t *n) {
    node_t *closest=NULL;
    for(;n && !ir_kind(n,"name");n=n->first) if(!ir_kind(n,"parenthesized_declarator")) closest=n;
    return ir_kind(closest,"function_declarator")?closest:NULL;
}
static ir_label *ir_find_label(ir_context *c,node_t *n) {
    for(ir_label *label=c->labels;label;label=label->next) if(strcmp(label->name,n->text)==0) return label;
    ir_label *label=ir_allocate(c,sizeof(*label)); label->name=n->text; label->node=n;
    label->block=ir_new_block(c); label->next=c->labels; c->labels=label; return label;
}
static void ir_cases(ir_context *c,ir_switch *sw,node_t *n) {
    if(ir_kind(n,"switch")) return;
    if(ir_kind(n,"case") || ir_kind(n,"default")) {
        ir_case *item=ir_allocate(c,sizeof(*item)); item->node=n; item->block=ir_new_block(c);
        item->next=sw->cases; sw->cases=item;
    }
    for(node_t *child=n->first;child;child=child->next) ir_cases(c,sw,child);
}
static void ir_assembly(ir_context *c,node_t *n) {
    node_t *text=NULL; int sideeffect=1; unsigned clobbers=0;
    for(node_t *group=n->first;group;group=group->next) {
        if(ir_kind(group,"asm_qualifier")) {
            if(strcmp(group->text,"goto")==0) ir_error(c,n,"asm goto is outside the C05 contract");
        } else if(ir_kind(group,"string_literal")) text=group;
        else if(ir_kind(group,"asm_inputs") || ir_kind(group,"asm_outputs")) {
            if(group->first) ir_error(c,n,"inline assembly operands are outside the C05 contract");
        } else if(ir_kind(group,"asm_clobbers")) {
            for(node_t *operand=group->first;operand;operand=operand->next) {
                node_t *str=operand->first; size_t size=0; unsigned char name[6];
                for(node_t *segment=str->first;segment;segment=segment->next) {
                    size_t length; unsigned char *bytes=badge_sema_literal(c->sema,segment,&length);
                    if(length>sizeof(name)-size) ir_error(c,n,"only memory and cc assembly clobbers are supported");
                    memcpy(name+size,bytes,length);
                    size+=length;
                }
                if(size==6 && memcmp(name,"memory",6)==0) clobbers|=1u;
                else if(size==2 && memcmp(name,"cc",2)==0) clobbers|=2u;
                else ir_error(c,n,"only memory and cc assembly clobbers are supported");
            }
        } else if(ir_kind(group,"asm_labels") && group->first) ir_error(c,n,"assembly labels unsupported");
    }
    if(text==NULL) ir_error(c,n,"assembly lacks a template");
    ir_buffer template={0}; ir_buffer *saved=c->buffer; c->buffer=&template;
    for(node_t *segment=text->first;segment;segment=segment->next) {
        size_t length; unsigned char *bytes=badge_sema_literal(c->sema,segment,&length);
        for(size_t k=0;k<length;++k) {
            if(c->function && bytes[k]=='$') ir_text(c,"$$");
            else ir_bytes(c,(const char *)&bytes[k],1);
        }
    }
    c->buffer=saved;
    ir_text(c,c->function?"  call void asm ":"module asm ");
    if(c->function && sideeffect) ir_text(c,"sideeffect ");
    ir_string(c,(const unsigned char *)template.data,template.size);
    if(c->function) {
        const char *clobber=clobbers==3?"~{memory},~{cc}":clobbers==2?"~{cc}":clobbers==1?"~{memory}":"";
        ir_text(c,", "); ir_string(c,(const unsigned char *)clobber,strlen(clobber)); ir_text(c,"()");
    }
    ir_text(c,"\n");
}
static void ir_statement(ir_context *c,node_t *n) {
    if(++c->depth>PARSE_MAX_DEPTH) ir_error(c,n,"IR statement depth exceeded");
    node_t *a=n->first,*b=a==NULL?NULL:a->next;
    if(ir_kind(n,"block")) { for(node_t *child=a;child;child=child->next) ir_statement(c,child); }
    else if(ir_kind(n,"static_assert") || ir_kind(n,"empty")) {}
    else if(ir_kind(n,"declaration")) {
        ir_live(c);
        for(node_t *item=a->next;item;item=item->next) {
            sem_info_t *i=ir_info(item); if(i==NULL || i->symbol==NULL || i->symbol->kind!=S_OBJECT || i->symbol->static_storage) continue;
            const char *place=ir_place_symbol(c,i->symbol);
            if(c->initializers[i->symbol->id]) ir_initialize(c,i->symbol->type,place,c->initializers[i->symbol->id]);
        }
    } else if(ir_kind(n,"expression_statement")) { if(a) { ir_live(c); (void)ir_expression(c,a); } }
    else if(ir_kind(n,"assembly")) { ir_live(c); ir_assembly(c,n); }
    else if(ir_kind(n,"return")) {
        ir_live(c); c_type_t *t=c->function->type->base;
        if(a) {
            ir_value value=ir_expression(c,a);
            if(ir_record(t)) {
                if(t->size>4) { ir_copy(c,"%result",value.text,t->size,(value.type->qualifiers&Q_VOLATILE)!=0); ir_text(c,"  ret void\n"); }
                else {
                    unsigned size=t->size==1?1:t->size==2?2:4; const char *place=ir_temp(c,t,size);
                    ir_zero(c,place,size,0); ir_copy(c,place,value.text,t->size,0);
                    const char *r=ir_reg(c); ir_emit(c,"  %s = load %s, ptr %s, align %u\n  ret %s %s\n",r,ir_small_return(t),place,t->alignment,ir_small_return(t),r);
                }
            } else if(t->kind==T_VOID) ir_text(c,"  ret void\n");
            else { value=ir_cast(c,value,t); ir_emit(c,"  ret %s %s\n",ir_type(c,t),value.text); }
        } else ir_text(c,"  ret void\n");
        c->terminated=1;
    } else if(ir_kind(n,"if")) {
        ir_live(c); const char *truth=ir_condition(c,a);
        unsigned yes=ir_new_block(c),no=ir_new_block(c),end=ir_new_block(c);
        ir_emit(c,"  br i1 %s, label %%b%u, label %%b%u\n",truth,yes,b->next?no:end); c->terminated=1;
        ir_block(c,yes); ir_statement(c,b); ir_branch(c,end);
        if(b->next) { ir_block(c,no); ir_statement(c,b->next); ir_branch(c,end); }
        ir_block(c,end);
    } else if(ir_kind(n,"while") || ir_kind(n,"do") || ir_kind(n,"for")) {
        ir_live(c); unsigned old_break=c->break_block,old_continue=c->continue_block;
        unsigned test=ir_new_block(c),body=ir_new_block(c),step=ir_new_block(c),end=ir_new_block(c);
        int do_loop=ir_kind(n,"do"),for_loop=ir_kind(n,"for");
        node_t *condition=do_loop?b:for_loop?b:a;
        node_t *loop_body=do_loop?a:for_loop?b->next->next:b;
        if(for_loop) {
            if(ir_kind(a,"declaration")) ir_statement(c,a); else if(!ir_kind(a,"empty")) (void)ir_expression(c,a);
        }
        c->break_block=end; c->continue_block=for_loop?step:test;
        ir_branch(c,do_loop?body:test); ir_block(c,test);
        if(ir_kind(condition,"empty")) ir_branch(c,body);
        else {
            const char *truth=ir_condition(c,condition);
            ir_emit(c,"  br i1 %s, label %%b%u, label %%b%u\n",truth,body,end); c->terminated=1;
        }
        ir_block(c,body); ir_statement(c,loop_body); ir_branch(c,for_loop?step:test);
        if(for_loop) {
            ir_block(c,step); if(!ir_kind(b->next,"empty")) (void)ir_expression(c,b->next); ir_branch(c,test);
        }
        ir_block(c,end); c->break_block=old_break; c->continue_block=old_continue;
    } else if(ir_kind(n,"break") || ir_kind(n,"continue")) {
        ir_live(c); ir_branch(c,ir_kind(n,"break")?c->break_block:c->continue_block);
    } else if(ir_kind(n,"goto")) { ir_live(c); ir_branch(c,ir_find_label(c,n)->block); }
    else if(ir_kind(n,"label")) { ir_label *label=ir_find_label(c,n); ir_branch(c,label->block); ir_block(c,label->block); ir_statement(c,a); }
    else if(ir_kind(n,"switch")) {
        ir_live(c); ir_switch sw={.parent=c->switches,.end=ir_new_block(c)}; ir_cases(c,&sw,b);
        c->switches=&sw; unsigned saved=c->break_block; c->break_block=sw.end;
        ir_value value=ir_expression(c,a); value=ir_cast(c,value,ir_promote(c,value.type));
        unsigned fallback=sw.end; for(ir_case *item=sw.cases;item;item=item->next) if(ir_kind(item->node,"default")) fallback=item->block;
        ir_emit(c,"  switch %s %s, label %%b%u [\n",ir_type(c,value.type),value.text,fallback);
        for(ir_case *item=sw.cases;item;item=item->next) if(ir_kind(item->node,"case")) {
            sem_info_t *constant=ir_info(item->node->first); uint64_t bits=constant->value;
            if(!constant->type->is_unsigned && (bits&(UINT64_C(1)<<(ir_bits(constant->type)-1)))) bits|=~ir_mask(constant->type);
            ir_emit(c,"    %s %" PRIu64 ", label %%b%u\n",ir_type(c,value.type),bits&ir_mask(value.type),item->block);
        }
        ir_text(c,"  ]\n"); c->terminated=1; ir_live(c); ir_statement(c,b); ir_branch(c,sw.end);
        ir_block(c,sw.end); c->break_block=saved; c->switches=sw.parent;
    } else if(ir_kind(n,"case") || ir_kind(n,"default")) {
        ir_case *item=c->switches->cases; while(item && item->node!=n) item=item->next;
        if(item==NULL) ir_error(c,n,"case lacks switch dispatch"); ir_branch(c,item->block); ir_block(c,item->block);
        ir_statement(c,ir_kind(n,"case")?b:a);
    } else ir_error(c,n,"unsupported IR statement");
    --c->depth;
}
static void ir_function_header(ir_context *c,symbol_t *s,int defined) {
    c_type_t *fn=s->type;
    ir_emit(c,"%s %s%s%s %s(",defined?"define":"declare",defined?(s->linkage==1?"internal ":"dso_local "):"",
        ir_record(fn->base)?"":ir_extension(fn->base),ir_return_type(c,fn),ir_symbol(c,s));
    ir_signature(c,fn,defined); ir_emit(c,") #%u",s->noreturn?1:0);
    if(s->section_name) { ir_text(c," section "); ir_string(c,(const unsigned char *)s->section_name,strlen(s->section_name)); }
}
static void ir_definition(ir_context *c,symbol_t *s,node_t *n) {
    c->function=s; c->reg=c->temp=c->label=c->depth=0; c->labels=NULL; c->switches=NULL;
    c->body=(ir_buffer){0}; c->entry=(ir_buffer){0}; c->break_block=c->continue_block=0;
    memset(c->allocated,0,badge_sema_symbol_count(c->sema)+1);
    c->buffer=&c->body; ir_block(c,0);
    node_t *fn=ir_function_declarator(n->first->next->first); size_t k=0;
    for(node_t *parameter=fn->first->next->first;parameter;parameter=parameter->next) {
        sem_info_t *i=ir_info(parameter); if(i==NULL || i->symbol==NULL) continue;
        symbol_t *v=i->symbol; const char *place;
        if(ir_byval(v->type)) {
            place=ir_format(c,"%%arg%zu",k); c->places[v->id]=place; c->allocated[v->id]=1;
        } else if(ir_record(v->type)) {
            unsigned word=ir_carrier_size(v->type),size=(v->type->size+word-1)/word*word;
            place=ir_temp(c,v->type,size); c->places[v->id]=place; c->allocated[v->id]=1;
            ir_emit(c,"  store %s %%arg%zu, ptr %s, align %u\n",ir_carrier(c,v->type),k,place,v->alignment);
        } else {
            place=ir_place_symbol(c,v); ir_store(c,v->type,place,(ir_value){v->type,ir_format(c,"%%arg%zu",k)});
        }
        ++k;
    }
    ir_statement(c,n->last);
    if(!c->terminated) {
        if(s->noreturn) ir_text(c,"  unreachable\n");
        else if(fn==NULL) ir_error(c,n,"function lacks a declarator");
        else if(s->type->base->kind==T_VOID || (ir_record(s->type->base) && s->type->base->size>4)) ir_text(c,"  ret void\n");
        else ir_emit(c,"  ret %s undef\n",ir_return_type(c,s->type));
    }
    c->buffer=NULL; ir_function_header(c,s,1); ir_text(c," {\nentry:\n");
    ir_bytes(c,c->entry.data,c->entry.size); ir_text(c,"  br label %b0\n"); ir_bytes(c,c->body.data,c->body.size); ir_text(c,"}\n");
    c->function=NULL;
}
static void ir_process_mode(parser_t *p,node_t *root,int ssa_expressions) {
    ir_context *c=parse_allocate(p,sizeof(*c)); c->p=p; c->sema=badge_sema_analyze(p,root);
    c->ssa_expressions=ssa_expressions;
    unsigned symbols=badge_sema_symbol_count(c->sema);
    c->initializers=ir_allocate(c,((size_t)symbols+1)*sizeof(*c->initializers));
    c->definitions=ir_allocate(c,((size_t)symbols+1)*sizeof(*c->definitions));
    c->places=ir_allocate(c,((size_t)symbols+1)*sizeof(*c->places));
    c->allocated=ir_allocate(c,(size_t)symbols+1); ir_collect(c,root);
    ir_text(c,"source_filename = "); ir_string(c,(const unsigned char *)p->file,strlen(p->file));
    ir_text(c,"\ntarget datalayout = " BADGE_IR_LAYOUT "\n"
        "target triple = " BADGE_IR_TRIPLE "\n");
    for(symbol_t *s=badge_sema_symbols(c->sema);s;s=s->next)
        if(s->kind==S_STRING || (s->kind==S_OBJECT && s->static_storage)) ir_global(c,s);
    for(node_t *n=root->first;n;n=n->next) if(ir_kind(n,"assembly")) ir_assembly(c,n);
    for(symbol_t *s=badge_sema_symbols(c->sema);s;s=s->next) if(s->kind==S_FUNCTION) {
        if(c->definitions[s->id]) ir_definition(c,s,c->definitions[s->id]);
        else { ir_function_header(c,s,0); ir_text(c,"\n"); }
    }
    unsigned used=0; for(symbol_t *s=badge_sema_symbols(c->sema);s;s=s->next) if(s->used) ++used;
    if(used) {
        ir_emit(c,"@llvm.compiler.used = appending global [%u x ptr] [",used); int comma=0;
        for(symbol_t *s=badge_sema_symbols(c->sema);s;s=s->next) if(s->used) {
            if(comma) ir_text(c,", "); comma=1; ir_emit(c,"ptr %s",ir_symbol(c,s));
        }
        ir_text(c,"], section \"llvm.metadata\"\n");
    }
    ir_text(c,"declare void @llvm.memcpy.inline.p0.p0.i32(ptr, ptr, i32 immarg, i1 immarg)\n"
        "declare void @llvm.memset.inline.p0.i32(ptr, i8, i32 immarg, i1 immarg)\n"
        "attributes #0 = { nounwind minsize optsize \"no-builtins\" \"target-cpu\"=\"cortex-m33\" \"use-soft-float\"=\"true\" }\n"
        "attributes #1 = { noreturn nounwind minsize optsize \"no-builtins\" \"target-cpu\"=\"cortex-m33\" \"use-soft-float\"=\"true\" }\n");
}
static void ir_process(parser_t *p,node_t *root) { ir_process_mode(p,root,0); }
static void ir_process_ssa(parser_t *p,node_t *root) { ir_process_mode(p,root,1); }
int main(int argc,char **argv) {
    const char *usage="usage: badge-ir [--expression-lowering=legacy|ssa] input.i -o output.ll";
    if(argc==2 && strcmp(argv[1],"--help")==0) { puts(usage); return 0; }
    if(argc==2 && strcmp(argv[1],"--default-expression-lowering")==0) { puts("ssa"); return 0; }
    int ssa=1;
    if(argc>1 && !strncmp(argv[1],"--expression-lowering=",22)) {
        if(strcmp(argv[1]+22,"legacy") && strcmp(argv[1]+22,"ssa")) {
            fputs("badge-ir: unsupported expression lowering\n",stderr); return 2;
        }
        ssa=!strcmp(argv[1]+22,"ssa");
        argc--; argv++;
    }
    if(argc!=4 || strcmp(argv[2],"-o")!=0 || argv[1][0]=='-' || argv[3][0]=='\0') {
        fprintf(stderr,"%s\n",usage); return 2;
    }
    return frontend_run(argc,argv,"badge-ir",ssa?ir_process_ssa:ir_process);
}
