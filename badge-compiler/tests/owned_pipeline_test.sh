#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../scripts/owned-pipeline.sh"
unset BADGE_OPT_PASSES
unset BADGE_CODEGEN_FRAME_ADDRESSING
unset BADGE_CODEGEN_REGISTER_RESIDENCY
unset BADGE_CODEGEN_SCALAR_SELECTION BADGE_CODEGEN_MEMORY_SELECTION
unset BADGE_IR_EXPRESSION_LOWERING

calls=0
trace=
fail_at=0
check_runner() {
    calls=$((calls + 1))
    trace="$trace${1##*/} "
    case "${1##*/}" in
        badge-cpp)
            if [[ $2 == --assembly ]]; then
                [[ $# == 6 && $3 == -DVALUE=7 && $4 == "source with spaces.S" &&
                   $5 == -o && $6 == "artifacts with spaces/preprocessed.s" ]]
            else
                [[ $# == 5 && $2 == -DVALUE=7 && $3 == "source with spaces.c" &&
                   $4 == -o && $5 == "artifacts with spaces/preprocessed.i" ]]
            fi ;;
        badge-ir)
            local ir_arguments=("$@") ir_index=1
            if [[ ${BADGE_IR_EXPRESSION_LOWERING+x} ]]; then
                [[ $2 == "--expression-lowering=$BADGE_IR_EXPRESSION_LOWERING" ]] || return 1
                ir_index=2
            fi
            [[ ${ir_arguments[$ir_index]} == "artifacts with spaces/preprocessed.i" ]] ;;
        badge-opt)
            if [[ $2 == --passes=* ]]; then
                [[ $2 == --passes=constant-fold && $3 == "artifacts with spaces/frontend.ll" ]]
            else
                [[ $2 == "artifacts with spaces/frontend.ll" ]]
            fi ;;
        badge-codegen)
            local arguments=("$@")
            local index=1
            if [[ ${arguments[$index]} == --memory-selection=* ]]; then
                [[ ${arguments[$index]} == "--memory-selection=$BADGE_CODEGEN_MEMORY_SELECTION" ]] || return 1
                index=$((index + 1))
            fi
            if [[ ${arguments[$index]} == --scalar-selection=* ]]; then
                [[ ${arguments[$index]} == "--scalar-selection=$BADGE_CODEGEN_SCALAR_SELECTION" ]] || return 1
                index=$((index + 1))
            fi
            if [[ ${arguments[$index]} == --frame-addressing=* ]]; then
                [[ ${arguments[$index]} == "--frame-addressing=$BADGE_CODEGEN_FRAME_ADDRESSING" ]] || return 1
                index=$((index + 1))
            fi
            if [[ ${arguments[$index]} == --register-residency=* ]]; then
                [[ ${arguments[$index]} == "--register-residency=$BADGE_CODEGEN_REGISTER_RESIDENCY" ]] || return 1
                index=$((index + 1))
            fi
            [[ ${arguments[$index]} == "artifacts with spaces/optimized.ll" ]] ;;
        badge-asm)
            [[ $2 == "artifacts with spaces/assembly.s" ||
               $2 == "artifacts with spaces/preprocessed.s" ]] ;;
        *) printf 'Unexpected owned command: %s\n' "$1" >&2; exit 1 ;;
    esac || { printf 'Unexpected owned arguments: %s\n' "$*" >&2; exit 1; }
    if [[ $calls == "$fail_at" ]]; then return 23; fi
}

owned_compile_c "/tools with spaces" "source with spaces.c" \
    "artifacts with spaces" "output with spaces.o" check_runner -DVALUE=7
[[ $calls == 5 && "$trace" == "badge-cpp badge-ir badge-opt badge-codegen badge-asm " ]]

for mode in legacy ssa; do
    calls=0
    BADGE_IR_EXPRESSION_LOWERING=$mode owned_compile_c "/tools with spaces" \
        "source with spaces.c" "artifacts with spaces" "output with spaces.o" check_runner -DVALUE=7
    [[ $calls == 5 ]]
done
calls=0
if BADGE_IR_EXPRESSION_LOWERING=invalid owned_compile_c "/tools" \
    "source.c" "artifacts" "output.o" check_runner; then
    printf 'Expected invalid frontend selection to fail\n' >&2; exit 1
else
    [[ $? == 2 && $calls == 0 ]]
fi

for fail_at in 1 2 3 4 5; do
    calls=0
    trace=
    if owned_compile_c "/tools with spaces" "source with spaces.c" \
        "artifacts with spaces" "output with spaces.o" check_runner -DVALUE=7; then
        printf 'Expected owned compilation failure at stage %s\n' "$fail_at" >&2
        exit 1
    else
        status=$?
        [[ $status == 23 && $calls == "$fail_at" ]]
    fi
done
calls=0
trace=
fail_at=0
owned_compile_assembly "/tools with spaces" "source with spaces.S" \
    "artifacts with spaces/preprocessed.s" "output with spaces.o" check_runner -DVALUE=7
[[ $calls == 2 && "$trace" == "badge-cpp badge-asm " ]]
for fail_at in 1 2; do
    calls=0
    trace=
    if owned_compile_assembly "/tools with spaces" "source with spaces.S" \
        "artifacts with spaces/preprocessed.s" "output with spaces.o" check_runner -DVALUE=7; then
        printf 'Expected owned assembly failure at stage %s\n' "$fail_at" >&2
        exit 1
    else
        status=$?
        [[ $status == 23 && $calls == "$fail_at" ]]
    fi
done
calls=0
trace=
fail_at=0
BADGE_OPT_PASSES=constant-fold owned_lower_ir "/tools with spaces" \
    "artifacts with spaces/frontend.ll" "artifacts with spaces/optimized.ll" \
    "artifacts with spaces/assembly.s" "output with spaces.o" check_runner
[[ $calls == 3 && "$trace" == "badge-opt badge-codegen badge-asm " ]]
for mode in materialized direct; do
    calls=0
    BADGE_CODEGEN_FRAME_ADDRESSING=$mode owned_lower_ir "/tools with spaces" \
        "artifacts with spaces/frontend.ll" "artifacts with spaces/optimized.ll" \
        "artifacts with spaces/assembly.s" "output with spaces.o" check_runner
    [[ $calls == 3 ]]
done
calls=0
if BADGE_CODEGEN_FRAME_ADDRESSING=invalid owned_lower_ir "/tools with spaces" \
    "frontend.ll" "optimized.ll" "assembly.s" "output.o" check_runner; then
    printf 'Expected invalid codegen selection to fail\n' >&2; exit 1
else
    [[ $? == 2 && $calls == 0 ]]
fi
for mode in stack local global global-flex global-compact; do
    calls=0
    BADGE_CODEGEN_FRAME_ADDRESSING=direct BADGE_CODEGEN_REGISTER_RESIDENCY=$mode \
        owned_lower_ir "/tools with spaces" "artifacts with spaces/frontend.ll" \
        "artifacts with spaces/optimized.ll" "artifacts with spaces/assembly.s" \
        "output with spaces.o" check_runner
    [[ $calls == 3 ]]
done
for mode in legacy thumb thumb-registers thumb-layout thumb-compact thumb-schedule thumb-relax thumb-branch thumb-homes thumb-calls thumb-effects thumb-frames thumb-control thumb-operands thumb-widths thumb-constants; do
    calls=0
    BADGE_CODEGEN_SCALAR_SELECTION=$mode BADGE_CODEGEN_FRAME_ADDRESSING=direct BADGE_CODEGEN_REGISTER_RESIDENCY=global \
        owned_lower_ir "/tools with spaces" "artifacts with spaces/frontend.ll" \
        "artifacts with spaces/optimized.ll" "artifacts with spaces/assembly.s" \
        "output with spaces.o" check_runner
    [[ $calls == 3 ]]
done
calls=0
if BADGE_CODEGEN_SCALAR_SELECTION=invalid owned_lower_ir "/tools with spaces" \
    "frontend.ll" "optimized.ll" "assembly.s" "output.o" check_runner; then
    printf 'Expected invalid scalar selection to fail\n' >&2; exit 1
else
    [[ $? == 2 && $calls == 0 ]]
fi
calls=0
if BADGE_CODEGEN_REGISTER_RESIDENCY=invalid owned_lower_ir "/tools with spaces" \
    "frontend.ll" "optimized.ll" "assembly.s" "output.o" check_runner; then
    printf 'Expected invalid residency to fail\n' >&2; exit 1
else
    [[ $? == 2 && $calls == 0 ]]
fi
printf 'owned-pipeline: stage order, quoted paths/flags and immediate failure propagation PASS\n'
for mode in byte word; do
    calls=0
    BADGE_CODEGEN_MEMORY_SELECTION=$mode BADGE_CODEGEN_SCALAR_SELECTION=thumb \
        BADGE_CODEGEN_FRAME_ADDRESSING=direct BADGE_CODEGEN_REGISTER_RESIDENCY=global \
        owned_lower_ir "/tools with spaces" "artifacts with spaces/frontend.ll" \
        "artifacts with spaces/optimized.ll" "artifacts with spaces/assembly.s" \
        "output with spaces.o" check_runner
    [[ $calls == 3 ]]
done
calls=0
if BADGE_CODEGEN_MEMORY_SELECTION=invalid owned_lower_ir "/tools with spaces" \
    "frontend.ll" "optimized.ll" "assembly.s" "output.o" check_runner; then
    printf 'Expected invalid memory selection to fail\n' >&2; exit 1
else
    [[ $? == 2 && $calls == 0 ]]
fi
