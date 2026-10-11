#define main badge_cpp_main
#include "badge_cpp.c"
#undef main
#include <assert.h>

static void save(const char *path, const char *bytes, size_t length) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(bytes, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static void matches(const char *path, const char *expected) {
    char buffer[4096];
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    size_t length = fread(buffer, 1, sizeof(buffer) - 1, file);
    assert(!ferror(file));
    assert(fclose(file) == 0);
    buffer[length] = '\0';
    if (strcmp(buffer, expected) != 0) {
        fprintf(stderr, "expected:\n%s\nactual:\n%s\n", expected, buffer);
        assert(0);
    }
}

int main(void) {
    char directory[] = "build/cpp-test-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char source[128], output[128], normalized[128], header[128], once[128], cycle[128], bad[128];
    assert(snprintf(source, sizeof(source), "%s/input.c", directory) > 0);
    assert(snprintf(output, sizeof(output), "%s/output.i", directory) > 0);
    assert(snprintf(normalized, sizeof(normalized), "%s/output.tokens", directory) > 0);
    assert(snprintf(header, sizeof(header), "%s/header.h", directory) > 0);
    assert(snprintf(once, sizeof(once), "%s/once.h", directory) > 0);
    assert(snprintf(cycle, sizeof(cycle), "%s/cycle.h", directory) > 0);
    assert(snprintf(bad, sizeof(bad), "%s/bad.h", directory) > 0);
    const char *guarded = "#ifndef GUARD\n#define GUARD\n#define VALUE 42\nint header;\n#endif\n";
    save(header, guarded, strlen(guarded));
    const char *once_text = "#pragma once\nint once;\n";
    save(once, once_text, strlen(once_text));
    const char *cycle_text = "#include \"cycle.h\"\n";
    save(cycle, cycle_text, strlen(cycle_text));
    save(bad, "#endif\n", 7);
    char *args[] = {"badge-cpp", "-I", directory, "-DCLI=7", "-UREMOVED",
                    source, "-o", output, NULL};
    char *token_args[] = {"badge-cpp", "--tokens", output, "-o", normalized, NULL};
    const struct { const char *input, *expected; } valid[] = {
        {"#define N 3\nN + N\n", "3\n+\n3\n"},
        {"#define A B\n#define B 9\nA A\n", "9\n9\n"},
        {"#define R R\nR\n", "R\n"},
        {"#define A B\n#define B A\nA B\n", "A\nB\n"},
        {"#define F(x) ((x)+1)\nF(F(2))\n", "(\n(\n(\n(\n2\n)\n+\n1\n)\n)\n+\n1\n)\n"},
        {"#define F(x) x+x\n#define ALIAS F\nALIAS(3) ALIAS(4)\n", "3\n+\n3\n4\n+\n4\n"},
        {"#define F(x,y) x+y\nF((1,2), 3)\n", "(\n1\n,\n2\n)\n+\n3\n"},
        {"#define F(x) x\nF\n(8)\n", "8\n"},
        {"#define ZERO() 0\nZERO()\n", "0\n"},
        {"#define EMPTY(x) x\nEMPTY()\n", ""},
        {"#define IGNORE(x) 4\n#define F(x) x\nIGNORE(F(1,2))\n", "4\n"},
        {"#define P (x)\nP\n", "(\nx\n)\n"},
        {"#define F(x) x\nF\n", "F\n"},
        {"#define N 2\n#undef N\n#define N 3\nN\n", "3\n"},
        {"#define N 1\n#define N 1\nN\n", "1\n"},
        {"#define N 1\\\n + 2\nN\n", "1\n+\n2\n"},
        {"#define N 3\r\nN\r\n", "3\n"},
        {"#define N /* spanning\n comment */ 3\nN\n", "3\n"},
        {"int x; // continued\\\n#error ignored\nint y;\n", "int\nx\n;\nint\ny\n;\n"},
        {"int/**/x; // removed\n\"// kept\" '\\'' u8\"/*kept*/\"\n", "int\nx\n;\n\"// kept\"\n'\\''\nu8\"/*kept*/\"\n"},
        {"a + + b; a - - b; 1e+2 .5\n", "a\n+\n+\nb\n;\na\n-\n-\nb\n;\n1e+2\n.5\n"},
        {"#include \"header.h\"\n#include <header.h>\nVALUE\n", "int\nheader\n;\n42\n"},
        {"#define H \"header.h\"\n#include H\nVALUE\n", "int\nheader\n;\n42\n"},
        {"#include \"once.h\"\n#include \"./once.h\"\n", "int\nonce\n;\n"},
        {"#if defined(CLI) && !defined REMOVED && CLI == 7\nok\n#else\nbad\n#endif\n", "ok\n"},
        {"#ifdef CLI\n#ifndef REMOVED\nok\n#endif\n#endif\n", "ok\n"},
        {"#if 0\n#error inactive\n#include \"missing.h\"\n#define F(...) __VA_ARGS__\n#if 1/0\nbad\n#endif\n#elif 2*3==6\nok\n#else\nbad\n#endif\n", "ok\n"},
        {"#if (1+2*3)==7 && (8>>1)==4 && (1<<3)==8 && (5&3)==1 && (5^3)==6 && (4|2)==6\nok\n#endif\n", "ok\n"},
        {"#if 0 && (1/0)\nbad\n#elif 1 || (1/0)\nok\n#endif\n", "ok\n"},
        {"#if (1 ? 4 : 1/0)==4 && (0 ? 1/0 : 5)==5\nok\n#endif\n", "ok\n"},
        {"#if -1 < 1U\nbad\n#else\nok\n#endif\n", "ok\n"},
        {"#if (1 ? -1 : 1 + 1U) < 0\nbad\n#else\nok\n#endif\n", "ok\n"},
        {"#if (1 ? -1 : !1U) < 0\nok\n#else\nbad\n#endif\n", "ok\n"},
        {"#if 0xffffffffffffffffULL > 0 && 01u==1 && 1lu==1 && 1LLu==1\nok\n#endif\n", "ok\n"},
        {"#if ~0U > 0 && (-8 >> 2)==-2 && 9/2==4 && 9%2==1\nok\n#endif\n", "ok\n"},
        {"#define L __LINE__\nL\n", "2\n"},
        {"#define L __LINE__\n\nL\\\n + __LINE__\n", "3\n+\n4\n"},
        {"__UINT8_TYPE__ __INT64_TYPE__ __UINTPTR_TYPE__\n", "unsigned\nchar\nlong\nlong\nint\nunsigned\nint\n"},
        {"#if __STDC_HOSTED__==0 && __STDC_VERSION__==201112L && __SIZEOF_POINTER__==4\nok\n#endif\n", "ok\n"},
        {"#define EMPTY\nEMPTY\n", ""},
        {"", ""}
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        save(source, valid[i].input, strlen(valid[i].input));
        assert(badge_cpp_main(8, args) == 0);
        assert(badge_cpp_main(5, token_args) == 0);
        matches(normalized, valid[i].expected);
    }
    const char *invalid[] = {
        "#define F(x) #x\n", "#define F(x,y) x##y\n", "#define F(...) 0\n",
        "#define F(x,x) x\n", "#define F(x x\n", "#define N 1\n#define N 2\n",
        "#define F(x) x\nF(1,2)\n", "#define F(x) x\nF(1\n",
        "#define F(x,y) x\nF()\n", "#if\n#endif\n", "#ifdef\n#endif\n",
        "#if 1\n", "#endif\n", "#else\n", "#if 1\n#else\n#else\n#endif\n",
        "#if 1\n#else\n#elif 1\n#endif\n", "#if 1\n#endif extra\n",
        "#if 1/0\n#endif\n", "#if 1<<64\n#endif\n",
        "#if 9223372036854775807 + 1\n#endif\n", "#if (1\n#endif\n",
        "#if 1 ? 2\n#endif\n", "#if 1.0\n#endif\n", "#if 'x'\n#endif\n",
        "#if 1lul\n#endif\n", "#if 1lL\n#endif\n",
        "#if 9223372036854775808\n#endif\n",
        "#include \"missing.h\"\n", "#include \"cycle.h\"\n",
        "#if 1\n#include \"bad.h\"\n#endif\n", "#include VALUE\n",
        "#pragma unknown\n", "#line 9\n", "#error requested failure\n",
        "/* unclosed\n", "\"unclosed\n", "\\u0041\n", "<% int x; %>\n",
        "__COUNTER__\n", "__DATE__\n", "_Pragma(\"once\")\n",
        "#define D defined\n#if D(X)\n#endif\n"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        save(source, invalid[i], strlen(invalid[i]));
        save(output, "preserved", 9);
        assert(badge_cpp_main(8, args) == 1);
        matches(output, "preserved");
    }
    const char mutation[] = "#define INC(x) ((x)+1)\nint x=INC(2);\n";
    for (size_t i = 0; i < sizeof(mutation) - 1; ++i) {
        char changed[sizeof(mutation)];
        memcpy(changed, mutation, sizeof(mutation));
        changed[i] ^= (char)0xff;
        save(source, changed, sizeof(mutation) - 1);
        int result = badge_cpp_main(8, args);
        assert(result == 0 || result == 1);
    }
    const char *alias_source = "#include \"header.h\"\n__FILE__\n";
    save(source, alias_source, strlen(alias_source));
    args[7] = header;
    assert(badge_cpp_main(8, args) == 1);
    matches(header, guarded);
    args[7] = source;
    assert(badge_cpp_main(8, args) == 1);
    args[7] = output;
    assert(badge_cpp_main(1, args) == 2);
    const char *assembly_text="#ifndef __ASSEMBLER__\n#error missing assembly mode\n#endif\n"
        "#define WORD .word\n.syntax unified\n.cpu cortex-m33\n.thumb\n"
        ".section .text.entry,\"ax\",%progbits\nWORD 3\n"
        "#define NAME label_suffix\nNAME:\n";
    save(source,assembly_text,strlen(assembly_text));
    char *assembly_args[]={"badge-cpp","--assembly",source,"-o",output,NULL};
    assert(badge_cpp_main(5,assembly_args)==0);
    FILE *assembly_file=fopen(output,"rb");assert(assembly_file);
    char assembly_buffer[4096]={0};assert(fread(assembly_buffer,1,sizeof(assembly_buffer)-1,assembly_file)>0&&fclose(assembly_file)==0);
    assert(strstr(assembly_buffer,".syntax unified")&&strstr(assembly_buffer,".cpu cortex-m33")&&
           strstr(assembly_buffer,".section .text.entry,\"ax\",%progbits")&&strstr(assembly_buffer,".word 3")&&strstr(assembly_buffer,"label_suffix:"));
    assert(badge_cpp_main(8,args)==1);
    size_t boundary_size = C_LEXER_MAX_TOKENS - 3u + 6u;
    char *boundary = malloc(boundary_size + 1);
    assert(boundary != NULL);
    memcpy(boundary, "int x;", 6);
    memset(boundary + 6, '\n', boundary_size + 1 - 6);
    save(source, boundary, boundary_size);
    assert(badge_cpp_main(8, args) == 0);
    assert(badge_cpp_main(5, token_args) == 0);
    matches(normalized, "int\nx\n;\n");
    char *boundary_args[] = {"badge-cpp", "--tokens", source, "-o", normalized, NULL};
    assert(badge_cpp_main(5, boundary_args) == 0);
    matches(normalized, "int\nx\n;\n");
    save(source, boundary, boundary_size + 1);
    save(output, "preserved", 9);
    assert(badge_cpp_main(8, args) == 1);
    matches(output, "preserved");
    save(normalized, "preserved", 9);
    assert(badge_cpp_main(5, boundary_args) == 1);
    matches(normalized, "preserved");
    free(boundary);
    assert(unlink(source) == 0 && unlink(output) == 0 && unlink(normalized) == 0 &&
           unlink(header) == 0 && unlink(once) == 0 && unlink(cycle) == 0 && unlink(bad) == 0);
    assert(rmdir(directory) == 0);
    printf("badge-cpp: %zu positive, %zu negative, %zu mutation and input-preservation tests; token boundary, assembly token adjacency and __ASSEMBLER__ PASS\n",
           sizeof(valid) / sizeof(valid[0]), sizeof(invalid) / sizeof(invalid[0]), sizeof(mutation) - 1);
    return 0;
}
