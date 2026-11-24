#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
dir=build/c-compat-oracle
mkdir -p "$dir"
./build/badge-cpp tests/fixtures/c_compat_fixture.c -o "$dir/probe.i"
./build/badge-ir --float-lowering=base-pcs "$dir/probe.i" -o "$dir/probe.ll"
"${LLVM_OPT:-opt}" -passes=verify -disable-output "$dir/probe.ll"
./build/badge-opt "$dir/probe.ll" -o "$dir/probe.opt.ll"
./build/badge-codegen "$dir/probe.opt.ll" -o "$dir/probe.s"
./build/badge-asm "$dir/probe.s" -o "$dir/probe.o"
./build/badge-asm tests/fixtures/codegen_start.S -o "$dir/start.o"
"${ARM_GCC:-arm-none-eabi-gcc}" -mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -Os \
    -ffreestanding -fno-builtin -c tests/fixtures/c_compat_oracle.c -o "$dir/oracle.o"
"${LLVM_LLD:-ld.lld}" --gc-sections -T tests/fixtures/codegen_fixture.ld \
    "$dir/start.o" "$dir/probe.o" "$dir/oracle.o" -o "$dir/check.elf"
timeout 30 "${QEMU_ARM:-qemu-system-arm}" -M mps2-an505 -nographic -monitor none -serial none \
    -semihosting-config enable=on,target=native -kernel "$dir/check.elf"
echo 'Mixed GCC/owned C compatibility oracle passed'
./build/badge-asm tests/fixtures/rp2350_inline_encoding.S -o "$dir/encoding-owned.o"
"${ARM_GCC:-arm-none-eabi-gcc}" -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 \
    -c tests/fixtures/rp2350_inline_encoding.S -o "$dir/encoding-gcc.o"
for role in owned gcc; do
    "${ARM_OBJCOPY:-arm-none-eabi-objcopy}" -O binary -j .text "$dir/encoding-$role.o" "$dir/encoding-$role.bin"
done
cmp "$dir/encoding-owned.bin" "$dir/encoding-gcc.bin"
echo 'RP2350 inline instruction bytes match GNU as'
