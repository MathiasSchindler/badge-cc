#include <assert.h>
#include <stdio.h>
#include "link.h"
#include "screen.h"

static uint16_t lcd[320u*240u];
static uint32_t lcd_x,lcd_width,lcd_position;
void badger_display_begin_region(uint32_t x,uint32_t y,uint32_t width,uint32_t height) {
    assert(y==0u && height==240u && x+width<=320u);
    lcd_x=x; lcd_width=width; lcd_position=0u;
}
void badger_display_write_pixels(const uint16_t *pixels,uint32_t count) {
    assert(lcd_position+count<=lcd_width*240u);
    for (uint32_t i=0u; i<count; ++i) {
        uint32_t column=lcd_x+lcd_position/240u,row=lcd_position%240u;
        lcd[column*240u+row]=pixels[i];
        ++lcd_position;
    }
}

static void round_trip(uint8_t role, uint8_t buttons, uint32_t start) {
    msg_link_t a,b;
    msg_init(&a,role,0x12345678u);
    msg_init(&b,(uint8_t)(3u-role),0x98765432u);
    assert(msg_enqueue(&a,buttons,MSG_USB,0xfeed0123u,start));
    uint8_t data[MSG_MPDU], ack[MSG_MPDU], tx[MSG_STORAGE];
    assert(msg_next(&a,start,data));
    msg_transmitted(&a,data,start+100u);
    assert(msg_template(tx,data));
    assert(msg_receive(&b,data,sizeof(data),start+1000u)==1);
    assert(b.peer_buttons==buttons && b.last_event.token==0xfeed0123u);
    uint32_t ack_at=start+1000u+MSG_ACK_DELAY_US;
    assert(!msg_next(&b,ack_at-1u,ack));
    assert(msg_next(&b,ack_at,ack));
    msg_transmitted(&b,ack,ack_at+100u);
    msg_displayed(&b,start+30000u);
    assert(msg_receive(&a,ack,sizeof(ack),ack_at+1000u)==3);
    assert(!a.active && a.rx_ack==1u && a.last_acked.token==0xfeed0123u);
    assert(a.last_timing.first_tx_at==start && a.last_timing.first_done_at==start+100u);
    assert(a.last_timing.completed_at-a.last_timing.first_tx_at==2000u+MSG_ACK_DELAY_US);
    assert(a.last_timing.queued_at==start && a.last_timing.attempts==1u);
    assert(b.receipt.ack_selected_at-b.receipt.received_at==MSG_ACK_DELAY_US);
    assert(b.receipt.flags==15u && b.receipt.displayed_at==start+30000u);
    assert(!msg_receive(&a,ack,sizeof(ack),start+23000u));
    assert(!msg_receive(&a,data,sizeof(data),start+24000u));
    assert(msg_receive(&b,data,sizeof(data),start+25000u)==2);
    assert(b.rx_data==1u && b.duplicates==1u);
}

int main(void) {
    static badger_canvas_t image;
    for (uint32_t i=0u; i<320u*240u; ++i) image.pixels[i]=(uint16_t)(i^(i>>16u));
    for (uint32_t x=0u; x<320u; x+=4u) msg_display_columns(&image,x,4u,0);
    assert(badger_bytes_equal((uint8_t *)lcd,(uint8_t *)image.pixels,sizeof(lcd)));
    badger_bytes_zero((uint8_t *)lcd,sizeof(lcd));
    msg_display_columns(&image,0u,320u,0);
    assert(badger_bytes_equal((uint8_t *)lcd,(uint8_t *)image.pixels,sizeof(lcd)));
    for (uint8_t role=1u; role<=2u; ++role)
        for (uint8_t buttons=0u; buttons<32u; ++buttons) {
            round_trip(role,buttons,1000u);
            round_trip(role,buttons,0xffffd000u);
        }
    msg_link_t a,b;
    uint8_t data[MSG_MPDU], ack[MSG_MPDU], damaged[MSG_MPDU];
    msg_frame_t parsed;
    msg_init(&a,1u,1u); msg_init(&b,2u,2u);
    assert(msg_enqueue(&a,4u,MSG_BUTTON,0u,0u));
    assert(msg_next(&a,0u,data));
    for (uint32_t i=0u; i<MSG_MPDU; ++i) {
        badger_bytes_copy(damaged,data,sizeof(data)); damaged[i]^=1u;
        assert(!msg_decode(&parsed,damaged,sizeof(damaged)));
    }
    assert(!msg_decode(&parsed,data,MSG_MPDU-1u));
    assert(!msg_decode(&parsed,data,MSG_MPDU+1u));
    badger_bytes_copy(damaged,data,sizeof(data)); damaged[60]=1u;
    badger_bytes_store_u32(damaged+76u,clean_mac_fcs(damaged,76u));
    assert(!msg_decode(&parsed,damaged,sizeof(damaged)));
    assert(msg_receive(&b,data,sizeof(data),100u)==1);
    assert(msg_next(&b,20100u,ack));
    for (uint32_t i=0u; i<5u; ++i) {
        assert(msg_decode(&parsed,ack,sizeof(ack)));
        if (i==0u) ++parsed.target;
        if (i==1u) ++parsed.sequence;
        if (i==2u) ++parsed.buttons;
        if (i==3u) parsed.origin=MSG_USB;
        if (i==4u) { parsed.origin=MSG_USB; parsed.token=42u; }
        assert(msg_encode(damaged,&parsed));
        assert(!msg_receive(&a,damaged,sizeof(damaged),21000u));
        assert(a.active);
    }
    /* A lost ACK repeats the same identity, not a second button event. */
    assert(msg_next(&a,a.retry_at,data));
    assert(msg_receive(&b,data,sizeof(data),600000u)==2);
    assert(msg_next(&b,620000u,ack));
    assert(msg_receive(&a,ack,sizeof(ack),630000u)==3);
    assert(b.rx_data==1u && a.tx_data==2u);
    assert(msg_enqueue(&a,0u,MSG_BUTTON,0u,700000u));
    for (uint32_t i=0u; i<MSG_ATTEMPTS; ++i)
        assert(msg_next(&a,i? a.retry_at:700000u,data));
    assert(!msg_next(&a,a.retry_at,data));
    assert(a.failed==1u && !a.active);
    assert(a.last_timing.outcome==2u && a.last_timing.attempts==MSG_ATTEMPTS);
    for (uint32_t i=0u; i<MSG_QUEUE; ++i)
        assert(msg_enqueue(&a,(uint8_t)i,MSG_BUTTON,0u,0u));
    assert(!msg_enqueue(&a,0u,MSG_BUTTON,0u,0u) && a.dropped==1u);
    assert(!msg_enqueue(&b,32u,MSG_BUTTON,0u,0u));
    assert(!msg_enqueue(&b,1u,MSG_BUTTON,1u,0u));
    msg_init(&a,1u,1u);
    msg_buttons(&a,1u,0xfffff000u);
    msg_buttons(&a,0u,0xfffff100u);
    msg_buttons(&a,5u,0xfffff200u);
    msg_buttons(&a,5u,0xfffff200u+19999u);
    assert(!a.count && !a.buttons);
    msg_buttons(&a,5u,0xfffff200u+20000u);
    assert(a.count==1u && a.buttons==5u);
    assert(a.queue[0].detected_at==0xfffff200u);
    assert(a.queue[0].queued_at-a.queue[0].detected_at==20000u);
    msg_buttons(&a,0u,30000u);
    msg_buttons(&a,0u,50000u);
    assert(a.count==2u && !a.buttons);
    msg_init(&a,1u,3u); a.sequence=0xffffffffu;
    assert(msg_enqueue(&a,2u,MSG_BUTTON,0u,0u));
    assert(msg_next(&a,0u,data));
    assert(a.pending.sequence==1u);
    assert(msg_receive(&b,data,sizeof(data),1u)==1);
    assert(b.peer_session==3u && b.peer_sequence==1u);
    msg_init(&a,1u,10u); msg_init(&b,2u,20u);
    assert(msg_enqueue(&a,1u,MSG_BUTTON,0u,100u));
    assert(msg_enqueue(&b,2u,MSG_BUTTON,0u,100u));
    assert(msg_next(&a,100u,data) && msg_next(&b,100u,ack));
    assert(msg_receive(&a,ack,sizeof(ack),200u)==1);
    assert(msg_receive(&b,data,sizeof(data),200u)==1);
    assert(msg_next(&a,20200u,data) && msg_next(&b,20200u,ack));
    assert(msg_receive(&a,ack,sizeof(ack),20300u)==3);
    assert(msg_receive(&b,data,sizeof(data),20300u)==3);
    assert(a.rx_ack==1u && b.rx_ack==1u && a.last_press==2u && b.last_press==1u);
    assert(msg_enqueue(&a,0u,MSG_HEARTBEAT,0u,30000u));
    assert(msg_next(&a,30000u,data));
    assert(msg_receive(&b,data,sizeof(data),31000u)==1);
    assert(msg_next(&b,51000u,ack));
    assert(msg_receive(&a,ack,sizeof(ack),52000u)==3);
    assert(a.last_acked.origin==MSG_BUTTON && b.last_press==1u && b.peer_presses==1u);
    puts("HT20 messenger: 128 both-role/mask/timer-wrap round trips; canonical HT descriptors,"
         " corruption, stale ACKs, lost ACK duplicate suppression, bounded retries/queue,"
         " reboot sessions and debounced press/release pass; NOT RF qualification");
    return 0;
}
