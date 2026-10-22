#include "lorawan.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const mtype_names[8] = {
    "JoinRequest", "JoinAccept", "UnconfirmedUp", "UnconfirmedDown",
    "ConfirmedUp", "ConfirmedDown", "RejoinRequest", "Proprietary",
};

const char *lorawan_mtype_name(uint32_t mtype) {
    return mtype < 8u ? mtype_names[mtype] : "?";
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}

int lorawan_parse(const uint8_t *frame, uint32_t length, lorawan_frame_t *out) {
    memset(out, 0, sizeof(*out));
    if (length == 0u) return 0;
    out->mtype = frame[0] >> 5u;
    out->major = frame[0] & 3u;
    /* LoRaWAN R1 frames only; RFU bits 4..2 must be zero. */
    if (out->major != 0u || (frame[0] & 0x1cu) != 0u) return 0;
    switch (out->mtype) {
    case 0u:
        if (length != 23u) return 0;
        for (uint32_t i = 0u; i < 8u; ++i) {
            out->join_eui[i] = frame[8u - i];
            out->dev_eui[i] = frame[16u - i];
        }
        out->dev_nonce = (uint16_t)(frame[17] | frame[18] << 8u);
        break;
    case 1u:
        if (length != 17u && length != 33u) return 0;
        break;
    case 2u: case 3u: case 4u: case 5u: {
        if (length < 12u) return 0;
        out->dev_addr = le32(frame + 1u);
        out->fctrl = frame[5];
        out->fopts_length = frame[5] & 15u;
        out->fcnt16 = (uint16_t)(frame[6] | frame[7] << 8u);
        uint32_t used = 8u + out->fopts_length + 4u;
        if (used > length) return 0;
        if (length > used) {
            out->has_fport = 1u;
            out->fport = frame[8u + out->fopts_length];
            out->frm_length = length - used - 1u;
        }
        break;
    }
    case 6u:
        if (length != 19u && length != 24u) return 0;
        break;
    default:
        break;
    }
    out->valid = 1u;
    return 1;
}

void lorawan_netid(uint32_t dev_addr, uint32_t *type, uint32_t *nwk_id) {
    static const uint8_t nwk_bits[8] = {6u, 6u, 9u, 11u, 12u, 13u, 15u, 17u};
    uint32_t t = 0u;
    while (t < 8u && (dev_addr & (0x80000000u >> t))) ++t;
    *type = t;
    if (t == 8u) { *nwk_id = 0u; return; }
    uint32_t shift = 32u - (t + 1u) - nwk_bits[t];
    *nwk_id = (dev_addr >> shift) & ((1u << nwk_bits[t]) - 1u);
}

const char *lorawan_operator(uint32_t dev_addr) {
    uint32_t type, nwk_id;
    lorawan_netid(dev_addr, &type, &nwk_id);
    if (type != 0u) return NULL;
    if (nwk_id == 0x13u) return "The Things Network";
    if (nwk_id <= 1u) return "experimental/private NetID";
    return NULL;
}

typedef struct { char *p; size_t left; } out_t;

static void put(out_t *o, const char *format, ...) {
    if (o->left <= 1u) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(o->p, o->left, format, args);
    va_end(args);
    if (n < 0) return;
    size_t used = (size_t)n < o->left ? (size_t)n : o->left - 1u;
    o->p += used;
    o->left -= used;
}

int lorawan_describe(const uint8_t *frame, uint32_t length, char *output, size_t capacity) {
    out_t o = {output, capacity};
    if (capacity) output[0] = 0;
    lorawan_frame_t f;
    if (!lorawan_parse(frame, length, &f)) {
        put(&o, "not a LoRaWAN R1 frame (%lu B)", (unsigned long)length);
        return 0;
    }
    put(&o, "%s", lorawan_mtype_name(f.mtype));
    if (f.mtype == 0u) {
        put(&o, " JoinEUI ");
        for (uint32_t i = 0u; i < 8u; ++i) put(&o, "%02X", f.join_eui[i]);
        put(&o, " DevEUI ");
        for (uint32_t i = 0u; i < 8u; ++i) put(&o, "%02X", f.dev_eui[i]);
        put(&o, " DevNonce %u", (unsigned)f.dev_nonce);
    } else if (f.mtype >= 2u && f.mtype <= 5u) {
        uint32_t type, nwk_id;
        lorawan_netid(f.dev_addr, &type, &nwk_id);
        const char *op = lorawan_operator(f.dev_addr);
        put(&o, " DevAddr %08lX (", (unsigned long)f.dev_addr);
        if (type < 8u) put(&o, "NetID type %lu NwkID 0x%lX", (unsigned long)type, (unsigned long)nwk_id);
        else put(&o, "invalid NetID prefix");
        if (op) put(&o, ", %s", op);
        put(&o, ") FCnt16 %u", (unsigned)f.fcnt16);
        if (f.has_fport) {
            if (f.fport == 0u) put(&o, " FPort 0 (MAC) %lu B", (unsigned long)f.frm_length);
            else put(&o, " FPort %u %lu B", (unsigned)f.fport, (unsigned long)f.frm_length);
        } else put(&o, " no FPort");
        if (f.fopts_length) put(&o, " FOpts %u B", (unsigned)f.fopts_length);
        uint8_t up = (f.mtype & 1u) == 0u;
        if (f.fctrl & 0x80u) put(&o, " [ADR]");
        if (up && (f.fctrl & 0x40u)) put(&o, " [ADRACKReq]");
        if (f.fctrl & 0x20u) put(&o, " [ACK]");
        if (f.fctrl & 0x10u) put(&o, up ? " [ClassB]" : " [FPending]");
    } else {
        put(&o, " %lu B (encrypted/opaque)", (unsigned long)length);
    }
    put(&o, " | MIC not verified");
    return 1;
}
