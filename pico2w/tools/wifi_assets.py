#!/usr/bin/env python3
"""Host-only packager for pinned CYW43439 data and private station credentials."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import re
import sys
from pathlib import Path
import zlib


ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = (ROOT / "build").resolve()
FIRMWARE_NAME = "w43439A0_7_95_49_00_combined"
NVRAM_NAME = "wifi_nvram_4343"


@dataclass(frozen=True)
class AssetPins:
    firmware_header_sha256: str
    nvram_header_sha256: str
    firmware_original_size: int
    firmware_size: int
    clm_offset: int
    clm_size: int
    nvram_original_size: int
    nvram_size: int
    firmware_crc32: int
    nvram_crc32: int
    clm_crc32: int


PINS = AssetPins(
    "120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea",
    "ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad",
    224190, 224192, 224256, 984, 743, 744,
    0xFE8B5E03, 0x357A3338, 0x29AA78FD,
)


class AssetError(ValueError):
    pass


def _strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\r\n]*", "", text, flags=re.S)


def _array_bytes(text: str, name: str) -> bytes:
    source = _strip_comments(text)
    match = re.search(
        r"\b" + re.escape(name) +
        r"\s*\[\s*\]\s*(?:CYW43_RESOURCE_ATTRIBUTE\s*)?=\s*(.*?);",
        source,
        flags=re.S,
    )
    if match is None:
        raise AssetError("vendor header is missing a required data array")
    body = re.sub(r"^\s*#.*$", "", match.group(1), flags=re.M).strip()
    if body.startswith('"'):
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
        if re.sub(r'"(?:[^"\\]|\\.)*"|\s', "", body):
            raise AssetError("unsupported vendor C string initializer")
        result = bytearray()
        escapes = {"n": 10, "r": 13, "t": 9, "\\": 92, '"': 34, "'": 39}
        for string in strings:
            index = 0
            while index < len(string):
                char = string[index]
                index += 1
                if char != "\\":
                    result.append(ord(char))
                    continue
                if index >= len(string):
                    raise AssetError("invalid vendor C escape")
                char = string[index]
                index += 1
                if char in "01234567":
                    digits = char
                    while index < len(string) and len(digits) < 3 and string[index] in "01234567":
                        digits += string[index]
                        index += 1
                    value = int(digits, 8)
                elif char == "x":
                    start = index
                    while index < len(string) and string[index] in "0123456789abcdefABCDEF":
                        index += 1
                    if start == index:
                        raise AssetError("invalid vendor C hex escape")
                    value = int(string[start:index], 16)
                elif char in escapes:
                    value = escapes[char]
                else:
                    raise AssetError("unsupported vendor C escape")
                if value > 255:
                    raise AssetError("vendor string contains an out-of-range byte")
                result.append(value)
        return bytes(result) + b"\0"
    if not (body.startswith("{") and body.endswith("}")):
        raise AssetError("unsupported vendor data initializer")
    body = body[1:-1]
    tokens = re.findall(r"0[xX][0-9a-fA-F]+|\b[0-9]+\b", body)
    residue = re.sub(r"0[xX][0-9a-fA-F]+|\b[0-9]+\b|[\s,]", "", body)
    if residue or not tokens:
        raise AssetError("vendor data array contains unsupported initializer syntax")
    values = [int(token, 0) for token in tokens]
    if any(value > 255 for value in values):
        raise AssetError("vendor data array contains an out-of-range byte")
    return bytes(values)


def _macro(text: str, name: str) -> int:
    match = re.search(r"^\s*#\s*define\s+" + re.escape(name) +
                      r"\s+\(?\s*(0[xX][0-9a-fA-F]+|[0-9]+)\b",
                      _strip_comments(text), flags=re.M)
    if match is None:
        raise AssetError("vendor header is missing a required length macro")
    return int(match.group(1), 0)


def _crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def extract_assets(firmware_text: str, nvram_text: str, pins: AssetPins = PINS,
                   source_hashes: tuple[str, str] | None = None) -> tuple[bytes, bytes, bytes]:
    if source_hashes is not None and source_hashes != (
            pins.firmware_header_sha256, pins.nvram_header_sha256):
        raise AssetError("vendor source SHA-256 does not match the pinned revision")
    combined = _array_bytes(firmware_text, FIRMWARE_NAME)
    nvram = _array_bytes(nvram_text, NVRAM_NAME)
    if _macro(firmware_text, "CYW43_WIFI_FW_LEN") != pins.firmware_original_size:
        raise AssetError("vendor firmware length does not match the pinned revision")
    if _macro(firmware_text, "CYW43_CLM_LEN") != pins.clm_size:
        raise AssetError("vendor regulatory-data length does not match the pinned revision")
    if len(combined) != pins.clm_offset + pins.clm_size:
        raise AssetError("combined vendor firmware array length is invalid")
    if len(nvram) != pins.nvram_original_size:
        raise AssetError("vendor board-configuration array length is invalid")
    if combined[:8] != b"\0\0\0\0\x65\x14\0\0":
        raise AssetError("vendor firmware vector sentinel mismatch")
    if combined[pins.firmware_original_size - 16:pins.firmware_original_size] != b"DVID 01-b0b4520f":
        raise AssetError("vendor firmware tail sentinel mismatch")
    clm = combined[pins.clm_offset:]
    nvram_head = b"NVRAMRev=$Rev$\0"
    nvram_tail = b"btc_mode=1\0\0\0\0"
    if (clm[:8] != b"BLOB<\0\0\0" or clm[60:68] != b"CLM DATA" or
            nvram[:len(nvram_head)] != nvram_head or
            nvram[-len(nvram_tail):] != nvram_tail):
        raise AssetError("vendor CLM or NVRAM sentinel mismatch")
    if any(combined[pins.firmware_original_size:pins.clm_offset]):
        raise AssetError("vendor firmware-to-CLM padding is not zero")
    if (_crc(combined[:pins.firmware_original_size]) != pins.firmware_crc32 or
            _crc(nvram) != pins.nvram_crc32 or _crc(clm) != pins.clm_crc32):
        raise AssetError("vendor payload CRC-32 does not match the pinned revision")
    firmware = combined[:pins.firmware_original_size]
    firmware += bytes(pins.firmware_size - len(firmware))
    nvram += bytes(pins.nvram_size - len(nvram))
    return firmware, nvram, clm


def read_credentials(path: Path) -> tuple[bytes, bytes]:
    try:
        raw = path.read_bytes()
        text = raw.decode("utf-8")
    except (OSError, UnicodeError):
        raise AssetError("credential file cannot be read as UTF-8") from None
    lines = text.splitlines()
    if len(lines) != 2 or any(line == "" for line in lines):
        raise AssetError("credential file must contain exactly two nonempty lines")
    ssid, passphrase = lines
    labelled_ssid = re.fullmatch(r"SSID\s*[:=]\s*(.*)", ssid, re.IGNORECASE)
    labelled_pass = re.fullmatch(r"PASS\s*[:=]\s*(.*)", passphrase, re.IGNORECASE)
    if labelled_ssid and labelled_pass:
        ssid, passphrase = labelled_ssid[1], labelled_pass[1]
    ssid_bytes = ssid.encode("utf-8")
    passphrase_bytes = passphrase.encode("utf-8")
    if not 1 <= len(ssid_bytes) <= 32 or b"\0" in ssid_bytes:
        raise AssetError("SSID must be 1 to 32 UTF-8 bytes")
    if (not 8 <= len(passphrase_bytes) <= 63 and
            not (len(passphrase_bytes) == 64 and
                 re.fullmatch(rb"[0-9a-fA-F]{64}", passphrase_bytes))):
        raise AssetError("passphrase must be 8 to 63 bytes or 64 hexadecimal digits")
    if b"\0" in passphrase_bytes or any(byte < 0x20 or byte == 0x7F
                                         for byte in ssid_bytes + passphrase_bytes):
        raise AssetError("credentials contain unsupported control characters")
    return ssid_bytes, passphrase_bytes


def _c_bytes(name: str, value: bytes) -> str:
    # Adjacent literals keep the owned preprocessor below its token-count limit.
    lines = ["    " + _c_string(value[start:start + 64])
             for start in range(0, len(value), 64)]
    body = "\n".join(lines)
    return f"const uint8_t {name}[{len(value) + 1}u] =\n{body};\n"


def render_header(firmware: bytes, nvram: bytes, clm: bytes, ssid: bytes,
                  passphrase: bytes, source_hashes: tuple[str, str] | None = None,
                  pins: AssetPins = PINS) -> bytes:
    firmware_crc = _crc(firmware)
    nvram_crc = _crc(nvram)
    clm_crc = _crc(clm)
    sections = [
        "/* Generated metadata for pinned CYW43439 vendor assets. */\n",
        "#ifndef PICO2W_WIFI_ASSETS_GENERATED_H\n#define PICO2W_WIFI_ASSETS_GENERATED_H\n",
        '#include "types.h"\n',
        f'#define WIFI_FIRMWARE_SOURCE_SHA256 "{source_hashes[0]}"\n'
        if source_hashes is not None else "",
        f'#define WIFI_NVRAM_SOURCE_SHA256 "{source_hashes[1]}"\n'
        if source_hashes is not None else "",
        f"#define WIFI_FIRMWARE_ORIGINAL_SIZE {pins.firmware_original_size}u\n"
        if source_hashes is not None else "",
        f"#define WIFI_CLM_SOURCE_OFFSET {pins.clm_offset}u\n"
        if source_hashes is not None else "",
        f"#define WIFI_FIRMWARE_SIZE {len(firmware)}u\n",
        f"#define WIFI_FIRMWARE_CRC32 0x{firmware_crc:08x}u\n",
        f"#define WIFI_NVRAM_SIZE {len(nvram)}u\n",
        f"#define WIFI_NVRAM_CRC32 0x{nvram_crc:08x}u\n",
        f"#define WIFI_CLM_SIZE {len(clm)}u\n",
        f"#define WIFI_CLM_CRC32 0x{clm_crc:08x}u\n",
        f"#define WIFI_SSID_SIZE {len(ssid)}u\n",
        f"#define WIFI_PASSPHRASE_SIZE {len(passphrase)}u\n",
        "extern const uint8_t wifi_firmware_data[WIFI_FIRMWARE_SIZE + 1u];\n",
        "extern const uint8_t wifi_nvram_data[WIFI_NVRAM_SIZE + 1u];\n",
        "extern const uint8_t wifi_clm_data[WIFI_CLM_SIZE + 1u];\n",
        "#endif\n",
    ]
    return "".join(sections).encode("ascii")


def render_assets_source(firmware: bytes, nvram: bytes, clm: bytes) -> bytes:
    sections = [
        "/* Generated under build/ as wifi_assets_generated.c. Embeds proprietary "
        "CYW43439 firmware, NVRAM, and CLM data; include metadata header "
        "wifi_assets_generated.h. Do not redistribute without authorization. */\n",
        '#include "types.h"\n',
        '#include "wifi_assets_generated.h"\n',
        _c_bytes("wifi_firmware_data", firmware),
        _c_bytes("wifi_nvram_data", nvram),
        _c_bytes("wifi_clm_data", clm),
    ]
    return "".join(sections).encode("ascii")


def _c_string(value: bytes) -> str:
    return '"' + "".join(f"\\{byte:03o}" for byte in value) + '"'


def render_credentials_header(ssid: bytes, passphrase: bytes) -> bytes:
    return (
        "/* Generated under build/ as wifi_credentials_generated.h; secret-bearing. "
        "Do not commit or distribute. */\n"
        "#ifndef PICO2W_WIFI_CREDENTIALS_GENERATED_H\n"
        "#define PICO2W_WIFI_CREDENTIALS_GENERATED_H\n"
        f"static const char wifi_default_ssid[] = {_c_string(ssid)};\n"
        f"static const char wifi_default_passphrase[] = {_c_string(passphrase)};\n"
        "#endif\n"
    ).encode("ascii")


def prepare_assets(output: Path, firmware_header: Path, nvram_header: Path,
                   credentials: Path, pins: AssetPins = PINS) -> dict[str, object]:
    """Package inputs under this standalone project's build directory."""
    destination = output.resolve()
    if destination != BUILD_ROOT and BUILD_ROOT not in destination.parents:
        raise AssetError("output directory must be inside the project's build directory")
    try:
        firmware_raw = firmware_header.read_bytes()
        nvram_raw = nvram_header.read_bytes()
    except OSError:
        raise AssetError("a required vendor header cannot be read") from None
    hashes = (hashlib.sha256(firmware_raw).hexdigest(),
              hashlib.sha256(nvram_raw).hexdigest())
    try:
        firmware_text = firmware_raw.decode("ascii")
        nvram_text = nvram_raw.decode("ascii")
    except UnicodeError:
        raise AssetError("vendor headers must be ASCII C data declarations") from None
    firmware, nvram, clm = extract_assets(firmware_text, nvram_text, pins, hashes)
    ssid, passphrase = read_credentials(credentials)
    generated = {
        "wifi_assets_generated.h": render_header(
            firmware, nvram, clm, ssid, passphrase, hashes, pins),
        "wifi_assets_generated.c": render_assets_source(firmware, nvram, clm),
        "wifi_credentials_generated.h": render_credentials_header(ssid, passphrase),
    }
    destination.mkdir(parents=True, exist_ok=True)
    staged: list[tuple[Path, Path]] = []
    try:
        for name, content in generated.items():
            target = destination / name
            staging = destination / (name + ".stage")
            staging.write_bytes(content)
            staged.append((staging, target))
        for staging, target in staged:
            staging.replace(target)
    except OSError:
        for staging, _ in staged:
            try:
                staging.unlink()
            except OSError:
                pass
        raise AssetError("cannot write generated package into pico2w/build") from None
    return {
        "assets_header": str(destination / "wifi_assets_generated.h"),
        "assets_source": str(destination / "wifi_assets_generated.c"),
        "credentials_header": str(destination / "wifi_credentials_generated.h"),
        "source_sha256": {
            "firmware": hashes[0],
            "nvram": hashes[1],
        },
        "sizes": {
            "firmware": len(firmware),
            "nvram": len(nvram),
            "clm": len(clm),
        },
        "crc32": {
            "firmware": f"{_crc(firmware):08x}",
            "nvram": f"{_crc(nvram):08x}",
            "clm": f"{_crc(clm):08x}",
        },
        "credentials_embedded": True,
    }


def package(firmware_path: Path, nvram_path: Path, credentials_path: Path,
            output_dir: Path, pins: AssetPins = PINS) -> dict[str, object]:
    return prepare_assets(output_dir, firmware_path, nvram_path, credentials_path, pins)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware-header", "--wifi-firmware",
                        dest="firmware_header", required=True, type=Path)
    parser.add_argument("--nvram-header", "--wifi-nvram",
                        dest="nvram_header", required=True, type=Path)
    parser.add_argument("--credentials", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        prepare_assets(args.output_dir, args.firmware_header, args.nvram_header,
                       args.credentials)
    except AssetError as error:
        print(f"wifi_assets: {error}", file=sys.stderr)
        return 1
    print("wifi_assets: generated local Wi-Fi assets and credentials in build directory")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
