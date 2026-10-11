#include <assert.h>
#include <stdlib.h>
#define main collector_main
#include "../../src/host/tools/compiler_test_host.c"
#undef main

static libusb_context context;
static libusb_device devices[2] = {{0}, {1}};
static libusb_device_handle handles[2] = {{0}, {1}};
static libusb_device *list[3] = {&devices[0], &devices[1], NULL};
static const char *serials[2] = {"9BBF88DD17D66E7D", "08DA26F86E57F5EA"};
static int claimed[2], submitted[2], released[2], closed[2], exits;
static int rebooting[2], references[2], removal_observed;
static int fail_claim, fail_page, fail_release, fail_events, fail_detach, event_calls;
static int release_disconnected, fail_bootsel;

int libusb_init(libusb_context **out) { *out = &context; return 0; }
void libusb_exit(libusb_context *ctx) {
    assert(ctx == &context);
    assert(closed[0] && closed[1]);
    assert(removal_observed || fail_events || fail_detach);
    assert(references[0] == 0 && references[1] == 0);
    ++exits;
}
ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***out) {
    assert(ctx == &context);
    if (!fail_detach && closed[0] && closed[1] && (rebooting[0] || rebooting[1])) {
        removal_observed = 1; *out = list; return 0;
    }
    *out = list; return 2;
}
void libusb_free_device_list(libusb_device **out, int unref) { assert(out == list && unref == 1); }
int libusb_get_device_descriptor(libusb_device *device, struct libusb_device_descriptor *out) {
    (void)device;
    *out = (struct libusb_device_descriptor){LIVE_USB_VID, LIVE_USB_PID, 1}; return 0;
}
int libusb_open(libusb_device *device, libusb_device_handle **out) { *out = &handles[device->role]; return 0; }
int libusb_get_string_descriptor_ascii(libusb_device_handle *handle, uint8_t index, unsigned char *data, int length) {
    assert(index == 1 && length >= 16); memcpy(data, serials[handle->role], 16); return 16;
}
int libusb_get_configuration(libusb_device_handle *handle, int *configuration) {
    (void)handle; *configuration = 1; return 0;
}
int libusb_set_configuration(libusb_device_handle *handle, int configuration) {
    (void)handle; assert(configuration == 1); return 0;
}
int libusb_claim_interface(libusb_device_handle *handle, int interface) {
    assert(interface == 0);
    if (fail_claim && handle->role == 1) return -1;
    claimed[handle->role] = 1; return 0;
}
int libusb_release_interface(libusb_device_handle *handle, int interface) {
    assert(interface == 0 && claimed[handle->role]);
    ++released[handle->role];
    return release_disconnected ? LIBUSB_ERROR_NO_DEVICE : fail_release ? -1 : 0;
}
void libusb_close(libusb_device_handle *handle) { ++closed[handle->role]; }
const char *libusb_error_name(int error) { (void)error; return "mock-error"; }
libusb_device *libusb_get_device(libusb_device_handle *handle) { return &devices[handle->role]; }
libusb_device *libusb_ref_device(libusb_device *device) { ++references[device->role]; return device; }
void libusb_unref_device(libusb_device *device) { --references[device->role]; }
int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *timeout) {
    assert(ctx == &context && timeout->tv_usec == 100000);
    ++event_calls; return fail_events ? -1 : 0;
}
int libusb_control_transfer(libusb_device_handle *handle, uint8_t type, uint8_t request,
                           uint16_t value, uint16_t index, unsigned char *data, uint16_t length, unsigned timeout) {
    (void)value; (void)index; (void)timeout;
    if (request == LIVE_USB_BOOTSEL) {
        assert(type == 0x40 && length == 0 && references[handle->role] == 1);
        if (fail_bootsel) return -1;
        rebooting[handle->role] = 1; return 0;
    }
    if (request == LIVE_USB_INFO) {
        assert(type == 0xc0 && length == 12);
        const uint8_t prefix[8] = {'U', '2', '6', 'L', 1u, 0u, 64u, 0u};
        memcpy(data, prefix, 8);
        for (unsigned i = 0; i < 4; ++i) data[8 + i] = (uint8_t)(COMPILER_TEST_BUILD_ID >> (i * 8));
        return 12;
    }
    if (request == LIVE_USB_COMPILER_SUBMIT) {
        assert(type == 0x40 && length == 64);
        if (usb_report_check_request_for_test(data, 64, COMPILER_TEST_BUILD_ID, COMPILER_SUITE_ID) != USB_RECORD_OK)
            return LIBUSB_ERROR_PIPE;
        submitted[handle->role] = 1; return 64;
    }
    assert(request == LIVE_USB_COMPILER_PAGE);
    if (!submitted[handle->role]) return LIBUSB_ERROR_PIPE;
    if (fail_page && handle->role == 1) return -1;
    memset(data, 0, length); return length;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    uint8_t nonce[16] = {1}, request[64];
    usb_report_request_for_test(request, COMPILER_TEST_BUILD_ID, nonce, COMPILER_SUITE_ID);
    char input[1024], a[1024], b[1024];
    assert(snprintf(input, sizeof(input), "%s/request.bin", argv[1]) > 0);
    assert(snprintf(a, sizeof(a), "%s/a.bin", argv[1]) > 0);
    assert(snprintf(b, sizeof(b), "%s/b.bin", argv[1]) > 0);
    FILE *file = fopen(input, "wb");
    assert(file && fwrite(request, 1, sizeof(request), file) == sizeof(request));
    assert(fclose(file) == 0);
    char *args[] = {"collector", (char *)serials[0], (char *)serials[1], input, a, b};
    for (int scenario = 0; scenario < 9; ++scenario) {
        memset(claimed, 0, sizeof(claimed)); memset(submitted, 0, sizeof(submitted));
        memset(released, 0, sizeof(released)); memset(closed, 0, sizeof(closed)); exits = 0;
        memset(rebooting, 0, sizeof(rebooting)); memset(references, 0, sizeof(references)); removal_observed = 0;
        event_calls = 0; fail_events = scenario == 4; fail_detach = scenario == 5 || scenario == 7 || scenario == 8;
        release_disconnected = scenario >= 6; fail_bootsel = scenario == 8;
        fail_claim = scenario == 1; fail_page = scenario == 2; fail_release = scenario == 3;
        assert(collector_main(6, args) == (scenario == 0 || scenario == 6 ? 0 : 1));
        assert(exits == 1);
        assert(released[0] == 1 && released[1] == (scenario == 1 ? 0 : 1));
        assert(event_calls == (fail_detach ? 300 : 1));
    }
    puts("collector: explicit BOOTSEL, observed detach, release/disconnect race, claim/page/release/event/BOOTSEL failures and bounded detach timeout PASS");
    return 0;
}
