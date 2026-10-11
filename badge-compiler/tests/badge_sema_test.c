#define BADGE_PARSE_LIBRARY
#include "badge_parse.c"
#define main badge_sema_main
#include "badge_sema.c"
#undef main
#include <assert.h>
#include <sys/wait.h>
#include "test_mode.h"

static void sem_test_save(const char *path, const char *source) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL && fwrite(source, 1, strlen(source), f) == strlen(source));
    assert(fclose(f) == 0);
}
static void sem_test_preserved(const char *path) {
    char bytes[16] = {0};
    FILE *f = fopen(path, "rb");
    assert(f != NULL && fread(bytes, 1, sizeof(bytes), f) == 9);
    assert(fclose(f) == 0 && strcmp(bytes, "preserved") == 0);
}
static int sem_test_clang(const char *clang, const char *input, const char *log) {
    pid_t pid = fork(); assert(pid >= 0);
    if (pid == 0) {
        assert(freopen(log, "wb", stderr) != NULL);
        execlp(clang, clang, "--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb",
            "-std=c11", "-ffreestanding", "-Werror", "-Wno-unused-value", "-Wno-shift-op-parentheses",
            "-Wno-deprecated-non-prototype", "-Wno-multichar",
            "-x", "cpp-output", "-fsyntax-only", input, (char *)NULL);
        _exit(127);
    }
    int status; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
    assert(WEXITSTATUS(status) != 127); return WEXITSTATUS(status);
}
static parser_t *sem_test_fixture(const char *text, node_t **root) {
    parser_t *p = calloc(1, sizeof(*p)); assert(p != NULL);
    p->tool = "badge-sema";
    if (setjmp(p->failure) != 0) { fprintf(stderr, "unexpected semantic fixture failure\n"); abort(); }
    *root = parse_source(p, text, strlen(text), "fixture.i");
    sem_process(p, *root);
    return p;
}
static void sem_test_release(parser_t *p) { parse_destroy(p); free(p); }
static node_t *sem_test_find(node_t *n, const char *kind, const char *text) {
    if (sem_kind(n, kind) && (text == NULL || (n->text != NULL && strcmp(n->text, text) == 0))) return n;
    for (node_t *c = n->first; c != NULL; c = c->next) {
        node_t *found = sem_test_find(c, kind, text); if (found != NULL) return found;
    }
    return NULL;
}
static void sem_test_layout_oracle(const char *clang) {
    char source[8192];
    FILE *input = fopen("tests/fixtures/sema_oracle_fixture.c", "rb");
    assert(input != NULL);
    size_t length = fread(source, 1, sizeof(source) - 1, input);
    assert(!ferror(input) && fgetc(input) == EOF && fclose(input) == 0);
    source[length] = '\0';
    node_t *root;
    parser_t *p = sem_test_fixture(source, &root);
    pid_t pid = fork(); assert(pid >= 0);
    if (pid == 0) {
        execlp(clang, clang, "--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb",
            "-std=c11", "-ffreestanding", "-Werror", "-O2", "-S", "-emit-llvm",
            "tests/fixtures/sema_oracle_fixture.c", "-o", "build/sema-layout-reference.ll", (char *)NULL);
        _exit(127);
    }
    int status; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    FILE *ir = fopen("build/sema-layout-reference.ll", "rb"); assert(ir != NULL);
    unsigned comparisons = 0;
    char line[2048];
    while (fgets(line, sizeof(line), ir) != NULL) {
        if (strncmp(line, "@oracle_", 8) != 0) continue;
        char *space = strchr(line, ' '); assert(space != NULL);
        *space = '\0';
        const char *name = line + 1;
        node_t *item = NULL;
        for (node_t *decl = root->first; decl != NULL; decl = decl->next) {
            if (!sem_kind(decl, "declaration")) continue;
            for (node_t *d = decl->first->next; d != NULL; d = d->next)
                if (strcmp(sem_decl_name(d->first), name) == 0) item = d;
        }
        assert(item != NULL);
        sem_info_t *value = item->first->next->semantic;
        symbol_t *symbol = ((sem_info_t *)item->semantic)->symbol;
        assert(value->constant && sem_integer(value->type));
        uint64_t expected = sem_convert_bits(value->type, symbol->type, value->value);
        char *global = strstr(space + 1, "global i"); assert(global != NULL);
        char *number = strchr(global + strlen("global "), ' '); assert(number != NULL);
        char *end;
        errno = 0; uint64_t actual = strtoull(number + 1, &end, 10);
        assert(errno != ERANGE && end != number + 1 && *end == ',');
        if ((actual & sem_mask(symbol->type)) != expected) {
            fprintf(stderr, "LLVM mismatch for %s: ours=%" PRIu64 ", LLVM=%" PRIu64 "\n", name, expected, actual);
            abort();
        }
        ++comparisons;
    }
    assert(!ferror(ir) && fclose(ir) == 0 && comparisons == 17);
    assert(host_write_file("badge-sema-test", "build/sema-layout-ours.json",
                          (const unsigned char *)p->output, p->output_size));
    sem_test_release(p);
    printf("badge-sema: %u/17 LLVM target-layout/constant probes match\n", comparisons);
}

int main(void) {
    char dir[] = "build/sema-test-XXXXXX"; assert(mkdtemp(dir) != NULL);
    char input[128], output[128], log[128], alias[128];
    assert(snprintf(input, sizeof(input), "%s/input.i", dir) > 0);
    assert(snprintf(output, sizeof(output), "%s/output.json", dir) > 0);
    assert(snprintf(log, sizeof(log), "%s/clang.log", dir) > 0);
    assert(snprintf(alias, sizeof(alias), "%s/alias.i", dir) > 0);
    const char *clang = badge_test_tool("SEMA_TEST_CLANG");
    char *args[] = {"badge-sema", input, "-o", output, NULL};
    const char *valid[] = {
        "", "int x;", "int x; extern int x; int x=3;",
        "typedef int T; typedef int T; T x;",
        "typedef int T; int f(int T){return T;} T x;",
        "int f(int x){ {int x=2; x++;}return x;}",
        "int f(int); int f(int x){return x;}",
        "int f(); int f(int);", "void f(void); int (*p)(int);",
        "typedef int A[]; A a={1,2}; A b={1,2,3};",
        "typedef int A[3]; const A a={1,2,3};",
        "typedef int (*F)(int); int f(int x){return x;} F fp=f;",
        "struct S; typedef const struct S CS; struct S{int x;}; CS a={1};",
        "struct S{char a; short b; int c; long long d;}; _Static_assert(sizeof(struct S)==16,\"layout\");",
        "struct S{int x;struct S *next;}; struct S s={.x=1,.next=0};",
        "struct S{int x;}; int f(void){struct S; struct S *p=0;return p==0;}",
        "union U{char x[9];long long y;}; _Static_assert(sizeof(union U)==16,\"union\");",
        "struct S{int n;char data[];}; _Static_assert(sizeof(struct S)==4,\"flex\");",
        "enum E{A=1,B=A+2}; int x=B;",
        "enum E{A=0}; _Static_assert(((enum E)0-1)>0,\"unsigned enum\");",
        "enum E{A=-1}; _Static_assert(((enum E)0-1)<0,\"signed enum\");",
        "_Static_assert((char)255==255,\"unsigned char\");",
        "_Static_assert((signed char)255==-1,\"sign extension\");",
        "_Static_assert(0xffffffffu+1u==0,\"wrap\");",
        "_Static_assert((unsigned long)-1==0xffffffffu,\"long width\");",
        "_Static_assert((1ull<<40)>>39==2,\"64-bit shift\");",
        "_Static_assert(sizeof(long)==4 && sizeof(void*)==4 && sizeof(long double)==8,\"ABI\");",
        "_Static_assert(0&&(1/0) || 1,\"short circuit\");",
        "_Static_assert(1 ? 1 : (1/0),\"conditional\");",
        "int a[]={1,2,3}; _Static_assert(sizeof(a)==12,\"infer\");",
        "char s[]=\"a\\0b\"; _Static_assert(sizeof(s)==4,\"string size\");",
        "char s[3]=\"abc\"; const char *p=\"x\" \"y\";",
        "struct S{int x;int a[2];}; struct S s={.a[1]=2,.x=1};",
        "int a[4]={[3]=7}; int *p=&a[2];",
        "int f(const int *p){return *p;} int g(void){int x=1;return f(&x);}",
        "void *p; int *q; void f(void){p=q;q=p;}",
        "int f(int x){return _Generic(x,int:1,default:2);}",
        "int f(int x){_Generic(x,int:x,default:x)=2;return x;}",
        "_Alignas(8) int a; _Static_assert(_Alignof(int)==4,\"type alignment\");",
        "void f(void){for(int i=0;i<2;++i){if(i)continue;else break;}}",
        "int f(int x){switch(x){case 1:return 1;default:break;}goto done;done:return 0;}",
        "void f(int *p){__asm__(\"\" : \"+r\"(*p) : \"r\"(*p) : \"memory\");}",
        "void f(void){__asm__ goto(\"\" : : : : done);done:;}",
        "static void f(void) __attribute__((noreturn)); static void f(void){for(;;){}}",
        "__attribute__((used,section(\".data\"))) static int a=1;",
        "void f(void){extern int x; x=1;} int x;",
        "int f(int a[static const 2]){return a[0];}",
        "struct S{int a;}; int f(void){struct S s={1};return ((struct S){.a=s.a}).a;}",
        "float f(float x){return x+x;}", "static float x=1;",
        "int (*factory(int x))(int){return 0;}"
        , "int x; int *p=0 ? &x : 0;"
        , "enum E{X=0}; _Static_assert(_Generic((enum E)0,unsigned int:1,default:0),\"enum compatible\");"
        , "enum E{X=0}; unsigned int*p;enum E*q;void f(void){p=q;q=p;}"
    };
    for (size_t k = 0; k < sizeof(valid)/sizeof(valid[0]); ++k) {
        sem_test_save(input, valid[k]);
        if (badge_sema_main(4,args) != 0) { fprintf(stderr,"valid %zu rejected: %s\n",k,valid[k]); abort(); }
        if (clang != NULL && sem_test_clang(clang,input,log) != 0) {
            fprintf(stderr,"Clang rejected valid %zu; see %s\n",k,log); abort();
        }
    }
    const char *invalid[] = {
        "int f(void){return missing;}", "int f(void){g();return 0;}",
        "int f(int x){int x;return x;}", "typedef int T; char T;",
        "int x; char x;", "int f(int); int f(char);", "int f(int); int f(void){return 0;}",
        "int x=1; int x=2;", "int f(void){return 1;} int f(void){return 2;}",
        "void x;", "unsigned float x;", "long short x;", "int int x;",
        "struct S{int x;char x;};", "struct S{struct S self;};",
        "struct S{int x;}; struct S{int y;};", "struct S{int x;}; int f(struct S s){return s.no;}",
        "int f(void){int x;return x.no;}", "int f(void){return *1;}",
        "int f(void){return 1();}", "int f(int); int g(void){return f();}",
        "int f(void); int g(void){return f(1);}",
        "int f(int*); int g(void){return f(1);}",
        "int f(void){1=2;return 0;}", "int f(void){const int x=1;x=2;return x;}",
        "typedef int A[2]; void f(void){const A a={1,2};a[0]=3;}",
        "void f(const int*p){int*q=p;}", "void f(char**p){const char**q=p;}",
        "void f(void){int a[2];int b[2];a=b;}",
        "struct S{int x;}; void f(struct S a,struct S b){a+b;}",
        "int f(void){return &1 != 0;}", "int f(void){return 1[2];}",
        "void f(void){int*x=1;}",
        "void f(void){int a[2]={1,2,3};}", "int a[]={ [1]=-1,[4]=2}; int b[2]={[2]=1};",
        "char a[2]=\"abc\";", "struct S{int x;}; struct S s={.missing=1};",
        "int n; int a=n;", "struct S{int x;}; struct S s; int a=s.x;",
        "int a=*(int*)0x1234;", "int *p; int *q=p;",
        "_Static_assert(0,\"fail\");", "enum E{X=1/0};", "int a[1/0];",
        "_Static_assert(1u<<32,\"shift\");", "_Static_assert(2147483647+1,\"overflow\");",
        "int f(void){return;}", "void f(void){return 1;}",
        "int*f(void){return 1;}", "void f(void){break;}", "void f(void){continue;}",
        "void f(void){goto missing;}", "void f(void){same:;same:;}",
        "void f(void){case 1:;}", "void f(int x){switch(x){case 1:;case 1:;}}",
        "void f(unsigned x){switch(x){case -1:;case 0xffffffffu:;}}",
        "void f(int x){switch(x){default:;default:;}}",
        "void f(void){static void g(void);}", "extern int x; static int x;",
        "void f(register int x){int*p=&x;}",
        "_Alignas(2) int x;", "_Alignas(3) int x;",
        "void f(int a[const 2]){a++;}",
        "void f(void){extern int x=1;}",
        "void f(void){extern int x;} int g(void){return x;}",
        "int f(int x){return _Generic(x,int:1,int:2);}",
        "int f(int x){return _Generic(x,char:1);}",
        "int f(int x){return _Generic(x,int:1,default:2,default:3);}",
        "char c='\\q';", "char c='\\x';", "unsigned long long x=18446744073709551616ULL;"
        , "int*p=(int*)*(unsigned*)0x16;"
        , "int*p=*(int**)0x16+1;"
        , "struct S{int x;}; struct S *p; int*q=&p->x;"
        , "int *p; int *q=&*p;"
        , "struct A{const int x;};struct B{struct A a;};void f(struct B x,struct B y){x=y;}"
        , "void f(void){int x __attribute__((section(\".data\")));}"
        , "_Alignas(8) int x; _Alignas(16) int x;"
        , "void f(void){extern int x;} static int x;"
        , "enum E{X=0};int f(void){return _Generic((enum E)0,enum E:1,unsigned int:2);}"
        , "enum E{X=0};enum F{Y=0};enum E*p;enum F*q;void f(void){p=q;}"
        , "int f(void){return(enum E)1;}"
        , "struct S;struct S f(void);void g(void){f();}"
        , "struct S;extern struct S x;void f(void){x;}"
    };
    for (size_t k = 0; k < sizeof(invalid)/sizeof(invalid[0]); ++k) {
        sem_test_save(input,invalid[k]); sem_test_save(output,"preserved");
        if (badge_sema_main(4,args) != 1) { fprintf(stderr,"invalid %zu accepted: %s\n",k,invalid[k]);abort(); }
        sem_test_preserved(output);
        if (clang != NULL && sem_test_clang(clang,input,log) == 0) {
            fprintf(stderr,"Clang accepted invalid %zu; see %s\n",k,log);abort();
        }
    }
    const char *unsupported[] = {
        "double x=1.0;", "struct S{unsigned x:2;};", "_Atomic(int) x;",
        "void f(int n){int a[n];}", "struct __attribute__((packed)) S{char a;int b;};",
        "unsigned x=0; char c=L'x';", "int c='ab';", "void f(void*p){p++;}",
        "int f(){return 0;}", "static inline int f(void){return 0;}",
        "extern int x __asm__(\"renamed\");"
    };
    for (size_t k=0;k<sizeof(unsupported)/sizeof(unsupported[0]);++k) {
        sem_test_save(input,unsupported[k]);sem_test_save(output,"preserved");
        assert(badge_sema_main(4,args)==1);sem_test_preserved(output);
        if(clang!=NULL) assert(sem_test_clang(clang,input,log)==0);
    }
    node_t *root;
    parser_t *p=sem_test_fixture("struct S{char c;long long x;short y;};struct S s={0};",&root);
    sema_t *s=p->semantic_context;
    symbol_t *v=sem_lookup(s->file_scope,"s",0);
    assert(v->type->size==24 && v->type->alignment==8);
    assert(v->type->fields->next->offset==8 && v->type->fields->next->next->offset==16);
    assert(strstr(p->output,"badge-c-typed-ast-v1")!=NULL);
    sem_test_release(p);
    p=sem_test_fixture("void*f(void){return(void*)*(volatile unsigned*)0x16;}",&root);
    node_t *runtime_cast=sem_test_find(root,"cast",NULL);
    assert(!((sem_info_t*)runtime_cast->semantic)->address);
    assert(!((sem_info_t*)runtime_cast->semantic)->constant);
    sem_test_release(p);
    p=sem_test_fixture("unsigned x=(signed char)255;long l=1;unsigned y=0xffffffffu+1u;",&root);
    s=p->semantic_context;
    node_t *cast=sem_test_find(root,"cast",NULL);
    sem_info_t *ci=cast->semantic;
    assert(ci->type->kind==T_SCHAR && ci->value==255 && ci->conversion->kind==T_UINT);
    assert(sem_convert_bits(ci->type,ci->conversion,ci->value)==UINT32_MAX);
    node_t *binary=sem_test_find(root,"binary","+");
    assert(((sem_info_t*)binary->semantic)->value==0);
    assert(s->primitive[T_LONG]->size==4 && s->primitive[T_LDOUBLE]->size==8);
    sem_test_release(p);
    p=sem_test_fixture("const int a=1;int f(void){return a;}",&root);
    node_t *id=sem_test_find(root,"identifier","a");
    assert(((sem_info_t*)id->semantic)->symbol!=NULL);
    assert(((sem_info_t*)id->semantic)->type->qualifiers & Q_CONST);
    assert(((sem_info_t*)id->semantic)->conversion->qualifiers==0);
    sem_test_release(p);
    p=sem_test_fixture("volatile unsigned v;void f(void){v;*(volatile unsigned*)0x16;}",&root);
    id=sem_test_find(root,"identifier","v");
    sem_info_t *read=id->semantic;
    assert(read->lvalue && (read->type->qualifiers & Q_VOLATILE));
    assert(read->conversion->kind==T_UINT && read->conversion->qualifiers==0);
    assert(!read->constant && !sem_address_value(read));
    node_t *load=sem_test_find(root,"unary","*");
    read=load->semantic;
    assert(read->lvalue && (read->type->qualifiers & Q_VOLATILE));
    assert(read->conversion->kind==T_UINT && read->conversion->qualifiers==0);
    assert(read->address && !read->constant && !sem_address_value(read));
    sem_test_release(p);
    const char mutation[]="int f(int x){return x+1;}";
    for(size_t k=0;k<sizeof(mutation)-1;++k) {
        char changed[sizeof(mutation)];
        memcpy(changed,mutation,k);memcpy(changed+k,mutation+k+1,sizeof(mutation)-k-1);
        sem_test_save(input,changed);sem_test_save(output,"preserved");
        int result=badge_sema_main(4,args);assert(result==0||result==1);
        if(result==1)sem_test_preserved(output);
    }
    sem_test_save(input,"int x;");
    assert(link(input,alias)==0);
    char *alias_args[]={"badge-sema",input,"-o",alias,NULL};
    assert(badge_sema_main(4,alias_args)==1); assert(unlink(alias)==0);
    assert(badge_sema_main(1,args)==2);
    assert(unlink(input)==0 && unlink(output)==0);
    if(clang!=NULL) assert(unlink(log)==0);
    assert(rmdir(dir)==0);
    if(clang!=NULL) sem_test_layout_oracle(clang);
    printf("badge-sema: %zu positive, %zu negative, %zu unsupported, %zu mutations; types/layouts/conversions/file safety PASS%s\n",
        sizeof(valid)/sizeof(valid[0]),sizeof(invalid)/sizeof(invalid[0]),sizeof(unsupported)/sizeof(unsupported[0]),
        sizeof(mutation)-1,clang==NULL?"":"; Clang differential PASS");
    return 0;
}
