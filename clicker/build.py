"""Build the freestanding clicker diagnostic; host tooling uses Python's standard library."""

import argparse
import binascii
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
HERE = Path(__file__).resolve().parent
BUILD = ROOT / "build" / "clicker"


def tool(name):
    found = shutil.which(name)
    if found is None:
        raise FileNotFoundError(f"required tool not found: {name}")
    return str(Path(found).resolve())


def run(command):
    print(subprocess.list2cmdline([str(part) for part in command]), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def png_rgb(path):
    """Decode only the noninterlaced 8-bit RGB PNG format used by this asset."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("splash is not a PNG")
    offset, payload, header, ended = 8, bytearray(), False, False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        tag = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + size]
        if offset + size + 12 > len(data):
            raise ValueError("truncated PNG data")
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if binascii.crc32(tag + body) != crc:
            raise ValueError("PNG checksum mismatch")
        if not header:
            if tag != b"IHDR" or size != 13:
                raise ValueError("PNG must start with IHDR")
            if struct.unpack(">IIBBBBB", body) != (320, 240, 8, 2, 0, 0, 0):
                raise ValueError("splash must be 320x240, RGB8, noninterlaced")
            header = True
        elif tag == b"IDAT":
            payload.extend(body)
        elif tag == b"IEND":
            if size or offset + 12 != len(data):
                raise ValueError("invalid PNG end")
            ended = True
            break
        elif tag[:1].isupper() and tag != b"PLTE":
            raise ValueError(f"unsupported critical PNG chunk: {tag!r}")
        offset += size + 12
    if not ended or not payload:
        raise ValueError("incomplete PNG")
    expected = (320 * 3 + 1) * 240
    decoder = zlib.decompressobj()
    filtered = decoder.decompress(payload, expected + 1)
    if len(filtered) != expected or not decoder.eof or decoder.unused_data:
        raise ValueError("invalid PNG pixel stream length")
    rows, previous, offset = [], bytearray(960), 0
    for _ in range(240):
        mode = filtered[offset]
        row = bytearray(filtered[offset + 1:offset + 961])
        if mode > 4:
            raise ValueError("invalid PNG filter")
        for x in range(960):
            left = row[x - 3] if x >= 3 else 0
            above = previous[x]
            corner = previous[x - 3] if x >= 3 else 0
            if mode == 1:
                prediction = left
            elif mode == 2:
                prediction = above
            elif mode == 3:
                prediction = (left + above) // 2
            elif mode == 4:
                p = left + above - corner
                a, b, c = abs(p - left), abs(p - above), abs(p - corner)
                prediction = left if a <= b and a <= c else above if b <= c else corner
            else:
                prediction = 0
            row[x] = (row[x] + prediction) & 255
        rows.append(row)
        previous = row
        offset += 961
    return b"".join(rows)


def splash(output):
    pixels = png_rgb(HERE / "clicker-load-320x240.png")
    packed = bytearray()
    for x in range(320):
        for y in range(240):
            offset = (y * 320 + x) * 3
            r, g, b = pixels[offset:offset + 3]
            packed.extend(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    (output / "splash.rgb565").write_bytes(packed)
    # GAS accepts forward slashes inside .incbin string literals on Windows.
    (output / "splash.S").write_text(
        '.section .rodata.clicker_splash,"a"\n.balign 2\n'
        '.global clicker_splash\nclicker_splash:\n'
        f'.incbin "{(output / "splash.rgb565").as_posix()}"\n', encoding="ascii")


def tests(cc):
    BUILD.mkdir(parents=True, exist_ok=True)
    run([sys.executable, HERE / "test_assets.py"])
    for board in (2025, 2026):
        target = BUILD / f"test-input-{board}.exe"
        run([tool(cc), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
             f"-DCLICKER_BOARD={board}", str(HERE / "input.c"),
             str(HERE / "test_input.c"), "-o", str(target)])
        run([target])
    for name, sources in [
        ("bluetooth", [HERE / "bluetooth.c", HERE / "test_bluetooth.c",
                       ROOT / "src" / "tools" / "diagnostics_logic.c"]),
        ("security", [HERE / name for name in ("crypto.c", "bond.c", "test_security.c")] +
                     [ROOT / "src" / "tools" / "diagnostics_logic.c"]),
        ("host", [HERE / name for name in ("host.c", "gatt.c", "crypto.c", "bond.c", "test_host.c")] +
                 [ROOT / "src" / "tools" / "diagnostics_logic.c"]),
        ("wifi-firmware", [ROOT / "src" / "host" / "tests" / "wifi_firmware_test.c",
                           ROOT / "src" / "radio" / "wifi_firmware.c",
                           ROOT / "src" / "radio" / "wifi_control.c",
                           ROOT / "src" / "shared" / "usb_report.c",
                           ROOT / "src" / "tools" / "diagnostics_logic.c"])
    ]:
        target = BUILD / f"test-{name}.exe"
        run([tool(cc), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
             *map(str, sources), "-o", str(target)])
        run([target])


def firmware(board, cc, app, host_cc):
    output = BUILD / str(board)
    if app != "input":
        output = output / app
    output.mkdir(parents=True, exist_ok=True)
    uf2 = output / (f"clicker-{app}.uf2" if app != "input" else "clicker-diagnostic.uf2")
    manifest = output / "manifest.txt"
    uf2.unlink(missing_ok=True)
    manifest.unlink(missing_ok=True)
    splash(output)
    compiler = tool(cc)
    common = ["--no-default-config", "--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb"]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Oz", "-ffreestanding",
             "-fno-builtin", "-fno-stack-protector", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-ffunction-sections", "-fdata-sections",
             "-nostdinc", "-I" + str(ROOT / "src" / "shared"),
             "-I" + str(ROOT / "src" / "platform" / "badger"),
             "-I" + str(ROOT / "src" / "radio"), "-I" + str(output),
             f"-DCLICKER_BOARD={board}"]
    sources = [
        ROOT / "src" / "arch" / "rp2350" / "start.c",
        ROOT / "src" / "arch" / "rp2350" / "picobin.S",
        ROOT / "src" / "platform" / "badger" / "display.c",
        ROOT / "src" / "platform" / "badger" / "graphics.c",
        HERE / "input.c", HERE / "main.c", output / "splash.S"
    ]
    provenance = None
    if app != "input":
        from vendor_assets import prepare
        provenance = prepare(output, tool(host_cc))
        flags.append("-DCLICKER_CONTROLLER")
        if app == "advertise":
            flags.append("-DCLICKER_ADVERTISE")
        if app == "hid":
            flags.append("-DCLICKER_HID")
            sources.extend(HERE / name for name in
                           ("host.c", "gatt.c", "crypto.c", "bond.c", "flash.c", "hid.c"))
        sources.extend([
            HERE / "bluetooth.c", HERE / "controller.c", output / "vendor.S",
            ROOT / "src" / "platform" / "badger" / "wifi_board.c",
            ROOT / "src" / "platform" / "badger" / "wifi_pio.c",
            ROOT / "src" / "radio" / "wifi_gspi.c",
            ROOT / "src" / "radio" / "wifi_probe.c",
            ROOT / "src" / "radio" / "wifi_firmware.c",
            ROOT / "src" / "radio" / "wifi_control.c",
            ROOT / "src" / "shared" / "usb_report.c",
            ROOT / "src" / "tools" / "diagnostics_logic.c",
            ROOT / "src" / "arch" / "rp2350" / "bootrom_reboot.c"
        ])
    objects = []
    for source in sources:
        obj = output / (source.stem + ".o")
        run([compiler, *common, *(flags if source.suffix == ".c" else []),
             "-c", str(source), "-o", str(obj)])
        objects.append(obj)
    elf, binary = output / "clicker.elf", output / "clicker.bin"
    linker = ROOT / "src" / "arch" / "rp2350" / "memmap_flash.ld"
    if app == "hid":
        linker = HERE / "memmap.ld"
    run([tool("ld.lld"), "-T", str(linker), "--gc-sections", "--fatal-warnings",
         "-Map=" + str(output / "clicker.map"), *map(str, objects), "-o", str(elf)])
    undefined = subprocess.check_output([tool("llvm-nm"), "--undefined-only", str(elf)], text=True)
    if undefined.strip():
        raise ValueError(f"unexpected runtime dependencies: {undefined}")
    run([tool("llvm-objcopy"), "-O", "binary", str(elf), str(binary)])
    image = binary.read_bytes()
    image_limit = 0x3fe000 if app == "hid" else 4 * 1024 * 1024
    if not 84 <= len(image) <= image_limit:
        raise ValueError("invalid firmware length")
    stack, reset = struct.unpack_from("<2I", image)
    if stack != 0x20082000 or not reset & 1 or not 0x10000000 <= (reset & ~1) < 0x10000000 + len(image):
        raise ValueError("invalid boot vectors")
    marker, kind, last, link, end = struct.unpack_from("<5I", image, 64)
    if (marker, kind, last, end) != (0xffffded3, 0x10210142, 0x1ff, 0xab123579):
        raise ValueError("invalid Arm Secure boot block")
    if link & 3 or 64 + link + 20 != len(image):
        raise ValueError("invalid final boot block location")
    if struct.unpack_from("<5I", image, 64 + link) != (
            0xffffded3, 0x1fe, 0x1ff, (-link) & 0xffffffff, 0xab123579):
        raise ValueError("invalid boot block loop")
    converter = BUILD / "bin2uf2.exe"
    run([tool(host_cc), "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
         str(ROOT / "src" / "host" / "tools" / "bin2uf2.c"), "-o", str(converter)])
    run([converter, binary, uf2, "0x10000000", "0xe48bff59"])
    blocks = uf2.read_bytes()
    count = (len(image) + 255) // 256
    if len(blocks) != count * 512:
        raise ValueError("invalid UF2 length")
    recovered = bytearray()
    for number in range(count):
        offset = number * 512
        if struct.unpack_from("<8I", blocks, offset) != (
                0x0a324655, 0x9e5d5157, 0x2000, 0x10000000 + number * 256,
                256, number, count, 0xe48bff59):
            raise ValueError("invalid UF2 block")
        if struct.unpack_from("<I", blocks, offset + 508)[0] != 0x0ab16f30:
            raise ValueError("invalid UF2 end marker")
        recovered.extend(blocks[offset + 32:offset + 288])
    if recovered[:len(image)] != image or any(byte != 255 for byte in recovered[len(image):]):
        raise ValueError("UF2 round trip mismatch")
    manifest.write_text(
        f"board={board}\napp={app}\n"
        f"status={'ble-hid-legacy-passkey-four-bonds' if app == 'hid' else app + '-diagnostic-no-hid'}\n"
        f"firmware_bytes={len(image)}\nuf2_sha256={hashlib.sha256(blocks).hexdigest()}\n",
        encoding="ascii")
    print(f"PASS: {uf2} ({len(image)} firmware bytes, no undefined symbols)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=("2025", "2026"))
    parser.add_argument("--app", choices=("input", "controller", "advertise", "hid"), default="input")
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--cc", default="cc", help="native host tools/test compiler")
    parser.add_argument("--target-cc", default="clang")
    args = parser.parse_args()
    if not args.test and not args.board:
        parser.error("select --test and/or --board")
    try:
        if args.test:
            tests(args.cc)
        if args.board:
            firmware(int(args.board), args.target_cc, args.app, args.cc)
    except (OSError, ValueError, zlib.error, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
