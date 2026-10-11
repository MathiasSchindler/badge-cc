#ifndef BADGE_COMPILER_C_LEXER_H
#define BADGE_COMPILER_C_LEXER_H

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C_LEXER_MAX_TOKENS 250000u
#define C_LEXER_MAX_MACROS 512u

typedef enum { IDENT, NUMBER, LITERAL, PUNCT, NEWLINE } token_kind_t;
typedef struct {
    char *text;
    const char *file;
    unsigned line;
    token_kind_t kind;
    int space;
    uint64_t hide[C_LEXER_MAX_MACROS / 64u];
} token_t;
typedef struct { token_t *items; size_t count, capacity; } tokens_t;
typedef struct {
    void *context;
    void *(*allocate)(void *, size_t);
    void (*error)(void *, const char *, unsigned, const char *);
    const char *file;
    unsigned line, end_line;
} c_lexer_t;

static void c_lex_error(c_lexer_t *c, const char *message) {
    c->error(c->context, c->file, c->line, message);
    abort();
}

static void c_lex_push(c_lexer_t *c, tokens_t *v, token_t token) {
    if (v->count == C_LEXER_MAX_TOKENS) c_lex_error(c, "token count limit exceeded");
    if (v->count == v->capacity) {
        size_t capacity = v->capacity == 0 ? 16 : v->capacity * 2;
        token_t *items = c->allocate(c->context, capacity * sizeof(*items));
        if (v->count != 0) memcpy(items, v->items, v->count * sizeof(*items));
        v->items = items;
        v->capacity = capacity;
    }
    v->items[v->count++] = token;
}

static int c_identifier_start(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static int c_identifier_part(unsigned char ch) {
    return c_identifier_start(ch) || (ch >= '0' && ch <= '9');
}

static inline tokens_t c_lex(c_lexer_t *c, const char *source, size_t length, const char *file) {
    c->file = file;
    c->line = 1;
    char *spliced = c->allocate(c->context, length + 1);
    unsigned *lines = c->allocate(c->context, (length + 1) * sizeof(*lines));
    size_t size = 0;
    unsigned line = 1;
    for (size_t i = 0; i < length; ++i) {
        c->line = line;
        if (source[i] == '\0') c_lex_error(c, "NUL byte in source");
        if (source[i] == '?' && i + 2 < length && source[i + 1] == '?' &&
            strchr("=/'()!<>-", source[i + 2]) != NULL)
            c_lex_error(c, "trigraphs are unsupported");
        if (source[i] == '\\' && i + 1 < length) {
            size_t next = i + 1;
            if (source[next] == '\r' && next + 1 < length && source[next + 1] == '\n') ++next;
            if (source[next] == '\n') { i = next; ++line; continue; }
        }
        char ch = source[i];
        if (ch == '\r') {
            if (i + 1 < length && source[i + 1] == '\n') ++i;
            ch = '\n';
        }
        lines[size] = line;
        spliced[size++] = ch;
        if (ch == '\n') ++line;
    }
    spliced[size] = '\0';
    c->end_line = line;
    tokens_t result = {0};
    int space = 0;
    for (size_t i = 0; i < size;) {
        c->line = lines[i];
        unsigned char ch = (unsigned char)spliced[i];
        if (ch == '\n') {
            c_lex_push(c, &result, (token_t){.text = "\n", .file = file,
                       .line = lines[i], .kind = NEWLINE});
            ++i; space = 1; continue;
        }
        if (isspace(ch)) { ++i; space = 1; continue; }
        if (ch == '/' && i + 1 < size && spliced[i + 1] == '/') {
            i += 2;
            while (i < size && spliced[i] != '\n') ++i;
            space = 1; continue;
        }
        if (ch == '/' && i + 1 < size && spliced[i + 1] == '*') {
            i += 2;
            int closed = 0;
            while (i < size) {
                if (i + 1 < size && spliced[i] == '*' && spliced[i + 1] == '/') {
                    i += 2; closed = 1; break;
                }
                ++i;
            }
            if (!closed) c_lex_error(c, "unterminated block comment");
            space = 1; continue;
        }
        size_t start = i, prefix = 0;
        token_kind_t kind;
        if ((ch == 'L' || ch == 'u' || ch == 'U') && i + 1 < size &&
            (spliced[i + 1] == '"' || spliced[i + 1] == '\'')) prefix = 1;
        if (ch == 'u' && i + 2 < size && spliced[i + 1] == '8' && spliced[i + 2] == '"') prefix = 2;
        if (ch == '"' || ch == '\'' || prefix != 0) {
            i += prefix;
            char quote = spliced[i++];
            int closed = 0;
            while (i < size) {
                if (spliced[i] == '\n') c_lex_error(c, "newline in string/character literal");
                if (spliced[i] == quote) { ++i; closed = 1; break; }
                if (spliced[i] == '\\' && ++i == size) break;
                ++i;
            }
            if (!closed) c_lex_error(c, "unterminated string/character literal");
            kind = LITERAL;
        } else if (c_identifier_start(ch)) {
            ++i;
            while (i < size && c_identifier_part((unsigned char)spliced[i])) ++i;
            kind = IDENT;
        } else if (isdigit(ch) || (ch == '.' && i + 1 < size && isdigit((unsigned char)spliced[i + 1]))) {
            ++i;
            while (i < size) {
                unsigned char next = (unsigned char)spliced[i];
                if (c_identifier_part(next) || next == '.' ||
                    ((next == '+' || next == '-') && strchr("eEpP", spliced[i - 1]) != NULL)) ++i;
                else break;
            }
            kind = NUMBER;
        } else {
            static const char *const punctuation[] = {
                "<<=", ">>=", "...", "##", "->", "++", "--", "<<", ">>", "<=", ">=",
                "==", "!=", "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|="
            };
            if (ch >= 128 || ch == '\\' || strchr("[](){}.,;:?~!+-*/%<>=&^|#", ch) == NULL) {
                char message[80];
                snprintf(message, sizeof(message), "unsupported source character 0x%02x", ch);
                c_lex_error(c, message);
            }
            if (i + 1 < size && ((ch == '<' && strchr(":%", spliced[i + 1]) != NULL) ||
                (ch == '%' && strchr(":>", spliced[i + 1]) != NULL) ||
                (ch == ':' && spliced[i + 1] == '>'))) c_lex_error(c, "digraphs are unsupported");
            size_t n = 1;
            for (size_t j = 0; j < sizeof(punctuation) / sizeof(punctuation[0]); ++j) {
                size_t candidate = strlen(punctuation[j]);
                if (candidate <= size - i && memcmp(spliced + i, punctuation[j], candidate) == 0) {
                    n = candidate; break;
                }
            }
            i += n;
            kind = PUNCT;
        }
        char *spelling = c->allocate(c->context, i - start + 1);
        memcpy(spelling, spliced + start, i - start);
        spelling[i - start] = '\0';
        c_lex_push(c, &result, (token_t){.text = spelling, .file = file,
                   .line = lines[start], .kind = kind, .space = space});
        space = 0;
    }
    return result;
}

#endif
