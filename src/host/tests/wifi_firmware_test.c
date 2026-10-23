#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../radio/wifi_firmware.h"
#include "../../radio/wifi_control.h"
#include "../../tools/diagnostics_logic.h"

static uint8_t memory[0x80000];
static uint8_t firmware[65540], nvram[68], clm[600];
static uint8_t queue[2040];
static uint8_t data_queue[2040];
static uint32_t data_size;
static uint8_t inject_data;
static uint32_t queue_size, window_base, ticks, off_count, confirmed;
static uint32_t arm_reset, ram_reset, arm_io, ram_io, token;
static uint32_t corrupt_address, controller_error, wrong_id;
static uint32_t reply_interface;
static int missing_ht, cancel_download;
static uint32_t asset_calls, advance_ticks, advanced, advance_at;
static wifi_fw_stage_t advance_stage;
static int cancel_assets;
static wifi_fw_assets_t assets;
static uint32_t raw_packets;
static uint8_t live_backplane;
static uint32_t diagnostic_sent;

static void raw_receive(void *context, const uint8_t *packet, uint32_t length) {
    assert(context == &raw_packets);
    wifi_control_packet_t decoded;
    assert(wifi_control_parse_raw(packet, length, &decoded) && decoded.size == length);
    assert(decoded.channel != 0u);
    ++raw_packets;
}

static uint32_t read32(const uint8_t *bytes) { return usb_report_u32(bytes); }
static void store(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) bytes[i] = (uint8_t)(value >> (8u*i));
}
static void write_pin(void *context, wifi_signal_t signal, uint8_t high) {
    (void)context; (void)signal; (void)high;
}
static void output(void *context, uint8_t on) { (void)context; (void)on; }
static uint8_t input(void *context) { (void)context; return 0u; }
static uint32_t clock_tick(void *context) { (void)context; return ticks++; }

wifi_bus_result_t wifi_probe_run(const wifi_wire_t *wire, wifi_probe_report_t *report,
                                 wifi_probe_progress_t progress, void *context) {
    (void)wire; (void)progress; (void)context;
    report->chip_id = 0x1545a9afu;
    report->result = WIFI_BUS_OK;
    return WIFI_BUS_OK;
}
void wifi_wire_power_off(const wifi_wire_t *wire) { (void)wire; ++off_count; }
wifi_bus_result_t wifi_wire_delay(const wifi_wire_t *wire, uint32_t duration) {
    (void)wire; ticks += duration; return WIFI_BUS_OK;
}
wifi_bus_result_t wifi_gspi_write(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                                  uint8_t size, wifi_word_order_t order, uint32_t value) {
    (void)wire; (void)size;
    assert(order == WIFI_ORDER_NORMAL32);
    if (function == 0u) { assert(address == 4u || address == 0xf0u); return WIFI_BUS_OK; }
    assert(function == 1u);
    if (address >= 0x1000au && address <= 0x1000cu) {
        unsigned shift = 8u + 8u*(address-0x1000au);
        window_base = (window_base & ~(255u << shift)) | (value << shift);
        return WIFI_BUS_OK;
    }
    if (address >= 0x10000u) {
        assert(address == 0x1000eu || address == 0x10008u || address == 0x1001eu ||
               address == 0x1001fu || address == 0x1000fu);
        return WIFI_BUS_OK;
    }
    if (size == 2u) assert(!(address & 0x8000u));
    address = window_base | (address & 0x7fffu);
    if (address == 0x18103800u) {
        assert(value == 0u && token != 0u);
        assert(memcmp(memory, firmware, sizeof(firmware)) == 0);
        assert(memcmp(memory+0x80000u-4u-sizeof(nvram), nvram, sizeof(nvram)) == 0);
        arm_reset = value;
    } else if (address == 0x18104800u) ram_reset = value;
    else if (address == 0x18103408u) arm_io = value;
    else if (address == 0x18104408u) ram_io = value;
    else if (address == 0x7fffcu) token = value;
    else if (address + 4u <= sizeof(memory)) {
        if (size == 2u) {
            memory[address] = (uint8_t)value;
            memory[address+1u] = (uint8_t)(value >> 8u);
        } else store(memory + address, value);
    }
    else assert(address == 0x18004010u || address == 0x18004044u || address == 0x18002024u);
    return WIFI_BUS_OK;
}
wifi_bus_result_t wifi_gspi_read(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                                 uint8_t size, wifi_word_order_t order, uint32_t *value) {
    (void)wire; (void)size; assert(order == WIFI_ORDER_NORMAL32);
    if (function == 0u) {
        assert(address == 8u || address == 4u);
        uint32_t size=data_size?data_size:queue_size;
        *value = address == 4u ? 0u : 0x20u | (size ? 0x100u | size << 9u : 0u);
    } else if (address == 0x1000eu) *value = 0x40u | (!arm_reset && !missing_ht ? 0x80u : 0u);
    else if (address == 0x1001eu) *value = 0u;
    else if (address == 0x1001fu) *value = 3u;
    else {
        if (size == 2u) assert(!(address & 0x8000u));
        address = window_base | (address & 0x7fffu);
        if (address == 0x18103800u) *value = arm_reset;
        else if (address == 0x18104800u) *value = ram_reset;
        else if (address == 0x18103408u) *value = arm_io;
        else if (address == 0x18104408u) *value = ram_io;
        else if (address == 0x7fffcu) *value = token;
        else if (address + 4u <= sizeof(memory)) {
            *value = size == 2u ? (uint32_t)memory[address] | (uint32_t)memory[address+1u] << 8u :
                read32(memory + address);
        }
        else assert(!"Unexpected register read");
    }
    return WIFI_BUS_OK;
}

static wifi_bus_result_t transfer(const wifi_wire_t *wire, const uint8_t *tx,
                                  uint32_t tx_length, uint8_t *rx, uint32_t rx_length) {
    (void)wire;
    uint32_t cmd = read32(tx), size = cmd & 0x7ffu, fn = cmd >> 28u & 3u;
    uint32_t address = cmd >> 11u & 0x1ffffu;
    assert(size > 0u && size <= 2040u && (cmd & (1u << 30u)) != 0u);
    if (fn == 1u) {
        assert((address & 0x8000u) == (cmd & 0x80000000u ? 0x8000u : 0u));
        address = window_base | (address & 0x7fffu);
        assert(size <= WIFI_FW_BACKPLANE_BLOCK && address+size <= sizeof(memory));
        assert((address & 0x7fffu) + size <= 0x8000u);
        if (live_backplane) assert((address & 0xfffu) + size <= 0x1000u);
        if (cmd & 0x80000000u) {
            assert(tx_length >= size+4u && rx_length == 0u && (arm_reset || live_backplane));
            memcpy(memory+address, tx+4u, size);
        } else {
            assert(rx_length >= size+16u && tx_length == 4u && (arm_reset || live_backplane));
            memset(rx, 0x99, rx_length);
            memcpy(rx+16u, memory+address, size);
            if (corrupt_address >= address && corrupt_address < address+size) rx[16u+corrupt_address-address] ^= 1u;
            confirmed += size;
        }
    } else {
        assert(fn == 2u && !arm_reset);
        if (cmd & 0x80000000u) {
            wifi_control_packet_t packet;
            assert(wifi_control_parse(tx+4u, size, &packet));
            if(packet.channel==2u) {
                if (packet.payload_size > WIFI_ETHERNET_MAX + 4u) {
                    assert(packet.payload[0] == 0x20u && packet.payload[2] == 1u);
                    diagnostic_sent = packet.payload_size - 4u;
                    return WIFI_BUS_OK;
                }
                const uint8_t *ethernet;uint8_t interface;uint32_t frame_length;
                assert(wifi_data_decode(&packet,&interface,&ethernet,&frame_length));
                queue_size=wifi_data_frame(queue,0u,interface,ethernet,frame_length);
                queue[9]=tx[8u]+2u;
                return WIFI_BUS_OK;
            }
            assert(packet.command == 262u || packet.command == 263u);
            uint8_t payload[192] = {0};
            uint32_t reply_size;
            if (packet.command == 262u && memcmp(packet.payload, "ver\0", 4u) == 0) {
                const char version[] = "wl0: Sep 15 2026 version TEST\r\n";
                memcpy(payload, version, sizeof(version)); reply_size = sizeof(version);
            } else if (packet.command == 263u && memcmp(packet.payload, "clmload", 7u) == 0) {
                assert((read32(packet.payload+8u) >> 16u) == 2u);
                assert((packet.payload_size & 7u) == 0u);
                uint32_t data_end = read32(packet.payload+12u) + 20u;
                assert(data_end <= packet.payload_size);
                for (uint32_t i=data_end;i<packet.payload_size;++i) assert(packet.payload[i]==0u);
                reply_size = 0u;
            } else {
                assert(memcmp(packet.payload, "clmload_status", 14u) == 0); reply_size = 4u;
            }
            queue_size = wifi_control_request(queue, 0u, (uint16_t)(packet.id+wrong_id),
                                               packet.command, 0u, payload, reply_size);
            queue[9] = tx[8u]+2u;
            store(queue+20u, read32(queue+20u) | (reply_interface << 12u));
            store(queue+24u, controller_error);
            if (controller_error) queue[20] |= 1u;
            if(inject_data) {
                const uint8_t ethernet[14]={2u,3u,4u,5u,6u,7u};
                data_size=wifi_data_frame(data_queue,0u,1u,ethernet,sizeof(ethernet));
                data_queue[9]=tx[8u]+2u;
                inject_data=0u;
            }
        } else {
            if(data_size) {
                assert(size==data_size && rx_length>=size);
                memcpy(rx,data_queue,data_size);data_size=0u;
            } else {
                assert(size == queue_size && rx_length >= size);
                memcpy(rx, queue, queue_size);
                queue_size = 0u;
            }
        }
    }
    return WIFI_BUS_OK;
}

static int progress(void *context, const wifi_fw_report_t *report) {
    (void)context;
    if (report->stage == WIFI_FW_IDLE) {
        ++asset_calls;
        ticks += 5000u;
    }
    if (advance_ticks != 0u && !advanced && report->stage == advance_stage) {
        advance_at = ticks;
        ticks += advance_ticks;
        advanced = 1u;
    }
    if (cancel_assets && asset_calls == 3u) return 0;
    return !(cancel_download && report->stage == WIFI_FW_DOWNLOAD);
}

static void initialize(void) {
    for (unsigned i=0u;i<sizeof(firmware);++i) firmware[i]=(uint8_t)(i*31u+7u);
    for (unsigned i=0u;i<sizeof(nvram);++i) nvram[i]=(uint8_t)(i*13u);
    for (unsigned i=0u;i<sizeof(clm);++i) clm[i]=(uint8_t)(i+1u);
    memset(memory, 0, sizeof(memory));
    queue_size=window_base=ticks=off_count=confirmed=token=0u;
    data_size=inject_data=diagnostic_sent=0u;
    controller_error=wrong_id=reply_interface=0u;
    corrupt_address=UINT32_MAX;
    arm_reset=ram_reset=1u; arm_io=ram_io=0u;
    cancel_download=missing_ht=0;
    asset_calls=advance_ticks=advanced=advance_at=0u;
    advance_stage=WIFI_FW_IDLE;
    cancel_assets=0;
    assets=(wifi_fw_assets_t){firmware,sizeof(firmware),diag_crc32(firmware,sizeof(firmware)),
        nvram,sizeof(nvram),diag_crc32(nvram,sizeof(nvram)),clm,sizeof(clm),diag_crc32(clm,sizeof(clm))};
}

static void protocol_tests(void) {
    uint8_t large_frame[2024], guarded[WIFI_CONTROL_MAX + 8u];
    memset(large_frame, 0x5a, sizeof(large_frame));
    for (uint32_t n = 14u; n <= 2022u; ++n) {
        memset(guarded, 0xa5, sizeof(guarded));
        uint32_t aligned = wifi_data_frame_diagnostic(guarded, 255u, 1u, large_frame, n);
        assert(aligned == ((n + 21u) & ~3u) && aligned <= WIFI_CONTROL_MAX);
        assert(memcmp(guarded + 18u, large_frame, n) == 0);
        for (uint32_t i = n + 18u; i < aligned; ++i) assert(!guarded[i]);
        for (uint32_t i = aligned; i < sizeof(guarded); ++i) assert(guarded[i] == 0xa5u);
        wifi_control_packet_t decoded;
        const uint8_t *frame; uint32_t length; uint8_t interface;
        assert(wifi_control_parse_raw(guarded,aligned,&decoded));
        assert(wifi_data_decode_diagnostic(&decoded,&interface,&frame,&length));
        assert(interface==1u && length==n && memcmp(frame,large_frame,n)==0);
        assert(wifi_data_decode(&decoded,&interface,&frame,&length)==(n<=WIFI_ETHERNET_MAX));
        assert(!!wifi_data_frame(guarded, 0u, 0u, large_frame, n) == (n <= WIFI_ETHERNET_MAX));
    }
    assert(!wifi_data_frame_diagnostic(guarded, 0u, 1u, large_frame, 2023u));
    assert(!wifi_data_frame_diagnostic(guarded, 0u, 2u, large_frame, 62u));
    assert(!wifi_data_frame_diagnostic(guarded, 0u, 1u, large_frame, 13u));
    assert(!wifi_data_frame_diagnostic(guarded, 0u, 1u, NULL, 62u));
    assert(!wifi_data_frame_diagnostic(NULL, 0u, 1u, large_frame, 62u));
    memset(guarded,0,sizeof(guarded));
    guarded[0]=(uint8_t)WIFI_CONTROL_MAX; guarded[1]=(uint8_t)(WIFI_CONTROL_MAX>>8u);
    guarded[2]=(uint8_t)~WIFI_CONTROL_MAX; guarded[3]=(uint8_t)(~WIFI_CONTROL_MAX>>8u);
    guarded[5]=2u; guarded[7]=12u; guarded[12]=0x20u;
    wifi_control_packet_t maximum;
    const uint8_t *maximum_frame; uint32_t maximum_length; uint8_t maximum_interface;
    assert(wifi_control_parse_raw(guarded,WIFI_CONTROL_MAX,&maximum));
    assert(wifi_data_decode_diagnostic(&maximum,&maximum_interface,&maximum_frame,&maximum_length));
    assert(maximum_interface==0u && maximum_length==2024u && maximum_frame==guarded+16u);
    assert(!wifi_data_decode(&maximum,&maximum_interface,&maximum_frame,&maximum_length));
    ++maximum.payload_size;
    assert(!wifi_data_decode_diagnostic(&maximum,&maximum_interface,&maximum_frame,&maximum_length));
    assert(!wifi_data_decode_diagnostic(NULL,&maximum_interface,&maximum_frame,&maximum_length));
    uint8_t packet[2040], payload[192] = "ver";
    uint32_t size = wifi_control_request(packet, 255u, 15u, 262u, 0u, payload, sizeof(payload));
    wifi_control_packet_t decoded;
    assert(size == 220u && wifi_control_parse(packet,size,&decoded));
    assert(decoded.id == 15u && decoded.command == 262u && decoded.payload_size == 192u);
    char version[192]; assert(wifi_control_version(&decoded,15u,version) && strcmp(version,"ver")==0);
    assert(!wifi_control_version(&decoded,14u,version));
    for (uint32_t i=0u;i<12u;++i) assert(!wifi_control_parse(packet,i,&decoded));
    packet[2]^=1u; assert(!wifi_control_parse(packet,size,&decoded)); packet[2]^=1u;
    packet[7]=250u; assert(!wifi_control_parse(packet,size,&decoded)); packet[7]=12u;
    packet[16]=255u; assert(!wifi_control_parse(packet,size,&decoded)); packet[16]=192u;
    memmove(packet+48u,packet+12u,size-12u);
    memset(packet+12u,0u,36u);
    size+=36u;
    packet[0]=(uint8_t)size; packet[1]=(uint8_t)(size>>8u);
    packet[2]=(uint8_t)~size; packet[3]=(uint8_t)(~size>>8u);
    packet[7]=48u;
    assert(wifi_control_parse(packet,size,&decoded));
    assert(wifi_control_version(&decoded,15u,version));
    packet[64u+3u]='\n'; packet[64u+4u]='x'; packet[64u+5u]=0u;
    assert(wifi_control_parse(packet,size,&decoded) && !wifi_control_version(&decoded,15u,version));
    packet[64u+4u]=0u;
    assert(wifi_control_parse(packet,size,&decoded) && wifi_control_version(&decoded,15u,version));
    size=wifi_control_request(packet,255u,15u,262u,0u,payload,sizeof(payload));
    memset(packet+28u,'x',192u); assert(wifi_control_parse(packet,size,&decoded));
    assert(!wifi_control_version(&decoded,15u,version));
    assert(wifi_control_request(packet,0u,0u,262u,0u,payload,4u)==0u);
    uint8_t ethernet[17]={2u,3u,4u};
    size=wifi_data_frame(packet,255u,1u,ethernet,sizeof(ethernet));
    assert(size==36u && packet[0]==35u && packet[7]==14u && packet[35]==0u);
    assert(wifi_control_parse(packet,size,&decoded));
    const uint8_t *frame;uint8_t interface;uint32_t frame_length;
    assert(wifi_data_decode(&decoded,&interface,&frame,&frame_length));
    assert(interface==1u && frame_length==17u && memcmp(frame,ethernet,17u)==0);
    packet[17]=255u;
    assert(wifi_control_parse(packet,size,&decoded) && !wifi_data_decode(&decoded,&interface,&frame,&frame_length));
    packet[17]=0u;packet[14]=0x10u;
    assert(wifi_control_parse(packet,size,&decoded) && !wifi_data_decode(&decoded,&interface,&frame,&frame_length));
    for (uint32_t channel = 1u; channel < 16u; ++channel) {
        packet[5] = (uint8_t)channel;
        assert(wifi_control_parse(packet,size,&decoded) == (channel <= 2u));
        assert(wifi_control_parse_raw(packet,size,&decoded) && decoded.channel == channel);
        packet[2] ^= 1u;
        assert(!wifi_control_parse_raw(packet,size,&decoded));
        packet[2] ^= 1u;
        uint8_t header = packet[7];
        packet[7] = 255u;
        assert(!wifi_control_parse_raw(packet,size,&decoded));
        packet[7] = header;
    }
}

int main(void) {
    protocol_tests();
    wifi_wire_t wire = {.write=write_pin,.data_output=output,.read_data=input,.now_us=clock_tick,.transfer=transfer};
    wifi_fw_report_t report;
    initialize(); wifi_firmware_run(&wire,&assets,&report,progress,0);
    assert(report.error==WIFI_FW_OK && report.stage==WIFI_FW_DONE);
    assert(report.verified_bytes==sizeof(firmware)+sizeof(nvram));
    assert(report.tx_packets==4u && report.rx_packets==4u && off_count==1u);
    assert(strcmp(report.version,"wl0: Sep 15 2026 version TEST")==0);
    assert(asset_calls == (sizeof(firmware) + 255u) / 256u +
                          (sizeof(nvram) + 255u) / 256u + (sizeof(clm) + 255u) / 256u);
    for (unsigned asset = 0u; asset < 3u; ++asset) {
        initialize();
        if (asset == 0u) assets.firmware_crc ^= 1u;
        if (asset == 1u) assets.nvram_crc ^= 1u;
        if (asset == 2u) assets.clm_crc ^= 1u;
        wifi_firmware_run(&wire, &assets, &report, progress, 0);
        assert(report.error == WIFI_FW_ASSET && report.stage == WIFI_FW_IDLE && off_count == 1u);
    }
    initialize();
    cancel_assets = 1;
    wifi_firmware_run(&wire, &assets, &report, progress, 0);
    assert(report.error == WIFI_FW_CANCELLED && report.stage == WIFI_FW_IDLE &&
           asset_calls == 3u && off_count == 1u);
    for (unsigned phase = 0u; phase < 2u; ++phase) {
        initialize();
        advance_stage = phase == 0u ? WIFI_FW_IDLE : WIFI_FW_DOWNLOAD;
        advance_ticks = phase == 0u ? 6000000u : 20000000u;
        wifi_firmware_run(&wire, &assets, &report, progress, 0);
        assert(advanced && report.error == WIFI_FW_TIMEOUT && report.stage == advance_stage &&
               ticks - advance_at >= advance_ticks && ticks - advance_at < advance_ticks + 10000u &&
               off_count == 1u);
    }
    const uint32_t corruptions[] = {0u, 63u, 64u, 513u, sizeof(firmware) - 1u,
                                    0x80000u - 5u};
    for (unsigned fault = 0u; fault < sizeof(corruptions) / sizeof(corruptions[0]); ++fault) {
        initialize();
        corrupt_address = corruptions[fault];
        wifi_firmware_run(&wire, &assets, &report, progress, 0);
        assert(report.error == WIFI_FW_READBACK && report.address == corrupt_address && off_count == 1u);
    }
    initialize();
    assert(wifi_firmware_begin(&wire,&assets,&report,progress,0) && off_count==0u);
    live_backplane = 1u;
    uint8_t bp_source[132], bp_result[132];
    for (uint32_t i = 0u; i < sizeof(bp_source); ++i) bp_source[i] = (uint8_t)(i * 7u);
    assert(wifi_firmware_backplane_write(0x7fc0u, bp_source, sizeof(bp_source)));
    assert(wifi_firmware_backplane_read(0x7fc0u, bp_result, sizeof(bp_result)));
    assert(memcmp(bp_source, bp_result, sizeof(bp_source)) == 0);
    assert(wifi_firmware_backplane_write(0xfffcu, bp_source, sizeof(bp_source)));
    assert(wifi_firmware_backplane_read(0xfffcu, bp_result, sizeof(bp_result)));
    assert(memcmp(bp_source, bp_result, sizeof(bp_source)) == 0);
    assert(wifi_firmware_backplane_write32(0x10000u, 0x12345678u));
    uint32_t bp_value;
    assert(wifi_firmware_backplane_read32(0x10000u, &bp_value) && bp_value == 0x12345678u);
    uint16_t halfword = 0u;
    assert(wifi_firmware_backplane_write32(0x10004u,0xabcdef01u));
    assert(wifi_firmware_backplane_read16(0x10002u,&halfword) && halfword == 0x1234u);
    assert(wifi_firmware_backplane_write16(0x10002u,0x9876u));
    assert(wifi_firmware_backplane_read32(0x10000u,&bp_value) && bp_value == 0x98765678u);
    assert(wifi_firmware_backplane_read32(0x10004u,&bp_value) && bp_value == 0xabcdef01u);
    assert(!wifi_firmware_backplane_read16(0x10001u,&halfword) && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_write16(0x10001u,0u) && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_read16(0x10002u,NULL) && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_read(1u, bp_result, 4u) && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_read(0u, bp_result, 3u) && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_read(0u, 0, 4u) && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_write(0u, 0, 4u) && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_read(0xfffffffcu, bp_result, 8u) &&
           report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_backplane_read32(0u, 0) && report.error == WIFI_FW_PROTOCOL);
    live_backplane = 0u;
    uint8_t version_request[192]="ver", version_reply[192];
    uint32_t reply_size;
    ticks+=4000000u;
    assert(wifi_firmware_control(262u,0u,1u,version_request,sizeof(version_request),
                                 version_reply,sizeof(version_reply),&reply_size));
    assert(reply_size>0u && memcmp(version_reply,"wl0:",4u)==0);
    reply_interface=1u;
    assert(!wifi_firmware_control(262u,0u,0u,version_request,sizeof(version_request),
                                  version_reply,sizeof(version_reply),&reply_size));
    assert(report.error==WIFI_FW_PROTOCOL);
    reply_interface=0u;
    assert(!wifi_firmware_control(262u,0u,0u,version_request,sizeof(version_request),
                                  version_reply,1u,&reply_size));
    assert(report.error==WIFI_FW_PROTOCOL);
    assert(wifi_firmware_control(262u,0u,0u,version_request,sizeof(version_request),
                                 version_reply,sizeof(version_reply),&reply_size));
    assert(wifi_firmware_data_enable());
    assert(!wifi_firmware_raw_enable(raw_receive, &raw_packets) &&
           report.error == WIFI_FW_PROTOCOL);
    for(unsigned i=0u;i<5u;++i) {
        inject_data=1u;
        assert(wifi_firmware_control(262u,0u,0u,version_request,sizeof(version_request),
                                     version_reply,sizeof(version_reply),&reply_size));
    }
    assert(wifi_firmware_rx_dropped()==1u);
    uint8_t frame[1514],interface;
    uint32_t frame_length;
    for(unsigned i=0u;i<4u;++i) {
        assert(wifi_firmware_read_frame(frame,&frame_length,&interface)==1);
        assert(frame_length==14u && interface==1u && frame[0]==2u);
    }
    assert(wifi_firmware_read_frame(frame,&frame_length,&interface)==0);
    const uint8_t ethernet[17]={2u,3u,4u,5u};
    assert(wifi_firmware_send_frame(0u,ethernet,sizeof(ethernet)));
    assert(wifi_firmware_read_frame(frame,&frame_length,&interface)==1);
    assert(interface==0u && frame_length==sizeof(ethernet) && memcmp(frame,ethernet,sizeof(ethernet))==0);
    uint8_t oversized[1614] = {0};
    assert(!wifi_firmware_send_frame(1u,oversized,sizeof(oversized)) &&
           report.error == WIFI_FW_PROTOCOL && !diagnostic_sent);
    assert(wifi_firmware_send_frame_diagnostic(1u,oversized,sizeof(oversized)) &&
           report.error == WIFI_FW_OK && diagnostic_sent == sizeof(oversized));
    wifi_firmware_stop();
    wifi_firmware_stop();
    assert(off_count==1u);
    assert(!wifi_firmware_control(262u,0u,0u,version_request,sizeof(version_request),
                                  version_reply,sizeof(version_reply),&reply_size));
    initialize();
    assert(wifi_firmware_begin(&wire,&assets,&report,progress,0));
    assert(wifi_firmware_raw_poll() == -1 && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_raw_enable(0, 0) && report.error == WIFI_FW_PROTOCOL);
    raw_packets = 0u;
    assert(wifi_firmware_raw_enable(raw_receive, &raw_packets));
    assert(!wifi_firmware_data_enable() && report.error == WIFI_FW_PROTOCOL);
    assert(!wifi_firmware_raw_enable(raw_receive, &raw_packets));
    assert(wifi_firmware_raw_poll() == 0);
    for (uint32_t channel = 1u; channel < 16u; ++channel) {
        data_size = wifi_data_frame(data_queue, 0u, 0u, ethernet, sizeof(ethernet));
        data_queue[5] = (uint8_t)channel;
        assert(wifi_firmware_raw_poll() == 1 && raw_packets == channel);
    }
    inject_data = 1u;
    assert(wifi_firmware_control(262u,0u,0u,version_request,sizeof(version_request),
                                 version_reply,sizeof(version_reply),&reply_size));
    assert(raw_packets == 16u && wifi_firmware_rx_dropped() == 0u);
    data_size = wifi_data_frame(data_queue, 0u, 0u, ethernet, sizeof(ethernet));
    data_queue[2] ^= 1u;
    assert(wifi_firmware_raw_poll() == -1 && report.error == WIFI_FW_PROTOCOL &&
           raw_packets == 16u);
    assert(!wifi_firmware_send_frame(0u, ethernet, sizeof(ethernet)));
    wifi_firmware_stop();
    assert(wifi_firmware_raw_poll() == -1);
    assert(off_count == 1u);
    initialize();
    assert(wifi_firmware_begin(&wire, &assets, &report, progress, 0));
    assert(wifi_firmware_raw_poll() == -1 && report.error == WIFI_FW_PROTOCOL);
    assert(raw_packets == 16u);
    advance_stage = WIFI_FW_DONE;
    advance_ticks = 3000000u;
    assert(!wifi_firmware_control(262u, 0u, 0u, version_request, sizeof(version_request),
                                  version_reply, sizeof(version_reply), &reply_size));
    assert(advanced && report.error == WIFI_FW_TIMEOUT && reply_size == 0u);
    wifi_firmware_stop();
    assert(off_count == 1u);
    for (unsigned fault=0u;fault<6u;++fault) {
        initialize();
        if (fault==0u) assets.firmware_crc^=1u;
        if (fault==1u) corrupt_address=513u;
        if (fault==2u) missing_ht=1;
        if (fault==3u) controller_error=7u;
        if (fault==4u) cancel_download=1;
        if (fault==5u) wrong_id=1u;
        wifi_firmware_run(&wire,&assets,&report,progress,0);
        const wifi_fw_error_t expected[]={WIFI_FW_ASSET,WIFI_FW_READBACK,WIFI_FW_TIMEOUT,
            WIFI_FW_CONTROLLER_ERROR,WIFI_FW_CANCELLED,WIFI_FW_TIMEOUT};
        assert(report.error==expected[fault] && off_count==1u);
        assert(report.stage!=WIFI_FW_DONE);
        if (fault==1u) assert(report.address==513u);
    }
    uint8_t request[64], response[64], nonce[16]={1u};
    usb_report_request_for_test(request,123u,nonce,WIFI_FW_TEST_ID);
    wifi_firmware_report(response,123u,nonce,&report);
    wifi_fw_report_t decoded;
    assert(wifi_firmware_decode(response,64u,request,123u,&decoded)==USB_RECORD_OK);
    for (unsigned i=0u;i<64u;++i) {
        response[i]^=1u; assert(wifi_firmware_decode(response,64u,request,123u,&decoded)!=USB_RECORD_OK); response[i]^=1u;
    }
    puts("wifi_firmware: bounded loader/readback/core gates, version/CLM, malformed packets, failures, CRC PASS");
    return 0;
}
