#ifndef CLEAN_RX_PSM_ACK_H
#define CLEAN_RX_PSM_ACK_H

#include "../unicast.h"

/* Only the final GoodFCS path reaches this program. RX streaming is unchanged.
 * Labels are enumerated from the existing image's last instruction. */
#define ACK_RATE_CODE(X, n, phy) \
    X(ACK_RATE_##n, 0x3f0u, CLEAN_ACK_TABLE + 4u * n, 0x1800u, 0x10e0u) \
    X(ACK_RATE_##n##_1, 0x3f0u, CLEAN_ACK_TABLE + 4u * n + 1u, 0x1800u, 0x10e1u) \
    X(ACK_RATE_##n##_2, 0x3f0u, CLEAN_ACK_TABLE + 4u * n + 2u, 0x1800u, 0x10e2u) \
    X(ACK_RATE_##n##_3, 0x3f0u, CLEAN_ACK_TABLE + 4u * n + 3u, 0x1800u, 0x108au) \
    X(ACK_RATE_##n##_4, 0x3f0u, 0x1800u | phy, 0x1800u, 0x1086u) \
    X(ACK_RATE_##n##_5, 0x77fu, 0x1780u, 0x1780u, ACK_PREPARE)

#define ACK_PROGRAM(X) \
    X(ACK_ENTER, 0x3f0u, 0x180fu, 0x1800u, 0x607u) \
    X(ACK_ENABLED, 0xd0u, CLEAN_ACK_ENABLE, 0x1800u, 198u) \
    X(ACK_RA0, 0xd1u, CLEAN_SHM_FRAME + 6u, 0x612u, 198u) \
    X(ACK_RA1, 0xd1u, CLEAN_SHM_FRAME + 7u, 0x613u, 198u) \
    X(ACK_RA2, 0xd1u, CLEAN_SHM_FRAME + 8u, 0x614u, 198u) \
    X(ACK_IS_CONTROL, 0xd1u, CLEAN_SHM_FRAME + 4u, 0x18d4u, ACK_DATA) \
    X(ACK_EXPECTED, 0xd0u, CLEAN_ACK_WAITING, 0x1800u, 198u) \
    X(ACK_LENGTH, 0xd1u, 0x100cu, 0x1814u, 198u) \
    X(ACK_RECORD, 0x3f0u, 0x1801u, 0x1800u, CLEAN_ACK_SEEN) \
    X(ACK_RECORD_END, 0x77fu, 0x1780u, 0x1780u, 198u) \
    X(ACK_DATA, 0xdau, 0x100cu, 0x1822u, 198u) \
    X(ACK_PEER0, 0xd1u, CLEAN_SHM_FRAME + 9u, CLEAN_ACK_PEER, 198u) \
    X(ACK_PEER1, 0xd1u, CLEAN_SHM_FRAME + 10u, CLEAN_ACK_PEER + 1u, 198u) \
    X(ACK_PEER2, 0xd1u, CLEAN_SHM_FRAME + 11u, CLEAN_ACK_PEER + 2u, 198u) \
    X(ACK_VERSION0, 0x500u, CLEAN_SHM_FRAME + 4u, 0x1800u, 198u) \
    X(ACK_VERSION1, 0x501u, CLEAN_SHM_FRAME + 4u, 0x1800u, 198u) \
    X(ACK_TYPE, 0x502u, CLEAN_SHM_FRAME + 4u, 0x1800u, 198u) \
    X(ACK_SUBTYPE, 0x270u, CLEAN_SHM_FRAME + 4u, 0x1800u, 0x1782u) \
    X(ACK_ACTION_NOACK, 0xd0u, 0x1782u, 0x18e0u, 198u) \
    X(ACK_MORE_FRAG, 0x50au, CLEAN_SHM_FRAME + 4u, 0x1800u, 198u) \
    X(ACK_FRAG0, 0x500u, CLEAN_SHM_FRAME + 15u, 0x1800u, 198u) \
    X(ACK_FRAG1, 0x501u, CLEAN_SHM_FRAME + 15u, 0x1800u, 198u) \
    X(ACK_FRAG2, 0x502u, CLEAN_SHM_FRAME + 15u, 0x1800u, 198u) \
    X(ACK_FRAG3, 0x503u, CLEAN_SHM_FRAME + 15u, 0x1800u, 198u) \
    X(ACK_MGMT, 0x403u, CLEAN_SHM_FRAME + 4u, 0x1800u, ACK_ENCODING) \
    X(ACK_QOS, 0x507u, CLEAN_SHM_FRAME + 4u, 0x1800u, 198u) \
    X(ACK_ENCODING, 0x50du, 0x100eu, 0x1800u, ACK_OFDM) \
    X(ACK_CCK_RATE, 0x270u, CLEAN_SHM_FRAME + 1u, 0x1800u, 0x1782u) \
    X(ACK_CCK1, 0xd0u, 0x1782u, 0x180au, ACK_RATE_0) \
    X(ACK_CCK2, 0xd0u, 0x1782u, 0x1814u, ACK_RATE_1) \
    X(ACK_CCK55, 0xd0u, 0x1782u, 0x1837u, ACK_RATE_2) \
    X(ACK_CCK11, 0xd0u, 0x1782u, 0x186eu, ACK_RATE_3) \
    X(ACK_CCK_UNKNOWN, 0x77fu, 0x1780u, 0x1780u, 198u) \
    X(ACK_OFDM, 0x230u, CLEAN_SHM_FRAME + 1u, 0x1800u, 0x1782u) \
    X(ACK_OFDM6, 0xd0u, 0x1782u, 0x180bu, ACK_RATE_4) \
    X(ACK_OFDM9, 0xd0u, 0x1782u, 0x180fu, ACK_RATE_5) \
    X(ACK_OFDM12, 0xd0u, 0x1782u, 0x180au, ACK_RATE_6) \
    X(ACK_OFDM18, 0xd0u, 0x1782u, 0x180eu, ACK_RATE_7) \
    X(ACK_OFDM24, 0xd0u, 0x1782u, 0x1809u, ACK_RATE_8) \
    X(ACK_OFDM36, 0xd0u, 0x1782u, 0x180du, ACK_RATE_9) \
    X(ACK_OFDM48, 0xd0u, 0x1782u, 0x1808u, ACK_RATE_10) \
    X(ACK_OFDM54, 0xd0u, 0x1782u, 0x180cu, ACK_RATE_11) \
    X(ACK_OFDM_UNKNOWN, 0x77fu, 0x1780u, 0x1780u, 198u) \
    ACK_RATE_CODE(X, 0, 0x40u) \
    ACK_RATE_CODE(X, 1, 0x40u) \
    ACK_RATE_CODE(X, 2, 0x40u) \
    ACK_RATE_CODE(X, 3, 0x40u) \
    ACK_RATE_CODE(X, 4, 0x41u) \
    ACK_RATE_CODE(X, 5, 0x41u) \
    ACK_RATE_CODE(X, 6, 0x41u) \
    ACK_RATE_CODE(X, 7, 0x41u) \
    ACK_RATE_CODE(X, 8, 0x41u) \
    ACK_RATE_CODE(X, 9, 0x41u) \
    ACK_RATE_CODE(X, 10, 0x41u) \
    ACK_RATE_CODE(X, 11, 0x41u) \
    X(ACK_PREPARE, 0x3f0u, 0x1800u, 0x1800u, 0x1080u) \
    X(ACK_MASK0, 0x3f0u, 0x1fffu, 0x1800u, 0x10c0u) \
    X(ACK_MASK1, 0x3f0u, 0x1fffu, 0x1800u, 0x10c1u) \
    X(ACK_MASK2, 0x3f0u, 0x1fffu, 0x1800u, 0x10c2u) \
    X(ACK_MASK3, 0x3f0u, 0x1fffu, 0x1800u, 0x10c3u) \
    X(ACK_MASK4, 0x3f0u, 0x1fffu, 0x1800u, 0x10c4u) \
    X(ACK_MASK5, 0x3f0u, 0x1fffu, 0x1800u, 0x10c5u) \
    X(ACK_MASK6, 0x3f0u, 0x1fffu, 0x1800u, 0x10c6u) \
    X(ACK_MASK7, 0x3f0u, 0x1fffu, 0x1800u, 0x10c7u) \
    X(ACK_FC, 0x3f0u, 0x18d4u, 0x1800u, 0x10e3u) \
    X(ACK_DURATION, 0x3f0u, 0x1800u, 0x1800u, 0x10e4u) \
    X(ACK_TA0, 0x3f0u, CLEAN_SHM_FRAME + 9u, 0x1800u, 0x10e5u) \
    X(ACK_TA1, 0x3f0u, CLEAN_SHM_FRAME + 10u, 0x1800u, 0x10e6u) \
    X(ACK_TA2, 0x3f0u, CLEAN_SHM_FRAME + 11u, 0x1800u, 0x10e7u) \
    X(ACK_WM0, 0x3f0u, 0x18ffu, 0x1800u, 0x1084u) \
    X(ACK_WM1, 0x3f0u, 0x1800u, 0x1800u, 0x1085u) \
    X(ACK_CTL_LOW, 0x3f0u, 0x1821u, 0x1800u, 0x1782u) \
    X(ACK_CTL_HIGH, 0x378u, 0x1840u, 0x1782u, 0x1782u) \
    X(ACK_CTL, 0x3f0u, 0x1782u, 0x1800u, 0x1080u) \
    X(ACK_COUNTER, 0x3f0u, 0x1bffu, 0x1800u, 0x1782u) \
    X(ACK_WAIT_NOW, 0x7a0u, 0x1780u, 0x1780u, ACK_NOW) \
    X(ACK_WAIT_DEC, 0x1d0u, 0x1782u, 0x1801u, 0x1782u) \
    X(ACK_WAIT_AGAIN, 0xd1u, 0x1782u, 0x1800u, ACK_WAIT_NOW) \
    X(ACK_FAILED, 0x1c0u, CLEAN_ACK_ERROR, 0x1801u, CLEAN_ACK_ERROR) \
    X(ACK_CANCEL, 0x3f0u, 0x1800u, 0x1800u, 0x1080u) \
    X(ACK_FAIL_END, 0x77fu, 0x1780u, 0x1780u, 198u) \
    X(ACK_NOW, 0x304u, 0x1801u, 0x1144u, 0x1144u) \
    X(ACK_SELECT_CLEAR, 0x3f0u, 0x1800u, 0x1800u, 0x10b4u) \
    X(ACK_POINTER, 0x3f0u, 0x1800u, 0x1800u, 0x10a6u) \
    X(ACK_COUNT, 0x3f0u, 0x1810u, 0x1800u, 0x10b5u) \
    X(ACK_SELECT_LOW, 0x3f0u, 0x1826u, 0x1800u, 0x1782u) \
    X(ACK_SELECT_HIGH, 0x378u, 0x1808u, 0x1782u, 0x1782u) \
    X(ACK_SELECT, 0x3f0u, 0x1782u, 0x1800u, 0x10b4u) \
    X(ACK_CRYPTO, 0x308u, 0x1801u, 0x1800u, 0x11e0u) \
    X(ACK_DIAG_STATUS, 0x3f0u, 0x1087u, 0x1800u, 0x635u) \
    X(ACK_DIAG_IFS, 0x3f0u, 0x1148u, 0x1800u, 0x636u) \
    X(ACK_COUNT_SENT, 0x1c0u, CLEAN_ACK_SENT, 0x1801u, CLEAN_ACK_SENT) \
    X(ACK_END, 0x77fu, 0x1780u, 0x1780u, 198u)

#define ACK_LABEL(name, op, a, b, d) name,
enum { ACK_BEFORE = CLEAN_ACK_HOOK - 1u, ACK_PROGRAM(ACK_LABEL) ACK_PROGRAM_END };
#undef ACK_LABEL

#endif
