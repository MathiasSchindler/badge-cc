"""Explicit Windows WinUSB SX1262 probe; no driver installation, flashing or RF."""

import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import secrets
import struct
import time
import zlib

ERRORS = {
    -3001: "RP2350 clock/peripheral initialization failed",
    -3002: "probe timer stopped or polling budget exhausted",
    -3003: "SX1262 BUSY remained high for 100 ms",
    -3004: "SPI transfer timed out",
    -3005: "invalid status, packet-type echo or inconsistent registers",
    -3006: "SX1262 reported device errors (see raw samples)",
    -3007: "probe cancelled by USB reset, configuration change or recovery",
}


def request_record(build_id, test_id, nonce):
    if len(nonce) != 16 or not any(nonce):
        raise ValueError("nonce must contain 16 bytes and must not be all zero")
    record = bytearray(64)
    struct.pack_into("<4sHHII16s", record, 0, b"P2WQ", 1, 64, build_id, test_id, nonce)
    struct.pack_into("<I", record, 60, zlib.crc32(record[:60]))
    return bytes(record)


def check_record(record, build_id, test_id, nonce):
    if len(record) != 64:
        raise ValueError("incorrect report length")
    if struct.unpack_from("<4sHHII16s", record) != (b"P2WR", 1, 64, build_id, test_id, nonce):
        raise ValueError("report identity/nonce mismatch")
    if struct.unpack_from("<I", record, 60)[0] != zlib.crc32(record[:60]):
        raise ValueError("report CRC mismatch")


def fresh_nonce():
    nonce = secrets.token_bytes(16)
    while not any(nonce):
        nonce = secrets.token_bytes(16)
    return nonce


def probe(usb, build_id, serial):
    device = usb.transfer(0x80, 6, 0x100, 18)
    if (len(device) != 18 or device[:2] != b"\x12\x01" or device[7] != 64 or
            struct.unpack_from("<HH", device, 8) != (0xCAFE, 0x4027)):
        raise ValueError("selected device is not the Pico 2 W diagnostic interface")
    info = usb.transfer(0xC0, 0x53, 0, 12)
    if info != struct.pack("<4sHHI", b"P2WL", 1, 64, build_id):
        raise ValueError("running firmware does not match the selected manifest")

    nonce = fresh_nonce()
    usb.transfer(0x40, 0x50, 0, request_record(build_id, 1, nonce))
    report = usb.transfer(0xC0, 0x51, 0, 64)
    check_record(report, build_id, 1, nonce)
    status, low, high, _, known1, known2, commit = struct.unpack_from("<7I", report, 32)
    if (status, known1, known2, commit) != (1, 0x01234567, 0x89ABCDEF, 0xC001CAFE):
        raise ValueError("chip-ID diagnostic failed")
    if f"{high:08X}{low:08X}" != serial.upper():
        raise ValueError("chip ID does not match the explicitly selected serial; radio untouched")

    nonce = fresh_nonce()
    usb.transfer(0x40, 0x54, 0, request_record(build_id, 2, nonce))
    deadline = time.monotonic() + 3.0
    sequence = None
    while time.monotonic() < deadline:
        report = usb.transfer(0xC0, 0x55, 0, 64)
        check_record(report, build_id, 2, nonce)
        status, error, current_sequence = struct.unpack_from("<IiI", report, 32)
        if sequence is not None and sequence != current_sequence:
            raise ValueError("probe sequence changed while collecting a response")
        sequence = current_sequence
        if status not in (0, 1, 2) or (status in (0, 1) and error != 0) or (status == 2 and error == 0):
            raise ValueError("inconsistent probe result")
        if status != 0:
            samples = []
            for offset in (44, 52):
                raw = report[offset:offset + 8]
                samples.append({
                    "status": f"{raw[0]:02x}",
                    "packet_type": raw[1],
                    "device_errors": raw[2:4].hex(),
                    "irq_status": raw[4:6].hex(),
                    "sync_word": raw[6:8].hex(),
                })
            result = {"serial": serial.upper(), "build_id": f"{build_id:08x}",
                      "sequence": sequence, "status": status, "error": error, "samples": samples}
            if error:
                raise RuntimeError(json.dumps(result) + "\n" + ERRORS.get(error, f"device error {error}"))
            if [sample["packet_type"] for sample in samples] != [1, 0]:
                raise ValueError("packet-type challenge was not echoed")
            return result
        time.sleep(0.01)
    raise TimeoutError("probe did not finish before the collector deadline")


class SetupPacket(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [("type", ctypes.c_uint8), ("request", ctypes.c_uint8),
                ("value", ctypes.c_uint16), ("index", ctypes.c_uint16),
                ("length", ctypes.c_uint16)]


class WinUSB:
    def __init__(self, path):
        if os.name != "nt":
            raise OSError("this transport requires Windows and an existing WinUSB binding")
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api = ctypes.WinDLL("winusb", use_last_error=True)
        handle = ctypes.c_void_p
        self.kernel.CreateFileW.argtypes = [
            ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, handle,
            ctypes.c_uint32, ctypes.c_uint32, handle]
        self.kernel.CreateFileW.restype = handle
        self.kernel.CloseHandle.argtypes = [handle]
        self.kernel.CloseHandle.restype = ctypes.c_int
        self.api.WinUsb_Initialize.argtypes = [handle, ctypes.POINTER(handle)]
        self.api.WinUsb_Initialize.restype = ctypes.c_int
        self.api.WinUsb_Free.argtypes = [handle]
        self.api.WinUsb_Free.restype = ctypes.c_int
        self.api.WinUsb_ControlTransfer.argtypes = [
            handle, SetupPacket, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32), handle]
        self.api.WinUsb_ControlTransfer.restype = ctypes.c_int
        self.file = self.kernel.CreateFileW(path, 0xC0000000, 3, None, 3, 0x40000000, None)
        if self.file == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        self.interface = handle()
        if not self.api.WinUsb_Initialize(self.file, ctypes.byref(self.interface)):
            error = ctypes.get_last_error()
            if not self.kernel.CloseHandle(self.file):
                raise ctypes.WinError(ctypes.get_last_error())
            raise ctypes.WinError(error)

    def transfer(self, request_type, request, value, payload):
        incoming = bool(request_type & 0x80)
        length = payload if incoming else len(payload)
        buffer = (ctypes.c_ubyte * length)()
        if not incoming:
            buffer[:] = payload
        transferred = ctypes.c_uint32()
        setup = SetupPacket(request_type, request, value, 0, length)
        if not self.api.WinUsb_ControlTransfer(
                self.interface, setup, buffer, length, ctypes.byref(transferred), None):
            raise ctypes.WinError(ctypes.get_last_error())
        if transferred.value != length:
            raise OSError(f"short USB control transfer: {transferred.value}/{length}")
        return bytes(buffer) if incoming else b""

    def __enter__(self):
        return self

    def __exit__(self, *_):
        freed = self.api.WinUsb_Free(self.interface)
        free_error = ctypes.get_last_error() if not freed else 0
        closed = self.kernel.CloseHandle(self.file)
        if not closed:
            raise ctypes.WinError(ctypes.get_last_error())
        if not freed:
            raise ctypes.WinError(free_error)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-path", required=True, help="explicit WinUSB device interface path")
    parser.add_argument("--serial", required=True, help="expected 16-digit Pico chip ID")
    parser.add_argument("--manifest", required=True, type=Path, help="manifest of the installed build")
    parser.add_argument("--confirm-waveshare", action="store_true",
                        help="confirm Pico-LoRa-SX1262-868M wiring; allow resetting the radio")
    args = parser.parse_args()
    if not args.confirm_waveshare:
        parser.error("--confirm-waveshare is required; this command changes GPIOs and resets the radio")
    if not re.fullmatch(r"[0-9a-fA-F]{16}", args.serial):
        parser.error("--serial must be exactly 16 hexadecimal digits")
    try:
        manifest = json.loads(args.manifest.read_text(encoding="ascii"))
        if (not isinstance(manifest, dict) or not isinstance(manifest.get("build_id"), str) or
                not re.fullmatch(r"[0-9a-fA-F]{8}", manifest["build_id"])):
            raise ValueError("manifest must contain an eight-digit hexadecimal build_id")
        build_id = int(manifest["build_id"], 16)
        with WinUSB(args.device_path) as usb:
            result = probe(usb, build_id, args.serial)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
