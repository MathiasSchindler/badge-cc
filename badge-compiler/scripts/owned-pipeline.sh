#!/usr/bin/env bash

owned_exec() {
    "$@"
}

owned_frontend_selection() {
    owned_frontend_arguments=()
    if [[ ${BADGE_IR_EXPRESSION_LOWERING+x} ]]; then
        case "$BADGE_IR_EXPRESSION_LOWERING" in
            legacy|ssa) owned_frontend_arguments+=("--expression-lowering=$BADGE_IR_EXPRESSION_LOWERING") ;;
            *) printf 'owned-pipeline: BADGE_IR_EXPRESSION_LOWERING must be legacy or ssa\n' >&2; return 2 ;;
        esac
    fi
}
owned_codegen_selection() {
    owned_codegen_arguments=()
    if [[ ${BADGE_CODEGEN_MEMORY_SELECTION+x} ]]; then
        case "$BADGE_CODEGEN_MEMORY_SELECTION" in
            byte|word) owned_codegen_arguments+=("--memory-selection=$BADGE_CODEGEN_MEMORY_SELECTION") ;;
            *) printf 'owned-pipeline: BADGE_CODEGEN_MEMORY_SELECTION must be byte or word\n' >&2; return 2 ;;
        esac
    fi
    if [[ ${BADGE_CODEGEN_SCALAR_SELECTION+x} ]]; then
        case "$BADGE_CODEGEN_SCALAR_SELECTION" in
            legacy|thumb|thumb-registers|thumb-layout|thumb-compact|thumb-schedule|thumb-relax|thumb-branch|thumb-homes|thumb-calls|thumb-effects|thumb-frames|thumb-control|thumb-operands|thumb-widths|thumb-constants) owned_codegen_arguments+=("--scalar-selection=$BADGE_CODEGEN_SCALAR_SELECTION") ;;
            *) printf 'owned-pipeline: unsupported BADGE_CODEGEN_SCALAR_SELECTION (including thumb-constants candidate)\n' >&2; return 2 ;;
        esac
    fi
    if [[ ${BADGE_CODEGEN_FRAME_ADDRESSING+x} ]]; then
        case "$BADGE_CODEGEN_FRAME_ADDRESSING" in
            materialized|direct) owned_codegen_arguments+=("--frame-addressing=$BADGE_CODEGEN_FRAME_ADDRESSING") ;;
            *) printf 'owned-pipeline: BADGE_CODEGEN_FRAME_ADDRESSING must be materialized or direct\n' >&2; return 2 ;;
        esac
    fi
    if [[ ${BADGE_CODEGEN_REGISTER_RESIDENCY+x} ]]; then
        case "$BADGE_CODEGEN_REGISTER_RESIDENCY" in
            stack|local|global|global-flex|global-compact) owned_codegen_arguments+=("--register-residency=$BADGE_CODEGEN_REGISTER_RESIDENCY") ;;
            *) printf 'owned-pipeline: BADGE_CODEGEN_REGISTER_RESIDENCY must be stack, local, global, global-flex or global-compact\n' >&2; return 2 ;;
        esac
    fi
}
owned_lower_ir() {
    local tools=$1 frontend=$2 optimized=$3 assembly=$4 object=$5 runner=$6
    owned_codegen_selection || return
    local codegen=(${owned_codegen_arguments[@]+"${owned_codegen_arguments[@]}"})
    if [[ ${BADGE_OPT_PASSES+x} ]]; then
        "$runner" "$tools/badge-opt" "--passes=$BADGE_OPT_PASSES" "$frontend" -o "$optimized" || return
    else
        "$runner" "$tools/badge-opt" "$frontend" -o "$optimized" || return
    fi
    "$runner" "$tools/badge-codegen" ${codegen[@]+"${codegen[@]}"} "$optimized" -o "$assembly" || return
    "$runner" "$tools/badge-asm" "$assembly" -o "$object"
}

owned_compile_c() {
    local tools=$1 source=$2 dir=$3 object=$4 runner=$5
    shift 5
    owned_frontend_selection || return
    local frontend=(${owned_frontend_arguments[@]+"${owned_frontend_arguments[@]}"})
    "$runner" "$tools/badge-cpp" "$@" "$source" -o "$dir/preprocessed.i" || return
    "$runner" "$tools/badge-ir" ${frontend[@]+"${frontend[@]}"} "$dir/preprocessed.i" -o "$dir/frontend.ll" || return
    owned_lower_ir "$tools" "$dir/frontend.ll" "$dir/optimized.ll" "$dir/assembly.s" "$object" "$runner"
}

owned_compile_assembly() {
    local tools=$1 source=$2 preprocessed=$3 object=$4 runner=$5
    shift 5
    "$runner" "$tools/badge-cpp" --assembly "$@" "$source" -o "$preprocessed" || return
    "$runner" "$tools/badge-asm" "$preprocessed" -o "$object"
}
