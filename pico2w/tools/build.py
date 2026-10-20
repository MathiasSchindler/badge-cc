"""LLVM or owned-toolchain firmware build and host tests; Python standard library only."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
FLASH_BASE = 0x10000000
FLASH_SIZE = 4 * 1024 * 1024
FAMILY = 0xE48BFF59
SOURCES = (
    "src/rp2350/start.c",
    "src/rp2350/bootrom.c",
    "src/usb/report.c",
    "src/usb/control.c",
    "src/usb/hardware.c",
    "src/main.c",
    "src/rp2350/picobin.S",
)


def run(command):
    print(subprocess.list2cmdline([str(arg) for arg in command]), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def resolve_tool(name):
    path = shutil.which(name)
    if path is None:
        raise FileNotFoundError(f"required host tool not found: {name}")
    # Windows CreateProcess does not append .exe to dotted names such as ld.lld.
    return str(Path(path).resolve())


def pack_uf2(image):
    if not image or len(image) > FLASH_SIZE:
        raise ValueError("firmware must fit in the Pico 2 W's 4 MiB flash")
    count = (len(image) + 255) // 256
    blocks = []
    for number in range(count):
        payload = image[number * 256:(number + 1) * 256].ljust(256, b"\xff")
        header = struct.pack("<8I", 0x0A324655, 0x9E5D5157, 0x2000,
                             FLASH_BASE + number * 256, 256, number, count, FAMILY)
        blocks.append(header + payload + bytes(220) + struct.pack("<I", 0x0AB16F30))
    return b"".join(blocks)


def validate_image(image):
    if len(image) < 84 or len(image) > FLASH_SIZE:
        raise ValueError("invalid firmware size")
    stack, reset = struct.unpack_from("<2I", image)
    if stack != 0x20082000 or not reset & 1:
        raise ValueError("invalid stack or Thumb reset vector")
    if not FLASH_BASE <= (reset & ~1) < FLASH_BASE + len(image):
        raise ValueError("reset vector outside firmware")
    # The linker places the boot block immediately after the 16-entry vector table.
    marker, image_type, last, link, end = struct.unpack_from("<5I", image, 64)
    if (marker, image_type, last, end) != (0xFFFFDED3, 0x10210142, 0x000001FF, 0xAB123579):
        raise ValueError("invalid RP2350 Arm Secure boot block")
    end_offset = 64 + link
    if end_offset + 20 != len(image) or end_offset & 3:
        raise ValueError("invalid final boot block location")
    final = struct.unpack_from("<5I", image, end_offset)
    if final != (0xFFFFDED3, 0x000001FE, 0x000001FF,
                 (-link) & 0xFFFFFFFF, 0xAB123579):
        raise ValueError("invalid boot block loop")


def firmware(args):
    output = BUILD / "owned" if args.toolchain == "owned" else BUILD
    output.mkdir(parents=True, exist_ok=True)
    for name in ("pico2w.uf2", "manifest.json"):
        (output / name).unlink(missing_ok=True)
    inputs = sorted(path for path in (ROOT / "src").rglob("*") if path.is_file())
    inputs.append(Path(__file__).resolve())
    digest = hashlib.sha256()
    for path in inputs:
        digest.update(path.relative_to(ROOT).as_posix().encode("ascii") + b"\0")
        digest.update(path.read_bytes())
    source_hash = digest.hexdigest()
    tool_hashes = {}
    if args.toolchain == "owned":
        for name, tool in args.owned.items():
            tool_hashes[name] = hashlib.sha256(Path(tool).read_bytes()).hexdigest()
    identity = hashlib.sha256(
        (source_hash + args.toolchain + json.dumps(tool_hashes, sort_keys=True)).encode("ascii")
    ).hexdigest()
    build_id = int(identity[:8], 16)
    (output / "build_id.h").write_text(
        f"#define PICO2W_BUILD_ID 0x{build_id:08x}u\n", encoding="ascii")
    common = ["--no-default-config", "--target=arm-none-eabi", "-mcpu=cortex-m33", "-mthumb"]
    cflags = [
        "-std=c11", "-Wall", "-Wextra", "-Werror", "-Oz",
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-ffunction-sections", "-fdata-sections", "-nostdinc",
        "-Isrc/include", "-Isrc", "-I" + str(output),
    ]
    objects = []
    for name in SOURCES:
        source = ROOT / name
        target = output / (source.stem + ".o")
        if args.toolchain == "owned":
            intermediates = output / source.stem
            intermediates.mkdir(exist_ok=True)
            preprocessed = intermediates / ("preprocessed.s" if source.suffix == ".S" else "preprocessed.i")
            run([args.owned["badge-cpp"], *(["--assembly"] if source.suffix == ".S" else []),
                 "-Isrc/include", "-Isrc", "-I" + str(output),
                 str(source), "-o", str(preprocessed)])
            if source.suffix == ".S":
                assembly = preprocessed
            else:
                frontend = intermediates / "frontend.ll"
                optimized = intermediates / "optimized.ll"
                assembly = intermediates / "assembly.s"
                run([args.owned["badge-ir"], str(preprocessed), "-o", str(frontend)])
                run([args.owned["badge-opt"], str(frontend), "-o", str(optimized)])
                run([args.owned["badge-codegen"], str(optimized), "-o", str(assembly)])
            run([args.owned["badge-asm"], str(assembly), "-o", str(target)])
        else:
            run([args.cc, *common, *(cflags if source.suffix == ".c" else []),
                 "-c", str(source), "-o", str(target)])
        objects.append(str(target))
    elf = output / "pico2w.elf"
    script = str(ROOT / "src" / "rp2350" / "memmap.ld")
    if args.toolchain == "owned":
        run([args.owned["badge-link"], "-T", script, "--gc-sections",
             "-Map", str(output / "pico2w.map"), *objects, "-o", str(elf)])
        info = [args.owned["badge-elf-info"], "-u", str(elf)]
    else:
        run([args.ld, "-T", script, "--gc-sections", "--fatal-warnings", "--no-undefined",
             "-Map=" + str(output / "pico2w.map"), *objects, "-o", str(elf)])
        info = [args.nm, "--undefined-only", str(elf)]
    undefined = subprocess.check_output(
        info, cwd=ROOT, text=True).strip()
    if undefined:
        raise RuntimeError("firmware has unresolved symbols:\n" + undefined)
    binary = output / "pico2w.bin"
    extractor = args.owned["elf2bin"] if args.toolchain == "owned" else args.objcopy
    run([extractor, "-O", "binary", str(elf), str(binary)])
    image = binary.read_bytes()
    validate_image(image)
    uf2 = pack_uf2(image)
    (output / "pico2w.uf2").write_bytes(uf2)
    manifest = {
        "build_id": f"{build_id:08x}",
        "source_sha256": source_hash,
        "binary_sha256": hashlib.sha256(image).hexdigest(),
        "uf2_sha256": hashlib.sha256(uf2).hexdigest(),
        "flash_bytes": len(image),
        "uf2_blocks": len(uf2) // 512,
        "family_id": f"{FAMILY:08x}",
        "undefined_symbols": [],
        "toolchain": args.toolchain,
        "owned_tools_sha256": tool_hashes,
        "compiler": "badge-compiler native tools" if args.toolchain == "owned" else
                    subprocess.check_output([args.cc, "--version"], text=True).splitlines()[0],
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")
    print(f"PASS: {len(image)} flash bytes; no unresolved symbols; verified boot layout")
    print(f"UF2: {output / 'pico2w.uf2'} (build {build_id:08x}); not flashed")


def tests(args):
    BUILD.mkdir(exist_ok=True)
    executable = BUILD / ("usb_test.exe" if os.name == "nt" else "usb_test")
    run([args.host_cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
         "-Isrc/include", "-Isrc", str(ROOT / "tests" / "usb_test.c"),
         str(ROOT / "src" / "usb" / "control.c"),
         str(ROOT / "src" / "usb" / "report.c"), "-o", str(executable)])
    run([str(executable)])
    run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("build", "test", "all"), nargs="?", default="build")
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--ld", default="ld.lld")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--toolchain", choices=("llvm", "owned"), default="llvm")
    parser.add_argument("--owned-tools", type=Path, help="directory containing native badge compiler tools")
    args = parser.parse_args()
    try:
        tools = []
        if args.action in ("test", "all"):
            tools.append("host_cc")
        if args.action in ("build", "all"):
            if args.toolchain == "owned":
                if args.owned_tools is None:
                    raise ValueError("--toolchain owned requires --owned-tools")
                directory = args.owned_tools.resolve()
                suffix = ".exe" if os.name == "nt" else ""
                args.owned = {
                    name: resolve_tool(str(directory / (name + suffix)))
                    for name in ("badge-cpp", "badge-ir", "badge-opt", "badge-codegen",
                                 "badge-asm", "badge-link", "badge-elf-info", "elf2bin")
                }
            else:
                tools.extend(("cc", "ld", "objcopy", "nm"))
        for tool in tools:
            setattr(args, tool, resolve_tool(getattr(args, tool)))
        if args.action in ("test", "all"):
            tests(args)
        if args.action in ("build", "all"):
            firmware(args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
