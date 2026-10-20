"""Build native owned compiler tools without Make, Bash, or third-party Python packages."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
CODEGEN = [
    "codegen.c", "codegen_cfg.c", "codegen_frame.c", "codegen_thumb.c",
    "codegen_register.c", "codegen_global.c", "codegen_select.c",
    "codegen_schedule.c", "codegen_assembly.c", "codegen_operands.c", "target_cost.c",
]
OPT = [
    "ir_core.c", "ir_analysis.c", "ir_passes.c", "ir_constant_fold.c",
    "ir_aggregate_memory.c", "ir_wide_division.c", "ir_dead_instructions.c",
    "ir_scalar_promotion.c", "ir_mem2reg.c", "ir_sccp.c", "ir_instruction_combine.c",
    "ir_local_cse.c", "ir_loop_invariant.c", "ir_target_strength.c",
    "ir_local_memory.c", "ir_cross_function.c", "ir_loop_admission.c",
    "assembly_cost.c", *CODEGEN,
]
TOOLS = {
    "elf2bin": ([], ["src/elf2bin.c"]),
    "badge-elf-info": ([], ["src/badge_elf_info.c"]),
    "badge-cpp": ([], ["src/badge_cpp.c"]),
    "badge-parse": ([], ["src/badge_parse.c"]),
    "badge-sema": (["BADGE_PARSE_LIBRARY"], ["src/badge_sema.c", "src/badge_parse.c"]),
    "badge-ir": (["BADGE_SEMA_LIBRARY", "BADGE_PARSE_LIBRARY"],
                 ["src/badge_ir.c", "src/badge_sema.c", "src/badge_parse.c"]),
    "badge-opt": ([], ["src/badge_opt.c", *["src/" + name for name in OPT]]),
    "badge-ir-metrics": ([], ["tools/badge_ir_metrics.c", *["src/" + name for name in OPT]]),
    "badge-codegen": ([], ["src/badge_codegen.c", *["src/" + name for name in CODEGEN]]),
    "badge-object": ([], ["src/badge_object.c"]),
    "badge-asm": (["BADGE_OBJECT_LIBRARY"], ["src/badge_asm.c", "src/badge_object.c"]),
    "badge-link": (["BADGE_OBJECT_LIBRARY"], ["src/badge_link.c", "src/badge_object.c"]),
    "badge-sha256": ([], ["src/badge_sha256.c"]),
}


def executable(name):
    return BUILD / (name + (".exe" if os.name == "nt" else ""))


def build_tools(cc):
    compiler = shutil.which(cc)
    if compiler is None:
        raise FileNotFoundError(f"host C compiler not found: {cc}")
    compiler = str(Path(compiler).resolve())
    BUILD.mkdir(exist_ok=True)
    manifest = BUILD / "native-tools.json"
    manifest.unlink(missing_ok=True)
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-Isrc"]
    # Compile shared source/definition combinations once, as Make's recipes
    # build several large tools from overlapping implementation files.
    objects = {}
    hashes = {}
    for name, (definitions, sources) in TOOLS.items():
        linked = []
        for source in sources:
            key = (source, tuple(definitions))
            if key not in objects:
                tag = hashlib.sha256(repr(key).encode("ascii")).hexdigest()[:12]
                obj = BUILD / (Path(source).stem + "-" + tag + ".o")
                command = [compiler, *flags, *["-D" + define for define in definitions],
                           "-c", str(ROOT / source), "-o", str(obj)]
                print(f"Compiling {source} {' '.join(definitions)}", flush=True)
                subprocess.run(command, cwd=ROOT, check=True)
                objects[key] = str(obj)
            linked.append(objects[key])
        target = executable(name)
        print(f"Linking {target.name}", flush=True)
        subprocess.run([compiler, *linked, "-o", str(target)], cwd=ROOT, check=True)
        hashes[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
    source_hash = hashlib.sha256()
    for path in sorted((ROOT / "src").glob("*")):
        if path.is_file():
            source_hash.update(path.name.encode("ascii") + b"\0" + path.read_bytes())
    manifest.write_text(json.dumps({
        "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
        "host_sources_sha256": source_hash.hexdigest(),
        "tools_sha256": hashes,
    }, indent=2) + "\n", encoding="ascii")
    print(f"PASS: built {len(TOOLS)} native tools in {BUILD}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="Clang or GCC-compatible native C compiler")
    args = parser.parse_args()
    try:
        build_tools(args.cc)
    except (OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
