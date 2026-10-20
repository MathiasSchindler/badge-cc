"""Portable CLI regressions for native compiler builds, including Windows."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SUFFIX = ".exe" if os.name == "nt" else ""


class NativeToolsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="badge compiler native ")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def tool(self, name, *arguments, ok=True):
        result = subprocess.run(
            [str(BUILD / (name + SUFFIX)), *map(str, arguments)],
            cwd=self.directory, capture_output=True)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout.decode(errors="replace"))
        return result

    def source(self, name, contents):
        path = self.directory / name
        path.write_text(contents, encoding="ascii")
        return path

    def test_preprocessor_identity_nested_includes_and_replacement(self):
        include = self.directory / "headers"
        include.mkdir()
        (include / "first.h").write_text('#pragma once\n#include "second.h"\nint first;\n', encoding="ascii")
        second = include / "second.h"
        second.write_text("#pragma once\nint second;\n", encoding="ascii")
        alias = include / "alias.h"
        os.link(second, alias)
        source = self.source("input.c",
            '#include "headers/first.h"\n#include "headers/alias.h"\nint last;\n')
        output = self.source("output.i", "old output")
        for _ in range(2):
            self.tool("badge-cpp", source, "-o", output)
            tokens = output.read_text(encoding="ascii").split()
            self.assertEqual(tokens.count("first"), 1)
            self.assertEqual(tokens.count("second"), 1)
            self.assertEqual(tokens.count("last"), 1)
        original = source.read_bytes()
        for destination in (source, self.directory / "hardlink.c"):
            if destination != source:
                os.link(source, destination)
            self.tool("badge-cpp", source, "-o", destination, ok=False)
            self.assertEqual(source.read_bytes(), original)
        directory_output = self.directory / "output-directory"
        directory_output.mkdir()
        self.tool("badge-cpp", source, "-o", directory_output, ok=False)
        self.assertFalse(list(self.directory.glob("*.tmp.*")))

    def test_binary_output_replacement_and_manifest_roundtrip(self):
        assembly = self.source("input.s",
            '.syntax unified\n.thumb\n.section .text,"ax"\n'
            '.global entry\n.type entry,%function\n.thumb_func\nentry:\n'
            'movs r0,#13\nbx lr\n.byte 0,10,13,26,255\n')
        obj = self.source("output.o", "old output")
        for _ in range(2):
            self.tool("badge-asm", assembly, "-o", obj)
        self.assertTrue(obj.read_bytes().startswith(b"\x7fELF"))
        self.assertIn(bytes((0, 10, 13, 26, 255)), obj.read_bytes())
        manifest = self.directory / "object.txt"
        self.tool("badge-object", "--emit-manifest", obj, "-o", manifest)
        self.assertIn("000a0d1aff", manifest.read_text(encoding="ascii"))
        restored = self.directory / "restored.o"
        self.tool("badge-object", manifest, "-o", restored)
        restored_manifest = self.directory / "restored.txt"
        self.tool("badge-object", "--emit-manifest", restored, "-o", restored_manifest)
        self.assertEqual(restored_manifest.read_bytes(), manifest.read_bytes())
        original = obj.read_bytes()
        alias = self.directory / "alias.o"
        os.link(obj, alias)
        self.tool("badge-object", "--from-elf", obj, "-o", alias, ok=False)
        self.assertEqual(obj.read_bytes(), original)
        listing = self.directory / "listing.json"
        self.tool("badge-asm", "--listing", listing, assembly, "-o", obj)
        preserved = obj.read_bytes()
        self.tool("badge-asm", "--listing", obj, assembly, "-o", obj, ok=False)
        self.assertEqual(obj.read_bytes(), preserved)
        self.assertFalse(list(self.directory.glob("*.tmp.*")))

    def test_link_extract_aliases_and_repeated_outputs(self):
        assembly = self.source("input.s",
            '.syntax unified\n.thumb\n.section .text,"ax"\n'
            '.global entry\n.type entry,%function\n.thumb_func\nentry:\nmovs r0,#7\nbx lr\n')
        obj = self.directory / "input.o"
        self.tool("badge-asm", assembly, "-o", obj)
        script = self.source("script.ld",
            "MEMORY { FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4096k }\n"
            "ENTRY(entry)\nSECTIONS { .text : { *(.text*) } > FLASH }\n")
        elf = self.directory / "output.elf"
        map_file = self.directory / "output.map"
        binary = self.directory / "output.bin"
        for _ in range(2):
            self.tool("badge-link", "-T", script, "-Map", map_file, obj, "-o", elf)
            self.tool("elf2bin", "-O", "binary", elf, binary)
            self.assertEqual(binary.read_bytes(), bytes.fromhex("07207047"))
            self.assertEqual(self.tool("badge-elf-info", "-u", elf).stdout.strip(), b"")
        original = elf.read_bytes()
        self.tool("badge-link", "-T", script, "-Map", elf, obj, "-o", elf, ok=False)
        if os.name == "nt":
            # Must reject aliases even when neither output exists yet.
            fresh = self.directory / "fresh.elf"
            self.tool("badge-link", "-T", script, "-Map", self.directory / "FRESH.ELF",
                      obj, "-o", fresh, ok=False)
            self.assertFalse(fresh.exists())
        alias = self.directory / "alias.elf"
        os.link(elf, alias)
        self.tool("elf2bin", "-O", "binary", elf, alias, ok=False)
        self.assertEqual(elf.read_bytes(), original)
        original_object = obj.read_bytes()
        self.tool("badge-link", "-T", script, obj, "-o", obj, ok=False)
        self.assertEqual(obj.read_bytes(), original_object)
        self.assertFalse(list(self.directory.glob("*.tmp.*")))

    def test_sha256_binary_input(self):
        path = self.directory / "binary input.dat"
        data = bytes(range(256)) * 1000 + b"\r\n\x1a\n"
        path.write_bytes(data)
        result = self.tool("badge-sha256", path)
        digest = result.stdout.lstrip(b"\\").split()[0].decode("ascii")
        self.assertEqual(digest, hashlib.sha256(data).hexdigest())

    def test_existing_core_c_regressions(self):
        compiler = shutil.which(os.environ.get("CC", "cc"))
        self.assertIsNotNone(compiler, "native C regression tests require CC or cc")
        cases = {
            "target_cost_test": ["tests/target_cost_test.c", "src/target_cost.c"],
            "ir_pipeline_test": ["tests/ir_pipeline_test.c", "src/ir_core.c", "src/ir_analysis.c"],
            "badge_elf_info_test": ["tests/badge_elf_info_test.c"],
        }
        for name, sources in cases.items():
            with self.subTest(name=name):
                target = self.directory / (name + SUFFIX)
                command = [str(Path(compiler).resolve()), "-std=c11", "-Wall", "-Wextra",
                           "-Werror", "-O1", "-g", "-I" + str(ROOT / "src"),
                           *[str(ROOT / source) for source in sources], "-o", str(target)]
                result = subprocess.run(command, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
                result = subprocess.run([str(target)], cwd=ROOT, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))

    def test_frontend_optimization_codegen_pipeline(self):
        source = self.source("input.c",
            "unsigned add(unsigned a,unsigned b) { return a+b; }\n"
            "unsigned entry(void) { unsigned n=0; for(unsigned i=0;i<10;++i) n=add(n,i); return n; }\n")
        preprocessed = self.directory / "input.i"
        self.tool("badge-cpp", source, "-o", preprocessed)
        # Exercise the independent parse and semantic entry points too.
        self.tool("badge-parse", preprocessed, "-o", self.directory / "ast.json")
        self.tool("badge-sema", preprocessed, "-o", self.directory / "sema.json")
        frontend = self.directory / "frontend.ll"
        optimized = self.directory / "optimized.ll"
        assembly = self.directory / "assembly.s"
        obj = self.directory / "output.o"
        self.tool("badge-ir", preprocessed, "-o", frontend)
        for _ in range(2):
            self.tool("badge-opt", frontend, "-o", optimized)
            self.tool("badge-codegen", optimized, "-o", assembly)
            self.tool("badge-asm", assembly, "-o", obj)
        self.assertTrue(obj.read_bytes().startswith(b"\x7fELF"))
        original = optimized.read_bytes()
        self.tool("badge-codegen", optimized, "-o", optimized, ok=False)
        self.assertEqual(optimized.read_bytes(), original)

    def test_symlink_output_rejected(self):
        source = self.source("source.c", "int x;\n")
        target = self.source("target.i", "preserved")
        link = self.directory / "symlink.i"
        try:
            link.symlink_to(target)
        except OSError as error:
            self.skipTest(f"host does not permit symlink creation: {error}")
        self.tool("badge-cpp", source, "-o", link, ok=False)
        self.assertEqual(target.read_text(encoding="ascii"), "preserved")
        self.assertTrue(link.is_symlink())


if __name__ == "__main__":
    unittest.main()
