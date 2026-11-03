#define main monitor_main
#include "monitor.c"
#undef main
#include <assert.h>

static uint8_t mocked_report[64];
static IOReturn mock_request(void *self, IOUSBDevRequestTO *request) {
    (void)self;
    assert(request->bmRequestType == 0xc0u);
    assert(request->wLength == 64u);
    if (request->bRequest == LIVE_USB_LOGI_FS_TRACE) {
        fs_result_t r = {.generation = (uint16_t)(mocked_report[54] | mocked_report[55] << 8u)};
        fs_trace_report(&r, USB_REPORT_BUILD_ID, request->pData);
    } else {
        assert(request->bRequest == LIVE_USB_LOGI_FS_STATUS);
        memcpy(request->pData, mocked_report, 64u);
    }
    request->wLenDone = 64u;
    return kIOReturnSuccess;
}

int main(void) {
    uint8_t trace[64] = {0};
    assert(trace_response_gap(trace) == -1);
    trace[8] = 2u;
    /* Four SE0 samples, a 17-sample idle gap crossing a word boundary, then K. */
    for (unsigned i = 4u; i < 32u; ++i)
        trace[16u + i / 4u] |= (i < 21u ? 1u : 2u) << (2u * (i % 4u));
    assert(trace_response_gap(trace) == 17);
    trace[21] |= 3u << 2u;
    assert(trace_response_gap(trace) == -1);
    memset(trace + 16u, 0x55, 32u);
    assert(trace_response_gap(trace) == -1);
    trace[16] = 0u;
    assert(trace_response_gap(trace) == -1);
    trace[8] = 9u;
    assert(trace_response_gap(trace) == -1);
    unsigned seconds = 123u;
    const char *invalid[] = {"", "0", "-1", "+1", " 1", "1x", "3601", "999999999999999999999999"};
    for (unsigned i = 0u; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        assert(!parse_seconds(invalid[i], &seconds) && seconds == 123u);
    }
    assert(parse_seconds("1", &seconds) && seconds == 1u);
    assert(parse_seconds("3600", &seconds) && seconds == 3600u);
    logi_board_t b = {.captures = 2u, .samples = 32u, .histogram = {8u,8u,8u,8u}};
    uint8_t bytes[64] = {0};
    badger_bytes_store_u32(bytes, LOGI_STATUS_MAGIC);
    badger_bytes_store_u32(bytes + 4u, USB_REPORT_BUILD_ID);
    badger_bytes_store_u32(bytes + 12u, b.captures);
    badger_bytes_store_u32(bytes + 16u, b.samples);
    for (unsigned i = 0u; i < 4u; ++i) badger_bytes_store_u32(bytes + 24u + i * 4u, b.histogram[i]);
    bytes[53] = LOGI_MODE_PASSIVE; bytes[54] = LOGI_DP; bytes[55] = LOGI_DM;
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
    assert(validate_status(bytes));
    for (unsigned i = 0; i < 64u; ++i) {
        bytes[i] ^= 1u;
        assert(!validate_status(bytes));
        bytes[i] ^= 1u;
    }
    badger_bytes_store_u32(bytes + 24u, 33u);
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
    assert(!validate_status(bytes));
    fs_result_t result = {0};
    fs_report(&result, USB_REPORT_BUILD_ID, 1u, bytes);
    assert(validate_fs(bytes));
    for (unsigned i = 0u; i < 64u; ++i) {
        bytes[i] ^= 1u; assert(!validate_fs(bytes)); bytes[i] ^= 1u;
    }
    bytes[34] = 19u;
    badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
    assert(!validate_fs(bytes));
    result.stage = 6u; result.descriptor_length = 18u; result.mps = 64u; result.generation = 10u;
    result.clock_ready = 1u;
    result.descriptor[0] = 18u; result.descriptor[1] = 1u; result.descriptor[7] = 64u;
    fs_report(&result, USB_REPORT_BUILD_ID, 1u, mocked_report);
    IOUSBDeviceInterface500 device = {.DeviceRequestTO = mock_request};
    IOUSBDeviceInterface500 *pointer = &device;
    assert(read_fs(&pointer, 10u, 1));
    assert(!read_fs(&pointer, 9u, 1));
    mocked_report[60] ^= 1u;
    assert(!read_fs(&pointer, 10u, 1));
    char *args[] = {"monitor", "--watch", "0"};
    assert(monitor_main(3, args) == 2);
    puts("Monitor: bounded CLI, build/CRC and sample/histogram invariants PASS");
    return 0;
}
