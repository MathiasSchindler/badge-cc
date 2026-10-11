#include "wifi_probe.h"

static void report_stage(wifi_probe_report_t *report, wifi_probe_stage_t stage,
                          wifi_probe_progress_t progress, void *context) {
    report->stage = stage;
    if (progress != 0) progress(context, report);
}

wifi_bus_result_t wifi_probe_run(const wifi_wire_t *wire, wifi_probe_report_t *report,
                                 wifi_probe_progress_t progress, void *context) {
    if (wire == 0 || report == 0 || wire->write == 0 || wire->data_output == 0 ||
        wire->read_data == 0 || wire->now_us == 0) return WIFI_BUS_BAD_ARGUMENT;
    report->result = WIFI_BUS_OK;
    report->reset_pattern = 0u;
    report->normal_pattern = 0u;
    report->bus_control = 0u;
    report->chip_id = 0u;
    report->last_value = 0u;
    report->elapsed_us = 0u;
    report->passed_reads = 0u;
    report->reset_attempts = 0u;
    report->clock_csr = 0u;
    report->powered = 0u;
    uint32_t started = wire->now_us(wire->context);
    wifi_bus_result_t result;
    uint32_t value = 0u;

    wifi_wire_power_off(wire);
    report_stage(report, WIFI_PROBE_RESET, progress, context);
    result = wifi_wire_delay(wire, 20000u);
    if (result != WIFI_BUS_OK) goto failed;
    /* Match the controller's reference startup before releasing shared D/IRQ. */
    wire->write(wire->context, WIFI_SIGNAL_DATA, 0u);
    wire->data_output(wire->context, 1u);
    wire->write(wire->context, WIFI_SIGNAL_ENABLE, 1u);
    report->powered = 1u;
    result = wifi_wire_delay(wire, 250000u);
    if (result != WIFI_BUS_OK) goto failed;
    wire->data_output(wire->context, 0u);

    report_stage(report, WIFI_PROBE_TEST16, progress, context);
    for (uint32_t attempt = 0u; attempt < 10u; ++attempt) {
        report->reset_attempts = (uint8_t)(attempt + 1u);
        result = wifi_gspi_read(wire, 0u, 0x14u, 4u, WIFI_ORDER_RESET16, &value);
        report->reset_pattern = value;
        report->last_value = value;
        if (result != WIFI_BUS_OK) goto failed;
        if (value == WIFI_GSPI_TEST_PATTERN) break;
        result = wifi_wire_delay(wire, 1000u);
        if (result != WIFI_BUS_OK) goto failed;
    }
    if (value != WIFI_GSPI_TEST_PATTERN) {
        result = WIFI_BUS_TEST_MISMATCH;
        goto failed;
    }
    report_stage(report, WIFI_PROBE_CONFIG32, progress, context);
    result = wifi_gspi_write(wire, 0u, 0u, 4u, WIFI_ORDER_RESET16, WIFI_GSPI_BUS_CONFIG);
    if (result != WIFI_BUS_OK) goto failed;
    result = wifi_gspi_read(wire, 0u, 0u, 4u, WIFI_ORDER_NORMAL32, &value);
    report->bus_control = value;
    report->last_value = value;
    if (result != WIFI_BUS_OK) goto failed;
    if ((value & WIFI_GSPI_CONFIG_MASK) != (WIFI_GSPI_BUS_CONFIG & WIFI_GSPI_CONFIG_MASK)) {
        result = WIFI_BUS_CONFIG_MISMATCH;
        goto failed;
    }

    report_stage(report, WIFI_PROBE_TEST32, progress, context);
    for (uint32_t i = 0u; i < 16u; ++i) {
        result = wifi_gspi_read(wire, 0u, 0x14u, 4u, WIFI_ORDER_NORMAL32, &value);
        report->normal_pattern = value;
        report->last_value = value;
        if (result != WIFI_BUS_OK) goto failed;
        if (value != WIFI_GSPI_TEST_PATTERN) {
            result = WIFI_BUS_TEST_MISMATCH;
            goto failed;
        }
        ++report->passed_reads;
    }

    report_stage(report, WIFI_PROBE_PADDING, progress, context);
    result = wifi_gspi_write(wire, 0u, 0x1du, 1u, WIFI_ORDER_NORMAL32, WIFI_GSPI_BACKPLANE_PAD);
    if (result != WIFI_BUS_OK) goto failed;
    result = wifi_gspi_read(wire, 0u, 0x1du, 1u, WIFI_ORDER_NORMAL32, &value);
    report->last_value = value;
    if (result != WIFI_BUS_OK) goto failed;
    if (value != WIFI_GSPI_BACKPLANE_PAD) {
        result = WIFI_BUS_CONFIG_MISMATCH;
        goto failed;
    }

    report_stage(report, WIFI_PROBE_CLOCK, progress, context);
    result = wifi_gspi_write(wire, 1u, 0x1000eu, 1u, WIFI_ORDER_NORMAL32, 0x08u);
    if (result != WIFI_BUS_OK) goto failed;
    uint32_t clock_started = wire->now_us(wire->context);
    for (uint32_t attempt = 0u; attempt < 100u; ++attempt) {
        result = wifi_gspi_read(wire, 1u, 0x1000eu, 1u, WIFI_ORDER_NORMAL32, &value);
        report->clock_csr = (uint8_t)value;
        report->last_value = value;
        if (result != WIFI_BUS_OK) goto failed;
        if (value != 0xffu && (value & 0x40u) != 0u) break;
        if (wire->now_us(wire->context) - clock_started >= 100000u) break;
        result = wifi_wire_delay(wire, 1000u);
        if (result != WIFI_BUS_OK) goto failed;
    }
    if (value == 0xffu || (value & 0x40u) == 0u) {
        result = WIFI_BUS_CLOCK_TIMEOUT;
        goto failed;
    }

    report_stage(report, WIFI_PROBE_BACKPLANE, progress, context);
    const uint32_t registers[] = {0x1000cu, 0x1000bu, 0x1000au};
    const uint8_t window[] = {0x18u, 0u, 0u};
    for (uint32_t i = 0u; i < 3u; ++i) {
        result = wifi_gspi_write(wire, 1u, registers[i], 1u, WIFI_ORDER_NORMAL32, window[i]);
        if (result != WIFI_BUS_OK) goto failed;
        result = wifi_gspi_read(wire, 1u, registers[i], 1u, WIFI_ORDER_NORMAL32, &value);
        report->last_value = value;
        if (result != WIFI_BUS_OK) goto failed;
        if (value != window[i]) {
            result = WIFI_BUS_CONFIG_MISMATCH;
            goto failed;
        }
    }
    for (uint32_t i = 0u; i < 2u; ++i) {
        result = wifi_gspi_read(wire, 1u, 0x8000u, 4u, WIFI_ORDER_NORMAL32, &value);
        report->last_value = value;
        if (i == 0u) report->chip_id = value;
        if (result != WIFI_BUS_OK) goto failed;
        if (value == 0u || value == 0xffffffffu || value != report->chip_id) {
            result = WIFI_BUS_BACKPLANE_INVALID;
            goto failed;
        }
    }
    result = wifi_gspi_write(wire, 1u, 0x1000eu, 1u, WIFI_ORDER_NORMAL32, 0u);
    if (result != WIFI_BUS_OK) goto failed;
    wifi_wire_idle(wire);
    report->elapsed_us = wire->now_us(wire->context) - started;
    report_stage(report, WIFI_PROBE_FINISHED, progress, context);
    return WIFI_BUS_OK;

failed:
    wifi_wire_power_off(wire);
    report->powered = 0u;
    report->result = result;
    report->elapsed_us = wire->now_us(wire->context) - started;
    if (progress != 0) progress(context, report);
    return result;
}

const char *wifi_probe_stage_name(wifi_probe_stage_t stage) {
    switch (stage) {
        case WIFI_PROBE_IDLE: return "WAITING FOR START";
        case WIFI_PROBE_RESET: return "RADIO COLD RESET";
        case WIFI_PROBE_TEST16: return "RESET ORDER TEST";
        case WIFI_PROBE_CONFIG32: return "SWITCH TO 32 BIT";
        case WIFI_PROBE_TEST32: return "REPEATED BUS READS";
        case WIFI_PROBE_PADDING: return "BACKPLANE DELAY";
        case WIFI_PROBE_CLOCK: return "WAIT FOR ALP CLOCK";
        case WIFI_PROBE_BACKPLANE: return "CHIPCOMMON ID READ";
        case WIFI_PROBE_FINISHED: return "REGISTER PROBE DONE";
    }
    return "UNKNOWN STAGE";
}
