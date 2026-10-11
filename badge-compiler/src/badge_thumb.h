#ifndef BADGE_COMPILER_THUMB_H
#define BADGE_COMPILER_THUMB_H
#include "thumb_immediate.h"

typedef struct { int base,index,shift,pre,writeback; int64_t offset; } thumb_memory_t;
static int thumb_data_register(unsigned r) { return r!=13 && r!=15; }

static int thumb_is_register(const char *p) {
    return (p[0]=='r' && isdigit((unsigned char)p[1])) || strcmp(p,"sp")==0 ||
        strcmp(p,"lr")==0 || strcmp(p,"pc")==0 || strcmp(p,"ip")==0;
}

static void thumb_count(assembler_t *s, as_line_t *l, unsigned minimum, unsigned maximum) {
    if(l->count<minimum || l->count>maximum) as_error(s,"invalid operands for '%s'",l->op);
}

static uint32_t thumb_immediate(assembler_t *s, const char *text) {
    int64_t value=as_absolute(s,text);
    if(value<INT32_MIN || value>UINT32_MAX) as_error(s,"immediate exceeds 32 bits"); return (uint32_t)value;
}

static unsigned thumb_emit(assembler_t *s, as_line_t *l, unsigned width, uint32_t a,uint32_t b) {
    if(l->width==4 && width==2) as_error(s,"instruction cannot satisfy selected wide encoding");
    if((l->encoding&4) && width!=2) as_error(s,"instruction does not fit explicit narrow encoding");
    as_word(s,a,2); if(width==4) as_word(s,b,2); return width;
}

static void thumb_shift(assembler_t *s, const char *text, unsigned *type, unsigned *amount) {
    const char *p=text;
    if(strncmp(p,"lsl",3)==0) *type=0; else if(strncmp(p,"lsr",3)==0) *type=1;
    else if(strncmp(p,"asr",3)==0) *type=2; else if(strncmp(p,"ror",3)==0) *type=3;
    else as_error(s,"unsupported shift");
    p+=3; *amount=as_bounded(s,p,32);
    if(*type==0 && *amount>31) as_error(s,"LSL shift outside range");
    if(*type!=0 && *amount==0) as_error(s,"non-LSL shift cannot be zero");
    if(*type==3 && *amount==32) as_error(s,"ROR shift outside range");
    *amount&=31;
}

static uint32_t thumb_shift_bits(unsigned type,unsigned amount) {
    return ((amount>>2)<<12)|((amount&3)<<6)|(type<<4);
}

static thumb_memory_t thumb_memory(assembler_t *s, const char *text, const char *post) {
    thumb_memory_t m={.index=-1,.pre=1};
    size_t n=strlen(text); char buffer[256];
    if(n<3 || n>=sizeof(buffer) || text[0]!='[') as_error(s,"invalid memory operand");
    if(text[n-1]=='!') { m.writeback=1; --n; }
    if(text[n-1]!=']') as_error(s,"invalid memory operand");
    memcpy(buffer,text+1,n-2); buffer[n-2]=0;
    char *args[16]; unsigned count=as_split(s,buffer,args);
    if(count<1 || count>3) as_error(s,"invalid memory operand");
    m.base=as_register(s,args[0]);
    if(count>=2) {
        if(thumb_is_register(args[1])) m.index=as_register(s,args[1]);
        else m.offset=as_absolute(s,args[1]);
    }
    if(count==3) {
        unsigned type,amount; thumb_shift(s,args[2],&type,&amount);
        if(m.index<0 || type!=0 || amount>3) as_error(s,"memory index supports LSL 0..3 only"); m.shift=(int)amount;
    }
    if(post!=NULL) {
        if(count!=1 || m.writeback) as_error(s,"invalid post-index operand");
        m.pre=0; m.writeback=1; m.offset=as_absolute(s,post);
    }
    return m;
}

static unsigned thumb_branch(assembler_t *s, as_line_t *l, const char *op, int cond,int wide) {
    int call=strcmp(op,"bl")==0;
    if(strcmp(op,"cbz")==0 || strcmp(op,"cbnz")==0) {
        thumb_count(s,l,2,2); int rn=as_register(s,l->arg[0]); as_value_t v=as_expression(s,l->arg[1]);
        if(rn>7 || s->it_count) as_error(s,"invalid CBZ/CBNZ register or IT placement");
        int64_t d=0;
        if(v.positive && !v.negative && !v.unknown) {
            as_symbol_t *target=&s->symbols[v.positive];
            if(target->out.section==s->current) {
                if(__builtin_add_overflow(v.value,(int64_t)target->raw_value-s->sections[s->current].out.size-4,&d))
                    as_error(s,"branch expression overflow");
            }
            else if(s->final) as_error(s,"CBZ/CBNZ requires same-section target");
        } else if(s->final) as_error(s,"invalid CBZ/CBNZ target");
        if((d<0 || d>126 || (d&1)) && s->final) as_error(s,"CBZ/CBNZ target outside forward 0..126 range");
        unsigned imm=(unsigned)d&127;
        return thumb_emit(s,l,2,0xb100|(strcmp(op,"cbnz")==0?0x800:0)|((imm>>6)<<9)|((imm>>1)<<3)|(unsigned)rn,0);
    }
    thumb_count(s,l,1,1);
    as_value_t v=as_expression(s,l->arg[0]); int64_t d=0; int external=0,known=0;
    if(v.positive && !v.negative && !v.unknown) {
        as_symbol_t *target=&s->symbols[v.positive];
        if(target->out.section==s->current) {
            if(__builtin_add_overflow(v.value,(int64_t)target->raw_value-s->sections[s->current].out.size-4,&d))
                as_error(s,"branch expression overflow");
            known=1;
        }
        else if(target->definition!=0 && target->out.section==0 && !s->final) { }
        else external=1;
    } else if(s->final) as_error(s,"branch requires a symbol plus an absolute addend");
    if(s->it_count) cond=14;
    if(call || external || (known && (d<(cond==14?-2048:-256) || d>(cond==14?2046:254)))) wide=1;
    if(s->final && (d&1)) as_error(s,"odd branch displacement");
    if(external) {
        if(cond!=14) as_error(s,"external conditional branch is unsupported");
        if(v.value<INT32_MIN+4 || v.value>INT32_MAX) as_error(s,"branch addend out of range");
        d=v.value-4; as_reloc(s,s->sections[s->current].out.size,call?10:30,v.positive);
    }
    if(!wide) return thumb_emit(s,l,2,cond==14?0xe000|((uint32_t)(d/2)&2047):
        0xd000|((unsigned)cond<<8)|((uint32_t)(d/2)&255),0);
    if(cond!=14) {
        if(d<-(INT64_C(1)<<20) || d>((INT64_C(1)<<20)-2)) as_error(s,"wide conditional branch out of range");
        uint32_t bits=(uint32_t)d;
        return thumb_emit(s,l,4,0xf000|((bits>>20&1)<<10)|((unsigned)cond<<6)|(bits>>12&63),
            0x8000|((bits>>18&1)<<13)|((bits>>19&1)<<11)|(bits>>1&2047));
    }
    if(d<-(INT64_C(1)<<24) || d>((INT64_C(1)<<24)-2)) as_error(s,"wide branch out of range");
    uint32_t bits=(uint32_t)d,sign=bits>>24&1,j1=(~((bits>>23&1)^sign))&1,j2=(~((bits>>22&1)^sign))&1;
    return thumb_emit(s,l,4,0xf000|(sign<<10)|(bits>>12&1023),
        (call?0xd000:0x9000)|(j1<<13)|(j2<<11)|(bits>>1&2047));
}

static unsigned thumb_loadstore(assembler_t *s, as_line_t *l, const char *op,int wide) {
    int load=strncmp(op,"ldr",3)==0,dual=strcmp(op,"ldrd")==0 || strcmp(op,"strd")==0;
    thumb_count(s,l,dual?3:2,dual?4:3);
    int rt=as_register(s,l->arg[0]), rt2=dual?as_register(s,l->arg[1]):-1;
    const char *memory=l->arg[dual?2:1];
    if(memory[0]!='[') {
        if(strcmp(op,"ldr")!=0 || l->count!=2) as_error(s,"unsupported literal load");
        as_value_t value=as_expression(s,memory); int64_t offset=0;
        if(value.positive && !value.negative && !value.unknown) {
            as_symbol_t *v=&s->symbols[value.positive];
            if(v->out.section==s->current) {
                if(__builtin_add_overflow(value.value,(int64_t)v->raw_value-((s->sections[s->current].out.size+4)&~3u),&offset))
                    as_error(s,"literal expression overflow");
            }
            else if(s->final) as_error(s,"literal must be in the same section");
        } else if(s->final) as_error(s,"invalid literal load expression");
        if(!wide && rt<8 && offset>=0 && offset<=1020 && offset%4==0)
            return thumb_emit(s,l,2,0x4800|((unsigned)rt<<8)|(unsigned)(offset/4),0);
        if(offset<-4095 || offset>4095) as_error(s,"literal load outside +/-4095 range");
        return thumb_emit(s,l,4,offset<0?0xf85f:0xf8df,((unsigned)rt<<12)|(unsigned)(offset<0?-offset:offset));
    }
    thumb_memory_t m=thumb_memory(s,memory,l->count==(unsigned)(dual?4:3)?l->arg[dual?3:2]:NULL);
    if(m.writeback && (m.base==rt || (dual && m.base==rt2))) as_error(s,"writeback overlaps transfer register");
    if(dual) {
        if(!thumb_data_register((unsigned)rt) || !thumb_data_register((unsigned)rt2) || (load && rt==rt2) || m.index>=0 || m.offset%4 || m.offset<-1020 || m.offset>1020)
            as_error(s,"invalid dual-register transfer");
        unsigned a=0xe840|(m.pre?0x100:0)|(m.offset>=0?0x80:0)|(m.writeback?0x20:0)|(load?0x10:0)|(unsigned)m.base;
        return thumb_emit(s,l,4,a,((unsigned)rt<<12)|((unsigned)rt2<<8)|(unsigned)((m.offset<0?-m.offset:m.offset)/4));
    }
    int byte=strchr(op,'b')!=NULL,half=strchr(op,'h')!=NULL,sign=strcmp(op,"ldrsb")==0 || strcmp(op,"ldrsh")==0;
    unsigned scale=byte?1:half?2:4;
    if(rt==15 && (!load || byte || half || m.writeback)) as_error(s,"invalid PC transfer");
    if(m.base==15) as_error(s,"use a symbolic literal for PC-relative loads");
    if(!wide && rt<8 && m.pre && !m.writeback) {
        if(m.index>=0 && m.index<8 && m.base<8 && m.shift==0) {
            unsigned base=sign?(byte?0x5600:0x5e00):load?(byte?0x5c00:half?0x5a00:0x5800):(byte?0x5400:half?0x5200:0x5000);
            return thumb_emit(s,l,2,base|((unsigned)m.index<<6)|((unsigned)m.base<<3)|(unsigned)rt,0);
        }
        if(m.index<0 && !sign && m.offset>=0 && m.offset%scale==0) {
            if(m.base==13 && scale==4 && m.offset<=1020)
                return thumb_emit(s,l,2,(load?0x9800:0x9000)|((unsigned)rt<<8)|(unsigned)(m.offset/4),0);
            if(m.base<8 && m.offset<(int64_t)scale*32) {
                unsigned base=load?(byte?0x7800:half?0x8800:0x6800):(byte?0x7000:half?0x8000:0x6000);
                return thumb_emit(s,l,2,base|((unsigned)(m.offset/scale)<<6)|((unsigned)m.base<<3)|(unsigned)rt,0);
            }
        }
    }
    unsigned base=sign?(byte?0xf990:0xf9b0):load?(byte?0xf890:half?0xf8b0:0xf8d0):(byte?0xf880:half?0xf8a0:0xf8c0);
    if(m.index>=0) {
        if(!thumb_data_register((unsigned)m.index) || m.writeback) as_error(s,"invalid indexed transfer");
        return thumb_emit(s,l,4,(base-0x80)|(unsigned)m.base,((unsigned)rt<<12)|((unsigned)m.shift<<4)|(unsigned)m.index);
    }
    if(m.pre && !m.writeback && m.offset>=0 && m.offset<=4095)
        return thumb_emit(s,l,4,base|(unsigned)m.base,((unsigned)rt<<12)|(unsigned)m.offset);
    if(m.offset<-255 || m.offset>255) as_error(s,"indexed offset outside +/-255 range");
    return thumb_emit(s,l,4,(base-0x80)|(unsigned)m.base,((unsigned)rt<<12)|0x800|
        (m.pre?0x400:0)|(m.offset>=0?0x200:0)|(m.writeback?0x100:0)|(unsigned)(m.offset<0?-m.offset:m.offset));
}

static unsigned thumb_alu(assembler_t *s, as_line_t *l, const char *op,int wide,int in_it) {
    size_t len=strlen(op); int flags=len>1 && op[len-1]=='s'; char base[16];
    if(len>=sizeof(base)) as_error(s,"invalid ALU mnemonic");
    memcpy(base,op,len+1); if(flags) base[len-1]=0;
    int compare=strcmp(base,"cmp")==0 || strcmp(base,"cmn")==0 || strcmp(base,"tst")==0;
    int unary=strcmp(base,"mov")==0 || strcmp(base,"mvn")==0;
    thumb_count(s,l,2,4);
    int rd=compare?15:as_register(s,l->arg[0]), rn;
    const char *operand,*shift=NULL;
    if(compare) { rn=as_register(s,l->arg[0]); operand=l->arg[1]; if(l->count>2) shift=l->arg[2]; if(l->count>3) as_error(s,"too many compare operands"); flags=1; }
    else if(unary) { rn=15; operand=l->arg[1]; if(l->count>2) shift=l->arg[2]; if(l->count>3) as_error(s,"too many unary operands"); }
    else if(l->count==2) { rn=rd; operand=l->arg[1]; }
    else { rn=as_register(s,l->arg[1]); operand=l->arg[2]; if(l->count==4) shift=l->arg[3]; }
    int immediate=!thumb_is_register(operand); uint32_t imm=0; int rm=-1;
    unsigned shift_type=0,amount=0;
    if(immediate) { if(shift) as_error(s,"shift on immediate"); imm=thumb_immediate(s,operand); }
    else { rm=as_register(s,operand); if(shift) thumb_shift(s,shift,&shift_type,&amount); }
    int short_flags=flags?!in_it:in_it;
    if(!wide) {
        if(strcmp(base,"mov")==0) {
            if(immediate && rd<8 && imm<=255 && short_flags) return thumb_emit(s,l,2,0x2000|((unsigned)rd<<8)|imm,0);
            if(!immediate && !shift && !flags) return thumb_emit(s,l,2,0x4600|((unsigned)rd&8)<<4|((unsigned)rm<<3)|((unsigned)rd&7),0);
            if(!immediate && !shift && flags && !in_it && rd<8 && rm<8) return thumb_emit(s,l,2,((unsigned)rm<<3)|(unsigned)rd,0);
        } else if(strcmp(base,"cmp")==0 && !shift) {
            if(immediate && rn<8 && imm<=255) return thumb_emit(s,l,2,0x2800|((unsigned)rn<<8)|imm,0);
            if(!immediate) {
                if(rn<8 && rm<8) return thumb_emit(s,l,2,0x4280|((unsigned)rm<<3)|(unsigned)rn,0);
                return thumb_emit(s,l,2,0x4500|((unsigned)rn&8)<<4|((unsigned)rm<<3)|((unsigned)rn&7),0);
            }
        } else if((strcmp(base,"add")==0 || strcmp(base,"sub")==0) && !shift) {
            int sub=strcmp(base,"sub")==0;
            if(immediate && rd==13 && rn==13 && !flags && imm<=508 && imm%4==0)
                return thumb_emit(s,l,2,0xb000|(sub?0x80:0)|(imm/4),0);
            if(immediate && !sub && rn==13 && rd<8 && !flags && imm<=1020 && imm%4==0)
                return thumb_emit(s,l,2,0xa800|((unsigned)rd<<8)|(imm/4),0);
            if(!immediate && !sub && rd==rn && !flags)
                return thumb_emit(s,l,2,0x4400|((unsigned)rd&8)<<4|((unsigned)rm<<3)|((unsigned)rd&7),0);
            if(rd<8 && rn<8 && short_flags) {
                if(immediate && l->count==2 && imm<=255) return thumb_emit(s,l,2,(sub?0x3800:0x3000)|((unsigned)rd<<8)|imm,0);
                if(immediate && imm<=7) return thumb_emit(s,l,2,(sub?0x1e00:0x1c00)|(imm<<6)|((unsigned)rn<<3)|(unsigned)rd,0);
                if(immediate && rd==rn && imm<=255) return thumb_emit(s,l,2,(sub?0x3800:0x3000)|((unsigned)rd<<8)|imm,0);
                if(!immediate && rm<8) return thumb_emit(s,l,2,(sub?0x1a00:0x1800)|((unsigned)rm<<6)|((unsigned)rn<<3)|(unsigned)rd,0);
            }
        } else if(strcmp(base,"rsb")==0 && immediate && imm==0 && short_flags && rd<8 && rn<8)
            return thumb_emit(s,l,2,0x4240|((unsigned)rn<<3)|(unsigned)rd,0);
        else if(!immediate && !shift && rm<8 && (compare?rn<8:rd<8) && (compare || short_flags)) {
            unsigned code=0;
            if(strcmp(base,"and")==0) code=0x4000; else if(strcmp(base,"eor")==0) code=0x4040;
            else if(strcmp(base,"adc")==0) code=0x4140; else if(strcmp(base,"sbc")==0) code=0x4180;
            else if(strcmp(base,"tst")==0) code=0x4200; else if(strcmp(base,"cmn")==0) code=0x42c0;
            else if(strcmp(base,"orr")==0) code=0x4300; else if(strcmp(base,"bic")==0) code=0x4380;
            else if(strcmp(base,"mvn")==0) code=0x43c0;
            if(code && (compare || unary || rd==rn)) return thumb_emit(s,l,2,code|((unsigned)rm<<3)|(unsigned)(compare?rn:rd),0);
        }
    }
    unsigned opcode;
    if(strcmp(base,"and")==0 || strcmp(base,"tst")==0) opcode=0;
    else if(strcmp(base,"bic")==0) opcode=1;
    else if(strcmp(base,"orr")==0 || strcmp(base,"mov")==0) opcode=2;
    else if(strcmp(base,"orn")==0 || strcmp(base,"mvn")==0) opcode=3;
    else if(strcmp(base,"eor")==0) opcode=4;
    else if(strcmp(base,"add")==0 || strcmp(base,"cmn")==0) opcode=8;
    else if(strcmp(base,"adc")==0) opcode=10;
    else if(strcmp(base,"sbc")==0) opcode=11;
    else if(strcmp(base,"sub")==0 || strcmp(base,"cmp")==0) opcode=13;
    else if(strcmp(base,"rsb")==0) opcode=14;
    else as_error(s,"unsupported ALU instruction");
    if(rd==13 || (!compare && rd==15) || (!unary && rn==15)) {
        if(!(strcmp(base,"add")==0 || strcmp(base,"sub")==0) || rd==15 || rn==15)
            as_error(s,"invalid SP/PC ALU register");
    }
    if(immediate) {
        int encoded=thumb_modified(imm);
        if(encoded<0) {
            if(strcmp(base,"mov")==0 && !flags && imm<=65535) {
                return thumb_emit(s,l,4,0xf240|(imm>>12)|((imm>>11&1)<<10),(imm>>8&7)<<12|((unsigned)rd<<8)|(imm&255));
            }
            as_error(s,"immediate cannot be encoded as Thumb modified immediate");
        }
        unsigned value=(unsigned)encoded;
        return thumb_emit(s,l,4,0xf000|(opcode<<5)|((unsigned)flags<<4)|(unsigned)rn|((value>>11)<<10),
            ((value>>8&7)<<12)|((unsigned)rd<<8)|(value&255));
    }
    if(rm==13 || rm==15) as_error(s,"invalid shifted register");
    return thumb_emit(s,l,4,0xea00|(opcode<<5)|((unsigned)flags<<4)|(unsigned)rn,
        ((unsigned)rd<<8)|thumb_shift_bits(shift_type,amount)|(unsigned)rm);
}

static unsigned as_instruction(assembler_t *s, as_line_t *l) {
    static const char *const bases[]={
        "adc","adcs","add","adds","addw","adr","and","ands","asr","asrs","b","bfc","bfi","bic","bics","bl","blx","bx","cbnz","cbz","clz","cmn","cmp","dmb","dsb","eor","eors","isb",
        "ldm","ldr","ldrb","ldrd","ldrh","ldrsb","ldrsh","lsl","lsls","lsr","lsrs","mla","mls","mov","movs","movt","movw","mul","muls","mvn","mvns","nop","orn","orr","orrs",
        "pop","push","rev","rev16","ror","rors","rsb","rsbs","sbc","sbcs","sdiv","sev","smlabb","smlal","smull","stm","str","strb","strd","strh","sub","subs","subw",
        "sxtb","sxth","tbb","tbh","tst","ubfx","udiv","umull","uxtah","uxtb","uxth","wfe","wfi","yield"
    };
    if(!l->encoding) {
        char mnemonic[32]; size_t len=strlen(l->op);
        if(len>=sizeof(mnemonic)) as_error(s,"instruction name too long");
        memcpy(mnemonic,l->op,len+1); l->encoding=1; l->condition=14;
        if(len>2 && (strcmp(mnemonic+len-2,".w")==0 || strcmp(mnemonic+len-2,".n")==0)) {
            l->encoding|=mnemonic[len-1]=='w'?2:4; mnemonic[len-2]=0; len-=2;
        }
        if(strncmp(mnemonic,"it",2)==0) {
            l->encoding|=8; l->mnemonic=as_copy(s,mnemonic);
        } else {
            for(size_t k=0;k<sizeof(bases)/sizeof(*bases);++k)
                if(strcmp(mnemonic,bases[k])==0) { l->mnemonic=bases[k]; break; }
            if(l->mnemonic==NULL && len>2) {
                l->condition=as_condition(mnemonic+len-2);
                if(l->condition>=0) {
                    mnemonic[len-2]=0;
                    for(size_t k=0;k<sizeof(bases)/sizeof(*bases);++k)
                        if(strcmp(mnemonic,bases[k])==0) { l->mnemonic=bases[k]; break; }
                }
            }
            if(l->mnemonic==NULL) as_error(s,"unsupported instruction '%s'",l->op);
        }
    }
    int wide=l->width==4 || (l->encoding&2);
    if(l->encoding&8) {
        thumb_count(s,l,1,1); unsigned count=(unsigned)strlen(l->mnemonic)-1;
        int cond=as_condition(l->arg[0]);
        if(count<1 || count>4 || cond<0 || cond>=14 || s->it_count || wide) as_error(s,"invalid IT instruction");
        unsigned mask=1u<<(4-count); s->it_at=0; s->it_count=count; s->it_conditions[0]=(unsigned)cond;
        for(unsigned k=1;k<count;++k) {
            char c=l->mnemonic[k+1]; if(c!='t' && c!='e') as_error(s,"invalid IT mask");
            unsigned next=(unsigned)cond^(c=='e'); s->it_conditions[k]=next; mask|=(next&1)<<(4-k);
        }
        return thumb_emit(s,l,2,0xbf00|((unsigned)cond<<4)|mask,0);
    }
    const char *op=l->mnemonic; int cond=l->condition;
    int in_it=s->it_count!=0;
    if(in_it) {
        if(cond!=(int)s->it_conditions[s->it_at]) as_error(s,"instruction condition does not match IT slot");
    } else if(cond!=14 && strcmp(op,"b")!=0) as_error(s,"conditional instruction outside IT");
    unsigned width=0;
    if(strcmp(op,"b")==0 || strcmp(op,"bl")==0 || strcmp(op,"cbz")==0 || strcmp(op,"cbnz")==0)
        width=thumb_branch(s,l,op,cond,wide);
    else if(strcmp(op,"bx")==0 || strcmp(op,"blx")==0) {
        thumb_count(s,l,1,1); int r=as_register(s,l->arg[0]); if(r==15 || wide) as_error(s,"invalid BX/BLX register or width");
        width=thumb_emit(s,l,2,(strcmp(op,"bx")==0?0x4700:0x4780)|((unsigned)r<<3),0);
    } else if(strncmp(op,"ldr",3)==0 || strncmp(op,"str",3)==0) width=thumb_loadstore(s,l,op,wide);
    else if(strcmp(op,"push")==0 || strcmp(op,"pop")==0 || strcmp(op,"ldm")==0 || strcmp(op,"stm")==0) {
        int pop=strcmp(op,"pop")==0,push=strcmp(op,"push")==0,load=pop || strcmp(op,"ldm")==0;
        thumb_count(s,l,pop||push?1:2,pop||push?1:2);
        uint32_t mask=as_register_list(s,l->arg[pop||push?0:1]); int rn=13,wb=1;
        if(!pop && !push) {
            char reg[32]; size_t n=strlen(l->arg[0]);
            if(n>=sizeof(reg)) as_error(s,"invalid base register"); memcpy(reg,l->arg[0],n+1);
            wb=n!=0 && reg[n-1]=='!'; if(wb) reg[n-1]=0; rn=as_register(s,reg);
        }
        if(mask&(1u<<13) || (push && mask&(1u<<15)) || (load && (mask&(1u<<14)) && (mask&(1u<<15))) || rn==15)
            as_error(s,"invalid multiple-transfer register list");
        if(!wide && (pop || push) && (mask&~(255u|(1u<<(pop?15:14))))==0) {
            width=thumb_emit(s,l,2,(pop?0xbc00:0xb400)|(mask&255)|((mask>>(pop?15:14)&1)<<8),0);
        } else if(!wide && rn<8 && mask<256 && ((!load && wb) || (load && wb==((mask&(1u<<rn))==0))))
            width=thumb_emit(s,l,2,(load?0xc800:0xc000)|((unsigned)rn<<8)|mask,0);
        else {
            if((mask&(mask-1))==0) as_error(s,"single-register wide multiple transfers are unsupported");
            if(wb && mask&(1u<<rn)) as_error(s,"writeback base overlaps register list");
            width=thumb_emit(s,l,4,push?0xe92d:pop?0xe8bd:(load?0xe890:0xe880)|(wb?32:0)|(unsigned)rn,mask);
        }
    } else if(strncmp(op,"lsl",3)==0 || strncmp(op,"lsr",3)==0 || strncmp(op,"asr",3)==0 || strncmp(op,"ror",3)==0) {
        thumb_count(s,l,2,3); unsigned type=op[0]=='a'?2:op[0]=='r'?3:op[2]=='r'?1:0;
        int flags=op[strlen(op)-1]=='s',rd=as_register(s,l->arg[0]),rn;
        const char *amount; if(l->count==2) { rn=rd; amount=l->arg[1]; } else { rn=as_register(s,l->arg[1]); amount=l->arg[2]; }
        if(thumb_is_register(amount)) {
            int rm=as_register(s,amount);
            if(!wide && rd<8 && rm<8 && rn==rd && (flags?!in_it:in_it))
                width=thumb_emit(s,l,2,(type==0?0x4080:type==1?0x40c0:type==2?0x4100:0x41c0)|((unsigned)rm<<3)|(unsigned)rd,0);
            else if(thumb_data_register((unsigned)rd) && thumb_data_register((unsigned)rn) && thumb_data_register((unsigned)rm))
                width=thumb_emit(s,l,4,0xfa00|(type<<5)|((unsigned)flags<<4)|(unsigned)rn,0xf000|((unsigned)rd<<8)|(unsigned)rm);
            else as_error(s,"invalid register shift");
        } else {
            unsigned shift=as_bounded(s,amount,32);
            if((type==0 && shift>31) || (type!=0 && shift==0) || (type==3 && shift==32)) as_error(s,"invalid shift amount");
            shift&=31;
            if(!wide && type<3 && rd<8 && rn<8 && (flags?!in_it:in_it))
                width=thumb_emit(s,l,2,(type<<11)|(shift<<6)|((unsigned)rn<<3)|(unsigned)rd,0);
            else if(thumb_data_register((unsigned)rd) && thumb_data_register((unsigned)rn)) width=thumb_emit(s,l,4,flags?0xea5f:0xea4f,((unsigned)rd<<8)|thumb_shift_bits(type,shift)|(unsigned)rn);
            else as_error(s,"invalid immediate shift");
        }
    } else if(strcmp(op,"adr")==0) {
        thumb_count(s,l,2,2); int rd=as_register(s,l->arg[0]); as_value_t value=as_expression(s,l->arg[1]);
        if(!thumb_data_register((unsigned)rd)) as_error(s,"invalid ADR destination");
        int64_t offset=0;
        if(value.positive && !value.negative && !value.unknown) {
            as_symbol_t *target=&s->symbols[value.positive];
            if(target->out.section==s->current) {
                if(__builtin_add_overflow(value.value,(int64_t)target->raw_value+target->thumb-((s->sections[s->current].out.size+4)&~3u),&offset))
                    as_error(s,"ADR expression overflow");
            } else if(s->final) as_error(s,"ADR requires a same-section target");
        } else if(s->final) as_error(s,"ADR requires a symbol plus an absolute addend");
        if(rd>7 || offset<0 || offset>1020 || (offset&3)) wide=1;
        if(offset < -4095 || offset > 4095) {
            if(s->final) as_error(s,"ADR target exceeds wide immediate range"); offset=0;
        }
        if(!wide) width=thumb_emit(s,l,2,0xa000|((unsigned)rd<<8)|((unsigned)offset>>2),0);
        else {
            unsigned imm=(unsigned)(offset<0?-offset:offset);
            width=thumb_emit(s,l,4,(offset<0?0xf2af:0xf20f)|((imm>>11&1)<<10),
                ((imm>>8&7)<<12)|((unsigned)rd<<8)|(imm&255));
        }
    } else if(strcmp(op,"movw")==0 || strcmp(op,"movt")==0 || strcmp(op,"addw")==0 || strcmp(op,"subw")==0) {
        int mov=strncmp(op,"mov",3)==0; thumb_count(s,l,mov?2:3,mov?2:3);
        int rd=as_register(s,l->arg[0]),rn=mov?0:as_register(s,l->arg[1]);
        const char *operand=l->arg[mov?1:2];
        unsigned imm;
        if(mov && (!strncmp(operand,"#:lower16:",10) || !strncmp(operand,"#:upper16:",10))) {
            int upper=!strncmp(operand,"#:upper16:",10);
            if(upper!=(strcmp(op,"movt")==0)) as_error(s,"MOVW/MOVT relocation modifier does not match opcode");
            as_value_t value=as_expression(s,operand+10);
            if(value.negative || value.unknown) as_error(s,"invalid MOVW/MOVT relocation expression");
            if(value.positive) {
                if(value.value < -32768 || value.value > 32767)
                    as_error(s,"MOVW/MOVT REL addend outside signed 16-bit range");
                imm=(uint32_t)value.value&65535;
                as_reloc(s,s->sections[s->current].out.size,upper?48:47,value.positive);
            } else {
                if(value.value<INT32_MIN || value.value>UINT32_MAX)
                    as_error(s,"MOVW/MOVT absolute value outside 32-bit range");
                imm=((uint32_t)value.value>>(upper?16:0))&65535;
            }
        } else imm=as_bounded(s,operand,mov?65535:4095);
        if(!thumb_data_register((unsigned)rd) || rn==15) as_error(s,"invalid plain-immediate register");
        unsigned first=mov?(strcmp(op,"movt")==0?0xf2c0:0xf240):(strcmp(op,"subw")==0?0xf2a0:0xf200);
        width=thumb_emit(s,l,4,first|((imm>>11&1)<<10)|(mov?imm>>12:(unsigned)rn),
            ((imm>>8&7)<<12)|((unsigned)rd<<8)|(imm&255));
    } else if(strcmp(op,"mul")==0 || strcmp(op,"muls")==0 || strcmp(op,"mla")==0 || strcmp(op,"mls")==0 ||
        strcmp(op,"smlabb")==0 || strcmp(op,"smull")==0 || strcmp(op,"umull")==0 || strcmp(op,"smlal")==0 || strcmp(op,"sdiv")==0 || strcmp(op,"udiv")==0) {
        int basic=strcmp(op,"mul")==0 || strcmp(op,"muls")==0,divide=strstr(op,"div")!=NULL;
        thumb_count(s,l,basic||divide?3:4,basic||divide?3:4);
        unsigned r[4]={0}; for(unsigned k=0;k<l->count;++k) { r[k]=(unsigned)as_register(s,l->arg[k]); if(!thumb_data_register(r[k])) as_error(s,"invalid multiply/divide register"); }
        if(basic && !wide && ((strcmp(op,"muls")==0 && !in_it) || (strcmp(op,"mul")==0 && in_it)) && r[0]<8 && r[1]<8 && r[2]<8 && (r[0]==r[1] || r[0]==r[2])) {
            unsigned other=r[0]==r[1]?r[2]:r[1]; width=thumb_emit(s,l,2,0x4340|(other<<3)|r[0],0);
        } else if(strcmp(op,"muls")==0) as_error(s,"MULS has no wide encoding");
        else if(strcmp(op,"smull")==0 || strcmp(op,"umull")==0 || strcmp(op,"smlal")==0) {
            if(r[0]==r[1]) as_error(s,"overlapping long multiply outputs");
            width=thumb_emit(s,l,4,(strcmp(op,"smlal")==0?0xfbc0:strcmp(op,"umull")==0?0xfba0:0xfb80)|r[2],(r[0]<<12)|(r[1]<<8)|r[3]);
        } else if(divide) width=thumb_emit(s,l,4,(strcmp(op,"udiv")==0?0xfbb0:0xfb90)|r[1],0xf0f0|(r[0]<<8)|r[2]);
        else width=thumb_emit(s,l,4,(strcmp(op,"smlabb")==0?0xfb10:0xfb00)|r[1],
            ((basic?15:r[3])<<12)|(r[0]<<8)|(strcmp(op,"mls")==0?16:0)|r[2]);
    } else if(strcmp(op,"bfc")==0 || strcmp(op,"bfi")==0 || strcmp(op,"ubfx")==0) {
        int clear=strcmp(op,"bfc")==0; thumb_count(s,l,clear?3:4,clear?3:4);
        unsigned rd=(unsigned)as_register(s,l->arg[0]),rn=clear?15:(unsigned)as_register(s,l->arg[1]);
        unsigned bit=as_bounded(s,l->arg[clear?1:2],31),bits=as_bounded(s,l->arg[clear?2:3],32);
        if(!thumb_data_register(rd) || (!clear && !thumb_data_register(rn)) || bits==0 || bit+bits>32) as_error(s,"invalid bitfield operands");
        width=thumb_emit(s,l,4,(strcmp(op,"ubfx")==0?0xf3c0:0xf360)|rn,
            ((bit>>2)<<12)|(rd<<8)|((bit&3)<<6)|(strcmp(op,"ubfx")==0?bits-1:bit+bits-1));
    } else if(strcmp(op,"tbb")==0 || strcmp(op,"tbh")==0) {
        thumb_count(s,l,1,1); thumb_memory_t m=thumb_memory(s,l->arg[0],NULL);
        int half=strcmp(op,"tbh")==0;
        if(m.index<0 || !thumb_data_register((unsigned)m.index) || m.writeback || m.shift!=(half?1:0)) as_error(s,"invalid table branch");
        width=thumb_emit(s,l,4,0xe8d0|(unsigned)m.base,0xf000|(half?16:0)|(unsigned)m.index);
    } else if(strcmp(op,"clz")==0 || strcmp(op,"rev")==0 || strcmp(op,"rev16")==0 ||
        strcmp(op,"uxtb")==0 || strcmp(op,"uxth")==0 || strcmp(op,"sxtb")==0 || strcmp(op,"sxth")==0 || strcmp(op,"uxtah")==0) {
        int add=strcmp(op,"uxtah")==0; thumb_count(s,l,add?3:2,add?3:2);
        unsigned rd=(unsigned)as_register(s,l->arg[0]),rm=(unsigned)as_register(s,l->arg[add?2:1]);
        if(!thumb_data_register(rd) || !thumb_data_register(rm)) as_error(s,"invalid extend/reverse register");
        if(!wide && !add && strcmp(op,"clz")!=0 && rd<8 && rm<8) {
            unsigned base=strcmp(op,"rev")==0?0xba00:strcmp(op,"rev16")==0?0xba40:
                strcmp(op,"uxtb")==0?0xb2c0:strcmp(op,"uxth")==0?0xb280:strcmp(op,"sxtb")==0?0xb240:0xb200;
            width=thumb_emit(s,l,2,base|(rm<<3)|rd,0);
        } else {
            unsigned a,b=0xf080|(rd<<8)|rm;
            if(strcmp(op,"clz")==0) a=0xfab0|rm;
            else if(strcmp(op,"rev")==0 || strcmp(op,"rev16")==0) { a=0xfa90|rm; if(strcmp(op,"rev16")==0) b|=16; }
            else if(add) { unsigned rn=(unsigned)as_register(s,l->arg[1]); if(!thumb_data_register(rn)) as_error(s,"invalid extend-add register"); a=0xfa10|rn; }
            else a=strcmp(op,"uxtb")==0?0xfa5f:strcmp(op,"uxth")==0?0xfa1f:strcmp(op,"sxtb")==0?0xfa4f:0xfa0f;
            width=thumb_emit(s,l,4,a,b);
        }
    } else if(strcmp(op,"dsb")==0 || strcmp(op,"dmb")==0 || strcmp(op,"isb")==0) {
        thumb_count(s,l,0,1); if(l->count && strcmp(l->arg[0],"sy")!=0) as_error(s,"only SY barriers are supported");
        width=thumb_emit(s,l,4,0xf3bf,strcmp(op,"dsb")==0?0x8f4f:strcmp(op,"dmb")==0?0x8f5f:0x8f6f);
    } else if(strcmp(op,"nop")==0 || strcmp(op,"wfi")==0 || strcmp(op,"wfe")==0 || strcmp(op,"sev")==0 || strcmp(op,"yield")==0) {
        thumb_count(s,l,0,0); unsigned hint=strcmp(op,"yield")==0?1:strcmp(op,"wfe")==0?2:strcmp(op,"wfi")==0?3:strcmp(op,"sev")==0?4:0;
        width=wide?thumb_emit(s,l,4,0xf3af,0x8000|hint):thumb_emit(s,l,2,0xbf00|(hint<<4),0);
    } else width=thumb_alu(s,l,op,wide,in_it);
    if(in_it) {
        int pc_write=strcmp(op,"b")==0 || strcmp(op,"bl")==0 || strcmp(op,"bx")==0 ||
            strcmp(op,"blx")==0 || strcmp(op,"tbb")==0 || strcmp(op,"tbh")==0;
        if((strcmp(op,"pop")==0 || strcmp(op,"ldm")==0) &&
            (as_register_list(s,l->arg[strcmp(op,"pop")==0?0:1])&(1u<<15))) pc_write=1;
        if(l->count && strcmp(l->arg[0],"pc")==0 && strcmp(op,"cmp")!=0 && strcmp(op,"tst")!=0) pc_write=1;
        if(pc_write && s->it_count!=1)
            as_error(s,"branch must be the last IT instruction");
        ++s->it_at; --s->it_count;
    }
    if((l->encoding&4) && width!=2)
        as_error(s,"instruction cannot use the requested narrow encoding");
    return width;
}

#endif
