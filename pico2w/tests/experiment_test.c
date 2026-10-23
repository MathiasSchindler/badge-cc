#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app/experiment.h"
#include "usb/report.h"

static experiment_t lab;
static health_t health;
static sx1262_receiver_t radio;
static uint8_t response[HTTP_RESPONSE_MAX];
static uint32_t now_us, transmitted, frames;
static int32_t failure;

uint32_t sx1262_hw_time_us(void) { return ++now_us; }
void sx1262_hw_release(void) {}
void sx1262_rx_lorawan_config(sx1262_rx_config_t *p) { *p=(sx1262_rx_config_t){868100000,7,125,0}; }
int32_t sx1262_receiver_configure(sx1262_receiver_t *rx,const sx1262_rx_config_t *p) {
    if(failure) return failure;
    rx->active=*p;rx->state=SX1262_RX_RUNNING;return 0;
}
int32_t sx1262_transfer(const uint8_t *out,uint8_t *in,uint32_t n,uint8_t *tail,
                        uint32_t tail_n,int32_t (*service)(void)) {
    (void)tail;(void)service;assert(!tail_n);
    ++frames;memset(in,0,n);
    if(n>1)in[1]=0x52;
    if(out[0]==0x83)++transmitted;
    if(out[0]==0x12)in[3]=1;
    return 0;
}
static uint32_t request(const char *path,const char *body,int allowed) {
    http_request_t r={0};
    strcpy(r.path,path);r.post=body!=NULL;r.write_allowed=(uint8_t)allowed;
    r.body=(const uint8_t *)body;r.body_size=body?(uint32_t)strlen(body):0;
    uint32_t n=experiment_http(&lab,&health,&radio,&r,response,sizeof(response));
    assert(n && n<sizeof(response));response[n]=0;return n;
}
static void fixture(void) {
    memset(&lab,0,sizeof(lab));memset(&health,0,sizeof(health));memset(&radio,0,sizeof(radio));
    uint32_t id[2]={1,2};health_init(&health,id,0);experiment_init(&lab);
    transmitted=frames=0;failure=0;
    assert(!experiment_configure(&lab,&radio));
}
static void status_is(const char *code) { assert(!memcmp(response,code,strlen(code))); }

int main(int argc,char **argv) {
    fixture();
    if(argc==2) {
        const char *path=!strcmp(argv[1],"--page")?"/":!strcmp(argv[1],"--script")?"/radio.js":"/api/radio";
        request(path,NULL,0);
        printf("%s",strstr((char *)response,"\r\n\r\n")+4);return 0;
    }
    request("/api/radio",NULL,0);status_is("HTTP/1.1 200");
    assert(strstr((char *)response,"\"airtime_us\":77056"));
    request("/api/radio/config","frequency_hz=869850000&sf=8&bandwidth_khz=250&coding_rate=2&preamble=12",0);
    status_is("HTTP/1.1 403");assert(lab.revision==1);
    const char *invalid[]={
        "", "frequency_hz=869850000&sf=8&bandwidth_khz=250&coding_rate=2&preamble=12&",
        "frequency_hz=42949672960&sf=8&bandwidth_khz=250&coding_rate=2&preamble=12",
        "frequency_hz=869850000&sf=8&bandwidth_khz=500&coding_rate=2&preamble=12",
        "frequency_hz=869850000&sf=6&bandwidth_khz=125&coding_rate=1&preamble=8",
        "frequency_hz=869850000&sf=8&bandwidth_khz=125&coding_rate=5&preamble=8",
        "frequency_hz=869850000&sf=8&bandwidth_khz=125&coding_rate=1&preamble=33",
        "frequency_hz=868000000&sf=8&bandwidth_khz=125&coding_rate=1&preamble=8"
    };
    uint32_t before=frames;
    for(uint32_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        request("/api/radio/config",invalid[i],1);status_is("HTTP/1.1 400");assert(frames==before);
    }
    request("/api/radio/config","frequency_hz=868300000&sf=9&bandwidth_khz=500&coding_rate=2&preamble=12",1);
    status_is("HTTP/1.1 200");assert(lab.revision==2&&lab.profile.coding_rate==2&&radio.active.bandwidth_khz==500);
    request("/api/radio/send","count=4&token=1",1);status_is("HTTP/1.1 400");
    request("/api/radio/send","count=1&token=0",1);status_is("HTTP/1.1 400");
    request("/api/radio/send","count=3&token=4294967295",1);status_is("HTTP/1.1 200");assert(lab.queued==3);
    request("/api/radio/reset","",1);status_is("HTTP/1.1 409");
    experiment_tick(&lab,&health,&radio,59999);assert(!transmitted);
    experiment_tick(&lab,&health,&radio,60000);assert(transmitted==1&&lab.queued==2);
    experiment_tick(&lab,&health,&radio,119999);assert(transmitted==1);
    experiment_tick(&lab,&health,&radio,120000);assert(transmitted==2&&lab.queued==1);
    request("/api/radio/cancel","",1);assert(!lab.queued);
    request("/api/radio/reset","x",1);status_is("HTTP/1.1 400");
    request("/api/radio/reset","",1);status_is("HTTP/1.1 200");
    assert(lab.profile.radio.frequency_hz==LINK_FREQUENCY_HZ&&experiment_wait(&lab,120000)==60000);
    request("/api/radio/config","frequency_hz=869850000&sf=12&bandwidth_khz=125&coding_rate=4&preamble=32",1);
    status_is("HTTP/1.1 200");assert(link_profile_airtime_us(&lab.profile)==3547136);
    experiment_charge(&lab,180000);
    assert(lab.guard_ms==359800&&experiment_wait(&lab,539799)==1&&!experiment_wait(&lab,539800));
    request("/api/radio/reset","",1);assert(lab.guard_ms==359800); /* reconfiguration never clears debt */
    failure=SX1262_SPI_ERROR;
    request("/api/radio/config","frequency_hz=869850000&sf=8&bandwidth_khz=125&coding_rate=1&preamble=8",1);
    status_is("HTTP/1.1 503");assert(lab.profile.radio.spreading_factor==7&&lab.config_error&&lab.restore_error);
    fixture();
    sx1262_rx_packet_t p={0};uint32_t peer[2]={3,4};uint8_t nonce[16];
    usb_store_u32(nonce,EXPERIMENT_MAGIC);usb_store_u32(nonce+4,42);usb_store_u32(nonce+8,1);usb_store_u32(nonce+12,3);
    link_test_payload(p.payload,peer,1,nonce);p.sequence=1;p.length=36;p.rssi_qdb=-280;p.snr_qdb=44;
    experiment_observe(&lab,&p,100);assert(lab.run_count==1&&lab.runs[0].mask==1);
    experiment_observe(&lab,&p,101);assert(!lab.runs[0].duplicates); /* same ring snapshot */
    p.sequence=2;experiment_observe(&lab,&p,102);assert(lab.runs[0].duplicates==1);
    usb_store_u32(nonce+8,3);link_test_payload(p.payload,peer,3,nonce);p.sequence=3;
    experiment_observe(&lab,&p,103);assert(lab.runs[0].mask==5);
    request("/api/radio",NULL,0);assert(strstr((char *)response,"\"received_unique\":2,\"not_yet_observed\":1"));
    p.payload[32]^=1;p.sequence=4;experiment_observe(&lab,&p,104);assert(lab.malformed==1);
    p.flags=SX1262_RX_CRC_ERROR;p.sequence=5;experiment_observe(&lab,&p,105);assert(lab.malformed==1);
    fixture();
    for(uint32_t i=1;i<=20;++i) {
        p.flags=0;p.sequence=i;usb_store_u32(nonce+4,i);link_test_payload(p.payload,peer,i,nonce);
        experiment_observe(&lab,&p,i);
    }
    assert(lab.run_count==EXPERIMENT_RUNS);
    request("/api/radio",NULL,0);assert(strlen((char *)response)<6144u);
    lab.last_tx_ms=0xfffffff0u;lab.guard_ms=60000;
    assert(experiment_wait(&lab,59983)==1&&!experiment_wait(&lab,59984));
    puts("PASS: radio API validation/CSRF, profile rollback, RAM reset, queued batch/cancel, shared airtime debt, receive correlation/dedup/CRC/history");
    return 0;
}
