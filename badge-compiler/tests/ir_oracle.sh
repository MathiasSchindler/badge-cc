#!/usr/bin/env bash
set -euo pipefail
clang=${LLVM_CLANG:-clang}
opt=${LLVM_OPT:-$(dirname "$clang")/opt}
dir=build/ir-oracle
mkdir -p "$dir"
source scripts/owned-pipeline.sh
owned_frontend_selection
frontend_flags=(${owned_frontend_arguments[@]+"${owned_frontend_arguments[@]}"})
ir() {
    ./build/badge-ir ${frontend_flags[@]+"${frontend_flags[@]}"} "$@"
}
ir tests/fixtures/ir_oracle_fixture.c -o "$dir/owned.ll"
"$clang" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 \
    -ffreestanding -S -emit-llvm tests/fixtures/ir_oracle_fixture.c -o "$dir/reference.ll"
for tool in owned reference; do
    "$opt" -passes=verify -disable-output "$dir/$tool.ll"
    "$clang" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -Oz \
        -S -x ir "$dir/$tool.ll" -o "$dir/$tool.s"
    # These fixtures avoid pointer-containing records and pointer-width assumptions.
    sed 's/"target-cpu"="cortex-m33"//g; s/"target-features"="[^"]*"//g; s/"use-soft-float"="true"//g' \
        "$dir/$tool.ll" > "$dir/$tool.host.ll"
    for level in 0 z; do
        "$clang" -Wno-override-module "-O$level" "$dir/$tool.host.ll" -o "$dir/$tool-$level"
        "$dir/$tool-$level"
    done
done
for side in caller callee; do
    flags=()
    if [[ "$side" == caller ]]; then flags=(-DCALLER); fi
    ./build/badge-cpp "${flags[@]}" tests/fixtures/ir_abi_fixture.c -o "$dir/$side.i"
    ir "$dir/$side.i" -o "$dir/owned-$side.ll"
    "$clang" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 \
        -ffreestanding -S -emit-llvm "${flags[@]}" tests/fixtures/ir_abi_fixture.c -o "$dir/reference-$side.ll"
    for tool in owned reference; do
        "$opt" -passes=verify -disable-output "$dir/$tool-$side.ll"
        "$clang" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -Oz \
            -c -x ir "$dir/$tool-$side.ll" -o "$dir/$tool-$side.arm.o"
        sed 's/"target-cpu"="cortex-m33"//g; s/"target-features"="[^"]*"//g; s/"use-soft-float"="true"//g' \
            "$dir/$tool-$side.ll" > "$dir/$tool-$side.host.ll"
    done
done
for level in 0 z; do
    for caller in owned reference; do
        callee=reference
        if [[ "$caller" == reference ]]; then callee=owned; fi
        "$clang" -Wno-override-module "-O$level" "$dir/$caller-caller.host.ll" \
            "$dir/$callee-callee.host.ll" -o "$dir/mixed-$caller-$level"
        "$dir/mixed-$caller-$level"
    done
done
printf 'badge-ir: LLVM verification, Cortex-M33 lowering, reference/owned runtime checks at O0/Oz PASS\n'
printf 'badge-ir: bidirectional mixed-frontend record/narrow-scalar ABI runtime checks at O0/Oz PASS\n'
