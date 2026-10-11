#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
clang=${LLVM_CLANG:-clang}
lld=${LLVM_LLD:-ld.lld}
qemu=${QEMU_ARM:-qemu-system-arm}
dir=build/tilt-maze-oracle
mkdir -p "$dir"
target=(--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -ffreestanding -fno-builtin
    -I../src/shared -I../src/platform/badger -I../src/tools)
sources=(tests/fixtures/tilt_maze_fixture.c ../src/platform/badger/graphics.c
    ../src/tools/diagnostics_logic.c)
./build/badge-asm tests/fixtures/codegen_start.S -o "$dir/start.o"
for compiler in owned llvm; do
    objects=()
    for source in "${sources[@]}"; do
        name=$(basename "$source" .c)
        object="$dir/$name-$compiler.o"
        if [[ "$compiler" == owned ]]; then
            bash scripts/owned-cc.sh "${target[@]}" -c "$source" -o "$object"
        else
            "$clang" "${target[@]}" -std=c11 -Oz -ffunction-sections \
                -fdata-sections -c "$source" -o "$object"
        fi
        objects+=("$object")
    done
    "$lld" --gc-sections -T tests/fixtures/tilt_maze_fixture.ld \
        "$dir/start.o" "${objects[@]}" -o "$dir/maze-$compiler.elf"
    python3 - "$qemu" "$dir/maze-$compiler.elf" <<'PY'
import subprocess
import sys

result = subprocess.run(
    [sys.argv[1], "-M", "mps2-an505", "-nographic", "-monitor", "none",
     "-serial", "none", "-semihosting-config", "enable=on,target=native",
     "-kernel", sys.argv[2]],
    timeout=120, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
if result.returncode:
    sys.stderr.buffer.write(result.stdout + result.stderr)
    raise SystemExit(f"Maze target execution failed: {sys.argv[2]} (exit {result.returncode})")
PY
    printf 'PASS: %s maze fingerprint, calibration, rendering, hazards and progression\n' "$compiler"
done
