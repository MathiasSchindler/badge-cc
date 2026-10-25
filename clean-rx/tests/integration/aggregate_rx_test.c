#define main psm_suite_main
#include "../unit/psm_ucode_test.c"
#undef main
#define mmio_read32 unused_mmio_read32
#define mmio_write32 unused_mmio_write32
#include "../../src/cyw43_hw.h"
#undef mmio_read32
#undef mmio_write32
#include "../../src/ht20.h"
#include "../../src/bytes.h"
#include "../../src/sdio_ring.h"
#include "../fixtures/ht20.h"

_Static_assert(CLEAN_RX_AMPDU_REPEAT, "Run with the repeated SHM diagnostic flags");

#define AGGREGATE(field) test_##field
#define TRANSPORT(field) test_##field
#define MULTI (&multi)
#undef ARM_CYCLE_COUNTER
#define ARM_CYCLE_COUNTER 65536u

static uint16_t registers[0x1800];
static fw_multi_t multi;
static uint32_t test_rx_published_frames, control, error, releases, barriers;
static uint32_t pauses, release_fault;
static int pending_release, reject_pause;

static uint16_t shm_read16(uint32_t word) {
    assert(word < 0x1000u);
    return registers[word];
}

static void shm_write16(uint32_t word, uint16_t value) {
    assert(word < 0x1000u);
    if (word == 0x601u) {
        assert(registers[word] == CLEAN_AMPDU_SHM_HELD_STATE && !value);
        assert(!(control & 2u) || !CLEAN_RX_AMPDU_REPEAT);
        pending_release=1;
    }
    registers[word]=value;
}

static uint32_t mmio_read32(uint32_t address) {
    if (address == D11_MACCONTROL) return control;
    assert(address == D11_BASE+0x180u);
    return 32u;
}

static void mmio_write32(uint32_t address, uint32_t value) {
    assert(address == D11_MACCONTROL);
    if (!(value&2u) && ++pauses==2u) {
        if (release_fault==1u)
            ++registers[CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_STATUS_WORD+2u];
        if (release_fault==2u) reject_pause=1;
    }
    control=reject_pause?value|2u:value;
    if ((control&2u) && pending_release) {
        assert(psm_run(registers,0,AMPDU_SHM_WAIT)==
               (registers[0x1782u]==2u?263u:266u));
        pending_release=0;
        ++releases;
    }
}

static int fail(uint32_t code, uint32_t address, uint32_t value) {
    (void)address; (void)value;
    assert(code == 0x8cu || code == 0x8du || code == 0x8eu);
    error=code;
    return 0;
}

static void ampdu_shm_barrier(void) { ++barriers; }

#include "../../src/runtime/aggregate_rx.inc"

static FILE *record;

static void record_hex(const volatile uint8_t *bytes, uint32_t length) {
    for (uint32_t i=0u;i<length;++i) fprintf(record,"%02x",(unsigned)bytes[i]);
}

static void record_state(void) {
    const uint32_t status=CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_STATUS_WORD;
    fprintf(record,",\"published\":%u,\"released\":%u,\"claimed\":%u,"
            "\"claim_state\":%u,\"state\":%u,\"marker\":%u,"
            "\"probe_count\":%u,\"mode\":%u,\"rows\":%u,"
            "\"captured\":%u,\"credited\":%u,\"copied\":%u,\"next\":%u,\"lease\":%u",
            (unsigned)registers[status+2u],(unsigned)registers[status+3u],
            (unsigned)registers[status+5u],(unsigned)registers[status+4u],
            (unsigned)registers[0x601u],
            (unsigned)registers[CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_REPEAT_WORD],
            (unsigned)registers[CLEAN_AMPDU_PROBE_SHM],
            (unsigned)registers[CLEAN_AMPDU_IF_SHM],
            (unsigned)registers[CLEAN_AMPDU_IF_SHM+1u],
            multi.captured_frames,test_rx_published_frames,
            (unsigned)test_ampdu_shm_copied,(unsigned)test_ampdu_shm_next,
            (unsigned)test_ampdu_shm_lease);
}

static void record_probe(void) {
    fputs(",\"probe\":\"",record);
    for (uint32_t i=0u;i<CLEAN_AMPDU_PROBE_WORDS;++i) {
        uint16_t word=registers[CLEAN_AMPDU_PROBE_SHM+i];
        fprintf(record,"%02x%02x",(unsigned)(word&255u),(unsigned)(word>>8u));
    }
    fputc('"',record);
}

static void record_frame(const fw_frame_slot_t *slot) {
    fprintf(record,",\"frame\":{\"index\":%u,\"length\":%u,\"status1\":%u,"
            "\"status2\":%u,\"phy\":%u,\"tick_hi\":%u,\"dma_length\":%u,"
            "\"dma_completed\":%u,\"f2_received\":%u,\"f2_length\":%u,\"raw\":\"",
            (unsigned)slot->frame_index,(unsigned)slot->rxe_len,
            (unsigned)slot->rxe_status1,(unsigned)slot->rxe_status2,
            (unsigned)slot->phy_status0,(unsigned)slot->rx_tick_hi,
            (unsigned)slot->dma_rx_len,(unsigned)slot->dma_completed,
            (unsigned)slot->f2_received,(unsigned)slot->f2_sdpcm_len);
    record_hex(slot->raw,88u);
    fputs("\"}",record);
}

static void record_boundary(const char *event, const char *scenario) {
    if (!record) return;
    fprintf(record,"{\"event\":\"%s\",\"scenario\":\"%s\"",event,scenario);
    record_state();
    fputs("}\n",record);
}

static int receive(void) {
    uint32_t before=multi.captured_frames;
    int ok=ampdu_shm_receive(3u,0u);
    if (record) {
        fprintf(record,"{\"event\":\"poll\",\"ok\":%d,\"error\":%u",ok,error);
        record_state();
        record_probe();
        if (multi.captured_frames!=before)
            record_frame(&multi.frames[before&(FW_MAX_FRAMES-1u)]);
        else fputs(",\"frame\":null",record);
        fputs("}\n",record);
    }
    return ok;
}

static void reset(void) {
    memset(registers,0,sizeof(registers));
    memset(&multi,0,sizeof(multi));
    memset(&test_ampdu_shm_snapshot,0,sizeof(test_ampdu_shm_snapshot));
    memset(test_ampdu_shm_outcomes,0,sizeof(test_ampdu_shm_outcomes));
    test_ampdu_shm_copied=test_ampdu_shm_next=0u;
    test_ampdu_shm_lease=0u;
    test_rx_published_frames=error=releases=barriers=0u;
    pauses=release_fault=0u;
    pending_release=reject_pause=0;
    control=3u;
}

static uint8_t expected[1024][80];
static uint32_t expected_count, consumed;

static void publish(uint32_t identity, uint32_t count, uint32_t bad) {
    assert(count && count<=3u && !registers[0x601u]);
    assert(!registers[CLEAN_AMPDU_PROBE_SHM] && !registers[CLEAN_AMPDU_IF_SHM]);
    uint8_t nonce[14]={0}, raw[88]={0};
    clean_bytes_store_u32(nonce,identity);
    nonce[13]=(uint8_t)(identity&7u);
    assert(clean_ht_ampdu_probe_sig(raw+2u,identity&7u));
    registers[0x1009u]=2u;
    for (uint32_t index=0u;index<count;++index) {
        assert(clean_ht_ampdu_probe_mpdu(raw+8u,nonce,index));
        int good=!(bad&(1u<<index));
        if (good) {
            assert(expected_count<1024u);
            memcpy(expected[expected_count++],raw+8u,80u);
        } else raw[87]^=1u;
        for (uint32_t i=0u;i<44u;++i)
            registers[CLEAN_SHM_FRAME+i]=(uint16_t)(raw[2u*i]|((uint16_t)raw[2u*i+1u]<<8u));
        if (!index) assert(psm_run(registers,0,AMPDU_IF_PREFIX)==148u);
        registers[0x100cu]=86u;
        registers[0x100du]=(uint16_t)(index+1u==count?0x4000u:0u);
        assert(psm_run(registers,0,good?AMPDU_TRACE_GOOD:AMPDU_TRACE_BAD)==
               (index+1u==count?AMPDU_SHM_WAIT:163u));
    }
    assert(registers[CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_REPEAT_WORD]==CLEAN_AMPDU_SHM_REPEAT_TAG);
    if (record) {
        fprintf(record,"{\"event\":\"publish\",\"identity\":%u,\"count\":%u,\"bad\":%u",
                identity,count,bad);
        record_state();
        record_probe();
        fputs(",\"outcomes\":[",record);
        for (uint32_t i=0u;i<count;++i)
            fprintf(record,"%s%u",i?",":"",
                    (unsigned)registers[CLEAN_AMPDU_IF_SHM+11u+8u*i]);
        fputs("]}\n",record);
    }
}

static void consume(void) {
    assert(multi.captured_frames!=test_rx_published_frames && consumed<expected_count);
    const fw_frame_slot_t *slot=&multi.frames[test_rx_published_frames&(FW_MAX_FRAMES-1u)];
    assert(slot->rxe_len==86u && clean_mac_valid(slot->raw+8u,80u));
    assert(!memcmp(slot->raw+8u,expected[consumed++],80u));
    ++test_rx_published_frames;
    if (record) {
        fputs("{\"event\":\"consume\"",record);
        record_state();
        record_frame(slot);
        fputs("}\n",record);
    }
}

static void receive_all(uint32_t count) {
    for (uint32_t i=0u;i<count;++i) assert(receive() && !error);
    assert(!registers[0x601u] && !test_ampdu_shm_copied && !pending_release);
}

static void repeated(void) {
    reset();
    expected_count=consumed=0u;
    record_boundary("begin","repeated");
    for (uint32_t batch=0u;batch<100u;++batch) {
        uint32_t count=1u+batch%3u, bad=batch&7u;
        publish(batch+1u,count,bad);
        for (uint32_t index=0u;index<count;++index) {
            assert(receive() && !error);
            while (test_rx_published_frames!=multi.captured_frames) consume();
        }
        assert(!registers[0x601u] && releases==batch+1u);
        assert(!test_ampdu_shm_copied && !test_ampdu_shm_next);
    }
    assert(consumed==expected_count && consumed>90u && barriers>=consumed+100u);
    record_boundary("end","repeated");
}

static void pressure(void) {
    reset();
    expected_count=consumed=0u;
    record_boundary("begin","pressure");
    for (uint32_t index=0u;index<FW_MAX_FRAMES;++index) {
        publish(index+1u,1u,0u);
        receive_all(1u);
    }
    fw_multi_t held=multi;
    publish(100u,3u,7u);
    receive_all(3u);
    assert(!memcmp(&held,&multi,sizeof(multi)));
    publish(101u,3u,0u);
    uint32_t released=releases;
    for (uint32_t i=0u;i<10u;++i)
        assert(receive() && !error &&
               registers[0x601u]==CLEAN_AMPDU_SHM_HELD_STATE && !test_ampdu_shm_next);
    assert(releases==released && !memcmp(&held,&multi,sizeof(multi)));
    for (uint32_t i=0u;i<3u;++i) {
        consume();
        assert(receive() && !error);
        assert(releases==released+(i==2u));
    }
    while (test_rx_published_frames!=multi.captured_frames) consume();
    assert(consumed==expected_count);
    assert(registers[CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_STATUS_WORD+5u]==FW_MAX_FRAMES+2u);
    record_boundary("end","pressure");
}

static void failures(void) {
    const uint32_t status=CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_STATUS_WORD;
    for (uint32_t fault=0u;fault<10u;++fault) {
        reset();
        expected_count=consumed=0u;
        publish(1u,1u,0u);
        if (fault==0u) registers[CLEAN_AMPDU_PROBE_SHM]=0u;
        if (fault==1u) registers[CLEAN_AMPDU_PROBE_SHM]=4u;
        if (fault==2u) registers[CLEAN_AMPDU_IF_SHM+1u]=2u;
        if (fault==3u) ++registers[status+2u];
        if (fault==4u) ++registers[status+3u];
        if (fault==5u) registers[CLEAN_AMPDU_PROBE_SHM+12u]^=1u;
        if (fault==6u) reject_pause=1;
        if (fault==7u) registers[CLEAN_AMPDU_IF_SHM]=2u;
        if (fault==8u) test_rx_published_frames=1u;
        if (fault==9u) registers[CLEAN_AMPDU_IF_SHM+11u]=0u;
        assert(!ampdu_shm_receive(3u,0u) && error && !multi.captured_frames && !releases);
        assert(registers[0x601u]==CLEAN_AMPDU_SHM_HELD_STATE);
    }
    for (uint32_t fault=1u;fault<=2u;++fault) {
        reset();
        expected_count=consumed=0u;
        release_fault=fault;
        publish(1u,1u,0u);
        assert(!ampdu_shm_receive(3u,0u) && error==0x8du);
        assert(multi.captured_frames==1u && !releases &&
               registers[0x601u]==CLEAN_AMPDU_SHM_HELD_STATE);
        consume();
    }
}

static void counter_wrap(void) {
    const uint32_t status=CLEAN_AMPDU_IF_SHM+CLEAN_AMPDU_SHM_STATUS_WORD;
    reset();
    expected_count=consumed=0u;
    test_ampdu_shm_lease=65534u;
    registers[status+2u]=registers[status+3u]=65534u;
    multi.captured_frames=test_rx_published_frames=UINT32_MAX-1u;
    record_boundary("begin","wrap");
    for (uint32_t i=0u;i<4u;++i) {
        publish(i+1u,1u,0u);
        receive_all(1u);
        consume();
    }
    assert(test_ampdu_shm_lease==2u && registers[status+2u]==2u &&
           registers[status+3u]==2u && multi.captured_frames==2u);
    record_boundary("end","wrap");
}

int main(int argc, char **argv) {
    if (argc!=1 && (argc!=3 || strcmp(argv[1],"--record-repeat"))) {
        fputs("Usage: aggregate_rx_test [--record-repeat OUTPUT.ndjson]\n",stderr);
        return 2;
    }
    assert(!psm_suite_main());
    failures();
    if (argc==3) {
        record=fopen(argv[2],"wb");
        if (!record) { perror(argv[2]); return 1; }
        fprintf(record,"{\"event\":\"header\",\"format\":\"clean-rx-repeat-model\","
                "\"version\":1,\"capacity\":%u,\"source\":\"controller-psm-model\"}\n",
                FW_MAX_FRAMES);
    }
    repeated();
    pressure();
    counter_wrap();
    if (record) {
        fputs("{\"event\":\"complete\"}\n",record);
        int failed=ferror(record);
        if (fclose(record)) failed=1;
        if (failed) { fputs("Repeated handoff transcript write failed\n",stderr); return 1; }
    }
    puts("D2 SHM runtime/PSM: 100 repeated leases, exact ring pressure, all-bad/no-credit, fresh byte delivery, malformed ownership and 16/32-bit wrap pass; NOT variable-length/RF qualification");
    return 0;
}
