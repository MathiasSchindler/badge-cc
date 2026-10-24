#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rx_contract.h"
#include "psm_ack.h"
#include "../../vendor-burst/protocol.h"

_Static_assert(CLEAN_RX_HT_COPY_SCOPE, "Select a copy scope profile");
extern const uint8_t psm_ucode_bytes[];

static uint16_t operand(const uint16_t *r, uint32_t a) {
    if (a < 0x1800u) return r[a];
    uint16_t v = (uint16_t)(a & 0x7ffu);
    return (uint16_t)((v & 0x400u) ? v | 0xf800u : v);
}

static void run(uint32_t fault) {
    uint16_t r[0x1800] = {0};
    r[CLEAN_OBSERVER_EPOCH] = 9u;
    r[0x1001u] = CLEAN_SHM_FRAME; r[0x1004u] = 0x802u;
    r[0x100cu] = 134u; r[0x100du] = 0x1922u;
    r[0x102au] = 298u; r[0x1078u] = 0xf020u;
    r[CLEAN_COPY_SCOPE_SHM-1u] = r[CLEAN_COPY_SCOPE_SHM+CLEAN_COPY_SCOPE_WORDS] = 0x71c3u;
    for (uint32_t i = 0u; i < 4u; ++i) r[0x1780u+i] = (uint16_t)(0x4300u+i);
    for (uint32_t i = 0u; i < 32u; ++i) r[CLEAN_SHM_FRAME+4u+64u+i] = (uint16_t)(0xa000u+i);
    uint32_t pc = COPY_SCOPE_EPOCH, steps = 0u, replays = 0u;
    while (pc != OBSERVER_COPY_REARM_COUNT) {
        assert(steps < 12000u && pc < ACK_PROGRAM_END);
        r[0x1119u] = (uint16_t)((fault == 4u ? 0xfff0u : 100u) +
            (fault == 3u ? 0u : steps/4u));
        if (pc == COPY_SCOPE_WAIT && fault == 1u) r[0x100du] |= 0x4000u;
        if (pc == COPY_SCOPE_WAIT && fault == 2u) r[0x100du] |= 0x8000u;
        uint64_t w = 0u;
        for (uint32_t i = 0u; i < 8u; ++i) w |= (uint64_t)psm_ucode_bytes[8u*pc+i] << (8u*i);
        ++pc; ++steps;
        uint32_t op = (uint32_t)(w>>39u), a = (uint32_t)(w>>26u)&0x1fffu;
        uint32_t b = (uint32_t)(w>>13u)&0x1fffu, d = (uint32_t)w&0x1fffu;
        uint16_t av = operand(r,a), bv = operand(r,b);
        if (op == 0x3f0u) r[d] = av;
        else if (op == 0x1c0u) r[d] = (uint16_t)(av+bv);
        else if (op == 0x1d0u) r[d] = (uint16_t)(av-bv);
        else if (op == 0x310u) r[d] = (uint16_t)((bv&~3u)|(av&3u));
        else if (op == 0x376u) {
            assert(a == (0x1800u|CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD) &&
                b == 0x1004u && d == 0x1004u);
            ++replays;
            r[d] = (uint16_t)((bv&~0x3fc0u)|((av<<6u)&0x3fc0u));
        }
        else if (op == 0x77fu) pc = d;
        else if (op == 0xd0u) { if (av == bv) pc = d; }
        else if (op == 0xdbu) { if (av >= bv) pc = d; }
        else if (op == 0x50eu) { if (av & 0x4000u) pc = d; }
        else { assert(op == 0x50fu); if (av & 0x8000u) pc = d; }
    }
    uint16_t *scope = r+CLEAN_COPY_SCOPE_SHM;
    assert(scope[0] == (fault == 1u ? 18u : fault == 2u ? 19u : fault == 3u ? 20u : 17u));
    assert(scope[1] == 9u);
    assert(replays == (CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY ? 0u : 1u));
    uint16_t command = CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY ? 0x800u :
        (uint16_t)(((CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD<<6u)&0x3fc0u) |
            (CLEAN_RX_HT_COPY_SCOPE == 1 ? 2u : 0u));
    assert(r[0x1004u] == command);
    for (uint32_t n = 0u; n < 3u; ++n) {
        const uint16_t *s = scope+2u+26u*n;
        assert(s[1] == 134u &&
               s[5] == CLEAN_SHM_FRAME+(n && CLEAN_STREAM_DEST_SHIFT ? 64u : 0u));
        assert(s[6] == 298u && s[9] == 0xf020u);
        assert((s[4]&3u) == (n && CLEAN_RX_HT_COPY_SCOPE >= 2 ? 0u : 2u));
        for (uint32_t i = 0u; i < 8u; ++i) {
            assert(s[10u+i] == 0xa000u+i);
            assert(s[18u+i] == 0xa010u+i);
        }
    }
    for (uint32_t i = 0u; i < 4u; ++i) assert(r[0x1780u+i] == 0x4300u+i);
    assert(r[CLEAN_COPY_SCOPE_SHM-1u] == 0x71c3u &&
        r[CLEAN_COPY_SCOPE_SHM+CLEAN_COPY_SCOPE_WORDS] == 0x71c3u);
    if (!fault || fault == 4u) assert((uint16_t)(scope[54u]-scope[28u]) >= CLEAN_COPY_SCOPE_US);
    uint16_t copy_words[54] = {0}, early_words[16] = {0};
    early_words[0] = copy_words[0] = 1u; early_words[1] = copy_words[1] = 9u;
    early_words[13] = 298u; copy_words[2] = 3u; copy_words[3] = 6u;
    copy_words[36] = 130u; copy_words[38] = 0x802u;
    copy_words[42] = 1u; copy_words[43] = 9u;
    copy_words[44] = 134u; copy_words[45] = 100u;
    copy_words[47] = 32u; copy_words[48] = 1u;
    uint8_t bytes[160], copy[108], early[32];
    for (uint32_t i = 0u; i < 80u; ++i) {
        bytes[2u*i] = (uint8_t)scope[i]; bytes[2u*i+1u] = (uint8_t)(scope[i]>>8u);
    }
    for (uint32_t i = 0u; i < 54u; ++i) {
        copy[2u*i] = (uint8_t)copy_words[i]; copy[2u*i+1u] = (uint8_t)(copy_words[i]>>8u);
    }
    for (uint32_t i = 0u; i < 16u; ++i) {
        early[2u*i] = (uint8_t)early_words[i]; early[2u*i+1u] = (uint8_t)(early_words[i]>>8u);
    }
    assert(ref_copy_scope_valid(bytes,copy,early,CLEAN_RX_HT_COPY_SCOPE));
    static const uint32_t mutations[] = {0u,2u,6u,12u,14u,16u,64u,66u,68u,116u,118u,120u};
    for (uint32_t i = 0u; i < sizeof(mutations)/sizeof(mutations[0]); ++i) {
        uint8_t bad[160]; memcpy(bad,bytes,sizeof(bad));
        bad[mutations[i]] ^= 1u;
        assert(!ref_copy_scope_valid(bad,copy,early,CLEAN_RX_HT_COPY_SCOPE));
    }
}

#if CLEAN_RX_HT_COPY_SCOPE >= 3
static void hold_guard(uint32_t fault) {
    uint16_t r[0x1800] = {0};
    r[CLEAN_COPY_SCOPE_SHM] = fault == 2u ? 0u : fault == 3u ? 21u : fault == 6u ? 16u : 17u;
    r[CLEAN_COPY_SCOPE_SHM+1u] = fault == 1u ? 8u : 9u;
    r[CLEAN_OBSERVER_EPOCH] = 9u;
    r[CLEAN_OBSERVER_ADVANCE] = fault == 4u ? 0u : fault == 5u ? 2u : 1u;
    r[0x1004u] = 0x802u; r[0x102au] = 298u;
    for (uint32_t i = 0u; i < 4u; ++i) r[0x1780u+i] = (uint16_t)(0x4310u+i);
    uint32_t pc = OBSERVER_STREAM_PHASE_WRITE_ONLY, steps = 0u;
    while (pc != 312u) {
        assert(++steps < 24u);
        uint64_t w = 0u;
        for (uint32_t i = 0u; i < 8u; ++i) w |= (uint64_t)psm_ucode_bytes[8u*pc+i] << (8u*i);
        ++pc;
        uint32_t op = (uint32_t)(w>>39u), a = (uint32_t)(w>>26u)&0x1fffu;
        uint32_t b = (uint32_t)(w>>13u)&0x1fffu, d = (uint32_t)w&0x1fffu;
        uint16_t av = operand(r,a), bv = operand(r,b);
        if (op == 0x3f0u) r[d] = av;
        else if (op == 0xd1u) { if (av != bv) pc = d; }
        else if (op == 0xdau) { if (av < bv) pc = d; }
        else if (op == 0xdcu) { if (av > bv) pc = d; }
        else if (op == 0x77fu) pc = d;
        else { assert(op == 0x310u); r[d] = (uint16_t)((bv&~3u)|(av&3u)); }
    }
    assert((r[0x1004u]&3u) == (!fault && CLEAN_RX_HT_COPY_SCOPE == 3 ? 0u : 2u));
    assert(r[0x102au] == 298u);
    for (uint32_t i = 0u; i < 4u; ++i) assert(r[0x1780u+i] == 0x4310u+i);
}
#endif

#if CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD != 0x83u
static void admission(uint32_t fault) {
    uint16_t r[0x1800] = {0};
    r[CLEAN_OBSERVER_ADVANCE] = fault == 6u ? 0u : 1u;
    r[CLEAN_OBSERVER_EARLY_SHM] = fault == 7u ? 0u : 2u;
    r[CLEAN_OBSERVER_PENDING] = fault == 8u ? 3u : 2u;
    r[CLEAN_OBSERVER_EPOCH] = fault == 9u ? 10u : 9u;
    r[CLEAN_OBSERVER_EARLY_SHM+1u] = fault == 10u ? 10u : 9u;
    r[CLEAN_OBSERVER_OUTCOME] = fault >= 3u && fault < 6u ? (uint16_t)(fault == 3u ? 2u :
        fault == 4u ? 5u : 6u) : 1u;
    r[CLEAN_OBSERVER_EARLY_SHM+13u] = fault == 2u ? 170u : 298u;
    r[0x100cu] = fault == 1u ? 304u : 176u;
    r[0x100du] = 0x4100u; r[0x1782u] = 0x71c3u;
    for (uint32_t i = 0u; i < 162u; ++i)
        r[CLEAN_OBSERVER_SECOND_SHM+i] = (uint16_t)(0xa100u+i);
    uint32_t pc = OBSERVER_SECOND_DISPATCH, steps = 0u;
    while (pc != OBSERVER_TERMINAL && pc != OBSERVER_EXIT_NATIVE) {
        assert(++steps < 24u);
        uint64_t w = 0u;
        for (uint32_t i = 0u; i < 8u; ++i) w |= (uint64_t)psm_ucode_bytes[8u*pc+i] << (8u*i);
        ++pc;
        uint32_t op = (uint32_t)(w>>39u), a = (uint32_t)(w>>26u)&0x1fffu;
        uint32_t b = (uint32_t)(w>>13u)&0x1fffu, d = (uint32_t)w&0x1fffu;
        uint16_t av = operand(r,a), bv = operand(r,b);
        if (op == 0xd0u) { if (av == bv) pc = d; }
        else if (op == 0xd1u) { if (av != bv) pc = d; }
        else if (op == 0x3f0u) r[d] = av;
        else if (op == 0x1c0u) r[d] = (uint16_t)(av+bv);
        else { assert(op == 0x77fu); pc = d; }
    }
    assert(pc == (!fault || fault == 4u || fault == 5u ? OBSERVER_EXIT_NATIVE : OBSERVER_TERMINAL));
    assert(r[CLEAN_OBSERVER_OUTCOME] == (!fault ? 4u : fault == 3u ? 2u :
        fault == 4u ? 5u : fault == 5u ? 6u : 1u));
    assert(r[0x100cu] == (fault == 1u ? 304u : 176u));
    assert(r[0x100du] == 0x4100u && r[0x1782u] == 0x71c3u);
    for (uint32_t i = 0u; i < 162u; ++i)
        assert(r[CLEAN_OBSERVER_SECOND_SHM+i] == (uint16_t)(0xa100u+i));
}
#endif

static int capture(const char *path) {
    uint8_t report[64u+REF_TRACE_BYTES];
    FILE *file = fopen(path,"rb");
    if (!file) { perror(path); return 1; }
    size_t bytes = fread(report,1u,sizeof(report),file);
    int extra = fgetc(file), failed = ferror(file), closed = fclose(file);
    if (bytes != sizeof(report) || extra != EOF || failed || closed ||
        badger_bytes_load_u32(report+60u) != diag_crc32(report,60u)) {
        fprintf(stderr,"Invalid or unreadable complete report: %s\n",path);
        return 1;
    }
    const uint8_t *trace = report+64u, *early = trace+REF_EARLY_BASE;
    const uint8_t *second = trace+REF_SECOND_BASE, *copy = trace+REF_COPY_BASE;
    const uint8_t *scope = early+2u*(CLEAN_COPY_SCOPE_SHM-CLEAN_OBSERVER_EARLY_SHM);
    int valid = ref_copy_probe_valid(copy,early,second) &&
        ref_copy_scope_valid(scope,copy,early,CLEAN_RX_HT_COPY_SCOPE);
    uint32_t length = ref_u16(early+26u);
    int fcs = length >= 44u && length <= CLEAN_OBSERVER_SECOND_MAX &&
        diag_crc32(second+24u,length-4u) == badger_bytes_load_u32(second+24u+length-4u);
    printf("{\"test\":\"copy-scope-capture\",\"file\":\"%s\",\"mode\":%u,"
           "\"replay_suppressed\":%s,\"replay_command_field\":%u,"
           "\"diagnostic_valid\":%s,\"expected_bytes\":%u,"
           "\"expected_length_fcs_valid\":%s,\"native_count\":%u,"
           "\"native_outcome\":%u,\"delivery_qualified\":false}\n",
           path,CLEAN_RX_HT_COPY_SCOPE,CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY?"true":"false",
           CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD,
           valid?"true":"false",length,fcs?"true":"false",
           ref_u16(second+4u),ref_u16(second+20u));
    if (!valid) fputs("Copy scope capture validation failed\n",stderr);
    return valid ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 2) return capture(argv[1]);
    if (argc != 1) { fputs("Usage: copy_scope_test [CAPTURE]\n",stderr); return 2; }
    for (uint32_t fault = 0u; fault < 5u; ++fault) run(fault);
#if CLEAN_RX_HT_COPY_SCOPE >= 3
    for (uint32_t fault = 0u; fault < 7u; ++fault) hold_guard(fault);
#endif
#if CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD != 0x83u
    for (uint32_t fault = 0u; fault < 11u; ++fault) admission(fault);
#endif
    puts("Copy scope: emitted PSM normal/EOF/abort/frozen/wrap paths, bounded writes/wait, GPR preservation and readback mutations pass; not RF emulation");
    return 0;
}
