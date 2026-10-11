#define main badge_parse_main
#include "badge_parse.c"
#undef main
#include <assert.h>
#include <sys/wait.h>
#include "test_mode.h"

static void save(const char *path, const char *bytes, size_t size) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fwrite(bytes, 1, size, f) == size);
    assert(fclose(f) == 0);
}
static void preserved(const char *path) {
    char bytes[16] = {0};
    FILE *f = fopen(path, "rb");
    assert(f != NULL && fread(bytes, 1, sizeof(bytes), f) == 9);
    assert(fclose(f) == 0 && strcmp(bytes, "preserved") == 0);
}
static node_t *find_node(node_t *n, const char *kind, const char *text) {
    if (strcmp(n->kind, kind) == 0 && (text == NULL || (n->text != NULL && strcmp(n->text, text) == 0)))
        return n;
    for (node_t *c = n->first; c != NULL; c = c->next) {
        node_t *found = find_node(c, kind, text);
        if (found != NULL) return found;
    }
    return NULL;
}
static parser_t *fixture(const char *source, node_t **root) {
    parser_t *p = calloc(1, sizeof(*p));
    assert(p != NULL);
    if (setjmp(p->failure) != 0) { fprintf(stderr, "unexpected fixture rejection\n"); abort(); }
    *root = parse_source(p, source, strlen(source), "fixture.i");
    return p;
}
static void release(parser_t *p) { parse_destroy(p); free(p); }
static void render(parser_t *p, node_t *root) {
    if (setjmp(p->failure) != 0) { fprintf(stderr, "unexpected serialization failure\n"); abort(); }
    serialize(p, root);
}
static int clang_check(const char *clang, const char *input, const char *log) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        assert(freopen(log, "wb", stderr) != NULL);
        execlp(clang, clang, "--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb",
              "-std=c11", "-ffreestanding", "-Werror", "-Wno-shift-op-parentheses",
              "-Wno-unused-value", "-Wno-dangling-else", "-x", "cpp-output",
              "-fsyntax-only", input, (char *)NULL);
        _exit(127);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
    assert(WEXITSTATUS(status) != 127);
    return WEXITSTATUS(status);
}

int main(void) {
    char directory[] = "build/parse-test-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char input[128], output[128], link_path[128], log[128];
    assert(snprintf(input, sizeof(input), "%s/input.i", directory) > 0);
    assert(snprintf(output, sizeof(output), "%s/output.json", directory) > 0);
    assert(snprintf(link_path, sizeof(link_path), "%s/alias.i", directory) > 0);
    assert(snprintf(log, sizeof(log), "%s/clang.log", directory) > 0);
    char *args[] = {"badge-parse", input, "-o", output, NULL};
    const char *clang = badge_test_tool("PARSE_TEST_CLANG");
    const char *valid[] = {
        "", "int x;", "const unsigned long long *restrict p;",
        "typedef int T; T x; T *f(T x);",
        "typedef int T; void f(void){ int T; T=1; {typedef char T; T x;} T=2;}",
        "typedef int T; int f(int T){return sizeof(T);} T x;",
        "typedef int T; int f(int T); T x;",
        "typedef int T; void f(void){for(int T=0;T<2;++T){} T x;}",
        "typedef int T; void f(void){T:; T x; goto T;}",
        "typedef int T; struct S{int T; T x;}; T x;",
        "typedef int T; int (*f(int T))(int){return 0;} T x;",
        "typedef int T; int f(int T,int a[sizeof(T)]){return a[0]+T;}",
        "typedef int (*fn)(int, const char *); fn callbacks[2];",
        "int *a[3]; int (*b)[3]; int (* const vectors[])(void)={0};",
        "int (*factory(int n))(int); int f(int (*callback)(int),int (*p)[3]);",
        "int f(int n,int a[static const 3]); int g(int n,int a[*]);",
        "int f(int *p,int a[*p]);",
        "struct S{unsigned a:3; unsigned :2; int b;};",
        "union U{int i; char c;}; enum E{A=1,B=A+1,};",
        "struct S{int x;int y[2];}; struct S s={.y[1]=2,.x=1};",
        "struct S{int x;}; int f(void){return ((struct S){.x=7}).x;}",
        "int a[3]={[1]=2,3,}; int b[2][2]={{1,2},{3,4}};",
        "int f(int a,int b,int c){a=b=c;return a+b*c<<2;}",
        "int f(int a,int b,int c){return a?b,c:a?b:c;}",
        "int f(int a){return sizeof(a)+sizeof(int*)+_Alignof(int);}",
        "int f(int *p){int x=*p++;x+=++*p;return x+p[0];}",
        "int f(int a){if(a) if(a>1)return 1;else return 2;return 0;}",
        "int f(int a){while(a){--a;if(a==2)continue;if(a==1)break;}do{++a;}while(a<2);return a;}",
        "int f(int a){switch(a){case 1:case 2:return 3;default:break;}goto done;done:return 0;}",
        "void f(void){for(;;);}",
        "static void f(void) __attribute__((noreturn));",
        "__attribute__((used)) static int x __attribute__((section(\".data\")));",
        "void f(void){__asm__ volatile(\"dsb\" ::: \"memory\"); __asm__(\"wfi\");}",
        "int f(int x){__asm__(\"\" : [out] \"+r\"(x) : \"r\"(x) : \"memory\");return x;}",
        "void f(void){__asm__ goto(\"\" : : : : done);done:;}",
        "extern int x __asm__(\"renamed\");",
        "_Static_assert(sizeof(int)==4,\"int size\"); _Alignas(4) int x;",
        "_Atomic(int) x; _Atomic int y; _Noreturn void f(void);",
        "int f(int x){return _Generic(x,int:1,default:2);}",
        "char *s=\"a\" \"b\\n\"; char c='\\''; unsigned x=0xffULL;",
        "char *s=\"'\" \"b\"; void *w=L\"'\"; void *u=u\"'\"; void *v=U\"'\"; char *q=u8\"'\";",
        "typedef char T; int f(void){enum {T=sizeof(T)};return T;}",
        "double x=0x1.fp+2; float y=.5f; double z=1e-3L;",
        "char *s=\"\xc3\xa4\";",
        "# 0 \"<built-in>\"\n# 42 \"file.c\" 1 3\nint x;\n",
        "#line 9 \"a\\\\b\\\"c.c\"\r\nint x;\r\n",
        "# 1 \"\\303\\244.c\"\nint x;",
        "# 9\nint x;\n#line 42\nint y;",
        "int x; /* multiline\n comment */\nint y;"
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        save(input, valid[i], strlen(valid[i]));
        if (badge_parse_main(4, args) != 0) {
            fprintf(stderr, "valid fixture %zu rejected: %s\n", i, valid[i]); abort();
        }
        if (clang != NULL && clang_check(clang, input, log) != 0) {
            fprintf(stderr, "Clang rejected valid fixture %zu; see %s\n", i, log); abort();
        }
    }
    const char *invalid[] = {
        "int x", "int =1;", "int x=;", "int x=1 garbage;", "int x; trailing",
        "struct {int x;", "struct;", "enum { };", "enum E{A=,B};",
        "int f(int,);", "int f(...);", "int f(a){return a;}",
        "int (*p)(int) {}", "int x, f(void) {}", "typedef int f(void) {}",
        "int f(void){if()return 0;}", "int f(void){else;}", "int f(void){return (1;}",
        "int f(void){goto;}", "int f(void){do{}while(1)}",
        "int f(void){for(int i=0 i<1;++i);}", "int f(void){case :;}",
        "int f(void){a+b=1;}", "int f(void){return 1?2;}", "int f(void){return a.;}",
        "int f(void){return (int x)1;}", "int f(void){return f(1,);}",
        "int x[]={.a 1};", "int x[2]={[0]=};", "struct S{int x=1;};",
        "typedef int T; int f(int T,T x);",
        "void f(void){typedef int T;} T x;",
        "typedef int T; int f(int T){return (T)1;}",
        "typedef int T; int f(void){return T;}",
        "int x=09;", "int x=0x;", "int x=1lul;", "int x=1lL;", "int x=1e+;",
        "double x=0x1.2;", "int x=3foo;", "char x='';",
        "void f(void){__asm__(42);}", "void f(void){__asm__(\"\" : x);}",
        "int x __attribute__((used);",
        "# bad \"a.c\"\nint x;", "# 1 \"a.c\" 5\nint x;",
        "# 4294967296 \"a.c\"\nint x;", "# 1 \"a\\q.c\"\nint x;",
        "char *s=\"\xff\";", "/* unclosed", "\"unclosed"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        save(input, invalid[i], strlen(invalid[i])); save(output, "preserved", 9);
        if (badge_parse_main(4, args) != 1) {
            fprintf(stderr, "invalid fixture %zu accepted: %s\n", i, invalid[i]); abort();
        }
        preserved(output);
        if (clang != NULL && clang_check(clang, input, log) == 0) {
            fprintf(stderr, "Clang accepted invalid fixture %zu; see %s\n", i, log); abort();
        }
    }
    const char *unsupported[] = {
        "int f(void){return ({1;});}", "#define X 1\nint x;",
        "# 4294967295 \"a.c\"\nint x;\nint y;\n",
        "# 1 \"\\377.c\"\nint x;"
    };
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        save(input, unsupported[i], strlen(unsupported[i])); save(output, "preserved", 9);
        assert(badge_parse_main(4, args) == 1); preserved(output);
        if (clang != NULL) assert(clang_check(clang, input, log) == 0);
    }
    node_t *root;
    parser_t *p = fixture("int f(int a,int b,int c){a=b=c;return a+b*c-2;}", &root);
    node_t *assignment = find_node(root, "assignment", "=");
    assert(assignment != NULL && strcmp(assignment->first->next->kind, "assignment") == 0);
    node_t *minus = find_node(root, "binary", "-");
    assert(minus != NULL && strcmp(minus->first->text, "+") == 0);
    assert(strcmp(minus->first->first->next->text, "*") == 0);
    release(p);
    p = fixture("typedef char T; int f(void){enum {T=sizeof(T)};return T;}", &root);
    assert(find_node(root, "sizeof_type", NULL) != NULL);
    release(p);
    p = fixture("int *a[3]; int (*b)[3];", &root);
    node_t *a = root->first->first->next->first;
    node_t *b = root->first->next->first->next->first;
    assert(strcmp(a->kind, "pointer_declarator") == 0);
    assert(strcmp(a->first->kind, "array_declarator") == 0);
    assert(strcmp(b->kind, "array_declarator") == 0);
    assert(strcmp(b->first->kind, "parenthesized_declarator") == 0);
    assert(strcmp(b->first->first->kind, "pointer_declarator") == 0);
    release(p);
    p = fixture("typedef int T; int f(int T){return sizeof(T);} T x;", &root);
    assert(find_node(root, "sizeof_expression", NULL) != NULL);
    assert(find_node(root, "sizeof_type", NULL) == NULL);
    release(p);
    p = fixture("# 42 \"a.c\" 1\nint x;\n# 7 \"b.c\" 2\nint y;", &root);
    token_t *x = &p->tokens.items[find_node(root, "name", "x")->begin];
    token_t *y = &p->tokens.items[find_node(root, "name", "y")->begin];
    assert(x->line == 42 && strcmp(x->file, "a.c") == 0);
    assert(y->line == 7 && strcmp(y->file, "b.c") == 0);
    render(p, root);
    assert(strstr(p->output, "\"schema\":\"badge-c-ast-v1\"") != NULL);
    assert(strstr(p->output, "\"file\":\"a.c\",\"line\":42") != NULL);
    p->output_size = 0; p->no_locations = 1;
    render(p, root);
    node_t *other_root;
    parser_t *other = fixture("int x; int y;", &other_root);
    other->no_locations = 1; render(other, other_root);
    assert(strcmp(p->output, other->output) == 0);
    release(other);
    release(p);
    const char mutation[] = "typedef int T; int f(T x){return x+1;}";
    for (size_t i = 0; i < sizeof(mutation) - 1; ++i) {
        char changed[sizeof(mutation)];
        memcpy(changed, mutation, sizeof(mutation)); changed[i] ^= (char)0xff;
        save(input, changed, sizeof(mutation) - 1); save(output, "preserved", 9);
        int result = badge_parse_main(4, args);
        assert(result == 0 || result == 1);
        if (result == 1) preserved(output);
        memcpy(changed, mutation, i);
        memcpy(changed + i, mutation + i + 1, sizeof(mutation) - i - 1);
        save(input, changed, sizeof(mutation) - 2); save(output, "preserved", 9);
        result = badge_parse_main(4, args);
        assert(result == 0 || result == 1);
        if (result == 1) preserved(output);
        memcpy(changed, mutation, sizeof(mutation)); changed[i] = '?';
        save(input, changed, sizeof(mutation) - 1); save(output, "preserved", 9);
        result = badge_parse_main(4, args);
        assert(result == 0 || result == 1);
        if (result == 1) preserved(output);
    }
    char deep[2048];
    size_t n = 0;
    memcpy(deep + n, "int x=", 6); n += 6;
    for (unsigned i = 0; i < PARSE_MAX_DEPTH + 1; ++i) deep[n++] = '(';
    deep[n++] = '1';
    for (unsigned i = 0; i < PARSE_MAX_DEPTH + 1; ++i) deep[n++] = ')';
    deep[n++] = ';';
    save(input, deep, n); save(output, "preserved", 9);
    assert(badge_parse_main(4, args) == 1); preserved(output);
    size_t boundary_size = C_LEXER_MAX_TOKENS - 3u + 6u;
    char *boundary = malloc(boundary_size + 1);
    assert(boundary != NULL);
    memcpy(boundary, "int x;", 6);
    memset(boundary + 6, '\n', boundary_size + 1 - 6);
    save(input, boundary, boundary_size);
    assert(badge_parse_main(4, args) == 0);
    save(input, boundary, boundary_size + 1); save(output, "preserved", 9);
    assert(badge_parse_main(4, args) == 1); preserved(output);
    free(boundary);
    save(input, "int x;\0int y;", 13);
    assert(badge_parse_main(4, args) == 1); preserved(output);
    save(input, "int x;", 6);
    assert(link(input, link_path) == 0);
    char *alias[] = {"badge-parse", input, "-o", link_path, NULL};
    assert(badge_parse_main(4, alias) == 1);
    assert(unlink(link_path) == 0);
    assert(symlink("input.i", link_path) == 0);
    assert(badge_parse_main(4, alias) == 1);
    assert(unlink(link_path) == 0);
    assert(mkdir(link_path, 0700) == 0);
    assert(badge_parse_main(4, alias) == 1);
    assert(rmdir(link_path) == 0);
    char *missing[] = {"badge-parse", link_path, "-o", output, NULL};
    assert(badge_parse_main(4, missing) == 1); preserved(output);
    assert(badge_parse_main(1, args) == 2);
    assert(unlink(input) == 0 && unlink(output) == 0);
    if (clang != NULL) assert(unlink(log) == 0);
    assert(rmdir(directory) == 0);
    printf("badge-parse: %zu positive, %zu negative, %zu unsupported, %zu mutations; AST shapes, locations, token boundary, limits and file safety PASS%s\n",
           sizeof(valid) / sizeof(valid[0]), sizeof(invalid) / sizeof(invalid[0]),
           sizeof(unsupported) / sizeof(unsupported[0]),
           (sizeof(mutation) - 1) * 3, clang == NULL ? "" : "; Clang differential PASS");
    return 0;
}
