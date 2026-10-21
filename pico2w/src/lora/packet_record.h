#ifndef PICO2W_LORA_PACKET_RECORD_H
#define PICO2W_LORA_PACKET_RECORD_H

#include "receiver.h"

/*
 * USB vendor request 0x59 record: the oldest stored packet whose sequence is
 * after the requested one, so a host can stream every packet without gaps
 * while the ring still holds it. Little-endian; bytes 324..335 carry the
 * receiver's noise-floor statistics; CRC-32 over bytes 0..335.
 */
#define LORA_PACKET_RECORD_SIZE 340u
#define LORA_PACKET_RECORD_CRC 336u
#define LORA_PACKET_RECORD_MAGIC 0x50573250u /* "P2WP" */
#define LORA_PACKET_RECORD_PRESENT 1u

void lora_packet_record(uint8_t record[LORA_PACKET_RECORD_SIZE], uint32_t build_id,
                        const sx1262_receiver_t *rx, uint32_t after_sequence);

#endif
