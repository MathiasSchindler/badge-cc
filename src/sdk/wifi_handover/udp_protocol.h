#ifndef WIFI_HANDOVER_UDP_PROTOCOL_H
#define WIFI_HANDOVER_UDP_PROTOCOL_H

#include <stddef.h>
#include "experiment.h"

#define UDP_PROTOCOL_PORT 4242u
#define UDP_PROTOCOL_SIZE 44u
#define UDP_PEER_STALE_MS 3000u
#define UDP_HANDSHAKE_MS 5000u
#define UDP_BENCH_TIMEOUT_US 1000000u
#define UDP_BENCH_GAP_US 20000u

/*
 * Fixed 44-byte, network-byte-order datagram (no native structs on the wire):
 *  0:u16 magic 0x5748, 2:u8 version 1, 3:u8 type, 4:u32 session,
 *  8:u64 endpoint token, 16:u32 sequence, 20:u32 connection epoch,
 * 24:u32 nonce/run generation, 28:u32 original timestamp (us),
 * 32:u8 sender, 33:u8 target, 34:u8 origin, 35:u8 online mask,
 * 36:4*u16 normalized positions, slots bottom/right/top/left.
 *
 * HELLO: client -> 0, epoch=0, run=request nonce.
 * WELCOME: 0 -> client, origin=client, run=echoed nonce, epoch=challenge.
 * INPUT: client -> 0, origin=client, only positions[sender] populated.
 * STATE: 0 -> client, origin=0, all four positions + online mask.
 * PING: sender=origin, target=echo endpoint; PONG: target=origin.
 * BENCH_ERROR: target=origin, sent by failed echo endpoint or host relay.
 * Unused fields must be zero. PING/PONG sequence is 1..1000.
 * Client -> host uses sender's token/epoch; host -> client uses recipient's.
 * Relay preserves sender, target, origin, sequence, run and timestamp.
 * Tokens are session admission checks, NOT cryptographic authentication.
 */
typedef enum {
    UDP_HELLO = 1, UDP_WELCOME, UDP_INPUT, UDP_STATE, UDP_PING, UDP_PONG,
    UDP_BENCH_ERROR
} udp_message_type_t;

typedef struct {
    uint32_t session;
    uint64_t token;
    uint32_t sequence;
    uint32_t epoch;
    uint32_t run;
    uint32_t stamp_us;
    uint8_t type;
    uint8_t sender;
    uint8_t target;
    uint8_t origin;
    uint8_t online_mask;
    uint16_t positions[EXP_MAX_PLAYERS];
} udp_message_t;

bool udp_protocol_encode(uint8_t out[UDP_PROTOCOL_SIZE], const udp_message_t *message);
bool udp_protocol_decode(udp_message_t *out, const uint8_t *data, size_t size);
uint32_t udp_protocol_ip(uint8_t slot); /* Host-order 192.168.4.(slot+1). */
bool udp_sequence_newer(uint32_t sequence, uint32_t previous);
bool udp_protocol_endpoint(const udp_message_t *message, uint32_t session,
                           uint8_t local_slot, const uint64_t tokens[EXP_MAX_PLAYERS],
                           uint32_t source_ip, uint16_t source_port);
bool udp_protocol_relay(udp_message_t *message, uint64_t token, uint32_t epoch);

typedef struct {
    uint32_t epoch;
    uint32_t nonce;
    uint32_t pending_epoch;
    uint32_t pending_nonce;
    uint32_t pending_ms;
    uint32_t sequence;
    uint32_t last_seen_ms;
    bool sequence_valid;
    bool online;
} udp_peer_t;

/* HELLO retransmissions reuse the challenge; only a confirming INPUT commits it. */
uint32_t udp_peer_hello(udp_peer_t *peer, uint32_t nonce, uint32_t challenge,
                        uint32_t now_ms);
bool udp_peer_input(udp_peer_t *peer, uint32_t epoch, uint32_t sequence,
                    uint32_t now_ms);
bool udp_peer_expire(udp_peer_t *peer, uint32_t now_ms);

typedef struct {
    uint32_t generation;
    uint32_t sequence;
    uint32_t stamp_us;
    uint32_t started_ms;
    uint8_t target;
    uint8_t origin;
    bool valid;
    bool outstanding;
} udp_relay_t;

bool udp_relay_ping(udp_relay_t *route, const udp_message_t *message, uint32_t now_ms);
bool udp_relay_reply(udp_relay_t *route, const udp_message_t *message, uint32_t now_ms);

typedef struct {
    experiment_metrics_t metrics;
    uint32_t samples[EXP_BENCH_SAMPLES];
    uint32_t generation;
    uint32_t pending_sequence;
    uint32_t pending_us;
    uint32_t next_us;
    bool outstanding;
} udp_benchmark_t;

/* Percentiles use nearest rank and are computed only on completion/cancel/error.
 * Cancel/local error does not invent radio loss for the outstanding sample.
 * local_errors includes explicitly reported remote echo/relay send failures.
 */
void udp_benchmark_start(udp_benchmark_t *bench, uint8_t target,
                         uint32_t generation, uint32_t now_us);
void udp_benchmark_tick(udp_benchmark_t *bench, uint32_t now_us);
bool udp_benchmark_due(const udp_benchmark_t *bench, uint32_t now_us);
/* Call sent ONLY after lwIP accepted the datagram. */
void udp_benchmark_sent(udp_benchmark_t *bench, uint32_t stamp_us);
bool udp_benchmark_echo(udp_benchmark_t *bench, const udp_message_t *message,
                        uint8_t local_slot, uint32_t now_us);
bool udp_benchmark_remote_error(udp_benchmark_t *bench, const udp_message_t *message,
                                uint8_t local_slot, uint32_t now_us);
void udp_benchmark_local_error(udp_benchmark_t *bench);
void udp_benchmark_cancel(udp_benchmark_t *bench);

#endif
