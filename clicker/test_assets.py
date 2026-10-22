import binascii
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from build import BUILD, png_rgb


def chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", binascii.crc32(tag + body))


def png(filtered, width=320):
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, 240, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(filtered)) + chunk(b"IEND", b""))


class AssetTests(unittest.TestCase):
    def test_all_filters(self):
        rows = [bytes((x * 3 + y * 7) & 255 for x in range(960)) for y in range(240)]
        filtered, previous = bytearray(), bytes(960)
        for y, row in enumerate(rows):
            mode = y % 5
            filtered.append(mode)
            for x, value in enumerate(row):
                a = row[x - 3] if x >= 3 else 0
                b = previous[x]
                c = previous[x - 3] if x >= 3 else 0
                p = a + b - c
                distances = (abs(p - a), abs(p - b), abs(p - c))
                paeth = (a, b, c)[distances.index(min(distances))]
                prediction = (0, a, b, (a + b) // 2, paeth)[mode]
                filtered.append((value - prediction) & 255)
            previous = row
        with tempfile.TemporaryDirectory(prefix="asset-test-", dir=BUILD) as directory:
            path = Path(directory) / "test.png"
            path.write_bytes(png(filtered))
            self.assertEqual(png_rgb(path), b"".join(rows))
            cases = [
                b"not a PNG", png(filtered, width=319), png(filtered)[:-1],
                png(filtered + b"\0"), png(bytes([5]) + bytes(filtered[1:])),
                png(filtered) + b"trailing",
            ]
            corrupt = bytearray(png(filtered))
            corrupt[-1] ^= 1
            cases.append(bytes(corrupt))
            for invalid in cases:
                with self.subTest(length=len(invalid)):
                    path.write_bytes(invalid)
                    with self.assertRaises(ValueError):
                        png_rgb(path)

    def test_real_asset(self):
        pixels = png_rgb(Path(__file__).with_name("clicker-load-320x240.png"))
        self.assertEqual(len(pixels), 320 * 240 * 3)


if __name__ == "__main__":
    BUILD.mkdir(parents=True, exist_ok=True)
    unittest.main()
