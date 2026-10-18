#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "block_ack.h"
#include "../../src/shared/bytes.h"

static const uint8_t own[6] = {2u,1u,2u,3u,4u,5u};
static const uint8_t peer[6] = {2u,6u,7u,8u,9u,10u};
static clean_ba_rx_t state;
static clean_ampdu_stream_t stream;

typedef struct {
    uint16_t sequence[256];
    uint8_t tid[256];
    uint32_t count, blocked, fail, last_now;
} output_t;

static int sink(void *context, const uint8_t *mpdu, uint32_t length, uint32_t now) {
    output_t *out = context;
    if (out->fail) return -1;
    if (out->blocked) return 0;
    assert(out->count < 256u && clean_mac_valid(mpdu,length));
    out->sequence[out->count] = (uint16_t)(((uint16_t)mpdu[22] |
                                         ((uint16_t)mpdu[23] << 8u)) >> 4u);
    out->tid[out->count++] = mpdu[24] & 15u;
    out->last_now = now;
    return 1;
}

static void store16(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void fcs(uint8_t *m, uint32_t n) {
    badger_bytes_store_u32(m+n-4u,clean_mac_fcs(m,n-4u));
}

static void header(uint8_t *m, uint32_t size, uint8_t fc) {
    memset(m,0,size);
    m[0] = fc;
    memcpy(m+4u,own,6u);
    memcpy(m+10u,peer,6u);
    memcpy(m+16u,peer,6u);
}

static void request(uint8_t m[37], uint32_t tid, uint32_t start,
                     uint32_t window, uint32_t timeout) {
    header(m,37u,0xd0u);
    m[24] = 3u;
    m[26] = 77u;
    store16(m+27u,3u | (tid << 2u) | (window << 6u));
    store16(m+29u,timeout);
    store16(m+31u,start << 4u);
    fcs(m,37u);
}

static void open_tid_at(uint32_t tid, uint32_t start, uint32_t timeout, uint32_t now) {
    uint8_t m[37], response[37];
    request(m,tid,start,64u,timeout);
    assert(clean_ba_action(&state,m,sizeof(m)) == CLEAN_BA_PREPARED);
    assert(!state.tid[tid].active);
    assert(!clean_ba_addba_response(&state,tid,0u,response));
    assert(clean_ba_activate(&state,tid,now) == CLEAN_BA_ACCEPTED);
    assert(clean_ba_addba_response(&state,tid,12u,response) == sizeof(response));
    assert(response[0] == 0xd0u && !response[1] && !response[27] && !response[28]);
    assert(response[24] == 3u && response[25] == 1u && response[26] == 77u);
    assert((response[29] & 1u) == 0u);
    assert(((response[29] | ((uint32_t)response[30] << 8u)) >> 6u) == CLEAN_BA_WINDOW);
    assert(clean_mac_valid(response,sizeof(response)));
    assert(!memcmp(response+4u,peer,6u) && !memcmp(response+10u,own,6u));
}

static void open_tid(uint32_t tid, uint32_t start, uint32_t timeout) {
    open_tid_at(tid,start,timeout,0u);
}

static uint32_t data(uint8_t *m, uint32_t tid, uint32_t sequence, uint32_t size) {
    header(m,size,0x88u);
    m[1] = 2u;
    m[24] = (uint8_t)(tid | 0x60u);
    store16(m+22u,sequence << 4u);
    for (uint32_t i=26u;i<size-4u;++i) m[i]=(uint8_t)(i ^ sequence);
    fcs(m,size);
    return size;
}

static int push(uint32_t tid, uint32_t sequence, uint32_t now, output_t *out) {
    uint8_t m[64];
    return clean_ba_receive(&state,m,data(m,tid,sequence,sizeof(m)),now,sink,out);
}

static void bar(uint8_t m[24], uint32_t tid, uint32_t sequence) {
    header(m,24u,0x84u);
    /* BAR has a 16-byte MAC header, unlike management/data frames. */
    store16(m+16u,4u | (tid << 12u));
    store16(m+18u,sequence << 4u);
    fcs(m,24u);
}

static void agreement_tests(void) {
    uint8_t m[37], response[37];
    clean_ba_init(&state,own,peer);
    request(m,3u,4094u,64u,9u);
    assert(clean_ba_action(&state,m,37u) == CLEAN_BA_PREPARED);
    assert(clean_ba_action(&state,m,37u) == CLEAN_BA_PREPARED);
    m[26]++;
    fcs(m,37u);
    assert(clean_ba_action(&state,m,37u) == CLEAN_BA_CONFLICT);
    m[26]--;
    fcs(m,37u);
    assert(clean_ba_activate(&state,3u,42u) == CLEAN_BA_ACCEPTED);
    assert(clean_ba_activate(&state,3u,42u) == CLEAN_BA_INVALID);
    assert(clean_ba_action(&state,m,37u) == CLEAN_BA_ACCEPTED);
    assert(state.tid[3].head == 4094u && state.tid[3].timeout_us == 9216u);
    assert(clean_ba_addba_response(&state,3u,4096u,response) == 0u);
    assert(clean_ba_close(&state,16u) == CLEAN_BA_INVALID);
    assert(clean_ba_activate(&state,16u,0u) == CLEAN_BA_INVALID);
    assert(clean_ba_close(&state,3u) == CLEAN_BA_ACCEPTED);
    request(m,3u,4094u,64u,9u);
    m[27] &= (uint8_t)~2u;
    fcs(m,37u);
    assert(clean_ba_action(&state,m,37u) == CLEAN_BA_INVALID);
    m[27] |= 2u;
    m[31] |= 1u;
    fcs(m,37u);
    assert(clean_ba_action(&state,m,37u) == CLEAN_BA_INVALID);
    request(m,3u,4094u,64u,9u);
    for (uint32_t i=0u;i<37u;++i) {
        m[i] ^= 1u;
        assert(clean_ba_action(&state,m,37u) == CLEAN_BA_INVALID);
        m[i] ^= 1u;
    }
    for (uint32_t tid=0u;tid<16u;++tid) open_tid(tid,100u,0u);
    uint8_t delba[34];
    header(delba,34u,0xd0u);
    delba[24]=3u; delba[25]=2u;
    store16(delba+26u,7u << 12u);
    fcs(delba,sizeof(delba));
    assert(clean_ba_action(&state,delba,sizeof(delba)) == CLEAN_BA_IGNORED);
    assert(state.tid[7].active);
    store16(delba+26u,(7u << 12u) | 0x800u);
    fcs(delba,sizeof(delba));
    assert(clean_ba_action(&state,delba,sizeof(delba)) == CLEAN_BA_ACCEPTED);
    assert(state.tid[7].closing && state.tid[6].active);
    output_t out={0};
    assert(clean_ba_poll(&state,0u,sink,&out) == CLEAN_BA_ACCEPTED);
    assert(!state.tid[7].active && state.tid[6].active);
}

static void reorder_tests(void) {
    uint8_t response[32], m[64];
    output_t out={0};
    clean_ba_init(&state,own,peer);
    open_tid(5u,4094u,0u);
    assert(push(5u,0u,10u,&out) == CLEAN_BA_ACCEPTED);
    assert(push(5u,4095u,11u,&out) == CLEAN_BA_ACCEPTED && !out.count);
    assert(push(5u,0u,12u,&out) == CLEAN_BA_DUPLICATE);
    assert(push(5u,4094u,13u,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == 3u && out.sequence[0] == 4094u && out.sequence[1] == 4095u &&
           out.sequence[2] == 0u && state.tid[5].head == 1u);
    assert(clean_ba_response(&state,5u,response) == 32u);
    assert(clean_mac_valid(response,32u) && response[0] == 0x94u &&
           response[16] == 4u && response[17] == 0x50u);
    assert(response[18] == 0xe0u && response[19] == 0xffu && response[20] == 7u);
    assert(push(5u,4093u,14u,&out) == CLEAN_BA_OLD);
    assert(push(4u,0u,14u,&out) == CLEAN_BA_NO_AGREEMENT);
    uint32_t n=data(m,5u,2u,sizeof(m));
    m[24] |= 0x80u;
    fcs(m,n);
    assert(clean_ba_receive(&state,m,n,0u,sink,&out) == CLEAN_BA_INVALID);
    assert(push(5u,2u,15u,&out) == CLEAN_BA_ACCEPTED && out.count == 3u);
    assert(push(5u,12u,16u,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == 4u && out.sequence[3] == 2u && state.tid[5].head == 5u);
    assert(clean_ba_poll(&state,16u+CLEAN_BA_GAP_US,sink,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == 5u && out.sequence[4] == 12u && state.tid[5].head == 13u);
    assert(push(5u,100u,200000u,&out) == CLEAN_BA_ACCEPTED);
    assert(state.tid[5].bitmap_start == 37u);
    assert(clean_ba_response(&state,5u,response) == 32u && response[27] == 128u);
    assert(push(5u,99u,200001u,&out) == CLEAN_BA_ACCEPTED);
    assert(clean_ba_response(&state,5u,response) == 32u && response[27] == 192u);
    uint8_t b[24];
    bar(b,5u,100u);
    assert(clean_ba_bar(&state,b,sizeof(b),200002u,sink,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == 7u && out.sequence[5] == 99u && out.sequence[6] == 100u);
    assert(clean_ba_response(&state,5u,response) == 32u &&
           response[20] == 1u && state.tid[5].bitmap_start == 100u);
    b[16] |= 2u;
    fcs(b,sizeof(b));
    assert(clean_ba_bar(&state,b,sizeof(b),0u,sink,&out) == CLEAN_BA_INVALID);
    assert(clean_ba_bar(&state,b,sizeof(b)-1u,0u,sink,&out) == CLEAN_BA_INVALID);
    assert(!clean_ba_response(&state,16u,response));
}

static void pressure_tests(void) {
    output_t out={.blocked=1u};
    clean_ba_init(&state,own,peer);
    open_tid(0u,0u,0u);
    for (uint32_t seq=0u;seq<CLEAN_BA_WINDOW;++seq)
        assert(push(0u,seq,1u,&out) == CLEAN_BA_ACCEPTED);
    assert(!out.count && state.accepted == CLEAN_BA_WINDOW);
    assert(push(0u,CLEAN_BA_WINDOW,2u,&out) == CLEAN_BA_BUSY);
    assert(!(state.tid[0].bitmap[1] & 1u));
    out.blocked=0u;
    assert(clean_ba_poll(&state,3u,sink,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == CLEAN_BA_WINDOW);
    for (uint32_t i=0u;i<out.count;++i) assert(out.sequence[i] == i);
    assert(push(0u,CLEAN_BA_WINDOW,4u,&out) == CLEAN_BA_ACCEPTED);
    clean_ba_init(&state,own,peer);
    memset(&out,0,sizeof(out));
    out.blocked=1u;
    for (uint32_t tid=0u;tid<16u;++tid) {
        open_tid(tid,0u,0u);
        assert(push(tid,1u,0u,&out) == CLEAN_BA_ACCEPTED);
    }
    assert(push(0u,2u,1u,&out) == CLEAN_BA_BUSY);
    assert(state.tid[0].bitmap[0] == 2u);
    out.blocked=0u;
    assert(clean_ba_poll(&state,CLEAN_BA_GAP_US,sink,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == 16u);
    assert(push(0u,2u,CLEAN_BA_GAP_US+1u,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == 17u);
    out.fail=1u;
    assert(push(0u,3u,CLEAN_BA_GAP_US+2u,&out) == CLEAN_BA_SINK_ERROR);
    assert(state.slot[0].used);
    assert(clean_ba_close(&state,0u) == CLEAN_BA_ACCEPTED);
    for (uint32_t i=0u;i<sizeof(state.slot[0]);++i)
        assert(!((const uint8_t *)&state.slot[0])[i]);
}

static void timeout_tests(void) {
    output_t out={0};
    clean_ba_init(&state,own,peer);
    open_tid_at(3u,1u,1u,0xfffffe00u);
    assert(push(3u,2u,0xffffff00u,&out) == CLEAN_BA_ACCEPTED);
    assert(clean_ba_poll(&state,0x100u,sink,&out) == CLEAN_BA_ACCEPTED && !out.count);
    out.blocked=1u;
    assert(clean_ba_poll(&state,0x300u,sink,&out) == CLEAN_BA_BUSY && state.tid[3].active);
    out.blocked=0u;
    assert(clean_ba_poll(&state,0x301u,sink,&out) == CLEAN_BA_ACCEPTED);
    assert(out.count == 1u && out.sequence[0] == 2u && !state.tid[3].active);
    assert(state.expired == 1u);
    clean_ba_init(&state,own,peer);
    open_tid(3u,1u,1u);
    assert(push(3u,1u,1024u,&out)==CLEAN_BA_NO_AGREEMENT);
    assert(!state.tid[3].active && state.expired==1u);
    clean_ba_init(&state,own,peer);
    open_tid(3u,1u,1u);
    uint8_t message[64];
    bar(message,3u,1u);
    assert(clean_ba_bar(&state,message,24u,1024u,sink,&out)==
           CLEAN_BA_NO_AGREEMENT);
    assert(!state.tid[3].active);
}

static void delba_flush_tests(void) {
    uint8_t message[64];
    output_t out={0};
    clean_ba_init(&state,own,peer);
    open_tid(3u,10u,0u);
    assert(push(3u,12u,100u,&out)==CLEAN_BA_ACCEPTED);
    assert(push(3u,11u,200u,&out)==CLEAN_BA_ACCEPTED && !out.count);
    header(message,34u,0xd0u);
    message[24]=3u; message[25]=2u;
    store16(message+26u,(3u << 12u)|0x0800u);
    fcs(message,34u);
    assert(clean_ba_action(&state,message,34u)==CLEAN_BA_ACCEPTED);
    assert(push(3u,10u,201u,&out)==CLEAN_BA_NO_AGREEMENT);
    assert(!clean_ba_response(&state,3u,message));
    out.blocked=1u;
    assert(clean_ba_poll(&state,202u,sink,&out)==CLEAN_BA_BUSY);
    assert(state.tid[3].active && state.tid[3].closing && !out.count);
    out.blocked=0u;
    assert(clean_ba_poll(&state,203u,sink,&out)==CLEAN_BA_ACCEPTED);
    assert(!state.tid[3].active && out.count==2u &&
           out.sequence[0]==11u && out.sequence[1]==12u && out.last_now==203u);
}

static void permutation_tests(void) {
    uint32_t random=0x862f7139u;
    output_t out={0};
    clean_ba_init(&state,own,peer);
    open_tid(9u,4000u,0u);
    for (uint32_t round=0u;round<1024u;++round) {
        uint32_t order[8]={0u,1u,2u,3u,4u,5u,6u,7u};
        for (uint32_t i=7u;i>0u;--i) {
            random=random*1664525u+1013904223u;
            uint32_t j=random%(i+1u), saved=order[i];
            order[i]=order[j]; order[j]=saved;
        }
        out.count=0u;
        for (uint32_t i=0u;i<8u;++i) {
            uint32_t sequence=(4000u+round*8u+order[i])&4095u;
            assert(push(9u,sequence,round*100u+i,&out)==CLEAN_BA_ACCEPTED);
            assert(push(9u,sequence,round*100u+i,&out)==CLEAN_BA_DUPLICATE);
        }
        assert(out.count==8u);
        for (uint32_t i=0u;i<8u;++i)
            assert(out.sequence[i]==((4000u+round*8u+i)&4095u));
        assert(state.tid[9].head==((4000u+(round+1u)*8u)&4095u));
    }
    assert(state.delivered==8192u && state.duplicates==8192u);
}

static uint32_t append(uint8_t *wire, uint32_t at, const uint8_t *mpdu, uint32_t length,
                        int padded) {
    store16(wire+at,length << 4u);
    wire[at+2u]=clean_ampdu_delimiter_crc(wire+at);
    wire[at+3u]=0x4eu;
    if (length) memcpy(wire+at+4u,mpdu,length);
    at+=4u+length;
    if (padded) while (at & 3u) wire[at++]=0u;
    return at;
}

static void stream_tests(void) {
    static uint8_t wire[CLEAN_AMPDU_BYTES], mpdu[CLEAN_MPDU_BYTES];
    output_t out;
    uint32_t sizes[3]={31u,CLEAN_MPDU_BYTES,CLEAN_MPDU_BYTES};
    uint32_t at=append(wire,0u,0,0u,1);
    for (uint32_t i=0u;i<3u;++i) {
        data(mpdu,0u,i,sizes[i]);
        at=append(wire,at,mpdu,sizes[i],i!=2u);
    }
    assert(at <= CLEAN_AMPDU_BYTES && at > 2u*CLEAN_MPDU_BYTES);
    /* Independently reverse the polynomial long-division remainder. */
    for (uint32_t value=0u;value<65536u;value+=17u) {
        uint8_t d[2]={(uint8_t)value,(uint8_t)(value >> 8u)};
        uint8_t normal=255u;
        for (uint32_t bit=0u;bit<16u;++bit) {
            uint8_t input=(uint8_t)((value >> bit)&1u);
            uint8_t top=(uint8_t)((normal >> 7u)^input);
            normal=(uint8_t)((normal << 1u)^(top?7u:0u));
        }
        uint8_t reversed=0u;
        for (uint32_t bit=0u;bit<8u;++bit)
            reversed|=(uint8_t)(((normal >> bit)&1u) << (7u-bit));
        assert(clean_ampdu_delimiter_crc(d) == (uint8_t)~reversed);
    }
    const uint32_t chunks[]={1u,2u,3u,4u,7u,63u,128u,1536u,CLEAN_AMPDU_BYTES};
    for (uint32_t c=0u;c<sizeof(chunks)/sizeof(chunks[0]);++c) {
        memset(&out,0,sizeof(out));
        assert(clean_ampdu_begin(&stream,at) == CLEAN_BA_ACCEPTED);
        for (uint32_t offset=0u;offset<at;) {
            uint32_t count=at-offset < chunks[c] ? at-offset : chunks[c], used=0u;
            assert(clean_ampdu_feed(&stream,wire+offset,count,&used,sink,&out,1u) ==
                   CLEAN_BA_ACCEPTED && used == count);
            offset+=used;
        }
        assert(clean_ampdu_end(&stream) == CLEAN_BA_ACCEPTED);
        assert(out.count == 3u && stream.frames == 3u && stream.null_delimiters == 1u);
        for (uint32_t i=0u;i<3u;++i) assert(out.sequence[i] == i);
    }
    memset(&out,0,sizeof(out));
    out.blocked=1u;
    assert(clean_ampdu_begin(&stream,at) == CLEAN_BA_ACCEPTED);
    uint32_t used, offset=0u;
    assert(clean_ampdu_feed(&stream,wire,at,&used,sink,&out,1u) == CLEAN_BA_BUSY);
    offset=used;
    assert(offset == 4u+4u+31u && !out.count);
    out.blocked=0u;
    assert(clean_ampdu_feed(&stream,wire+offset,at-offset,&used,sink,&out,2u) ==
           CLEAN_BA_ACCEPTED);
    assert(clean_ampdu_end(&stream) == CLEAN_BA_ACCEPTED && out.count == 3u);
    wire[12u] ^= 1u;
    memset(&out,0,sizeof(out));
    assert(clean_ampdu_begin(&stream,at) == CLEAN_BA_ACCEPTED);
    assert(clean_ampdu_feed(&stream,wire,at,&used,sink,&out,1u) == CLEAN_BA_ACCEPTED);
    assert(clean_ampdu_end(&stream) == CLEAN_BA_ACCEPTED && stream.bad_fcs == 1u &&
           out.count == 2u);
    wire[12u] ^= 1u;
    for (uint32_t i=0u;i<4u;++i) {
        wire[i]^=1u;
        assert(clean_ampdu_begin(&stream,at) == CLEAN_BA_ACCEPTED);
        assert(clean_ampdu_feed(&stream,wire,at,&used,sink,&out,1u) == CLEAN_BA_INVALID);
        assert(clean_ampdu_end(&stream) == CLEAN_BA_INVALID);
        wire[i]^=1u;
    }
    assert(clean_ampdu_begin(&stream,at) == CLEAN_BA_ACCEPTED);
    assert(clean_ampdu_feed(&stream,wire,at-1u,&used,sink,&out,1u) == CLEAN_BA_ACCEPTED);
    assert(clean_ampdu_end(&stream) == CLEAN_BA_INVALID);
    assert(clean_ampdu_begin(&stream,CLEAN_AMPDU_BYTES+1u) == CLEAN_BA_INVALID);
    assert(clean_ampdu_begin(&stream,3u) == CLEAN_BA_INVALID);
    /* Resume an end-of-input MPDU after delivery backpressure. */
    uint32_t single=append(wire,0u,mpdu,data(mpdu,0u,9u,31u),0);
    memset(&out,0,sizeof(out)); out.blocked=1u;
    assert(clean_ampdu_begin(&stream,single) == CLEAN_BA_ACCEPTED);
    assert(clean_ampdu_feed(&stream,wire,single,&used,sink,&out,0u) == CLEAN_BA_BUSY);
    assert(used == single && clean_ampdu_end(&stream) == CLEAN_BA_INVALID);
    out.blocked=0u;
    assert(clean_ampdu_feed(&stream,0,0u,&used,sink,&out,1u) == CLEAN_BA_ACCEPTED);
    assert(out.count == 1u && clean_ampdu_end(&stream) == CLEAN_BA_ACCEPTED);
    /* The mandatory exponent-zero aggregate bound, not merely two MPDUs. */
    at=0u;
    for (uint32_t i=0u;i<128u;++i)
        at=append(wire,at,mpdu,data(mpdu,0u,i,59u),i<127u);
    assert(at==8191u);
    memset(&out,0,sizeof(out));
    assert(clean_ampdu_begin(&stream,at)==CLEAN_BA_ACCEPTED);
    for (uint32_t offset=0u;offset<at;) {
        uint32_t chunk=at-offset<1536u?at-offset:1536u;
        assert(clean_ampdu_feed(&stream,wire+offset,chunk,&used,sink,&out,0u)==
               CLEAN_BA_ACCEPTED && used==chunk);
        offset+=used;
    }
    assert(clean_ampdu_end(&stream)==CLEAN_BA_ACCEPTED &&
           out.count==128u && stream.frames==128u);
}

int main(void) {
    agreement_tests();
    reorder_tests();
    pressure_tests();
    timeout_tests();
    delba_flush_tests();
    permutation_tests();
    stream_tests();
    puts("Block ACK agreement, bitmap, bounded reordering and aggregate stream tests passed");
    return 0;
}
