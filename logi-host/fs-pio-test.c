#include "fs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t sample(const fs_tx_t *tx, unsigned index) {
    return (uint8_t)(tx->words[index / 16u] >> (2u * (index % 16u))) & 3u;
}

typedef struct {
    uint32_t x, y, isr, osr, clock, irq, fifo[160], emitted, consumed;
    uint32_t output_clock[1024], release_clock, irq_clock[5];
    uint8_t pc, shift, out_shift, dirs, pins, output[1024];
} machine_t;

static void tx_model(const fs_tx_t *tx, uint8_t ack) {
    machine_t m = {.pins = 1u};
    uint32_t fifo[66]; unsigned count = 0u;
    if (ack) { m.pc = 30u; m.x = tx->count - 1u; }
    else fifo[count++] = tx->count - 1u;
    for (unsigned i = 0u; i < (tx->count + 15u) / 16u; ++i) fifo[count++] = tx->words[i];
    fifo[count++] = 0u;
    m.out_shift = 32u;
    for (unsigned steps = 0u; steps < 10000u && m.pc != 9u; ++steps) {
        uint16_t op = fs_pio_program[m.pc];
        uint8_t next = (uint8_t)(m.pc + 1u), kind = op >> 13u;
        if (kind == 4u) {
            assert((op & 0xffu) == 0xa0u && m.consumed < count);
            m.osr = fifo[m.consumed++]; m.out_shift = 0u;
        } else if (kind == 3u) {
            uint8_t bits = op & 31u; if (!bits) bits = 32u;
            if (m.out_shift >= 32u) {
                assert(m.consumed < count); m.osr = fifo[m.consumed++]; m.out_shift = 0u;
            }
            uint32_t value = bits == 32u ? m.osr : m.osr & ((1u << bits) - 1u);
            m.osr = bits == 32u ? 0u : m.osr >> bits; m.out_shift += bits;
            if (((op >> 5u) & 7u) == 1u) m.x = value;
            else {
                assert(((op >> 5u) & 7u) == 0u && bits == 2u && m.dirs == 3u);
                assert(m.emitted < 1024u);
                m.output[m.emitted] = (uint8_t)value;
                m.output_clock[m.emitted++] = m.clock;
            }
        } else if (kind == 7u) {
            unsigned dest = (op >> 5u) & 7u;
            if (dest == 0u) m.pins = op & 31u;
            else {
                assert(dest == 4u);
                m.dirs = op & 31u;
                if (!m.dirs) m.release_clock = m.clock;
            }
        } else if (kind == 0u) {
            unsigned condition = (op >> 5u) & 7u;
            if (!condition || (condition == 2u && m.x--)) next = op & 31u;
            else assert(condition == 2u);
        } else {
            assert(kind == 6u);
            unsigned flag = op & 7u;
            m.irq |= 1u << flag; m.irq_clock[flag] = m.clock;
        }
        if (ack && m.pc == 30u) next = 4u;
        m.clock += 1u + ((op >> 8u) & 31u);
        m.pc = next;
    }
    assert(m.pc == 9u && !m.dirs && m.emitted == tx->count);
    for (unsigned i = 0u; i < tx->count; ++i) {
        assert(m.output[i] == sample(tx, i));
        if (i) assert(m.output_clock[i] - m.output_clock[i - 1u] == 12u);
    }
    assert(m.release_clock - m.output_clock[m.emitted - 1u] == 12u);
    assert(m.irq_clock[4] < m.irq_clock[0]);
    if (ack) assert(m.output_clock[0] == 1u);
}

static uint8_t input(const fs_tx_t *tx, uint32_t clock, uint32_t start, int jitter, uint8_t short_eop) {
    if (clock < start + 2u) return 1u; /* RP2350 input synchronizer, two system clocks. */
    clock -= 2u;
    unsigned index = 0u;
    while (index + 1u < tx->count) {
        uint32_t boundary = start + (index + 1u) * 12u;
        if ((index + 1u) & 1u) boundary = (uint32_t)((int)boundary + jitter);
        if (clock < boundary) break;
        ++index;
    }
    if (short_eop == 1u && index >= tx->count - 2u) return 1u;
    if (short_eop == 2u && index >= tx->count - 1u) return 3u;
    if (clock >= start + tx->count * 12u) return 1u;
    return sample(tx, index);
}

static void rx_model(const fs_tx_t *tx, unsigned phase, int jitter, uint8_t short_eop) {
    machine_t m = {.pc = 10u, .y = 1023u, .irq = 16u};
    unsigned start = 40u + phase;
    for (unsigned steps = 0u; steps < 20000u && m.pc != 27u && !(m.irq & 8u); ++steps) {
        uint16_t op = fs_pio_program[m.pc];
        uint8_t next = (uint8_t)(m.pc + 1u), kind = op >> 13u;
        uint8_t pins = input(tx, m.clock, start, jitter, short_eop);
        unsigned delay = (op >> 8u) & 31u;
        if (kind == 1u) {
            unsigned source = (op >> 5u) & 3u, polarity = (op >> 7u) & 1u, index = op & 31u;
            unsigned value = source == 2u ? !!(m.irq & (1u << index)) : pins & 1u;
            if (value != polarity) { next = m.pc; delay = 0u; }
            else if (source == 2u && polarity) m.irq &= ~(1u << index);
        } else if (kind == 5u) {
            unsigned dest = (op >> 5u) & 7u, source = op & 7u;
            uint32_t value = source == 0u ? pins : source == 2u ? m.y : 0u;
            assert(source == 0u || source == 2u);
            if (dest == 7u) m.osr = value;
            else if (dest == 6u) m.isr = value;
            else assert(dest == 2u);
        } else if (kind == 3u) { assert((op & 255u) == 0x22u); m.x = m.osr & 3u; m.osr >>= 2u; }
        else if (kind == 2u) {
            unsigned bits = op & 31u, source = (op >> 5u) & 7u;
            assert((bits == 2u && source == 1u) || (bits == 16u && source == 2u));
            uint32_t value = source == 1u ? m.x : m.y;
            m.isr = (m.isr >> bits) | value << (32u - bits);
            m.shift += bits;
            if (m.shift >= 16u) {
                m.fifo[m.emitted++] = m.isr;
                m.isr = 0u; m.shift = 0u;
            }
        } else if (kind == 0u) {
            unsigned condition = (op >> 5u) & 7u;
            if (!condition || (condition == 1u && !m.x) || (condition == 4u && m.y--) ||
                (condition == 6u && (pins & 2u)))
                next = op & 31u;
        } else if (kind == 4u) {
            assert((op & 255u) == 0x20u);
            m.fifo[m.emitted++] = m.isr; m.isr = 0u; m.shift = 0u;
        } else {
            assert(kind == 6u);
            unsigned flag = op & 7u;
            m.irq |= 1u << flag; m.irq_clock[flag] = m.clock;
        }
        assert(m.emitted < 160u);
        m.clock += 1u + delay;
        m.pc = next;
    }
    if (short_eop) { assert(m.irq & 8u); return; }
    assert(m.pc == 27u && m.irq & 4u);
    unsigned count = 1023u - (m.fifo[m.emitted - 1u] >> 16u);
    assert(count == tx->count - 3u);
    assert(m.emitted == count / 8u + 1u);
    fs_rx_t rx; fs_rx_init(&rx);
    for (unsigned i = 0u; i < count / 8u; ++i)
        assert(!fs_rx_feed(&rx, (uint16_t)(m.fifo[i] >> 16u), 8u));
    if (count % 8u)
        assert(!fs_rx_feed(&rx, (uint16_t)m.fifo[m.emitted - 1u] >> (16u - 2u * (count % 8u)),
                           (uint8_t)(count % 8u)));
    fs_packet_t packet;
    assert(!fs_rx_finish(&rx, &packet));
    assert(m.irq_clock[2] >= start + (tx->count - 1u) * 12u + 24u);
}

static uint32_t timer, pins, writes, drives, released, pads, ctrl, clock_fail;
static uint32_t last_dirs, clock_multiplier, clock_post;
static uint32_t time_step = 100u, release_delay, awaiting_release, release_stuck;
static uint32_t transport, flags, fifo[100], available, consumed, ack_count, bad_eop, fifo_fault;
static uint32_t eop_early, ack_preloaded;

static void trace_model(void) {
    fs_tx_t ack = {0};
    assert(!fs_handshake(&ack, FS_ACK));
    for (unsigned phase = 0u; phase < 12u; ++phase) {
        machine_t m = {0};
        unsigned start = 40u + phase;
        uint8_t captured[128];
        unsigned count = 0u;
        uint32_t previous_sample = 0u;
        while (count < 128u && m.clock < 3000u) {
            uint8_t pins = 1u;
            if (m.clock >= start + 2u) {
                unsigned bit = (m.clock - start - 2u) / 12u;
                pins = bit < 2u ? 0u : bit < 6u ? 1u :
                       bit < 6u + ack.count ? sample(&ack, bit - 6u) : 1u;
            }
            uint16_t op = fs_trace_program[m.pc];
            uint8_t next = (uint8_t)(m.pc + 1u);
            switch (op >> 13u) {
                case 5u: m.osr = pins; break;
                case 3u: m.x = m.osr & 3u; break;
                case 0u:
                    if (!(op & 0xe0u) || !m.x) next = op & 31u;
                    break;
                case 2u:
                    if (count) assert(m.clock - previous_sample == 6u);
                    previous_sample = m.clock;
                    captured[count++] = pins;
                    break;
                case 6u: break;
                default: assert(0);
            }
            m.clock += 1u + ((op >> 8u) & 31u);
            m.pc = next;
        }
        assert(count == 128u);
        unsigned idle = captured[3] == 1u ? 3u : 4u;
        for (unsigned i = 0u; i < idle; ++i) assert(!captured[i]);
        for (unsigned i = idle; i < idle + 8u; ++i) assert(captured[i] == 1u);
        for (unsigned i = 0u; i < ack.count * 2u; ++i)
            assert(captured[idle + 8u + i] == sample(&ack, i / 2u));
    }
}
uint32_t fs_test_read(uint32_t address) {
    if (address == 0x400b000cu) return timer += time_step;
    if (address == 0xd0000004u) {
        if (last_dirs || (awaiting_release && release_stuck)) return 0u;
        if (awaiting_release && release_delay) { --release_delay; return 0u; }
        return pins << LOGI_DP;
    }
    if (address == 0x4001003cu) return ctrl;
    if (address == 0x40010044u) return ctrl & 1u ? 2u : 1u;
    if (address == 0x40020008u) return (1u << 14u) | (1u << 12u);
    if (address == 0x40050000u) return clock_fail ? 0u : 1u << 31u;
    if (transport && address == LOGI_PIO + 4u) return consumed < available ? 0u : 1u << 9u;
    if (transport && address == LOGI_PIO + 0x30u) return flags;
    if (transport && address == LOGI_PIO + 8u) return fifo_fault;
    if (transport && address == LOGI_PIO + 0x24u) {
        assert(consumed < available);
        uint32_t value = fifo[consumed++];
        if (eop_early || consumed == available) flags |= 2u;
        if (consumed == available) flags |= 2u | (bad_eop ? 8u : 4u);
        return value;
    }
    assert(0);
    return 0u;
}
void fs_test_write(uint32_t address, uint32_t value) {
    ++writes;
    assert(address != LOGI_PIO + 0x14u);
    if (address == LOGI_PIO + 0xe8u && value)
        assert(value == ((1u << 31u) | (16u << 20u) | (1u << 19u) | (1u << 18u) | (1u << 16u)));
    if (address == 0x4001003cu) ctrl = value;
    if (address == 0x40050008u) clock_multiplier = value;
    if (address == 0x4005000cu) clock_post = value;
    if (transport && address == LOGI_PIO + 0x30u) flags &= ~value;
    if (transport && address == LOGI_PIO + 0x18u) ++ack_preloaded;
    if (transport && address == LOGI_PIO && value == 3u) {
        assert(ack_preloaded == 4u);
        flags |= 17u;
    }
    if (transport && address == LOGI_PIO && value == 4u) { ++ack_count; flags |= 17u; }
    if (address == LOGI_PIO + 0xd8u && (value == 0xe083u || value == 0xe080u)) {
        if (last_dirs && !(value & 3u)) awaiting_release = 1u;
        if (value & 3u) awaiting_release = 0u;
        last_dirs = value & 3u;
        if (last_dirs) { assert(pads == 3u); ++drives; released = timer; }
        else if (drives) assert(timer - released >= 20000u);
    }
    for (unsigned pin = LOGI_DP; pin <= LOGI_DM; ++pin)
        if (address == 0x40038004u + pin * 4u && value == 0x67u) pads |= 1u << (pin - LOGI_DP);
}

static void response_fifo(const fs_tx_t *response) {
    available = consumed = flags = ack_count = bad_eop = fifo_fault = ack_preloaded = 0u;
    unsigned n = 0u;
    uint32_t word = 0u, bits = response->count - 3u;
    for (unsigned i = 0u; i < bits; ++i) {
        word = word >> 2u | (uint32_t)sample(response, i) << 30u;
        if (++n == 8u) { fifo[available++] = word; word = n = 0u; }
    }
    fifo[available++] = ((1023u - bits) << 16u) | (word >> 16u);
}

static void transport_tests(void) {
    fs_tx_t tx = {0}, response = {0};
    fs_packet_t packet;
    assert(!fs_token(&tx, FS_IN, 0u, 0u));
    const uint8_t bytes[] = {18,1,0,2,0,0,0,64};
    assert(!fs_data(&response, FS_DATA1, bytes, 8u));
    transport = 1u;
    response_fifo(&response);
    assert(!fs_pio_exchange(0, &tx, 1u, &packet));
    assert(packet.pid == FS_DATA1 && packet.length == 8u && !memcmp(packet.data, bytes, 8u));
    assert(ack_count == 1u && !last_dirs);
    for (unsigned early = 0u; early < 2u; ++early) {
        eop_early = early;
        uint8_t payload[64];
        memset(payload, 255, sizeof(payload));
        for (unsigned length = 0u; length <= 64u; ++length) {
            fs_tx_t data = {0};
            assert(!fs_data(&data, FS_DATA1, payload, (uint8_t)length));
            response_fifo(&data);
            assert(!fs_pio_exchange(0, &tx, 1u, &packet));
            assert(packet.length == length && !memcmp(packet.data, payload, length) && ack_count == 1u);
        }
    }
    eop_early = 0u;
    response_fifo(&response); bad_eop = 1u;
    assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_ENCODING && !ack_count && !last_dirs);
    for (unsigned fault = 1u; fault <= 4u; fault <<= 1u) {
        response_fifo(&response); fifo_fault = fault;
        assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_FIFO && !ack_count && !last_dirs);
    }
    response.words[1] ^= 3u << 8u;
    response_fifo(&response);
    assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_CRC && !ack_count);
    response.words[1] ^= 3u << 8u;
    response.words[0] ^= 3u << 16u;
    response_fifo(&response);
    assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_PID && !ack_count);
    response.words[0] ^= 3u << 16u;
    response.words[0] ^= 3u;
    response_fifo(&response);
    assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_ENCODING && !ack_count);
    response.words[0] ^= 3u;
    response.words[0] |= 3u;
    response_fifo(&response);
    assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_LINE && !ack_count);
    fs_tx_init(&response);
    assert(!fs_handshake(&response, FS_NAK));
    response_fifo(&response);
    assert(!fs_pio_exchange(0, &tx, 1u, &packet) && packet.pid == FS_NAK && !ack_count);
    response_fifo(&response); bad_eop = 1u;
    assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_ENCODING && !ack_count);
    available = consumed = ack_count = ack_preloaded = 0u;
    assert(fs_pio_exchange(0, &tx, 1u, &packet) == FS_TIMEOUT && !ack_count && !last_dirs);
    transport = 0u;
}

int main(void) {
    fs_wire_init();
    assert(!fs_pio_clock());
    assert(clock_multiplier == 120u && clock_post == ((5u << 16u) | (2u << 12u)));
    assert(12000000u * clock_multiplier / 5u / 2u == FS_SYS_HZ);
    clock_fail = 1u;
    assert(fs_pio_clock() == FS_CLOCK);
    for (pins = 0u; pins < 4u; ++pins) if (pins != 1u) {
        writes = 0u;
        assert(fs_pio_reset(0) == FS_LINE && !writes);
    }
    pins = 1u;
    assert(!fs_pio_reset(0) && drives == 1u && !last_dirs);
    fs_pio_stop();
    awaiting_release = 0u; release_delay = 4u; time_step = 1u;
    fs_result_t reset_result = {0};
    assert(!fs_pio_reset(&reset_result) && reset_result.reset_levels == 0xd1u && !release_delay);
    awaiting_release = 0u; release_stuck = 1u;
    assert(fs_pio_reset(&reset_result) == FS_LINE && reset_result.reset_levels == 0xc1u && !last_dirs);
    release_stuck = awaiting_release = 0u; time_step = 100u;
    uint8_t payload[64];
    memset(payload, 255, sizeof(payload));
    for (unsigned length = 0u; length <= 64u; ++length) {
        fs_tx_t tx = {0};
        assert(!fs_data(&tx, FS_DATA1, payload, (uint8_t)length));
        tx_model(&tx, 0u);
        for (unsigned phase = 0u; phase < 12u; ++phase) {
            rx_model(&tx, phase, 0, 0u);
            rx_model(&tx, phase, 1, 0u);
            rx_model(&tx, phase, -1, 0u);
        }
        rx_model(&tx, 0u, 0, 1u);
        rx_model(&tx, 0u, 0, 2u);
    }
    fs_tx_t ack = {0};
    assert(!fs_handshake(&ack, FS_ACK));
    tx_model(&ack, 1u);
    trace_model();
    transport_tests();
    puts("PIO: actual 32-instruction TX/RX model, 12 clocks/bit, all payload sizes,");
    puts("phase/jitter, partial FIFO trailers, 2-bit EOP, ACK entry/gap, gated reset/release PASS");
    return 0;
}
