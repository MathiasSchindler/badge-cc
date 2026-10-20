import importlib.util
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
import zlib

SPEC = importlib.util.spec_from_file_location(
    "lora_probe", Path(__file__).resolve().parents[1] / "tools" / "lora_probe.py")
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)
BUILD = 0x12345678
SERIAL = "7C4918BAB9492C77"


class FakeUSB:
    def __init__(self, error=0, corrupt=False):
        self.submitted = None
        self.lora_submits = 0
        self.polls = 0
        self.error = error
        self.corrupt = corrupt

    def transfer(self, request_type, request, value, payload):
        if request == 6:
            assert (request_type, value, payload) == (0x80, 0x100, 18)
            device = bytearray(18)
            device[:2] = b"\x12\x01"
            device[7] = 64
            struct.pack_into("<HH", device, 8, 0xCAFE, 0x4027)
            return bytes(device)
        assert value == 0
        if request == 0x53:
            assert (request_type, payload) == (0xC0, 12)
            return struct.pack("<4sHHI", b"P2WL", 1, 64, BUILD)
        if request in (0x50, 0x54):
            assert request_type == 0x40 and len(payload) == 64
            assert struct.unpack_from("<I", payload, 12)[0] == (1 if request == 0x50 else 2)
            self.submitted = payload
            self.lora_submits += request == 0x54
            return b""
        assert request_type == 0xC0 and request in (0x51, 0x55) and payload == 64
        response = bytearray(self.submitted)
        response[:4] = b"P2WR"
        if request == 0x51:
            struct.pack_into("<7I", response, 32,
                             1, 0xB9492C77, 0x7C4918BA, 1, 0x01234567, 0x89ABCDEF, 0xC001CAFE)
        else:
            self.polls += 1
            status = 0 if self.polls == 1 else (2 if self.error else 1)
            struct.pack_into("<IiI", response, 32, status, self.error if status == 2 else 0, 7)
            if status:
                response[44:60] = bytes([0x24, 1, 0, 0, 0, 0, 0x14, 0x24,
                                         0x24, 0, 0, 0, 0, 0, 0x14, 0x24])
        struct.pack_into("<I", response, 60, zlib.crc32(response[:60]))
        if self.corrupt:
            response[63] ^= 1
        return bytes(response)


class CollectorTests(unittest.TestCase):
    def test_request_and_response_validation(self):
        nonce = bytes(range(16))
        request = collector.request_record(BUILD, 2, nonce)
        self.assertEqual(len(request), 64)
        self.assertEqual(request[:4], b"P2WQ")
        self.assertEqual(request[32:60], bytes(28))
        response = bytearray(request)
        response[:4] = b"P2WR"
        struct.pack_into("<I", response, 60, zlib.crc32(response[:60]))
        collector.check_record(response, BUILD, 2, nonce)
        for byte in range(64):
            bad = bytearray(response)
            bad[byte] ^= 1
            with self.assertRaises(ValueError):
                collector.check_record(bad, BUILD, 2, nonce)
        for length in (0, 63, 65):
            with self.assertRaises(ValueError):
                collector.check_record(bytes(length), BUILD, 2, nonce)
        with self.assertRaises(ValueError):
            collector.request_record(BUILD, 2, bytes(16))

    def test_success(self):
        usb = FakeUSB()
        result = collector.probe(usb, BUILD, SERIAL)
        self.assertEqual((result["status"], result["error"], result["sequence"]), (1, 0, 7))
        self.assertEqual(usb.lora_submits, 1)
        self.assertEqual([sample["packet_type"] for sample in result["samples"]], [1, 0])
        self.assertEqual(result["samples"][0]["sync_word"], "1424")

    def test_wrong_identity_never_touches_radio(self):
        for build, serial in ((0, SERIAL), (BUILD, "0000000000000000")):
            usb = FakeUSB()
            with self.assertRaises(ValueError):
                collector.probe(usb, build, serial)
            self.assertEqual(usb.lora_submits, 0)
        usb = FakeUSB(corrupt=True)
        with self.assertRaisesRegex(ValueError, "CRC"):
            collector.probe(usb, BUILD, SERIAL)
        self.assertEqual(usb.lora_submits, 0)

    def test_device_error_is_failure(self):
        with self.assertRaisesRegex(RuntimeError, "BUSY remained high"):
            collector.probe(FakeUSB(error=-3003), BUILD, SERIAL)

    def test_setup_packet_layout(self):
        self.assertEqual(bytes(collector.SetupPacket(0x40, 0x54, 0, 0, 64)),
                         b"\x40\x54\x00\x00\x00\x00\x40\x00")

    def test_cli_safety_checks_precede_open(self):
        base = ["lora_probe.py", "--device-path", "not-a-real-device",
                "--serial", SERIAL, "--manifest", "unused.json"]
        for argv in (base, base + ["--confirm-waveshare", "--serial", "invalid"]):
            with patch.object(sys, "argv", argv), patch.object(collector, "WinUSB") as transport, \
                    patch.object(sys, "stderr"), self.assertRaises(SystemExit):
                collector.main()
            transport.assert_not_called()
        for manifest in ('[]', '{}', '{"build_id": 7}', '{"build_id": "not-hex!"}'):
            with patch.object(sys, "argv", base + ["--confirm-waveshare"]), \
                    patch.object(Path, "read_text", return_value=manifest), \
                    patch.object(collector, "WinUSB") as transport, \
                    patch.object(sys, "stderr"), self.assertRaises(SystemExit):
                collector.main()
            transport.assert_not_called()


if __name__ == "__main__":
    unittest.main()
