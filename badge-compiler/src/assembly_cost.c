#define BADGE_ASM_LIBRARY
#include "badge_asm.c"
#include "assembly_cost.h"
#define BADGE_OBJECT_LIBRARY
#include "badge_object.c"

int badge_assembly_code_bytes(const char *source, size_t length,
                             const char *filename, uint32_t *bytes) {
    if(!source || !bytes || length>AS_MAX_SOURCE) {
        fputs("badge-asm: invalid cost input\n",stderr);
        return 0;
    }
    *bytes=0;
    unsigned char *input=malloc(length+1);
    assembler_t *s=calloc(1,sizeof(*s));
    if(!input || !s) {
        fputs("badge-asm: out of memory measuring code\n",stderr);
        free(input); free(s); return 0;
    }
    memcpy(input,source,length); input[length]=0; s->input=filename;
    int ok=0;
    if(setjmp(s->failure)==0) {
        as_assemble(s,input,length,&s->result,&s->result_size);
        for(uint32_t k=1;k<=s->section_count;++k)
            if(s->sections[k].out.flags&4) *bytes+=s->sections[k].out.size;
        ok=1;
    }
    free(s->result); as_destroy(s); free(input);
    return ok;
}
