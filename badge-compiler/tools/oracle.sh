#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"

if [[ ${1:-} == --help ]]; then
    printf 'Usage: bash badge-compiler/tools/oracle.sh [freestanding make targets...]\n'
    printf 'Default: all usb-wifi-udp. LLVM_BIN may select an LLVM installation.\n'
    exit 0
fi
if [[ $# == 0 ]]; then
    set -- all usb-wifi-udp
fi
vendor_assets=0
for target in "$@"; do
    case "$target" in
        all|diagnostics) ;;
        usb-wifi-udp) vendor_assets=1 ;;
        *)
            printf 'Unsupported target: %s (allowed: all, diagnostics, usb-wifi-udp)\n' "$target" >&2
            exit 2
            ;;
    esac
done

llvm_bin=${LLVM_BIN:-$(dirname "$(command -v clang)")}
export ORACLE_CLANG="$llvm_bin/clang"
export ORACLE_OPT="$llvm_bin/opt"
export ORACLE_LLC="$llvm_bin/llc"
export ORACLE_AS="$llvm_bin/llvm-as"
export ORACLE_DIS="$llvm_bin/llvm-dis"
export ORACLE_OBJCOPY="$llvm_bin/llvm-objcopy"
export ORACLE_READOBJ="$llvm_bin/llvm-readobj"
export ORACLE_OBJDUMP="$llvm_bin/llvm-objdump"
export ORACLE_NM="$llvm_bin/llvm-nm"
export ORACLE_SIZE="$llvm_bin/llvm-size"
export ORACLE_LLD="${LLD:-$(command -v ld.lld || true)}"
for tool in clang opt llc llvm-as llvm-dis llvm-objcopy llvm-readobj llvm-objdump llvm-nm llvm-size; do
    if [[ ! -x "$llvm_bin/$tool" ]]; then
        printf 'Missing LLVM tool: %s/%s; set LLVM_BIN to a complete installation.\n' "$llvm_bin" "$tool" >&2
        exit 1
    fi
done
if [[ ! -x "$ORACLE_LLD" ]]; then
    printf 'Missing LLD; set LLD to the path of ld.lld.\n' >&2
    exit 1
fi
clang_version=$("$ORACLE_CLANG" --version | sed -n '1s/.*version \([0-9][0-9.]*\).*/\1/p')
lld_version=$("$ORACLE_LLD" --version | sed -n '1s/.*LLD \([0-9][0-9.]*\).*/\1/p')
if [[ -z "$clang_version" || "$clang_version" != "$lld_version" ]]; then
    printf 'Clang/LLD versions must match: %s / %s\n' "$clang_version" "$lld_version" >&2
    exit 1
fi
export PATH="$llvm_bin:$PATH"
export ORACLE_DIR=badge-compiler/build/oracle
export ORACLE_ELF2BIN="$root/badge-compiler/build/elf2bin"
export ORACLE_CPP="$root/badge-compiler/build/badge-cpp"
export ORACLE_PARSE="$root/badge-compiler/build/badge-parse"
export ORACLE_SEMA="$root/badge-compiler/build/badge-sema"
export ORACLE_IR="$root/badge-compiler/build/badge-ir"
export ORACLE_CODEGEN="$root/badge-compiler/build/badge-codegen"
export ORACLE_BADGE_OPT="$root/badge-compiler/build/badge-opt"
export ORACLE_OBJECT="$root/badge-compiler/build/badge-object"
export ORACLE_BADGE_ASM="$root/badge-compiler/build/badge-asm"
export ORACLE_LINK="$root/badge-compiler/build/badge-link"
export ORACLE_LINK_REFERENCE="$root/badge-compiler/build/link_oracle"
mkdir -p "$ORACLE_DIR"
rm -f "$ORACLE_DIR/SUCCESS"
make -C badge-compiler all build/link_oracle
printf '%s\n' "${BADGE_CODEGEN_FRAME_ADDRESSING-$("$ORACLE_CODEGEN" --default-frame-addressing)}" \
    > "$ORACLE_DIR/codegen-frame-addressing.txt"
printf '%s\n' "${BADGE_CODEGEN_REGISTER_RESIDENCY-$("$ORACLE_CODEGEN" --default-register-residency)}" \
    > "$ORACLE_DIR/codegen-register-residency.txt"
printf '%s\n' "${BADGE_CODEGEN_SCALAR_SELECTION-$("$ORACLE_CODEGEN" --default-scalar-selection)}" \
    > "$ORACLE_DIR/codegen-scalar-selection.txt"
printf '%s\n' "${BADGE_CODEGEN_MEMORY_SELECTION-$("$ORACLE_CODEGEN" --default-memory-selection)}" \
    > "$ORACLE_DIR/codegen-memory-selection.txt"
printf 'image\tboundary\tbinary_comparison\n' > "$ORACLE_DIR/comparisons.tsv"
printf 'image\troute\tbinary_comparison\n' > "$ORACLE_DIR/objcopy-comparisons.tsv"
printf 'object\tsource\ttoken_comparison\n' > "$ORACLE_DIR/cpp-comparisons.tsv"
printf 'object\tsource\tclang_i\towned_i\tstructural_ast\n' > "$ORACLE_DIR/parser-comparisons.tsv"
printf 'object\tsource\tclang_i\towned_i\ttyped_ast\n' > "$ORACLE_DIR/sema-comparisons.tsv"
printf 'object\tsource\tclang_i\towned_i\towned_ir\n' > "$ORACLE_DIR/ir-comparisons.tsv"
printf 'object\tsource\towned_backend\towned_assembly\n' > "$ORACLE_DIR/codegen-comparisons.tsv"
printf 'object\tsource\towned_optimized_ir\towned_backend\towned_assembly\n' > "$ORACLE_DIR/opt-comparisons.tsv"
printf 'object\tsource\tobject_model\towned_object\n' > "$ORACLE_DIR/object-comparisons.tsv"
printf 'object\tsource\tassembly\towned_object\n' > "$ORACLE_DIR/asm-comparisons.tsv"
printf 'image\tunmerged_lld_bytes\tpublic_symbols\n' > "$ORACLE_DIR/linker-comparisons.tsv"
printf 'image\tllvm_mc_unmerged_lld_bytes\tpublic_symbols\n' > "$ORACLE_DIR/ir-linker-comparisons.tsv"
printf 'image\tllvm_mc_unmerged_lld_bytes\tpublic_symbols\n' > "$ORACLE_DIR/codegen-linker-comparisons.tsv"
printf 'image\tllvm_mc_unmerged_lld_bytes\tpublic_symbols\n' > "$ORACLE_DIR/opt-linker-comparisons.tsv"
{
    for tool in clang opt llc llvm-as llvm-dis llvm-objcopy llvm-readobj llvm-objdump llvm-nm llvm-size; do
        printf '\n== %s ==\n' "$llvm_bin/$tool"
        "$llvm_bin/$tool" --version
    done
    printf '\n== %s ==\n' "$ORACLE_LLD"
    "$ORACLE_LLD" --version
} > "$ORACLE_DIR/toolchain.txt"
badge-compiler/build/badge-sha256 Makefile badge-compiler/tools/oracle.sh badge-compiler/scripts/capture-cc.sh \
    badge-compiler/Makefile badge-compiler/src/elf2bin.c \
    badge-compiler/src/badge_cpp.c badge-compiler/src/badge_parse.c \
    badge-compiler/src/badge_sema.c badge-compiler/src/c_frontend.h badge-compiler/src/c_semantic.h \
    badge-compiler/src/badge_ir.c badge-compiler/src/badge_codegen.c \
    badge-compiler/src/codegen.c badge-compiler/src/codegen.h badge-compiler/src/codegen_internal.h \
    badge-compiler/src/codegen_cfg.c badge-compiler/src/codegen_frame.c badge-compiler/src/codegen_thumb.c badge-compiler/src/codegen_register.c badge-compiler/src/codegen_global.c badge-compiler/src/codegen_select.c badge-compiler/src/codegen_operands.c badge-compiler/src/thumb_immediate.h \
    badge-compiler/src/badge_opt.c badge-compiler/src/ir_core.c badge-compiler/src/ir_passes.c \
    badge-compiler/src/ir_constant_fold.c badge-compiler/src/ir_aggregate_memory.c \
    badge-compiler/src/ir_loop_invariant.c \
    badge-compiler/src/ir_cross_function.c \
    badge-compiler/src/ir_loop_admission.c badge-compiler/src/ir_loop_admission.h \
    badge-compiler/src/assembly_cost.c badge-compiler/src/assembly_cost.h \
    badge-compiler/src/ir_target_strength.c badge-compiler/src/target_cost.c badge-compiler/src/target_cost.h \
    badge-compiler/src/ir_wide_division.c badge-compiler/src/ir_pass.h badge-compiler/src/target.h \
    badge-compiler/scripts/owned-pipeline.sh \
    badge-compiler/src/badge_object.c badge-compiler/src/badge_object.h \
    badge-compiler/src/badge_asm.c badge-compiler/src/badge_thumb.h \
    badge-compiler/src/badge_link.c badge-compiler/tools/link_oracle.c \
    badge-compiler/src/c_lexer.h badge-compiler/src/host_io.h badge-compiler/src/badge_sha256.c \
    > "$ORACLE_DIR/harness.sha256"
printf '%s\n' "$@" > "$ORACLE_DIR/targets.txt"

make -B -j1 BUILD_DIR="$ORACLE_DIR" \
    RP2350_CC='bash badge-compiler/scripts/capture-cc.sh' \
    RP2350_OBJCOPY="$ORACLE_ELF2BIN" RP2350_NM="$ORACLE_NM" \
    RP2350_SIZE="$ORACLE_SIZE" "$@" 2>&1 | tee "$ORACLE_DIR/build.log"

while IFS=$'\t' read -r image boundary result; do
    if [[ "$boundary" != ours-cpp && "$boundary" != object && "$boundary" != asm && "$boundary" != linker && "$boundary" != owned-ir && "$boundary" != backend && "$boundary" != owned ]]; then continue; fi
    image_dir="$ORACLE_DIR/universe26/firmware/$image.oracle"
    "$ORACLE_DIR/host-tools/bin2uf2" "$image_dir/ours-$boundary.bin" \
        "$image_dir/$boundary.uf2" 0x10000000 0xe48bff59
done < "$ORACLE_DIR/comparisons.tsv"

if [[ $vendor_assets == 1 ]]; then
    badge-compiler/build/badge-sha256 \
        vendor/pico-sdk/lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h \
        vendor/pico-sdk/lib/cyw43-driver/firmware/wifi_nvram_43439.h \
        "$ORACLE_DIR/wifi-assets/wifi_firmware.bin" \
        "$ORACLE_DIR/wifi-assets/wifi_nvram.bin" \
        "$ORACLE_DIR/wifi-assets/wifi_clm.bin" \
        "$ORACLE_DIR/wifi-assets/wifi_assets_generated.h" \
        "$ORACLE_DIR/universe26/firmware/usb_udp_build.h" \
        > "$ORACLE_DIR/vendor-assets.sha256"
    blob_dir="$ORACLE_DIR/universe26/firmware/wifi_assets_blob.o.oracle"
    for asset in firmware nvram clm; do
        "$ORACLE_OBJCOPY" \
            "--dump-section=.rodata.wifi_$asset=$blob_dir/embedded-$asset.bin" \
            "$ORACLE_DIR/universe26/firmware/wifi_assets_blob.o" "$blob_dir/asset-check.o"
        cmp "$ORACLE_DIR/wifi-assets/wifi_$asset.bin" "$blob_dir/embedded-$asset.bin"
        "$ORACLE_OBJCOPY" \
            "--dump-section=.rodata.wifi_$asset=$blob_dir/owned-embedded-$asset.bin" \
            "$blob_dir/object.o" "$blob_dir/owned-asset-check.o"
        cmp "$ORACLE_DIR/wifi-assets/wifi_$asset.bin" "$blob_dir/owned-embedded-$asset.bin"
        "$ORACLE_OBJCOPY" \
            "--dump-section=.rodata.wifi_$asset=$blob_dir/asm-embedded-$asset.bin" \
            "$blob_dir/asm.o" "$blob_dir/asm-asset-check.o"
        cmp "$ORACLE_DIR/wifi-assets/wifi_$asset.bin" "$blob_dir/asm-embedded-$asset.bin"
    done
    rm -f "$blob_dir/asset-check.o"
    rm -f "$blob_dir/owned-asset-check.o"
    rm -f "$blob_dir/asm-asset-check.o"
fi

printf 'PASS: all selected artifacts generated, checked, and relinked.\n' \
    | tee "$ORACLE_DIR/SUCCESS"
printf 'Comparisons: %s/comparisons.tsv\n' "$ORACLE_DIR"
