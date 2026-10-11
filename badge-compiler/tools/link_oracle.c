#define _POSIX_C_SOURCE 200809L
#include "badge_object.h"
#include "host_io.h"

int main(int argc,char **argv) {
    if(argc!=3) { fprintf(stderr,"usage: link_oracle input.o reference.o\n"); return 2; }
    struct stat st,out;
    FILE *f=fopen(argv[1],"rb");
    if(f==NULL) { fprintf(stderr,"link oracle: cannot open input: %s\n",strerror(errno)); return 1; }
    if(fstat(fileno(f),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>BO_MAX_FILE ||
       (stat(argv[2],&out)==0 && st.st_dev==out.st_dev && st.st_ino==out.st_ino)) {
        fprintf(stderr,"link oracle: invalid input or output alias\n"); fclose(f); return 1;
    }
    size_t size=(size_t)st.st_size; unsigned char *input=malloc(size+1);
    if(input==NULL) { fprintf(stderr,"link oracle: out of memory\n"); fclose(f); return 1; }
    int ok=fread(input,1,size,f)==size; if(fclose(f)!=0) ok=0;
    if(!ok) { fprintf(stderr,"link oracle: input read/close failed\n"); free(input); return 1; }
    bo_object_t *model=badge_object_read(input,size); free(input); if(model==NULL) return 1;
    bo_section_t *sections=calloc(model->section_count+1,sizeof(*sections));
    if(sections==NULL) { fprintf(stderr,"link oracle: out of memory\n"); badge_object_free(model); return 1; }
    for(size_t k=0;k<model->section_count;++k) {
        sections[k]=model->sections[k];
        if(sections[k].flags&0x30) { sections[k].flags&=~UINT32_C(0x30); sections[k].entry_size=0; }
    }
    bo_object_t reference=*model; reference.sections=sections;
    unsigned char *bytes=NULL; size_t length=0;
    ok=badge_object_write(&reference,&bytes,&length);
    if(ok) ok=host_write_file("link oracle",argv[2],bytes,length);
    free(bytes); free(sections); badge_object_free(model); return ok?0:1;
}
