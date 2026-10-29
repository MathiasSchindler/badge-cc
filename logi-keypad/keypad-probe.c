#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "keypad-lcd.h"

enum { KEYPAD_VENDOR = 0x046d, KEYPAD_PRODUCT = 0xc354 };

static void usage(FILE *out, const char *program) {
    fprintf(out, "Usage: %s [--inspect | --other-speed | --watch SECONDS | --lcd-test SECONDS]\n"
            "       [--navigation] [--serial SERIAL]\n"
            "       %s --help\n"
            "Inspect cached USB/HID descriptors (default), or read raw input for\n"
            "1..3600 seconds. Default modes send no output/feature reports.\n"
            "--navigation temporarily enables page-button reports and restores\n"
            "their original diversion state on normal exit, Ctrl-C or SIGTERM.\n"
            "--lcd-test uploads colored digits 1..9 and maintains a keepalive;\n"
            "it replaces current icons temporarily, without changing brightness.\n"
            "--other-speed reads standard USB qualifier/other-speed descriptors;\n"
            "it does not change configuration, reset or seize the device.\n",
            program, program);
}

static int parse_seconds(const char *text, unsigned *seconds) {
    unsigned value = 0;
    if (*text == '\0') return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return 0;
        value = value * 10u + (unsigned)(*text - '0');
        if (value > 3600u) return 0;
    }
    if (value == 0) return 0;
    *seconds = value;
    return 1;
}

static int number(CFTypeRef object, int64_t *value) {
    return object && CFGetTypeID(object) == CFNumberGetTypeID() &&
        CFNumberGetValue(object, kCFNumberSInt64Type, value);
}

static void print_property(const char *name, CFTypeRef value) {
    int64_t integer;
    char text[1024];
    if (!value) {
        printf("%s: <not published>\n", name);
    } else if (number(value, &integer)) {
        printf("%s: %lld (0x%llx)\n", name, (long long)integer,
               (unsigned long long)integer);
    } else if (CFGetTypeID(value) == CFStringGetTypeID() &&
               CFStringGetCString(value, text, sizeof(text), kCFStringEncodingUTF8)) {
        printf("%s: %s\n", name, text);
    } else {
        printf("%s: <unprintable property>\n", name);
    }
}

static void hex_bytes(const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) printf("%s%02x", i ? " " : "", bytes[i]);
    putchar('\n');
}

static unsigned little16(const uint8_t *bytes) {
    return (unsigned)bytes[0] | ((unsigned)bytes[1] << 8);
}

static int print_configuration(const uint8_t *bytes, size_t size) {
    puts("USB configuration descriptor (raw):");
    hex_bytes(bytes, size);
    for (size_t offset = 0; offset < size;) {
        if (size - offset < 2 || bytes[offset] < 2 || bytes[offset] > size - offset) {
            fputs("Malformed USB descriptor length.\n", stderr);
            return 0;
        }
        const uint8_t *d = bytes + offset;
        if ((d[1] == 2 || d[1] == 7) && d[0] >= 9) {
            printf("Configuration %u: interfaces=%u attributes=0x%02x "
                   "bMaxPower=%u (%u mA at USB 2.0)\n",
                   d[5], d[4], d[7], d[8], 2u * d[8]);
        } else if (d[1] == 4 && d[0] >= 9) {
            printf("Interface %u alt=%u: class=0x%02x subclass=0x%02x "
                   "protocol=0x%02x endpoints=%u\n",
                   d[2], d[3], d[5], d[6], d[7], d[4]);
        } else if (d[1] == 5 && d[0] >= 7) {
            printf("Endpoint 0x%02x: attributes=0x%02x wMaxPacketSize=0x%04x "
                   "bInterval=%u\n", d[2], d[3], little16(d + 4), d[6]);
        }
        offset += d[0];
    }
    return 1;
}

static int get_usb_descriptor(IOUSBDeviceInterface **usb, uint8_t type,
                               uint8_t *bytes, uint16_t length) {
    IOUSBDevRequest request = { .bmRequestType = 0x80, .bRequest = 6,
        .wValue = (uint16_t)(type << 8), .wLength = length, .pData = bytes };
    IOReturn result = (*usb)->DeviceRequest(usb, &request);
    if (result != kIOReturnSuccess || request.wLenDone != length) {
        fprintf(stderr, "GET_DESCRIPTOR type %u failed: 0x%08x, got %u/%u bytes.\n",
                type, (unsigned)result, (unsigned)request.wLenDone, length);
        return 0;
    }
    return 1;
}

static int inspect_other_speed(IOUSBDeviceInterface **usb) {
    IOReturn result = (*usb)->USBDeviceOpen(usb);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Cannot open USB device for standard descriptor reads: 0x%08x.\n",
                (unsigned)result);
        return 0;
    }
    uint8_t qualifier[10], configuration[1024];
    int ok = get_usb_descriptor(usb, 6, qualifier, sizeof(qualifier));
    if (ok) {
        puts("USB device qualifier:");
        hex_bytes(qualifier, sizeof(qualifier));
        ok = qualifier[0] == 10 && qualifier[1] == 6;
        if (!ok) fputs("Malformed USB device qualifier.\n", stderr);
    }
    if (ok) ok = get_usb_descriptor(usb, 7, configuration, 9);
    if (ok) {
        unsigned size = little16(configuration + 2);
        if (configuration[0] != 9 || configuration[1] != 7 ||
            size < 9 || size > sizeof(configuration)) {
            fputs("Malformed or oversized other-speed configuration.\n", stderr);
            ok = 0;
        } else {
            ok = get_usb_descriptor(usb, 7, configuration, (uint16_t)size);
            if (ok) {
                ok = configuration[0] == 9 && configuration[1] == 7 &&
                     little16(configuration + 2) == size;
                if (!ok) fputs("Other-speed configuration header changed between reads.\n", stderr);
                else {
                    puts("OTHER SPEED (opposite the current link):");
                    ok = print_configuration(configuration, size);
                }
            }
        }
    }
    result = (*usb)->USBDeviceClose(usb);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "USB device close failed: 0x%08x.\n", (unsigned)result);
        ok = 0;
    }
    return ok;
}

static int inspect_usb(IOHIDDeviceRef device, int other_speed) {
    io_service_t service = IOHIDDeviceGetService(device);
    if (!service || IOObjectRetain(service) != kIOReturnSuccess) {
        fputs("Cannot retain keypad registry service.\n", stderr);
        return 0;
    }
    while (!IOObjectConformsTo(service, "IOUSBHostDevice")) {
        io_service_t parent = IO_OBJECT_NULL;
        IOReturn result = IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent);
        IOObjectRelease(service);
        if (result != kIOReturnSuccess) {
            fprintf(stderr, "Cannot find USB device parent: 0x%08x\n", (unsigned)result);
            return 0;
        }
        service = parent;
    }
    const char *keys[] = { "UsbLinkSpeed", "bcdUSB", "bcdDevice", "bMaxPacketSize0",
                           "bNumConfigurations", "UsbPowerSinkAllocation" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        CFStringRef key = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
        CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, NULL, 0);
        print_property(keys[i], value);
        if (value) CFRelease(value);
        CFRelease(key);
    }
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    IOReturn result = IOCreatePlugInInterfaceForService(
        service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (result != kIOReturnSuccess || !plugin) {
        fprintf(stderr, "Cannot access cached USB descriptors: 0x%08x\n", (unsigned)result);
        return 0;
    }
    IOUSBDeviceInterface **usb = NULL;
    HRESULT query = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&usb);
    IODestroyPlugInInterface(plugin);
    if (query != S_OK || !usb) {
        fprintf(stderr, "Cannot query USB device interface: 0x%08x\n", (unsigned)query);
        return 0;
    }
    /* This API reads the OS's cached descriptor without USBDeviceOpen. */
    IOUSBConfigurationDescriptorPtr config = NULL;
    result = (*usb)->GetConfigurationDescriptorPtr(usb, 0, &config);
    int ok = 0;
    if (result != kIOReturnSuccess || !config) {
        fprintf(stderr, "Cannot read configuration 0: 0x%08x\n", (unsigned)result);
    } else {
        const uint8_t *bytes = (const uint8_t *)config;
        unsigned size = little16(bytes + 2);
        if (size < 9) {
            fputs("USB configuration is shorter than its header.\n", stderr);
        } else {
            ok = print_configuration(bytes, size);
        }
    }
    if (ok && other_speed) ok = inspect_other_speed(usb);
    (*usb)->Release(usb);
    return ok;
}

static int inspect(IOHIDDeviceRef device, int other_speed) {
    const char *keys[] = { kIOHIDManufacturerKey, kIOHIDProductKey,
        kIOHIDSerialNumberKey, kIOHIDTransportKey, kIOHIDVendorIDKey,
        kIOHIDProductIDKey, kIOHIDLocationIDKey, kIOHIDPrimaryUsagePageKey,
        kIOHIDPrimaryUsageKey, kIOHIDMaxInputReportSizeKey,
        kIOHIDMaxOutputReportSizeKey, kIOHIDMaxFeatureReportSizeKey };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        CFStringRef key = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
        print_property(keys[i], IOHIDDeviceGetProperty(device, key));
        CFRelease(key);
    }
    CFTypeRef descriptor = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDReportDescriptorKey));
    if (!descriptor || CFGetTypeID(descriptor) != CFDataGetTypeID()) {
        fputs("No HID report descriptor published.\n", stderr);
        return 0;
    }
    printf("HID report descriptor (%ld bytes):\n", (long)CFDataGetLength(descriptor));
    hex_bytes(CFDataGetBytePtr(descriptor), (size_t)CFDataGetLength(descriptor));
    return inspect_usb(device, other_speed);
}

typedef struct {
    uint64_t start;
    double tick_seconds;
    size_t count;
    CFIndex capacity;
    int failed;
    uint8_t navigation_feature;
    uint8_t software_id;
    uint8_t request[20];
    uint8_t reply[32];
    int pending;
    int reply_status;
    size_t lcd_events;
    size_t navigation_events;
    uint8_t keepalive_feature;
    double last_keepalive;
    size_t lcd_images;
} watch_state;

static volatile sig_atomic_t stop_requested;

static void stop_capture(int signal_number) {
    stop_requested = signal_number;
}

static double elapsed(const watch_state *state) {
    return (double)(mach_absolute_time() - state->start) * state->tick_seconds;
}

#include "keypad-input.h"

static void print_button_event(FILE *out, int key) {
    if (key < 0) {
        fputs("  decoded: unknown (raw report preserved)\n", out);
    } else if (key == 0) {
        fputs("  decoded: release (no key; released key not encoded)\n", out);
    } else {
        fprintf(out, "  decoded: press key=%d row=%d column=%d\n",
                key, (key - 1) / 3 + 1, (key - 1) % 3 + 1);
    }
}

static void print_navigation_event(FILE *out, int active) {
    const char *names[] = { "none (release)", "left", "right", "left+right" };
    fprintf(out, "  decoded: navigation active=%s\n", names[active]);
}

static void input_report(void *context, IOReturn result, void *sender,
                         IOHIDReportType type, uint32_t report_id,
                         uint8_t *report, CFIndex length) {
    (void)sender;
    watch_state *state = context;
    if (result != kIOReturnSuccess || type != kIOHIDReportTypeInput ||
        length < 0 || length > state->capacity) {
        fprintf(stderr, "Input callback error: status=0x%08x type=%u length=%ld\n",
                (unsigned)result, (unsigned)type, (long)length);
        state->failed = 1;
        CFRunLoopStop(CFRunLoopGetCurrent());
        return;
    }
    printf("+%.6f input id=0x%02x length=%ld raw=",
           elapsed(state), report_id, (long)length);
    hex_bytes(report, (size_t)length);
    int reply = state->pending ?
        match_hidpp_reply(state->request, report_id, report, (size_t)length) : 0;
    int key = decode_button(report_id, report, (size_t)length);
    int navigation = decode_navigation(state->navigation_feature, report_id,
                                       report, (size_t)length);
    if (reply) {
        memcpy(state->reply, report, (size_t)length);
        state->reply_status = reply;
        state->pending = 0;
        puts("  decoded: HID++ command reply (not a button event)");
    } else if (navigation >= 0) {
        print_navigation_event(stdout, navigation);
        ++state->navigation_events;
    } else {
        print_button_event(stdout, key);
        if (key >= 0) ++state->lcd_events;
    }
    fflush(stdout);
    ++state->count;
}

static void removed(void *context, IOReturn result, void *sender) {
    (void)sender;
    watch_state *state = context;
    fprintf(stderr, "Keypad disconnected (status=0x%08x).\n", (unsigned)result);
    state->failed = 1;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

static int exchange_report(IOHIDDeviceRef device, watch_state *state,
                            const uint8_t *request, size_t size) {
    if (!request || (size != 20 && size != LCD_PACKET_SIZE)) {
        fputs("Invalid internal HID output report.\n", stderr);
        return 0;
    }
    memcpy(state->request, request, sizeof(state->request));
    state->pending = 1;
    state->reply_status = 0;
    printf("HID output (%zu bytes; header): ", size);
    hex_bytes(state->request, sizeof(state->request));
    fflush(stdout);
    IOReturn result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput,
                                          request[0], request, (CFIndex)size);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "HID output report 0x%02x failed: 0x%08x\n",
                request[0], (unsigned)result);
        state->pending = 0;
        return 0;
    }
    double deadline = elapsed(state) + 1.0;
    while (state->pending && !state->failed && elapsed(state) < deadline) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    }
    state->pending = 0;
    if (state->reply_status == 1) return 1;
    if (state->reply_status == -1) {
        fprintf(stderr, "HID++ error 0x%02x for feature 0x%02x function %u.\n",
                state->reply[5], request[2], request[3] >> 4);
    } else {
        fprintf(stderr, "No HID++ reply for feature 0x%02x function %u within 1 second.\n",
                request[2], request[3] >> 4);
    }
    return 0;
}

static int hidpp_exchange(IOHIDDeviceRef device, watch_state *state,
                          uint8_t feature, uint8_t function,
                          const uint8_t *parameters, size_t count) {
    if (!parameters || count > 16 || function > 15) {
        fputs("Invalid internal HID++ request.\n", stderr);
        return 0;
    }
    state->software_id = (uint8_t)(state->software_id % 15 + 1);
    uint8_t request[20] = { 0x11, 0xff, feature,
                            (uint8_t)((function << 4) | state->software_id) };
    memcpy(request + 4, parameters, count);
    return exchange_report(device, state, request, sizeof(request));
}

static int discover_feature(IOHIDDeviceRef device, watch_state *state,
                             unsigned id, uint8_t *index) {
    const uint8_t feature_id[] = { (uint8_t)(id >> 8), (uint8_t)id };
    if (!hidpp_exchange(device, state, 0, 0, feature_id, sizeof(feature_id))) return 0;
    *index = state->reply[4];
    if (!*index || *index == 0xff) {
        fprintf(stderr, "The keypad does not expose HID++ feature 0x%04x.\n", id);
        return 0;
    }
    printf("HID++ feature 0x%04x discovered at index 0x%02x.\n", id, *index);
    return 1;
}

static int get_diversion(IOHIDDeviceRef device, watch_state *state,
                          unsigned cid, int *diverted) {
    uint8_t parameters[] = { (uint8_t)(cid >> 8), (uint8_t)cid };
    if (!hidpp_exchange(device, state, state->navigation_feature, 2,
                         parameters, sizeof(parameters))) return 0;
    if (state->reply[4] != parameters[0] || state->reply[5] != parameters[1]) {
        fprintf(stderr, "HID++ reporting reply has the wrong control ID for 0x%04x.\n", cid);
        return 0;
    }
    *diverted = state->reply[6] & 1;
    printf("Navigation CID 0x%04x: diverted=%d\n", cid, *diverted);
    return 1;
}

static int set_diversion(IOHIDDeviceRef device, watch_state *state,
                          unsigned cid, int diverted) {
    /* Only the temporary-diversion value and its validity bit are written. */
    uint8_t parameters[] = { (uint8_t)(cid >> 8), (uint8_t)cid,
                             (uint8_t)(2 | diverted) };
    if (!hidpp_exchange(device, state, state->navigation_feature, 3,
                         parameters, sizeof(parameters))) return 0;
    int actual;
    if (!get_diversion(device, state, cid, &actual)) return 0;
    if (actual != diverted) {
        fprintf(stderr, "Navigation CID 0x%04x did not adopt diverted=%d.\n", cid, diverted);
        return 0;
    }
    return 1;
}

static int enable_navigation(IOHIDDeviceRef device, watch_state *state,
                              int original[2], int changed[2]) {
    if (!discover_feature(device, state, 0x1b04, &state->navigation_feature)) return 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (!get_diversion(device, state, 0x01a1 + i, &original[i])) return 0;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (stop_requested) return 0;
        if (original[i]) continue;
        /* Even a timed-out write may have taken effect and needs restoration. */
        changed[i] = 1;
        if (!set_diversion(device, state, 0x01a1 + i, 1)) return 0;
    }
    return 1;
}

static int lcd_keepalive(IOHIDDeviceRef device, watch_state *state) {
    const uint8_t interval[] = { 0x0b, 0xb8 };
    if (!hidpp_exchange(device, state, state->keepalive_feature, 1,
                         interval, sizeof(interval))) return 0;
    state->last_keepalive = elapsed(state);
    return 1;
}

static int paint_numbered_keys(IOHIDDeviceRef device, watch_state *state) {
    if (!discover_feature(device, state, 0x0008, &state->keepalive_feature) ||
        !lcd_keepalive(device, state)) return 0;
    for (unsigned key = 1; key <= 9 && !stop_requested && !state->failed; ++key) {
        CFDataRef jpeg = lcd_jpeg(key);
        if (!jpeg) return 0;
        size_t size = (size_t)CFDataGetLength(jpeg);
        printf("LCD key %u: JPEG %zu bytes, rectangle (%u,%u) 118x118.\n",
                key, size, 23 + (key - 1) % 3 * 158, 6 + (key - 1) / 3 * 158);
        int accepted = 0;
        for (unsigned attempt = 0; attempt < 3 && !accepted && !stop_requested; ++attempt) {
            size_t offset = 0;
            unsigned part = 1;
            while (offset < size && !stop_requested && !state->failed) {
                if (elapsed(state) - state->last_keepalive >= 1.0 &&
                    !lcd_keepalive(device, state)) {
                    CFRelease(jpeg);
                    return 0;
                }
                uint8_t packet[LCD_PACKET_SIZE];
                size_t count = lcd_packet(packet, key, CFDataGetBytePtr(jpeg), size, offset, part);
                if (!count || !exchange_report(device, state, packet, sizeof(packet))) {
                    CFRelease(jpeg);
                    return 0;
                }
                unsigned status = state->reply[5];
                if (status == 0xfb) {
                    fprintf(stderr, "LCD busy on key %u; retrying image (%u/3).\n", key, attempt + 1);
                    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
                    break;
                }
                if (status != 0) {
                    fprintf(stderr, "LCD key %u part %u rejected with status 0x%02x.\n",
                            key, part, status);
                    CFRelease(jpeg);
                    return 0;
                }
                offset += count;
                ++part;
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
            }
            accepted = offset == size;
        }
        CFRelease(jpeg);
        if (!accepted) {
            fprintf(stderr, "LCD key %u upload incomplete or interrupted.\n", key);
            return 0;
        }
        ++state->lcd_images;
        printf("LCD key %u: all packets acknowledged as accepted.\n", key);
    }
    return state->lcd_images == 9;
}

static int watch(IOHIDDeviceRef device, unsigned seconds, int navigation, int lcd_test) {
    mach_timebase_info_data_t clock;
    kern_return_t clock_result = mach_timebase_info(&clock);
    if (clock_result != KERN_SUCCESS || clock.denom == 0) {
        fprintf(stderr, "Cannot obtain monotonic clock scale: %d\n", clock_result);
        return 0;
    }
    int64_t capacity = 0;
    if (!number(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDMaxInputReportSizeKey)),
                &capacity) || capacity < 1 || capacity > 65536) {
        fputs("Missing or invalid HID input buffer size.\n", stderr);
        return 0;
    }
    uint8_t *buffer = malloc((size_t)capacity);
    if (!buffer) {
        fputs("Cannot allocate HID input buffer.\n", stderr);
        return 0;
    }
    IOReturn result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Cannot open keypad non-exclusively: 0x%08x\n"
                "Check macOS Input Monitoring permissions and other keypad software.\n",
                (unsigned)result);
        free(buffer);
        return 0;
    }
    watch_state state = { .start = mach_absolute_time(),
        .tick_seconds = (double)clock.numer / (double)clock.denom / 1e9,
        .capacity = (CFIndex)capacity };
    IOHIDDeviceRegisterInputReportCallback(device, buffer, (CFIndex)capacity, input_report, &state);
    IOHIDDeviceRegisterRemovalCallback(device, removed, &state);
    IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    struct sigaction action = { .sa_handler = stop_capture }, old_int, old_term;
    int int_installed = 0, term_installed = 0;
    int original[2] = { 0, 0 }, changed[2] = { 0, 0 };
    int ready = 1;
    stop_requested = 0;
    if (navigation || lcd_test) {
        if (sigemptyset(&action.sa_mask) != 0 ||
            sigaction(SIGINT, &action, &old_int) != 0) {
            perror("Cannot install device cleanup handler");
            ready = 0;
        } else {
            int_installed = 1;
            if (sigaction(SIGTERM, &action, &old_term) != 0) {
                perror("Cannot install device cleanup handler");
                ready = 0;
            } else {
                term_installed = 1;
                if (navigation) ready = enable_navigation(device, &state, original, changed);
            }
            if (ready && lcd_test && !stop_requested) ready = paint_numbered_keys(device, &state);
        }
    }
    if (ready && !stop_requested && !state.failed) {
        printf("Watching for %u seconds; press and release each button in a recorded order.\n",
               seconds);
        fflush(stdout);
        if (lcd_test) puts("LCD test active: digits 1..9 should be visible. Check alignment and colors.");
        double deadline = elapsed(&state) + seconds;
        while (!state.failed && !stop_requested && elapsed(&state) < deadline) {
            if (lcd_test && elapsed(&state) - state.last_keepalive >= 1.0 &&
                !lcd_keepalive(device, &state)) {
                ready = 0;
                break;
            }
            double remaining = deadline - elapsed(&state);
            if (remaining > 0) {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode,
                                   remaining < 0.1 ? remaining : 0.1, false);
            }
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!changed[i]) continue;
        printf("Restoring navigation CID 0x%04x to diverted=%d.\n", 0x01a1 + i, original[i]);
        if (!set_diversion(device, &state, 0x01a1 + i, original[i])) {
            fputs("Navigation restoration failed; unplug/replug the keypad to clear "
                  "temporary diversion before using other software.\n", stderr);
            ready = 0;
        }
    }
    IOHIDDeviceUnscheduleFromRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(device, buffer, (CFIndex)capacity, NULL, NULL);
    IOHIDDeviceRegisterRemovalCallback(device, NULL, NULL);
    result = IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    free(buffer);
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Cannot close keypad: 0x%08x\n", (unsigned)result);
        state.failed = 1;
    }
    if (term_installed && sigaction(SIGTERM, &old_term, NULL) != 0) {
        perror("Cannot restore SIGTERM handler");
        ready = 0;
    }
    if (int_installed && sigaction(SIGINT, &old_int, NULL) != 0) {
        perror("Cannot restore SIGINT handler");
        ready = 0;
    }
    printf("Captured %zu input reports.%s\n", state.count,
           state.count ? "" : " No button events were observed in this session.");
    printf("Decoded %zu LCD-key and %zu navigation state reports.\n",
           state.lcd_events, state.navigation_events);
    if (lcd_test) {
        printf("LCD images acknowledged: %zu/9. Keepalive stopped; firmware may return "
               "to its idle logo. Prior icons cannot be read back/restored.\n", state.lcd_images);
    }
    return ready && !state.failed;
}

int main(int argc, char **argv) {
    unsigned seconds = 0;
    const char *serial = NULL;
    int mode_set = 0;
    int navigation = 0;
    int lcd_test = 0;
    int other_speed = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") && argc == 2) {
            usage(stdout, argv[0]);
            return 0;
        } else if ((!strcmp(argv[i], "--inspect") || !strcmp(argv[i], "--other-speed")) &&
                   !mode_set) {
            other_speed = !strcmp(argv[i], "--other-speed");
            mode_set = 1;
        } else if ((!strcmp(argv[i], "--watch") || !strcmp(argv[i], "--lcd-test")) &&
                   !mode_set && i + 1 < argc) {
            lcd_test = !strcmp(argv[i], "--lcd-test");
            if (!parse_seconds(argv[++i], &seconds)) goto bad_arguments;
            mode_set = 1;
        } else if (!strcmp(argv[i], "--serial") && !serial && i + 1 < argc) {
            serial = argv[++i];
            if (!*serial || *serial == '-') goto bad_arguments;
        } else if (!strcmp(argv[i], "--navigation") && !navigation) {
            navigation = 1;
        } else {
            goto bad_arguments;
        }
    }
    if (navigation && !seconds) goto bad_arguments;
    IOHIDManagerRef manager = IOHIDManagerCreate(NULL, kIOHIDOptionsTypeNone);
    if (!manager) {
        fputs("Cannot create HID manager.\n", stderr);
        return 1;
    }
    int vendor = KEYPAD_VENDOR, product = KEYPAD_PRODUCT;
    CFNumberRef vid = CFNumberCreate(NULL, kCFNumberIntType, &vendor);
    CFNumberRef pid = CFNumberCreate(NULL, kCFNumberIntType, &product);
    CFStringRef serial_string = serial ?
        CFStringCreateWithCString(NULL, serial, kCFStringEncodingUTF8) : NULL;
    if (!vid || !pid || (serial && !serial_string)) {
        fputs("Cannot create device matching values (serial must be UTF-8).\n", stderr);
        if (vid) CFRelease(vid);
        if (pid) CFRelease(pid);
        if (serial_string) CFRelease(serial_string);
        CFRelease(manager);
        return 1;
    }
    const void *keys[] = { CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey),
                          CFSTR(kIOHIDTransportKey), CFSTR(kIOHIDSerialNumberKey) };
    const void *values[] = { vid, pid, CFSTR("USB"), serial_string };
    CFDictionaryRef matching = CFDictionaryCreate(NULL, keys, values, serial ? 4 : 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(vid);
    CFRelease(pid);
    if (serial_string) CFRelease(serial_string);
    if (!matching) {
        fputs("Cannot create device matching dictionary.\n", stderr);
        CFRelease(manager);
        return 1;
    }
    IOHIDManagerSetDeviceMatching(manager, matching);
    CFRelease(matching);
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    CFIndex count = devices ? CFSetGetCount(devices) : 0;
    int ok = 0;
    if (count != 1) {
        fprintf(stderr, "Expected one MX Keypad (046d:c354%s%s); found %ld.\n",
                serial ? ", serial=" : "", serial ? serial : "", (long)count);
        if (count > 1) fputs("Select one using --serial SERIAL.\n", stderr);
    } else {
        const void *device = NULL;
        CFSetGetValues(devices, &device);
        ok = seconds ? watch((IOHIDDeviceRef)device, seconds, navigation, lcd_test) :
                       inspect((IOHIDDeviceRef)device, other_speed);
    }
    if (devices) CFRelease(devices);
    CFRelease(manager);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fputs("Cannot write probe output.\n", stderr);
        return 1;
    }
    return ok ? 0 : 1;

bad_arguments:
    usage(stderr, argv[0]);
    return 2;
}
