#define BADGE_PARSE_LIBRARY
#define BADGE_SEMA_LIBRARY
#include "badge_parse.c"
#include "badge_sema.c"
#define main badge_ir_main
#include "badge_ir.c"
#undef main
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>
#include "test_mode.h"

static void verify(const char *path) {
    const char *opt=badge_test_tool("IR_TEST_OPT");
    if(opt==NULL) return;
    pid_t pid=fork(); assert(pid>=0);
    if(pid==0) {
        execlp(opt,opt,"-passes=verify","-disable-output",path,(char *)NULL);
        _exit(127);
    }
    int status; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static void fixture_checked(const char *source,const char *fragment,const char *forbidden,int accepted,const char *path) {
    for(unsigned mode=0;mode<2;++mode) {
    parser_t *p=calloc(1,sizeof(*p)); assert(p!=NULL); p->tool="badge-ir-test";
    int failed=setjmp(p->failure);
    if(!failed) {
        node_t *root=parse_source(p,source,strlen(source),"fixture.i");
        ir_process_mode(p,root,mode!=0);
    }
    if(accepted>=0 && (failed==0)!=accepted) { fprintf(stderr,"unexpected fixture result: %s\n",source); abort(); }
    if(!failed) {
        if(mode==0) {
            assert(fragment==NULL || strstr(p->output,fragment)!=NULL);
            assert(forbidden==NULL || strstr(p->output,forbidden)==NULL);
        }
        assert(host_write_file("badge-ir-test",path,(unsigned char *)p->output,p->output_size));
        verify(path);
    }
    parse_destroy(p); free(p);
    }
}
static void fixture(const char *source,const char *fragment,int accepted,const char *path) {
    fixture_checked(source,fragment,NULL,accepted,path);
}
static void save(const char *path,const char *source) {
    FILE *f=fopen(path,"wb"); assert(f!=NULL);
    assert(fwrite(source,1,strlen(source),f)==strlen(source) && fclose(f)==0);
}
static void global_storage_tests(const char *path) {
    static const struct { const char *declaration,*zero,*nonzero; unsigned size; } types[]={
        {"unsigned char value","0","1",1},
        {"short value","0","-1",2},
        {"int value","0","1",4},
        {"unsigned long long value","0","1",8},
        {"float value","0","1",4},
        {"double value","0","1",8},
        {"int *value","0","(int *)4",4},
        {"unsigned char value[37]","{0}","{[36]=1}",37},
        {"int *value[3]","{0,0,0}","{0,(int *)4,0}",12},
        {"struct Record value","{.p=0,.tail=0}","{.tail=1}",12},
        {"union Storage value","{.p=0}","{.word=1}",4},
    };
    const char *qualifiers[]={"","static ","volatile ","const "};
    const char *sections[]={""," __attribute__((section(\".retained\"),used))"};
    for(size_t type=0;type<sizeof(types)/sizeof(*types);++type)
        for(size_t qualifier=0;qualifier<sizeof(qualifiers)/sizeof(*qualifiers);++qualifier)
            for(size_t section=0;section<sizeof(sections)/sizeof(*sections);++section)
                for(unsigned initialization=0;initialization<3;++initialization) {
                    char source[512],fragment[160];
                    const char *name=strstr(types[type].declaration,"value");
                    assert(name!=NULL);
                    assert(snprintf(source,sizeof(source),
                        "struct Record{char head;int *p;short tail;};"
                        "union Storage{int *p;unsigned word;};"
                        "typedef %.*sObjectType%s;%sObjectType value%s%s%s;",
                        (int)(name-types[type].declaration),types[type].declaration,name+5,
                        qualifiers[qualifier],sections[section],
                        initialization?"=":"",
                        initialization==1?types[type].zero:
                        initialization==2?types[type].nonzero:"")>0);
                    if(initialization==2)
                        assert(snprintf(fragment,sizeof(fragment),"@\"value\" = %s%s ",
                            qualifier==1?"internal ":"dso_local ",
                            qualifier==3?"constant":"global")>0);
                    else
                        assert(snprintf(fragment,sizeof(fragment),"@\"value\" = %s%s [%u x i8] zeroinitializer",
                            qualifier==1?"internal ":"dso_local ",
                            qualifier==3?"constant":"global",types[type].size)>0);
                    fixture_checked(source,fragment,initialization==2?"zeroinitializer":NULL,1,path);
                }
    fixture("int target;struct S{int *p;int n;};struct S value={&target,0};",
            "ptr @\"target\"",1,path);
    fixture("int target;struct S{int *p;int n;};struct S value={.p=&target,.p=0};",
            "global [8 x i8] zeroinitializer",1,path);
}
int main(void) {
    char dir[]="build/ir-test-XXXXXX"; assert(mkdtemp(dir)!=NULL);
    char path[128]; assert(snprintf(path,sizeof(path),"%s/output.ll",dir)>0);
    static const struct { const char *source,*fragment; } positive[]={
        {"int f(int x){return x+3;}","add i32"},
        {"signed char f(signed char x){return x;}","signext i8"},
        {"unsigned short f(unsigned short x){return x;}","zeroext i16"},
        {"_Bool f(int x){return x;}","icmp ne i32"},
        {"int f(volatile int *p){return (*p)++;}","store volatile i32"},
        {"void f(volatile unsigned short *p){++*p;}","store volatile i16"},
        {"long long f(unsigned x){return x;}","zext i32"},
        {"long long f(int x){return x;}","sext i32"},
        {"int f(unsigned x){return x>>2;}","lshr i32"},
        {"int f(int x){return x>>2;}","ashr i32"},
        {"unsigned f(unsigned x,unsigned y){return x/y+x%y;}","urem i32"},
        {"double f(int x){float a=x;return a+3;}","sitofp i32"},
        {"int f(double x){return (int)x;}","fptosi double"},
        {"int *f(int *p,int n){return p+n;}","getelementptr i8"},
        {"int f(int *p,int *q){return p-q;}","ptrtoint ptr"},
        {"int f(int *p){return p==0;}","icmp eq ptr"},
        {"int f(int x,int y){return x && y;}","br i1"},
        {"int f(int x,int y){return x || y;}","br i1"},
        {"int f(int x,int y){return x?y:3;}","br i1"},
        {"int f(int x){int s=0;for(int k=0;k<x;k++){if(k==2)continue;s+=k;if(s>9)break;}return s;}","br label"},
        {"int f(int x){while(x>0)--x;do{++x;}while(x<3);return x;}","icmp slt"},
        {"int f(int x){switch(x){case 1:x++;case 2:return x;default:return 0;}}","switch i32"},
        {"int f(int x){goto done;x++;done:return x;}","br label"},
        {"int a; int *p=&a; int *q=&a+1;","getelementptr (i8"},
        {"int *p=(int*)0x40000000;","inttoptr (i32"},
        {"int a[3]={[2]=5};","\\05\\00\\00\\00"},
        {"char a[3]=\"abc\";","[3 x i8] c\"abc\""},
        {"const unsigned char a[1]={3};","constant [1 x i8]"},
        {"int f(void){char a[3]=\"abc\";return a[2];}","llvm.memcpy.inline"},
        {"struct S{int x,y;}; struct S f(struct S x){return x;}","sret([8 x i8])"},
        {"struct S{char x[3];}; struct S f(struct S x){return x;}","define dso_local i32"},
        {"struct S{long long x;int y;}; int f(struct S x){return x.y;}","[2 x i64] %arg0"},
        {"struct S{char x[64];}; int f(struct S x){return x.x[0];}","[16 x i32] %arg0"},
        {"struct S{char x[65];}; int f(struct S x){return x.x[0];}","ptr byval([65 x i8]) align 4 %arg0"},
        {"struct S{int x,y;};int f(int n){struct S a={1,2},b={3,4};return (n?a:b).x;}","llvm.memcpy.inline"},
        {"struct S{int x,y;};int f(void){return ((struct S){.y=2,.x=1}).y;}","getelementptr i8"},
        {"int f(void){static int x;return ++x;}","internal global"},
        {"static int f(void){return 0;}","define internal i32 @\"f\""},
        {"__attribute__((section(\".vectors\"),used)) int x=2;","section \".vectors\""},
        {"__attribute__((section(\".\\x74\"))) int f(void){return 0;}","section \".t\""},
        {"int f(int x){return _Generic(x,int:x+2,default:0);}","add i32"},
        {"int f(int x){return _Generic(&f,int(*)(int):x+2,default:0);}","add i32"},
        {"int f(int x){_Generic(x,int:x,default:x)=3;return x;}","store i32 3"},
        {"void f(void){__asm__ volatile(\"nop\":::\"memory\");}","~{memory}"},
        {"void f(void){__asm__ volatile(\"nop\":::\"mem\" \"ory\");}","~{memory}"},
        {"void f(void){__asm__(\"dollar$label: nop\");}","dollar$$label: nop"},
        {"struct S{int x;};void f(volatile struct S *s){(void)*s;}","i32 4, i1 true"},
        {"struct S{int x;};void f(void){volatile struct S s={3};}","store volatile i32"},
        {"int f(void){int a[2]={1,2};return sizeof(a)+_Alignof(int);}","ret i32 12"},
        {"struct S{int x,y;}; struct S a[1]={[0].x=5,[0]={.y=7}};","c\"\\00\\00\\00\\00\\07"},
        {"union S{int x;unsigned char b[4];}; union S a={.x=7};","\\07\\00\\00\\00"},
        {"extern int f(int,...); int g(void){return f(2,3);}","call i32 (i32"},
        {"int f(int (*g)(int),int x){return g(x);}","call i32 %"},
    };
    global_storage_tests(path);
    for(size_t k=0;k<sizeof(positive)/sizeof(*positive);++k) fixture(positive[k].source,positive[k].fragment,1,path);
    const char *negative[]={
        "void f(int x){__asm__(\"nop\":\"=r\"(x));}",
        "void f(int x){__asm__(\"nop\"::\"r\"(x));}",
        "void f(void){__asm__(\"nop\":::\"r0\");}",
        "void f(void){__asm__ goto(\"nop\"::::done);done:;}",
        "int f(void){return missing;}",
        "int f(void){break;}",
        "int f(void){goto missing;}",
        "int f(void){return 1.5;}",
        "struct S{int x:2;};",
        "void f(int n){int x[n];}",
        "int a; unsigned address=(unsigned)&a;",
        "struct A{long long x __attribute__((aligned(16)));};",
        "union U{int x;char y;}; union U u={.x=4,.y=1};",
        "union U{int x;char y;};struct S{union U u;};struct S s={.u.x=4,.u.y=1};",
        "struct S; struct S f(void);",
        "char oversized[16777217];",
    };
    for(size_t k=0;k<sizeof(negative)/sizeof(*negative);++k) fixture(negative[k],NULL,0,path);
    const char seed[]="int f(int x){return (x+3)*2;}";
    for(size_t k=0;k<sizeof(seed)-1;++k) {
        char mutated[sizeof(seed)]; memcpy(mutated,seed,sizeof(seed)); mutated[k]^=64;
        fixture(mutated,NULL,-1,path);
    }
    char input[128],alias[128];
    assert(snprintf(input,sizeof(input),"%s/input.i",dir)>0);
    assert(snprintf(alias,sizeof(alias),"%s/alias.i",dir)>0);
    save(input,"int f(void){return missing;}"); save(path,"preserved");
    char *args[]={"badge-ir",input,"-o",path,NULL};
    assert(badge_ir_main(4,args)==1);
    char preserved[16]={0}; FILE *f=fopen(path,"rb"); assert(f!=NULL);
    assert(fread(preserved,1,sizeof(preserved),f)==9 && fclose(f)==0 && strcmp(preserved,"preserved")==0);
    save(input,"int f(void){return 3;}");
    args[3]=input; assert(badge_ir_main(4,args)==1);
    assert(link(input,alias)==0); args[3]=alias; assert(badge_ir_main(4,args)==1);
    assert(unlink(alias)==0 && symlink("input.i",alias)==0); assert(badge_ir_main(4,args)==1);
    assert(unlink(alias)==0 && unlink(input)==0);
    assert(badge_ir_main(1,args)==2);
    assert(unlink(path)==0 && rmdir(dir)==0);
    printf("badge-ir: %zu %spositive, %zu negative and %zu mutation sanitizer fixtures; CLI preservation/alias checks PASS\n",
        sizeof(positive)/sizeof(*positive),badge_test_oracles_enabled()?"LLVM-verified ":"owned ",
        sizeof(negative)/sizeof(*negative),sizeof(seed)-1);
    return 0;
}
