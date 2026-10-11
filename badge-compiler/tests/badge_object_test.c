#define _DARWIN_C_SOURCE
#define main badge_object_main
#include "badge_object.c"
#undef main
#include <assert.h>

static const unsigned char entry_bytes[]={0xff,0xf7,0xfe,0xff,0x70,0x47};
static const unsigned char callee_bytes[]={7,0x20,0x70,0x47};
static const unsigned char ref_bytes[12]={0};
static const unsigned char significance[]={1,4};
static const unsigned char unwind[]={0,0,0,0,1,0,0,0};
static bo_section_t sections[6];
static bo_symbol_t symbols[9];
static bo_relocation_t relocations[5];
static bo_object_t object;

static void fixture(void) {
    sections[0]=(bo_section_t){".text.entry",BO_PROGBITS,6,2,0,0,sizeof(entry_bytes),entry_bytes};
    sections[1]=(bo_section_t){".text.callee",BO_PROGBITS,6,2,0,0,sizeof(callee_bytes),callee_bytes};
    sections[2]=(bo_section_t){".data.refs",BO_PROGBITS,3,4,0,0,sizeof(ref_bytes),ref_bytes};
    sections[3]=(bo_section_t){".bss.zero",BO_NOBITS,3,8,0,0,16,NULL};
    sections[4]=(bo_section_t){".llvm_addrsig",BO_LLVM_ADDRSIG,0x80000000u,1,BO_LINK_SYMTAB,0,sizeof(significance),significance};
    sections[5]=(bo_section_t){".ARM.exidx.text.callee",BO_ARM_EXIDX,130,4,2,0,sizeof(unwind),unwind};
    symbols[0]=(bo_symbol_t){"entry",1,2,0,1,1,sizeof(entry_bytes)};
    symbols[1]=(bo_symbol_t){"$t.0",0,0,0,1,0,0};
    symbols[2]=(bo_symbol_t){"",0,3,0,2,0,0};
    symbols[3]=(bo_symbol_t){"callee",1,2,2,2,1,sizeof(callee_bytes)};
    symbols[4]=(bo_symbol_t){"weak_missing",2,0,0,0,0,0};
    symbols[5]=(bo_symbol_t){"common_data",1,1,0,BO_COMMON,8,8};
    symbols[6]=(bo_symbol_t){"absolute",1,0,0,BO_ABS,42,0};
    symbols[7]=(bo_symbol_t){"source file.c",0,4,0,BO_ABS,0,0};
    symbols[8]=(bo_symbol_t){"refs",1,1,0,3,0,sizeof(ref_bytes)};
    relocations[0]=(bo_relocation_t){1,0,10,4};
    relocations[1]=(bo_relocation_t){3,0,2,4};
    relocations[2]=(bo_relocation_t){3,4,2,5};
    relocations[3]=(bo_relocation_t){3,8,2,6};
    relocations[4]=(bo_relocation_t){6,0,42,3};
    object=(bo_object_t){sections,6,symbols,9,relocations,5};
}

static const unsigned char *header(const unsigned char *bytes, unsigned index) {
    return bytes+bo_u32(bytes+32)+(size_t)index*40;
}

static unsigned find_section(const unsigned char *bytes, const char *name) {
    const unsigned char *h=header(bytes,bo_u16(bytes+50));
    const char *strings=(const char *)bytes+bo_u32(h+16);
    for(unsigned k=1;k<bo_u16(bytes+48);++k)
        if(strcmp(strings+bo_u32(header(bytes,k)),name)==0) return k;
    assert(!"missing section"); return 0;
}

static void reject(void) {
    unsigned char *bytes=(unsigned char *)&object; size_t size=123;
    assert(!badge_object_write(&object,&bytes,&size)); assert(bytes==NULL && size==0);
}

static void save(const char *path, const unsigned char *data, size_t size) {
    FILE *f=fopen(path,"wb"); assert(f!=NULL);
    assert(fwrite(data,1,size,f)==size); assert(fclose(f)==0);
}

static void preserved(const char *path) {
    FILE *f=fopen(path,"rb"); assert(f!=NULL); char text[16]={0};
    assert(fread(text,1,sizeof(text),f)==9); assert(memcmp(text,"preserved",9)==0);
    assert(fclose(f)==0);
}

int main(void) {
    fixture();
    unsigned char *bytes; size_t size;
    assert(badge_object_write(&object,&bytes,&size));
    assert(size<2048 && bo_u16(bytes+16)==1 && bo_u16(bytes+18)==40);
    assert(bo_u32(bytes+36)==0x05000000 && bo_u16(bytes+44)==0);
    assert(bo_u16(bytes+48)==13);
    const unsigned char *sym=header(bytes,find_section(bytes,".symtab"));
    assert(bo_u32(sym+28)==4 && bo_u32(sym+20)==160);
    const unsigned char *entries=bytes+bo_u32(sym+16);
    assert(entries[16+12]==0 && entries[32+12]==3 && entries[48+12]==4);
    assert(entries[64+12]==0x12 && bo_u32(entries+64+4)==1);
    const unsigned char *rh=header(bytes,find_section(bytes,".rel.text.entry"));
    assert(bo_u32(rh+24)==find_section(bytes,".symtab") && bo_u32(rh+28)==1);
    assert(bo_u32(bytes+bo_u32(rh+16)+4)==(5u<<8|10u));
    const unsigned char *ah=header(bytes,5);
    assert(bo_u32(ah+24)==find_section(bytes,".symtab"));
    assert(bytes[bo_u32(ah+16)]==4 && bytes[bo_u32(ah+16)+1]==5);
    assert(bo_u32(header(bytes,4)+4)==BO_NOBITS && bo_u32(header(bytes,4)+20)==16);
    save("build/object-owned-fixture.o",bytes,size);
    bo_model_t model; assert(bo_model_init(&model)); assert(bo_import(bytes,size,&model));
    unsigned char *again; size_t again_size;
    assert(badge_object_write(&model.object,&again,&again_size));
    /* A first write groups local symbols; subsequent writes are canonical. */
    bo_model_t second; assert(bo_model_init(&second)); assert(bo_import(again,again_size,&second));
    unsigned char *third; size_t third_size;
    assert(badge_object_write(&second.object,&third,&third_size));
    assert(again_size==third_size && memcmp(again,third,again_size)==0);
    bo_model_free(&second); free(third); free(again); bo_model_free(&model);

    size_t malformed=0;
#define BAD(change) do { fixture(); change; reject(); ++malformed; } while(0)
    BAD(object.section_count=BO_MAX_SECTIONS+1);
    BAD(object.symbol_count=BO_MAX_SYMBOLS+1);
    BAD(object.relocation_count=BO_MAX_RELOCATIONS+1);
    BAD(object.sections=NULL);
    BAD(sections[0].name=NULL);
    BAD(sections[0].name="");
    BAD(sections[0].name=".symtab");
    BAD(sections[1].name=sections[0].name);
    BAD(sections[0].type=4);
    BAD(sections[0].flags=0x400);
    BAD(sections[0].alignment=0);
    BAD(sections[0].alignment=3);
    BAD(sections[0].alignment=131072);
    BAD(sections[0].data=NULL);
    BAD(sections[0].size=BO_MAX_FILE+1);
    BAD(sections[3].data=entry_bytes);
    BAD(sections[0].entry_size=4);
    BAD(sections[2].flags=32);
    BAD(sections[2].flags=16);
    BAD(sections[0].link=1);
    BAD(sections[5].link=6);
    BAD(sections[5].link=99);
    BAD(sections[5].link=3);
    BAD(sections[5].flags=2);
    BAD(sections[4].link=0);
    BAD(symbols[0].binding=3);
    BAD(symbols[0].type=6);
    BAD(symbols[0].visibility=4);
    BAD(symbols[0].section=99);
    BAD(symbols[0].value=0);
    BAD(symbols[0].size=100);
    BAD(symbols[4].binding=0);
    BAD(symbols[4].value=1);
    BAD(symbols[5].value=3);
    BAD(symbols[2].name="wrong");
    BAD(symbols[7].section=0);
    BAD(relocations[0].type=99);
    BAD(relocations[0].symbol=0);
    BAD(relocations[0].symbol=99);
    BAD(relocations[0].section=0);
    BAD(relocations[0].section=4);
    BAD(relocations[0].section=5);
    BAD(relocations[0].offset=1);
    BAD(relocations[0].offset=4);
    BAD(relocations[0].section=3);
    const unsigned char bad_leb[]={0x80};
    BAD(sections[4].data=bad_leb; sections[4].size=1);
    const unsigned char overflowing_leb[]={0xff,0xff,0xff,0xff,0x10};
    BAD(sections[4].data=overflowing_leb; sections[4].size=5);
    const unsigned char unknown_id[]={10};
    BAD(sections[4].data=unknown_id; sections[4].size=1);
#undef BAD
    fixture();
    sections[3].size=UINT32_MAX;
    unsigned char *large; size_t large_size;
    assert(badge_object_write(&object,&large,&large_size)); assert(large_size<2048); free(large);
    bo_object_t empty={0}; assert(badge_object_write(&empty,&large,&large_size)); free(large);

    /* Reordering can increase a ULEB128's width. */
    bo_symbol_t many[130];
    for(size_t k=0;k<130;++k) many[k]=(bo_symbol_t){"x",k==0?1u:0u,0,0,BO_ABS,0,0};
    const unsigned char one_id[]={1};
    bo_section_t sig={".llvm_addrsig",BO_LLVM_ADDRSIG,0x80000000u,1,BO_LINK_SYMTAB,0,1,one_id};
    bo_object_t expanded={&sig,1,many,130,NULL,0};
    assert(badge_object_write(&expanded,&large,&large_size));
    ah=header(large,1); assert(bo_u32(ah+20)==2);
    assert(large[bo_u32(ah+16)]==0x82 && large[bo_u32(ah+16)+1]==1); free(large);

    char dir[]="build/object-test-XXXXXX"; assert(mkdtemp(dir)!=NULL);
    char input[128],output[128],alias[128],symlink_path[128];
    assert(snprintf(input,sizeof(input),"%s/input.bobj",dir)>0);
    assert(snprintf(output,sizeof(output),"%s/output.o",dir)>0);
    assert(snprintf(alias,sizeof(alias),"%s/alias.o",dir)>0);
    assert(snprintf(symlink_path,sizeof(symlink_path),"%s/symlink.o",dir)>0);
    const char valid[]="badge-object-v1\n# comment\nsection 1 \".text\" 1 6 2 0 0 2 7047\n"
        "symbol 1 \"function name\" 1 2 0 1 1 2\n";
    char *args[]={"badge-object",input,"-o",output,NULL};
    save(input,(const unsigned char *)valid,strlen(valid)); assert(badge_object_main(4,args)==0);
    const char *invalid[]={
        "", "wrong\n", "badge-object-v1 extra\n", "badge-object-v1\nwat 1\n",
        "badge-object-v1\nsection 2 x 1 2 4 0 0 0 -\n",
        "badge-object-v1\nsection 1 x 1 2 3 0 0 0 -\n",
        "badge-object-v1\nsection 1 x 1 2 4 0 0 2 00\n",
        "badge-object-v1\nsection 1 x 1 2 4 0 0 1 gg\n",
        "badge-object-v1\nsection 1 x 8 3 4 0 0 1 00\n",
        "badge-object-v1\nsection 1 x 1 2 4 0 0 -1 -\n",
        "badge-object-v1\nsection 1 x 1 2 4 0 0 4294967296 -\n",
        "badge-object-v1\nsection 1 \"unterminated 1 2 4 0 0 0 -\n",
        "badge-object-v1\nsection 1 \"bad\\x\" 1 2 4 0 0 0 -\n",
        "badge-object-v1\nsection 1 \"name\"junk 1 2 4 0 0 0 -\n",
        "badge-object-v1\nsymbol 2 x 1 0 0 0 0 0\n",
        "badge-object-v1\nreloc 1 0 2 1\n",
        "badge-object-v1\nsection 1 x 1 2 4 0 0 4 00000000\nreloc 1 0 2 1\n"
    };
    for(size_t k=0;k<sizeof(invalid)/sizeof(invalid[0]);++k) {
        save(input,(const unsigned char *)invalid[k],strlen(invalid[k]));
        save(output,(const unsigned char *)"preserved",9);
        assert(badge_object_main(4,args)==1); preserved(output);
    }
    const unsigned char nul_manifest[]="badge-object-v1\0\n";
    save(input,nul_manifest,sizeof(nul_manifest)); assert(badge_object_main(4,args)==1); preserved(output);
    for(size_t k=0;k<strlen(valid);++k) {
        char changed[sizeof(valid)]; memcpy(changed,valid,sizeof(valid)); changed[k]='?';
        save(input,(const unsigned char *)changed,strlen(changed));
        save(output,(const unsigned char *)"preserved",9);
        int result=badge_object_main(4,args); assert(result==0 || result==1);
        if(result==1) preserved(output);
    }
    for(size_t k=0;k<size;++k) {
        if(k%4!=0) continue;
        unsigned char *changed=malloc(size); assert(changed!=NULL); memcpy(changed,bytes,size); changed[k]^=0xff;
        bo_model_t mutated; assert(bo_model_init(&mutated));
        if(bo_import(changed,size,&mutated)) {
            unsigned char *rebuilt; size_t rebuilt_size;
            assert(badge_object_write(&mutated.object,&rebuilt,&rebuilt_size)); free(rebuilt);
        }
        bo_model_free(&mutated); free(changed);
    }
    for(size_t k=0;k<size;++k) {
        bo_model_t truncated; assert(bo_model_init(&truncated));
        assert(!bo_import(bytes,k,&truncated)); bo_model_free(&truncated);
    }
    save(input,(const unsigned char *)valid,strlen(valid));
    assert(link(input,alias)==0);
    char *alias_args[]={"badge-object",input,"-o",alias,NULL};
    assert(badge_object_main(4,alias_args)==1); assert(unlink(alias)==0);
    assert(symlink(output,symlink_path)==0);
    alias_args[3]=symlink_path; assert(badge_object_main(4,alias_args)==1); assert(unlink(symlink_path)==0);
    alias_args[3]=dir; assert(badge_object_main(4,alias_args)==1);
    assert(badge_object_main(1,args)==2);
    assert(unlink(input)==0 && unlink(output)==0 && rmdir(dir)==0);
    free(bytes);
    printf("badge-object: %zu rejected API cases, %zu rejected manifests, %zu manifest mutations; ELF truncation/mutations, local ordering, Thumb symbols, REL, addrsig, NOBITS and file safety PASS\n",
        malformed,sizeof(invalid)/sizeof(invalid[0])+1,strlen(valid));
    return 0;
}
