"""Explicit, pinned, data-only vendor exception; no vendor host driver is compiled."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
VENDOR = ROOT / "vendor" / "clicker-cyw43"
MANIFEST = HERE / "vendor-assets.json"


def inputs(fetch=False):
    manifest = json.loads(MANIFEST.read_text(encoding="ascii"))
    identities = {}
    for entry in manifest["files"]:
        path = VENDOR / entry["path"]
        if not path.is_file():
            if not fetch:
                raise FileNotFoundError(f"missing vendor asset {path}; run python clicker\\vendor_assets.py --fetch")
            revision = entry.get("revision", manifest["revision"])
            url = f'https://raw.githubusercontent.com/{manifest["repository"]}/{revision}/{entry["path"]}'
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read(2 * 1024 * 1024 + 1)
            if len(data) > 2 * 1024 * 1024:
                raise ValueError(f"oversized vendor input: {entry['path']}")
        else:
            data = path.read_bytes()
        blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
        if blob != entry["git_blob"]:
            raise ValueError(f"pinned vendor Git blob mismatch: {entry['path']}")
        if not path.is_file():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        identities[entry["path"]] = hashlib.sha256(data).hexdigest()
    return identities


def prepare(output, compiler):
    identities = inputs()
    packer = output / "pack-vendor.exe"
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                    "-I" + str(VENDOR / "firmware"), str(HERE / "pack_vendor.c"),
                    "-o", str(packer)], check=True)
    subprocess.run([str(packer)], cwd=output, check=True)
    names = {"wifi_bt": "wifi-bt.bin", "nvram": "nvram.bin",
             "clm": "clm.bin", "bt_patch": "bluetooth.bin"}
    assembly, definitions, provenance = [], [], {"source_sha256": identities, "outputs": {}}
    for symbol, filename in names.items():
        path = output / filename
        data = path.read_bytes()
        assembly.append(f'.section .rodata.{symbol},"a"\n.balign 4\n.global {symbol}\n'
                        f'{symbol}:\n.incbin "{path.as_posix()}"\n')
        definitions.append(f"extern const uint8_t {symbol}[{len(data)}];\n"
                           f"#define {symbol.upper()}_SIZE {len(data)}u\n"
                           f"#define {symbol.upper()}_CRC 0x{zlib.crc32(data):08x}u\n")
        provenance["outputs"][filename] = {
            "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "crc32": f"{zlib.crc32(data):08x}"
        }
    (output / "vendor.S").write_text("".join(assembly), encoding="ascii")
    (output / "vendor_generated.h").write_text(
        '#include <stdint.h>\n' + "".join(definitions), encoding="ascii")
    (output / "vendor-provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="ascii")
    return provenance


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    try:
        result = inputs(args.fetch)
    except (OSError, ValueError) as error:
        parser.exit(1, f"ERROR: {error}\n")
    for name, digest in result.items():
        print(f"{digest}  {name}")
