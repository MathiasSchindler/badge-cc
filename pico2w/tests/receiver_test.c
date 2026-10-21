#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lora/receiver.h"
#include "lora/packet_record.h"
#include "usb/report.h"
#include "lora/hardware.h"

/* Simulated SX1262 behind the mocked hardware interface. */
typedef struct { uint8_t bytes[12]; uint32_t length; } frame_t;

static struct {
    int selected, reset, pins, antenna;
    uint32_t inits, releases;
    uint32_t now, reset_at, released_at;
    uint8_t mode, packet_type, stuck_miso, busy_stuck, spi_fail_at_opcode;
    uint8_t calibrate_error, profile_error_at_freq, transient, reset_seen;
    uint8_t startup_fault;
    uint8_t reserved_status;
    uint16_t reset_errors;
    uint16_t irq, errors;
    uint8_t regs_0736, sync[2], rx_gain;
    uint8_t buffer[256], rx_length, rx_start, rssi, snr, signal, rssi_inst;
    uint8_t out[300], in[300];
    uint32_t index;
    frame_t log[512];
    uint32_t frames, services, cancel;
} sim;

static const uint8_t allowed[] = {
    0x80, 0x97, 0x07, 0x89, 0x9d, 0x8a, 0x98, 0x86, 0x8b, 0x8c, 0x1d, 0x0d,
    0x8f, 0x08, 0x02, 0x17, 0x82, 0xc0, 0x12, 0x13, 0x14, 0x1e, 0x15
};
/* SetTx, TX CW, TX infinite preamble, CAD, CAD params, TX params, PA config,
   WriteBuffer, Sleep, RxDutyCycle, FS, regulator/fallback/stop-timer changes. */
static const uint8_t forbidden[] = {
    0x83, 0xd1, 0xd2, 0xc5, 0x88, 0x8e, 0x95, 0x0e, 0x84, 0x94, 0xc1, 0x96,
    0x93, 0x9f, 0xa0
};

static uint8_t status(void) {
    if (sim.stuck_miso) return 0xff;
    return (uint8_t)((sim.mode << 4) | ((sim.startup_fault ? 5u : 1u) << 1) |
                     sim.reserved_status);
}

int32_t sx1262_hw_clock_init(void) { return 0; }

int32_t sx1262_hw_init(void) {
    ++sim.inits;
    sim.pins = 1;
    return 0;
}

uint32_t sx1262_hw_time_us(void) {
    sim.now += 10u;
    return sim.now;
}

uint8_t sx1262_hw_busy(void) {
    assert(!sim.selected && !sim.reset && sim.pins);
    return sim.busy_stuck;
}

static void power_on_reset(void) {
    sim.mode = 2;
    sim.packet_type = 0;
    sim.irq = 0;
    sim.errors = sim.reset_errors;
    sim.startup_fault = 1u;
    sim.regs_0736 = 0x09; /* bit 2 clear: errata write must set it */
    sim.sync[0] = 0x14;
    sim.sync[1] = 0x24;
}

void sx1262_hw_reset(uint8_t asserted) {
    assert(!sim.selected && sim.pins);
    if (asserted) {
        sim.reset_at = sim.now;
    } else if (sim.reset) {
        if (!sim.cancel) assert(sim.now - sim.reset_at >= 20000u);
        sim.released_at = sim.now;
        sim.reset_seen = 1;
        power_on_reset();
    }
    sim.reset = asserted;
}

void sx1262_hw_select(uint8_t value) {
    assert(value != sim.selected && !sim.reset && sim.pins);
    sim.selected = value;
    if (value) {
        if (sim.reset_seen) assert(sim.now - sim.released_at >= 10000u);
        sim.index = 0;
        return;
    }
    if (sim.index == 0) return;
    uint8_t op = sim.out[0];
    int ok = 0;
    for (uint32_t i = 0; i < sizeof(allowed); ++i) ok |= allowed[i] == op;
    for (uint32_t i = 0; i < sizeof(forbidden); ++i) assert(forbidden[i] != op);
    assert(ok);
    assert(sim.frames < 512);
    frame_t *f = &sim.log[sim.frames++];
    f->length = sim.index;
    for (uint32_t i = 0; i < 12 && i < sim.index; ++i) f->bytes[i] = sim.out[i];
    switch (op) {
    case 0x80:
        assert(sim.index == 2 && sim.out[1] == 0);
        sim.mode = 2;
        if (sim.transient) sim.errors = 0;
        break;
    case 0x82: assert(sim.index == 4); sim.mode = 5; break;
    case 0x8a: assert(sim.index == 2 && sim.out[1] == 1); sim.packet_type = 1; break;
    case 0x07: sim.errors = 0; sim.startup_fault = 0u; break;
    case 0x89: if (sim.calibrate_error) sim.errors = 0x20; break;
    case 0x02: sim.irq &= (uint16_t)~((sim.out[1] << 8) | sim.out[2]); break;
    case 0x0d: {
        uint32_t address = ((uint32_t)sim.out[1] << 8) | sim.out[2];
        assert(address == 0x0736 || address == 0x0740 || address == 0x08ac);
        if (address == 0x0736) { assert(sim.index == 4); sim.regs_0736 = sim.out[3]; }
        else if (address == 0x08ac) { assert(sim.index == 4); sim.rx_gain = sim.out[3]; }
        else { assert(sim.index == 5); sim.sync[0] = sim.out[3]; sim.sync[1] = sim.out[4]; }
        break;
    }
    case 0x86:
        if (sim.profile_error_at_freq && sim.out[1] == sim.profile_error_at_freq)
            sim.errors = 0x40;
        break;
    default: break;
    }
}

int32_t sx1262_hw_byte(uint8_t out, uint8_t *in) {
    assert(sim.selected && sim.index < sizeof(sim.out));
    uint32_t i = sim.index;
    sim.out[i] = out;
    uint8_t op = sim.out[0];
    if (sim.spi_fail_at_opcode && op == sim.spi_fail_at_opcode) return SX1262_SPI_ERROR;
    uint8_t v = status();
    if (op != 0xc0 && !sim.stuck_miso) v |= 0x80u;
    if (!sim.stuck_miso && i > 0) {
        switch (op) {
        case 0x12: v = i == 2 ? (uint8_t)(sim.irq >> 8) : i == 3 ? (uint8_t)sim.irq : v; break;
        case 0x13: v = i == 2 ? sim.rx_length : i == 3 ? sim.rx_start : v; break;
        case 0x15: v = i == 2 ? sim.rssi_inst : v; break;
        case 0x14: v = i == 2 ? sim.rssi : i == 3 ? sim.snr : i == 4 ? sim.signal : v; break;
        case 0x17: v = i == 2 ? (uint8_t)(sim.errors >> 8) : i == 3 ? (uint8_t)sim.errors : v; break;
        case 0x1d:
            if (i >= 4) {
                uint32_t a = (((uint32_t)sim.out[1] << 8) | sim.out[2]) + (i - 4);
                v = a == 0x0736 ? sim.regs_0736 : a == 0x0740 ? sim.sync[0] :
                    a == 0x0741 ? sim.sync[1] : 0;
            }
            break;
        case 0x1e:
            if (i >= 3) v = sim.buffer[(uint8_t)(sim.out[1] + (i - 3))];
            break;
        default: break;
        }
    }
    *in = v;
    sim.in[i] = v;
    ++sim.index;
    return 0;
}

void sx1262_hw_release(void) {
    assert(!sim.selected && !sim.reset);
    sim.pins = 0;
    sim.antenna = 0;
    ++sim.releases;
}

void sx1262_hw_antenna_power(void) {
    assert(sim.pins && !sim.selected && !sim.antenna);
    sim.antenna = 1;
}

static int32_t service(void) {
    ++sim.services;
    return sim.cancel ? -2999 : 0;
}

static void fresh(sx1262_receiver_t *rx) {
    memset(&sim, 0, sizeof(sim));
    sim.reset_errors = 0x20u; /* XOSC start error until TCXO is configured */
    sim.now = 0xfffff000u; /* exercises timer wrap */
    sx1262_receiver_init(rx, service);
}

static sx1262_receiver_t rx;

static void expect(uint32_t n, const uint8_t *bytes, uint32_t length) {
    assert(n < sim.frames);
    assert(sim.log[n].length == length);
    for (uint32_t i = 0; i < length && i < 12; ++i) assert(sim.log[n].bytes[i] == bytes[i]);
}

static uint32_t count_opcode(uint32_t from, uint8_t op) {
    uint32_t n = 0;
    for (uint32_t i = from; i < sim.frames; ++i) n += sim.log[i].bytes[0] == op;
    return n;
}

static void start_default(void) {
    fresh(&rx);
    assert(sx1262_receiver_confirm_board(&rx, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M) == 0);
    sx1262_rx_config_t c;
    sx1262_rx_lorawan_config(&c);
    assert(sx1262_receiver_configure(&rx, &c) == 0);
}

static void test_init_frames(void) {
    start_default();
    static const struct { uint8_t b[10]; uint32_t n; } want[] = {
        {{0xc0, 0}, 2},
        {{0x17, 0, 0, 0}, 4},
        {{0x80, 0}, 2},
        {{0x97, 0x01, 0x00, 0x01, 0x40}, 5},
        {{0x07, 0, 0}, 3},
        {{0x89, 0x7f}, 2},
        {{0x9d, 0x01}, 2},
        {{0x80, 0}, 2},
        {{0x8a, 0x01}, 2},
        {{0x98, 0xd7, 0xdb}, 3},
        {{0x86, 0x36, 0x41, 0x99, 0x99}, 5},
        {{0x8b, 7, 0x04, 0x01, 0x00}, 5},
        {{0x8c, 0x00, 0x08, 0x00, 0xff, 0x01, 0x00}, 7},
        {{0x1d, 0x07, 0x36, 0, 0}, 5},
        {{0x0d, 0x07, 0x36, 0x0d}, 4},
        {{0x0d, 0x07, 0x40, 0x34, 0x44}, 5},
        {{0x0d, 0x08, 0xac, 0x96}, 4},
        {{0x8f, 0, 0}, 3},
        {{0x08, 0x02, 0x72, 0x02, 0x72, 0, 0, 0, 0}, 9},
        {{0x02, 0xff, 0xff}, 3},
        {{0x17, 0, 0, 0}, 4},
        {{0x82, 0xff, 0xff, 0xff}, 4},
        {{0xc0, 0}, 2},
    };
    assert(sim.frames == sizeof(want) / sizeof(want[0]));
    for (uint32_t i = 0; i < sim.frames; ++i) expect(i, want[i].b, want[i].n);
    assert(rx.state == SX1262_RX_RUNNING && rx.active_valid && rx.last_error == 0);
    assert(rx.device_errors == 0u && rx.last_status == 0x52u && rx.last_command == 0xc0u);
    assert(rx.active.frequency_hz == 868100000u && sim.mode == 5 && sim.inits == 1);
    assert(sim.sync[0] == 0x34 && sim.sync[1] == 0x44 && sim.regs_0736 == 0x0d && sim.rx_gain == 0x96);
}

static void test_profiles(void) {
    fresh(&rx);
    sx1262_receiver_confirm_board(&rx, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
    sim.reserved_status = 0x81u;
    sx1262_rx_config_t initial;
    sx1262_rx_lorawan_config(&initial);
    assert(sx1262_receiver_configure(&rx, &initial) == 0);
    assert(rx.state == SX1262_RX_RUNNING && rx.last_status == 0xd3u);
    static const struct { uint32_t f, sf, bw; uint8_t freq[4], bwc, ldro; } cases[] = {
        {863000000u, 12, 125, {0x35, 0xf0, 0x00, 0x00}, 0x04, 1},
        {870000000u, 11, 125, {0x36, 0x60, 0x00, 0x00}, 0x04, 1},
        {868500000u, 12, 250, {0x36, 0x48, 0x00, 0x00}, 0x05, 1},
        {868300000u, 11, 250, {0x36, 0x44, 0xcc, 0xcc}, 0x05, 0},
        {869525000u, 9, 500, {0x36, 0x58, 0x66, 0x66}, 0x06, 0},
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        start_default();
        uint32_t inits = sim.inits, from = sim.frames;
        sx1262_rx_config_t c = {cases[i].f, cases[i].sf, cases[i].bw, SX1262_RX_PROFILE_LORAWAN};
        assert(sx1262_receiver_configure(&rx, &c) == 0);
        /* Live reconfiguration: no hardware re-init, reset or TCXO/calibration. */
        assert(sim.inits == inits && count_opcode(from, 0x97) == 0 && count_opcode(from, 0x89) == 0);
        uint8_t standby[] = {0x80, 0};
        expect(from, standby, 2);
        uint8_t f[] = {0x86, cases[i].freq[0], cases[i].freq[1], cases[i].freq[2], cases[i].freq[3]};
        expect(from + 3, f, 5);
        uint8_t m[] = {0x8b, (uint8_t)cases[i].sf, cases[i].bwc, 0x01, cases[i].ldro};
        expect(from + 4, m, 5);
        assert(rx.active.frequency_hz == cases[i].f && rx.active.spreading_factor == cases[i].sf &&
               rx.active.bandwidth_khz == cases[i].bw && rx.counters.configs == 2);
    }
}

static void test_invalid_config(void) {
    fresh(&rx);
    sx1262_rx_config_t c;
    sx1262_rx_lorawan_config(&c);
    assert(sx1262_receiver_configure(&rx, &c) == SX1262_RX_BOARD_ERROR);
    assert(sx1262_receiver_confirm_board(&rx, 1234u) == SX1262_RX_BOARD_ERROR);
    assert(sx1262_receiver_configure(&rx, &c) == SX1262_RX_BOARD_ERROR);
    assert(sim.inits == 0 && sim.frames == 0 && rx.state == SX1262_RX_IDLE);
    assert(sx1262_receiver_poll(&rx, 1) == SX1262_RX_STATE_ERROR && sim.frames == 0);

    start_default();
    uint32_t frames = sim.frames;
    static const sx1262_rx_config_t bad[] = {
        {862999999u, 7, 125, 0}, {870000001u, 7, 125, 0}, {0u, 7, 125, 0},
        {868100000u, 6, 125, 0}, {868100000u, 13, 125, 0}, {868100000u, 7, 0, 0},
        {868100000u, 7, 62, 0}, {868100000u, 7, 200, 0}, {868100000u, 7, 1000, 0},
        {868100000u, 7, 125, 2},
    };
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(sx1262_rx_validate(&bad[i]) == SX1262_RX_CONFIG_ERROR);
        assert(sx1262_receiver_configure(&rx, &bad[i]) == SX1262_RX_CONFIG_ERROR);
    }
    assert(sx1262_receiver_configure(&rx, 0) == SX1262_RX_CONFIG_ERROR);
    assert(sim.frames == frames && rx.state == SX1262_RX_RUNNING);
    assert(rx.active.frequency_hz == 868100000u && rx.counters.config_errors == 11);
    assert(rx.last_error == SX1262_RX_CONFIG_ERROR);
}

static void inject(const uint8_t *data, uint32_t length, uint8_t start) {
    for (uint32_t i = 0; i < length; ++i) sim.buffer[(uint8_t)(start + i)] = data[i];
    sim.rx_length = (uint8_t)length;
    sim.rx_start = start;
    sim.irq |= 0x0002 | 0x0010;
}

static void test_reception(void) {
    start_default();
    assert(sx1262_receiver_poll(&rx, 5) == 0 && rx.count == 0);
    /* Unconfirmed uplink, DevAddr 26011234, FCtrl 0x81 (1 FOpts byte), FCnt 0x0102. */
    static const uint8_t frame[] = {0x40, 0x34, 0x12, 0x01, 0x26, 0x81, 0x02, 0x01,
                                    0x06, 0x01, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    inject(frame, sizeof(frame), 0xf8); /* wraps the 256-byte radio buffer */
    sim.rssi = 0x50; sim.snr = 0xf8; sim.signal = 0x52;
    uint32_t from = sim.frames;
    assert(sx1262_receiver_poll(&rx, 123456u) == 0);
    assert(rx.count == 1 && rx.counters.packets == 1 && rx.counters.headers_valid == 1);
    assert(sim.irq == 0 && count_opcode(from, 0x1e) == 1);
    uint8_t clear[] = {0x02, 0x00, 0x12};
    expect(sim.frames - 1, clear, 3);
    const sx1262_rx_packet_t *p = sx1262_receiver_packet(&rx, 0);
    assert(p && p->sequence == 1 && p->timestamp_ms == 123456u && p->length == sizeof(frame));
    assert(memcmp(p->payload, frame, sizeof(frame)) == 0);
    assert(p->rssi_qdb == -160 && p->snr_qdb == -8 && p->signal_rssi_qdb == -164);
    assert(p->frequency_hz == 868100000u && p->spreading_factor == 7 && p->bandwidth_khz == 125);
    assert(p->flags == (SX1262_RX_HAS_MHDR | SX1262_RX_LORAWAN_DATA));
    assert(p->mhdr == 0x40 && p->mtype == 2 && p->major == 0 && p->dev_addr == 0x26011234u);
    assert(p->fctrl == 0x81 && p->fopts_length == 1 && p->fcnt16 == 0x0102);
    assert(sx1262_receiver_packet(&rx, 1) == 0);

    sim.snr = 0x1c;
    /* CRC error: kept for diagnostics, flagged, unparsed and not a good packet. */
    inject(frame, sizeof(frame), 0x10);
    sim.irq = 0x0002 | 0x0040;
    from = sim.frames;
    assert(sx1262_receiver_poll(&rx, 2) == 0);
    assert(rx.counters.crc_errors == 1 && rx.counters.packets == 1 && rx.count == 2);
    assert(count_opcode(from, 0x1e) == 1 && sim.irq == 0);
    p = sx1262_receiver_packet(&rx, 0);
    assert(p->sequence == 2 && p->flags == SX1262_RX_CRC_ERROR && p->length == sizeof(frame));
    assert(p->snr_qdb == 28 && p->rssi_qdb == -160 && memcmp(p->payload, frame, sizeof(frame)) == 0);
    assert(p->mhdr == 0 && p->mtype == 0 && p->dev_addr == 0 && p->fcnt16 == 0 && p->fctrl == 0);
    sim.irq = 0x0040; /* CRC flag without RxDone: counted only */
    assert(sx1262_receiver_poll(&rx, 3) == 0);
    assert(rx.counters.crc_errors == 2 && rx.count == 2 && sim.irq == 0);
    sim.irq = 0x0020;
    assert(sx1262_receiver_poll(&rx, 3) == 0);
    assert(rx.counters.header_errors == 1 && rx.count == 2 && sim.irq == 0);

    /* Arbitrary non-LoRaWAN-looking payloads are stored without claims. */
    static const uint8_t join[] = {0x00, 1, 2, 3};
    inject(join, sizeof(join), 0);
    sim.snr = 0x1c;
    assert(sx1262_receiver_poll(&rx, 4) == 0);
    p = sx1262_receiver_packet(&rx, 0);
    assert(p->flags == SX1262_RX_HAS_MHDR && p->mtype == 0 && p->dev_addr == 0 && p->snr_qdb == 28);
    inject(join, 0, 0);
    assert(sx1262_receiver_poll(&rx, 5) == 0);
    p = sx1262_receiver_packet(&rx, 0);
    assert(p->length == 0 && p->flags == 0);
}

static void test_parse(void) {
    static sx1262_rx_packet_t p;
    static const struct { uint8_t data[16]; uint32_t length; uint8_t flags; } cases[] = {
        {{0x40, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9, 9}, 12, 3},     /* minimal data up */
        {{0x80, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9, 9}, 12, 3},     /* confirmed up */
        {{0x60, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9, 9}, 12, 3},     /* unconfirmed down */
        {{0x40, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9}, 11, 1},        /* too short */
        {{0x40, 1, 2, 3, 4, 0x01, 5, 6, 9, 9, 9, 9}, 12, 1},     /* FOpts overflow */
        {{0x41, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9, 9}, 12, 1},     /* major 1 */
        {{0x44, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9, 9}, 12, 1},     /* RFU bits */
        {{0x20, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9, 9}, 12, 1},     /* join accept */
        {{0xe0, 1, 2, 3, 4, 0x00, 5, 6, 9, 9, 9, 9}, 12, 1},     /* proprietary */
    };
    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memset(&p, 0xa5, sizeof(p));
        p.profile = SX1262_RX_PROFILE_LORAWAN;
        memcpy(p.payload, cases[i].data, cases[i].length);
        p.length = (uint8_t)cases[i].length;
        sx1262_rx_parse(&p);
        assert(p.flags == cases[i].flags && p.mhdr == cases[i].data[0]);
        if (p.flags & SX1262_RX_LORAWAN_DATA)
            assert(p.dev_addr == 0x04030201u && p.fcnt16 == 0x0605);
        else
            assert(p.dev_addr == 0 && p.fcnt16 == 0 && p.fctrl == 0);
    }
}

static void test_meshtastic(void) {
    sx1262_rx_config_t c;
    sx1262_rx_default_config(&c);
    assert(c.profile == SX1262_RX_PROFILE_MESHTASTIC && c.frequency_hz == 869525000u &&
           c.spreading_factor == 11 && c.bandwidth_khz == 250);
    fresh(&rx);
    assert(sx1262_receiver_confirm_board(&rx, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M) == 0);
    uint32_t from = sim.frames;
    assert(sx1262_receiver_configure(&rx, &c) == 0);
    from += 7; /* status, errors, standby, TCXO, clear, calibrate, RF switch */
    static const struct { uint8_t b[7]; uint32_t n; } want[] = {
        {{0x80, 0}, 2},
        {{0x8a, 0x01}, 2},
        {{0x98, 0xd7, 0xdb}, 3},
        {{0x86, 0x36, 0x58, 0x66, 0x66}, 5},
        {{0x8b, 11, 0x05, 0x01, 0x00}, 5},           /* SF11/BW250/CR4/5, LDRO off */
        {{0x8c, 0x00, 0x10, 0x00, 0xff, 0x01, 0x00}, 7}, /* preamble 16 */
        {{0x1d, 0x07, 0x36, 0, 0}, 5},
        {{0x0d, 0x07, 0x36, 0x0d}, 4},
        {{0x0d, 0x07, 0x40, 0x24, 0xb4}, 5},         /* sync word 0x2b */
        {{0x0d, 0x08, 0xac, 0x96}, 4},               /* RX boosted gain */
    };
    for (uint32_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i) expect(from + i, want[i].b, want[i].n);
    assert(sim.sync[0] == 0x24 && sim.sync[1] == 0xb4 && sim.mode == 5);
    assert(rx.active.profile == SX1262_RX_PROFILE_MESHTASTIC);
    for (uint32_t i = 0; i < sizeof(forbidden); ++i) assert(count_opcode(0, forbidden[i]) == 0);

    /* Header-looking LoRaWAN bytes are never parsed as LoRaWAN in this profile. */
    static const uint8_t frame[] = {0xff, 0xff, 0xff, 0xff, 0x44, 0x33, 0x22, 0x11,
                                    0x88, 0x77, 0x66, 0x55, 0x63, 0x08, 0x00, 0x44, 0x40, 0x41};
    inject(frame, sizeof(frame), 0);
    assert(sx1262_receiver_poll(&rx, 77) == 0);
    const sx1262_rx_packet_t *p = sx1262_receiver_packet(&rx, 0);
    assert(p && p->profile == SX1262_RX_PROFILE_MESHTASTIC && p->flags == SX1262_RX_MESHTASTIC_HEADER);
    assert(p->dev_addr == 0 && p->mtype == 0 && p->frequency_hz == 869525000u);
    inject(frame, 15, 0);
    assert(sx1262_receiver_poll(&rx, 78) == 0);
    assert(sx1262_receiver_packet(&rx, 0)->flags == 0);

    /* Switching back to LoRaWAN restores preamble 8 and the public sync word. */
    sx1262_rx_lorawan_config(&c);
    assert(sx1262_receiver_configure(&rx, &c) == 0);
    assert(sim.sync[0] == 0x34 && sim.sync[1] == 0x44 && rx.active.profile == SX1262_RX_PROFILE_LORAWAN);
}

static int32_t s16(const uint8_t *b) {
    return (int32_t)(((uint32_t)b[0] | (uint32_t)b[1] << 8) ^ 0x8000u) - 0x8000;
}

static void test_packet_record(void) {
    static uint8_t record[LORA_PACKET_RECORD_SIZE];
    start_default();
    lora_packet_record(record, 0x12345678u, &rx, 0);
    assert(usb_load_u32(record) == LORA_PACKET_RECORD_MAGIC && record[4] == 1);
    assert(record[6] == (LORA_PACKET_RECORD_SIZE & 255u) && record[7] == (LORA_PACKET_RECORD_SIZE >> 8));
    assert(usb_load_u32(record + 8) == 0x12345678u && usb_load_u32(record + 12) == 0);
    assert(usb_load_u32(record + 16) == 0 && usb_load_u32(record + 336) == usb_crc32(record, 336));
    assert(usb_load_u32(record + 332) == 0 && record[66] == 0 && record[323] == 0 && record[330] == 0);
    static uint8_t data[40];
    for (uint32_t n = 1; n <= 18; ++n) {
        data[0] = (uint8_t)n;
        inject(data, 20 + n, 0);
        sim.rssi = 0x50; sim.snr = 0xf8;
        assert(sx1262_receiver_poll(&rx, n) == 0);
    }
    /* Ring holds 3..18; asking after 0 or 1 returns the oldest held packet. */
    lora_packet_record(record, 1u, &rx, 1);
    assert(usb_load_u32(record + 12) == 1 && usb_load_u32(record + 16) == 18);
    assert(usb_load_u32(record + 20) == 3 && usb_load_u32(record + 24) == 3);
    assert(record[64] == 23 && record[68] == 3 && (int32_t)usb_load_u32(record + 40) == -160);
    assert((int32_t)usb_load_u32(record + 44) == -8 && usb_load_u32(record + 52) == 18);
    assert(usb_load_u32(record + 32) == 868100000u && record[36] == 125 && record[38] == 7);
    lora_packet_record(record, 1u, &rx, 10);
    assert(usb_load_u32(record + 24) == 11 && record[68] == 11);
    lora_packet_record(record, 1u, &rx, 18);
    assert(usb_load_u32(record + 12) == 0 && usb_load_u32(record + 24) == 0 && record[64] == 0);
    assert(usb_load_u32(record + 336) == usb_crc32(record, 336));
    /* Noise-floor block: int16 now/min/max at 324/326/328, samples at 332. */
    sim.rssi_inst = 211; /* -105.5 dBm */
    assert(sx1262_receiver_poll(&rx, 5000) == 0 && rx.noise_samples == 1);
    lora_packet_record(record, 1u, &rx, 18);
    assert(s16(record + 324) == -422);
    assert(s16(record + 326) == -422);
    assert(s16(record + 328) == -422);
    assert(usb_load_u32(record + 332) == 1 && usb_load_u32(record + 336) == usb_crc32(record, 336));
}

static void test_noise_floor(void) {
    start_default();
    assert(rx.noise_samples == 0);
    sim.rssi_inst = 180; /* -90 dBm */
    uint32_t before = sim.frames;
    assert(sx1262_receiver_poll(&rx, 1000) == 0);
    assert(sim.log[sim.frames - 1].bytes[0] == 0x15 && sim.log[sim.frames - 1].length == 3);
    assert(rx.noise_samples == 1 && rx.noise_qdb == -360);
    assert(rx.noise_min_qdb == -360 && rx.noise_max_qdb == -360);
    /* Rate limited to one sample per SX1262_RX_NOISE_INTERVAL_MS. */
    before = sim.frames;
    sim.rssi_inst = 240;
    assert(sx1262_receiver_poll(&rx, 1000 + SX1262_RX_NOISE_INTERVAL_MS - 1) == 0);
    assert(rx.noise_samples == 1);
    for (uint32_t i = before; i < sim.frames; ++i) assert(sim.log[i].bytes[0] != 0x15);
    assert(sx1262_receiver_poll(&rx, 1000 + SX1262_RX_NOISE_INTERVAL_MS) == 0);
    assert(rx.noise_samples == 2 && rx.noise_qdb == -480);
    assert(rx.noise_min_qdb == -480 && rx.noise_max_qdb == -360);
    sim.rssi_inst = 100;
    assert(sx1262_receiver_poll(&rx, 2000) == 0);
    assert(rx.noise_samples == 3 && rx.noise_qdb == -200);
    assert(rx.noise_min_qdb == -480 && rx.noise_max_qdb == -200);
    /* A pending IRQ is serviced instead of sampling. */
    static uint8_t data[4] = {1, 2, 3, 4};
    inject(data, 4, 0);
    before = sim.frames;
    assert(sx1262_receiver_poll(&rx, 3000) == 0);
    assert(rx.noise_samples == 3 && rx.counters.packets == 1);
    for (uint32_t i = before; i < sim.frames; ++i) assert(sim.log[i].bytes[0] != 0x15);
    /* A SPI failure while sampling is reported like any other command error. */
    sim.spi_fail_at_opcode = 0x15;
    assert(sx1262_receiver_poll(&rx, 4000) != 0 && rx.noise_samples == 3);
    sim.spi_fail_at_opcode = 0;
    /* Reconfiguring starts fresh statistics. */
    sx1262_rx_config_t c;
    sx1262_rx_lorawan_config(&c);
    c.frequency_hz = 868300000u;
    assert(sx1262_receiver_configure(&rx, &c) == 0);
    assert(rx.noise_samples == 0 && rx.noise_qdb == 0 && rx.noise_min_qdb == 0 && rx.noise_max_qdb == 0);
}

static void test_ring_bounds(void) {
    start_default();
    static uint8_t big[255];
    for (uint32_t i = 0; i < 255; ++i) big[i] = (uint8_t)(i * 7u);
    for (uint32_t n = 1; n <= 20; ++n) {
        big[0] = (uint8_t)n;
        inject(big, n == 20 ? 255 : n, (uint8_t)(n * 13u));
        assert(sx1262_receiver_poll(&rx, n * 1000u) == 0);
    }
    assert(rx.count == SX1262_RX_RING_SIZE && rx.counters.overwritten == 4 && rx.counters.packets == 20);
    const sx1262_rx_packet_t *p = sx1262_receiver_packet(&rx, 0);
    assert(p->sequence == 20 && p->length == 255 && p->timestamp_ms == 20000u);
    assert(memcmp(p->payload, big, 255) == 0);
    p = sx1262_receiver_packet(&rx, 15);
    assert(p->sequence == 5 && p->length == 5 && p->payload[0] == 5);
    assert(sx1262_receiver_packet(&rx, 16) == 0 && sx1262_receiver_packet(&rx, 0xffffffffu) == 0);
}

static void test_runtime_faults(void) {
    /* Radio drops out of RX (e.g. brown-out): poll rearms with the active profile. */
    start_default();
    sim.mode = 2;
    uint32_t from = sim.frames;
    assert(sx1262_receiver_poll(&rx, 1) == 0);
    assert(rx.counters.rearms == 1 && sim.mode == 5 && count_opcode(from, 0x82) == 1);
    sim.irq = 0x0200;
    assert(sx1262_receiver_poll(&rx, 2) == 0 && rx.counters.timeouts == 1 && rx.counters.rearms == 2);

    /* Cancellation keeps listening and never leaves NSS asserted. */
    sim.cancel = 1;
    assert(sx1262_receiver_poll(&rx, 3) == -2999);
    assert(rx.state == SX1262_RX_RUNNING && rx.last_error == -2999 && !sim.selected && sim.pins);
    sim.cancel = 0;
    assert(sx1262_receiver_poll(&rx, 4) == 0 && rx.last_error == -2999);

    /* Stuck BUSY: bounded, faults and releases pins. */
    sim.busy_stuck = 1;
    uint32_t services = sim.services;
    assert(sx1262_receiver_poll(&rx, 5) == SX1262_BUSY_ERROR);
    assert(sim.services - services < 20000u);
    assert(rx.state == SX1262_RX_FAULT && !sim.pins && sim.releases == 1 && !sim.selected);
    assert(sx1262_receiver_poll(&rx, 6) == SX1262_RX_STATE_ERROR);
    sim.busy_stuck = 0;
    sx1262_rx_config_t c;
    sx1262_rx_lorawan_config(&c);
    assert(sx1262_receiver_configure(&rx, &c) == 0 && rx.state == SX1262_RX_RUNNING && sim.inits == 2);

    /* Stuck MISO (0xff) is rejected, not reported as packets. */
    sim.stuck_miso = 1;
    assert(sx1262_receiver_poll(&rx, 7) == SX1262_RESPONSE_ERROR && rx.count == 0);
    assert(rx.state == SX1262_RX_FAULT && !sim.pins);

    /* SPI failure during packet read leaves no partial ring entry. */
    start_default();
    static const uint8_t data[] = {1, 2, 3};
    inject(data, 3, 0);
    sim.spi_fail_at_opcode = 0x1e;
    assert(sx1262_receiver_poll(&rx, 8) == SX1262_SPI_ERROR);
    assert(rx.count == 0 && rx.state == SX1262_RX_FAULT && !sim.selected && !sim.pins);

    /* Failed live reconfiguration rolls back to the previous profile. */
    start_default();
    sx1262_rx_config_t next = {863000000u, 8, 125, SX1262_RX_PROFILE_LORAWAN};
    sim.profile_error_at_freq = 0x35;
    assert(sx1262_receiver_configure(&rx, &next) == SX1262_DEVICE_ERROR);
    assert(rx.device_errors == 0x40);
    /* Device error is sticky in the radio, so rollback also fails: fault. */
    assert(rx.state == SX1262_RX_FAULT && !sim.pins && rx.active.frequency_hz == 868100000u);

    start_default();
    sim.spi_fail_at_opcode = 0x8b;
    assert(sx1262_receiver_configure(&rx, &next) == SX1262_SPI_ERROR);
    assert(rx.state == SX1262_RX_FAULT && rx.active.frequency_hz == 868100000u && !sim.selected);

    /* Transient radio error: rollback restores the previous profile and keeps listening. */
    start_default();
    sim.profile_error_at_freq = 0x35;
    sim.transient = 1;
    uint32_t configs = rx.counters.configs;
    assert(sx1262_receiver_configure(&rx, &next) == SX1262_DEVICE_ERROR);
    assert(rx.state == SX1262_RX_RUNNING && sim.mode == 5 && sim.pins);
    assert(rx.counters.configs == configs && rx.active.frequency_hz == 868100000u);
    assert(rx.active.spreading_factor == 7 && rx.last_error == SX1262_DEVICE_ERROR);
    assert(sim.log[sim.frames - 13].bytes[0] == 0x86 && sim.log[sim.frames - 13].bytes[1] == 0x36);
    /* TCXO start failure at init is explicit, released, not silently cleared. */
    fresh(&rx);
    sx1262_receiver_confirm_board(&rx, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
    sim.calibrate_error = 1;
    sx1262_rx_lorawan_config(&c);
    assert(sx1262_receiver_configure(&rx, &c) == SX1262_DEVICE_ERROR);
    assert(rx.device_errors == 0x20 && rx.state == SX1262_RX_FAULT && !rx.active_valid);
    assert(!sim.pins && !sim.reset && count_opcode(0, 0x82) == 0);

    static const uint16_t unexpected_reset_errors[] = {0u, 0x21u, 0x40u};
    for (uint32_t i = 0u; i < 3u; ++i) {
        fresh(&rx);
        sx1262_receiver_confirm_board(&rx, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
        sim.reset_errors = unexpected_reset_errors[i];
        int32_t expected = i == 0u ? SX1262_RESPONSE_ERROR : SX1262_DEVICE_ERROR;
        assert(sx1262_receiver_configure(&rx, &c) == expected);
        assert(rx.state == SX1262_RX_FAULT && !sim.pins && !rx.active_valid);
        assert(rx.device_errors == unexpected_reset_errors[i]);
        assert(count_opcode(0, 0x97) == 0 && count_opcode(0, 0x07) == 0 &&
               count_opcode(0, 0x82) == 0);
    }

    /* Floating MISO at init is rejected before any configuration write. */
    fresh(&rx);
    sx1262_receiver_confirm_board(&rx, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
    sim.stuck_miso = 1;
    assert(sx1262_receiver_configure(&rx, &c) == SX1262_RESPONSE_ERROR && sim.frames == 1);

    /* Cancellation during reset releases RESET and pins. */
    fresh(&rx);
    sx1262_receiver_confirm_board(&rx, SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M);
    sim.cancel = 1;
    assert(sx1262_receiver_configure(&rx, &c) == -2999);
    assert(!sim.reset && !sim.pins && rx.state == SX1262_RX_FAULT && sim.frames == 0);
}

static void test_stop(void) {
    start_default();
    uint32_t from = sim.frames;
    sx1262_receiver_stop(&rx);
    uint8_t standby[] = {0x80, 0};
    assert(sim.frames == from + 1);
    expect(from, standby, 2);
    assert(rx.state == SX1262_RX_IDLE && !sim.pins && sim.mode == 2);
}

static void test_survey(void) {
    sx1262_rx_survey_t r;
    fresh(&rx);
    assert(sx1262_receiver_survey(&rx, 806000000u, &r) == SX1262_RX_STATE_ERROR && sim.frames == 0);
    start_default();
    assert(sim.antenna); /* ANT_SW option B: GP22 powers the RF switch */
    uint32_t before = sim.frames;
    assert(sx1262_receiver_survey(&rx, 149999999u, &r) == SX1262_RX_CONFIG_ERROR);
    assert(sx1262_receiver_survey(&rx, 960000001u, &r) == SX1262_RX_CONFIG_ERROR);
    assert(sim.frames == before && r.samples == 0);

    sim.rssi_inst = 150; /* -75 dBm */
    assert(sx1262_receiver_survey(&rx, 806000000u, &r) == 0);
    assert(r.frequency_hz == 806000000u && r.bandwidth_khz == 125u);
    assert(r.samples == SX1262_RX_SURVEY_SAMPLES);
    assert(r.min_qdb == -300 && r.mean_qdb == -300 && r.max_qdb == -300);
    /* Standby, image calibration 800..812 MHz, 806 MHz, continuous RX, status. */
    const uint8_t standby[] = {0x80, 0x00}, image[] = {0x98, 200, 203};
    const uint8_t freq[] = {0x86, 0x32, 0x60, 0x00, 0x00}, set_rx[] = {0x82, 0xff, 0xff, 0xff};
    expect(before, standby, 2);
    expect(before + 1, image, 3);
    expect(before + 2, freq, 5);
    expect(before + 3, set_rx, 4);
    assert(sim.log[before + 4].bytes[0] == 0xc0);
    for (uint32_t i = 0; i < SX1262_RX_SURVEY_SAMPLES; ++i)
        assert(sim.log[before + 5 + i].bytes[0] == 0x15 && sim.log[before + 5 + i].length == 3);
    /* Active profile restored: back on 868.1 MHz, in RX, statistics untouched. */
    const uint8_t home[] = {0x86, 0x36, 0x41, 0x99, 0x99};
    int restored = 0;
    for (uint32_t i = before + 5 + SX1262_RX_SURVEY_SAMPLES; i < sim.frames; ++i)
        restored |= memcmp(sim.log[i].bytes, home, 5) == 0;
    assert(restored && sim.mode == 5 && rx.state == SX1262_RX_RUNNING);
    assert(rx.active.frequency_hz == 868100000u && rx.noise_samples == 0);
    const uint8_t edge_low[] = {0x98, 36, 39}, edge_high[] = {0x98, 239, 241};
    before = sim.frames;
    assert(sx1262_receiver_survey(&rx, 150000000u, &r) == 0);
    expect(before + 1, edge_low, 3);
    before = sim.frames;
    assert(sx1262_receiver_survey(&rx, 960000000u, &r) == 0);
    expect(before + 1, edge_high, 3);

    /* SPI fault mid-survey: error returned, radio released like a poll fault. */
    sim.spi_fail_at_opcode = 0x15;
    assert(sx1262_receiver_survey(&rx, 806000000u, &r) == SX1262_SPI_ERROR);
    assert(r.samples == 0 && rx.state == SX1262_RX_FAULT && rx.last_error == SX1262_SPI_ERROR);
    assert(!sim.antenna);
    sim.spi_fail_at_opcode = 0;
}

int main(void) {
    test_init_frames();
    test_profiles();
    test_invalid_config();
    test_reception();
    test_parse();
    test_ring_bounds();
    test_runtime_faults();
    test_stop();
    test_meshtastic();
    test_packet_record();
    test_noise_floor();
    test_survey();
    puts("PASS: SX1262 RX-only frames, LoRaWAN/Meshtastic profiles, metadata, ring, records, noise floor, errors and cleanup");
    return 0;
}
