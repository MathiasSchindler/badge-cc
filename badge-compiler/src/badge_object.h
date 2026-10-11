#ifndef BADGE_COMPILER_OBJECT_H
#define BADGE_COMPILER_OBJECT_H

#include <stddef.h>
#include <stdint.h>

#define BO_PROGBITS UINT32_C(1)
#define BO_NOBITS UINT32_C(8)
#define BO_ARM_EXIDX UINT32_C(0x70000001)
#define BO_ARM_ATTRIBUTES UINT32_C(0x70000003)
#define BO_LLVM_ADDRSIG UINT32_C(0x6fff4c03)
#define BO_ABS UINT32_C(0xfff1)
#define BO_COMMON UINT32_C(0xfff2)
#define BO_LINK_SYMTAB UINT32_MAX
#define BO_MAX_SECTIONS 4096u
#define BO_MAX_SYMBOLS 65536u
#define BO_MAX_RELOCATIONS 262144u
#define BO_MAX_FILE (64u * 1024u * 1024u)

typedef struct {
    const char *name;
    uint32_t type, flags, alignment, link, entry_size, size;
    const unsigned char *data;
} bo_section_t;

typedef struct {
    const char *name;
    uint32_t binding, type, visibility, section, value, size;
} bo_symbol_t;

typedef struct {
    uint32_t section, offset, type, symbol;
} bo_relocation_t;

/* Section/symbol references are one-based input indices; zero is undefined.
 * ABS/COMMON are symbol sections; LINK_SYMTAB is an address-significance link.
 * REL addends are already encoded in section bytes, not in this structure. */
typedef struct {
    const bo_section_t *sections;
    size_t section_count;
    const bo_symbol_t *symbols;
    size_t symbol_count;
    const bo_relocation_t *relocations;
    size_t relocation_count;
} bo_object_t;

/* Input storage remains caller-owned. Success returns a malloc-owned buffer;
 * failure prints a diagnostic and returns NULL/zero through the outputs. */
int badge_object_write(const bo_object_t *object, unsigned char **bytes, size_t *length);

/* Decode the supported ET_REL subset into independent owned storage.
 * Failure prints a diagnostic. Release successful results with object_free. */
bo_object_t *badge_object_read(const unsigned char *bytes, size_t length);
void badge_object_free(bo_object_t *object);

#endif
