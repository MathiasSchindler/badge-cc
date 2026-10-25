#define main waterfall_collector_main
#include "collect.c"
#undef main

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static void encode_pages(uint8_t archive[ARCHIVE_BYTES], const wf_report_t *r) {
    uint8_t trace[WIFI_TRACE_SIZE] = {0};
    memcpy(trace, r, sizeof(*r));
    for (uint32_t page = 0u; page < WIFI_TRACE_PAGES; ++page)
        wifi_trace_page(archive + 128u + page * 64u, trace, archive + 64u, (uint8_t)page);
}

int main(void) {
    uint8_t archive[ARCHIVE_BYTES] = {0}, nonce[16] = {1u};
    usb_report_request_for_test(archive, USB_REPORT_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    uint8_t *reply = archive + 64u;
    usb_report_header(reply, 0x57363255u, USB_REPORT_BUILD_ID, nonce, WIFI_USB_TEST_ID);
    badger_bytes_store_u32(reply + 48u, WF_MAGIC);
    badger_bytes_store_u32(reply + 52u, WF_VERSION);
    badger_bytes_store_u32(reply + 56u, WF_REPORT_BYTES);
    badger_bytes_store_u32(reply + 60u, diag_crc32(reply, 60u));
    wf_report_t r = {0};
    r.magic = WF_MAGIC; r.version = WF_VERSION; r.generation = 1u;
    r.count = WF_RECORDS; r.complete = 1u;
    for (uint32_t i = 0u; i < WF_RECORDS; ++i) {
        wf_record_t *p = &r.records[i];
        p->mhz = wf_frequency(i); p->baseline = wf_nearest_channel(p->mhz);
        p->divider = wf_divider(p->mhz);
        p->frequency_readback = p->mhz; p->divider_readback = p->divider;
        p->vco_after = 1u; p->dc_idac = 0xffu; p->dc_dcoe = 0xfffu;
        p->result = WF_READBACK_OK; p->cycles = 1u;
        p->phy_guard = 0x4000u; p->radio_guard = 0x40000u;
        p->maccontrol = 0x84020400u;
    }
    encode_pages(archive, &r);
    CHECK(inspect(archive, "synthetic"));
    archive[128u + 36u] ^= 1u;
    CHECK(!inspect(archive, "synthetic"));
    encode_pages(archive, &r);
    archive[128u + 16u] ^= 1u;
    CHECK(!inspect(archive, "synthetic"));
    r.generation = 3u; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    r.generation = 1u; r.count--; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    r.count++; r.complete = 0u; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    r.complete = 1u; r.records[14].mhz++; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    r.records[14].mhz--; r.records[14].vco_after = 0xffffu; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    r.records[14].vco_after = 1u; r.records[14].maccontrol |= 1u; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    r.records[14].maccontrol &= ~1u; r.records[14].dc_idac = 0u; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    r.records[14].dc_idac = 0xffu; r.error = WF_DC_FAILED; encode_pages(archive, &r);
    CHECK(!inspect(archive, "synthetic"));
    return 0;
}
