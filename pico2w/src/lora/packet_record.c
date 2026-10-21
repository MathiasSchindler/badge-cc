#include "packet_record.h"
#include "usb/report.h"

void lora_packet_record(uint8_t record[LORA_PACKET_RECORD_SIZE], uint32_t build_id,
                        const sx1262_receiver_t *rx, uint32_t after_sequence) {
    for (uint32_t i = 0u; i < LORA_PACKET_RECORD_SIZE; ++i) record[i] = 0u;
    usb_store_u32(record, LORA_PACKET_RECORD_MAGIC);
    record[4] = 1u;
    record[6] = (uint8_t)LORA_PACKET_RECORD_SIZE;
    record[7] = (uint8_t)(LORA_PACKET_RECORD_SIZE >> 8u);
    usb_store_u32(record + 8u, build_id);
    const sx1262_rx_packet_t *newest = sx1262_receiver_packet(rx, 0u);
    const sx1262_rx_packet_t *oldest = rx->count ? sx1262_receiver_packet(rx, rx->count - 1u) : 0;
    usb_store_u32(record + 16u, newest ? newest->sequence : 0u);
    usb_store_u32(record + 20u, oldest ? oldest->sequence : 0u);
    usb_store_u32(record + 52u, rx->counters.packets);
    usb_store_u32(record + 56u, rx->counters.crc_errors);
    usb_store_u32(record + 60u, rx->counters.header_errors);
    const sx1262_rx_packet_t *packet = 0;
    /* Oldest first; serial-number arithmetic tolerates sequence wrap. */
    for (uint32_t i = rx->count; i > 0u && packet == 0; --i) {
        const sx1262_rx_packet_t *candidate = sx1262_receiver_packet(rx, i - 1u);
        if ((int32_t)(candidate->sequence - after_sequence) > 0) packet = candidate;
    }
    if (packet != 0) {
        usb_store_u32(record + 12u, LORA_PACKET_RECORD_PRESENT);
        usb_store_u32(record + 24u, packet->sequence);
        usb_store_u32(record + 28u, packet->timestamp_ms);
        usb_store_u32(record + 32u, packet->frequency_hz);
        record[36] = (uint8_t)packet->bandwidth_khz;
        record[37] = (uint8_t)(packet->bandwidth_khz >> 8u);
        record[38] = packet->spreading_factor;
        record[39] = packet->profile;
        usb_store_u32(record + 40u, (uint32_t)packet->rssi_qdb);
        usb_store_u32(record + 44u, (uint32_t)packet->snr_qdb);
        usb_store_u32(record + 48u, (uint32_t)packet->signal_rssi_qdb);
        record[64] = packet->length;
        record[65] = packet->flags;
        for (uint32_t i = 0u; i < packet->length; ++i) record[68u + i] = packet->payload[i];
    }
    record[324] = (uint8_t)rx->noise_qdb;
    record[325] = (uint8_t)((uint32_t)rx->noise_qdb >> 8u);
    record[326] = (uint8_t)rx->noise_min_qdb;
    record[327] = (uint8_t)((uint32_t)rx->noise_min_qdb >> 8u);
    record[328] = (uint8_t)rx->noise_max_qdb;
    record[329] = (uint8_t)((uint32_t)rx->noise_max_qdb >> 8u);
    usb_store_u32(record + 332u, rx->noise_samples);
    usb_store_u32(record + LORA_PACKET_RECORD_CRC, usb_crc32(record, LORA_PACKET_RECORD_CRC));
}
