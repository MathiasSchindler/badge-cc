#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lora/link_test.h"
#include "usb/report.h"

static uint32_t clock_us, tx_count, config_count, release_count;
static uint8_t fail_opcode, timeout_irq, never_done, device_error;
static int32_t config_error;
static uint8_t air[LINK_PACKET_SIZE];
static sx1262_receiver_t receiver;
static link_test_t link;
static uint32_t frames;
static uint8_t ops[128];

uint32_t sx1262_hw_time_us(void) { clock_us += 1000; return clock_us; }
void sx1262_hw_release(void) { ++release_count; }
void sx1262_rx_lorawan_config(sx1262_rx_config_t *c) {
    *c = (sx1262_rx_config_t){868100000, 7, 125, 0};
}
int32_t sx1262_receiver_configure(sx1262_receiver_t *rx, const sx1262_rx_config_t *c) {
    ++config_count;
    if (config_error) return config_error;
    rx->active = *c; rx->state = SX1262_RX_RUNNING; rx->last_error = 0;
    return 0;
}
int32_t sx1262_transfer(const uint8_t *out, uint8_t *in, uint32_t length,
                        uint8_t *tail, uint32_t tail_length, int32_t (*service)(void)) {
    (void)tail; (void)service;
    assert(!tail_length && length <= LINK_PACKET_SIZE + 2);
    memset(in, 0, length);
    if (frames < sizeof(ops)) ops[frames] = out[0];
    ++frames;
    if (out[0] == fail_opcode) return SX1262_SPI_ERROR;
    switch (out[0]) {
    case 0x95: { const uint8_t want[] = {0x95,4,7,0,1}; assert(!memcmp(out,want,sizeof(want))); break; }
    case 0x8e: assert(length == 3 && out[1] == 0 && out[2] == 4); break;
    case 0x8c: assert(length == 7 && out[2] == 8 && out[4] == 36 && out[5] == 1 && out[6] == 0); break;
    case 0x0e: assert(out[1] == 0 && length == 38); memcpy(air,out+2,36); break;
    case 0x83:
        assert(length == 4 && out[1] == 0 && out[2] == 0x25 && out[3] == 0x80);
        ++tx_count; break;
    case 0x12:
        if (timeout_irq) in[2] = 2;
        else if (!never_done) in[3] = 1;
        break;
    case 0x17: in[3] = device_error; break;
    case 0x1d: assert(length == 5 && out[1] == 8 && out[2] == 0xd8); in[4] = 0x81; break;
    case 0x0d:
        assert((out[1] == 8 && out[2] == 0xd8 && out[3] == 0x9f) ||
               (out[1] == 7 && out[2] == 0x40 && out[3] == 0x14 && out[4] == 0x24));
        break;
    default: assert(out[0] == 0x80 || out[0] == 0x08 || out[0] == 0x02 || out[0] == 0x82);
    }
    return 0;
}

static void fresh(void) {
    memset(&link,0,sizeof(link)); memset(&receiver,0,sizeof(receiver));
    clock_us = tx_count = config_count = release_count = frames = 0;
    fail_opcode = timeout_irq = never_done = device_error = 0; config_error = 0;
    assert(!link_test_configure(&receiver));
    assert(receiver.active.frequency_hz == LINK_FREQUENCY_HZ && !tx_count);
}

int main(void) {
    const uint32_t id[2] = {0x12345678,0x9abcdef0};
    const uint8_t nonce[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    fresh();
    uint32_t before = frames;
    assert(link_test_send(&link,&receiver,19999,id,nonce) == LINK_LIMIT_ERROR && frames == before);
    assert(link_test_send(&link,&receiver,20000,id,nonce) == 0 && tx_count == 1 && link.transmitted == 1);
    assert(usb_load_u32(air) == LINK_PACKET_MAGIC && usb_load_u32(air+4) == id[0]);
    assert(usb_load_u32(air+8) == id[1] && usb_load_u32(air+12) == 1);
    assert(!memcmp(air+16,nonce,16) && usb_load_u32(air+32) == usb_crc32(air,32));
    assert(receiver.state == SX1262_RX_RUNNING);
    before = frames;
    assert(link_test_send(&link,&receiver,39999,id,nonce) == LINK_LIMIT_ERROR && frames == before);
    for (uint32_t n = 2; n <= LINK_MAX_ATTEMPTS; ++n)
        assert(!link_test_send(&link,&receiver,n*LINK_INTERVAL_MS,id,nonce));
    before = frames;
    assert(link_test_send(&link,&receiver,140000,id,nonce) == LINK_LIMIT_ERROR && frames == before);
    assert(tx_count == 6);
    fresh(); timeout_irq = 1;
    assert(link_test_send(&link,&receiver,20000,id,nonce) == LINK_TX_TIMEOUT);
    assert(link.attempts == 1 && !link.transmitted && !link.restore_error && receiver.state == SX1262_RX_RUNNING);
    fresh(); never_done = 1;
    assert(link_test_send(&link,&receiver,20000,id,nonce) == LINK_TX_TIMEOUT && clock_us >= 200000);
    fresh(); fail_opcode = 0x0e;
    assert(link_test_send(&link,&receiver,20000,id,nonce) == SX1262_SPI_ERROR && !tx_count && link.attempts == 1);
    fresh(); device_error = 1;
    assert(link_test_send(&link,&receiver,20000,id,nonce) == SX1262_DEVICE_ERROR && !tx_count);
    fresh(); config_error = SX1262_SPI_ERROR;
    assert(link_test_send(&link,&receiver,20000,id,nonce) == SX1262_SPI_ERROR);
    assert(link.transmitted == 1 && link.restore_error == SX1262_SPI_ERROR && receiver.state == SX1262_RX_FAULT);
    fresh(); receiver.state = SX1262_RX_FAULT; before = frames;
    assert(link_test_send(&link,&receiver,20000,id,nonce) == SX1262_RX_STATE_ERROR && before == frames);
    fresh(); fail_opcode = 0x0d;
    assert(link_test_configure(&receiver) == SX1262_SPI_ERROR && receiver.state == SX1262_RX_FAULT);
    puts("PASS: explicit LoRa TX, fixed 0 dBm/timeout/profile, payload CRC/challenge, spacing/budget, failures and RX restore");
    return 0;
}
