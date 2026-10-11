#!/usr/bin/env bash
set -euo pipefail
clang=${LLVM_CLANG:-clang}
lld=${LLVM_LLD:-ld.lld}
objcopy=${LLVM_OBJCOPY:-llvm-objcopy}
for fixture in asm_oracle_fixture object_oracle_fixture frame_addressing_oracle_fixture system_oracle_fixture ergonomics_oracle_fixture synchronization_oracle_fixture; do
    "$clang" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -x assembler \
        -c "tests/fixtures/$fixture.S" -o "build/$fixture-llvm.o"
    ./build/badge-asm "tests/fixtures/$fixture.S" -o "build/$fixture-ours.o"
    for variant in llvm ours; do
        "$lld" -m armelf -e entry -Ttext=0x10000000 -Tdata=0x10001000 \
            "build/$fixture-$variant.o" -o "build/$fixture-$variant.elf"
        "$objcopy" -O binary "build/$fixture-$variant.elf" "build/$fixture-$variant.bin"
    done
    cmp "build/$fixture-llvm.bin" "build/$fixture-ours.bin"
done
printf 'badge-asm: instruction/IT/metadata/expression/fixup fixtures match LLVM after LLD linking PASS\n'
