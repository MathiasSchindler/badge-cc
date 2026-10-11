#define _DARWIN_C_SOURCE
#define main badge_asm_main
#include "badge_asm.c"
#undef main
#include <assert.h>

static void save(const char *path,const void *bytes,size_t length) {
    FILE *f=fopen(path,"wb"); assert(f!=NULL);
    assert(fwrite(bytes,1,length,f)==length); assert(fclose(f)==0);
}

static void preserved(const char *path) {
    FILE *f=fopen(path,"rb"); char text[16]; assert(f!=NULL);
    assert(fread(text,1,sizeof(text),f)==9 && memcmp(text,"preserved",9)==0); assert(fclose(f)==0);
}

static assembler_t *fixture(const char *source) {
    assembler_t *s=calloc(1,sizeof(*s)); assert(s!=NULL);
    s->input="fixture.s"; s->output="build/asm-fixture.o";
    assert(setjmp(s->failure)==0);
    size_t n=strlen(source); unsigned char *input=as_allocate(s,n+1); memcpy(input,source,n+1);
    as_assemble(s,input,n,&s->result,&s->result_size);
    return s;
}

static void release(assembler_t *s) { free(s->result); as_destroy(s); }

static uint32_t read_word(const unsigned char *bytes, unsigned width) {
    uint32_t value=0;
    for(unsigned k=0;k<width;++k) value|=(uint32_t)bytes[k]<<(k*8);
    return value;
}

static void expression_error(assembler_t *s, const char *text) {
    if(setjmp(s->failure)==0) {
        (void)as_expression(s,text);
        assert(!"expected cached expression diagnostic");
    }
}

static void check_branch(const char *source,unsigned width) {
    assembler_t *s=fixture(source);
    int found=0;
    for(size_t k=0;k<s->line_count;++k)
        if(s->lines[k].op[0]=='b') { assert(s->lines[k].width==width); found=1; }
    assert(found); release(s);
}
static void check_mov_relocations(void) {
    assembler_t *s=fixture("nop\nmovw r12,#:lower16:(data-32768)\n"
        "movt r12,#:upper16:(data-32768)\n"
        "movw r0,#:lower16:0xfedc1235\nmovt r0,#:upper16:0xfedc1235\n"
        ".section .data,\"aw\",%progbits\ndata:.word 0\n");
    assert(s->relocation_count==2);
    assert(s->relocations[0].offset==2 && s->relocations[0].type==47);
    assert(s->relocations[1].offset==6 && s->relocations[1].type==48);
    assert(s->relocations[0].symbol==s->relocations[1].symbol);
    const unsigned char *p=s->sections[1].out.data;
    assert((read_word(p+2,2)&15)==8 && (read_word(p+6,2)&15)==8);
    assert((read_word(p+12,2)&255)==0x35 && (read_word(p+16,2)&255)==0xdc);
    release(s);
}
static void check_directive_layout(void) {
    assembler_t *s=fixture("nop\n.p2align 2\naligned:nop\n");
    s->final=0;
    s->lines[1].arg[0]=as_copy(s,"4");
    as_pass(s);
    assert(s->changed && s->sections[1].out.size==18);
    assert(s->symbols[as_symbol(s,"aligned")].raw_value==16);
    as_pass(s);
    assert(!s->changed && s->sections[1].out.size==18);
    release(s);
}

static void check_expression_cache(void) {
    assembler_t *s=fixture(
        "start:b target\n.equ here,.\n.equ delta,target-here\n.space 2050\n"
        "target:nop\n.word delta,.-start,.-start\n"
        "it eq\nmoveq.w r0,#1\nldr.w r1,[r2,#(1+3)]\nldr.w r1,[r2,#(2+6)]\n");
    assert(s->lines[1].width==4);
    assert(s->symbols[as_symbol(s,"here")].raw_value==4);
    assert(s->symbols[as_symbol(s,"delta")].raw_value==2050);
    const unsigned char *bytes=s->sections[1].out.data;
    assert(read_word(bytes+2056,4)==2050);
    assert(read_word(bytes+2060,4)==2060 && read_word(bytes+2064,4)==2064);
    assert(read_word(bytes+2068,2)==0xbf08);
    assert((read_word(bytes+2076,2)&4095)==4 && (read_word(bytes+2080,2)&4095)==8);
    as_expression_cache_t *cache=s->expressions[as_hash(".-start")];
    while(cache && strcmp(cache->text,".-start")!=0) cache=cache->next;
    assert(cache!=NULL && cache->stack==1 && cache->stack_max==2);
    size_t allocated=s->allocated; s->current=1;
    s->sections[1].out.size=42;
    assert(as_absolute(s,".-start")==42);
    s->sections[1].out.size=100;
    assert(as_absolute(s,".-start")==100);
    /* Only the dynamically named dot symbols allocate during replay. */
    assert(s->allocated>allocated);
    allocated=s->allocated;
    assert(as_absolute(s,".-start")==100 && s->allocated==allocated);
    assert(as_absolute(s,"((3+4)*5-2)/3%7")==4);
    allocated=s->allocated;
    assert(as_absolute(s,"((3+4)*5-2)/3%7")==4 && s->allocated==allocated);
    assert(as_absolute(s,"~(-1)")==0 && as_absolute(s,"~(-1)")==0);
    assert(as_absolute(s,"((8<<2)>>1)==16")==1 && as_absolute(s,"((8<<2)>>1)==16")==1);
    assert(as_absolute(s,"((7&3)^1)|8") ==10 && as_absolute(s,"((7&3)^1)|8")==10);
    s->sections[1].out.size=0;
    assert(as_absolute(s,"9223372036854775807+(.-start)")==INT64_MAX);
    s->sections[1].out.size=1;
    expression_error(s,"9223372036854775807+(.-start)");
    assert(as_absolute(s,"1/(.-start)")==1);
    assert(as_absolute(s,"1<<(.-start)")==2);
    s->sections[1].out.size=0;
    expression_error(s,"1/(.-start)");
    assert(as_absolute(s,"-((.-start)<<63)")==0);
    s->sections[1].out.size=1;
    expression_error(s,"-((.-start)<<63)");
    s->sections[1].out.size=64;
    expression_error(s,"1<<(.-start)");
    char chain[8192];
    for(unsigned k=0;k<4095;++k) { chain[k*2]='1'; chain[k*2+1]='+'; }
    chain[8190]='1'; chain[8191]=0;
    assert(as_absolute(s,chain)==4096 && as_absolute(s,chain)==4096);
    allocated=s->allocated;
    s->allocated=AS_MAX_MEMORY-sizeof(as_expression_cache_t)-1;
    expression_error(s,"uncached_expression+1");
    assert(s->allocated<=AS_MAX_MEMORY);
    s->allocated=allocated;
    release(s);
}

int main(void) {
    check_mov_relocations();
    check_directive_layout();
    char dir[]="build/asm-test-XXXXXX"; assert(mkdtemp(dir)!=NULL);
    char input[128],output[128],binary[128],alias[128];
    assert(snprintf(input,sizeof(input),"%s/input.s",dir)>0);
    assert(snprintf(output,sizeof(output),"%s/output.o",dir)>0);
    assert(snprintf(binary,sizeof(binary),"%s/payload.bin",dir)>0);
    assert(snprintf(alias,sizeof(alias),"%s/alias.o",dir)>0);
    char *args[]={"badge-asm",input,"-o",output,NULL};
    const char *valid[]={
        "", "nop", ".syntax unified\n.cpu cortex-m33\n.thumb\nmovs r0,#7\nbx lr\n",
        ".byte -128,255\n.short -32768,65535\n.long -2147483648,4294967295\n",
        ".global f\n.thumb_func\nf: bx lr\n.size f,.-f\n",
        ".equ X, 7\nmovw r0,#X\n",
        ".equ X, (3+4)*2\n.word X\n",
        ".word ((16 >> 2) | 3) ^ 1\n",
        ".section .bss.x,\"aw\",%nobits\n.long 0\n.zero 16\n",
        ".section .rodata.s,\"aMS\",%progbits,1\n.asciz \"hello\"\n",
        ".section .data,\"aw\"\na:.word b\nb:.word a\n",
        "a:nop\n.equ here,.\nnop\n.word here\n",
        ".equ a,b+1\n.equ b,42\n.word a\n",
        ".if 0\n.error \"not reached\"\n.endif\n",
        ".global f\n.type f,%function\n.thumb_func f\nf:bx lr\n",
        "ite eq\nmoveq r0,#1\nmovne r0,#2\n",
        "itt ne\naddne r0,r1,#1\nsubne r0,#7\n",
        "it eq\naddseq r0,r1,#1\n",
        ".section .data.x,\"aw\"\na:.long b-a\n.section .data.y,\"aw\"\nb:.long a-b\n",
        ".section .text.f,\"ax\"\n.thumb_func\nf:.fnstart\nbx lr\n.cantunwind\n.fnend\n",
        ".comm x,8,8\n.word x\n",
        ".global x\n.word x\n.addrsig\n.addrsig_sym x\n",
        ".file \"quote\\\" slash\\\\ space.c\"\n.ident \"version\"\n",
        ".code 16\n.fpu fpv5-sp-d16\n.eabi_attribute 67,\"2.09\"\n",
        "mov.w lr,#0xff00ff00\nlsl.w lr,r8,r9\nclz lr,r8\n",
        "movs r0,#256\n",
        "subw sp,sp,#1044\naddw sp,sp,#1044\n",
        "addw sp,sp,#0\nsubw sp,sp,#4095\naddw r0,sp,#4095\nsubw lr,sp,#1\n",
        "cbz r0,x\n.space 128\nx:nop\n",
        "ldrd r0,r1,[sp,#1020]\nstrd r0,r1,[r8,#-1020]\n",
        "strb r0,[r1,#255]!\nldrh r2,[r3],#255\n",
        "a:ldr.w r8,b\nnop\n.p2align 2\nb:.word 42\n",
        "b.n target\ntarget:nop\nb.w target\nnop.w\n",
        ".equ extent,end-start\nstart:nop\nend:.word extent,extent\n",
        ".equ first,second+1\n.word first,first\n.equ second,41\n",
        "itttt eq\nmoveq r0,#1\naddeq r0,#1\nsubeq r0,#1\nbxeq lr\n"
    };
    for(size_t k=0;k<sizeof(valid)/sizeof(*valid);++k) {
        save(input,valid[k],strlen(valid[k])); assert(badge_asm_main(4,args)==0);
    }
    const char *invalid[]={
        ".byte -129\n",".byte 256\n",".short -32769\n",".long 4294967296\n",
        "movs r16,#1\n","movs r0,#0x12345678\n","movw r0,#65536\n",
        "mov.w r0,#0x12345678\n","addw r0,r1,#4096\n",
        "movw sp,#1\n","movt sp,#1\n","movw pc,#1\n","movt pc,#1\n",
        "addw sp,r0,#4\n","subw sp,r0,#4\n","addw pc,sp,#4\n","subw pc,sp,#4\n",
        "addw r0,pc,#4\n","subw r0,pc,#4\n","addw sp,sp,#4096\n","subw sp,sp,#4096\n",
        "ldr.w r0,[r1,#4096]\n","strb r0,[r1],#256\n","ldrd r0,r1,[r2,#2]\n",
        "str r0,[r0],#4\n","ldr r0,[r1,r13]\n","lsls r0,r1,#32\n",
        "lsrs r0,r1,#0\n","asrs r0,r1,#33\n","bfc r0,#31,#2\n","ubfx r0,r1,#0,#0\n",
        "push {r0,r0}\n","push {pc}\n","pop {sp}\n",
        "ldm.w r0!,{r0,r1}\n","smull r0,r0,r1,r2\n",
        "it eq\nmovne r0,#1\n","moveq r0,#1\n","it eq\n",
        "ite eq\npopeq {pc}\nmovne r0,#1\n","it al\n","ittttt eq\n",
        "cbz r8,x\nx:nop\n","x:nop\ncbz r0,x\n","cbz r0,x\n.space 130\nx:nop\n",
        "b x+1\nx:nop\n","ldr r0,x\n.space 4096\n.p2align 2\nx:.word 0\n",
        "x:nop\nx:nop\n",":nop\n",
        ".equ x,x\n.word x\n",".equ x,y\n.equ y,x\n.word x\n",
        ".long (1/0)\n",".long 1<<64\n",".long (9223372036854775807+1)\n",
        ".long 4294967295*4294967295\n",".long foo+9223372036854775807\n",
        ".word foo-x\n.equ x,-9223372036854775807-1\n",
        ".section .bss.x,\"aw\",%nobits\n.ascii \"nonzero\"\n",
        ".section .bss.x,\"aw\",%nobits\n.long 1\n",
        ".section .data.x,\"aw\"\nnop\n",
        ".p2align 17\n",".balign 3\n",".ascii \"unterminated\n",
        ".type x,%object\n.thumb_func\nx:nop\n.size x,-1\n",
        ".endif\n",".if 1\n.error \"expected failure\"\n.endif\n",
        ".if x\n.error \"unknown\"\n.endif\n",
        ".fnstart\nnop\n.fnend\n","#define X 1\n",
        ".eabi_attribute 28,1\n",".cpu cortex-m0\n",".code 32\n",
        "b .missing\n",".equ x,missing\n.word x\n",
        "b.n x\n.space 2050\nx:nop\n", "it.w eq\n",
        ".word amount/0\n.equ amount,1\n",
        ".word amount<<64\n.equ amount,1\n",
        ".word amount+9223372036854775807\n.equ amount,1\n",
        ".word -amount\n.equ amount,-9223372036854775807-1\n",
        "start:b target\n.equ here,.\n.space 2050\ntarget:nop\n.word 9223372036854775805+(here-start)\n",
        "start:b target\n.equ here,.\n.space 2050\ntarget:nop\n.word 1/(4-(here-start))\n",
        ".word ~external\n", ".word external*2\n",
        ".word (1+2\n", ".word 1junk\n", "movseq r0,#1\n"
    };
    for(size_t k=0;k<sizeof(invalid)/sizeof(*invalid);++k) {
        save(input,invalid[k],strlen(invalid[k])); save(output,"preserved",9);
        assert(badge_asm_main(4,args)==1); preserved(output);
    }
    const char *unsupported[]={
        ".macro foo\n.endm\n","svc #0\n","mrs r0,primask\n",".org 0x100\n",
        "push.w {lr}\n","beq external\n","1:nop\nb 1b\n",
        ".if 0\nnop\n.endif\n",".if 0\n.else\nnop\n.endif\n",
        ".ascii \"\\x41\"\n",".loc 1 1\n",".set x,1\n.set x,2\n"
    };
    for(size_t k=0;k<sizeof(unsupported)/sizeof(*unsupported);++k) {
        save(input,unsupported[k],strlen(unsupported[k])); save(output,"preserved",9);
        assert(badge_asm_main(4,args)==1); preserved(output);
    }
    check_branch("b x\n.space 2048\nx:nop\n",2);
    check_branch("b x\n.space 2050\nx:nop\n",4);
    check_branch("beq x\n.space 256\nx:nop\n",2);
    check_branch("beq x\n.space 258\nx:nop\n",4);
    check_branch("x:nop\n.space 2042\nb x\n",2);
    check_branch("x:nop\n.space 2044\nb x\n",4);
    check_branch("b alias\n.space 2050\ntarget:nop\n.equ alias,target\n",4);
    check_branch("target:nop\n.space 2044\n.equ alias,target\nb alias\n",4);
    check_branch("beq target\n.space 254\nb far\ntarget:nop\n.space 2050\nfar:nop\n",4);
    check_expression_cache();
    assembler_t *s=fixture("it eq\naddseq r0,r1,#1\n");
    assert(s->sections[1].out.size==6 && s->lines[1].width==4); release(s);
    s=fixture("a:nop\n.equ location,.\nnop\n.word location\n");
    as_symbol_t *location=&s->symbols[as_symbol(s,"location")];
    assert(location->raw_value==2 && location->out.section==1); release(s);
    s=fixture(".section \".note.GNU-stack\",\"\",%progbits\n");
    assert(strcmp(s->sections[2].out.name,".note.GNU-stack")==0); release(s);
    const unsigned char payload[]={1,2,3,4}; save(binary,payload,sizeof(payload));
    char source[512];
    assert(snprintf(source,sizeof(source),".section .data.x,\"aw\"\n.incbin \"%s\",1,2\n",binary)>0);
    save(input,source,strlen(source)); assert(badge_asm_main(4,args)==0);
    assert(snprintf(source,sizeof(source),".incbin \"%s\",5\n",binary)>0);
    save(input,source,strlen(source)); save(output,"preserved",9);
    assert(badge_asm_main(4,args)==1); preserved(output);
    assert(snprintf(source,sizeof(source),".section .bss.x,\"aw\",%%nobits\n.incbin \"%s\"\n",binary)>0);
    save(input,source,strlen(source)); assert(badge_asm_main(4,args)==1); preserved(output);
    char aliases[4096]; size_t used=0;
    for(unsigned k=0;k<66;++k) {
        int n=snprintf(aliases+used,sizeof(aliases)-used,".equ a%u,a%u\n",k,k+1);
        assert(n>0 && (size_t)n<sizeof(aliases)-used); used+=(size_t)n;
    }
    int n=snprintf(aliases+used,sizeof(aliases)-used,".equ a66,1\n.word a0\n");
    assert(n>0 && (size_t)n<sizeof(aliases)-used); used+=(size_t)n;
    save(input,aliases,used); assert(badge_asm_main(4,args)==1); preserved(output);
    assert(snprintf(source,sizeof(source),".incbin \"%s\"\n",binary)>0);
    save(input,source,strlen(source)); char *bin_args[]={"badge-asm",input,"-o",binary,NULL};
    assert(badge_asm_main(4,bin_args)==1);
    FILE *f=fopen(binary,"rb"); unsigned char read[4]; assert(f!=NULL && fread(read,1,4,f)==4 && fclose(f)==0);
    assert(memcmp(read,payload,4)==0);
    const char mutation[]=".thumb\nmovs r0,#1\nbx lr\n";
    for(size_t k=0;k<sizeof(mutation)-1;++k) {
        char changed[sizeof(mutation)]; memcpy(changed,mutation,sizeof(mutation)); changed[k]='?';
        save(input,changed,sizeof(changed)-1); save(output,"preserved",9);
        int result=badge_asm_main(4,args); assert(result==0 || result==1); if(result==1) preserved(output);
    }
    const char nul[]="nop\0nop\n"; save(input,nul,sizeof(nul)); save(output,"preserved",9);
    assert(badge_asm_main(4,args)==1); preserved(output);
    save(input,"nop\n",4); assert(link(input,alias)==0);
    char *alias_args[]={"badge-asm",input,"-o",alias,NULL};
    assert(badge_asm_main(4,alias_args)==1); assert(unlink(alias)==0);
    assert(symlink(output,alias)==0); assert(badge_asm_main(4,alias_args)==1); assert(unlink(alias)==0);
    alias_args[3]=dir; assert(badge_asm_main(4,alias_args)==1);
    assert(badge_asm_main(1,args)==2);
    assert(unlink(input)==0 && unlink(output)==0 && unlink(binary)==0 && rmdir(dir)==0);
    printf("badge-asm: %zu positive, %zu negative, %zu unsupported, %zu mutations; exact branch thresholds, IT flags, aliases, syntax cache replay, incbin and atomic file safety PASS\n",
        sizeof(valid)/sizeof(*valid),sizeof(invalid)/sizeof(*invalid),sizeof(unsupported)/sizeof(*unsupported),sizeof(mutation)-1);
    return 0;
}
