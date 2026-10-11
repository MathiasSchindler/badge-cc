#include "wifi_udp_report.h"
#include "../tools/diagnostics_logic.h"
#include "../shared/bytes.h"

static void store(uint8_t *bytes, uint32_t value) {
    badger_bytes_store_u32(bytes, value);
}

void wifi_udp_report_encode(uint8_t bytes[64], uint32_t build, const uint8_t nonce[16],
                            const wifi_udp_t *net, const wifi_udp_metrics_t *metrics) {
    usb_report_header(bytes,0x55363255u,build,nonce,4u);
    bytes[32]=(uint8_t)net->stage;
    bytes[33]=(uint8_t)net->error;
    bytes[34]=net->role;
    bytes[35]=net->arp_ready;
    store(bytes+36u,net->sent | net->received<<16u);
    store(bytes+40u,net->lost | net->echoed<<16u);
    store(bytes+44u,net->dropped);
    store(bytes+48u,net->last_rtt);
    store(bytes+52u,metrics->calibration_ticks);
    store(bytes+56u,metrics->elapsed_ticks);
    store(bytes+60u,diag_crc32(bytes,60u));
}

usb_record_result_t wifi_udp_report_decode(const uint8_t bytes[64], uint32_t build,
                                           const uint8_t nonce[16], uint8_t role,
                                           wifi_udp_summary_t *out) {
    if (bytes==0 || nonce==0 || out==0 || role>1u) return USB_RECORD_LENGTH;
    if (usb_report_u32(bytes)!=0x55363255u) return USB_RECORD_MAGIC;
    if (bytes[4]!=1u || bytes[5]!=0u) return USB_RECORD_VERSION;
    if (bytes[6]!=64u || bytes[7]!=0u) return USB_RECORD_LENGTH;
    if (usb_report_u32(bytes+60u)!=diag_crc32(bytes,60u)) return USB_RECORD_CRC;
    if (usb_report_u32(bytes+8u)!=build) return USB_RECORD_BUILD;
    if (usb_report_u32(bytes+12u)!=4u) return USB_RECORD_TEST;
    for (uint32_t i=0u;i<16u;++i) if (bytes[16u+i]!=nonce[i]) return USB_RECORD_NONCE;
    if (bytes[32]>WIFI_UDP_FAILED || bytes[33]>WIFI_UDP_RADIO_ERROR ||
        bytes[34]!=role || bytes[35]>1u ||
        (bytes[33]!=0u && bytes[32]!=WIFI_UDP_FAILED)) return USB_RECORD_PAYLOAD;
    out->stage=bytes[32]; out->error=bytes[33]; out->role=bytes[34]; out->arp_ready=bytes[35];
    uint32_t counts=usb_report_u32(bytes+36u), results=usb_report_u32(bytes+40u);
    out->sent=counts&0xffffu; out->received=counts>>16u;
    out->lost=results&0xffffu; out->echoed=results>>16u;
    out->dropped=usb_report_u32(bytes+44u);
    out->last_rtt=usb_report_u32(bytes+48u);
    out->calibration_ticks=usb_report_u32(bytes+52u);
    out->elapsed_ticks=usb_report_u32(bytes+56u);
    if (out->sent>WIFI_UDP_SAMPLES || out->received+out->lost>out->sent ||
        (out->stage==WIFI_UDP_DONE && (out->sent!=WIFI_UDP_SAMPLES ||
         out->received+out->lost!=WIFI_UDP_SAMPLES || !out->arp_ready || out->error))) return USB_RECORD_PAYLOAD;
    return USB_RECORD_OK;
}

void wifi_udp_detail_encode(uint8_t bytes[WIFI_UDP_DETAIL_SIZE],
                            const wifi_udp_t *net, const wifi_udp_metrics_t *metrics) {
    badger_bytes_zero(bytes,WIFI_UDP_DETAIL_SIZE);
    store(bytes,0x31504455u);
    bytes[4]=net->role; bytes[5]=(uint8_t)net->stage; bytes[6]=(uint8_t)net->error; bytes[7]=net->arp_ready;
    const uint32_t values[] = {WIFI_UDP_SAMPLES,net->sent,net->received,net->lost,net->echoed,
        net->dropped,net->duplicates,net->arp_tx,net->arp_rx,metrics->calibration_ticks,
        metrics->calibration_frames,metrics->elapsed_ticks,metrics->tx_frames,metrics->rx_frames,
        metrics->radio_drops,net->received?net->rtt_min:0u,net->rtt_max,net->rtt_sum,
        (uint32_t)net->last_drop,net->ip,net->peer_ip};
    for (uint32_t i=0u;i<sizeof(values)/sizeof(values[0]);++i) store(bytes+8u+i*4u,values[i]);
    for (uint32_t i=0u;i<WIFI_UDP_SAMPLES;++i) store(bytes+WIFI_UDP_SAMPLE_OFFSET+i*4u,net->samples[i]);
}

usb_record_result_t wifi_udp_detail_check(const uint8_t bytes[WIFI_UDP_DETAIL_SIZE],
                                         const wifi_udp_summary_t *s) {
    if(bytes==0 || s==0)return USB_RECORD_LENGTH;
    if (usb_report_u32(bytes)!=0x31504455u) return USB_RECORD_MAGIC;
    if (bytes[4]!=s->role || bytes[5]!=s->stage || bytes[6]!=s->error || bytes[7]!=s->arp_ready ||
        usb_report_u32(bytes+8u)!=WIFI_UDP_SAMPLES || usb_report_u32(bytes+12u)!=s->sent ||
        usb_report_u32(bytes+16u)!=s->received || usb_report_u32(bytes+20u)!=s->lost ||
        usb_report_u32(bytes+24u)!=s->echoed || usb_report_u32(bytes+28u)!=s->dropped ||
        usb_report_u32(bytes+44u)!=s->calibration_ticks || usb_report_u32(bytes+52u)!=s->elapsed_ticks ||
        usb_report_u32(bytes+48u)<128u || usb_report_u32(bytes+48u)>256u ||
        s->calibration_ticks==0u) return USB_RECORD_PAYLOAD;
    uint32_t count=0u,sum=0u,min=0xffffffffu,max=0u;
    for (uint32_t i=0u;i<WIFI_UDP_SAMPLES;++i) {
        uint32_t value=usb_report_u32(bytes+WIFI_UDP_SAMPLE_OFFSET+i*4u);
        if (value>WIFI_UDP_TIMEOUT_TICKS || (i>=s->sent && value!=0u)) return USB_RECORD_PAYLOAD;
        if (value) { ++count; sum+=value; if(value<min)min=value; if(value>max)max=value; }
    }
    if (!count) min=0u;
    if (count!=s->received || usb_report_u32(bytes+68u)!=min ||
        usb_report_u32(bytes+72u)!=max || usb_report_u32(bytes+76u)!=sum) return USB_RECORD_PAYLOAD;
    for (uint32_t i=92u;i<WIFI_UDP_SAMPLE_OFFSET;++i) if(bytes[i]) return USB_RECORD_RESERVED;
    return USB_RECORD_OK;
}

void wifi_udp_page(uint8_t out[64], const uint8_t detail[WIFI_UDP_DETAIL_SIZE],
                    const uint8_t report[64], uint8_t page) {
    for (uint32_t i=0u;i<64u;++i) out[i]=0u;
    if (page>=WIFI_UDP_DETAIL_PAGES) return;
    for (uint32_t i=0u;i<32u;++i) out[i]=report[i];
    out[3]='T';
    uint32_t offset=(uint32_t)page*24u, length=WIFI_UDP_DETAIL_SIZE-offset;
    if(length>24u)length=24u;
    out[32]=page; out[33]=WIFI_UDP_DETAIL_PAGES; out[34]=(uint8_t)length;
    for (uint32_t i=0u;i<length;++i) out[36u+i]=detail[offset+i];
    store(out+60u,diag_crc32(out,60u));
}

usb_record_result_t wifi_udp_page_decode(uint8_t detail[WIFI_UDP_DETAIL_SIZE],
                                         const uint8_t page[64], const uint8_t report[64],
                                         uint8_t expected_page) {
    if (detail==0 || page==0 || report==0 || expected_page>=WIFI_UDP_DETAIL_PAGES) return USB_RECORD_LENGTH;
    if (usb_report_u32(page)!=0x54363255u) return USB_RECORD_MAGIC;
    if (usb_report_u32(page+60u)!=diag_crc32(page,60u)) return USB_RECORD_CRC;
    for (uint32_t i=4u;i<32u;++i) if(page[i]!=report[i]) return USB_RECORD_NONCE;
    uint32_t offset=(uint32_t)expected_page*24u, length=WIFI_UDP_DETAIL_SIZE-offset;
    if(length>24u)length=24u;
    if(page[32]!=expected_page || page[33]!=WIFI_UDP_DETAIL_PAGES ||
       page[34]!=length || page[35]!=0u) return USB_RECORD_PAYLOAD;
    for (uint32_t i=length;i<24u;++i) if(page[36u+i]) return USB_RECORD_RESERVED;
    for (uint32_t i=0u;i<length;++i) detail[offset+i]=page[36u+i];
    return USB_RECORD_OK;
}
