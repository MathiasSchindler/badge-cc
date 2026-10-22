#include "receiver.h"
#include "hardware.h"

#define IRQ_RX_DONE 0x0002u
#define IRQ_HEADER_VALID 0x0010u
#define IRQ_HEADER_ERROR 0x0020u
#define IRQ_CRC_ERROR 0x0040u
#define IRQ_TIMEOUT 0x0200u
#define IRQ_MASK (IRQ_RX_DONE | IRQ_HEADER_VALID | IRQ_HEADER_ERROR | \
                  IRQ_CRC_ERROR | IRQ_TIMEOUT)
#define MODE_STANDBY_RC 2u
#define MODE_RX 5u

static int32_t command(sx1262_receiver_t *rx, const uint8_t *out, uint8_t *in,
                       uint32_t length) {
    rx->last_command = out[0];
    rx->last_response = 0u;
    int32_t error = sx1262_transfer(out, in, length, 0, 0u, rx->service);
    if (error == 0) {
        for (uint32_t i = 0u; i < length && i < 4u; ++i)
            rx->last_response |= (uint32_t)in[i] << (8u * i);
    }
    return error;
}

static uint32_t chip_mode(uint8_t status) {
    return (uint32_t)(status >> 4u) & 7u;
}

static int command_ok(uint8_t status) {
    uint32_t result = (uint32_t)(status >> 1u) & 7u;
    return result != 3u && result != 4u &&
           result != 5u && result != 7u;
}

void sx1262_rx_meshtastic_config(sx1262_rx_config_t *config) {
    config->frequency_hz = SX1262_RX_MESHTASTIC_FREQUENCY_HZ;
    config->spreading_factor = SX1262_RX_MESHTASTIC_SF;
    config->bandwidth_khz = SX1262_RX_MESHTASTIC_BANDWIDTH_KHZ;
    config->profile = SX1262_RX_PROFILE_MESHTASTIC;
}

void sx1262_rx_lorawan_config(sx1262_rx_config_t *config) {
    config->frequency_hz = SX1262_RX_LORAWAN_FREQUENCY_HZ;
    config->spreading_factor = SX1262_RX_LORAWAN_SF;
    config->bandwidth_khz = SX1262_RX_LORAWAN_BANDWIDTH_KHZ;
    config->profile = SX1262_RX_PROFILE_LORAWAN;
}

void sx1262_rx_default_config(sx1262_rx_config_t *config) {
    sx1262_rx_meshtastic_config(config);
}

int32_t sx1262_rx_validate(const sx1262_rx_config_t *config) {
    if (config == 0) return SX1262_RX_CONFIG_ERROR;
    if (config->profile > SX1262_RX_PROFILE_MESHTASTIC) return SX1262_RX_CONFIG_ERROR;
    if (config->frequency_hz < SX1262_RX_MIN_FREQUENCY_HZ ||
        config->frequency_hz > SX1262_RX_MAX_FREQUENCY_HZ)
        return SX1262_RX_CONFIG_ERROR;
    if (config->spreading_factor < 7u || config->spreading_factor > 12u)
        return SX1262_RX_CONFIG_ERROR;
    if (config->bandwidth_khz != 125u && config->bandwidth_khz != 250u &&
        config->bandwidth_khz != 500u)
        return SX1262_RX_CONFIG_ERROR;
    return 0;
}

int32_t sx1262_receiver_confirm_board(sx1262_receiver_t *rx, uint32_t board) {
    rx->board_confirmed = board == SX1262_RX_BOARD_WAVESHARE_PICO_LORA_SX1262_868M;
    return rx->board_confirmed ? 0 : SX1262_RX_BOARD_ERROR;
}

void sx1262_receiver_init(sx1262_receiver_t *rx, sx1262_service_t service) {
    rx->service = service;
    rx->state = SX1262_RX_IDLE;
    rx->last_error = 0;
    rx->device_errors = 0u;
    rx->last_status = 0u;
    rx->last_command = 0u;
    rx->last_response = 0u;
    rx->active_valid = 0u;
    rx->board_confirmed = 0u;
    rx->last_irq = 0u;
    sx1262_rx_default_config(&rx->active);
    rx->counters.packets = rx->counters.crc_errors = 0u;
    rx->counters.header_errors = rx->counters.headers_valid = 0u;
    rx->counters.timeouts = rx->counters.overwritten = 0u;
    rx->counters.rearms = rx->counters.polls = rx->counters.poll_errors = 0u;
    rx->counters.configs = rx->counters.config_errors = 0u;
    rx->next_sequence = 0u;
    rx->head = 0u;
    rx->count = 0u;
}

/* Fstep = 32 MHz / 2^25, so register = f * 16384 / 15625 without overflow. */
static uint32_t frequency_register(uint32_t hz) {
    uint32_t quotient = hz / 15625u;
    uint32_t remainder = hz % 15625u;
    return quotient * 16384u + remainder * 16384u / 15625u;
}

/* Packet type, image calibration, RF and LoRa parameters, then continuous RX. */
static int32_t apply_profile(sx1262_receiver_t *rx, const sx1262_rx_config_t *config) {
    static const uint8_t standby[] = {0x80u, 0x00u};
    static const uint8_t packet_type[] = {0x8au, 0x01u};
    static const uint8_t calibrate_image[] = {0x98u, 0xd7u, 0xdbu};      /* 863-870 MHz */
    static const uint8_t read_iq[] = {0x1du, 0x07u, 0x36u, 0x00u, 0x00u};
    /* Register 0x08ac = 0x96: RX boosted gain (+~2 dB sensitivity), as Meshtastic uses. */
    static const uint8_t rx_boosted_gain[] = {0x0du, 0x08u, 0xacu, 0x96u};
    static const uint8_t buffer_base[] = {0x8fu, 0x00u, 0x00u};
    static const uint8_t irq_params[] = {0x08u, (uint8_t)(IRQ_MASK >> 8u), (uint8_t)IRQ_MASK,
                                         (uint8_t)(IRQ_MASK >> 8u), (uint8_t)IRQ_MASK,
                                         0x00u, 0x00u, 0x00u, 0x00u};
    static const uint8_t clear_irq[] = {0x02u, 0xffu, 0xffu};
    static const uint8_t get_errors[] = {0x17u, 0x00u, 0x00u, 0x00u};
    static const uint8_t set_rx[] = {0x82u, 0xffu, 0xffu, 0xffu};       /* continuous */
    static const uint8_t get_status[] = {0xc0u, 0x00u};
    uint8_t in[9];
    uint8_t out[7];
    uint8_t meshtastic = config->profile == SX1262_RX_PROFILE_MESHTASTIC;
    int32_t error = command(rx, standby, in, sizeof(standby));
    if (error == 0) error = command(rx, packet_type, in, sizeof(packet_type));
    if (error == 0) error = command(rx, calibrate_image, in, sizeof(calibrate_image));
    if (error != 0) return error;

    uint32_t frequency = frequency_register(config->frequency_hz);
    out[0] = 0x86u;
    out[1] = (uint8_t)(frequency >> 24u);
    out[2] = (uint8_t)(frequency >> 16u);
    out[3] = (uint8_t)(frequency >> 8u);
    out[4] = (uint8_t)frequency;
    error = command(rx, out, in, 5u);
    if (error != 0) return error;

    uint32_t sf = config->spreading_factor;
    uint32_t bw = config->bandwidth_khz;
    out[0] = 0x8bu;
    out[1] = (uint8_t)sf;
    out[2] = bw == 125u ? 0x04u : bw == 250u ? 0x05u : 0x06u;
    out[3] = 0x01u; /* CR 4/5; receivers decode the header's coding rate. */
    /* Low data-rate optimization where the symbol time is >= 16.38 ms. */
    out[4] = (uint8_t)((bw == 125u && sf >= 11u) || (bw == 250u && sf == 12u));
    error = command(rx, out, in, 5u);
    if (error != 0) return error;
    /* Preamble 8 (LoRaWAN) or 16 (Meshtastic), explicit header, max length 255,
       CRC on, standard IQ. */
    out[0] = 0x8cu;
    out[1] = 0x00u;
    out[2] = meshtastic ? 0x10u : 0x08u;
    out[3] = 0x00u;
    out[4] = 0xffu;
    out[5] = 0x01u;
    out[6] = 0x00u;
    error = command(rx, out, in, 7u);
    if (error == 0) error = command(rx, read_iq, in, sizeof(read_iq));
    if (error != 0) return error;
    /* SX1262 errata 15.4: set bit 2 of register 0x0736 for standard IQ. */
    out[0] = 0x0du;
    out[1] = 0x07u;
    out[2] = 0x36u;
    out[3] = (uint8_t)(in[4] | 0x04u);
    error = command(rx, out, in, 4u);
    if (error != 0) return error;
    /* Register 0x0740: one-byte LoRa sync word nibble-expanded with control
       bits 0x44 (RadioLib layout): 0x34 public LoRaWAN, 0x2b Meshtastic. */
    out[0] = 0x0du;
    out[1] = 0x07u;
    out[2] = 0x40u;
    out[3] = meshtastic ? 0x24u : 0x34u;
    out[4] = meshtastic ? 0xb4u : 0x44u;
    error = command(rx, out, in, 5u);
    if (error == 0) error = command(rx, rx_boosted_gain, in, sizeof(rx_boosted_gain));
    if (error == 0) error = command(rx, buffer_base, in, sizeof(buffer_base));
    if (error == 0) error = command(rx, irq_params, in, sizeof(irq_params));
    if (error == 0) error = command(rx, clear_irq, in, sizeof(clear_irq));
    if (error == 0) error = command(rx, get_errors, in, sizeof(get_errors));
    if (error != 0) return error;
    rx->last_status = in[1];
    if (!command_ok(in[1])) return SX1262_RESPONSE_ERROR;
    uint32_t device_errors = ((uint32_t)in[2] << 8u) | in[3];
    if (device_errors != 0u) {
        rx->device_errors = device_errors;
        return SX1262_DEVICE_ERROR;
    }
    error = command(rx, set_rx, in, sizeof(set_rx));
    if (error == 0) error = command(rx, get_status, in, sizeof(get_status));
    if (error != 0) return error;
    rx->last_status = in[1];
    if (chip_mode(in[1]) != MODE_RX || !command_ok(in[1])) return SX1262_RESPONSE_ERROR;
    return 0;
}

/* Hardware reset and board setup from the Waveshare reference: TCXO on DIO3
   at 1.7 V with 5 ms start-up, DIO2 drives the PE4259 RF switch, whose
   supply may come from GP22 (ANT_SW option B). */
static int32_t start_radio(sx1262_receiver_t *rx, const sx1262_rx_config_t *config) {
    static const uint8_t get_status[] = {0xc0u, 0x00u};
    static const uint8_t get_errors[] = {0x17u, 0x00u, 0x00u, 0x00u};
    static const uint8_t standby[] = {0x80u, 0x00u};
    static const uint8_t tcxo[] = {0x97u, 0x01u, 0x00u, 0x01u, 0x40u}; /* 1.7 V, 320 x 15.625 us */
    static const uint8_t clear_errors[] = {0x07u, 0x00u, 0x00u};
    static const uint8_t calibrate[] = {0x89u, 0x7fu};
    static const uint8_t rf_switch[] = {0x9du, 0x01u};
    uint8_t in[5];
    int32_t error = sx1262_hw_init();
    if (error != 0) return error;
    sx1262_hw_antenna_power();
    sx1262_hw_reset(1u);
    error = sx1262_wait(rx->service, 20000u);
    sx1262_hw_reset(0u);
    if (error == 0) error = sx1262_wait(rx->service, 10000u);
    if (error == 0) error = command(rx, get_status, in, sizeof(get_status));
    if (error != 0) return error;
    rx->last_status = in[1];
    uint8_t startup_status = in[1];
    if (chip_mode(startup_status) != MODE_STANDBY_RC ||
        (!command_ok(startup_status) && (startup_status & 0x7eu) != 0x2au))
        return SX1262_RESPONSE_ERROR;
    error = command(rx, get_errors, in, sizeof(get_errors));
    if (error != 0) return error;
    if (chip_mode(in[1]) != MODE_STANDBY_RC ||
        (!command_ok(in[1]) && (in[1] & 0x7eu) != 0x2au)) return SX1262_RESPONSE_ERROR;
    rx->device_errors = ((uint32_t)in[2] << 8u) | in[3];
    /* Reset calibration precedes DIO3 TCXO power. Only XOSC_START_ERR is expected. */
    if ((rx->device_errors & ~0x20u) != 0u) return SX1262_DEVICE_ERROR;
    if (!command_ok(startup_status) && rx->device_errors != 0x20u)
        return SX1262_RESPONSE_ERROR;
    error = command(rx, standby, in, sizeof(standby));
    if (error == 0) error = command(rx, tcxo, in, sizeof(tcxo));
    /* Clears the expected power-on XOSC start error before calibrating on the TCXO. */
    if (error == 0) error = command(rx, clear_errors, in, sizeof(clear_errors));
    if (error == 0) rx->device_errors = 0u;
    if (error == 0) error = command(rx, calibrate, in, sizeof(calibrate));
    if (error == 0) error = command(rx, rf_switch, in, sizeof(rf_switch));
    if (error == 0) error = apply_profile(rx, config);
    return error;
}

static void fault(sx1262_receiver_t *rx, int32_t error) {
    rx->last_error = error;
    sx1262_hw_release();
    rx->state = SX1262_RX_FAULT;
}

static void commit(sx1262_receiver_t *rx, const sx1262_rx_config_t *config) {
    rx->active.frequency_hz = config->frequency_hz;
    rx->active.spreading_factor = config->spreading_factor;
    rx->active.bandwidth_khz = config->bandwidth_khz;
    rx->active.profile = config->profile;
    rx->active_valid = 1u;
    rx->noise_qdb = rx->noise_min_qdb = rx->noise_max_qdb = 0;
    rx->noise_samples = 0u;
    rx->state = SX1262_RX_RUNNING;
    rx->last_error = 0;
    ++rx->counters.configs;
}

int32_t sx1262_receiver_configure(sx1262_receiver_t *rx, const sx1262_rx_config_t *config) {
    int32_t error = rx->board_confirmed ? sx1262_rx_validate(config) : SX1262_RX_BOARD_ERROR;
    if (error != 0) {
        ++rx->counters.config_errors;
        rx->last_error = error;
        return error;
    }
    sx1262_rx_config_t next;
    next.frequency_hz = config->frequency_hz;
    next.spreading_factor = config->spreading_factor;
    next.bandwidth_khz = config->bandwidth_khz;
    next.profile = config->profile;
    if (rx->state == SX1262_RX_RUNNING) {
        error = apply_profile(rx, &next);
        if (error == 0) {
            commit(rx, &next);
            return 0;
        }
        ++rx->counters.config_errors;
        rx->last_error = error;
        if (rx->active_valid && apply_profile(rx, &rx->active) == 0) return error;
        fault(rx, error);
        return error;
    }
    error = start_radio(rx, &next);
    if (error != 0) {
        ++rx->counters.config_errors;
        fault(rx, error);
        return error;
    }
    commit(rx, &next);
    return 0;
}

void sx1262_rx_parse(sx1262_rx_packet_t *packet) {
    packet->flags = 0u;
    packet->mhdr = packet->mtype = packet->major = 0u;
    packet->fctrl = packet->fopts_length = 0u;
    packet->fcnt16 = 0u;
    packet->dev_addr = 0u;
    if (packet->length == 0u) return;
    if (packet->profile == SX1262_RX_PROFILE_MESHTASTIC) {
        /* Meshtastic: 16-byte little-endian header, never LoRaWAN. */
        if (packet->length >= 16u) packet->flags = SX1262_RX_MESHTASTIC_HEADER;
        return;
    }
    const uint8_t *p = packet->payload;
    packet->mhdr = p[0];
    packet->mtype = (uint8_t)(p[0] >> 5u);
    packet->major = (uint8_t)(p[0] & 3u);
    packet->flags = SX1262_RX_HAS_MHDR;
    /* Data frames only: MHDR(1) + DevAddr(4) + FCtrl(1) + FCnt(2) + FOpts + MIC(4). */
    if (packet->mtype < 2u || packet->mtype > 5u || packet->major != 0u ||
        (p[0] & 0x1cu) != 0u || packet->length < 12u)
        return;
    uint32_t fopts = p[5] & 0x0fu;
    if (12u + fopts > packet->length) return;
    packet->dev_addr = (uint32_t)p[1] | ((uint32_t)p[2] << 8u) |
                       ((uint32_t)p[3] << 16u) | ((uint32_t)p[4] << 24u);
    packet->fctrl = p[5];
    packet->fopts_length = (uint8_t)fopts;
    packet->fcnt16 = (uint16_t)(p[6] | (p[7] << 8u));
    packet->flags |= SX1262_RX_LORAWAN_DATA;
}

static int32_t read_packet(sx1262_receiver_t *rx, uint32_t now_ms, uint8_t crc_error) {
    static const uint8_t buffer_status[] = {0x13u, 0x00u, 0x00u, 0x00u};
    static const uint8_t packet_status[] = {0x14u, 0x00u, 0x00u, 0x00u, 0x00u};
    uint8_t in[5];
    uint8_t out[3];
    int32_t error = command(rx, buffer_status, in, sizeof(buffer_status));
    if (error != 0) return error;
    uint32_t length = in[2];
    out[0] = 0x1eu;
    out[1] = in[3];
    out[2] = 0x00u;
    error = command(rx, packet_status, in, sizeof(packet_status));
    if (error != 0) return error;
    uint32_t rssi = in[2], snr = in[3], signal = in[4];
    /* Staged so a failed read never corrupts a visible ring entry. */
    error = sx1262_transfer(out, in, 3u, rx->scratch, length, rx->service);
    if (error != 0) return error;

    sx1262_rx_packet_t *packet = &rx->ring[rx->head];
    if (++rx->next_sequence == 0u) rx->next_sequence = 1u;
    packet->sequence = rx->next_sequence;
    packet->timestamp_ms = now_ms;
    packet->frequency_hz = rx->active.frequency_hz;
    packet->spreading_factor = (uint8_t)rx->active.spreading_factor;
    packet->bandwidth_khz = (uint16_t)rx->active.bandwidth_khz;
    packet->rssi_qdb = -(int32_t)rssi * 2;
    packet->snr_qdb = snr >= 128u ? (int32_t)snr - 256 : (int32_t)snr;
    packet->signal_rssi_qdb = -(int32_t)signal * 2;
    packet->length = (uint8_t)length;
    packet->profile = (uint8_t)rx->active.profile;
    for (uint32_t i = 0u; i < length; ++i) packet->payload[i] = rx->scratch[i];
    if (crc_error) {
        /* Clear parsed fields by parsing an empty frame, then mark it. */
        packet->length = 0u;
        sx1262_rx_parse(packet);
        packet->length = (uint8_t)length;
        packet->flags = SX1262_RX_CRC_ERROR;
    } else {
        sx1262_rx_parse(packet);
    }
    rx->head = (rx->head + 1u) % SX1262_RX_RING_SIZE;
    if (rx->count < SX1262_RX_RING_SIZE) ++rx->count;
    else ++rx->counters.overwritten;
    if (!crc_error) ++rx->counters.packets;
    return 0;
}

static int32_t poll_once(sx1262_receiver_t *rx, uint32_t now_ms) {
    static const uint8_t get_irq[] = {0x12u, 0x00u, 0x00u, 0x00u};
    uint8_t in[4];
    int32_t error = command(rx, get_irq, in, sizeof(get_irq));
    if (error != 0) return error;
    uint8_t status = in[1];
    uint32_t irq = ((uint32_t)in[2] << 8u) | in[3];
    rx->last_status = status;
    rx->last_irq = (uint16_t)irq;
    uint32_t mode = chip_mode(status);
    if (status == 0xffu || mode == 0u) {
        /* Floating or stuck MISO: do not interpret IRQ bits. */
        irq = 0u;
    }
    if (irq & IRQ_HEADER_VALID) ++rx->counters.headers_valid;
    if (irq & IRQ_HEADER_ERROR) ++rx->counters.header_errors;
    if (irq & IRQ_CRC_ERROR) ++rx->counters.crc_errors;
    if (irq & IRQ_RX_DONE) {
        error = read_packet(rx, now_ms, (irq & IRQ_CRC_ERROR) != 0u);
        if (error != 0) return error;
    }
    if (irq & IRQ_TIMEOUT) ++rx->counters.timeouts;
    if (irq != 0u) {
        uint8_t clear[3];
        clear[0] = 0x02u;
        clear[1] = (uint8_t)(irq >> 8u);
        clear[2] = (uint8_t)irq;
        error = command(rx, clear, in, sizeof(clear));
        if (error != 0) return error;
    }
    if (mode != MODE_RX || (irq & IRQ_TIMEOUT) != 0u) {
        error = apply_profile(rx, &rx->active);
        if (error != 0) return error;
        ++rx->counters.rearms;
        return 0;
    }
    if (irq == 0u && (rx->noise_samples == 0u ||
                      now_ms - rx->noise_at_ms >= SX1262_RX_NOISE_INTERVAL_MS)) {
        int32_t qdb;
        error = sx1262_receiver_rssi(rx, &qdb);
        if (error != 0) return error;
        rx->noise_qdb = qdb;
        if (rx->noise_samples == 0u || qdb < rx->noise_min_qdb) rx->noise_min_qdb = qdb;
        if (rx->noise_samples == 0u || qdb > rx->noise_max_qdb) rx->noise_max_qdb = qdb;
        ++rx->noise_samples;
        rx->noise_at_ms = now_ms;
    }
    return 0;
}

static int radio_fault(int32_t error) {
    return error <= SX1262_INIT_ERROR && error >= SX1262_DEVICE_ERROR;
}

int32_t sx1262_receiver_rssi(sx1262_receiver_t *rx, int32_t *qdb) {
    static const uint8_t get_rssi_inst[] = {0x15u, 0x00u, 0x00u};
    uint8_t in[3];
    if (rx->state != SX1262_RX_RUNNING) return SX1262_RX_STATE_ERROR;
    int32_t error = command(rx, get_rssi_inst, in, sizeof(get_rssi_inst));
    if (error != 0) {
        rx->last_error = error;
        if (radio_fault(error) && rx->state != SX1262_RX_FAULT) fault(rx, error);
        return error;
    }
    *qdb = -2 * (int32_t)in[2];
    return 0;
}

int32_t sx1262_receiver_poll(sx1262_receiver_t *rx, uint32_t now_ms) {
    if (rx->state != SX1262_RX_RUNNING) return SX1262_RX_STATE_ERROR;
    ++rx->counters.polls;
    int32_t error = poll_once(rx, now_ms);
    if (error != 0) {
        ++rx->counters.poll_errors;
        rx->last_error = error;
        /* Service cancellations keep the radio listening; radio faults release it. */
        if (radio_fault(error) && rx->state != SX1262_RX_FAULT) fault(rx, error);
    }
    return error;
}

static int32_t survey_once(sx1262_receiver_t *rx, uint32_t frequency_hz,
                           sx1262_rx_survey_t *result) {
    static const uint8_t standby[] = {0x80u, 0x00u};
    static const uint8_t set_rx[] = {0x82u, 0xffu, 0xffu, 0xffu};
    static const uint8_t get_status[] = {0xc0u, 0x00u};
    static const uint8_t get_rssi_inst[] = {0x15u, 0x00u, 0x00u};
    uint8_t in[5];
    uint8_t out[5];
    int32_t error = command(rx, standby, in, sizeof(standby));
    if (error != 0) return error;
    /* CalibrateImage takes 4 MHz steps; cover at least +-4 MHz. */
    uint32_t mhz = frequency_hz / 1000000u;
    out[0] = 0x98u;
    out[1] = (uint8_t)((mhz - 4u) / 4u);
    out[2] = (uint8_t)((mhz + 7u) / 4u);
    error = command(rx, out, in, 3u);
    if (error != 0) return error;
    uint32_t frequency = frequency_register(frequency_hz);
    out[0] = 0x86u;
    out[1] = (uint8_t)(frequency >> 24u);
    out[2] = (uint8_t)(frequency >> 16u);
    out[3] = (uint8_t)(frequency >> 8u);
    out[4] = (uint8_t)frequency;
    error = command(rx, out, in, 5u);
    if (error == 0) error = command(rx, set_rx, in, sizeof(set_rx));
    if (error == 0) error = command(rx, get_status, in, sizeof(get_status));
    if (error != 0) return error;
    rx->last_status = in[1];
    if (chip_mode(in[1]) != MODE_RX || !command_ok(in[1])) return SX1262_RESPONSE_ERROR;
    int32_t sum = 0;
    for (uint32_t i = 0u; i < SX1262_RX_SURVEY_SAMPLES; ++i) {
        error = sx1262_wait(rx->service, i == 0u ? 2000u : 500u);
        if (error == 0) error = command(rx, get_rssi_inst, in, sizeof(get_rssi_inst));
        if (error != 0) return error;
        int32_t qdb = -2 * (int32_t)in[2];
        if (i == 0u || qdb < result->min_qdb) result->min_qdb = qdb;
        if (i == 0u || qdb > result->max_qdb) result->max_qdb = qdb;
        sum += qdb;
    }
    result->samples = SX1262_RX_SURVEY_SAMPLES;
    result->mean_qdb = sum / (int32_t)SX1262_RX_SURVEY_SAMPLES;
    return 0;
}

int32_t sx1262_receiver_survey(sx1262_receiver_t *rx, uint32_t frequency_hz,
                               sx1262_rx_survey_t *result) {
    result->frequency_hz = frequency_hz;
    result->bandwidth_khz = rx->active.bandwidth_khz;
    result->samples = 0u;
    result->min_qdb = result->mean_qdb = result->max_qdb = 0;
    if (rx->state != SX1262_RX_RUNNING) return SX1262_RX_STATE_ERROR;
    if (frequency_hz < SX1262_RX_SURVEY_MIN_HZ || frequency_hz > SX1262_RX_SURVEY_MAX_HZ)
        return SX1262_RX_CONFIG_ERROR;
    int32_t error = survey_once(rx, frequency_hz, result);
    /* Always try to return to the listening profile, even after a failure. */
    int32_t restore = apply_profile(rx, &rx->active);
    if (error == 0) error = restore;
    if (error != 0) {
        result->samples = 0u;
        rx->last_error = error;
        if (radio_fault(error)) fault(rx, error);
    }
    return error;
}

void sx1262_receiver_stop(sx1262_receiver_t *rx) {
    static const uint8_t standby[] = {0x80u, 0x00u};
    uint8_t in[2];
    if (rx->state == SX1262_RX_RUNNING) {
        int32_t error = command(rx, standby, in, sizeof(standby));
        if (error != 0) rx->last_error = error;
    }
    sx1262_hw_release();
    rx->state = SX1262_RX_IDLE;
}

const sx1262_rx_packet_t *sx1262_receiver_packet(const sx1262_receiver_t *rx, uint32_t index) {
    if (index >= rx->count) return 0;
    return &rx->ring[(rx->head + SX1262_RX_RING_SIZE - 1u - index) % SX1262_RX_RING_SIZE];
}
