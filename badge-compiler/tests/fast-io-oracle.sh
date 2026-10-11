#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
clang=${LLVM_CLANG:-clang}
lld=${LLVM_LLD:-ld.lld}
objcopy=${LLVM_OBJCOPY:-llvm-objcopy}
qemu=${QEMU_ARM:-qemu-system-arm}
dir=build/fast-io-oracle
mkdir -p "$dir"
target=(--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -ffreestanding -fno-builtin -I../src/shared)
helpers=(bytes wifi_pio_fifo display_pixels)
objects=()
for helper in "${helpers[@]}"; do
    ./build/badge-asm "../src/arch/rp2350/$helper.S" -o "$dir/$helper.o"
    "$clang" "${target[@]}" -c "../src/arch/rp2350/$helper.S" -o "$dir/$helper-reference.o"
    objects+=("$dir/$helper.o")
done
for section in badger_bytes_copy badger_bytes_zero badger_bytes_equal badger_pixels_fill \
               badger_bytes_load_u32 badger_bytes_store_u32 \
               wifi_pio_write_words wifi_pio_read_words wifi_pio_prepare_hw wifi_pio_finish_hw \
               badger_display_write_pixels_hw; do
    case "$section" in
        wifi_pio_*) helper=wifi_pio_fifo ;;
        badger_display_*) helper=display_pixels ;;
        *) helper=bytes ;;
    esac
    "$objcopy" -O binary --only-section=".text.$section" "$dir/$helper.o" "$dir/$section.bin"
    "$objcopy" -O binary --only-section=".text.$section" "$dir/$helper-reference.o" "$dir/$section-reference.bin"
    cmp "$dir/$section.bin" "$dir/$section-reference.bin"
done
./build/badge-asm tests/fixtures/codegen_start.S -o "$dir/start.o"
for fixture in bytes fast_io; do
    bash scripts/owned-cc.sh "${target[@]}" -DBADGER_FAST_BYTES -c "tests/fixtures/${fixture}_fixture.c" -o "$dir/$fixture-owned.o"
    "$clang" "${target[@]}" -DBADGER_FAST_BYTES -std=c11 -Oz \
        -c "tests/fixtures/${fixture}_fixture.c" -o "$dir/$fixture-reference.o"
    for caller in owned reference; do
        "$lld" -T tests/fixtures/codegen_fixture.ld "$dir/start.o" "$dir/$fixture-$caller.o" "${objects[@]}" \
            -o "$dir/$fixture-$caller.elf"
        python3 - "$qemu" "$dir/$fixture-$caller.elf" <<'PY'
import subprocess, sys
result = subprocess.run(
    [sys.argv[1], "-M", "mps2-an505", "-nographic", "-monitor", "none", "-serial", "none",
     "-semihosting-config", "enable=on,target=native", "-kernel", sys.argv[2]],
    timeout=15, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
if result.returncode:
    sys.stderr.buffer.write(result.stdout + result.stderr)
    raise SystemExit(f"Target execution failed: {sys.argv[2]} (exit {result.returncode})")
PY
    done
done
printf 'Owned fast I/O: exact LLVM assembly encodings; owned/LLVM callers, boundaries, endian order, deadlines, wrap and stalled-FIFO bounds PASS\n'
