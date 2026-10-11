#define _DARWIN_C_SOURCE
#define main badge_link_main
#include "badge_link.c"
#undef main
#include <assert.h>

static unsigned char entry_bytes[]={0xff,0xf7,0xfe,0xff,0xff,0xf7,0xfe,0xbf,0x70,0x47,0,0xbf,0,0,0,0};
static unsigned char callee_bytes[]={7,0x20,0x70,0x47};
static unsigned char data_bytes[20];
static bo_section_t sections[5];
static bo_symbol_t symbols[7];
static bo_relocation_t relocations[7];
static bo_object_t object;

static void fixture(void) {
    memset(data_bytes,0,sizeof(data_bytes));
    sections[0]=(bo_section_t){".text.entry",BO_PROGBITS,6,4,0,0,sizeof(entry_bytes),entry_bytes};
    sections[1]=(bo_section_t){".text.callee",BO_PROGBITS,6,4,0,0,sizeof(callee_bytes),callee_bytes};
    sections[2]=(bo_section_t){".data.refs",BO_PROGBITS,3,8,0,0,sizeof(data_bytes),data_bytes};
    sections[3]=(bo_section_t){".bss.zero",BO_NOBITS,3,8,0,0,16,NULL};
    sections[4]=(bo_section_t){".text.dead",BO_PROGBITS,6,2,0,0,4,callee_bytes};
    symbols[0]=(bo_symbol_t){"entry",1,2,0,1,1,sizeof(entry_bytes)};
    symbols[1]=(bo_symbol_t){"callee",1,2,2,2,1,sizeof(callee_bytes)};
    symbols[2]=(bo_symbol_t){"refs",1,1,0,3,0,sizeof(data_bytes)};
    symbols[3]=(bo_symbol_t){"weak_missing",2,0,0,0,0,0};
    symbols[4]=(bo_symbol_t){"common",1,1,0,BO_COMMON,8,8};
    symbols[5]=(bo_symbol_t){"dead",1,2,0,5,1,4};
    symbols[6]=(bo_symbol_t){"source.c",0,4,0,BO_ABS,0,0};
    relocations[0]=(bo_relocation_t){1,0,10,2};
    relocations[1]=(bo_relocation_t){1,4,30,2};
    relocations[2]=(bo_relocation_t){1,12,2,3};
    relocations[3]=(bo_relocation_t){3,0,2,2};
    relocations[4]=(bo_relocation_t){3,4,3,2};
    relocations[5]=(bo_relocation_t){3,8,2,4};
    relocations[6]=(bo_relocation_t){3,12,2,5};
    object=(bo_object_t){sections,5,symbols,7,relocations,7};
}
static void save(const char *path,const void *data,size_t size) {
    FILE *f=fopen(path,"wb"); assert(f!=NULL); assert(fwrite(data,1,size,f)==size); assert(fclose(f)==0);
}
static void write_object(const char *path) {
    unsigned char *bytes=NULL; size_t size=0; assert(badge_object_write(&object,&bytes,&size));
    save(path,bytes,size); free(bytes);
}
static void preserved(const char *path) {
    FILE *f=fopen(path,"rb"); char text[16]; assert(f!=NULL);
    assert(fread(text,1,sizeof(text),f)==9 && memcmp(text,"preserved",9)==0); assert(fclose(f)==0);
}
static unsigned char *read_file(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb"); assert(f!=NULL); assert(fseek(f,0,SEEK_END)==0);
    long n=ftell(f); assert(n>=0); *size=(size_t)n; rewind(f);
    unsigned char *p=malloc(*size+1); assert(p!=NULL); assert(fread(p,1,*size,f)==*size); assert(fclose(f)==0); return p;
}
static const unsigned char *section(const unsigned char *elf,const char *name) {
    const unsigned char *table=elf+lk_u32(elf+32);
    const char *names=(const char *)elf+lk_u32(table+lk_u16(elf+50)*40+16);
    for(unsigned k=1;k<lk_u16(elf+48);++k)
        if(strcmp(names+lk_u32(table+k*40),name)==0) return table+k*40;
    assert(!"missing section"); return NULL;
}
static void branch_threshold(int64_t displacement,int valid) {
    linker_t *l=calloc(1,sizeof(*l)); assert(l!=NULL);
    if(setjmp(l->failure)==0) {
        unsigned char p[]={0xff,0xf7,0xfe,0xff};
        lk_fixup(l,p,10,(uint32_t)(0x20000000+displacement+4),0x20000000,0);
        assert(valid);
        unsigned h=lk_u16(p),low=lk_u16(p+2),s=(h>>10)&1;
        unsigned i1=(~((low>>13)^s))&1,i2=(~((low>>11)^s))&1;
        assert(lk_sign((s<<24)|(i1<<23)|(i2<<22)|((h&1023)<<12)|((low&2047)<<1),25)==displacement);
    } else assert(!valid);
    lk_destroy(l);
}
static void mov_address_fixups(void) {
    linker_t *l=calloc(1,sizeof(*l)); assert(l!=NULL);
    assert(setjmp(l->failure)==0);
    const uint32_t values[]={0,0x10000001,0x20012345,0xfffffff0};
    for(unsigned addend=0;addend<65536;++addend)
        for(unsigned v=0;v<4;++v)
            for(unsigned upper=0;upper<2;++upper) {
                unsigned char p[4];
                unsigned reg=addend%13,opcode=upper?0xf2c0:0xf240;
                lk_put16(p,opcode|((addend>>11&1)<<10)|(addend>>12));
                lk_put16(p+2,((addend>>8&7)<<12)|(reg<<8)|(addend&255));
                lk_mov_fixup(l,p,upper?48:47,values[v]);
                uint32_t full=values[v]+addend-(addend>=32768?65536u:0u);
                unsigned expected=upper?full>>16:full&65535;
                unsigned h=lk_u16(p),low=lk_u16(p+2);
                unsigned actual=((h&15)<<12)|((h>>10&1)<<11)|((low>>12&7)<<8)|(low&255);
                assert(actual==expected && (h&0xfbf0)==opcode && ((low>>8)&15)==reg);
            }
    for(unsigned bad=0;bad<4;++bad) {
        if(setjmp(l->failure)==0) {
            unsigned char p[4];
            lk_put16(p,bad==0?0xf2c0:0xf240);
            lk_put16(p+2,bad==1?0x8000:bad==2?0x0d00:bad==3?0x0f00:0);
            lk_mov_fixup(l,p,47,0x10000001);
            assert(!"expected malformed MOV relocation diagnostic");
        }
    }
    lk_destroy(l);
    puts("badge-link: 524288 exhaustive signed MOVW/MOVT addend/carry/register fixups PASS");
}
int main(void) {
    mov_address_fixups();
    branch_threshold(-16777216,1); branch_threshold(16777214,1);
    branch_threshold(-16777218,0); branch_threshold(16777216,0);
    char dir[]="build/link-test-XXXXXX"; assert(mkdtemp(dir)!=NULL);
    char in[128],out[128],script[128],alias[128],other[128];
    snprintf(in,sizeof(in),"%s/input.o",dir); snprintf(out,sizeof(out),"%s/output.elf",dir);
    snprintf(script,sizeof(script),"%s/script.ld",dir); snprintf(alias,sizeof(alias),"%s/alias.elf",dir);
    snprintf(other,sizeof(other),"%s/other.o",dir);
    char *args[]={"badge-link","-T","tests/fixtures/link_oracle_fixture.ld","-o",out,in,NULL};
    fixture(); write_object(in); assert(badge_link_main(6,args)==0);
    size_t size; unsigned char *elf=read_file(out,&size);
    assert(lk_u16(elf+16)==2 && lk_u32(elf+24)==0x10000001 && lk_u16(elf+44)==4);
    const unsigned char *text=section(elf,".text"),*data=section(elf,".data"),*bss=section(elf,".bss");
    assert(lk_u32(text+20)==20 && lk_u32(data+20)==24 && lk_u32(bss+20)==24);
    assert(lk_u32(data+12)==0x20000000 && lk_u32(bss+12)==0x20000018);
    const unsigned char *payload=elf+lk_u32(data+16);
    assert(lk_u32(payload)==0x10000011 && lk_u32(payload+4)==0xf000000d);
    assert(lk_u32(payload+8)==0 && lk_u32(payload+12)==0x20000028);
    assert(lk_u32(elf+lk_u32(text+16)+12)==0x20000000); free(elf);
    char *no_gc[]={"badge-link","--no-gc-sections","-T","tests/fixtures/link_oracle_fixture.ld","-o",out,in,NULL};
    assert(badge_link_main(7,no_gc)==0); elf=read_file(out,&size); assert(lk_u32(section(elf,".text")+20)==24); free(elf);
    fixture(); symbols[3].binding=1; write_object(in); save(out,"preserved",9);
    assert(badge_link_main(6,args)==1); preserved(out);
    fixture(); sections[2].name=".orphan"; write_object(in); assert(badge_link_main(6,args)==1); preserved(out);
    fixture(); symbols[1].section=BO_ABS; symbols[1].type=0; symbols[1].value=0x80000000; symbols[1].size=0;
    write_object(in); assert(badge_link_main(6,args)==1); preserved(out);
    fixture(); sections[3].size=0x80001; write_object(in); assert(badge_link_main(6,args)==1); preserved(out);
    fixture(); sections[0].size=0; symbols[0].size=0; object.relocation_count=0;
    write_object(in); assert(badge_link_main(6,args)==1); preserved(out);
    fixture(); write_object(in); write_object(other);
    char *duplicate[]={"badge-link","-T","tests/fixtures/link_oracle_fixture.ld","-o",out,in,other,NULL};
    assert(badge_link_main(7,duplicate)==1); preserved(out);
    fixture(); relocations[6].type=42; relocations[6].symbol=2; write_object(in);
    assert(badge_link_main(6,args)==0); elf=read_file(out,&size); payload=elf+lk_u32(section(elf,".data")+16);
    assert(lk_u32(payload+12)==0x70000005); free(elf);
    write_object("build/link-prel-fixture.o");
    fixture(); symbols[1].binding=2; write_object(in);
    bo_section_t one=sections[1]; bo_symbol_t defined={"callee",1,2,0,1,1,4};
    object=(bo_object_t){&one,1,&defined,1,NULL,0}; write_object(other);
    assert(badge_link_main(7,duplicate)==0);
    fixture(); symbols[1].binding=2; symbols[1].section=0; symbols[1].value=0; symbols[1].size=0;
    write_object("build/link-weak-fixture.o"); write_object(in); assert(badge_link_main(6,args)==0);
    unsigned char weak_code[sizeof(entry_bytes)]; memcpy(weak_code,entry_bytes,sizeof(weak_code));
    lk_put16(weak_code,0xf000); lk_put16(weak_code+2,0xf800);
    lk_put16(weak_code+4,0xf000); lk_put16(weak_code+6,0xb800);
    sections[0].data=weak_code; lk_put32(data_bytes,8); lk_put32(data_bytes+4,8);
    lk_put32(data_bytes+12,0x7ffffff0); relocations[6]=(bo_relocation_t){3,12,42,2};
    write_object("build/link-weak-addend-fixture.o");
    fixture(); relocations[1]=relocations[0]; write_object(in); save(out,"preserved",9);
    assert(badge_link_main(6,args)==1); preserved(out);
    fixture(); write_object(in);
    one=sections[3]; one.name=".bss.unused"; defined=(bo_symbol_t){"common",1,1,0,BO_COMMON,16,32};
    object=(bo_object_t){&one,1,&defined,1,NULL,0}; write_object(other);
    assert(badge_link_main(7,duplicate)==0); elf=read_file(out,&size);
    assert(lk_u32(section(elf,".bss")+12)%16==0 && lk_u32(section(elf,".bss")+20)==48); free(elf);
    fixture(); write_object("build/link-owned-fixture.o");
    const char *invalid[]={
        "", "MEMORY {}", "INCLUDE missing.ld", "MEMORY { X(r) : ORIGIN=0,LENGTH=0 }",
        "MEMORY { X(r) : ORIGIN=0,LENGTH=4 X(r) : ORIGIN=8,LENGTH=4 }",
        "MEMORY { X(r) : ORIGIN=0,LENGTH=8 Y(r) : ORIGIN=4,LENGTH=8 }",
        "MEMORY { X(r) : ORIGIN=4294967296,LENGTH=4 }",
        "MEMORY { X(q) : ORIGIN=0,LENGTH=4 }",
        "MEMORY { X(r) : ORIGIN=0,LENGTH=4 } ENTRY(entry) SECTIONS { .text : { *(.text*) } > missing }",
        "MEMORY { X(r) : ORIGIN=0x10000000,LENGTH=4k } ENTRY(entry) SECTIONS { .text : { SORT(*(.text*)) } > X }",
        "MEMORY { X(r) : ORIGIN=0x10000000,LENGTH=4k } ENTRY(entry) SECTIONS { .text : { *(.text*) . = . - 1; } > X }",
        "MEMORY { X(r) : ORIGIN=0x10000000,LENGTH=4k } ENTRY(entry) SECTIONS { .text : { *(.text*) } > X ASSERT(1 == 0,\"expected\") }",
        "MEMORY { X(r) : ORIGIN=0x10000000,LENGTH=4k } ENTRY(entry) SECTIONS { .text : { *(.text*) } > X =0x100 }",
        "MEMORY { X(r) : ORIGIN=0x10000000,LENGTH=4k } ENTRY(entry) SECTIONS { .text : { *(.text*) } > X foo=unknown; }",
        "MEMORY { X(r) : ORIGIN=0x10000000,LENGTH=4k } ENTRY(entry) SECTIONS { .text : { *(.text*) } > X foo=1; foo=2; }",
        "MEMORY { X(r) : ORIGIN=0x10000000,LENGTH=4k } ENTRY(entry) SECTIONS { .text : { *(.text*) . = ALIGN(3); } > X }",
        "/* unterminated"
    };
    args[2]=script;
    one=(bo_section_t){".text.entry",BO_PROGBITS,6,4,0,0,4,callee_bytes};
    defined=(bo_symbol_t){"entry",1,2,0,1,1,4};
    object=(bo_object_t){&one,1,&defined,1,NULL,0}; write_object(in);
    for(size_t k=0;k<sizeof(invalid)/sizeof(*invalid);++k) {
        save(script,invalid[k],strlen(invalid[k])); save(out,"preserved",9);
        assert(badge_link_main(6,args)==1); preserved(out);
    }
    args[2]="tests/fixtures/link_oracle_fixture.ld"; fixture(); write_object(in);
    unsigned char *input=read_file(in,&size);
    bo_object_t *decoded=badge_object_read(input,size); assert(decoded!=NULL);
    assert(decoded->section_count==5 && decoded->symbol_count==7); badge_object_free(decoded);
    for(size_t k=0;k<52;++k) { save(in,input,k); save(out,"preserved",9); assert(badge_link_main(6,args)==1); preserved(out); }
    for(size_t k=0;k<size;k+=17) {
        unsigned char old=input[k]; input[k]^=0x80; save(in,input,size); save(out,"preserved",9);
        int result=badge_link_main(6,args); assert(result==0 || result==1); if(result==1) preserved(out); input[k]=old;
    }
    save(in,input,size); free(input); save(out,"preserved",9);
    char *map_alias[]={"badge-link","-T","tests/fixtures/link_oracle_fixture.ld","-Map",out,"-o",out,in,NULL};
    assert(badge_link_main(8,map_alias)==1); preserved(out);
    char alternate[128]; snprintf(alternate,sizeof(alternate),"%s/./output.elf",dir);
    map_alias[4]=alternate; assert(badge_link_main(8,map_alias)==1); preserved(out);
    assert(unlink(out)==0); assert(badge_link_main(8,map_alias)==1);
    save(out,"preserved",9);
    assert(link(in,alias)==0); args[4]=alias; assert(badge_link_main(6,args)==1); assert(unlink(alias)==0);
    assert(symlink(out,alias)==0); assert(badge_link_main(6,args)==1); assert(unlink(alias)==0);
    args[4]=dir; assert(badge_link_main(6,args)==1); args[4]=out;
    save(script,"MEMORY\0{}",10); args[2]=script; assert(badge_link_main(6,args)==1); preserved(out);
    assert(badge_link_main(1,args)==2);
    assert(unlink(in)==0 && unlink(out)==0 && unlink(script)==0 && unlink(other)==0 && rmdir(dir)==0);
    printf("badge-link: GC, KEEP, weak/strong/common, all five relocations, RAM LMA, exact layouts, %zu script rejections, ELF truncations/mutations and atomic file safety PASS\n",sizeof(invalid)/sizeof(*invalid));
    return 0;
}
