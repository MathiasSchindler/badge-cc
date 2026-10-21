import hashlib
import importlib.util
import shutil
import sys
import subprocess
from pathlib import Path
import tempfile
import unittest
import zlib


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "wifi_assets.py"
SPEC = importlib.util.spec_from_file_location("wifi_assets", SCRIPT)
wifi_assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = wifi_assets
SPEC.loader.exec_module(wifi_assets)


def header_array(name, data):
    values = ", ".join(f"0x{byte:02x}" for byte in data)
    return f"const unsigned char {name}[] = {{{values}}};\n"


def fixture_headers():
    fw_size = 32
    clm_offset = 64
    clm_size = 68
    firmware = bytearray(clm_offset + clm_size)
    firmware[:8] = b"\0\0\0\0\x65\x14\0\0"
    firmware[fw_size - 16:fw_size] = b"DVID 01-b0b4520f"
    firmware[clm_offset:clm_offset + 8] = b"BLOB<\0\0\0"
    firmware[clm_offset + 60:clm_offset + 68] = b"CLM DATA"
    nvram = b"NVRAMRev=$Rev$\0" + b" " * 2 + b"btc_mode=1\0\0\0\0"
    pins = wifi_assets.AssetPins(
        "unused", "unused", fw_size, fw_size, clm_offset, clm_size,
        len(nvram), len(nvram) + 2,
        zlib.crc32(firmware[:fw_size]) & 0xFFFFFFFF,
        zlib.crc32(nvram) & 0xFFFFFFFF,
        zlib.crc32(firmware[clm_offset:]) & 0xFFFFFFFF,
    )
    fw_text = (
        f"#define CYW43_WIFI_FW_LEN {fw_size}\n"
        f"#define CYW43_CLM_LEN {clm_size}\n" +
        header_array(wifi_assets.FIRMWARE_NAME, firmware)
    )
    nvram_text = header_array(wifi_assets.NVRAM_NAME, nvram)
    return fw_text, nvram_text, pins


class WifiAssetsTests(unittest.TestCase):
    def test_renamed_standalone_tree_output_containment(self):
        with tempfile.TemporaryDirectory(dir=wifi_assets.BUILD_ROOT) as directory:
            project = Path(directory) / "renamed-monitor"
            tools = project / "tools"
            tools.mkdir(parents=True)
            copied = tools / "wifi_assets.py"
            shutil.copyfile(SCRIPT, copied)
            spec = importlib.util.spec_from_file_location("renamed_wifi_assets", copied)
            module = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = module
            try:
                spec.loader.exec_module(module)
                self.assertEqual(module.BUILD_ROOT, (project / "build").resolve())
                missing = project / "missing.h"
                # Getting as far as input validation proves output is accepted.
                with self.assertRaisesRegex(module.AssetError, "vendor header cannot be read"):
                    module.prepare_assets(project / "build" / "monitor",
                                          missing, missing, missing)
                for forbidden in (project / "src", project, project.parent / "outside"):
                    with self.assertRaisesRegex(module.AssetError, "output directory"):
                        module.prepare_assets(forbidden, missing, missing, missing)
            finally:
                del sys.modules[spec.name]

    def test_pinned_header_attribute_and_parenthesized_length_shape(self):
        fw_text, nvram_text, pins = fixture_headers()
        fw_text = fw_text.replace("[] =", "[] CYW43_RESOURCE_ATTRIBUTE =")
        fw_text = fw_text.replace("CYW43_WIFI_FW_LEN 32", "CYW43_WIFI_FW_LEN (32)")
        fw_text = fw_text.replace("CYW43_CLM_LEN 68", "CYW43_CLM_LEN (68)")
        nvram_text = nvram_text.replace("[] =", "[] CYW43_RESOURCE_ATTRIBUTE =")
        firmware, nvram, clm = wifi_assets.extract_assets(fw_text, nvram_text, pins)
        self.assertEqual(len(firmware), 32)
        self.assertEqual(len(nvram), pins.nvram_size)
        self.assertEqual(len(clm), 68)

    def test_nvram_c_strings_retain_implicit_terminal_null(self):
        text = r'const unsigned char wifi_nvram_4343[] = "a\0" "b\x00\000";'
        self.assertEqual(wifi_assets._array_bytes(text, wifi_assets.NVRAM_NAME),
                         b"a\0b\0\0\0")

    def test_station_failure_latching_and_service_callback(self):
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("gcc is unavailable")
        harness = r'''
#include "wifi_station.c"
const uint8_t wifi_firmware_data[WIFI_FIRMWARE_SIZE] = {0};
const uint8_t wifi_nvram_data[WIFI_NVRAM_SIZE] = {0};
const uint8_t wifi_clm_data[WIFI_CLM_SIZE] = {0};
static uint32_t ticks, calls, reads, last_command, last_id, request_size;
static uint32_t asset_writes;
static uint8_t radio_memory[128];
static uint32_t authorization_flags;
static int mode;
static void callback(void *context) { ++*(uint32_t *)context; }
static uint32_t now(void *context) { (void)context; return ticks; }
void wifi_wire_init(wifi_wire_t *wire) { wire->now_us = now; wire->context = 0; }
void wifi_wire_power_off(const wifi_wire_t *wire) { (void)wire; }
int wifi_wire_delay(const wifi_wire_t *wire, uint32_t duration) {
    (void)wire; ticks += duration; return 1;
}
int wifi_gspi_read(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                   uint8_t size, wifi_word_order_t order, uint32_t *value) {
    (void)wire; (void)function; (void)address; (void)size; (void)order;
    if (mode == 0) { *value = 0xffffffffu; return 1; }
    if (mode == 3) {
        *value = 0x120u | ((last_command == CMD_BSSID ? 34u : 284u) << 9u);
        return 1;
    }
    ++reads;
    *value = reads == 1u ? 0u : reads == 2u ? 0x20u : 0x100u | (34u << 9u);
    return 1;
}
int wifi_gspi_write(const wifi_wire_t *wire, uint8_t function, uint32_t address,
                    uint8_t size, wifi_word_order_t order, uint32_t value) {
    (void)wire; (void)function; (void)address; (void)size; (void)order; (void)value;
    return 1;
}
int wifi_wire_transfer(const wifi_wire_t *wire, const uint8_t *tx, uint32_t tx_size,
                       uint8_t *rx, uint32_t rx_size) {
    (void)wire;
    if (mode == 2) {
        uint32_t cmd = load32(tx);
        uint32_t address = (cmd >> 11u) & 0x7fffu, size = cmd & 0x7ffu;
        if (address + size > sizeof(radio_memory)) return 0;
        if (tx_size > 4u) {
            ++asset_writes;
            bytes_copy(radio_memory + address, tx + 4u, size);
        } else {
            bytes_zero(rx, rx_size);
            bytes_copy(rx + WIFI_GSPI_BACKPLANE_PAD, radio_memory + address, size);
        }
        return 1;
    }
    if (tx_size > 4u) {
        last_command = load32(tx + 16u);
        last_id = load32(tx + 24u) >> 16u;
        request_size = load32(tx + 20u);
    }
    if (rx_size != 0u) {
        bytes_zero(rx, rx_size);
        if (mode == 3) {
            uint32_t size = last_command == CMD_BSSID ? 6u : 256u;
            uint32_t total = size + 28u;
            rx[0] = (uint8_t)total; rx[1] = (uint8_t)(total >> 8u);
            rx[2] = (uint8_t)~total; rx[3] = (uint8_t)(~total >> 8u);
            rx[7] = 12u; rx[9] = 100u;
            store32(rx + 12u, last_command);
            store32(rx + 16u, size);
            store32(rx + 20u, last_id << 16u);
            if (last_command == CMD_BSSID) rx[28] = 2u;
            else {
                rx[28] = 4u; rx[31] = 1u;
                store32(rx + 36u, authorization_flags);
                rx[44] = 2u;
            }
            return 1;
        }
        rx[0] = 34u; rx[2] = 0xddu; rx[3] = 0xffu;
        rx[7] = 12u; rx[9] = 100u;
        store32(rx + 12u, last_command);
        store32(rx + 16u, 6u);
        store32(rx + 20u, last_id << 16u);
    }
    return 1;
}
int main(void) {
    unsigned int length = 9u;
    uint8_t frame[1514];
    wifi_station_set_service(callback, &calls);
    if (wifi_station_init("synthetic", "password") != 0 ||
        wifi_station_error() != WIFI_STATION_ERROR_ASSET || calls == 0u) return 1;
    bytes_zero((uint8_t *)&state, sizeof(state));
    state.wire.now_us = now; state.active = state.linked = 1u;
    wifi_station_service();
    if (wifi_station_stage() != WIFI_STATION_FAILED ||
        wifi_station_error() != WIFI_STATION_ERROR_BUS ||
        wifi_station_link_up() || wifi_station_read(frame, sizeof(frame), &length) != -1 ||
        length != 0u) return 2;
    wifi_station_service();
    if (wifi_station_error() != WIFI_STATION_ERROR_BUS) return 3;
    bytes_zero((uint8_t *)&state, sizeof(state));
    state.wire.now_us = now; state.active = state.linked = 1u;
    state.credit = 100u; mode = 1; reads = 0u; ticks = 600000u;
    wifi_station_service();
    if (wifi_station_stage() != WIFI_STATION_FAILED ||
        wifi_station_error() != WIFI_STATION_ERROR_DISCONNECTED ||
        wifi_station_link_up()) return 4;
    if (last_command != CMD_BSSID || request_size != 6u) return 5;
    uint8_t asset[128];
    for (uint32_t i = 0u; i < sizeof(asset); ++i) asset[i] = (uint8_t)i;
    mode = 2;
    state.window_valid = 0u;
    if (!load_asset(0u, asset, sizeof(asset), 0u) || asset_writes != 2u ||
        !load_asset(0u, asset, sizeof(asset), 1u) || asset_writes != 2u ||
        state.diagnostic.completed != sizeof(asset)) return 6;
    radio_memory[67] ^= 1u;
    if (load_asset(0u, asset, sizeof(asset), 1u) || asset_writes != 2u ||
        state.diagnostic.address != 67u || state.diagnostic.completed != 64u ||
        state.diagnostic.expected != 67u || state.diagnostic.actual != 66u) return 7;
    mode = 3; state.active = 1u; state.error = WIFI_STATION_ERROR_NONE;
    authorization_flags = 0x10u;
    if (station_authorized() || state.error != WIFI_STATION_ERROR_NONE ||
        request_size != 512u) return 8;
    authorization_flags = 0x30u;
    if (!station_authorized() || request_size != 512u) return 9;
    return 0;
}
'''
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(dir=wifi_assets.BUILD_ROOT) as directory:
            scratch = Path(directory)
            (scratch / "wifi_assets_generated.h").write_text(
                '#include "types.h"\n#define WIFI_FIRMWARE_SIZE 16u\n'
                '#define WIFI_NVRAM_SIZE 4u\n#define WIFI_CLM_SIZE 4u\n'
                '#define WIFI_FIRMWARE_CRC32 0u\n#define WIFI_NVRAM_CRC32 0u\n'
                '#define WIFI_CLM_CRC32 0u\n'
                'extern const uint8_t wifi_firmware_data[16];\n'
                'extern const uint8_t wifi_nvram_data[4];\n'
                'extern const uint8_t wifi_clm_data[4];\n', encoding="ascii")
            source = scratch / "station_test.c"
            executable = scratch / "station_test.exe"
            source.write_text(harness, encoding="ascii")
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(root / "pico2w" / "src" / "include"),
                 "-I", str(root / "pico2w" / "src" / "wifi"), "-I", str(scratch),
                 str(source), str(root / "pico2w" / "src" / "wifi" / "wifi_control.c"),
                 "-o", str(executable)], check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], check=True, capture_output=True)

    def test_package_synthetic_headers_and_private_credentials(self):
        fw_text, nvram_text, pins = fixture_headers()
        with tempfile.TemporaryDirectory(dir=wifi_assets.BUILD_ROOT) as directory:
            root = Path(directory)
            firmware = root / "fw.h"
            nvram = root / "nvram.h"
            creds = root / "credentials.txt"
            firmware.write_text(fw_text, encoding="ascii")
            nvram.write_text(nvram_text, encoding="ascii")
            creds.write_text("example-network\nsecure-passphrase\n", encoding="ascii")
            pins = wifi_assets.AssetPins(
                hashlib.sha256(firmware.read_bytes()).hexdigest(),
                hashlib.sha256(nvram.read_bytes()).hexdigest(),
                pins.firmware_original_size, pins.firmware_size, pins.clm_offset,
                pins.clm_size, pins.nvram_original_size, pins.nvram_size,
                pins.firmware_crc32, pins.nvram_crc32, pins.clm_crc32,
            )
            metadata = wifi_assets.package(firmware, nvram, creds, root, pins)
            relocated = root / "renamed-output"
            relocated_metadata = wifi_assets.package(firmware, nvram, creds,
                                                     relocated, pins)
            for name in ("wifi_assets_generated.h", "wifi_assets_generated.c",
                         "wifi_credentials_generated.h"):
                self.assertEqual((root / name).read_bytes(),
                                 (relocated / name).read_bytes())
            for key in ("source_sha256", "sizes", "crc32", "credentials_embedded"):
                self.assertEqual(metadata[key], relocated_metadata[key])
            generated = (root / "wifi_assets_generated.h").read_text(encoding="ascii")
            assets_source = (root / "wifi_assets_generated.c").read_text(encoding="ascii")
            credentials = (root / "wifi_credentials_generated.h").read_text(encoding="ascii")
            self.assertIn("#define WIFI_FIRMWARE_SIZE 32u", generated)
            self.assertIn("#define WIFI_SSID_SIZE 15u", generated)
            self.assertIn("#define WIFI_PASSPHRASE_SIZE 17u", generated)
            self.assertIn("extern const uint8_t wifi_firmware_data", generated)
            self.assertIn("const uint8_t wifi_firmware_data[33u]", assets_source)
            self.assertIn("secret-bearing", credentials)
            self.assertNotIn("example-network", credentials)
            self.assertNotIn("secure-passphrase", credentials)
            self.assertNotIn("example-network", generated + assets_source)
            self.assertNotIn("secure-passphrase", generated + assets_source)
            self.assertTrue(metadata["credentials_embedded"])
            self.assertNotIn("example-network", repr(metadata))
            self.assertNotIn("secure-passphrase", repr(metadata))
            self.assertNotIn("credential_sha", repr(metadata))
            compiler = shutil.which("gcc")
            if compiler is not None:
                repo = Path(__file__).resolve().parents[2]
                subprocess.run(
                    [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                     "-ffreestanding", "-fsyntax-only",
                     "-I", str(repo / "pico2w" / "src" / "include"),
                     "-I", str(repo / "pico2w" / "src" / "wifi"),
                     "-I", str(root),
                     str(repo / "pico2w" / "src" / "wifi" / "wifi_station.c"),
                     str(root / "wifi_assets_generated.c")],
                    check=True, capture_output=True, text=True,
                )

    def test_credential_limits_without_echoing_values(self):
        for passphrase in ("short", "x" * 64, "g" * 64):
            with self.subTest(length=len(passphrase)):
                with tempfile.TemporaryDirectory(dir=wifi_assets.BUILD_ROOT) as directory:
                    path = Path(directory) / "credentials.txt"
                    path.write_text("ssid\n" + passphrase + "\n", encoding="ascii")
                    with self.assertRaises(wifi_assets.AssetError) as raised:
                        wifi_assets.read_credentials(path)
                    self.assertNotIn(passphrase, str(raised.exception))

    def test_labelled_credentials_and_raw_backwards_compatibility(self):
        examples = (
            ("SSID: synthetic\nPASS: password\n", b"synthetic", b"password"),
            ("ssid=synthetic\npass=password\n", b"synthetic", b"password"),
            ("synthetic\npassword\n", b"synthetic", b"password"),
            ("SSID:literal\npassword\n", b"SSID:literal", b"password"),
            (" synthetic \n password \n", b" synthetic ", b" password "),
        )
        with tempfile.TemporaryDirectory(dir=wifi_assets.BUILD_ROOT) as directory:
            path = Path(directory) / "credentials.txt"
            for text, ssid, password in examples:
                path.write_text(text, encoding="ascii")
                self.assertEqual(wifi_assets.read_credentials(path), (ssid, password))
            for text in ("SSID:\nPASS: password\n", "SSID: synthetic\nPASS:\n"):
                path.write_text(text, encoding="ascii")
                with self.assertRaises(wifi_assets.AssetError):
                    wifi_assets.read_credentials(path)

    def test_source_hash_pin_fails_closed(self):
        fw_text, nvram_text, _ = fixture_headers()
        with self.assertRaisesRegex(wifi_assets.AssetError, "SHA-256"):
            wifi_assets.extract_assets(fw_text, nvram_text, source_hashes=("0" * 64, "1" * 64))

    def test_generated_include_is_simple_freestanding_c(self):
        generated = wifi_assets.render_header(b"\x01\x02", b"\x03", b"\x04",
                                              b"ssid", b"password")
        text = generated.decode("ascii")
        self.assertIn('#include "types.h"', text)
        self.assertIn("extern const uint8_t wifi_firmware_data", text)
        self.assertNotIn("#include <", text)
        credentials = wifi_assets.render_credentials_header(b'a"b', b"password")
        self.assertIn(r'\141\042\142', credentials.decode("ascii"))

    def test_control_and_ethernet_frame_framing(self):
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("gcc is unavailable")
        harness = r'''
#include "wifi_control.h"
int main(void) {
    uint8_t packet[WIFI_CONTROL_MAX];
    uint8_t payload[2] = {0xaau, 0xbbu};
    uint8_t frame[14];
    uint8_t interface = 9u;
    const uint8_t *decoded = 0;
    uint32_t decoded_size = 0u;
    wifi_control_packet_t parsed;
    uint32_t size = wifi_control_request(packet, 7u, 5u, 262u, 0u, payload, 2u);
    if (size != 30u || packet[0] != 30u || packet[2] != 0xe1u ||
        packet[3] != 0xffu || packet[28] != 0xaau || packet[29] != 0xbbu) return 1;
    for (uint32_t i = 0u; i < sizeof(frame); ++i) frame[i] = (uint8_t)i;
    size = wifi_data_frame(packet, 9u, 0u, frame, sizeof(frame));
    if (size != 32u || packet[4] != 9u || packet[5] != 2u ||
        packet[7] != 14u || packet[14] != 0x20u || packet[16] != 0u) return 2;
    parsed.channel = 2u;
    parsed.payload = packet + 14u;
    parsed.payload_size = 18u;
    if (!wifi_data_decode(&parsed, &interface, &decoded, &decoded_size) ||
        interface != 0u || decoded_size != sizeof(frame)) return 3;
    for (uint32_t i = 0u; i < sizeof(frame); ++i) {
        if (decoded[i] != frame[i]) return 4;
    }
    packet[0] = 30u; packet[1] = 0u; packet[2] = 0xe1u; packet[3] = 0xffu;
    packet[4] = 1u; packet[5] = 0u; packet[7] = 12u;
    packet[12] = 6u; packet[13] = 1u; packet[14] = 0u; packet[15] = 0u;
    packet[16] = 2u; packet[20] = 0u; packet[21] = 0u;
    packet[22] = 5u; packet[23] = 0u;
    packet[24] = 0u; packet[25] = 0u; packet[26] = 0u; packet[27] = 0u;
    packet[28] = 0xaau; packet[29] = 0xbbu;
    if (!wifi_control_parse(packet, 30u, &parsed) || parsed.id != 5u ||
        parsed.command != 262u || parsed.payload_size != 2u ||
        parsed.payload[0] != 0xaau || parsed.payload[1] != 0xbbu) return 5;
    char version[192];
    const uint8_t valid[] = "CYW43439 firmware\r\n";
    if (!wifi_version_string(valid, sizeof(valid), version) ||
        version[sizeof(valid) - 3u] != '\0') return 6;
    const uint8_t malformed[] = "version\nunexpected";
    if (wifi_version_string(malformed, sizeof(malformed), version) ||
        wifi_version_string(valid, sizeof(valid) - 1u, version) ||
        wifi_version_string((const uint8_t *)"", 1u, version)) return 7;
    uint8_t event[76] = {0};
    event[0] = 0x20u;
    event[16] = 0x88u; event[17] = 0x6cu;
    event[24] = 0x10u; event[25] = 0x18u;
    event[35] = 46u; event[39] = 6u; event[43] = 15u;
    parsed.channel = 1u; parsed.payload = event; parsed.payload_size = sizeof(event);
    uint32_t type, status, reason;
    if (!wifi_control_event(&parsed, &type, &status, &reason) ||
        type != 46u || status != 6u || reason != 15u) return 8;
    parsed.payload_size--;
    if (wifi_control_event(&parsed, &type, &status, &reason)) return 9;
    parsed.payload_size++;
    event[3] = 255u;
    if (wifi_control_event(&parsed, &type, &status, &reason)) return 10;
    return 0;
}
'''
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(dir=wifi_assets.BUILD_ROOT) as directory:
            scratch = Path(directory)
            source = scratch / "wifi_framing_test.c"
            executable = scratch / "wifi_framing_test.exe"
            source.write_text(harness, encoding="ascii")
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(root / "pico2w" / "src" / "include"),
                 "-I", str(root / "pico2w" / "src" / "wifi"),
                 str(root / "pico2w" / "src" / "wifi" / "wifi_control.c"),
                 str(source), "-o", str(executable)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(executable)], check=True, capture_output=True)

    def test_station_configuration_is_germany_wpa2_psk(self):
        source = (Path(__file__).resolve().parents[1] / "src" / "wifi" /
                  "wifi_station.c").read_text(encoding="ascii")
        self.assertIn("country[0] = 'D'; country[1] = 'E';", source)
        self.assertIn("scalar(CMD_WSEC, 4u)", source)
        self.assertIn("scalar(CMD_WPA_AUTH, 0x80u)", source)
        self.assertIn("state.pmk[2] = 1u", source)


if __name__ == "__main__":
    unittest.main()
