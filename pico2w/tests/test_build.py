import importlib.util
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("pico_build", ROOT / "tools" / "build.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class FirmwareTests(unittest.TestCase):
    def test_tool_resolution(self):
        self.assertEqual(build.resolve_tool(sys.executable), str(Path(sys.executable).resolve()))
        with patch.object(build.shutil, "which", return_value=None):
            with self.assertRaisesRegex(FileNotFoundError, "required host tool not found"):
                build.resolve_tool("missing-tool")
        with patch.object(build.shutil, "which", return_value=str(ROOT / "ld.lld.exe")):
            self.assertEqual(build.resolve_tool("ld.lld"), str(ROOT / "ld.lld.exe"))

    def test_owned_selection_never_resolves_llvm_tools(self):
        directory = ROOT / "test-owned-tools"
        argv = ["build.py", "build", "--toolchain", "owned", "--owned-tools", str(directory)]
        with patch.object(sys, "argv", argv), \
                patch.object(build, "resolve_tool", side_effect=lambda name: name) as resolver, \
                patch.object(build, "firmware") as firmware:
            build.main()
        selected = [Path(call.args[0]).stem for call in resolver.call_args_list]
        self.assertEqual(set(selected), {
            "badge-cpp", "badge-ir", "badge-opt", "badge-codegen",
            "badge-asm", "badge-link", "badge-elf-info", "elf2bin",
        })
        self.assertEqual(firmware.call_args.args[0].toolchain, "owned")

    def test_uf2_boundaries_and_payload(self):
        for length in (1, 255, 256, 257, 512, 513, build.FLASH_SIZE):
            with self.subTest(length=length):
                image = (bytes(range(256)) * ((length + 255) // 256))[:length]
                uf2 = build.pack_uf2(image)
                count = (length + 255) // 256
                self.assertEqual(len(uf2), count * 512)
                reconstructed = bytearray()
                for number in range(count):
                    block = uf2[number * 512:(number + 1) * 512]
                    self.assertEqual(struct.unpack_from("<8I", block), (
                        0x0A324655, 0x9E5D5157, 0x2000,
                        build.FLASH_BASE + 256 * number, 256, number, count, build.FAMILY))
                    self.assertEqual(block[288:508], bytes(220))
                    self.assertEqual(struct.unpack_from("<I", block, 508)[0], 0x0AB16F30)
                    reconstructed.extend(block[32:288])
                self.assertEqual(reconstructed[:length], image)
                self.assertEqual(reconstructed[length:], b"\xff" * (count * 256 - length))

    def test_reject_bad_sizes(self):
        for image in (b"", bytes(build.FLASH_SIZE + 1)):
            with self.assertRaises(ValueError):
                build.pack_uf2(image)

    @staticmethod
    def valid_image():
        image = bytearray(104)
        struct.pack_into("<2I", image, 0, 0x20082000, 0x10000055)
        struct.pack_into("<5I", image, 64,
                         0xFFFFDED3, 0x10210142, 0x1FF, 20, 0xAB123579)
        struct.pack_into("<5I", image, 84,
                         0xFFFFDED3, 0x1FE, 0x1FF, (-20) & 0xFFFFFFFF, 0xAB123579)
        return image

    def test_validate_boot_blocks(self):
        image = self.valid_image()
        build.validate_image(image)
        for offset in (0, 4, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100):
            with self.subTest(offset=offset):
                corrupt = bytearray(image)
                corrupt[offset] ^= 1
                with self.assertRaises(ValueError):
                    build.validate_image(corrupt)

    def test_reject_bad_reset_range_and_truncation(self):
        for address in (0x10000000, 0x10000069, 0x20000001):
            image = self.valid_image()
            struct.pack_into("<I", image, 4, address)
            with self.assertRaises(ValueError):
                build.validate_image(image)
        image = self.valid_image()
        for length in (0, 8, 64, 83, 84, 103):
            with self.assertRaises(ValueError):
                build.validate_image(image[:length])

    def test_sources_stay_inside_tree(self):
        for name in build.SOURCES:
            path = (ROOT / name).resolve()
            self.assertTrue(path.is_relative_to(ROOT))
            self.assertTrue(path.is_file())
        self.assertFalse((ROOT / "src").joinpath("build_id.h").exists())


if __name__ == "__main__":
    unittest.main()
