#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/usb/live_usb.h"

static live_usb_control_t usb;

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
    const uint8_t packet[8]={type,request,(uint8_t)value,(uint8_t)(value>>8u),
        (uint8_t)index,(uint8_t)(index>>8u),(uint8_t)length,(uint8_t)(length>>8u)};
    live_usb_control_setup(&usb,packet);
}

static void configure(void) {
    setup(0u,5u,12u,0u,0u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    setup(0u,9u,1u,0u,0u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    assert(usb.configured);
}

static void wiped(void) {
    assert(!usb.clean_secret_ready && !usb.clean_secret_out);
    for (unsigned i=0u;i<64u;++i) assert(!usb.clean_secret[i]);
}

static void provision(void) {
    uint8_t secret[64];
    memset(secret,0xa5,sizeof(secret));
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,64u);
    assert(usb.action==USB_ACTION_OUT && !usb.clean_secret_ready);
    live_usb_control_complete(&usb,0u,secret,64u);
    assert(!usb.clean_secret_ready && usb.clean_secret_out==2u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    assert(usb.clean_secret_ready && !usb.clean_secret_out);
    assert(!memcmp(secret,usb.clean_secret,64u));
}

int main(void) {
    live_usb_control_init(&usb,123u,"0123456789ABCDEF");
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    configure();
    provision();
    setup(0xc0u,LIVE_USB_INFO,0u,0u,12u);
    assert(usb.clean_secret_ready && usb.length==12u && usb.data[0]=='U');
    live_usb_control_complete(&usb,1u,NULL,12u);
    live_usb_control_complete(&usb,0u,NULL,0u);
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,64u);
    assert(!usb.clean_secret_ready);
    setup(0xc0u,LIVE_USB_INFO,0u,0u,12u);
    wiped();
    uint8_t secret[64];
    memset(secret,0x5a,sizeof(secret));
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,64u);
    live_usb_control_complete(&usb,0u,secret,64u);
    setup(0xc0u,LIVE_USB_INFO,0u,0u,12u);
    wiped();
    provision();
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,63u);
    assert(usb.phase==USB_PHASE_STALL);
    wiped();
    provision();
    setup(0x40u,LIVE_USB_CLEAN_SECRET,1u,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    wiped();
    provision();
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,1u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    wiped();
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,64u);
    live_usb_control_complete(&usb,0u,secret,32u);
    assert(usb.phase==USB_PHASE_STALL);
    wiped();
    provision();
    usb.probe_inflight=1u;
    setup(0x40u,LIVE_USB_CLEAN_SECRET,0u,0u,64u);
    assert(usb.phase==USB_PHASE_STALL);
    wiped();
    usb.probe_inflight=0u;
    provision();
    setup(0u,9u,0u,0u,0u);
    live_usb_control_complete(&usb,1u,NULL,0u);
    assert(!usb.configured);
    wiped();
    configure();
    provision();
    live_usb_control_reset(&usb);
    wiped();
    puts("clean secret: configured-only, status commit, abort, overwrite, lengths, request fields, inflight, deconfigure and reset pass");
    return 0;
}
