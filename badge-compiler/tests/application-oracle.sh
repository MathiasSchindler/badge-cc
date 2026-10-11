#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
clang=${LLVM_CLANG:-clang}
lld=${LLVM_LLD:-ld.lld}
qemu=${QEMU_ARM:-qemu-system-arm}
dir=build/application-oracle
mkdir -p "$dir"
target=(--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -ffreestanding -fno-builtin
    -std=c11 -Oz -DCOMPILER_TEST_APPLICATION -DCOMPILER_TEST_TIMING
    -ffunction-sections -fdata-sections
    -I../src/shared -I../src/platform/badger -I../src/tools)
sources=(tests/application_test.c ../src/platform/badger/graphics.c ../src/tools/diagnostics_logic.c)
compiler_dir=$PWD
reference_target=()
for flag in "${target[@]}"; do
    if [[ "$flag" == -I* ]]; then reference_target+=("-I$compiler_dir/${flag#-I}")
    else reference_target+=("$flag"); fi
done
./build/badge-asm tests/fixtures/codegen_start.S -o "$dir/start.o"
compilers=(owned llvm)
if [[ ${APPLICATION_FRONTEND_IR:-0} == 1 ]]; then compilers+=(llvm-frontend); fi
for compiler in "${compilers[@]}"; do
    objects=()
    for source in "${sources[@]}"; do
        name=$(basename "$source" .c)
        object="$dir/$name-$compiler.o"
        if [[ "$compiler" == owned ]]; then
            bash scripts/owned-cc.sh "${target[@]}" -c "$source" -o "$object"
        elif [[ "$compiler" == llvm-frontend ]]; then
            (cd .. && node badge-compiler/tools/backend-attribution.mjs --cc llvm-frontend \
                "${reference_target[@]}" -c "$compiler_dir/$source" -o "$compiler_dir/$object")
        else
            "$clang" "${target[@]}" -c "$source" -o "$object"
        fi
        objects+=("$object")
    done
    "$lld" --gc-sections -T tests/fixtures/tilt_maze_fixture.ld \
        "$dir/start.o" "${objects[@]}" -o "$dir/application-$compiler.elf"
    python3 - "$qemu" "$dir/application-$compiler.elf" <<'PY'
import subprocess
import sys
result = subprocess.run(
    [sys.argv[1], "-M", "mps2-an505", "-nographic", "-monitor", "none",
     "-serial", "none", "-semihosting-config", "enable=on,target=native",
     "-kernel", sys.argv[2]], timeout=180, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
if result.returncode:
    sys.stderr.buffer.write(result.stdout + result.stderr)
    raise SystemExit(f"Application target execution failed: {sys.argv[2]} (exit {result.returncode})")
PY
    printf 'PASS: %s actual application determinism, guards, regions, hazards and goals\n' "$compiler"
done
