#!/usr/bin/env bash
set -euo pipefail
pipeline_tools=$(cd "$(dirname "$0")/../build" && pwd)
source "$(dirname "$0")/owned-pipeline.sh"
owned_codegen_selection
codegen_flags=(${owned_codegen_arguments[@]+"${owned_codegen_arguments[@]}"})

: "${ORACLE_CLANG:?Run through badge-compiler/tools/oracle.sh}"
args=("$@")
flags=()
output=
source=
compile=0
index=0
while [[ $index -lt ${#args[@]} ]]; do
    arg=${args[$index]}
    case "$arg" in
        -o)
            index=$((index + 1))
            output=${args[$index]}
            ;;
        -c) compile=1 ;;
        *.c|*.S) source=$arg ;;
        *) flags+=("$arg") ;;
    esac
    index=$((index + 1))
done
if [[ -z "$output" ]]; then
    printf 'Oracle compiler invocation has no output path.\n' >&2
    exit 2
fi
dir="$output.oracle"
mkdir -p "$dir"
: > "$dir/commands.sh"
run() {
    printf '%q ' "$@" >> "$dir/commands.sh"
    printf '\n' >> "$dir/commands.sh"
    "$@"
}
if [[ $compile == 1 ]]; then
    run "$ORACLE_CLANG" "$@"
else
    run "$ORACLE_CLANG" "$@" "--ld-path=$ORACLE_LLD"
fi

target_flags=(--target=arm-none-eabi -mcpu=cortex-m33 -mthumb)
variants=(preprocessed ir assembly llc opt ours-cpp object asm linker owned-ir backend owned)
if [[ $compile == 1 ]]; then
    if [[ -z "$source" ]]; then
        printf 'Oracle compile invocation has no supported source file.\n' >&2
        exit 2
    fi
    "$pipeline_tools/badge-sha256" "$source" > "$dir/source.sha256"
    run "$ORACLE_CLANG" -### "$@" 2> "$dir/driver.txt"
    run "$ORACLE_READOBJ" --file-headers --sections --symbols --relocations "$output" \
        > "$dir/object.txt"
    run "$ORACLE_OBJDUMP" -dr "$output" > "$dir/disassembly.txt"
    run "$ORACLE_OBJECT" --emit-manifest "$output" -o "$dir/object.bobj"
    run "$ORACLE_OBJECT" "$dir/object.bobj" -o "$dir/object.o"
    run "$ORACLE_OBJECT" --from-elf "$dir/object.o" -o "$dir/object-repeat.o"
    cmp "$dir/object.o" "$dir/object-repeat.o"
    run "$ORACLE_READOBJ" --file-headers --sections --symbols --relocations \
        "$dir/object.o" > "$dir/owned-object.txt"
    printf '%s\t%s\temitted\twritten\n' "$(basename "$output")" "$source" \
        >> "$ORACLE_DIR/object-comparisons.tsv"
    if [[ "$source" == *.S ]]; then
        run "$ORACLE_CLANG" "${flags[@]}" -E "$source" -o "$dir/preprocessed.s"
        run "$ORACLE_CLANG" "${target_flags[@]}" -x assembler \
            -c "$dir/preprocessed.s" -o "$dir/preprocessed.o"
        run "$ORACLE_BADGE_ASM" "$dir/preprocessed.s" -o "$dir/asm.o"
        cp "$dir/asm.o" "$dir/owned-ir.o"
        cp "$dir/preprocessed.o" "$dir/owned-ir.llvm.o"
        run "$ORACLE_LINK_REFERENCE" "$dir/owned-ir.llvm.o" "$dir/owned-ir.link-reference.o"
        cp "$dir/asm.o" "$dir/backend.o"
        cp "$dir/preprocessed.o" "$dir/backend.llvm.o"
        run "$ORACLE_LINK_REFERENCE" "$dir/backend.llvm.o" "$dir/backend.link-reference.o"
        cpp_flags=()
        for flag in "${flags[@]}"; do
            case "$flag" in
                -I*|-D*|-U*) cpp_flags+=("$flag") ;;
                --target=arm-none-eabi|-mcpu=cortex-m33|-mthumb) ;;
                *) printf 'Unsupported assembly flag in owned route: %s\n' "$flag" >&2; exit 2 ;;
            esac
        done
        owned_compile_assembly "$pipeline_tools" "$source" "$dir/owned.s" "$dir/owned.o" run ${cpp_flags[@]+"${cpp_flags[@]}"}
        run "$ORACLE_CLANG" "${target_flags[@]}" -x assembler -c "$dir/owned.s" -o "$dir/owned.llvm.o"
        run "$ORACLE_LINK_REFERENCE" "$dir/owned.llvm.o" "$dir/owned.link-reference.o"
        run "$ORACLE_LINK_REFERENCE" "$dir/asm.o" "$dir/link-reference.o"
        printf '%s\t%s\tassembled\twritten\n' "$(basename "$output")" "$source" \
            >> "$ORACLE_DIR/asm-comparisons.tsv"
        for variant in ir assembly llc opt ours-cpp; do
            cp "$dir/preprocessed.o" "$dir/$variant.o"
        done
        exit 0
    fi

    run "$ORACLE_CLANG" "${flags[@]}" -E "$source" -o "$dir/preprocessed.i"
    run "$ORACLE_CLANG" "${flags[@]}" -E -dM "$source" > "$dir/macros.txt"
    run "$ORACLE_CLANG" "${flags[@]}" -M "$source" > "$dir/dependencies.d"
    run "$ORACLE_CLANG" "${flags[@]}" -Xclang -ast-dump=json \
        -fsyntax-only "$source" > "$dir/ast.json"
    run "$ORACLE_CLANG" "${flags[@]}" -Xclang -fdump-record-layouts-complete \
        -fsyntax-only "$source" > "$dir/record-layouts.txt"
    run "$ORACLE_CLANG" "${flags[@]}" -S -emit-llvm -Xclang -emit-llvm-uselists \
        -Xclang -disable-llvm-passes \
        "$source" -o "$dir/frontend.ll"
    run "$ORACLE_CLANG" "${flags[@]}" -S -emit-llvm -Xclang -emit-llvm-uselists \
        "$source" -o "$dir/optimized.ll"
    run "$ORACLE_OPT" -passes=verify -disable-output "$dir/frontend.ll"
    run "$ORACLE_OPT" -passes=verify -disable-output "$dir/optimized.ll"
    run "$ORACLE_AS" "$dir/frontend.ll" -o "$dir/frontend.bc"
    run "$ORACLE_DIS" "$dir/frontend.bc" -o "$dir/disassembled.ll"
    run "$ORACLE_OPT" -passes=verify -S -preserve-ll-uselistorder \
        "$dir/frontend.bc" -o "$dir/roundtrip.ll"
    run "$ORACLE_AS" "$dir/roundtrip.ll" -o "$dir/roundtrip.bc"
    run "$ORACLE_DIS" "$dir/roundtrip.bc" -o "$dir/roundtrip-disassembled.ll"
    sed '/^; ModuleID = /d' "$dir/disassembled.ll" > "$dir/canonical.ll"
    sed '/^; ModuleID = /d' "$dir/roundtrip-disassembled.ll" > "$dir/roundtrip-canonical.ll"
    cmp "$dir/canonical.ll" "$dir/roundtrip-canonical.ll"
    run "$ORACLE_OPT" '-passes=default<Oz>' -S -preserve-ll-uselistorder \
        "$dir/frontend.ll" -o "$dir/opt.ll"
    run "$ORACLE_OPT" -passes=verify -disable-output "$dir/opt.ll"
    run "$ORACLE_CLANG" "${flags[@]}" -S "$source" -o "$dir/native.s"
    run "$ORACLE_BADGE_ASM" "$dir/native.s" -o "$dir/asm.o"
    run "$ORACLE_LINK_REFERENCE" "$dir/asm.o" "$dir/link-reference.o"
    printf '%s\t%s\tassembled\twritten\n' "$(basename "$output")" "$source" \
        >> "$ORACLE_DIR/asm-comparisons.tsv"

    preprocessed_flags=()
    cpp_flags=()
    skip=0
    for flag in "${flags[@]}"; do
        if [[ $skip == 1 ]]; then cpp_flags+=("$flag"); skip=0; continue; fi
        case "$flag" in
            -I|-D|-U) cpp_flags+=("$flag"); skip=1 ;;
            -I*|-D*|-U*) cpp_flags+=("$flag") ;;
            *) preprocessed_flags+=("$flag") ;;
        esac
    done
    run "$ORACLE_CLANG" "${preprocessed_flags[@]}" -x cpp-output \
        -c "$dir/preprocessed.i" -o "$dir/preprocessed.o"
    run "$ORACLE_CPP" "${cpp_flags[@]}" "$source" -o "$dir/ours.i"
    run "$ORACLE_CPP" --tokens "$dir/preprocessed.i" -o "$dir/oracle.tokens"
    run "$ORACLE_CPP" --tokens "$dir/ours.i" -o "$dir/ours.tokens"
    cmp "$dir/oracle.tokens" "$dir/ours.tokens"
    printf '%s\t%s\tidentical\n' "$(basename "$output")" "$source" \
        >> "$ORACLE_DIR/cpp-comparisons.tsv"
    for input in preprocessed ours; do
        run "$ORACLE_PARSE" "$dir/$input.i" -o "$dir/$input.ast.json"
        run "$ORACLE_PARSE" --no-locations "$dir/$input.i" -o "$dir/$input.structure.ast.json"
    done
    cmp "$dir/preprocessed.structure.ast.json" "$dir/ours.structure.ast.json"
    printf '%s\t%s\tparsed\tparsed\tidentical\n' "$(basename "$output")" "$source" \
        >> "$ORACLE_DIR/parser-comparisons.tsv"
    for input in preprocessed ours; do
        run "$ORACLE_SEMA" "$dir/$input.i" -o "$dir/$input.typed.ast.json"
        run "$ORACLE_SEMA" --no-locations "$dir/$input.i" -o "$dir/$input.typed.structure.ast.json"
    done
    cmp "$dir/preprocessed.typed.structure.ast.json" "$dir/ours.typed.structure.ast.json"
    printf '%s\t%s\tchecked\tchecked\tidentical\n' "$(basename "$output")" "$source" \
        >> "$ORACLE_DIR/sema-comparisons.tsv"
    for input in preprocessed ours; do
        run "$ORACLE_IR" "$dir/$input.i" -o "$dir/$input.frontend.ll"
        run "$ORACLE_OPT" -passes=verify -disable-output "$dir/$input.frontend.ll"
        sed '/^source_filename = /d' "$dir/$input.frontend.ll" > "$dir/$input.canonical.frontend.ll"
    done
    cmp "$dir/preprocessed.canonical.frontend.ll" "$dir/ours.canonical.frontend.ll"
    printf '%s\t%s\tverified\tverified\tidentical\n' "$(basename "$output")" "$source" \
        >> "$ORACLE_DIR/ir-comparisons.tsv"
    run "$ORACLE_OPT" '-passes=default<Oz>' -S "$dir/ours.frontend.ll" -o "$dir/owned-ir.optimized.ll"
    run "$ORACLE_OPT" -passes=verify -disable-output "$dir/owned-ir.optimized.ll"
    run "$ORACLE_CLANG" "${target_flags[@]}" -ffreestanding -fno-builtin -Oz \
        -ffunction-sections -fdata-sections -S -x ir "$dir/owned-ir.optimized.ll" -o "$dir/owned-ir.s"
    run "$ORACLE_BADGE_ASM" "$dir/owned-ir.s" -o "$dir/owned-ir.o"
    run "$ORACLE_CLANG" "${target_flags[@]}" -x assembler -c "$dir/owned-ir.s" -o "$dir/owned-ir.llvm.o"
    run "$ORACLE_LINK_REFERENCE" "$dir/owned-ir.llvm.o" "$dir/owned-ir.link-reference.o"
    run "$ORACLE_CODEGEN" ${codegen_flags[@]+"${codegen_flags[@]}"} "$dir/owned-ir.optimized.ll" -o "$dir/backend.s"
    run "$ORACLE_BADGE_ASM" "$dir/backend.s" -o "$dir/backend.o"
    run "$ORACLE_CLANG" "${target_flags[@]}" -x assembler -c "$dir/backend.s" -o "$dir/backend.llvm.o"
    run "$ORACLE_LINK_REFERENCE" "$dir/backend.llvm.o" "$dir/backend.link-reference.o"
    printf '%s\t%s\tgenerated\tassembled\n' "$(basename "$output")" "$source" >> "$ORACLE_DIR/codegen-comparisons.tsv"
    owned_lower_ir "$pipeline_tools" "$dir/ours.frontend.ll" "$dir/owned.optimized.ll" "$dir/owned.s" "$dir/owned.o" run
    run "$ORACLE_OPT" -passes=verify -disable-output "$dir/owned.optimized.ll"
    run "$ORACLE_CLANG" "${target_flags[@]}" -x assembler -c "$dir/owned.s" -o "$dir/owned.llvm.o"
    run "$ORACLE_LINK_REFERENCE" "$dir/owned.llvm.o" "$dir/owned.link-reference.o"
    printf '%s\t%s\tverified\tgenerated\tassembled\n' "$(basename "$output")" "$source" >> "$ORACLE_DIR/opt-comparisons.tsv"
    run "$ORACLE_CLANG" "${preprocessed_flags[@]}" -x cpp-output \
        -c "$dir/ours.i" -o "$dir/ours-cpp.o"
    run "$ORACLE_CLANG" "${target_flags[@]}" -Oz \
        -ffunction-sections -fdata-sections -Xclang -disable-llvm-passes \
        -x ir -c "$dir/optimized.ll" -o "$dir/ir.o"
    run "$ORACLE_CLANG" "${target_flags[@]}" -x assembler \
        -c "$dir/native.s" -o "$dir/assembly.o"
    for variant in llc opt; do
        input="$dir/optimized.ll"
        if [[ "$variant" == opt ]]; then input="$dir/opt.ll"; fi
        run "$ORACLE_LLC" -O=2 -function-sections -data-sections \
            -filetype=obj "$input" -o "$dir/$variant.o"
    done
    exit 0
fi

run "$ORACLE_READOBJ" --file-headers --program-headers --sections --symbols "$output" \
    > "$dir/executable.txt"
run "$ORACLE_OBJCOPY" -O binary "$output" "$dir/reference.bin"
run "$ORACLE_ELF2BIN" -O binary "$output" "$dir/ours-reference.bin"
cmp "$dir/reference.bin" "$dir/ours-reference.bin"
printf '%s\treference\tidentical\n' "$(basename "$output")" \
    >> "$ORACLE_DIR/objcopy-comparisons.tsv"
for variant in "${variants[@]}"; do
    link_args=()
    owned_args=(-o "$dir/$variant.elf" -Map "$dir/$variant.map")
    index=0
    while [[ $index -lt ${#args[@]} ]]; do
        arg=${args[$index]}
        case "$arg" in
            -o)
                index=$((index + 1))
                link_args+=(-o "$dir/$variant.elf")
                ;;
            *.o)
                if [[ "$variant" == linker ]]; then
                    link_args+=("$arg.oracle/link-reference.o")
                    owned_args+=("$arg.oracle/asm.o")
                else
                    link_args+=("$arg.oracle/$variant.o")
                    if [[ "$variant" == owned-ir || "$variant" == backend || "$variant" == owned ]]; then owned_args+=("$arg.oracle/$variant.o"); fi
                fi
                ;;
            -T*)
                link_args+=("$arg")
                owned_args+=(-T "${arg#-T}")
                ;;
            -Wl,-Map=*) link_args+=("-Wl,-Map=$dir/$variant.map") ;;
            *) link_args+=("$arg") ;;
        esac
        index=$((index + 1))
    done
    if [[ "$variant" == linker ]]; then
        reference_args=()
        for arg in "${link_args[@]}"; do
            case "$arg" in
                "$dir/linker.elf") reference_args+=("$dir/link-reference.elf") ;;
                "-Wl,-Map=$dir/linker.map") reference_args+=("-Wl,-Map=$dir/link-reference.map") ;;
                *) reference_args+=("$arg") ;;
            esac
        done
        run "$ORACLE_CLANG" "${reference_args[@]}" "--ld-path=$ORACLE_LLD"
        run "$ORACLE_LINK" "${owned_args[@]}"
        run "$ORACLE_OBJCOPY" -O binary "$dir/link-reference.elf" "$dir/link-reference.bin"
        run "$ORACLE_ELF2BIN" -O binary "$dir/link-reference.elf" "$dir/ours-link-reference.bin"
        cmp "$dir/link-reference.bin" "$dir/ours-link-reference.bin"
        printf '%s\tlink-reference\tidentical\n' "$(basename "$output")" >> "$ORACLE_DIR/objcopy-comparisons.tsv"
        run "$ORACLE_NM" --defined-only --extern-only --format=posix "$dir/link-reference.elf" > "$dir/link-reference.symbols"
        run "$ORACLE_NM" --defined-only --extern-only --format=posix "$dir/linker.elf" > "$dir/linker.symbols"
        LC_ALL=C sort "$dir/link-reference.symbols" > "$dir/link-reference.sorted.symbols"
        LC_ALL=C sort "$dir/linker.symbols" > "$dir/linker.sorted.symbols"
        cmp "$dir/link-reference.sorted.symbols" "$dir/linker.sorted.symbols"
        run "$ORACLE_READOBJ" --file-headers --program-headers --sections --symbols "$dir/linker.elf" > "$dir/linker-executable.txt"
    elif [[ "$variant" == owned-ir || "$variant" == backend || "$variant" == owned ]]; then
        reference_args=()
        for arg in "${link_args[@]}"; do
            case "$arg" in
                "$dir/$variant.elf") reference_args+=("$dir/$variant-reference.elf") ;;
                "-Wl,-Map=$dir/$variant.map") reference_args+=("-Wl,-Map=$dir/$variant-reference.map") ;;
                *.oracle/"$variant.o") reference_args+=("${arg%$variant.o}$variant.link-reference.o") ;;
                *) reference_args+=("$arg") ;;
            esac
        done
        run "$ORACLE_CLANG" "${reference_args[@]}" "--ld-path=$ORACLE_LLD"
        run "$ORACLE_LINK" "${owned_args[@]}"
        run "$ORACLE_OBJCOPY" -O binary "$dir/$variant-reference.elf" "$dir/$variant-reference.bin"
        run "$ORACLE_ELF2BIN" -O binary "$dir/$variant-reference.elf" "$dir/ours-$variant-reference.bin"
        cmp "$dir/$variant-reference.bin" "$dir/ours-$variant-reference.bin"
        printf '%s\t%s-reference\tidentical\n' "$(basename "$output")" "$variant" >> "$ORACLE_DIR/objcopy-comparisons.tsv"
        run "$ORACLE_NM" --defined-only --extern-only --format=posix "$dir/$variant-reference.elf" > "$dir/$variant-reference.symbols"
        run "$ORACLE_NM" --defined-only --extern-only --format=posix "$dir/$variant.elf" > "$dir/$variant.symbols"
        LC_ALL=C sort "$dir/$variant-reference.symbols" > "$dir/$variant-reference.sorted.symbols"
        LC_ALL=C sort "$dir/$variant.symbols" > "$dir/$variant.sorted.symbols"
        cmp "$dir/$variant-reference.sorted.symbols" "$dir/$variant.sorted.symbols"
    else
        run "$ORACLE_CLANG" "${link_args[@]}" "--ld-path=$ORACLE_LLD"
    fi
    run "$ORACLE_NM" -u "$dir/$variant.elf" > "$dir/$variant.undefined.txt"
    if [[ -s "$dir/$variant.undefined.txt" ]]; then
        printf 'Unresolved symbols in %s/%s.elf\n' "$dir" "$variant" >&2
        exit 1
    fi
    run "$ORACLE_OBJCOPY" -O binary "$dir/$variant.elf" "$dir/$variant.bin"
    run "$ORACLE_ELF2BIN" -O binary "$dir/$variant.elf" "$dir/ours-$variant.bin"
    cmp "$dir/$variant.bin" "$dir/ours-$variant.bin"
    if [[ "$variant" == linker ]]; then
        cmp "$dir/link-reference.bin" "$dir/linker.bin"
        printf '%s\tidentical\tidentical\n' "$(basename "$output")" >> "$ORACLE_DIR/linker-comparisons.tsv"
    fi
    if [[ "$variant" == owned-ir ]]; then
        cmp "$dir/owned-ir-reference.bin" "$dir/owned-ir.bin"
        printf '%s\tidentical\tidentical\n' "$(basename "$output")" >> "$ORACLE_DIR/ir-linker-comparisons.tsv"
    fi
    if [[ "$variant" == backend ]]; then
        cmp "$dir/backend-reference.bin" "$dir/backend.bin"
        printf '%s\tidentical\tidentical\n' "$(basename "$output")" >> "$ORACLE_DIR/codegen-linker-comparisons.tsv"
    fi
    if [[ "$variant" == owned ]]; then
        cmp "$dir/owned-reference.bin" "$dir/owned.bin"
        printf '%s\tidentical\tidentical\n' "$(basename "$output")" >> "$ORACLE_DIR/opt-linker-comparisons.tsv"
    fi
    printf '%s\t%s\tidentical\n' "$(basename "$output")" "$variant" \
        >> "$ORACLE_DIR/objcopy-comparisons.tsv"
    result=identical
    if ! cmp -s "$dir/reference.bin" "$dir/$variant.bin"; then
        result=different
    fi
    printf '%s\t%s\t%s\n' "$(basename "$output")" "$variant" "$result" \
        >> "$ORACLE_DIR/comparisons.tsv"
    case "$variant:$result" in
        preprocessed:different|assembly:different|object:different|asm:different)
            printf 'Required byte-identical round trip failed: %s via %s\n' "$output" "$variant" >&2
            exit 1
            ;;
    esac
done
