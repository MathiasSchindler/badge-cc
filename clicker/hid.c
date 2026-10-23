#include "hid.h"
#include "host.h"
#include "display.h"
#include "wifi_board.h"
#include "../src/radio/wifi_firmware.h"
#include "../src/tools/diagnostics_logic.h"
#include "../src/arch/rp2350/bootrom_reboot.h"

static clicker_host_t host;
static clicker_bonds_t bonds;
static const clicker_bond_io_t storage={0,clicker_flash_erase,clicker_flash_program,clicker_flash_read};
static uint32_t now(void) {
    const wifi_wire_t *wire=wifi_board_wire();return wire->now_us(wire->context);
}
static void number(badger_canvas_t *canvas,uint32_t y,const char *label,uint32_t n) {
    char text[9];diag_hex(text,n,8u);
    badger_gfx_text(canvas,4,(int32_t)y,label,1u,0xffffu);
    badger_gfx_text(canvas,112,(int32_t)y,text,1u,0x07ffu);
}
static void draw(badger_canvas_t *canvas) {
    badger_gfx_rect(canvas,0,124,320,116,0x0843u);
    badger_gfx_text(canvas,4,128,"GITHUB BADGE PRESENTATION CLICKER",1u,0xffffu);
    const char *status=host.error?"ERROR - RADIO OFF":
        host.closing?"DISCONNECTING":
        host.pair_error?"PAIRING FAILED":
        host.pairing==1u?"PREPARING PASSKEY":
        host.pairing?"ENTER PASSKEY ON COMPUTER":
        clicker_host_ready(&host)?"CONNECTED - READY":
        host.encrypted?"PAIRED - WAITING FOR HID":
        host.connected?"CONNECTED - SECURING LINK":
        host.new_only?"PAIR A NEW COMPUTER":"PAIR OR RECONNECT ON COMPUTER";
    badger_gfx_text(canvas,4,142,status,1u,host.error?0xf800u:0xffe0u);
    if (host.pairing>=2u && host.pairing<=4u) {
        char digits[7];uint32_t code=host.passkey;
        digits[6]='\0';
        for (uint32_t i=0u;i<6u;++i) {digits[5u-i]=(char)('0'+code%10u);code/=10u;}
        badger_gfx_text(canvas,4,158,"PASSKEY",1u,0xffffu);
        badger_gfx_text(canvas,112,154,digits,2u,0x07ffu);
    } else number(canvas,158u,"NOTIFICATIONS",host.notifications);
    uint32_t count=0u;for (uint32_t i=0u;i<CLICKER_BONDS;++i) count+=bonds.peers[i].valid!=0u;
    number(canvas,176u,"SAVED COMPUTERS",count);
    number(canvas,188u,"ERROR/PAIR",((uint32_t)host.error<<16u)|host.pair_error);
    number(canvas,200u,host.error==5u?"FLASH ROM ERROR":"CMD/STATUS",
           host.error==5u?(uint32_t)clicker_flash_error():((uint32_t)host.command_opcode<<16u)|host.last_status);
    number(canvas,212u,host.error?"EVENT/META/DISC":host.pair_error?"PAIR FEATURES":"SM/ENC/DISC",
           host.error?((uint32_t)host.last_event<<24u)|((uint32_t)host.last_meta<<16u)|host.disconnect_reason:
           host.pair_error?host.pairing_features:
           ((uint32_t)(host.pairing?host.pairing:host.stopped_pairing)<<24u) |
           ((uint32_t)host.last_smp<<16u) | ((uint32_t)host.encryption_status<<8u) | host.disconnect_reason);
    badger_gfx_text(canvas,4,228,host.error?"B TO BOOTSEL":
#if CLICKER_BOARD == 2025
                    "HOLD A+UP+DOWN FOR NEW COMPUTER",
#else
                    "HOLD LEFT+UP+DOWN FOR NEW COMPUTER",
#endif
                    1u,0xffffu);
    badger_display_begin_region(0u,124u,320u,116u);
    for (uint32_t x=0u;x<320u;++x) badger_display_write_pixels(&canvas->pixels[x*240u+124u],116u);
    badger_display_end_update();
}
void clicker_hid_run(badger_canvas_t *canvas,clicker_input_t *input,
                      uint8_t (*buttons_read)(void),clicker_bt_t *bt,
                      const uint8_t address[6],uint16_t acl_size,uint8_t acl_packets) {
    if (!clicker_bonds_load(&bonds,&storage) ||
        (!bonds.initialized && !clicker_bonds_save(&bonds,&storage))) host.error=5u;
    else (void)clicker_host_init(&host,bt,&bonds,address,acl_size,acl_packets);
    uint32_t drawn=now();
    clicker_chord_t chord={0u,0u,0u,0u};
    draw(canvas);
    while (!host.error) {
        if (clicker_host_pump(&host)<0) break;
        uint32_t tick=now();uint8_t raw=buttons_read();
        clicker_input_sample(input,raw,tick);
        uint8_t was_blocked=chord.blocked;
        if (clicker_chord_update(&chord,raw,input->stable,tick)) clicker_host_new_computer(&host);
        if (chord.blocked && (!was_blocked || input->count)) clicker_input_cancel_taps(input);
        clicker_input_set_ready(input,(uint8_t)clicker_host_ready(&host));
        if (input->overflow) {host.error=2u;break;}
        uint8_t report[8];
        if (clicker_input_peek(input,report) && clicker_host_notify(&host,report)) clicker_input_accept(input);
        if (!clicker_host_persist(&host,&storage)) break;
        if (tick-drawn>=250000u) {drawn=tick;draw(canvas);}
    }
    wifi_firmware_stop();wifi_wire_power_off(wifi_board_wire());
    draw(canvas);clicker_input_init(input);clicker_input_set_ready(input,1u);
    for (;;) {
        uint8_t report[8];clicker_input_sample(input,buttons_read(),now());
        if (clicker_input_peek(input,report)) {
            if (report[2]==CLICKER_KEY_BLANK) {
                int32_t result=badger_reboot_to_bootsel();
                host.last_status=(uint8_t)result;draw(canvas);
            }
            clicker_input_accept(input);
        }
    }
}
