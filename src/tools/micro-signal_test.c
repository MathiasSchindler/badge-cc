#include "micro-signal.h"
#include "../../clean-rx/src/sdio_ring.h"
#include "../../clean-rx/sdio_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static micro_signal_event_t sample(void) {
    micro_signal_event_t event = {{0x12,0x34,0x56,0x78,0x90,0xab,0xcd,0xef},
                                  0x12345678u, 42u, 1u};
    return event;
}

static uint32_t independent_crc(const uint8_t *bytes, uint32_t length) {
    uint32_t crc = UINT32_MAX;
    while (length--) {
        crc ^= *bytes++;
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

static void frames(void) {
    micro_signal_event_t input = sample(), output;
    uint8_t frame[96], changed[96];
    for (uint8_t button = 1u; button <= 5u; ++button) {
        input.button = button;
        micro_signal_frame(frame, &input);
        assert(frame[0] == 0x6eu && frame[1] == 4u && frame[2] == 64u);
        assert(frame[3] == 0u && frame[4] == 0x7fu && frame[5] == 0xeeu);
        assert(frame[6] == 0xd0u && frame[30] == 127u);
        assert(frame[94] == 0u && frame[95] == 0u);
        assert(frame[63] == '0' + button);
        assert(memcmp(frame + 56u, "BUTTON ", 7u) == 0);
        assert(memcmp(frame + 64u, " PRESSED", 8u) == 0);
        assert(independent_crc(frame + 6u, 84u) == badger_bytes_load_u32(frame + 90u));
        assert(micro_signal_decode(frame + 6u, 88u, &output));
        assert(output.button == button && output.sequence == input.sequence);
        assert(output.session == input.session && memcmp(output.sender, input.sender, 8u) == 0);
        for (uint32_t length = 0u; length < 88u; ++length)
            assert(!micro_signal_decode(frame + 6u, length, &output));
        assert(!micro_signal_decode(frame + 6u, 89u, &output));
        for (uint32_t i = 6u; i < 94u; ++i) {
            memcpy(changed, frame, sizeof(frame));
            changed[i] ^= 1u;
            assert(!micro_signal_decode(changed + 6u, 88u, &output));
        }
        static const uint8_t fixed[] = {6,7,10,16,22,30,31,34,38,56,64,80};
        for (uint32_t i = 0u; i < sizeof(fixed); ++i) {
            memcpy(changed, frame, sizeof(frame));
            changed[fixed[i]] ^= 1u;
            badger_bytes_store_u32(changed + 90u, independent_crc(changed + 6u, 84u));
            assert(!micro_signal_decode(changed + 6u, 88u, &output));
        }
    }
    input.button = 0u;
    micro_signal_frame(frame, &input);
    assert(!micro_signal_decode(frame + 6u, 88u, &output));
    input.button = 6u;
    micro_signal_frame(frame, &input);
    assert(!micro_signal_decode(frame + 6u, 88u, &output));
}

static void duplicates(void) {
    uint8_t self[8] = {1u};
    micro_signal_peers_t peers = {0};
    micro_signal_event_t event = sample();
    assert(!micro_signal_accept(&peers, &event, event.sender));
    assert(micro_signal_accept(&peers, &event, self));
    assert(!micro_signal_accept(&peers, &event, self));
    ++event.sequence;
    assert(micro_signal_accept(&peers, &event, self));
    --event.sequence;
    assert(!micro_signal_accept(&peers, &event, self));
    ++event.session;
    event.sequence = UINT32_MAX;
    assert(micro_signal_accept(&peers, &event, self));
    event.sequence = 0u;
    assert(micro_signal_accept(&peers, &event, self));
    assert(!micro_signal_accept(&peers, &event, self));
    ++event.session;
    event.sequence = 1u;
    assert(micro_signal_accept(&peers, &event, self));
    for (uint32_t i = 0u; i < 10u; ++i) {
        event.sender[0] = (uint8_t)(i + 40u);
        assert(micro_signal_accept(&peers, &event, self));
        assert(!micro_signal_accept(&peers, &event, self));
    }
    assert(peers.used == 4u);
}

static void debounce(void) {
    diag_buttons_t state = {0};
    assert(diag_buttons_update(&state, 0u, 0u) == 0u);
    assert(diag_buttons_update(&state, 1u, 100u) == 0u);
    assert(diag_buttons_update(&state, 0u, 1000u) == 0u);
    assert(diag_buttons_update(&state, 1u, 1100u) == 0u);
    assert(diag_buttons_update(&state, 1u, 21099u) == 0u);
    assert(diag_buttons_update(&state, 1u, 21100u) == 1u);
    assert(diag_buttons_update(&state, 1u, 100000u) == 0u);
    assert(diag_buttons_update(&state, 0u, 100100u) == 0u);
    assert(diag_buttons_update(&state, 0u, 120100u) == 0u);
    assert(diag_buttons_update(&state, 31u, 120101u) == 0u);
    assert(diag_buttons_update(&state, 31u, 140101u) == 31u);
    assert(diag_buttons_update(&state, 31u, 160101u) == 0u);
    memset(&state, 0, sizeof(state));
    assert(diag_buttons_update(&state, 16u, UINT32_MAX - 9999u) == 0u);
    assert(diag_buttons_update(&state, 16u, 10000u) == 16u);
}

static void sdio_ring(void) {
    uint32_t producer = 0u, consumer = 0u, written = 0u, read = 0u, counts = 0u;
    uint32_t buffers[32] = {0};
    for (uint32_t step = 0u; step < 10000u; ++step) {
        int available = clean_sdio_available(producer, 0x10000000u | consumer * 16u);
        assert(available >= 0);
        if (available) {
            buffers[producer] = ++written;
            assert((clean_sdio_flags(producer) & 0x10000000u) ==
                   (producer == 31u ? 0x10000000u : 0u));
            producer = (producer + 1u) & 31u;
            counts = clean_sdio_count(counts, 0u);
        } else {
            assert(written - read == 31u);
            counts = clean_sdio_count(counts, 1u);
        }
        if (step % 3u == 0u && consumer != producer) {
            assert(buffers[consumer] == ++read);
            consumer = (consumer + 1u) & 31u;
        }
        assert(written - read <= 31u);
    }
    while (consumer != producer) {
        assert(buffers[consumer] == ++read);
        consumer = (consumer + 1u) & 31u;
    }
    assert(read == written && written > 3000u);
    assert((counts & 65535u) == written && (counts >> 16u) != 0u);
    assert(clean_sdio_available(32u, 0u) == -1);
    assert(clean_sdio_available(0u, 1u) == -1);
    assert(clean_sdio_available(0u, 512u) == -1);
    assert(clean_sdio_count(0xffffffffu, 0u) == 0xffffffffu);
    assert(clean_sdio_count(0xffffffffu, 1u) == 0xffffffffu);
    assert(clean_sdio_count(0x1234ffffu, 0u) == 0x1234ffffu);
    assert(clean_sdio_count(0xffff1234u, 1u) == 0xffff1234u);
}

static void sdio_patterns(void) {
    uint8_t lengths[497] = {0};
    for (uint32_t sequence = 1u; sequence <= 497u; ++sequence) {
        uint32_t length = clean_sdio_test_length(sequence);
        assert(length >= 8u && length <= 504u);
        assert(++lengths[length - 8u] == 1u);
        assert(length + 6u <= 510u);
        assert(((length + 21u) & ~3u) <= 524u);
        assert(clean_sdio_test_pattern_length(sequence, 0u) == length);
        uint32_t transition = clean_sdio_test_pattern_length(sequence, 1u) + 18u;
        assert(transition == ((sequence & 1u) ? 483u : 520u));
        assert(((transition + 3u) & ~3u) == ((sequence & 1u) ? 484u : 520u));
    }
    assert(clean_sdio_test_length(6743u) == 502u);
    assert(clean_sdio_test_length(927u) == 14u);
    assert(clean_sdio_test_byte(0u, 0u) == 0u);
    assert(clean_sdio_test_byte(1u, 0u) == 52u);
    assert(clean_sdio_test_byte(1u, 503u) == 183u);
    assert(clean_sdio_test_byte(927u, 0u) == 5u);
    assert(clean_sdio_test_byte(6743u, 501u) == 189u);
    assert(clean_sdio_test_byte(UINT32_MAX, UINT32_MAX) == 150u);
}

static void sdio_windows(void) {
    static const uint32_t consumed[] = {0u, 32u, UINT32_MAX - 4u};
    for (uint32_t base = 0u; base < 3u; ++base) {
        for (uint32_t window = 0u; window <= 16u; ++window) {
            for (uint32_t pending = 0u; pending <= 17u; ++pending) {
                assert(clean_sdio_test_window_open(consumed[base] + pending,
                                                   consumed[base], window) ==
                       (window == 0u || pending < window));
            }
        }
    }
    assert(!clean_sdio_test_window_open(2u, 3u, 1u));
    assert(clean_sdio_credit_available(0u, 0u, 1u) == 1);
    assert(clean_sdio_credit_available(1u, 0u, 1u) == 0);
    assert(clean_sdio_credit_available(2u, 0u, 1u) == -1);
    assert(clean_sdio_credit_available(0u, 1u, 1u) == -1);
    assert(clean_sdio_credit_available(0u, 0xffffffffu, 1u) == 0);
    assert(clean_sdio_credit_available(0u, 0u, 17u) == -1);
    assert(((CLEAN_SDIO_DMA_CONTROL >> 18u) & 7u) == 1u);
    assert((CLEAN_SDIO_DMA_CONTROL & 0x801u) == 0x801u);
}

int main(void) {
    frames();
    duplicates();
    debounce();
    sdio_ring();
    sdio_patterns();
    sdio_windows();
    puts("micro-signal: framing, duplicates, debounce, SDIO ring, host windows and all 497 test lengths OK");
    return 0;
}
