#include <assert.h>
#include "../peer/network_json.h"

static clean_network_t a,b;
static void transfer(clean_network_t *from,clean_network_t *to,int drop) {
    uint32_t size;
    const uint8_t *p=clean_network_output(from,&size);
    if (!p) return;
    if (!drop) assert(clean_network_input(to,p,size));
    clean_network_sent(from);
}
int main(int argc,char **argv) {
    assert(argc==2);
    const uint8_t ap[6]={2,1,2,3,4,5},sta[6]={2,6,7,8,9,10},session[6]={1,2,3,4,5,6};
    uint8_t empty[6]={0};
    clean_network_init(&a,0,ap,empty,empty,0,30,0);
    clean_network_init(&b,1,sta,ap,session,1,30,0);
    int dropped=0;
    for(uint32_t now=0;now<=30000000u;now+=10000u) {
        clean_network_step(&a,now);clean_network_step(&b,now);
        transfer(&a,&b,0);
        int drop=!dropped && b.pending==3u && b.attempts==1u && b.tx.count;
        transfer(&b,&a,drop);dropped|=drop;
    }
    b.report.state=2;
    assert(clean_network_valid(&b.report));
    assert(b.report.retries==1 && b.report.ack_attempts[1]==1);
    assert(b.report.event_count==1 && b.report.service_gaps==0);
    uint8_t raw[2092]={0};
    badger_bytes_copy(raw+1280,(const uint8_t *)&b.report,sizeof(b.report));
    raw[2000]=33;badger_bytes_store_u32(raw+612,1);badger_bytes_store_u32(raw+616,30000000u);
    FILE *file=fopen(argv[1],"wb");
    if(!file){perror("Fixture report");return 1;}
    int ok=fwrite(raw,1,sizeof(raw),file)==sizeof(raw);
    if(fclose(file))ok=0;
    if(!ok){fputs("Fixture report write failed\n",stderr);return 1;}
    clean_network_print(&b.report);puts("");
    return 0;
}
