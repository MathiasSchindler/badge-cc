#define _POSIX_C_SOURCE 200809L
#include "badge_object.h"
#ifndef BADGE_OBJECT_LIBRARY
#include "host_io.h"
#endif
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint32_t type, flags, alignment, link, info, entry_size, size, offset, name_offset, written;
    const unsigned char *data;
    unsigned char *owned;
} bo_output_section_t;

static int bo_error(const char *message) {
    fprintf(stderr, "badge-object: %s\n", message);
    return 0;
}

static uint16_t bo_u16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t bo_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void bo_put16(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

static void bo_put32(unsigned char *p, uint32_t v) {
    for (unsigned k = 0; k < 4; ++k) p[k] = (unsigned char)(v >> (8 * k));
}

static int bo_power(uint32_t n) { return n != 0 && (n & (n - 1)) == 0; }
static int bo_range(size_t length, uint32_t offset, uint64_t size) {
    return offset <= length && size <= length - offset;
}

static int bo_name(const char *name) {
    return name != NULL && strnlen(name, 4097) <= 4096;
}

static int bo_section_type(uint32_t type) {
    return type == BO_PROGBITS || type == BO_NOBITS || type == BO_ARM_EXIDX ||
        type == BO_ARM_ATTRIBUTES || type == BO_LLVM_ADDRSIG;
}

static unsigned bo_relocation_width(uint32_t type) {
    switch (type) {
        case 2: case 3: case 10: case 30: case 42: case 47: case 48: return 4;
        default: return 0;
    }
}

static int bo_uleb(const unsigned char *data, size_t size, size_t *at, uint32_t *value) {
    uint32_t result = 0;
    for (unsigned k = 0; k < 5; ++k) {
        if (*at >= size) return bo_error("truncated address-significance ULEB128");
        unsigned byte = data[(*at)++];
        if (k == 4 && byte > 15) return bo_error("overflowing address-significance ULEB128");
        result |= (uint32_t)(byte & 127) << (7 * k);
        if ((byte & 128) == 0) { *value = result; return 1; }
    }
    return bo_error("invalid address-significance ULEB128");
}

static int bo_validate(const bo_object_t *o) {
    if (o == NULL || o->section_count > BO_MAX_SECTIONS ||
        o->symbol_count > BO_MAX_SYMBOLS || o->relocation_count > BO_MAX_RELOCATIONS ||
        (o->section_count != 0 && o->sections == NULL) ||
        (o->symbol_count != 0 && o->symbols == NULL) ||
        (o->relocation_count != 0 && o->relocations == NULL))
        return bo_error("invalid object arrays or count limit exceeded");
    uint64_t payload = 0;
    for (size_t k = 0; k < o->section_count; ++k) {
        const bo_section_t *s = &o->sections[k];
        if (!bo_name(s->name) || s->name[0] == 0 || !bo_section_type(s->type) ||
            !bo_power(s->alignment) || s->alignment > 65536 ||
            (s->flags & ~UINT32_C(0x800000b7)) != 0)
            return bo_error("invalid/unsupported section name, type, flags or alignment");
        if (strcmp(s->name, ".symtab") == 0 || strcmp(s->name, ".strtab") == 0 ||
            strcmp(s->name, ".shstrtab") == 0)
            return bo_error("reserved generated section name");
        for (size_t j = 0; j < k; ++j)
            if (strcmp(s->name, o->sections[j].name) == 0) return bo_error("duplicate section name");
        if (s->type == BO_NOBITS) {
            if (s->data != NULL) return bo_error("NOBITS cannot contain file bytes");
        } else {
            payload += s->size;
            if (payload > BO_MAX_FILE || (s->size != 0 && s->data == NULL))
                return bo_error("missing section bytes or payload limit exceeded");
        }
        if (s->entry_size != 0 && s->size % s->entry_size != 0)
            return bo_error("section size is not a multiple of entry size");
        if ((s->flags & 32) && (!(s->flags & 16) || s->entry_size == 0))
            return bo_error("string merging requires SHF_MERGE and an entry size");
        if ((s->flags & 16) && s->entry_size == 0)
            return bo_error("SHF_MERGE requires an entry size");
        if (s->type == BO_LLVM_ADDRSIG) {
            if (s->link != BO_LINK_SYMTAB || s->flags != UINT32_C(0x80000000) ||
                s->alignment != 1 || s->entry_size != 0)
                return bo_error("invalid LLVM address-significance section metadata");
            size_t at = 0;
            while (at < s->size) {
                uint32_t id;
                if (!bo_uleb(s->data, s->size, &at, &id)) return 0;
                if (id == 0 || id > o->symbol_count)
                    return bo_error("address-significance symbol reference out of range");
            }
        } else if (s->flags & 128) {
            if (s->link == 0 || s->link > o->section_count || s->link == k + 1)
                return bo_error("invalid link-order section reference");
        } else if (s->link != 0) return bo_error("unexpected section link");
        if (s->type == BO_ARM_EXIDX &&
            (s->flags != 130 || s->alignment != 4 || s->size % 8 != 0 ||
             (s->entry_size != 0 && s->entry_size != 8) ||
             !(o->sections[s->link - 1].flags & 4)))
            return bo_error("invalid Arm exception-index section metadata");
        if (s->type == BO_ARM_ATTRIBUTES &&
            (s->flags != 0 || s->alignment != 1 || s->entry_size != 0))
            return bo_error("invalid Arm attribute section metadata");
        if ((s->flags & 4) && s->type != BO_PROGBITS)
            return bo_error("executable sections must be PROGBITS");
    }
    for (size_t k = 0; k < o->symbol_count; ++k) {
        const bo_symbol_t *v = &o->symbols[k];
        if (!bo_name(v->name) || v->binding > 2 || v->type > 4 || v->visibility > 3)
            return bo_error("invalid/unsupported symbol metadata");
        if (v->section == 0) {
            if (v->binding == 0 || v->value != 0 || v->size != 0 ||
                v->name[0] == 0 || v->type > 2)
                return bo_error("invalid undefined symbol");
        } else if (v->section == BO_COMMON) {
            if (v->binding == 0 || v->type != 1 || !bo_power(v->value) || v->value > 65536)
                return bo_error("invalid common symbol alignment/binding/type");
        } else if (v->section != BO_ABS) {
            if (v->section > o->section_count) return bo_error("symbol section out of range");
            const bo_section_t *s = &o->sections[v->section - 1];
            uint32_t offset = v->type == 2 ? v->value & ~UINT32_C(1) : v->value;
            if (offset > s->size || v->size > s->size - offset)
                return bo_error("symbol extends beyond its section");
            if (v->type == 2 && (!(s->flags & 4) || !(v->value & 1)))
                return bo_error("defined functions require executable Thumb symbols");
        }
        if (v->type == 3 && (v->binding != 0 || v->section == 0 ||
            v->section > o->section_count || v->value != 0 || v->size != 0 || v->name[0] != 0))
            return bo_error("invalid section symbol");
        if (v->type == 4 && (v->binding != 0 || v->section != BO_ABS || v->value != 0 || v->size != 0))
            return bo_error("invalid file symbol");
    }
    for (size_t k = 0; k < o->relocation_count; ++k) {
        const bo_relocation_t *r = &o->relocations[k];
        unsigned width = bo_relocation_width(r->type);
        if (r->section == 0 || r->section > o->section_count ||
            r->symbol == 0 || r->symbol > o->symbol_count || width == 0)
            return bo_error("invalid relocation reference or unsupported relocation kind");
        const bo_section_t *s = &o->sections[r->section - 1];
        if (s->type == BO_NOBITS || s->type == BO_ARM_ATTRIBUTES || s->type == BO_LLVM_ADDRSIG ||
            r->offset > s->size || width > s->size - r->offset ||
            r->offset % ((r->type == 10 || r->type == 30 ||
                          r->type == 47 || r->type == 48) ? 2 : 4) != 0)
            return bo_error("invalid relocation place, width or alignment");
        if ((r->type == 10 || r->type == 30 || r->type == 47 ||
             r->type == 48) && !(s->flags & 4))
            return bo_error("Thumb instruction relocations require executable sections");
    }
    return 1;
}

int badge_object_write(const bo_object_t *o, unsigned char **bytes, size_t *length) {
    if (bytes == NULL || length == NULL) return bo_error("missing output buffer parameters");
    *bytes = NULL; *length = 0;
    if (!bo_validate(o)) return 0;
    size_t capacity = o->section_count * 2 + 4;
    bo_output_section_t *out = calloc(capacity, sizeof(*out));
    uint32_t *symbol_map = calloc(o->symbol_count + 1, sizeof(*symbol_map));
    uint32_t *rel_counts = calloc(o->section_count + 1, sizeof(*rel_counts));
    unsigned char *result = NULL;
    size_t count = o->section_count + 1;
    int ok = 0;
    if (out == NULL || symbol_map == NULL || rel_counts == NULL) {
        bo_error("out of memory constructing object"); goto done;
    }
    for (size_t k = 0; k < o->section_count; ++k) {
        const bo_section_t *s = &o->sections[k];
        out[k + 1] = (bo_output_section_t){
            .name=s->name, .type=s->type, .flags=s->flags, .alignment=s->alignment,
            .link=s->link, .entry_size=s->entry_size, .size=s->size, .data=s->data
        };
    }
    uint32_t next_symbol = 1;
    for (size_t k = 0; k < o->symbol_count; ++k)
        if (o->symbols[k].binding == 0) symbol_map[k + 1] = next_symbol++;
    uint32_t first_global = next_symbol;
    for (size_t k = 0; k < o->symbol_count; ++k)
        if (o->symbols[k].binding != 0) symbol_map[k + 1] = next_symbol++;
    for (size_t k = 0; k < o->relocation_count; ++k) ++rel_counts[o->relocations[k].section];
    for (size_t k = 1; k <= o->section_count; ++k) {
        if (rel_counts[k] == 0) continue;
        size_t n = strlen(out[k].name);
        char *name = malloc(n + 5);
        unsigned char *data = calloc(rel_counts[k], 8);
        if (name == NULL || data == NULL) {
            free(name); free(data); bo_error("out of memory constructing relocations"); goto done;
        }
        memcpy(name, ".rel", 4); memcpy(name + 4, out[k].name, n + 1);
        uint32_t relocation_count = rel_counts[k];
        rel_counts[k] = (uint32_t)count;
        out[count++] = (bo_output_section_t){
            .name=name, .type=9, .flags=64, .alignment=4, .info=(uint32_t)k,
            .entry_size=8, .size=relocation_count*8, .data=data, .owned=data
        };
    }
    uint32_t symtab_index = (uint32_t)count;
    uint32_t strtab_index = symtab_index + 1, shstrtab_index = symtab_index + 2;
    for (size_t k = o->section_count + 1; k < symtab_index; ++k) out[k].link = symtab_index;
    for (size_t k = 0; k < o->relocation_count; ++k) {
        const bo_relocation_t *r = &o->relocations[k];
        bo_output_section_t *s = &out[rel_counts[r->section]];
        bo_put32(s->owned+s->written, r->offset);
        bo_put32(s->owned+s->written+4, (symbol_map[r->symbol] << 8) | r->type);
        s->written += 8;
    }
    size_t strings_size = 1;
    for (size_t k = 0; k < o->symbol_count; ++k) strings_size += strlen(o->symbols[k].name) + 1;
    if (strings_size > BO_MAX_FILE) { bo_error("symbol string table exceeds output limit"); goto done; }
    out[count++] = (bo_output_section_t){
        .name=".symtab", .type=2, .alignment=4, .link=strtab_index, .info=first_global,
        .entry_size=16, .size=(uint32_t)((o->symbol_count+1)*16)
    };
    out[count++] = (bo_output_section_t){.name=".strtab", .type=3, .alignment=1, .size=(uint32_t)strings_size};
    out[count++] = (bo_output_section_t){.name=".shstrtab", .type=3, .alignment=1};
    for (size_t k = 1; k < count; ++k) {
        for (size_t j = 1; j < k; ++j)
            if (strcmp(out[k].name, out[j].name) == 0) {
                bo_error("generated relocation section name collides with input"); goto done;
            }
    }
    size_t names_size = 1;
    for (size_t k = 1; k < count; ++k) {
        out[k].name_offset = (uint32_t)names_size;
        names_size += strlen(out[k].name) + 1;
    }
    if (names_size > BO_MAX_FILE) { bo_error("section string table exceeds output limit"); goto done; }
    out[shstrtab_index].size = (uint32_t)names_size;
    for (size_t k = 1; k <= o->section_count; ++k) {
        if (out[k].type != BO_LLVM_ADDRSIG) continue;
        const bo_section_t *s = &o->sections[k - 1];
        size_t at = 0, written = 0;
        if ((uint64_t)s->size * 3 > BO_MAX_FILE) {
            bo_error("address-significance output exceeds limit"); goto done;
        }
        out[k].owned = malloc((size_t)s->size * 3 + 1);
        if (out[k].owned == NULL) { bo_error("out of memory remapping address significance"); goto done; }
        while (at < s->size) {
            uint32_t id;
            if (!bo_uleb(s->data, s->size, &at, &id)) goto done;
            uint32_t value = symbol_map[id];
            do {
                unsigned byte = value & 127; value >>= 7;
                out[k].owned[written++] = (unsigned char)(byte | (value != 0 ? 128 : 0));
            } while (value != 0);
        }
        out[k].data = out[k].owned; out[k].size = (uint32_t)written; out[k].link = symtab_index;
    }
    uint64_t offset = 52;
    for (size_t k = 1; k < count; ++k) {
        uint32_t alignment = out[k].alignment;
        offset = (offset + alignment - 1) & ~((uint64_t)alignment - 1);
        if (offset > BO_MAX_FILE) { bo_error("object exceeds output limit"); goto done; }
        out[k].offset = (uint32_t)offset;
        if (out[k].type != BO_NOBITS) offset += out[k].size;
    }
    offset = (offset + 3) & ~UINT64_C(3);
    uint32_t shoff = (uint32_t)offset;
    offset += count * 40;
    if (offset > BO_MAX_FILE) { bo_error("object exceeds output limit"); goto done; }
    result = calloc((size_t)offset, 1);
    if (result == NULL) { bo_error("out of memory allocating ELF"); goto done; }
    memcpy(result, "\177ELF\1\1\1", 7);
    bo_put16(result+16, 1); bo_put16(result+18, 40); bo_put32(result+20, 1);
    bo_put32(result+32, shoff); bo_put32(result+36, UINT32_C(0x05000000));
    bo_put16(result+40, 52); bo_put16(result+46, 40);
    bo_put16(result+48, (uint32_t)count); bo_put16(result+50, shstrtab_index);
    for (size_t k = 1; k < count; ++k) {
        bo_output_section_t *s = &out[k];
        if (s->type != BO_NOBITS && s->size != 0 && s->data != NULL)
            memcpy(result+s->offset, s->data, s->size);
        unsigned char *sh = result+shoff+k*40;
        bo_put32(sh, s->name_offset); bo_put32(sh+4, s->type); bo_put32(sh+8, s->flags);
        bo_put32(sh+16, s->offset); bo_put32(sh+20, s->size); bo_put32(sh+24, s->link);
        bo_put32(sh+28, s->info); bo_put32(sh+32, s->alignment); bo_put32(sh+36, s->entry_size);
        memcpy(result+out[shstrtab_index].offset+s->name_offset, s->name, strlen(s->name)+1);
    }
    size_t string_offset = 1;
    for (size_t k = 0; k < o->symbol_count; ++k) {
        const bo_symbol_t *v = &o->symbols[k];
        unsigned char *entry = result+out[symtab_index].offset+symbol_map[k+1]*16;
        bo_put32(entry, (uint32_t)string_offset); bo_put32(entry+4, v->value); bo_put32(entry+8, v->size);
        entry[12] = (unsigned char)((v->binding << 4) | v->type); entry[13] = (unsigned char)v->visibility;
        bo_put16(entry+14, v->section);
        size_t n = strlen(v->name)+1;
        memcpy(result+out[strtab_index].offset+string_offset, v->name, n); string_offset += n;
    }
    *bytes = result; *length = (size_t)offset; result = NULL; ok = 1;
done:
    if (out != NULL) {
        for (size_t k = 1; k < count; ++k) {
            if (k > o->section_count && out[k].type == 9) free((void *)out[k].name);
            free(out[k].owned);
        }
    }
    free(result); free(out); free(symbol_map); free(rel_counts);
    return ok;
}

typedef struct {
    bo_object_t object;
    bo_section_t *sections;
    bo_symbol_t *symbols;
    bo_relocation_t *relocations;
} bo_model_t;

static int bo_model_init(bo_model_t *m) {
    memset(m, 0, sizeof(*m));
    m->sections = calloc(BO_MAX_SECTIONS, sizeof(*m->sections));
    m->symbols = calloc(BO_MAX_SYMBOLS, sizeof(*m->symbols));
    m->relocations = calloc(BO_MAX_RELOCATIONS, sizeof(*m->relocations));
    if (m->sections == NULL || m->symbols == NULL || m->relocations == NULL)
        return bo_error("out of memory allocating input model");
    m->object.sections=m->sections; m->object.symbols=m->symbols; m->object.relocations=m->relocations;
    return 1;
}

static void bo_model_free(bo_model_t *m) {
    for (size_t k=0; k<m->object.section_count; ++k) {
        free((void *)m->sections[k].name); free((void *)m->sections[k].data);
    }
    for (size_t k=0; k<m->object.symbol_count; ++k) free((void *)m->symbols[k].name);
    free(m->sections); free(m->symbols); free(m->relocations);
}

static char *bo_copy_name(const char *name) {
    if (!bo_name(name)) { bo_error("name exceeds 4096-byte limit"); return NULL; }
    char *copy=strdup(name);
    if (copy==NULL) bo_error("out of memory copying name");
    return copy;
}

#ifndef BADGE_OBJECT_LIBRARY
static int bo_number(const char *text, uint32_t *value) {
    if (text[0]<'0' || text[0]>'9') return bo_error("expected unsigned decimal or hexadecimal number");
    errno=0; char *end;
    unsigned long long v=strtoull(text,&end, text[0]=='0' && text[1]=='x' ? 16 : 10);
    if (errno!=0 || *end!=0 || v>UINT32_MAX) return bo_error("number outside uint32 range");
    *value=(uint32_t)v; return 1;
}

static int bo_hex(unsigned char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}

static int bo_tokens(char *line, char **fields, size_t *count) {
    *count=0; char *p=line;
    while (*p!=0) {
        while (isspace((unsigned char)*p)) ++p;
        if (*p==0 || *p=='#') return 1;
        if (*count==12) return bo_error("too many manifest fields");
        if (*p=='"') {
            ++p; fields[(*count)++]=p; char *out=p;
            while (*p!=0 && *p!='"') {
                unsigned char c=(unsigned char)*p++;
                if (c=='\\') {
                    c=(unsigned char)*p++;
                    if (c==0) return bo_error("unterminated quoted field");
                    if (c=='n') c='\n'; else if(c=='r') c='\r'; else if(c=='t') c='\t';
                    else if(c!='\\' && c!='"') return bo_error("unsupported quoted-field escape");
                }
                *out++=(char)c;
            }
            if (*p!='"') return bo_error("unterminated quoted field");
            ++p;
            if (*p!=0 && !isspace((unsigned char)*p) && *p!='#')
                return bo_error("quoted field needs a separator");
            *out=0;
        } else {
            fields[(*count)++]=p;
            while (*p!=0 && !isspace((unsigned char)*p)) ++p;
            if (*p!=0) *p++=0;
        }
    }
    return 1;
}

static int bo_manifest(unsigned char *input, size_t length, bo_model_t *m) {
    if (memchr(input,0,length)!=NULL) return bo_error("NUL byte in manifest");
    char *line=(char *)input; unsigned line_number=0; int header=0;
    while (line!=NULL) {
        ++line_number;
        char *next=strchr(line,'\n'); if(next!=NULL) *next++=0;
        char *f[12]; size_t n;
        if (!bo_tokens(line,f,&n)) goto invalid;
        if (n==0) { line=next; continue; }
        if (!header) {
            if(n!=1 || strcmp(f[0],"badge-object-v1")!=0) { bo_error("expected badge-object-v1 header"); goto invalid; }
            header=1; line=next; continue;
        }
        uint32_t v[8];
        if(strcmp(f[0],"section")==0 && n==10) {
            if(m->object.section_count==BO_MAX_SECTIONS) { bo_error("section count limit exceeded"); goto invalid; }
            if(!bo_number(f[1],&v[0])) goto invalid;
            for(unsigned k=0;k<6;++k) if(!bo_number(f[k+3],&v[k+1])) goto invalid;
            if(v[0]!=m->object.section_count+1) { bo_error("section IDs must be sequential from one"); goto invalid; }
            bo_section_t *s=&m->sections[m->object.section_count++];
            *s=(bo_section_t){.type=v[1],.flags=v[2],.alignment=v[3],.link=v[4],.entry_size=v[5],.size=v[6]};
            s->name=bo_copy_name(f[2]); if(s->name==NULL) goto invalid;
            if(strcmp(f[9],"-")==0) {
                if(s->type!=BO_NOBITS && s->size!=0) { bo_error("missing section hex bytes"); goto invalid; }
            } else {
                if(s->type==BO_NOBITS || s->size>BO_MAX_FILE/2 || strlen(f[9])!=(uint64_t)s->size*2) {
                    bo_error("section hex length/type mismatch or payload limit exceeded"); goto invalid;
                }
                unsigned char *data=malloc((size_t)s->size+1);
                if(data==NULL) { bo_error("out of memory decoding section bytes"); goto invalid; }
                s->data=data;
                for(size_t k=0;k<s->size;++k) {
                    int a=bo_hex((unsigned char)f[9][k*2]), b=bo_hex((unsigned char)f[9][k*2+1]);
                    if(a<0 || b<0) { bo_error("invalid section hex byte"); goto invalid; }
                    data[k]=(unsigned char)((a<<4)|b);
                }
            }
        } else if(strcmp(f[0],"symbol")==0 && n==9) {
            if(m->object.symbol_count==BO_MAX_SYMBOLS) { bo_error("symbol count limit exceeded"); goto invalid; }
            if(!bo_number(f[1],&v[0])) goto invalid;
            for(unsigned k=0;k<6;++k) if(!bo_number(f[k+3],&v[k+1])) goto invalid;
            if(v[0]!=m->object.symbol_count+1) { bo_error("symbol IDs must be sequential from one"); goto invalid; }
            bo_symbol_t *s=&m->symbols[m->object.symbol_count++];
            *s=(bo_symbol_t){.binding=v[1],.type=v[2],.visibility=v[3],.section=v[4],.value=v[5],.size=v[6]};
            s->name=bo_copy_name(f[2]); if(s->name==NULL) goto invalid;
        } else if(strcmp(f[0],"reloc")==0 && n==5) {
            if(m->object.relocation_count==BO_MAX_RELOCATIONS) { bo_error("relocation count limit exceeded"); goto invalid; }
            for(unsigned k=0;k<4;++k) if(!bo_number(f[k+1],&v[k])) goto invalid;
            m->relocations[m->object.relocation_count++]=(bo_relocation_t){v[0],v[1],v[2],v[3]};
        } else { bo_error("unknown manifest record or incorrect field count"); goto invalid; }
        line=next; continue;
invalid:
        fprintf(stderr,"badge-object: manifest line %u\n",line_number); return 0;
    }
    if(!header) return bo_error("empty manifest");
    return bo_validate(&m->object);
}

#endif

static const char *bo_elf_string(const unsigned char *data, size_t size, uint32_t offset) {
    if(offset>=size || memchr(data+offset,0,size-offset)==NULL) {
        bo_error("ELF string offset/terminator out of bounds"); return NULL;
    }
    const char *name=(const char *)data+offset;
    if(!bo_name(name)) { bo_error("ELF name exceeds limit"); return NULL; }
    return name;
}

static int bo_import(const unsigned char *input, size_t length, bo_model_t *m) {
    if(length<52 || memcmp(input,"\177ELF\1\1\1\0\0",9)!=0 ||
       bo_u16(input+16)!=1 || bo_u16(input+18)!=40 || bo_u32(input+20)!=1 ||
       bo_u32(input+24)!=0 || bo_u32(input+28)!=0 || bo_u32(input+36)!=UINT32_C(0x05000000) ||
       bo_u16(input+40)!=52 || bo_u16(input+44)!=0 || bo_u16(input+46)!=40)
        return bo_error("expected ELF32 little-endian Arm EABI5 relocatable object");
    uint32_t shoff=bo_u32(input+32); uint16_t count=bo_u16(input+48), names_index=bo_u16(input+50);
    if(count<2 || count>BO_MAX_SECTIONS*2+4 || names_index==0 || names_index>=count ||
       !bo_range(length,shoff,(uint64_t)count*40))
        return bo_error("invalid ELF section table");
    const unsigned char *headers=input+shoff;
    for(unsigned k=0;k<40;++k) if(headers[k]!=0) return bo_error("nonzero/extended ELF null section");
    uint32_t sym_index=0, strings_index=0;
    for(unsigned k=1;k<count;++k) {
        const unsigned char *h=headers+k*40; uint32_t type=bo_u32(h+4), size=bo_u32(h+20);
        uint32_t align=bo_u32(h+32);
        if(bo_u32(h+12)!=0 || (type!=BO_NOBITS && !bo_range(length,bo_u32(h+16),size)) ||
           !bo_power(align) || align>65536 || bo_u32(h+16)%align!=0)
            return bo_error("invalid ELF section bounds/address/alignment");
        if(type==2) {
            if(sym_index!=0 || bo_u32(h+36)!=16 || size<16 || size%16!=0 ||
               size/16-1>BO_MAX_SYMBOLS)
                return bo_error("invalid/multiple ELF symbol tables");
            sym_index=k; strings_index=bo_u32(h+24);
        }
    }
    if(sym_index==0 || strings_index==0 || strings_index>=count ||
       bo_u32(headers+names_index*40+4)!=3 || bo_u32(headers+strings_index*40+4)!=3)
        return bo_error("missing ELF symbol/name tables");
    const unsigned char *nh=headers+names_index*40, *sth=headers+strings_index*40;
    const unsigned char *names=input+bo_u32(nh+16), *strings=input+bo_u32(sth+16);
    uint32_t names_size=bo_u32(nh+20), strings_size=bo_u32(sth+20);
    if(names_size==0 || strings_size==0 || names[0]!=0 || strings[0]!=0)
        return bo_error("invalid ELF string-table sentinel");
    uint32_t *map=calloc(count,sizeof(*map));
    if(map==NULL) return bo_error("out of memory mapping ELF sections");
    int ok=0;
    uint64_t imported_payload=0;
    for(unsigned k=1;k<count;++k) {
        const unsigned char *h=headers+k*40; uint32_t type=bo_u32(h+4);
        if(type==2 || type==9 || k==names_index || k==strings_index) continue;
        if(!bo_section_type(type) || m->object.section_count==BO_MAX_SECTIONS) {
            bo_error("unsupported ELF section type or section limit exceeded"); goto done_import;
        }
        const char *name=bo_elf_string(names,names_size,bo_u32(h)); if(name==NULL) goto done_import;
        bo_section_t *s=&m->sections[m->object.section_count++]; map[k]=(uint32_t)m->object.section_count;
        *s=(bo_section_t){.type=type,.flags=bo_u32(h+8),.alignment=bo_u32(h+32),
            .link=bo_u32(h+24),.entry_size=bo_u32(h+36),.size=bo_u32(h+20)};
        if(bo_u32(h+28)!=0) { bo_error("unsupported input section sh_info"); goto done_import; }
        s->name=bo_copy_name(name); if(s->name==NULL) goto done_import;
        if(type!=BO_NOBITS && s->size!=0) {
            imported_payload+=s->size;
            if(imported_payload>BO_MAX_FILE) { bo_error("ELF payload limit exceeded"); goto done_import; }
            unsigned char *data=malloc(s->size);
            if(data==NULL) { bo_error("out of memory copying ELF section"); goto done_import; }
            memcpy(data,input+bo_u32(h+16),s->size); s->data=data;
        }
    }
    for(size_t k=0;k<m->object.section_count;++k) {
        bo_section_t *s=&m->sections[k];
        if(s->type==BO_LLVM_ADDRSIG) {
            if(s->link!=sym_index) { bo_error("invalid ELF address-significance link"); goto done_import; }
            s->link=BO_LINK_SYMTAB;
        } else if(s->link!=0) {
            if(s->link>=count || map[s->link]==0) { bo_error("unsupported ELF section link"); goto done_import; }
            s->link=map[s->link];
        }
    }
    const unsigned char *syh=headers+sym_index*40, *symbols=input+bo_u32(syh+16);
    uint32_t num_symbols=bo_u32(syh+20)/16, first_global=bo_u32(syh+28);
    if(first_global==0 || first_global>num_symbols) { bo_error("invalid ELF local-symbol boundary"); goto done_import; }
    for(unsigned k=0;k<16;++k) if(symbols[k]!=0) { bo_error("nonzero ELF null symbol"); goto done_import; }
    for(uint32_t k=1;k<num_symbols;++k) {
        const unsigned char *entry=symbols+k*16;
        const char *name=bo_elf_string(strings,strings_size,bo_u32(entry)); if(name==NULL) goto done_import;
        bo_symbol_t *v=&m->symbols[m->object.symbol_count++];
        *v=(bo_symbol_t){.binding=entry[12]>>4,.type=entry[12]&15,.visibility=entry[13],
            .section=bo_u16(entry+14),.value=bo_u32(entry+4),.size=bo_u32(entry+8)};
        v->name=bo_copy_name(name); if(v->name==NULL) goto done_import;
        if((k<first_global)!=(v->binding==0)) { bo_error("invalid ELF local/global symbol ordering"); goto done_import; }
        if(v->section!=0 && v->section!=BO_ABS && v->section!=BO_COMMON) {
            if(v->section>=count || map[v->section]==0) { bo_error("unsupported ELF symbol section"); goto done_import; }
            v->section=map[v->section];
        }
    }
    for(unsigned k=1;k<count;++k) {
        const unsigned char *h=headers+k*40; if(bo_u32(h+4)!=9) continue;
        uint32_t size=bo_u32(h+20), target=bo_u32(h+28);
        if(bo_u32(h+24)!=sym_index || target>=count || map[target]==0 ||
           bo_u32(h+36)!=8 || size%8!=0 || (bo_u32(h+8)!=0 && bo_u32(h+8)!=64) ||
           size/8>BO_MAX_RELOCATIONS-m->object.relocation_count) {
            bo_error("invalid ELF REL table"); goto done_import;
        }
        const unsigned char *data=input+bo_u32(h+16);
        for(uint32_t at=0;at<size;at+=8) {
            uint32_t info=bo_u32(data+at+4);
            m->relocations[m->object.relocation_count++]=(bo_relocation_t){
                map[target],bo_u32(data+at),info&255,info>>8
            };
        }
    }
    ok=bo_validate(&m->object);
done_import:
    free(map); return ok;
}

bo_object_t *badge_object_read(const unsigned char *bytes, size_t length) {
    if(bytes==NULL || length>BO_MAX_FILE) { bo_error("invalid/oversized ELF input"); return NULL; }
    bo_model_t *m=calloc(1,sizeof(*m));
    if(m==NULL) { bo_error("out of memory allocating ELF model"); return NULL; }
    if(!bo_model_init(m) || !bo_import(bytes,length,m)) {
        bo_model_free(m); free(m); return NULL;
    }
    bo_section_t *sections=realloc(m->sections,(m->object.section_count+1)*sizeof(*sections));
    if(sections==NULL) { bo_error("out of memory sizing ELF model"); goto failed_read; }
    m->sections=sections; m->object.sections=sections;
    bo_symbol_t *symbols=realloc(m->symbols,(m->object.symbol_count+1)*sizeof(*symbols));
    if(symbols==NULL) { bo_error("out of memory sizing ELF model"); goto failed_read; }
    m->symbols=symbols; m->object.symbols=symbols;
    bo_relocation_t *relocations=realloc(m->relocations,(m->object.relocation_count+1)*sizeof(*relocations));
    if(relocations==NULL) { bo_error("out of memory sizing ELF model"); goto failed_read; }
    m->relocations=relocations; m->object.relocations=relocations;
    return &m->object;
failed_read:
    bo_model_free(m); free(m); return NULL;
}

void badge_object_free(bo_object_t *object) {
    if(object!=NULL) { bo_model_t *m=(bo_model_t *)object; bo_model_free(m); free(m); }
}

#ifndef BADGE_OBJECT_LIBRARY
static int bo_quote(FILE *f, const char *text) {
    if(fputc('"',f)==EOF) return 0;
    for(const unsigned char *p=(const unsigned char *)text;*p!=0;++p) {
        if(*p=='"' || *p=='\\') { if(fputc('\\',f)==EOF || fputc(*p,f)==EOF) return 0; }
        else if(*p=='\n' || *p=='\r' || *p=='\t') {
            if(fputc('\\',f)==EOF || fputc(*p=='\n'?'n':*p=='\r'?'r':'t',f)==EOF) return 0;
        } else if(fputc(*p,f)==EOF) return 0;
    }
    return fputc('"',f)!=EOF;
}

static int bo_emit_manifest(const bo_object_t *o, unsigned char **bytes, size_t *length) {
    *bytes=NULL; *length=0;
    uint64_t estimate=32;
    for(size_t k=0;k<o->section_count;++k)
        estimate+=strlen(o->sections[k].name)*2+128+(o->sections[k].type==BO_NOBITS?0:(uint64_t)o->sections[k].size*2);
    for(size_t k=0;k<o->symbol_count;++k) estimate+=strlen(o->symbols[k].name)*2+128;
    estimate+=o->relocation_count*64;
    if(estimate>BO_MAX_FILE) return bo_error("manifest exceeds file limit");
    char *text=NULL; size_t size=0; FILE *f=open_memstream(&text,&size);
    if(f==NULL) return bo_error("cannot allocate manifest output stream");
    int ok=fputs("badge-object-v1\n",f)>=0;
    for(size_t k=0;ok && k<o->section_count;++k) {
        const bo_section_t *s=&o->sections[k];
        ok=fprintf(f,"section %zu ",k+1)>=0 && bo_quote(f,s->name) &&
            fprintf(f," %" PRIu32 " 0x%" PRIx32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " ",
                s->type,s->flags,s->alignment,s->link,s->entry_size,s->size)>=0;
        if(s->type==BO_NOBITS || s->size==0) ok=ok && fputc('-',f)!=EOF;
        else for(size_t j=0;ok && j<s->size;++j) ok=fprintf(f,"%02x",s->data[j])>=0;
        ok=ok && fputc('\n',f)!=EOF;
    }
    for(size_t k=0;ok && k<o->symbol_count;++k) {
        const bo_symbol_t *v=&o->symbols[k];
        ok=fprintf(f,"symbol %zu ",k+1)>=0 && bo_quote(f,v->name) &&
            fprintf(f," %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
                v->binding,v->type,v->visibility,v->section,v->value,v->size)>=0;
    }
    for(size_t k=0;ok && k<o->relocation_count;++k) {
        const bo_relocation_t *r=&o->relocations[k];
        ok=fprintf(f,"reloc %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 "\n",
            r->section,r->offset,r->type,r->symbol)>=0;
    }
    if(fclose(f)!=0) ok=0;
    if(!ok) { free(text); return bo_error("failed to serialize manifest"); }
    *bytes=(unsigned char *)text; *length=size; return 1;
}

int main(int argc, char **argv) {
    int mode=0, offset=1;
    if(argc>1 && strcmp(argv[1],"--from-elf")==0) { mode=1; offset=2; }
    else if(argc>1 && strcmp(argv[1],"--emit-manifest")==0) { mode=2; offset=2; }
    if(argc!=offset+3 || strcmp(argv[offset+1],"-o")!=0) {
        fprintf(stderr,"usage: badge-object [--from-elf | --emit-manifest] input -o output\n");
        return argc==2 && strcmp(argv[1],"--help")==0 ? 0 : 2;
    }
    const char *path=argv[offset], *output=argv[offset+2];
    FILE *f=fopen(path,"rb");
    if(f==NULL) { fprintf(stderr,"badge-object: cannot open %s: %s\n",path,strerror(errno)); return 1; }
    struct stat st, dest;
    int ok=fstat(fileno(f),&st)==0;
    if(!ok || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>BO_MAX_FILE) {
        fclose(f); bo_error("input must be a regular file of at most 64 MiB"); return 1;
    }
    if(stat(output,&dest)==0 && st.st_dev==dest.st_dev && st.st_ino==dest.st_ino) {
        fclose(f); bo_error("output aliases input"); return 1;
    }
    size_t size=(size_t)st.st_size;
    unsigned char *input=malloc(size+1);
    if(input==NULL) { fclose(f); bo_error("out of memory reading input"); return 1; }
    ok=fread(input,1,size,f)==size && fgetc(f)==EOF && !ferror(f);
    if(fclose(f)!=0) ok=0;
    if(!ok) { free(input); bo_error("failed to read or close input"); return 1; }
    input[size]=0;
    bo_model_t model;
    ok=bo_model_init(&model);
    if(ok) ok=mode!=0 ? bo_import(input,size,&model) : bo_manifest(input,size,&model);
    unsigned char *bytes=NULL; size_t length=0;
    if(ok) ok=mode==2 ? bo_emit_manifest(&model.object,&bytes,&length) :
        badge_object_write(&model.object,&bytes,&length);
    if(ok) ok=host_write_file("badge-object",output,bytes,length);
    if(ok) printf("%s: %zu bytes, %zu sections, %zu symbols, %zu relocations\n",
        output,length,model.object.section_count,model.object.symbol_count,model.object.relocation_count);
    free(bytes); bo_model_free(&model); free(input);
    return ok ? 0 : 1;
}
#endif
