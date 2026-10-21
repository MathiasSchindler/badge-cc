#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winusb.h>
#include <bcrypt.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include "picoctl_protocol.h"
#include "meshtastic.h"
#include "lorawan.h"

static const GUID usb_guid = {0xa5dcbf10, 0x6530, 0x11d2,
                             {0x90, 0x1f, 0x00, 0xc0, 0x4f, 0xb9, 0x51, 0xed}};
static const GUID winusb_guid = {0xdee824ef, 0x729b, 0x4a0e,
                                {0x9c, 0x14, 0xb7, 0x11, 0x7d, 0x33, 0xa8, 0x17}};
static const GUID picoboot_guid = {0xbc7398c1, 0x73cd, 0x4cb7,
                                  {0x98, 0xb8, 0x91, 0x3a, 0x8f, 0xca, 0x7b, 0xf6}};
static const GUID picoctl_guid = {0xd17518e7, 0xbd1d, 0x4c3d,
                                  {0x81, 0xa0, 0x56, 0x05, 0xca, 0xfe, 0x40, 0x27}};

typedef struct {
    HANDLE file;
    WINUSB_INTERFACE_HANDLE iface;
} usb_t;

typedef struct {
    const wchar_t *command, *image, *picotool, *path, *psk_file;
    char serial[17];
    wchar_t wide_serial[17];
    uint32_t build, count, from_mhz, to_mhz;
    uint8_t sha256[32];
    DWORD timeout;
    GUID guid;
    int has_serial, has_build, has_hash, has_guid, has_timeout, wait_ip, hex;
} options_t;

static void win_error(const wchar_t *operation, DWORD error) {
    wchar_t *message = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, (wchar_t *)&message, 0, NULL);
    fwprintf(stderr, L"%ls: Windows error %lu: %ls\n", operation,
             (unsigned long)error, message ? message : L"(no message)");
    LocalFree(message);
}

static int failure(const wchar_t *message) {
    fwprintf(stderr, L"%ls\n", message);
    return 0;
}

static int ascii(const wchar_t *wide, char *text, size_t capacity) {
    size_t i;
    for (i = 0; wide[i]; ++i) {
        if (i + 1 >= capacity || wide[i] > 127) return 0;
        text[i] = (char)wide[i];
    }
    text[i] = 0;
    return 1;
}

static void usage(void) {
    puts("picoctl list\n"
         "picoctl status --serial HEX16 --build-id HEX8 [--wait-ip]\n"
         "picoctl wifi --serial HEX16 --build-id HEX8\n"
         "picoctl listen --serial HEX16 [--build-id HEX8] [--psk-file PATH] [--count N] [--hex]\n"
         "picoctl survey --serial HEX16 [--build-id HEX8] [--from MHZ] [--to MHZ]\n"
         "picoctl bootsel --serial HEX16\n"
         "picoctl upload --serial HEX16 --build-id HEX8 --uf2 PATH --sha256 HEX64 --picotool PATH [--wait-ip]\n"
         "picoctl check-image --uf2 PATH --sha256 HEX64\n"
         "Options: --timeout SECONDS (1..300, default 60; listen: 1..86400), --interface-guid GUID,\n"
         "         --device-path PATH (explicit existing WinUSB interface).\n"
         "listen streams received LoRa frames over USB until --timeout/--count (default: forever).\n"
         "--psk-file lines are \"CHANNEL_NAME BASE64_PSK\"; keys are used on this host only.\n"
         "The default LongFast channel key (AQ==) is always tried.\n"
         "survey measures received power per MHz (default 700..960, range 150..960; receive-only).\n"
         "No driver installation, CDC baud reset, or arbitrary-device selection.\n"
         "upload overwrites selected Pico firmware; status/list/check-image do not reset it.");
}

static int parse(int argc, wchar_t **argv, options_t *o) {
    memset(o, 0, sizeof(*o));
    o->timeout = 60000;
    if (argc < 2) return failure(L"A command is required; use --help.");
    o->command = argv[1];
    if (!wcscmp(o->command, L"--help")) return argc == 2;
    if (wcscmp(o->command, L"list") && wcscmp(o->command, L"status") && wcscmp(o->command, L"wifi") &&
        wcscmp(o->command, L"listen") && wcscmp(o->command, L"survey") &&
        wcscmp(o->command, L"bootsel") && wcscmp(o->command, L"upload") &&
        wcscmp(o->command, L"check-image")) return failure(L"Unknown command.");
    unsigned seen = 0;
    for (int i = 2; i < argc; ++i) {
        const wchar_t *key = argv[i], *value;
        unsigned bit;
        if (!wcscmp(key, L"--wait-ip")) {
            if (o->wait_ip) return failure(L"Duplicate --wait-ip.");
            o->wait_ip = 1;
            continue;
        }
        if (!wcscmp(key, L"--hex")) {
            if (o->hex) return failure(L"Duplicate --hex.");
            o->hex = 1;
            continue;
        }
        if (++i == argc) return failure(L"Missing option value.");
        value = argv[i];
        char text[65];
        if (!wcscmp(key, L"--serial")) {
            bit = 1;
            if (!ascii(value, text, sizeof(text)) || !picoctl_serial(text, o->serial))
                return failure(L"--serial requires exactly 16 hexadecimal digits.");
            for (unsigned j = 0; j < 17; ++j) o->wide_serial[j] = (wchar_t)o->serial[j];
            o->has_serial = 1;
        } else if (!wcscmp(key, L"--build-id")) {
            uint8_t bytes[4];
            bit = 2;
            if (!ascii(value, text, sizeof(text)) || !picoctl_hex(text, bytes, 4))
                return failure(L"--build-id requires exactly 8 hexadecimal digits.");
            o->build = (uint32_t)bytes[0] << 24u | (uint32_t)bytes[1] << 16u |
                       (uint32_t)bytes[2] << 8u | bytes[3];
            o->has_build = 1;
        } else if (!wcscmp(key, L"--sha256")) {
            bit = 4;
            if (!ascii(value, text, sizeof(text)) || !picoctl_hex(text, o->sha256, 32))
                return failure(L"--sha256 requires exactly 64 hexadecimal digits.");
            o->has_hash = 1;
        } else if (!wcscmp(key, L"--uf2")) {
            bit = 8; o->image = value;
        } else if (!wcscmp(key, L"--picotool")) {
            bit = 16; o->picotool = value;
        } else if (!wcscmp(key, L"--device-path")) {
            bit = 32; o->path = value;
        } else if (!wcscmp(key, L"--interface-guid")) {
            bit = 64;
            if (CLSIDFromString(value, &o->guid) != S_OK)
                return failure(L"Invalid --interface-guid.");
            o->has_guid = 1;
        } else if (!wcscmp(key, L"--timeout")) {
            wchar_t *end;
            bit = 128;
            unsigned long seconds = wcstoul(value, &end, 10);
            unsigned long limit = wcscmp(o->command, L"listen") ? 300ul : 86400ul;
            if (!*value || *end || seconds < 1 || seconds > limit)
                return failure(limit == 300ul ? L"--timeout must be 1..300 seconds."
                                              : L"listen --timeout must be 1..86400 seconds.");
            o->timeout = (DWORD)seconds * 1000;
            o->has_timeout = 1;
        } else if (!wcscmp(key, L"--psk-file")) {
            bit = 256; o->psk_file = value;
        } else if (!wcscmp(key, L"--count")) {
            wchar_t *end;
            bit = 512;
            unsigned long count = wcstoul(value, &end, 10);
            if (!*value || *end || count < 1 || count > 1000000)
                return failure(L"--count must be 1..1000000.");
            o->count = (uint32_t)count;
        } else if (!wcscmp(key, L"--from") || !wcscmp(key, L"--to")) {
            wchar_t *end;
            int from = !wcscmp(key, L"--from");
            bit = from ? 1024 : 2048;
            unsigned long mhz = wcstoul(value, &end, 10);
            if (!*value || *end || mhz < 150 || mhz > 960)
                return failure(L"--from/--to must be 150..960 MHz.");
            if (from) o->from_mhz = (uint32_t)mhz; else o->to_mhz = (uint32_t)mhz;
        } else return failure(L"Unknown option.");
        if (seen & bit) return failure(L"Duplicate option.");
        seen |= bit;
    }
    if (!wcscmp(o->command, L"listen")) {
        if (!o->has_serial) return failure(L"Explicit --serial is required.");
        if (seen & ~(1u | 2u | 32u | 64u | 128u | 256u | 512u) || o->wait_ip)
            return failure(L"listen takes --serial, --build-id, --psk-file, --count, --hex, --timeout and interface options.");
        return 1;
    }
    if (!wcscmp(o->command, L"survey")) {
        if (!o->has_serial) return failure(L"Explicit --serial is required.");
        if (seen & ~(1u | 2u | 32u | 64u | 1024u | 2048u) || o->wait_ip || o->hex)
            return failure(L"survey takes --serial, --build-id, --from, --to and interface options.");
        if (!o->from_mhz) o->from_mhz = 700;
        if (!o->to_mhz) o->to_mhz = 960;
        if (o->from_mhz > o->to_mhz) return failure(L"--from must not exceed --to.");
        return 1;
    }
    if (seen & (1024u | 2048u)) return failure(L"--from and --to are only valid for survey.");
    if (o->hex || (seen & (256u | 512u)))
        return failure(L"--psk-file, --count and --hex are only valid for listen.");
    if (!wcscmp(o->command, L"list"))
        return seen == 0 && !o->wait_ip ? 1 : failure(L"list takes no options.");
    if (!wcscmp(o->command, L"check-image"))
        return o->image && o->has_hash && !(seen & ~12u) && !o->wait_ip ?
               1 : failure(L"check-image requires only --uf2 and --sha256.");
    if (!o->has_serial) return failure(L"Explicit --serial is required.");
    if (!wcscmp(o->command, L"wifi") && o->wait_ip)
        return failure(L"wifi does not take --wait-ip.");
    if (!wcscmp(o->command, L"bootsel")) {
        if (o->has_build || o->image || o->has_hash || o->picotool || o->wait_ip)
            return failure(L"bootsel does not take image/build/status options.");
    } else if (!o->has_build) return failure(L"--build-id is required.");
    if (!wcscmp(o->command, L"upload")) {
        if (!o->image || !o->has_hash || !o->picotool)
            return failure(L"upload requires --uf2, --sha256 and --picotool.");
    } else if (o->image || o->has_hash || o->picotool)
        return failure(L"Image options are only valid for upload/check-image.");
    return 1;
}

static void usb_close(usb_t *usb) {
    if (usb->iface) WinUsb_Free(usb->iface);
    if (usb->file != INVALID_HANDLE_VALUE) CloseHandle(usb->file);
    usb->file = INVALID_HANDLE_VALUE;
    usb->iface = NULL;
}

static DWORD usb_open(usb_t *usb, const wchar_t *path) {
    usb->iface = NULL;
    usb->file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, NULL);
    if (usb->file == INVALID_HANDLE_VALUE) return GetLastError();
    if (!WinUsb_Initialize(usb->file, &usb->iface)) {
        DWORD error = GetLastError();
        usb_close(usb);
        return error;
    }
    return ERROR_SUCCESS;
}

static int transfer_index(usb_t *usb, UCHAR type, UCHAR request, USHORT value,
                          USHORT index, uint8_t *data, USHORT length) {
    WINUSB_SETUP_PACKET setup = {type, request, value, index, length};
    OVERLAPPED ov = {0};
    ULONG transferred = 0;
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) { win_error(L"CreateEvent", GetLastError()); return 0; }
    BOOL ok = WinUsb_ControlTransfer(usb->iface, setup, data, length, &transferred, &ov);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && error == ERROR_IO_PENDING) {
        DWORD wait = WaitForSingleObject(ov.hEvent, 2000);
        if (wait != WAIT_OBJECT_0) {
            error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            CancelIoEx(usb->file, &ov);
            WinUsb_GetOverlappedResult(usb->iface, &ov, &transferred, TRUE);
            ok = FALSE;
        } else {
            ok = WinUsb_GetOverlappedResult(usb->iface, &ov, &transferred, FALSE);
            error = ok ? ERROR_SUCCESS : GetLastError();
        }
    }
    CloseHandle(ov.hEvent);
    if (!ok) { win_error(L"USB control transfer", error); return 0; }
    if (transferred != length) return failure(L"Short USB control transfer.");
    return 1;
}

static int transfer(usb_t *usb, UCHAR type, UCHAR request, USHORT value,
                    uint8_t *data, USHORT length) {
    return transfer_index(usb, type, request, value, 0, data, length);
}

static int matches(DEVINST node, const wchar_t *serial, const wchar_t *product) {
    wchar_t id[MAX_DEVICE_ID_LEN];
    for (unsigned depth = 0; depth < 8; ++depth) {
        CONFIGRET result = CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0);
        if (result != CR_SUCCESS) return 0;
        const wchar_t *tail = wcsrchr(id, L'\\');
        if (wcsstr(id, product) && tail && !_wcsicmp(tail + 1, serial)) return 1;
        DEVINST parent;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
        node = parent;
    }
    return 0;
}

/* BOOTSEL and application devices need not use the same interface GUID or USB address. */
static int discover(const options_t *o, int boot, int listing, usb_t *result) {
    GUID guids[5] = {usb_guid, picoctl_guid, winusb_guid, picoboot_guid, {0}};
    if (boot) guids[0] = picoboot_guid;
    if (o->has_guid) guids[4] = o->guid;
    unsigned count = o->has_guid ? 5 : 4;
    int found = 0;
    DWORD open_error = ERROR_NOT_FOUND;
    if (o->path && !boot && !listing) {
        DWORD error = usb_open(result, o->path);
        if (error) win_error(L"Open explicit WinUSB path", error);
        return error ? -1 : 1;
    }
    for (unsigned g = 0; g < (listing || boot ? 1u : count); ++g) {
        HDEVINFO devices = SetupDiGetClassDevsW(&guids[g], NULL, NULL,
                                                DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devices == INVALID_HANDLE_VALUE) {
            win_error(L"Enumerate USB interfaces", GetLastError());
            return -1;
        }
        for (DWORD i = 0;; ++i) {
            SP_DEVICE_INTERFACE_DATA iface = {0};
            iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(devices, NULL, &guids[g], i, &iface)) {
                DWORD error = GetLastError();
                if (error != ERROR_NO_MORE_ITEMS) {
                    win_error(L"Read USB interface", error);
                    SetupDiDestroyDeviceInfoList(devices);
                    if (found && !boot && !listing) usb_close(result);
                    return -1;
                }
                break;
            }
            DWORD size = 0;
            SetupDiGetDeviceInterfaceDetailW(devices, &iface, NULL, 0, &size, NULL);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                win_error(L"Measure USB interface path", GetLastError());
                SetupDiDestroyDeviceInfoList(devices);
                if (found && !boot && !listing) usb_close(result);
                return -1;
            }
            PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = malloc(size);
            if (!detail) {
                SetupDiDestroyDeviceInfoList(devices);
                if (found && !boot && !listing) usb_close(result);
                return failure(L"Out of memory enumerating USB.") - 1;
            }
            detail->cbSize = sizeof(*detail);
            SP_DEVINFO_DATA device = {0};
            device.cbSize = sizeof(device);
            if (!SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, size, NULL, &device)) {
                win_error(L"Read USB interface path", GetLastError());
                free(detail);
                SetupDiDestroyDeviceInfoList(devices);
                if (found && !boot && !listing) usb_close(result);
                return -1;
            }
            if (listing) {
                wchar_t id[MAX_DEVICE_ID_LEN];
                CONFIGRET cr = CM_Get_Device_IDW(device.DevInst, id, MAX_DEVICE_ID_LEN, 0);
                if (cr != CR_SUCCESS) {
                    fwprintf(stderr, L"Cannot read USB device ID: CONFIGRET %lu\n", (unsigned long)cr);
                    free(detail);
                    SetupDiDestroyDeviceInfoList(devices);
                    return -1;
                }
                if (wcsstr(id, L"VID_2E8A") || wcsstr(id, L"VID_CAFE&PID_4027")) {
                    printf("%ls\n", id);
                    ++found;
                }
            } else if (matches(device.DevInst, o->wide_serial,
                               boot ? L"VID_2E8A&PID_000F" : L"VID_CAFE&PID_4027")) {
                if (boot) {
                    usb_t rom = {INVALID_HANDLE_VALUE, NULL};
                    DWORD error = usb_open(&rom, detail->DevicePath);
                    if (!error) {
                        ++found;
                        usb_close(&rom);
                    }
                }
                else if (!found) {
                    open_error = usb_open(result, detail->DevicePath);
                    if (!open_error) found = 1;
                }
            }
            free(detail);
        }
        SetupDiDestroyDeviceInfoList(devices);
    }
    if (!found && !boot && open_error != ERROR_NOT_FOUND) {
        win_error(L"Open selected application WinUSB interface", open_error);
        return -1;
    }
    return found;
}

static int identify(usb_t *usb, const options_t *o, int enforce_build) {
    uint8_t device[18], info[12], nonce[16], record[64];
    if (!transfer(usb, 0x80, 6, 0x100, device, 18)) return 0;
    if (device[0] != 18 || device[1] != 1 || device[7] != 64 ||
        device[8] != 0xfe || device[9] != 0xca || device[10] != 0x27 || device[11] != 0x40)
        return failure(L"Selected USB device is not the Pico diagnostic interface.");
    if (!transfer(usb, 0xc0, 0x53, 0, info, 12)) return 0;
    const uint8_t prefix[8] = {'P', '2', 'W', 'L', 1, 0, 64, 0};
    if (memcmp(info, prefix, 8)) return failure(L"Unsupported Pico protocol.");
    uint32_t build = usb_load_u32(info + 8);
    if (enforce_build && build != o->build) return failure(L"Running build ID differs from expected build.");
    uint8_t any;
    do {
        NTSTATUS status = BCryptGenRandom(NULL, nonce, sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0) {
            fwprintf(stderr, L"BCryptGenRandom failed: 0x%08lx\n", (unsigned long)status);
            return 0;
        }
        any = 0;
        for (unsigned i = 0; i < sizeof(nonce); ++i) any |= nonce[i];
    } while (!any);
    usb_report_request(record, build, nonce);
    if (!transfer(usb, 0x40, 0x50, 0, record, 64) ||
        !transfer(usb, 0xc0, 0x51, 0, record, 64)) return 0;
    if (!picoctl_identity(record, build, nonce, o->serial))
        return failure(L"Chip ID, nonce, build or diagnostic report integrity mismatch.");
    return 1;
}

static void print_status(const options_t *o, const picoctl_status_t *s) {
    unsigned a = s->ip >> 24u, b = (s->ip >> 16u) & 255u;
    unsigned c = (s->ip >> 8u) & 255u, d = s->ip & 255u;
    printf("{\"serial\":\"%s\",\"build_id\":\"%08lx\",\"identity_verified\":true,"
           "\"wifi_stage\":%lu,\"wifi_error\":%ld,\"dhcp_state\":%lu,"
           "\"network_error\":%lu,\"lora_error\":%ld,\"ip\":\"%u.%u.%u.%u\","
           "\"received\":%lu,\"network_drops\":%lu,\"network_tx\":%lu,"
           "\"uptime_ms\":%lu,\"wifi_connected\":%s,\"wifi_controller_status\":%lu,"
           "\"network_ready\":%s,\"url\":",
           o->serial, (unsigned long)o->build, (unsigned long)s->wifi_stage,
           (long)s->wifi_error, (unsigned long)s->dhcp_state, (unsigned long)s->network_error,
           (long)s->lora_error, a, b, c, d, (unsigned long)s->received,
           (unsigned long)s->dropped, (unsigned long)s->transmitted,
           (unsigned long)s->uptime_ms, s->connected ? "true" : "false",
           (unsigned long)s->controller_status, picoctl_network_ready(s) ? "true" : "false");
    if (picoctl_network_ready(s)) printf("\"http://%u.%u.%u.%u/\"", a, b, c, d);
    else printf("null");
    puts("}");
}

static int status_wait(const options_t *o) {
    ULONGLONG deadline = GetTickCount64() + o->timeout;
    int observed = 0;
    picoctl_status_t last = {0};
    while (GetTickCount64() < deadline) {
        usb_t usb = {INVALID_HANDLE_VALUE, NULL};
        int found = discover(o, 0, 0, &usb);
        if (found < 0) return 0;
        if (found) {
            uint8_t record[64];
            picoctl_status_t status;
            int ok = identify(&usb, o, 1) && transfer(&usb, 0xc0, 0x56, 0, record, 64);
            usb_close(&usb);
            if (!ok) return 0;
            if (!picoctl_status(record, o->build, &status)) return failure(L"Invalid monitor status.");
            observed = 1;
            last = status;
            if (!o->wait_ip || picoctl_network_ready(&status) ||
                status.wifi_error || status.network_error || status.wifi_stage == 8 ||
                status.dhcp_state == 5) {
                print_status(o, &status);
                if (o->wait_ip && !picoctl_network_ready(&status))
                    return failure(L"Device reported network failure; see status JSON.");
                return 1;
            }
        }
        Sleep(250);
    }
    if (observed) print_status(o, &last);
    return failure(observed ? L"Timed out waiting for DHCP/network readiness." :
                   L"No accessible selected application interface. Check firmware enumeration, WinUSB binding and interface GUID.");
}

static int wifi_status(const options_t *o) {
    usb_t usb = {INVALID_HANDLE_VALUE, NULL};
    int found = discover(o, 0, 0, &usb);
    if (found <= 0) return found < 0 ? 0 : failure(L"No accessible selected application interface.");
    uint8_t record[64];
    int ok = identify(&usb, o, 1) && transfer(&usb, 0xc0, 0x58, 0, record, 64);
    usb_close(&usb);
    if (!ok) return 0;
    if (!picoctl_wifi_diagnostic(record, o->build))
        return failure(L"Invalid Wi-Fi diagnostic record.");
    printf("{\"serial\":\"%s\",\"build_id\":\"%08lx\",\"identity_verified\":true",
           o->serial, (unsigned long)o->build);
    const char *names[] = {"step", "address", "expected", "actual", "completed",
                           "command", "stage", "error", "controller_status",
                           "event_type", "event_status", "event_reason"};
    for (unsigned i = 0; i < 12; ++i)
        printf(",\"%s\":%lu", names[i], (unsigned long)usb_load_u32(record + 12 + 4 * i));
    puts("}");
    return 1;
}

static int load_channels(const wchar_t *path, mesh_channel_t *channels, uint32_t *count) {
    FILE *file = _wfopen(path, L"rb");
    char line[256];
    unsigned number = 0;
    int ok = 1;
    if (!file) return failure(L"Cannot open --psk-file.");
    while (ok && fgets(line, sizeof(line), file)) {
        char *text = line;
        ++number;
        if (number == 1 && (unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
            (unsigned char)text[2] == 0xbf) text += 3;
        while (*text == ' ' || *text == '\t') ++text;
        if (!*text || *text == '\r' || *text == '\n' || *text == '#') continue;
        if (!strchr(text, '\n') && !feof(file)) {
            fwprintf(stderr, L"--psk-file line %u is too long.\n", number);
            ok = 0;
        } else if (*count == MESH_MAX_CHANNELS) {
            ok = failure(L"Too many channels in --psk-file.");
        } else if (!mesh_channel_parse(channels + *count, text)) {
            /* Never echo the line: it holds key material. */
            fwprintf(stderr, L"--psk-file line %u must be \"CHANNEL_NAME BASE64_PSK\".\n", number);
            ok = 0;
        } else ++*count;
    }
    SecureZeroMemory(line, sizeof(line));
    fclose(file);
    return ok;
}

static void print_packet(const picoctl_packet_t *p, const mesh_channel_t *channels,
                         uint32_t count, int hex) {
    SYSTEMTIME now;
    char line[1536];
    GetLocalTime(&now);
    printf("%02u:%02u:%02u #%lu %.3f MHz SF%lu/%lu RSSI %.1f dBm SNR %.2f dB %lu B | ",
           now.wHour, now.wMinute, now.wSecond, (unsigned long)p->sequence,
           p->frequency_hz / 1e6, (unsigned long)p->sf, (unsigned long)p->bandwidth_khz,
           p->rssi_qdb / 4.0, p->snr_qdb / 4.0, (unsigned long)p->length);
    if (p->flags & 0x80u) {
        fputs("CRC ERROR: payload failed the radio's CRC, not decoded", stdout);
        hex = 1;
    } else if (p->profile == 1u) {
        mesh_describe(p->payload, p->length, channels, count, line, sizeof(line));
        fputs(line, stdout);
    } else {
        lorawan_describe(p->payload, p->length, line, sizeof(line));
        fputs(line, stdout);
    }
    putchar('\n');
    if (hex) {
        printf("    ");
        for (uint32_t i = 0; i < p->length; ++i) printf("%02x", p->payload[i]);
        putchar('\n');
    }
    fflush(stdout);
}

/* Periodic stderr line so silence can be told apart from a dead RF path. */
static void print_radio_stats(const picoctl_packet_t *p) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(stderr, "%02u:%02u:%02u radio: rx=%lu crc_err=%lu hdr_err=%lu",
            now.wHour, now.wMinute, now.wSecond, (unsigned long)p->packets,
            (unsigned long)p->crc_errors, (unsigned long)p->header_errors);
    if (p->noise_samples)
        fprintf(stderr, " noise now %.1f min %.1f max %.1f dBm (%lu samples since config)\n",
                p->noise_qdb / 4.0, p->noise_min_qdb / 4.0, p->noise_max_qdb / 4.0,
                (unsigned long)p->noise_samples);
    else fputs(" noise: no sample yet\n", stderr);
}

static int listen_packets(const options_t *o) {
    mesh_channel_t channels[MESH_MAX_CHANNELS];
    uint32_t count = 0, last = 0, printed = 0, build = 0;
    const uint8_t default_index = 1;
    int connected = 0, ok = 1;
    usb_t usb = {INVALID_HANDLE_VALUE, NULL};
    mesh_channel_init(channels + count++, "LongFast", &default_index, 1);
    if (o->psk_file && !load_channels(o->psk_file, channels, &count)) {
        SecureZeroMemory(channels, sizeof(channels));
        return 0;
    }
    SetConsoleOutputCP(CP_UTF8);
    fprintf(stderr, "Channels:");
    for (uint32_t i = 0; i < count; ++i)
        fprintf(stderr, " %s(hash 0x%02x)", channels[i].name, channels[i].hash);
    fputc('\n', stderr);
    ULONGLONG deadline = o->has_timeout ? GetTickCount64() + o->timeout : 0;
    ULONGLONG next_stats = 0; /* first line right after connecting */
    while (!deadline || GetTickCount64() < deadline) {
        uint8_t record[PICOCTL_PACKET_SIZE];
        picoctl_packet_t p;
        if (!connected) {
            uint8_t info[12];
            int found = discover(o, 0, 0, &usb);
            if (found < 0) { ok = 0; break; }
            if (!found) { Sleep(500); continue; }
            if (!identify(&usb, o, o->has_build) || !transfer(&usb, 0xc0, 0x53, 0, info, 12)) {
                usb_close(&usb);
                ok = 0;
                break;
            }
            build = usb_load_u32(info + 8);
            connected = 1;
            fprintf(stderr, "Listening to Pico %s (build %08lx) over USB; Ctrl+C to stop.\n",
                    o->serial, (unsigned long)build);
        }
        if (!transfer_index(&usb, 0xc0, 0x59, (USHORT)last, (USHORT)(last >> 16),
                            record, PICOCTL_PACKET_SIZE)) {
            usb_close(&usb);
            connected = 0;
            fprintf(stderr, "USB link lost (old firmware without request 0x59, or a reset); reconnecting.\n");
            Sleep(1000);
            continue;
        }
        if (!picoctl_packet(record, build, &p)) {
            usb_close(&usb);
            ok = failure(L"Invalid LoRa packet record (CRC/format/build).");
            break;
        }
        if ((int32_t)(p.newest - last) < 0) {
            fprintf(stderr, "Device packet sequence restarted (firmware reset).\n");
            last = 0;
            continue;
        }
        if (GetTickCount64() >= next_stats) {
            next_stats = GetTickCount64() + 60000u;
            print_radio_stats(&p);
        }
        if (!p.present) { Sleep(200); continue; }
        if (last && p.sequence != last + 1)
            fprintf(stderr, "(missed %lu packets: device ring overwritten)\n",
                    (unsigned long)(p.sequence - last - 1));
        print_packet(&p, channels, count, o->hex);
        last = p.sequence;
        if (o->count && ++printed >= o->count) break;
    }
    if (connected) usb_close(&usb);
    SecureZeroMemory(channels, sizeof(channels));
    return ok;
}

/* Common German allocations, to make the survey readable. */
static const char *band_name(uint32_t mhz) {
    if (mhz >= 703 && mhz < 733) return "LTE 700 uplink (phones)";
    if (mhz >= 758 && mhz < 788) return "LTE 700 downlink (towers)";
    if (mhz >= 791 && mhz < 821) return "LTE 800 downlink (towers)";
    if (mhz >= 832 && mhz < 862) return "LTE 800 uplink (phones)";
    if (mhz >= 863 && mhz <= 870) return "SRD 868: LoRa/Meshtastic";
    if (mhz >= 880 && mhz < 915) return "GSM/LTE 900 uplink (phones)";
    if (mhz >= 925 && mhz <= 960) return "GSM/LTE 900 downlink (towers)";
    return "";
}

static int survey_spectrum(const options_t *o) {
    usb_t usb = {INVALID_HANDLE_VALUE, NULL};
    uint8_t info[12];
    int found = discover(o, 0, 0, &usb);
    if (found <= 0) return found < 0 ? 0 : failure(L"Selected Pico application is not connected.");
    if (!identify(&usb, o, o->has_build) || !transfer(&usb, 0xc0, 0x53, 0, info, 12)) {
        usb_close(&usb);
        return 0;
    }
    uint32_t build = usb_load_u32(info + 8), points = o->to_mhz - o->from_mhz + 1;
    int32_t mean[811], peak[811];
    uint32_t bandwidth = 0;
    fprintf(stderr, "Surveying %lu..%lu MHz on Pico %s (build %08lx); receive-only.\n",
            (unsigned long)o->from_mhz, (unsigned long)o->to_mhz, o->serial, (unsigned long)build);
    int ok = 1;
    for (uint32_t i = 0; i < points && ok; ++i) {
        uint32_t mhz = o->from_mhz + i, khz = mhz * 1000u;
        picoctl_survey_t s = {0};
        ULONGLONG deadline = GetTickCount64() + 2000;
        for (;;) {
            uint8_t record[PICOCTL_SURVEY_SIZE];
            if (!transfer_index(&usb, 0xc0, 0x5a, (USHORT)khz, (USHORT)(khz >> 16),
                                record, PICOCTL_SURVEY_SIZE)) {
                ok = failure(L"Survey request failed (firmware without request 0x5a?).");
                break;
            }
            if (!picoctl_survey(record, build, khz, &s)) {
                ok = failure(L"Invalid survey record (CRC/format/build/frequency).");
                break;
            }
            if (s.state != 1u) break;
            if (GetTickCount64() > deadline) { ok = failure(L"Survey timed out."); break; }
            Sleep(5);
        }
        if (!ok) break;
        if (s.state == 3u) {
            fprintf(stderr, "Survey at %lu MHz failed: error %ld.\n", (unsigned long)mhz, (long)s.error);
            ok = 0;
            break;
        }
        bandwidth = s.bandwidth_khz;
        mean[i] = s.mean_qdb;
        peak[i] = s.max_qdb;
        /* Bar from -125 dBm (empty) to -45 dBm (full), 2 dB per character. */
        int bars = (s.max_qdb / 4 + 125) / 2;
        if (bars < 0) bars = 0;
        if (bars > 40) bars = 40;
        char bar[41];
        memset(bar, '#', (size_t)bars);
        bar[bars] = 0;
        printf("%4lu MHz  mean %7.1f  max %7.1f dBm  %-40s %s\n", (unsigned long)mhz,
               s.mean_qdb / 4.0, s.max_qdb / 4.0, bar, band_name(mhz));
        fflush(stdout);
    }
    usb_close(&usb);
    if (!ok) return 0;
    /* Floor = the quietest 25% of the sweep; strongest = the loudest point. */
    int32_t sorted[811];
    for (uint32_t i = 0; i < points; ++i) {
        uint32_t j = i;
        while (j && sorted[j - 1] > mean[i]) { sorted[j] = sorted[j - 1]; --j; }
        sorted[j] = mean[i];
    }
    /* Real carriers span several MHz; single-step spikes are usually on-board
       clock harmonics. Score each step by the weakest mean of 3 neighbours. */
    int32_t floor = sorted[points / 4], best = -32768, spike = -32768;
    uint32_t best_mhz = o->from_mhz, spike_mhz = o->from_mhz, width = points < 3 ? points : 3;
    for (uint32_t i = 0; i + width <= points; ++i) {
        int32_t level = mean[i];
        for (uint32_t k = 1; k < width; ++k) if (mean[i + k] < level) level = mean[i + k];
        if (level > best) { best = level; best_mhz = o->from_mhz + i + width / 2; }
    }
    for (uint32_t i = 0; i < points; ++i)
        if (peak[i] > spike) { spike = peak[i]; spike_mhz = o->from_mhz + i; }
    printf("\nMeasured in %lu kHz bandwidth. Floor (25th percentile) %.1f dBm.\n"
           "Strongest %lu MHz-wide signal %.1f dBm around %lu MHz (+%.1f dB); strongest single step %.1f dBm at %lu MHz.\n",
           (unsigned long)bandwidth, floor / 4.0, (unsigned long)width, best / 4.0,
           (unsigned long)best_mhz, (best - floor) / 4.0, spike / 4.0, (unsigned long)spike_mhz);
    if (best - floor >= 15 * 4)
        puts("Verdict: clear signals well above the floor - the antenna path receives.");
    else if (best - floor >= 6 * 4)
        puts("Verdict: weak signals above the floor - the path receives, but poorly or far from transmitters.");
    else
        puts("Verdict: nothing stands out from the floor - suspect the antenna/RF path (or no transmitters in range).");
    return 1;
}

static int enter_bootsel(const options_t *o) {
    int boot = discover(o, 1, 0, NULL);
    if (boot < 0) return 0;
    if (boot > 0) return 1;
    usb_t usb = {INVALID_HANDLE_VALUE, NULL};
    int found = discover(o, 0, 0, &usb);
    if (found <= 0) {
        if (!found) failure(L"Selected Pico is neither in BOOTSEL nor accessible via application WinUSB. Use physical BOOTSEL or SWD.");
        return 0;
    }
    int ok = identify(&usb, o, 0) && transfer(&usb, 0x40, 0x52, 0, NULL, 0);
    usb_close(&usb);
    if (!ok) return 0;
    ULONGLONG deadline = GetTickCount64() + o->timeout;
    while (GetTickCount64() < deadline) {
        boot = discover(o, 1, 0, NULL);
        if (boot < 0) return 0;
        if (boot > 0) return 1;
        Sleep(250);
    }
    return failure(L"BOOTSEL request acknowledged, but selected BOOTSEL device did not appear.");
}

static HANDLE check_image(const options_t *o) {
    HANDLE file = CreateFileW(o->image, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { win_error(L"Open UF2", GetLastError()); return file; }
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    LARGE_INTEGER size;
    uint8_t block[512], digest[32];
    int ok = 0;
    if (!GetFileSizeEx(file, &size)) { win_error(L"UF2 size", GetLastError()); goto done; }
    if (size.QuadPart <= 0 || size.QuadPart % 512 || size.QuadPart > 16384 * 512) {
        failure(L"Invalid UF2 size (expected dense image within 4 MiB flash)."); goto done;
    }
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0);
    if (status < 0) {
        fwprintf(stderr, L"SHA256 initialization failed: 0x%08lx\n", (unsigned long)status);
        goto done;
    }
    uint32_t count = (uint32_t)(size.QuadPart / 512);
    for (uint32_t i = 0; i < count; ++i) {
        DWORD read;
        if (!ReadFile(file, block, sizeof(block), &read, NULL)) {
            win_error(L"Read UF2", GetLastError()); goto done;
        }
        if (read != sizeof(block) || !picoctl_uf2_block(block, i, count)) {
            failure(L"Invalid UF2 block, family, address, flags or ordering."); goto done;
        }
        status = BCryptHashData(hash, block, sizeof(block), 0);
        if (status < 0) { failure(L"SHA256 update failed."); goto done; }
    }
    status = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    if (status < 0) { failure(L"SHA256 finalization failed."); goto done; }
    if (memcmp(digest, o->sha256, sizeof(digest))) {
        failure(L"UF2 SHA256 does not match expected hash; refusing upload."); goto done;
    }
    ok = 1;
done:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) { CloseHandle(file); return INVALID_HANDLE_VALUE; }
    return file;
}

static int append(wchar_t *command, size_t capacity, size_t *used, wchar_t c) {
    if (*used + 1 >= capacity) return 0;
    command[(*used)++] = c;
    command[*used] = 0;
    return 1;
}

static int quote(wchar_t *command, size_t capacity, size_t *used, const wchar_t *arg) {
    if (*used && !append(command, capacity, used, L' ')) return 0;
    if (!append(command, capacity, used, L'"')) return 0;
    while (*arg) {
        unsigned slashes = 0;
        while (*arg == L'\\') { ++slashes; ++arg; }
        unsigned escaped = (*arg == L'"' || !*arg) ? slashes * 2u : slashes;
        for (unsigned i = 0; i < escaped; ++i)
            if (!append(command, capacity, used, L'\\')) return 0;
        if (!*arg) break;
        if (*arg == L'"' && !append(command, capacity, used, L'\\')) return 0;
        if (!append(command, capacity, used, *arg++)) return 0;
    }
    return append(command, capacity, used, L'"');
}

static int picotool(const options_t *o, const wchar_t *const *args, size_t count) {
    wchar_t command[32768] = {0};
    size_t used = 0;
    if (!quote(command, 32768, &used, o->picotool)) return failure(L"Command line too long.");
    for (size_t i = 0; i < count; ++i)
        if (!quote(command, 32768, &used, args[i])) return failure(L"Command line too long.");
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job) { win_error(L"Create process job", GetLastError()); return 0; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        win_error(L"Set process job limits", GetLastError()); CloseHandle(job); return 0;
    }
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &sa, OPEN_EXISTING, 0, NULL), output = NULL;
    if (input == INVALID_HANDLE_VALUE ||
        !DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE),
                         GetCurrentProcess(), &output, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
        win_error(L"Set picotool standard handles", GetLastError());
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        CloseHandle(job);
        return 0;
    }
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = startup.hStdError = output;
    PROCESS_INFORMATION process = {0};
    BOOL created = CreateProcessW(o->picotool, command, NULL, NULL, TRUE,
                                  CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL,
                                  &startup, &process);
    DWORD error = created ? 0 : GetLastError();
    CloseHandle(input);
    CloseHandle(output);
    if (!created) { win_error(L"Start picotool", error); CloseHandle(job); return 0; }
    int ok = 0;
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        win_error(L"Assign picotool process job", GetLastError());
        TerminateProcess(process.hProcess, 1);
    } else if (ResumeThread(process.hThread) == (DWORD)-1) {
        win_error(L"Resume picotool", GetLastError());
    } else {
        DWORD wait = WaitForSingleObject(process.hProcess, o->timeout);
        if (wait == WAIT_OBJECT_0) {
            DWORD code;
            if (!GetExitCodeProcess(process.hProcess, &code)) win_error(L"picotool exit code", GetLastError());
            else if (code) fwprintf(stderr, L"picotool failed with exit code %lu.\n", (unsigned long)code);
            else ok = 1;
        } else if (wait == WAIT_TIMEOUT) failure(L"picotool timed out; flash may be partially written. Device recovery is required.");
        else win_error(L"Wait for picotool", GetLastError());
    }
    CloseHandle(job);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ok;
}

static int upload(const options_t *o) {
    HANDLE image = check_image(o);
    if (image == INVALID_HANDLE_VALUE) return 0;
    int ok = 0;
    DWORD attributes = GetFileAttributesW(o->picotool);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        failure(L"--picotool must name an existing executable file (not a PATH search)."); goto done;
    }
    if (!enter_bootsel(o)) goto done;
    const wchar_t *load[] = {L"load", L"-v", o->image, L"--ser", o->wide_serial};
    if (!picotool(o, load, sizeof(load) / sizeof(load[0]))) goto done;
    fprintf(stderr, "Selected-device flash verification passed.\n");
    const wchar_t *reboot[] = {L"reboot", L"--ser", o->wide_serial};
    if (!picotool(o, reboot, sizeof(reboot) / sizeof(reboot[0]))) goto done;
    fprintf(stderr, "Reboot command completed; waiting for verified runtime status.\n");
    ok = status_wait(o);
done:
    CloseHandle(image);
    return ok;
}

int wmain(int argc, wchar_t **argv) {
    options_t options;
    if (!parse(argc, argv, &options)) return 2;
    wchar_t image_path[32768], tool_path[32768];
    if (options.image) {
        DWORD size = GetFullPathNameW(options.image, 32768, image_path, NULL);
        if (!size || size >= 32768) { failure(L"Cannot resolve UF2 absolute path."); return 2; }
        options.image = image_path;
    }
    if (options.picotool) {
        DWORD size = GetFullPathNameW(options.picotool, 32768, tool_path, NULL);
        if (!size || size >= 32768) { failure(L"Cannot resolve picotool absolute path."); return 2; }
        options.picotool = tool_path;
    }
    if (!wcscmp(options.command, L"--help")) { usage(); return 0; }
    if (!wcscmp(options.command, L"list")) {
        int found = discover(&options, 0, 1, NULL);
        if (found < 0) return 1;
        if (!found) fprintf(stderr, "No enumerated Pico/diagnostic USB devices found. Descriptor failures have no usable identity.\n");
        return 0;
    }
    if (!wcscmp(options.command, L"check-image")) {
        HANDLE image = check_image(&options);
        if (image == INVALID_HANDLE_VALUE) return 1;
        CloseHandle(image);
        puts("{\"uf2_valid\":true,\"sha256_verified\":true}");
        return 0;
    }
    if (!wcscmp(options.command, L"status")) return status_wait(&options) ? 0 : 1;
    if (!wcscmp(options.command, L"wifi")) return wifi_status(&options) ? 0 : 1;
    if (!wcscmp(options.command, L"listen")) return listen_packets(&options) ? 0 : 1;
    if (!wcscmp(options.command, L"survey")) return survey_spectrum(&options) ? 0 : 1;
    if (!wcscmp(options.command, L"bootsel")) {
        if (!enter_bootsel(&options)) return 1;
        printf("{\"serial\":\"%s\",\"bootsel_observed\":true}\n", options.serial);
        return 0;
    }
    return upload(&options) ? 0 : 1;
}
