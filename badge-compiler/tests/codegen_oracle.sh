#!/usr/bin/env bash
set -euo pipefail
clang=${LLVM_CLANG:-clang}
opt=${LLVM_OPT:-opt}
lld=${LLVM_LLD:-ld.lld}
qemu=${QEMU_ARM:-qemu-system-arm}
dir=build/codegen-oracle
if [[ ${IR_OPTIMIZER:-llvm} == owned ]]; then dir=build/opt-oracle; fi
mkdir -p "$dir"
target=(--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -ffreestanding -fno-builtin)
source scripts/owned-pipeline.sh
owned_frontend_selection
frontend_flags=(${owned_frontend_arguments[@]+"${owned_frontend_arguments[@]}"})
ir() {
    ./build/badge-ir ${frontend_flags[@]+"${frontend_flags[@]}"} "$@"
}
owned_codegen_selection
codegen_flags=(${owned_codegen_arguments[@]+"${owned_codegen_arguments[@]}"})
codegen() {
    ./build/badge-codegen ${codegen_flags[@]+"${codegen_flags[@]}"} "$@"
}
optimize() {
    if [[ ${IR_OPTIMIZER:-llvm} == owned ]]; then
        if [[ ${BADGE_OPT_PASSES+x} ]]; then
            ./build/badge-opt "--passes=$BADGE_OPT_PASSES" "$1" -o "$2"
        else
            ./build/badge-opt "$1" -o "$2"
        fi
        "$opt" -passes=verify -disable-output "$2"
    else
        "$opt" '-passes=default<Oz>' -S "$1" -o "$2"
    fi
}
./build/badge-asm tests/fixtures/codegen_start.S -o "$dir/start.o"
./build/badge-cpp tests/fixtures/codegen_oracle_fixture.c -o "$dir/fixture.i"
ir "$dir/fixture.i" -o "$dir/frontend.ll"
optimize "$dir/frontend.ll" "$dir/optimized.ll"
codegen "$dir/optimized.ll" -o "$dir/owned.s"
./build/badge-asm "$dir/owned.s" -o "$dir/owned.o"
"$clang" "${target[@]}" -Oz -c -x ir "$dir/optimized.ll" -o "$dir/reference.o"
"$clang" "${target[@]}" -std=c11 -Oz -c tests/fixtures/codegen_oracle_fixture.c -o "$dir/reference-source.o"
run_target() {
    python3 - "$qemu" "$1" <<'PY'
import subprocess,sys
result=subprocess.run([sys.argv[1],"-M","mps2-an505","-nographic","-monitor","none",
    "-serial","none","-semihosting-config","enable=on,target=native","-kernel",sys.argv[2]],
    timeout=15,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
if result.returncode:
    sys.stderr.buffer.write(result.stdout+result.stderr)
    raise SystemExit(f"Target execution failed: {sys.argv[2]} (exit {result.returncode})")
PY
}
for tool in owned reference reference-source; do
    "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$tool.o" -o "$dir/$tool.elf"
    run_target "$dir/$tool.elf"
done
"$opt" -passes=verify -disable-output tests/fixtures/global_residency_oracle_fixture.ll
codegen tests/fixtures/global_residency_oracle_fixture.ll -o "$dir/global-residency.s"
./build/badge-asm "$dir/global-residency.s" -o "$dir/global-residency.o"
"$clang" "${target[@]}" -Oz -c -x ir tests/fixtures/global_residency_oracle_fixture.ll -o "$dir/global-residency-llvm.o"
for tool in global-residency global-residency-llvm; do
    "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$tool.o" -o "$dir/$tool.elf"
    run_target "$dir/$tool.elf"
done
"$opt" -passes=verify -disable-output tests/fixtures/scalar_selection_oracle_fixture.ll
codegen tests/fixtures/scalar_selection_oracle_fixture.ll -o "$dir/scalar-selection.s"
./build/badge-asm "$dir/scalar-selection.s" -o "$dir/scalar-selection.o"
"$clang" "${target[@]}" -Oz -c -x ir tests/fixtures/scalar_selection_oracle_fixture.ll -o "$dir/scalar-selection-llvm.o"
"$clang" "${target[@]}" -std=c11 -Oz -c tests/fixtures/scalar_selection_oracle_caller.c -o "$dir/scalar-selection-caller.o"
for tool in scalar-selection scalar-selection-llvm; do
    "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/scalar-selection-caller.o" "$dir/$tool.o" -o "$dir/$tool.elf"
    run_target "$dir/$tool.elf"
done
"$opt" -passes=verify -disable-output tests/fixtures/memory_selection_oracle_fixture.ll
codegen tests/fixtures/memory_selection_oracle_fixture.ll -o "$dir/memory-selection.s"
./build/badge-asm "$dir/memory-selection.s" -o "$dir/memory-selection.o"
"$clang" "${target[@]}" -Oz -c -x ir tests/fixtures/memory_selection_oracle_fixture.ll -o "$dir/memory-selection-llvm.o"
"$clang" "${target[@]}" -std=c11 -Oz -c tests/fixtures/memory_selection_oracle_caller.c -o "$dir/memory-selection-caller.o"
for tool in memory-selection memory-selection-llvm; do
    "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/memory-selection-caller.o" "$dir/$tool.o" -o "$dir/$tool.elf"
    run_target "$dir/$tool.elf"
done
printf 'badge-codegen: memory length/tail/nonzero-fill/full-buffer guard differential execution PASS\n'
"$opt" -passes=verify -disable-output tests/fixtures/loop_invariant_ir_fixture.ll
"$clang" "${target[@]}" -Oz -c -x ir tests/fixtures/loop_invariant_ir_fixture.ll -o "$dir/loop-invariant-raw-llvm.o"
"$clang" "${target[@]}" -std=c11 -Oz -c tests/fixtures/loop_invariant_oracle_caller.c -o "$dir/loop-invariant-caller.o"
for pass in loop-invariant loop-cost; do
    if [[ ${IR_OPTIMIZER:-llvm} == owned ]]; then
        ./build/badge-opt "--passes=$pass" tests/fixtures/loop_invariant_ir_fixture.ll -o "$dir/$pass-raw.ll"
    else
        "$opt" -S -passes=verify tests/fixtures/loop_invariant_ir_fixture.ll -o "$dir/$pass-raw.ll"
    fi
    "$opt" -passes=verify -disable-output "$dir/$pass-raw.ll"
    codegen "$dir/$pass-raw.ll" -o "$dir/$pass-raw.s"
    ./build/badge-asm "$dir/$pass-raw.s" -o "$dir/$pass-raw.o"
    "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/loop-invariant-caller.o" "$dir/$pass-raw.o" -o "$dir/$pass-raw.elf"
    run_target "$dir/$pass-raw.elf"
    printf 'badge-opt: %s zero/one/many/nested/irreducible/volatile/trap differential execution PASS\n' "$pass"
done
for tool in loop-invariant-raw-llvm; do
    "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/loop-invariant-caller.o" "$dir/$tool.o" -o "$dir/$tool.elf"
    run_target "$dir/$tool.elf"
done
for abi in ir_abi_fixture frame_addressing_abi_fixture register_residency_abi_fixture call_transport_abi_fixture; do
abi_dir=$dir
if [[ "$abi" == frame_addressing_abi_fixture ]]; then
    abi_dir="$dir/frame-addressing-abi"
    mkdir -p "$abi_dir"
fi
if [[ "$abi" == register_residency_abi_fixture ]]; then
    abi_dir="$dir/register-residency-abi"
    mkdir -p "$abi_dir"
fi
if [[ "$abi" == call_transport_abi_fixture ]]; then
    abi_dir="$dir/call-transport-abi"
    mkdir -p "$abi_dir"
fi
for side in caller callee; do
    flags=()
    if [[ "$side" == caller ]]; then flags=(-DCALLER); fi
    ./build/badge-cpp "${flags[@]}" "tests/fixtures/$abi.c" -o "$abi_dir/$side.i"
    ir "$abi_dir/$side.i" -o "$abi_dir/$side.frontend.ll"
    optimize "$abi_dir/$side.frontend.ll" "$abi_dir/$side.ll"
    codegen "$abi_dir/$side.ll" -o "$abi_dir/$side.s"
    if [[ "$abi" == call_transport_abi_fixture && "$side" == callee &&
          ${IR_OPTIMIZER:-llvm} == owned ]]; then
        if ! grep -q '^blx r12$' "$abi_dir/$side.s" ||
           ! grep -q '^bl call_recursive$' "$abi_dir/$side.s"; then
            printf 'call transport ABI oracle did not retain indirect and recursive calls\n' >&2
            exit 1
        fi
    fi
    if [[ "$abi" == frame_addressing_abi_fixture && "$side" == callee &&
          ${IR_OPTIMIZER:-llvm} == owned ]]; then
        if ! grep -q 'movt r12, #1' "$abi_dir/$side.s"; then
            printf 'frame-addressing ABI fixture did not exercise incoming offsets above 64 KiB\n' >&2
            exit 1
        fi
    fi
    ./build/badge-asm "$abi_dir/$side.s" -o "$abi_dir/owned-$side.o"
    "$clang" "${target[@]}" "${flags[@]}" -std=c11 -Oz -c "tests/fixtures/$abi.c" -o "$abi_dir/reference-$side.o"
done
for caller in owned reference; do
    callee=reference
    if [[ "$caller" == reference ]]; then callee=owned; fi
    "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$abi_dir/$caller-caller.o" \
        "$abi_dir/$callee-callee.o" -o "$abi_dir/mixed-$caller.elf"
    run_target "$abi_dir/mixed-$caller.elf"
done
"$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$abi_dir/owned-caller.o" \
    "$abi_dir/owned-callee.o" -o "$abi_dir/all-owned-abi.elf"
run_target "$abi_dir/all-owned-abi.elf"
done
printf 'badge-codegen: target Thumb execution and bidirectional mixed LLVM/owned AAPCS calls PASS\n'
if [[ ${IR_OPTIMIZER:-llvm} == owned ]]; then
    for fixture in division scalar-promotion sccp instruction-combine local-cse loop-invariant; do
        source=tests/fixtures/opt_oracle_fixture.c
        if [[ "$fixture" == scalar-promotion ]]; then source=tests/fixtures/scalar_promotion_oracle_fixture.c; fi
        if [[ "$fixture" == sccp ]]; then source=tests/fixtures/sccp_oracle_fixture.c; fi
        if [[ "$fixture" == instruction-combine ]]; then source=tests/fixtures/instruction_combine_oracle_fixture.c; fi
        if [[ "$fixture" == local-cse ]]; then source=tests/fixtures/local_cse_oracle_fixture.c; fi
        if [[ "$fixture" == loop-invariant ]]; then source=tests/fixtures/loop_invariant_oracle_fixture.c; fi
        ./build/badge-cpp "$source" -o "$dir/$fixture.i"
        ir "$dir/$fixture.i" -o "$dir/$fixture.frontend.ll"
        optimize "$dir/$fixture.frontend.ll" "$dir/$fixture.ll"
        codegen "$dir/$fixture.ll" -o "$dir/$fixture.s"
        ./build/badge-asm "$dir/$fixture.s" -o "$dir/$fixture.o"
        "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$fixture.o" -o "$dir/$fixture.elf"
        run_target "$dir/$fixture.elf"
        "$clang" "${target[@]}" -Oz -c -x ir "$dir/$fixture.ll" -o "$dir/$fixture-reference.o"
        "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$fixture-reference.o" -o "$dir/$fixture-reference.elf"
        run_target "$dir/$fixture-reference.elf"
        if [[ "$fixture" != division ]]; then
            "$clang" "${target[@]}" -std=c11 -Oz -c "$source" -o "$dir/$fixture-source.o"
            "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$fixture-source.o" -o "$dir/$fixture-source.elf"
            run_target "$dir/$fixture-source.elf"
        fi
        "$clang" -std=c11 -Wall -Wextra -Werror -O2 "$source" -o "$dir/$fixture-host"
        "$dir/$fixture-host"
    done
    for fixture in scalar-promotion mem2reg sccp instruction-combine local-cse; do
        source=tests/fixtures/scalar_promotion_ir_fixture.ll
        if [[ "$fixture" == mem2reg ]]; then source=tests/fixtures/mem2reg_ir_fixture.ll; fi
        if [[ "$fixture" == sccp ]]; then source=tests/fixtures/sccp_ir_fixture.ll; fi
        if [[ "$fixture" == instruction-combine ]]; then source=tests/fixtures/instruction_combine_ir_fixture.ll; fi
        if [[ "$fixture" == local-cse ]]; then source=tests/fixtures/local_cse_ir_fixture.ll; fi
        optimize "$source" "$dir/$fixture-ir.ll"
        codegen "$dir/$fixture-ir.ll" -o "$dir/$fixture-ir.s"
        ./build/badge-asm "$dir/$fixture-ir.s" -o "$dir/$fixture-ir.o"
        "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$fixture-ir.o" -o "$dir/$fixture-ir.elf"
        run_target "$dir/$fixture-ir.elf"
        "$clang" "${target[@]}" -Oz -c -x ir "$source" -o "$dir/$fixture-ir-reference.o"
        "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$fixture-ir-reference.o" -o "$dir/$fixture-ir-reference.elf"
        run_target "$dir/$fixture-ir-reference.elf"
    done
    printf 'badge-opt: owned passes, general wide division/remainder and complete target ABI execution PASS\n'
    printf 'badge-opt: scalar promotion source-level narrow/wide/pointer/escape/volatile/loop differential execution PASS\n'
    printf 'badge-opt: scalar promotion entry-store branch/wide/PHI and block-local loop IR differential execution PASS\n'
    printf 'badge-opt: general loop-carried narrow/wide/bool/pointer and irreducible/parallel-edge differential execution PASS\n'
    printf 'badge-opt: SCCP source and raw-IR branch/switch/PHI/loop/volatile differential execution PASS\n'
    printf 'badge-opt: instruction combining source/raw-IR exhaustive narrow and wide/flag boundary differential execution PASS\n'
    printf 'badge-opt: local CSE typed expressions/addresses/casts with calls and volatile accesses differential execution PASS\n'
fi
