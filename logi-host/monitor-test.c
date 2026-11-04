#define main monitor_main
#include "monitor.c"
#undef main
#include <assert.h>

static uint8_t mocked_report[64];
static unsigned keypad_mock, keypad_requests, keypad_started, keypad_lcd;
static void mock_keypad(IOUSBDevRequestTO *request) {
    if (request->bRequest == LIVE_USB_LOGI_KEYPAD || request->bRequest == LIVE_USB_LOGI_LCD_TEST ||
        request->bRequest == LIVE_USB_LOGI_LCD_TOGGLE || request->bRequest == LIVE_USB_LOGI_LCD_ANIMATE) {
        assert(request->bmRequestType == 0x40u && request->wValue == 2u && !request->wLength);
        keypad_started = 1u;
        keypad_lcd = request->bRequest == LIVE_USB_LOGI_LCD_ANIMATE ? FS_LCD_ANIMATE :
                     request->bRequest == LIVE_USB_LOGI_LCD_TOGGLE ? FS_LCD_TOGGLE :
                     request->bRequest == LIVE_USB_LOGI_LCD_TEST ? FS_LCD_STATIC : 0u;
        return;
    }
    assert(request->bmRequestType == 0xc0u && request->wLength == 64u && !request->wIndex);
    uint8_t *bytes = request->pData;
    if (request->bRequest == LIVE_USB_LOGI_KEYPAD_STATUS) {
        fs_keypad_t k = {.stage = keypad_started ? 3u : 0u, .phase = 7u,
                         .reports = keypad_started ? 2u : 0u, .buttons = 1u, .query_done = 1u};
        if (keypad_mock == 1u && keypad_started) k.reports = FS_KEYPAD_EVENTS + 1u;
        if (keypad_mock == 4u) k.error = k.cleanup_error = FS_STALL;
        if (keypad_mock == 7u) k.query_done = 0u;
        if (keypad_mock == 10u) { k.stage = keypad_started ? 4u : 0u; k.error = FS_LCD_REPLY; }
        if (keypad_mock == 13u) k.cleanup_error = FS_STALL;
        fs_keypad_status(&k, USB_REPORT_BUILD_ID, keypad_started && keypad_mock != 5u ? 8u : 7u, bytes);
        if (keypad_started && keypad_lcd) {
            bytes[50] = keypad_mock == 9u ? 8u : 9u;
            bytes[51] = keypad_mock == 14u ? 0u : 27u;
            bytes[58] = keypad_mock == 11u ? 0u : (uint8_t)keypad_lcd;
            bytes[59] = keypad_mock == 10u ? FS_LCD_FAILED : keypad_mock == 12u ? FS_LCD_HOLD : FS_LCD_DONE;
            badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
        }
        if (keypad_mock == 3u && keypad_started) bytes[60] ^= 1u;
    } else {
        assert(request->bRequest == LIVE_USB_LOGI_KEYPAD_EVENT && request->wValue < 2u);
        memset(bytes, 0, 64u);
        badger_bytes_store_u32(bytes, FS_KEYPAD_EVENT_MAGIC);
        badger_bytes_store_u32(bytes + 4u, USB_REPORT_BUILD_ID);
        badger_bytes_store_u32(bytes + 8u, request->wValue + 1u + (keypad_mock == 2u));
        bytes[12] = 8u; bytes[14] = keypad_mock == 6u ? 33u : 32u;
        memcpy(bytes + 16u, (uint8_t[]){0x13,0xff,2,0,42,1,9}, 7u);
        badger_bytes_store_u32(bytes + 60u, diag_crc32(bytes, 60u));
    }
    ++keypad_requests;
}

static IOReturn mock_request(void *self, IOUSBDevRequestTO *request) {
    (void)self;
    if ((request->bRequest >= LIVE_USB_LOGI_KEYPAD && request->bRequest <= LIVE_USB_LOGI_KEYPAD_EVENT) ||
        request->bRequest == LIVE_USB_LOGI_LCD_TEST || request->bRequest == LIVE_USB_LOGI_LCD_TOGGLE ||
        request->bRequest == LIVE_USB_LOGI_LCD_ANIMATE) {
        mock_keypad(request);
        request->wLenDone = request->wLength;
        return kIOReturnSuccess;
    }
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
    assert(keypad_watch(&pointer, 2u, 0) && keypad_requests == 4u);
    for (unsigned mode = 1u; mode <= 7u; ++mode) {
        keypad_mock = mode; keypad_started = keypad_requests = 0u;
        assert(!keypad_watch(&pointer, 2u, 0));
    }
    for (unsigned lcd_mode = FS_LCD_STATIC; lcd_mode <= FS_LCD_ANIMATE; ++lcd_mode) {
        for (unsigned mode = 8u; mode <= 13u; ++mode) {
            keypad_mock = mode; keypad_started = keypad_requests = 0u;
            assert(keypad_watch(&pointer, 2u, (int)lcd_mode) == (mode == 8u));
        }
    }
    keypad_mock = 14u; keypad_started = keypad_requests = 0u;
    assert(!keypad_watch(&pointer, 2u, FS_LCD_ANIMATE));
    mocked_report[60] ^= 1u;
    assert(!read_fs(&pointer, 10u, 1));
    char *args[] = {"monitor", "--watch", "0"};
    assert(monitor_main(3, args) == 2);
    char *bad_watch[] = {"monitor", "--keypad-watch", "31"};
    assert(monitor_main(3, bad_watch) == 2);
    char *bad_lcd[] = {"monitor", "--lcd-test", "31"};
    assert(monitor_main(3, bad_lcd) == 2);
    char *bad_toggle[] = {"monitor", "--lcd-toggle", "31"};
    assert(monitor_main(3, bad_toggle) == 2);
    char *bad_animate[] = {"monitor", "--lcd-animate", "31"};
    assert(monitor_main(3, bad_animate) == 2);
    char *two_lcd_modes[] = {"monitor", "--lcd-toggle", "1", "--lcd-test", "1"};
    assert(monitor_main(5, two_lcd_modes) == 2);
    puts("Monitor: bounded CLI, build/CRC and sample/histogram invariants PASS");
    return 0;
}
