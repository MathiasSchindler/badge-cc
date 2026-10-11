#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../radio/wifi_probe.h"
#include "../../tools/wifi_bringup_view.h"

typedef struct {
    uint32_t now;
    uint32_t tick_step;
    uint32_t ticks;
    uint32_t freeze_after;
    uint32_t tx_bits;
    uint32_t rx_bits;
    uint32_t transactions;
    uint32_t writes;
    uint32_t cold_reads;
    uint32_t normal_reads;
    uint32_t clock_reads;
    uint32_t chip_reads;
    uint32_t config;
    uint32_t fail_normal_read;
    uint32_t cold_failures;
    uint8_t mode32;
    uint8_t power;
    uint8_t clock;
    uint8_t select;
    uint8_t data;
    uint8_t output;
    uint8_t padding;
    uint8_t window[3];
    uint8_t tx[8];
    uint8_t rx[20];
    uint8_t last_tx[8];
    uint32_t last_tx_length;
    uint32_t last_rx_length;
    uint8_t bad_config;
    uint8_t bad_padding;
    uint8_t no_clock;
    uint8_t clock_all_ones;
    uint8_t bad_window;
    uint8_t bad_chip;
    uint8_t changing_chip;
    uint8_t progress_count;
    wifi_probe_stage_t last_stage;
} model_t;

static uint32_t parse_word(const uint8_t *bytes, uint8_t mode32) {
    if (mode32) return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8u |
                      (uint32_t)bytes[2] << 16u | (uint32_t)bytes[3] << 24u;
    return (uint32_t)bytes[1] | (uint32_t)bytes[0] << 8u |
           (uint32_t)bytes[3] << 16u | (uint32_t)bytes[2] << 24u;
}

static void prepare_read(model_t *model) {
    assert(model->tx_bits == 32u);
    uint32_t command = parse_word(model->tx, model->mode32);
    assert((command & 0xc0000000u) == 0x40000000u);
    uint8_t function = (uint8_t)((command >> 28u) & 3u);
    uint32_t address = (command >> 11u) & 0x1ffffu;
    uint32_t length = command & 0x7ffu;
    assert(length == 1u || length == 2u || length == 4u);
    uint32_t value = 0u;
    if (function == 0u && address == 0x14u) {
        if (!model->mode32) {
            ++model->cold_reads;
            value = model->cold_reads <= model->cold_failures ? 0u : 0xfeedbeadu;
        } else {
            ++model->normal_reads;
            value = model->normal_reads == model->fail_normal_read ? 0u : 0xfeedbeadu;
        }
    } else if (function == 0u && address == 0u) {
        value = model->config ^ (model->bad_config ? 1u : 0u);
    } else if (function == 0u && address == 0x1du) {
        value = model->bad_padding ? 4u : model->padding;
    } else if (function == 1u && address == 0x1000eu) {
        ++model->clock_reads;
        value = model->no_clock ? 8u : 0x48u;
        if (model->clock_all_ones) value = 0xffu;
    } else if (function == 1u && address >= 0x1000au && address <= 0x1000cu) {
        value = model->window[address - 0x1000au];
        if (model->bad_window) value ^= 1u;
    } else if (function == 1u && address == 0x8000u) {
        assert(model->window[2] == 0x18u && model->window[1] == 0u && model->window[0] == 0u);
        ++model->chip_reads;
        value = model->bad_chip ? 0xffffffffu : 0x1234a9afu;
        if (model->changing_chip && model->chip_reads > 1u) value ^= 1u;
    } else {
        assert(!"Unexpected probe register read");
    }
    memset(model->rx, 0x69, sizeof(model->rx));
    unsigned offset = function == 1u ? 16u : 0u;
    if (function == 1u) assert(model->padding == 16u);
    if (model->mode32) {
        for (unsigned i = 0u; i < 4u; ++i) model->rx[offset + i] = (uint8_t)(value >> (8u * i));
    } else {
        model->rx[offset] = (uint8_t)(value >> 8u);
        model->rx[offset + 1u] = (uint8_t)value;
        model->rx[offset + 2u] = (uint8_t)(value >> 24u);
        model->rx[offset + 3u] = (uint8_t)(value >> 16u);
    }
}

static void finish_transaction(model_t *model) {
    ++model->transactions;
    model->last_tx_length = model->tx_bits / 8u;
    model->last_rx_length = model->rx_bits / 8u;
    memcpy(model->last_tx, model->tx, sizeof(model->tx));
    if (model->tx_bits != 64u) return;
    assert(model->rx_bits == 0u);
    uint32_t command = parse_word(model->tx, model->mode32);
    assert((command & 0xc0000000u) == 0xc0000000u);
    uint8_t function = (uint8_t)((command >> 28u) & 3u);
    uint32_t address = (command >> 11u) & 0x1ffffu;
    uint32_t value = parse_word(model->tx + 4u, model->mode32);
    ++model->writes;
    if (function == 0u && address == 0u) {
        assert(!model->mode32);
        assert(value == 0x000200a3u);
        model->mode32 = 1u;
        model->config = value & ~0x80u;
    } else if (function == 0u && address == 0x1du) {
        assert(value == 16u);
        model->padding = (uint8_t)value;
    } else if (function == 1u && address == 0x1000eu) {
        assert(value == 8u || value == 0u);
    } else if (function == 1u && address >= 0x1000au && address <= 0x1000cu) {
        model->window[address - 0x1000au] = (uint8_t)value;
    } else {
        assert(!"Unexpected probe register write");
    }
}

static void write_signal(void *context, wifi_signal_t signal, uint8_t high) {
    model_t *model = context;
    assert(high <= 1u);
    switch (signal) {
        case WIFI_SIGNAL_ENABLE:
            assert(model->select && !model->clock);
            if (high) assert(model->output && model->data == 0u);
            else assert(!model->output);
            if (!high) {
                model->mode32 = 0u;
                model->padding = 0u;
                model->config = 0u;
            }
            model->power = high;
            break;
        case WIFI_SIGNAL_SELECT:
            if (!model->select && high) finish_transaction(model);
            if (model->select && !high) {
                assert(!model->clock && !model->output);
                model->tx_bits = model->rx_bits = 0u;
                memset(model->tx, 0, sizeof(model->tx));
            }
            model->select = high;
            break;
        case WIFI_SIGNAL_CLOCK:
            if (!model->clock && high) {
                assert(!model->select);
                if (model->output) {
                    assert(model->tx_bits < 64u);
                    unsigned byte = model->tx_bits / 8u;
                    model->tx[byte] = (uint8_t)((model->tx[byte] << 1u) | model->data);
                    ++model->tx_bits;
                }
            }
            model->clock = high;
            break;
        case WIFI_SIGNAL_DATA:
            assert(!model->clock);
            model->data = high;
            break;
    }
}

static void data_output(void *context, uint8_t enabled) {
    model_t *model = context;
    if (enabled && model->power) assert(!model->select);
    if (!model->select && !enabled && model->output) {
        assert(!model->clock);
        if (model->tx_bits == 32u) prepare_read(model);
    }
    model->output = enabled;
}

static uint8_t read_data(void *context) {
    model_t *model = context;
    assert(!model->select && !model->output && !model->clock);
    assert(model->rx_bits < 160u);
    uint8_t value = (uint8_t)((model->rx[model->rx_bits / 8u] >>
                              (7u - model->rx_bits % 8u)) & 1u);
    ++model->rx_bits;
    return value;
}

static uint32_t time_us(void *context) {
    model_t *model = context;
    if (model->freeze_after == 0u || model->ticks < model->freeze_after) {
        model->now += model->tick_step;
    }
    ++model->ticks;
    return model->now;
}

static wifi_wire_t setup(model_t *model) {
    memset(model, 0, sizeof(*model));
    model->select = 1u;
    model->tick_step = 1u;
    return (wifi_wire_t){model, write_signal, data_output, read_data, time_us, 0, 0};
}

static uint32_t trace_events;
static void trace_observe(void *context, wifi_trace_event_t event, const uint8_t *bytes, uint32_t length) {
    model_t *model = context;
    trace_events |= 1u << event;
    if (event == WIFI_TRACE_IDLE) {
        assert(model->select && !model->output && length == 4u);
        const uint8_t command[] = {0xa0u, 0x04u, 0x40u, 0u};
        assert(memcmp(bytes, command, 4u) == 0);
    }
    if (event == WIFI_TRACE_FINISHED) {
        assert(model->select && !model->output && length == 4u);
        const uint8_t response[] = {0xbeu, 0xadu, 0xfeu, 0xedu};
        assert(memcmp(bytes, response, 4u) == 0);
    }
}

static void progress(void *context, const wifi_probe_report_t *report) {
    model_t *model = context;
    assert(model->select && !model->output && !model->clock);
    assert(report->stage >= model->last_stage || report->stage == WIFI_PROBE_RESET);
    model->last_stage = report->stage;
    ++model->progress_count;
}

static void test_framing(void) {
    uint8_t command[4], reply[4];
    const uint8_t reset_read[] = {0xa0u, 0x04u, 0x40u, 0x00u};
    const uint8_t normal_read[] = {0x04u, 0xa0u, 0x00u, 0x40u};
    const uint8_t backplane[] = {0x04u, 0x00u, 0x00u, 0x54u};
    const uint8_t reset_reply[] = {0xbeu, 0xadu, 0xfeu, 0xedu};
    const uint8_t normal_reply[] = {0xadu, 0xbeu, 0xedu, 0xfeu};
    assert(wifi_gspi_command(command, 0u, 0u, 0x14u, 4u, WIFI_ORDER_RESET16) == WIFI_BUS_OK);
    assert(memcmp(command, reset_read, 4u) == 0);
    assert(wifi_gspi_command(command, 0u, 0u, 0x14u, 4u, WIFI_ORDER_NORMAL32) == WIFI_BUS_OK);
    assert(memcmp(command, normal_read, 4u) == 0);
    assert(wifi_gspi_command(command, 0u, 1u, 0x8000u, 4u, WIFI_ORDER_NORMAL32) == WIFI_BUS_OK);
    assert(memcmp(command, backplane, 4u) == 0);
    assert(wifi_gspi_decode_word(reset_reply, WIFI_ORDER_RESET16) == 0xfeedbeadu);
    assert(wifi_gspi_decode_word(normal_reply, WIFI_ORDER_NORMAL32) == 0xfeedbeadu);
    wifi_gspi_encode_word(reply, 0xfeedbeadu, WIFI_ORDER_RESET16);
    assert(memcmp(reply, reset_reply, 4u) == 0);
    assert(wifi_gspi_command(command, 0u, 2u, 0u, 4u, WIFI_ORDER_NORMAL32) == WIFI_BUS_BAD_ARGUMENT);
    assert(wifi_gspi_command(command, 0u, 1u, 0x20000u, 4u, WIFI_ORDER_NORMAL32) == WIFI_BUS_BAD_ARGUMENT);
    assert(wifi_gspi_command(command, 0u, 0u, 0u, 3u, WIFI_ORDER_NORMAL32) == WIFI_BUS_BAD_ARGUMENT);
    assert(wifi_gspi_command(command, 0u, 1u, 0u, 4u, WIFI_ORDER_RESET16) == WIFI_BUS_BAD_ARGUMENT);
    assert(wifi_gspi_command(command, 0u, 0u, 0u, 1u, WIFI_ORDER_RESET16) == WIFI_BUS_BAD_ARGUMENT);
}

static void test_wire(void) {
    model_t model;
    wifi_wire_t wire = setup(&model);
    uint32_t value = 0u;
    assert(wifi_gspi_read(&wire, 0u, 0x14u, 4u, WIFI_ORDER_RESET16, &value) == WIFI_BUS_OK);
    assert(value == 0xfeedbeadu);
    assert(model.last_tx_length == 4u && model.last_rx_length == 4u);
    const uint8_t expected[] = {0xa0u, 0x04u, 0x40u, 0u};
    assert(memcmp(model.last_tx, expected, 4u) == 0);
    assert(model.select && !model.clock && !model.output);
    wire.observe = trace_observe;
    trace_events = 0u;
    assert(wifi_gspi_read(&wire, 0u, 0x14u, 4u, WIFI_ORDER_RESET16, &value) == WIFI_BUS_OK);
    assert(trace_events == 0x1fu);

    wire = setup(&model);
    model.mode32 = 1u;
    model.padding = 16u;
    assert(wifi_gspi_read(&wire, 1u, 0x1000eu, 1u, WIFI_ORDER_NORMAL32, &value) == WIFI_BUS_OK);
    assert(value == 0x48u);
    assert(model.last_tx_length == 4u && model.last_rx_length == 20u);
    uint32_t transactions = model.transactions;
    assert(wifi_gspi_read(&wire, 1u, 0x20000u, 1u, WIFI_ORDER_NORMAL32, &value) == WIFI_BUS_BAD_ARGUMENT);
    assert(value == 0u && model.transactions == transactions);
    assert(wifi_gspi_write(&wire, 1u, 0x1000eu, 1u, WIFI_ORDER_NORMAL32, 256u) == WIFI_BUS_BAD_ARGUMENT);
    assert(model.transactions == transactions);
    assert(wifi_wire_transfer(&wire, expected, 9u, 0, 0u) == WIFI_BUS_BAD_ARGUMENT);
    assert(wifi_wire_transfer(&wire, expected, 4u, 0, 4u) == WIFI_BUS_BAD_ARGUMENT);

    wire = setup(&model);
    model.freeze_after = 20u;
    assert(wifi_gspi_read(&wire, 0u, 0x14u, 4u, WIFI_ORDER_RESET16, &value) == WIFI_BUS_TIMER_STALLED);
    assert(model.select && !model.clock && !model.output && value == 0u);
    wire = setup(&model);
    model.tick_step = 50000u;
    assert(wifi_gspi_read(&wire, 0u, 0x14u, 4u, WIFI_ORDER_RESET16, &value) == WIFI_BUS_TRANSFER_TIMEOUT);
    assert(model.select && !model.clock && !model.output);
    wire = setup(&model);
    model.now = UINT32_MAX - 100u;
    assert(wifi_gspi_read(&wire, 0u, 0x14u, 4u, WIFI_ORDER_RESET16, &value) == WIFI_BUS_OK);
    assert(value == 0xfeedbeadu);
}

static void test_probe(void) {
    model_t model;
    wifi_wire_t wire = setup(&model);
    wifi_probe_report_t report;
    model.cold_failures = 2u;
    model.now = UINT32_MAX - 5000u;
    assert(wifi_probe_run(&wire, &report, progress, &model) == WIFI_BUS_OK);
    assert(report.stage == WIFI_PROBE_FINISHED);
    assert(report.reset_attempts == 3u && report.passed_reads == 16u);
    assert(report.normal_pattern == 0xfeedbeadu && report.chip_id == 0x1234a9afu);
    assert(report.powered && model.power && model.select && !model.output && !model.clock);
    assert(model.progress_count == 8u && model.chip_reads == 2u);
    assert(report.elapsed_us >= 270000u && report.elapsed_us < 1000000u);
    assert(wifi_probe_run(&wire, &report, progress, &model) == WIFI_BUS_OK);
    wifi_wire_power_off(&wire);
    assert(!model.power && !model.mode32 && model.select && !model.output);

    for (unsigned fault = 0u; fault < 9u; ++fault) {
        wire = setup(&model);
        wifi_bus_result_t expected;
        switch (fault) {
            case 0: model.cold_failures = 100u; expected = WIFI_BUS_TEST_MISMATCH; break;
            case 1: model.bad_config = 1u; expected = WIFI_BUS_CONFIG_MISMATCH; break;
            case 2: model.fail_normal_read = 5u; expected = WIFI_BUS_TEST_MISMATCH; break;
            case 3: model.bad_padding = 1u; expected = WIFI_BUS_CONFIG_MISMATCH; break;
            case 4: model.no_clock = 1u; expected = WIFI_BUS_CLOCK_TIMEOUT; break;
            case 5: model.clock_all_ones = 1u; expected = WIFI_BUS_CLOCK_TIMEOUT; break;
            case 6: model.bad_window = 1u; expected = WIFI_BUS_CONFIG_MISMATCH; break;
            case 7: model.bad_chip = 1u; expected = WIFI_BUS_BACKPLANE_INVALID; break;
            default: model.changing_chip = 1u; expected = WIFI_BUS_BACKPLANE_INVALID; break;
        }
        assert(wifi_probe_run(&wire, &report, progress, &model) == expected);
        assert(report.result == expected && !report.powered && !model.power);
        assert(model.select && !model.output && !model.clock);
        assert(report.stage != WIFI_PROBE_FINISHED);
        assert(report.elapsed_us < 1000000u);
        if (fault == 0u) assert(model.writes == 0u && report.reset_attempts == 10u);
        if (fault == 2u) assert(report.passed_reads == 4u);
    }
    wire = setup(&model);
    model.tick_step = 0u;
    assert(wifi_probe_run(&wire, &report, progress, &model) == WIFI_BUS_TIMER_STALLED);
    assert(report.stage == WIFI_PROBE_RESET && !model.power);
}

static badger_canvas_t canvas;

static void test_view(const char *path) {
    wifi_probe_report_t report = {0};
    for (unsigned stage = WIFI_PROBE_IDLE; stage <= WIFI_PROBE_FINISHED; ++stage) {
        report.stage = (wifi_probe_stage_t)stage;
        for (unsigned result = WIFI_BUS_OK; result <= WIFI_BUS_RADIO_OFF; ++result) {
            report.result = (wifi_bus_result_t)result;
            wifi_bringup_render(&report, &canvas);
        }
    }
    model_t model;
    wifi_wire_t wire = setup(&model);
    assert(wifi_probe_run(&wire, &report, 0, 0) == WIFI_BUS_OK);
    wifi_bringup_render(&report, &canvas);
    if (path == 0) return;
    FILE *file = fopen(path, "wb");
    assert(file != 0);
    assert(fprintf(file, "P6\n320 240\n255\n") > 0);
    for (unsigned y = 0u; y < 240u; ++y) {
        for (unsigned x = 0u; x < 320u; ++x) {
            uint16_t pixel = canvas.pixels[x * 240u + y];
            uint8_t rgb[] = {(uint8_t)(((pixel >> 11u) & 31u) * 255u / 31u),
                             (uint8_t)(((pixel >> 5u) & 63u) * 255u / 63u),
                             (uint8_t)((pixel & 31u) * 255u / 31u)};
            assert(fwrite(rgb, 1u, 3u, file) == 3u);
        }
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv) {
    test_framing();
    test_wire();
    test_probe();
    test_view(argc > 1 ? argv[1] : 0);
    puts("wifi_bringup: wire ordering, turnaround, padding, cold reset, timeouts, faults and UI PASS");
    return 0;
}
