#include "experiment.h"
#include "usb/report.h"
#include "experiment_page.h"

void experiment_init(experiment_t *lab) {
    link_profile_default(&lab->profile);
    lab->revision = 1u;
    lab->guard_ms = HEALTH_MIN_MS;
}

int32_t experiment_configure(experiment_t *lab, sx1262_receiver_t *rx) {
    lab->config_error = link_profile_configure(rx, &lab->profile);
    return lab->config_error;
}

uint32_t experiment_wait(const experiment_t *lab, uint32_t now) {
    uint32_t elapsed = now - lab->last_tx_ms;
    return elapsed < lab->guard_ms ? lab->guard_ms - elapsed : 0u;
}

void experiment_charge(experiment_t *lab, uint32_t now) {
    uint32_t guard = link_profile_timeout_ms(&lab->profile) * 100u;
    lab->guard_ms = guard < HEALTH_MIN_MS ? HEALTH_MIN_MS : guard;
    lab->last_tx_ms = now;
}

void experiment_tick(experiment_t *lab, health_t *h, sx1262_receiver_t *rx, uint32_t now) {
    if (!lab->queued || experiment_wait(lab, now) || rx->state != SX1262_RX_RUNNING) return;
    uint8_t nonce[16], payload[LINK_PACKET_SIZE];
    usb_store_u32(nonce, EXPERIMENT_MAGIC);
    usb_store_u32(nonce+4u, lab->token);
    usb_store_u32(nonce+8u, ++lab->ordinal);
    usb_store_u32(nonce+12u, lab->batch_count);
    link_test_payload(payload, h->id, ++lab->test_sequence, nonce);
    experiment_charge(lab, now);
    ++lab->attempts;
    --lab->queued;
    lab->tx_error = link_profile_send(rx, &lab->profile, payload, &lab->restore_error);
    if (!lab->tx_error) ++lab->sent;
    if (lab->tx_error || lab->restore_error) lab->queued = 0u;
}

void experiment_observe(experiment_t *lab, const sx1262_rx_packet_t *p, uint32_t now) {
    if (p->sequence == lab->packet_cursor) return;
    lab->packet_cursor = p->sequence;
    if (p->flags & SX1262_RX_CRC_ERROR) return;
    if (p->length != LINK_PACKET_SIZE || usb_load_u32(p->payload) != LINK_PACKET_MAGIC ||
        usb_load_u32(p->payload+16u) != EXPERIMENT_MAGIC) return;
    if (usb_load_u32(p->payload+32u) != usb_crc32(p->payload,32u)) { ++lab->malformed; return; }
    uint32_t token = usb_load_u32(p->payload+20u), ordinal = usb_load_u32(p->payload+24u);
    uint32_t count = usb_load_u32(p->payload+28u), low = usb_load_u32(p->payload+4u);
    uint32_t high = usb_load_u32(p->payload+8u);
    if (!token || !count || count > 3u || !ordinal || ordinal > count) { ++lab->malformed; return; }
    experiment_run_t *run = 0;
    for (uint32_t i = 0u; i < lab->run_count; ++i) {
        experiment_run_t *candidate = &lab->runs[i];
        if (candidate->sender[0] == low && candidate->sender[1] == high && candidate->token == token &&
            candidate->profile.radio.frequency_hz == lab->profile.radio.frequency_hz &&
            candidate->profile.radio.spreading_factor == lab->profile.radio.spreading_factor &&
            candidate->profile.radio.bandwidth_khz == lab->profile.radio.bandwidth_khz &&
            candidate->profile.coding_rate == lab->profile.coding_rate &&
            candidate->profile.preamble == lab->profile.preamble) {
            run = candidate; break;
        }
    }
    if (!run) {
        run = &lab->runs[lab->run_head];
        uint8_t *bytes = (uint8_t *)run;
        for (uint32_t i = 0u; i < sizeof(*run); ++i) bytes[i] = 0u;
        lab->run_head = (lab->run_head+1u) % EXPERIMENT_RUNS;
        if (lab->run_count < EXPERIMENT_RUNS) ++lab->run_count;
        run->sender[0] = low; run->sender[1] = high; run->token = token; run->expected = count;
        run->profile = lab->profile;
    }
    if (run->expected != count) { ++lab->malformed; return; }
    uint32_t bit = 1u << (ordinal-1u);
    if (run->mask & bit) ++run->duplicates;
    run->mask |= bit;
    run->last_ms = now; run->rssi_qdb = p->rssi_qdb; run->snr_qdb = p->snr_qdb;
}

static int equal(const char *a, const char *b) {
    uint32_t i = 0u;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}

static int parse(const http_request_t *r, const char *const *keys, uint32_t count, uint32_t *values) {
    uint32_t at = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        if (i && (at >= r->body_size || r->body[at++] != '&')) return 0;
        for (uint32_t j = 0u; keys[i][j]; ++j)
            if (at >= r->body_size || r->body[at++] != (uint8_t)keys[i][j]) return 0;
        uint32_t value = 0u, digits = 0u;
        while (at < r->body_size && r->body[at] != '&') {
            uint8_t c = r->body[at++];
            if (c < '0' || c > '9' || ++digits > 10u ||
                value > (0xffffffffu - (uint32_t)(c-'0')) / 10u) return 0;
            value = value*10u + c-'0';
        }
        if (!digits) return 0;
        values[i] = value;
    }
    return at == r->body_size;
}

static uint32_t text(uint8_t *r, uint32_t n, const char *s) {
    while (*s) r[n++] = (uint8_t)*s++;
    return n;
}
static uint32_t number(uint8_t *r, uint32_t n, uint32_t v) {
    uint8_t digits[10]; uint32_t count = 0u;
    do { digits[count++] = (uint8_t)('0'+v%10u); v/=10u; } while (v);
    while (count) r[n++] = digits[--count];
    return n;
}
static uint32_t field(uint8_t *r, uint32_t n, const char *key, uint32_t v) {
    return number(r,text(r,n,key),v);
}
static uint32_t signed_field(uint8_t *r,uint32_t n,const char *key,int32_t v) {
    n=text(r,n,key);
    if (v<0) { r[n++]='-'; return number(r,n,0u-(uint32_t)v); }
    return number(r,n,(uint32_t)v);
}
static uint32_t profile_json(uint8_t *r,uint32_t n,const link_profile_t *p) {
    n=field(r,n,"{\"frequency_hz\":",p->radio.frequency_hz);
    n=field(r,n,",\"sf\":",p->radio.spreading_factor);
    n=field(r,n,",\"bandwidth_khz\":",p->radio.bandwidth_khz);
    n=field(r,n,",\"coding_rate\":",p->coding_rate);
    n=field(r,n,",\"preamble\":",p->preamble);
    return text(r,n,",\"power_dbm\":0,\"sync_word\":18}");
}
static uint32_t error_response(uint8_t *out,uint32_t capacity,uint32_t code,const char *message) {
    uint32_t size=0u;
    while (message[size]) ++size;
    return http_response(out,capacity,code,"application/json",(const uint8_t *)message,size);
}

uint32_t experiment_http(experiment_t *lab, health_t *h, sx1262_receiver_t *rx,
                         const http_request_t *request, uint8_t *out, uint32_t capacity) {
    if (!request->post && equal(request->path,"/"))
        return http_response(out,capacity,200u,"text/html; charset=utf-8",experiment_page,sizeof(experiment_page)-1u);
    if (!request->post && equal(request->path,"/radio.js"))
        return http_response(out,capacity,200u,"text/javascript",experiment_script,sizeof(experiment_script)-1u);
    uint8_t config=equal(request->path,"/api/radio/config"), reset=equal(request->path,"/api/radio/reset");
    uint8_t send=equal(request->path,"/api/radio/send"), cancel=equal(request->path,"/api/radio/cancel");
    if (request->post) {
        if (!request->write_allowed)
            return error_response(out,capacity,403u,"{\"error\":\"Missing X-Pico-Request: 1\"}");
        if (!config && !reset && !send && !cancel)
            return error_response(out,capacity,404u,"{\"error\":\"Unknown radio command\"}");
        if ((reset || cancel) && request->body_size)
            return error_response(out,capacity,400u,"{\"error\":\"Command body must be empty\"}");
        if (cancel) lab->queued=0u;
        else if (lab->queued)
            return error_response(out,capacity,409u,"{\"error\":\"Cancel queued test before changing settings or starting another\"}");
        else if (send) {
            static const char *keys[]={"count=","token="};
            uint32_t v[2];
            if (!parse(request,keys,2u,v) || !v[0] || v[0]>3u || !v[1])
                return error_response(out,capacity,400u,"{\"error\":\"Use count=1..3&token=1..4294967295\"}");
            if (rx->state != SX1262_RX_RUNNING)
                return error_response(out,capacity,503u,"{\"error\":\"Radio is not receiving; restore a profile first\"}");
            lab->queued=lab->batch_count=v[0]; lab->token=v[1]; lab->ordinal=0u;
            lab->tx_error=lab->restore_error=0;
        } else {
            link_profile_t next;
            link_profile_default(&next);
            if (config) {
                static const char *keys[]={"frequency_hz=","sf=","bandwidth_khz=","coding_rate=","preamble="};
                uint32_t v[5];
                if (!parse(request,keys,5u,v))
                    return error_response(out,capacity,400u,"{\"error\":\"Malformed radio configuration\"}");
                next.radio.frequency_hz=v[0]; next.radio.spreading_factor=v[1];
                next.radio.bandwidth_khz=v[2]; next.coding_rate=v[3]; next.preamble=v[4];
            }
            if (link_profile_validate(&next))
                return error_response(out,capacity,400u,"{\"error\":\"Invalid profile: occupied bandwidth must fit 868.0-868.6 or 869.7-870.0 MHz; SF7-12, BW125/250/500, CR1-4, preamble8-32\"}");
            lab->config_error=link_profile_configure(rx,&next);
            if (lab->config_error) {
                lab->restore_error=link_profile_configure(rx,&lab->profile);
                return error_response(out,capacity,503u,"{\"error\":\"Configuration failed; inspect config_error and restore_error\"}");
            }
            lab->profile=next; ++lab->revision; lab->restore_error=0;
            h->pending=1u;
        }
    } else if (!equal(request->path,"/api/radio"))
        return error_response(out,capacity,404u,"{\"error\":\"Not found\"}");
    uint8_t *r=lab->json;
    uint32_t n=field(r,0u,"{\"revision\":",lab->revision);
    n=text(r,n,",\"config\":"); n=profile_json(r,n,&lab->profile);
    n=field(r,n,",\"airtime_us\":",link_profile_airtime_us(&lab->profile));
    n=field(r,n,",\"timeout_ms\":",link_profile_timeout_ms(&lab->profile));
    n=field(r,n,",\"wait_ms\":",experiment_wait(lab,h->uptime_ms));
    n=field(r,n,",\"queued\":",lab->queued);
    n=field(r,n,",\"batch_count\":",lab->batch_count);
    n=field(r,n,",\"token\":",lab->token);
    n=field(r,n,",\"test_attempts\":",lab->attempts);
    n=field(r,n,",\"test_tx_done\":",lab->sent);
    n=signed_field(r,n,",\"config_error\":",lab->config_error);
    n=signed_field(r,n,",\"tx_error\":",lab->tx_error);
    n=signed_field(r,n,",\"restore_error\":",lab->restore_error);
    n=field(r,n,",\"malformed_test_packets\":",lab->malformed);
    n=text(r,n,",\"runs\":[");
    for (uint32_t i=0u;i<lab->run_count;++i) {
        const experiment_run_t *run=&lab->runs[(lab->run_head+EXPERIMENT_RUNS-1u-i)%EXPERIMENT_RUNS];
        if (i) r[n++]=',';
        n=field(r,n,"{\"sender_low\":",run->sender[0]); n=field(r,n,",\"sender_high\":",run->sender[1]);
        n=field(r,n,",\"token\":",run->token); n=field(r,n,",\"expected\":",run->expected);
        uint32_t received=0u;
        for (uint32_t bit=0u;bit<3u;++bit) if (run->mask&(1u<<bit)) ++received;
        n=field(r,n,",\"received_unique\":",received);
        n=field(r,n,",\"not_yet_observed\":",run->expected-received);
        n=field(r,n,",\"duplicates\":",run->duplicates);
        n=field(r,n,",\"last_rx_uptime_ms\":",run->last_ms);
        n=signed_field(r,n,",\"rssi_qdb\":",run->rssi_qdb);
        n=signed_field(r,n,",\"snr_qdb\":",run->snr_qdb);
        n=text(r,n,",\"config\":"); n=profile_json(r,n,&run->profile); r[n++]='}';
    }
    n=text(r,n,"]}");
    return http_response(out,capacity,200u,"application/json",r,n);
}
