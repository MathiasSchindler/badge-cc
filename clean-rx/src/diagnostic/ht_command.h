#ifndef CLEAN_RX_HT_COMMAND_H
#define CLEAN_RX_HT_COMMAND_H

#include "../ht20.h"
#include "../unicast.h"
#include "../block_ack.h"

static inline uint8_t clean_ht_data_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint8_t clean_ht_amsdu_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint8_t clean_ht_ack_byte(uint32_t at, const uint8_t nonce[14]);
static inline uint32_t clean_ht_data_length(const uint8_t nonce[14]);

static inline uint32_t clean_ht_reject_bad_fcs_word(uint32_t word, uint32_t index,
        uint32_t length, uint32_t ht_probe, const volatile uint8_t *packet) {
    if (CLEAN_RX_HT_REJECT_BAD_FCS && ht_probe == 2u &&
        length == CLEAN_PLCP_BYTES + CLEAN_HT_ACK_MPDU &&
        packet[0] == CLEAN_HT_CLOCKED_VERSION &&
        index == (length - 1u) / 4u)
        word ^= 0x80u << (8u * ((length - 1u) & 3u));
    return word;
}

static inline int clean_ht_ampdu_probe_mpdu(uint8_t m[80], const uint8_t nonce[14],
                                            uint32_t sequence) {
    if (nonce[13] >= 8u || sequence >= CLEAN_HT_AMPDU_PROBE_COUNT) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_MPDU; ++i) m[i] = 0u;
    m[0] = 0x88u; m[1] = 2u;
    m[4] = m[10] = m[16] = 2u;
    for (uint32_t i = 0u; i < 5u; ++i) {
        m[5u+i] = nonce[i];
        m[11u+i] = m[17u+i] = nonce[i];
    }
    /* Keep receiver and transmitter distinct even for repeated challenge bytes. */
    m[15] ^= 0x80u; m[21] ^= 0x80u;
    m[22] = (uint8_t)(sequence << 4u);
    m[24] = 0x60u;
    for (uint32_t i = 0u; i < 14u; ++i) m[26u+i] = nonce[i];
    static const uint8_t marker[8] = {'A','M','P','D','U','-','D','2'};
    for (uint32_t i = 0u; i < sizeof(marker); ++i) m[40u+i] = marker[i];
    for (uint32_t i = 48u; i < 76u; ++i) m[i] = (uint8_t)(nonce[i%14u] ^ i ^ sequence);
    uint32_t fcs = clean_mac_fcs(m,76u);
    for (uint32_t i = 0u; i < 4u; ++i) m[76u+i] = (uint8_t)(fcs >> (8u*i));
    return 1;
}

static inline int clean_ht_ampdu_probe_frame(uint8_t packet[CLEAN_HT_AMPDU_PROBE_STORAGE],
                                             const uint8_t nonce[14]) {
    if (nonce[13] >= 8u) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_STORAGE; ++i) packet[i] = 0u;
    if (!clean_ht_ampdu_probe_sig(packet+CLEAN_HT_DATA_HEADER,nonce[13])) return 0;
    packet[0] = CLEAN_HT_AMPDU_PROBE_VERSION;
    packet[2] = nonce[13];
    packet[4] = (uint8_t)CLEAN_HT_AMPDU_PROBE_BYTES;
    packet[5] = (uint8_t)(CLEAN_HT_AMPDU_PROBE_BYTES >> 8u);
    for (uint32_t i = 0u; i < CLEAN_HT_AMPDU_PROBE_COUNT; ++i) {
        uint8_t *delimiter = packet + CLEAN_HT_DATA_HEADER + 6u + i*84u;
        delimiter[0] = 0u; delimiter[1] = 5u;
        delimiter[2] = clean_ampdu_delimiter_crc(delimiter);
        delimiter[3] = 0x4eu;
        if (!clean_ht_ampdu_probe_mpdu(delimiter+4u,nonce,i)) return 0;
        if (CLEAN_RX_AMPDU_BAD_FCS & (1u << i)) delimiter[4u+79u] ^= 0x80u;
    }
    return 1;
}

static inline int clean_ht_ampdu_probe_command(uint32_t cmd,
                                               const volatile uint8_t *packet) {
    if (!CLEAN_RX_AMPDU_PROBE ||
        cmd != (CLEAN_HT_DATA_CMD | clean_tx_length_bits(CLEAN_HT_AMPDU_PROBE_TX_BYTES) |
                (1u << 19u))) return 0;
    uint8_t expected[CLEAN_HT_AMPDU_PROBE_STORAGE], nonce[14];
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = packet[44u+i];
    if (!clean_ht_ampdu_probe_frame(expected,nonce)) return 0;
    for (uint32_t i = 0u; i < sizeof(expected); ++i)
        if (packet[i] != expected[i]) return 0;
    return 1;
}

static inline int clean_ht_stimulus_sig(uint8_t sig[6], uint32_t mcs) {
    return clean_ht20_ampdu_sig(sig,CLEAN_HT_STIMULUS_PSDU,mcs);
}

static inline int clean_ht_stimulus_sig_valid(const volatile uint8_t *sig, uint32_t length) {
    uint8_t expected[6];
    if (!CLEAN_RX_HT_STIMULUS || length != CLEAN_HT_STIMULUS_PSDU ||
        !clean_ht_stimulus_sig(expected,sig[0])) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (sig[i] != expected[i]) return 0;
    return 1;
}

/* Kinds cycle through legacy OFDM, single HT and a fixed two-body HT PPDU. */
static inline uint32_t clean_ht_stimulus_frame(uint8_t packet[CLEAN_HT_STIMULUS_STORAGE],
                                               const uint8_t nonce[14], uint32_t index) {
    if (nonce[12] >= 8u || index >= CLEAN_HT_STIMULUS_COUNT) return 0u;
    uint32_t kind = index%3u, aggregate = kind == 2u;
    uint32_t length = aggregate ? CLEAN_HT_STIMULUS_PSDU : CLEAN_HT_STIMULUS_MPDU;
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_STORAGE; ++i) packet[i] = 0u;
    uint8_t *sig = packet+CLEAN_HT_DATA_HEADER;
    if (kind == 0u) {
        if (!clean_ofdm_plcp_rate(sig,length,0u)) return 0u;
    } else if (!(aggregate ? clean_ht_stimulus_sig(sig,nonce[12]) :
                            clean_ht20_sig(sig,length,nonce[12]))) return 0u;
    packet[0] = aggregate ? CLEAN_HT_STIMULUS_VERSION : CLEAN_HT_DATA_VERSION;
    packet[2] = nonce[12];
    packet[4] = (uint8_t)length; packet[5] = (uint8_t)(length>>8u);
    for (uint32_t body = 0u; body < (aggregate ? 2u : 1u); ++body) {
        uint8_t *m = sig+6u;
        if (aggregate) {
            uint8_t *d = m+body*560u;
            d[0] = 0xa0u; d[1] = 0x22u;
            d[2] = clean_ampdu_delimiter_crc(d); d[3] = 0x4eu;
            m = d+4u;
        }
        m[0] = aggregate ? 0x88u : 8u;
        for (uint32_t i = 0u; i < 6u; ++i) m[4u+i] = m[16u+i] = 255u;
        m[10] = 2u;
        for (uint32_t i = 0u; i < 5u; ++i) m[11u+i] = nonce[i];
        uint32_t sequence = 2u*index+body;
        m[22] = (uint8_t)(sequence<<4u); m[23] = (uint8_t)(sequence>>4u);
        if (aggregate) m[24] = 0x60u;
        for (uint32_t i = 0u; i < 14u; ++i) m[26u+i] = nonce[i];
        m[40] = 'D'; m[41] = '2'; m[42] = (uint8_t)kind; m[43] = (uint8_t)body;
        for (uint32_t i = 44u; i < CLEAN_HT_STIMULUS_MPDU-4u; ++i)
            m[i] = (uint8_t)(nonce[i%14u]^i^sequence);
        uint32_t fcs = clean_mac_fcs(m,CLEAN_HT_STIMULUS_MPDU-4u);
        for (uint32_t i = 0u; i < 4u; ++i)
            m[CLEAN_HT_STIMULUS_MPDU-4u+i] = (uint8_t)(fcs>>(8u*i));
    }
    return kind == 0u ? 5u | clean_tx_length_bits(length+6u) | (1u<<18u) | (1u<<19u) :
        CLEAN_HT_DATA_CMD | clean_tx_length_bits(length+6u) | (1u<<19u);
}

static inline int clean_ht_stimulus_command(uint32_t cmd, const volatile uint8_t *packet) {
    if (!CLEAN_RX_HT_STIMULUS || packet[0] != CLEAN_HT_STIMULUS_VERSION) return 0;
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE], nonce[14];
    const volatile uint8_t *m = packet+CLEAN_HT_DATA_HEADER+10u;
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = m[26u+i];
    uint32_t sequence = ((uint32_t)m[22] | ((uint32_t)m[23]<<8u))>>4u;
    uint32_t index = sequence/2u;
    if (index%3u != 2u || cmd != clean_ht_stimulus_frame(expected,nonce,index)) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_STORAGE; ++i)
        if (packet[i] != expected[i]) return 0;
    return 1;
}

static inline uint32_t clean_ht_handoff_length(const uint8_t nonce[14], uint32_t body) {
    return (nonce[11] == 1u || (CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u && !(nonce[11]&1u))) !=
        (body != 0u) ? 170u : 298u;
}

static inline uint32_t clean_ht_handoff_length_at(const uint8_t nonce[14],
                                                  uint32_t index, uint32_t body) {
    if (index >= CLEAN_HT_STIMULUS_COUNT || body > 1u) return 0u;
    if (CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u && index%3u == 2u) {
        static const uint16_t lengths[8][2] = {
            {80u,554u},{127u,381u},{170u,298u},{298u,170u},
            {257u,255u},{479u,554u},{80u,80u},{321u,173u}};
        return lengths[(index/3u+(nonce[11]-4u)/2u)%8u][body ^ (nonce[11]&1u)];
    }
    return clean_ht_handoff_length(nonce,body);
}

static inline uint32_t clean_ht_handoff_psdu(const uint8_t nonce[14], uint32_t index) {
    uint32_t first = clean_ht_handoff_length_at(nonce,index,0u);
    if (!CLEAN_RX_HT_VARIABLE) return index%3u == 2u ? CLEAN_HT_HANDOFF_PSDU : first;
    return index%3u == 2u ? 4u+((first+3u)&~3u)+4u+
        clean_ht_handoff_length_at(nonce,index,1u) : first;
}

/* Only aggregate 2 carries the selected fault; later PPDUs remain good. */
static inline uint32_t clean_ht_handoff_frame(uint8_t packet[CLEAN_HT_STIMULUS_STORAGE],
                                              const uint8_t nonce[14], uint32_t index) {
    if (nonce[10] >= CLEAN_HT_HANDOFF_CASES ||
        (nonce[11] != 1u && nonce[11] != 2u &&
         !(CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u && nonce[11] < 20u)) ||
        !clean_ht_stimulus_frame(packet,nonce,index)) return 0u;
    uint32_t kind = index%3u, first = clean_ht_handoff_length_at(nonce,index,0u);
    uint32_t second = clean_ht_handoff_length_at(nonce,index,1u);
    uint32_t length = CLEAN_RX_HT_HANDOFF_GUARD && kind == 2u ?
        CLEAN_HT_HANDOFF_PSDU : clean_ht_handoff_psdu(nonce,index);
    uint8_t *sig = packet+CLEAN_HT_DATA_HEADER, *m = sig+6u;
    if (CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u) {
        for (uint32_t ordinal = 0u; ordinal < (kind == 2u ? 2u : 1u); ++ordinal) {
            uint32_t size = clean_ht_handoff_length_at(nonce,index,ordinal);
            uint8_t *bytes = m+(kind == 2u ?
                4u+(ordinal ? 4u+((first+3u)&~3u) : 0u) : 0u);
            for (uint32_t i = 0u; i < size; ++i) bytes[i] = 0u;
            bytes[0] = kind == 2u ? 0x88u : 8u;
            for (uint32_t i = 0u; i < 6u; ++i) bytes[4u+i] = bytes[16u+i] = 255u;
            bytes[10] = 2u;
            for (uint32_t i = 0u; i < 5u; ++i) bytes[11u+i] = nonce[i];
            uint32_t sequence = 2u*index+ordinal;
            bytes[22] = (uint8_t)(sequence<<4u); bytes[23] = (uint8_t)(sequence>>4u);
            if (kind == 2u) bytes[24] = 0x60u;
            for (uint32_t i = 0u; i < 14u; ++i) bytes[26u+i] = nonce[i];
            bytes[40] = 'D'; bytes[41] = '2'; bytes[42] = (uint8_t)kind;
            bytes[43] = (uint8_t)ordinal;
            for (uint32_t i = 44u; i < size-4u; ++i)
                bytes[i] = (uint8_t)(nonce[i%14u]^i^sequence);
        }
    }
    if (kind == 2u) {
        uint32_t at = 4u+((first+3u)&~3u);
        if (!(CLEAN_RX_HT_VARIABLE && nonce[11] >= 4u))
            for (uint32_t i = 0u; i < second; ++i) m[at+4u+i] = m[564u+i];
        for (uint32_t body = 0u; body < 2u; ++body) {
            uint8_t *delimiter = m+(body ? at : 0u), *bytes = delimiter+4u;
            uint32_t size = body ? second : first;
            /* Legal More Data variation distinguishes even a stale MAC prefix. */
            bytes[1] = body ? 2u : 0x22u;
            bytes[4] = bytes[16] = 2u;
            for (uint32_t i = 0u; i < 5u; ++i)
                bytes[5u+i] = bytes[17u+i] = nonce[i];
            bytes[9] ^= 0x80u;
            delimiter[0] = (uint8_t)(size<<4u); delimiter[1] = (uint8_t)(size>>4u);
            delimiter[2] = clean_ampdu_delimiter_crc(delimiter); delimiter[3] = 0x4eu;
            uint32_t fcs = clean_mac_fcs(bytes,size-4u);
            for (uint32_t i = 0u; i < 4u; ++i) bytes[size-4u+i] = (uint8_t)(fcs>>(8u*i));
        }
        for (uint32_t i = first+4u; i < at; ++i) m[i] = 0u;
#if CLEAN_RX_HT_HANDOFF_GUARD
        uint32_t guard_at = at+4u+((second+3u)&~3u);
        uint8_t *guard = m+guard_at+4u;
        for (uint32_t i = at+4u+second; i < guard_at; ++i) m[i] = 0u;
        for (uint32_t i = 0u; i < CLEAN_HT_HANDOFF_GUARD_BYTES; ++i) guard[i] = m[at+4u+i];
        guard[22] = (uint8_t)((2u*index+2u)<<4u);
        guard[23] = (uint8_t)((2u*index+2u)>>4u);
        guard[43] = 2u;
        uint32_t guard_fcs = clean_mac_fcs(guard,CLEAN_HT_HANDOFF_GUARD_BYTES-4u);
        for (uint32_t i = 0u; i < 4u; ++i)
            guard[CLEAN_HT_HANDOFF_GUARD_BYTES-4u+i] = (uint8_t)(guard_fcs>>(8u*i));
        m[guard_at] = (uint8_t)(CLEAN_HT_HANDOFF_GUARD_BYTES<<4u);
        m[guard_at+1u] = (uint8_t)(CLEAN_HT_HANDOFF_GUARD_BYTES>>4u);
        m[guard_at+2u] = clean_ampdu_delimiter_crc(m+guard_at);
        m[guard_at+3u] = 0x4eu;
#endif
        if (index == 2u) {
            if (nonce[10] == 1u) m[4u+first-1u] ^= 1u;
            if (nonce[10] == 2u) m[at+4u+second-1u] ^= 1u;
            if (nonce[10] == 3u) m[at+2u] ^= 1u;
            if (nonce[10] == 4u) {
                uint32_t truncated = second+8u;
                m[at] = (uint8_t)(truncated<<4u); m[at+1u] = (uint8_t)(truncated>>4u);
                m[at+2u] = clean_ampdu_delimiter_crc(m+at);
            }
        }
        packet[0] = CLEAN_HT_HANDOFF_VERSION;
        if (!clean_ht20_ampdu_sig(sig,length,nonce[12])) return 0u;
    } else {
        uint32_t fcs = clean_mac_fcs(m,first-4u);
        for (uint32_t i = 0u; i < 4u; ++i) m[first-4u+i] = (uint8_t)(fcs>>(8u*i));
        if (!(kind ? clean_ht20_sig(sig,length,nonce[12]) :
                     clean_ofdm_plcp_rate(sig,length,0u))) return 0u;
    }
    packet[4] = (uint8_t)length; packet[5] = (uint8_t)(length>>8u);
    for (uint32_t i = CLEAN_HT_DATA_HEADER+6u+length; i < CLEAN_HT_STIMULUS_STORAGE; ++i)
        packet[i] = 0u;
    return kind == 0u ? 5u | clean_tx_length_bits(length+6u) | (1u<<18u) | (1u<<19u) :
        CLEAN_HT_DATA_CMD | clean_tx_length_bits(length+6u) | (1u<<19u);
}

static inline int clean_ht_handoff_sig_valid(const volatile uint8_t *sig, uint32_t length) {
    uint8_t expected[6];
    if (!CLEAN_RX_HT_HANDOFF_STIMULUS ||
        (CLEAN_RX_HT_VARIABLE ? length < 104u || length > CLEAN_HT_STIMULUS_PSDU :
         length != CLEAN_HT_HANDOFF_PSDU) ||
        !clean_ht20_ampdu_sig(expected,length,sig[0])) return 0;
    for (uint32_t i = 0u; i < 6u; ++i) if (sig[i] != expected[i]) return 0;
    return 1;
}

static inline int clean_ht_handoff_command(uint32_t cmd, const volatile uint8_t *packet) {
    if (!CLEAN_RX_HT_HANDOFF_STIMULUS || packet[0] != CLEAN_HT_HANDOFF_VERSION) return 0;
    uint8_t expected[CLEAN_HT_STIMULUS_STORAGE], nonce[14];
    const volatile uint8_t *m = packet+CLEAN_HT_DATA_HEADER+10u;
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = m[26u+i];
    uint32_t index = (((uint32_t)m[22] | ((uint32_t)m[23]<<8u))>>4u)/2u;
    if (index%3u != 2u || cmd != clean_ht_handoff_frame(expected,nonce,index)) return 0;
    for (uint32_t i = 0u; i < CLEAN_HT_STIMULUS_STORAGE; ++i)
        if (packet[i] != expected[i]) return 0;
    return 1;
}

#include "../ht_payload.h"

static inline int clean_ht_data_command_valid(uint32_t cmd, uint32_t enabled,
        uint32_t station, const volatile uint8_t *packet) {
    return !station && enabled && (clean_ht_stimulus_command(cmd,packet) ||
        clean_ht_handoff_command(cmd,packet) ||
        clean_ht_ampdu_probe_command(cmd,packet) ||
        clean_ht_payload_command_valid(cmd, enabled, 0u, packet));
}

static inline int clean_ht_clocked_command_valid(uint32_t cmd, uint32_t enabled,
        uint32_t profile, const volatile uint8_t *packet) {
    return CLEAN_RX_HT_RESPONSE && profile == 3u &&
        clean_ht_payload_command_valid(cmd, enabled, 1u, packet);
}

static inline uint32_t clean_ht_data_length(const uint8_t nonce[14]) {
    return (uint32_t)nonce[11] | ((uint32_t)nonce[12] << 8u);
}

static inline uint8_t clean_ht_data_byte(uint32_t at, const uint8_t nonce[14]) {
    if (at == 0u) return 8u;
    if ((at >= 4u && at < 10u) || (at >= 16u && at < 22u)) return 255u;
    if (at == 10u) return 2u;
    if (at >= 11u && at < 16u) return nonce[at - 11u];
    if (at == 22u) return (uint8_t)(nonce[0] << 4u);
    if (at == 23u) return (uint8_t)((nonce[0] >> 4u) | (nonce[1] << 4u));
    if (at >= 24u) return nonce[(at - 24u) % 14u];
    return 0u;
}

static inline uint8_t clean_ht_amsdu_byte(uint32_t at, const uint8_t nonce[14]) {
    if (at == 0u) return 0x88u;
    if (at >= 4u && at < 22u) {
        uint32_t address = (at - 4u) / 6u, pos = (at - 4u) % 6u;
        if (!pos) return 2u;
        return nonce[pos - 1u] ^ (pos == 5u ? (address == 0u ? 128u :
                                                         address == 2u ? 64u : 0u) : 0u);
    }
    if (at == 22u || at == 23u) return clean_ht_data_byte(at, nonce);
    if (at == 24u) return 0xa0u; /* A-MSDU, No Ack, TID 0. */
    if (at < 26u) return 0u;
    uint32_t offset = at - 26u;
    if (offset == 2318u || offset == 2319u) return 0u;
    uint32_t size = offset < 2320u ? 2304u : 1505u;
    if (offset >= 2320u) offset -= 2320u;
    if (offset < 12u)
        return clean_ht_amsdu_byte(4u + offset, nonce);
    if (offset == 12u) return (uint8_t)(size >> 8u);
    if (offset == 13u) return (uint8_t)size;
    return nonce[(offset - 14u) % 14u];
}

static inline uint8_t clean_ht_ack_byte(uint32_t at, const uint8_t nonce[14]) {
    if (at == 2u) return (uint8_t)(10u + clean_ofdm_airtime_us(14u,
        (uint32_t)clean_ht_ack_rate(0x150u, nonce[13]) - 4u));
    if (at < 24u) return clean_ht_amsdu_byte(at, nonce);
    if (at == 24u) return nonce[6] & 15u;
    if (at == 25u) return 0u;
    return nonce[(at - 26u) % 14u];
}

static inline int clean_ht_probe_frame(uint8_t frame[96], const uint8_t nonce[14]) {
    if (!clean_ht20_sig(frame, CLEAN_HT_PROBE_MPDU, nonce[13])) return 0;
    uint8_t *m = frame + 6u;
    for (uint32_t i = 0u; i < 90u; ++i) m[i] = 0u;
    m[0] = 8u;
    for (uint32_t i = 0u; i < 6u; ++i) m[4u+i] = m[16u+i] = 255u;
    m[10] = 2u;
    for (uint32_t i = 0u; i < 5u; ++i) m[11u+i] = nonce[i];
    m[22] = (uint8_t)(nonce[0] << 4u);
    m[23] = (uint8_t)((nonce[0] >> 4u) | (nonce[1] << 4u));
    m[24] = 'H'; m[25] = 'T'; m[26] = 'D'; m[27] = '1';
    for (uint32_t i = 28u; i < CLEAN_HT_PROBE_MPDU - 4u; ++i)
        m[i] = nonce[(i - 28u) % 14u];
    uint32_t crc = clean_mac_fcs(m, CLEAN_HT_PROBE_MPDU - 4u);
    for (uint32_t i = 0u; i < 4u; ++i)
        m[CLEAN_HT_PROBE_MPDU - 4u + i] = (uint8_t)(crc >> (8u*i));
    return 1;
}

static inline int clean_ht_probe_match(const volatile uint8_t *m, uint32_t length,
                                        const uint8_t nonce[14]) {
    uint8_t expected[96];
    if (length != CLEAN_HT_PROBE_MPDU || !clean_ht_probe_frame(expected, nonce)) return 0;
    for (uint32_t i = 0u; i < length; ++i)
        if (m[i] != expected[6u+i]) return 0;
    return 1;
}

static inline int clean_ht_probe_command_valid(uint32_t cmd, uint32_t enabled,
                                               uint32_t station,
                                               const volatile uint8_t *frame) {
    if (!enabled || station ||
        cmd != (CLEAN_HT_PROBE_CMD | clean_tx_length_bits(CLEAN_HT_PROBE_BYTES) | (1u << 19u)) ||
        !clean_ht20_sig_valid(frame, CLEAN_HT_PROBE_MPDU)) return 0;
    uint8_t nonce[14];
    for (uint32_t i = 0u; i < 14u; ++i) nonce[i] = frame[6u+28u+i];
    return clean_ht_probe_match(frame + 6u, CLEAN_HT_PROBE_MPDU, nonce) &&
           nonce[13] == frame[0];
}

#endif
