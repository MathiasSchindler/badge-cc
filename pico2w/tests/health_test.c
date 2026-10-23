#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app/health.h"
#include "usb/report.h"

static health_t health;
static uint8_t response[HTTP_RESPONSE_MAX];

int main(int argc, char **argv) {
    const uint32_t id[2] = {0x5e2cd5b4u, 0x76edd29au};
    health_init(&health, id, 0u);
    health_snapshot_t state = {7u, 0u, 3u, 0u, 0xc0a8b244u, 1u, SX1262_RX_RUNNING, 0};
    health_update(&health, &state);
    assert(!health_due(&health, 59999) && health_due(&health, 60000));
    uint8_t payload[36];
    health_payload(&health, 60000u, payload);
    assert(health.sequence == 1 && !health.pending && health.attempted);
    assert(usb_load_u32(payload) == HEALTH_MAGIC && usb_load_u32(payload+4) == id[0]);
    assert(usb_load_u32(payload+8) == id[1] && usb_load_u32(payload+12) == 1);
    assert(usb_load_u32(payload+16) == 60000u && usb_load_u32(payload+24) == state.ip);
    assert(usb_load_u32(payload+32) == usb_crc32(payload,32));
    uint32_t bits = usb_load_u32(payload+20);
    assert((bits&15) == 7 && ((bits>>12)&15) == 3 && ((bits>>24)&1) == 1 && ((bits>>25)&3) == 1);
    health_update(&health, &state);
    assert(!health.pending && !health_due(&health,359999) && health_due(&health,360000));
    /* Wi-Fi failure still yields a LoRa health report; changes cannot bypass spacing. */
    state.wifi_stage = 8; state.wifi_error = 7; state.connected = 0; state.ip = 0;
    health_update(&health,&state);
    assert(health.pending && !health_due(&health,119999) && health_due(&health,120000));
    health_payload(&health,120000,payload);
    bits = usb_load_u32(payload+20);
    assert((bits&15) == 8 && ((bits>>4)&255) == 7 && !((bits>>24)&1));
    /* DHCP failure is reported independently of association success. */
    state.wifi_stage = 7; state.wifi_error = 0; state.connected = 1;
    state.dhcp_state = 5; state.network_error = 1;
    health_update(&health,&state);
    assert(health_due(&health,180000));
    health_payload(&health,180000,payload);
    bits = usb_load_u32(payload+20);
    assert(((bits>>12)&15) == 5 && ((bits>>16)&255) == 1);
    /* A broken radio prevents TX but leaves diagnostics available over HTTP. */
    state.radio_state = SX1262_RX_FAULT; state.radio_error = -3005;
    health_update(&health,&state);
    health.tx_error = -3021; health.restore_error = -3005;
    assert(!health_due(&health,1000000));
    http_request_t request = {0};
    strcpy(request.path,"/api/health");
    uint32_t size = health_http(&health,&request,response,sizeof(response));
    assert(size && size < sizeof(response)); response[size] = 0;
    assert(!memcmp(response,"HTTP/1.1 200 OK",15));
    assert(strstr((char *)response,"\"radio_error\":-3005"));
    assert(strstr((char *)response,"\"tx_error\":-3021"));
    assert(strstr((char *)response,"\"restore_error\":-3005"));
    assert(strstr((char *)response,"\"delivery_confirmed\":false"));
    assert(strstr((char *)response,"76EDD29A5E2CD5B4"));
    if (argc == 2 && !strcmp(argv[1],"--json")) {
        printf("%s",strstr((char *)response,"\r\n\r\n")+4);
        return 0;
    }
    request.post = 1;
    size = health_http(&health,&request,response,sizeof(response));
    assert(size && !memcmp(response,"HTTP/1.1 403",12));
    request.post = 0; request.path[1] = 0;
    size = health_http(&health,&request,response,sizeof(response));
    assert(size && size < sizeof(response));
    response[size] = 0;
    if (argc == 2 && !strcmp(argv[1],"--page")) {
        printf("%s",strstr((char *)response,"\r\n\r\n")+4);
        return 0;
    }
    state.radio_state = SX1262_RX_RUNNING; state.radio_error = 0;
    health_update(&health,&state);
    assert(health_due(&health,1000000));
    health_payload(&health,0xfffffff0u,payload);
    health.pending = 1;
    assert(!health_due(&health,59983u) && health_due(&health,59984u));
    health.sequence = health.sent = health.last_attempt_ms = health.last_success_ms = 0xffffffffu;
    health.current.radio_error = health.tx_error = health.restore_error = (-2147483647-1);
    strcpy(request.path,"/api/health");
    size = health_http(&health,&request,response,sizeof(response));
    assert(size && size < sizeof(response));
    puts("PASS: autonomous health startup/change/heartbeat, spacing/wrap, Wi-Fi/DHCP failure packets, LoRa fault HTTP and bounds");
    return 0;
}
