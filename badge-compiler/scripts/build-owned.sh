#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
if [[ ${1:-} == --help ]]; then
    printf 'Usage: bash badge-compiler/scripts/build-owned.sh [all|diagnostics|usb-wifi-udp ...]\n'
    printf 'No LLVM target tools are used. Host CC/build utilities are still required.\n'
    printf 'BADGE_BUILD_JOBS=1..16 selects parallelism (default: 4; use 1 for serial comparisons).\n'
    printf 'BADGE_CODEGEN_FRAME_ADDRESSING=materialized|direct selects C07 frame addressing.\n'
    printf 'BADGE_CODEGEN_REGISTER_RESIDENCY=stack|local|global|global-flex|global-compact selects C07 register residency.\n'
    printf 'BADGE_CODEGEN_SCALAR_SELECTION=legacy|thumb|thumb-registers|thumb-layout|thumb-compact|thumb-schedule|thumb-relax|thumb-branch|thumb-homes|thumb-calls|thumb-effects|thumb-frames|thumb-control|thumb-operands|thumb-widths|thumb-constants selects C07 scalar forms.\n'
    printf 'BADGE_CODEGEN_MEMORY_SELECTION=byte|word selects C07 memory forms.\n'
    printf 'BADGE_IR_EXPRESSION_LOWERING=legacy|ssa selects C05 expression lowering (default: ssa).\n'
    printf 'Unset policies inherit compiler defaults; query badge-compiler/build/badge-codegen --default-{frame-addressing,register-residency,scalar-selection,memory-selection}.\n'
    exit 0
fi
jobs=${BADGE_BUILD_JOBS-4}
if [[ ! "$jobs" =~ ^([1-9]|1[0-6])$ ]]; then
    printf 'build-owned: BADGE_BUILD_JOBS must be an integer from 1 to 16\n' >&2
    exit 2
fi
if [[ $# == 0 ]]; then set -- all usb-wifi-udp; fi
for target in "$@"; do
    case "$target" in
        all|diagnostics|usb-wifi-udp) ;;
        *) printf 'build-owned: unsupported target: %s\n' "$target" >&2; exit 2 ;;
    esac
done
make -j"$jobs" -C badge-compiler all
frontend=${BADGE_IR_EXPRESSION_LOWERING-$(badge-compiler/build/badge-ir --default-expression-lowering)}
case "$frontend" in
    legacy|ssa) ;;
    *) printf 'build-owned: BADGE_IR_EXPRESSION_LOWERING must be legacy or ssa\n' >&2; exit 2 ;;
esac
codegen=${BADGE_CODEGEN_FRAME_ADDRESSING-$(badge-compiler/build/badge-codegen --default-frame-addressing)}
case "$codegen" in
    materialized|direct) ;;
    *) printf 'build-owned: BADGE_CODEGEN_FRAME_ADDRESSING must be materialized or direct\n' >&2; exit 2 ;;
esac
residency=${BADGE_CODEGEN_REGISTER_RESIDENCY-$(badge-compiler/build/badge-codegen --default-register-residency)}
case "$residency" in
    stack|local|global|global-flex|global-compact) ;;
    *) printf 'build-owned: BADGE_CODEGEN_REGISTER_RESIDENCY must be stack, local, global, global-flex or global-compact\n' >&2; exit 2 ;;
esac
selection=${BADGE_CODEGEN_SCALAR_SELECTION-$(badge-compiler/build/badge-codegen --default-scalar-selection)}
case "$selection" in
    legacy|thumb|thumb-registers|thumb-layout|thumb-compact|thumb-schedule|thumb-relax|thumb-branch|thumb-homes|thumb-calls|thumb-effects|thumb-frames|thumb-control|thumb-operands|thumb-widths|thumb-constants) ;;
    *) printf 'build-owned: unsupported BADGE_CODEGEN_SCALAR_SELECTION (including thumb-constants candidate)\n' >&2; exit 2 ;;
esac
memory=${BADGE_CODEGEN_MEMORY_SELECTION-$(badge-compiler/build/badge-codegen --default-memory-selection)}
case "$memory" in
    byte|word) ;;
    *) printf 'build-owned: BADGE_CODEGEN_MEMORY_SELECTION must be byte or word\n' >&2; exit 2 ;;
esac
dir=badge-compiler/build/owned
mkdir -p "$dir"
rm -f "$dir/SUCCESS"
printf '%s\n' "$@" > "$dir/targets.txt"
printf '%s\n' "${BADGE_OPT_PASSES-default}" > "$dir/optimizer-selection.txt"
printf '%s\n' "$codegen" > "$dir/codegen-frame-addressing.txt"
printf '%s\n' "$residency" > "$dir/codegen-register-residency.txt"
printf '%s\n' "$selection" > "$dir/codegen-scalar-selection.txt"
printf '%s\n' "$memory" > "$dir/codegen-memory-selection.txt"
printf '%s\n' "$jobs" > "$dir/build-jobs.txt"
printf '%s\n' "$frontend" > "$dir/frontend-expression-lowering.txt"
targets=()
for target in "$@"; do
    if [[ "$target" == usb-wifi-udp ]]; then
        targets+=("$dir/universe26/uf2/usb_wifi_udp.uf2")
    else targets+=("$target"); fi
done
badge-compiler/build/badge-sha256 badge-compiler/src/badge_opt.c badge-compiler/src/ir_core.c badge-compiler/src/ir_passes.c \
    badge-compiler/src/ir_constant_fold.c badge-compiler/src/ir_aggregate_memory.c \
    badge-compiler/src/ir_wide_division.c badge-compiler/src/ir_dead_instructions.c \
    badge-compiler/src/ir_scalar_promotion.c badge-compiler/src/ir_mem2reg.c badge-compiler/src/ir_promotion.h \
    badge-compiler/src/ir_sccp.c badge-compiler/src/ir_integer.h \
    badge-compiler/src/ir_instruction_combine.c \
    badge-compiler/src/ir_local_cse.c \
    badge-compiler/src/ir_local_memory.c \
    badge-compiler/src/ir_loop_invariant.c \
    badge-compiler/src/ir_cross_function.c \
    badge-compiler/src/ir_loop_admission.c badge-compiler/src/ir_loop_admission.h \
    badge-compiler/src/assembly_cost.c badge-compiler/src/assembly_cost.h \
    badge-compiler/src/ir_target_strength.c \
    badge-compiler/src/ir_analysis.c badge-compiler/src/ir_analysis.h \
    badge-compiler/src/ir_pass.h badge-compiler/src/target.h \
    badge-compiler/src/badge_codegen.c badge-compiler/src/codegen.c badge-compiler/src/codegen.h \
    badge-compiler/src/codegen_internal.h badge-compiler/src/codegen_cfg.c badge-compiler/src/codegen_frame.c badge-compiler/src/codegen_thumb.c badge-compiler/src/codegen_register.c badge-compiler/src/codegen_global.c badge-compiler/src/codegen_select.c badge-compiler/src/codegen_schedule.c badge-compiler/src/codegen_assembly.c badge-compiler/src/codegen_operands.c badge-compiler/src/thumb_immediate.h badge-compiler/src/target_cost.c badge-compiler/src/target_cost.h \
    badge-compiler/src/badge_cpp.c badge-compiler/src/badge_ir.c \
    badge-compiler/src/badge_parse.c badge-compiler/src/badge_sema.c badge-compiler/src/c_frontend.h \
    badge-compiler/src/c_semantic.h badge-compiler/src/c_lexer.h badge-compiler/src/host_io.h \
    badge-compiler/src/badge_asm.c badge-compiler/src/badge_thumb.h badge-compiler/src/badge_object.c \
    badge-compiler/src/badge_object.h badge-compiler/src/badge_link.c badge-compiler/src/elf2bin.c \
    badge-compiler/src/badge_elf_info.c src/host/tools/bin2uf2.c Makefile badge-compiler/Makefile \
    badge-compiler/scripts/owned-cc.sh badge-compiler/scripts/owned-pipeline.sh badge-compiler/scripts/build-owned.sh \
    badge-compiler/src/badge_sha256.c > "$dir/harness.sha256"
make -B -j"$jobs" BUILD_DIR="$dir" RP2350_CC='bash badge-compiler/scripts/owned-cc.sh' \
    RP2350_OBJCOPY="$root/badge-compiler/build/elf2bin" \
    RP2350_NM="$root/badge-compiler/build/badge-elf-info" \
    RP2350_SIZE="$root/badge-compiler/build/badge-elf-info" "${targets[@]}" 2>&1 | tee "$dir/build.log"
printf 'PASS: selected firmware built from source to UF2 without LLVM target tools.\n' | tee "$dir/SUCCESS"
