#!/usr/bin/env bash
set -euo pipefail
clang=${LLVM_CLANG:-clang}
lld=${LLVM_LLD:-ld.lld}
objcopy=${LLVM_OBJCOPY:-llvm-objcopy}

"$clang" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -c \
    tests/fixtures/object_oracle_fixture.S -o build/object-reference.o
./build/badge-object --emit-manifest build/object-reference.o -o build/object-reference.bobj
./build/badge-object build/object-reference.bobj -o build/object-rebuilt.o
./build/badge-object --from-elf build/object-reference.o -o build/object-direct.o
cmp build/object-rebuilt.o build/object-direct.o
./build/badge-object --from-elf build/object-rebuilt.o -o build/object-rebuilt-twice.o
cmp build/object-rebuilt.o build/object-rebuilt-twice.o
for variant in reference rebuilt direct; do
    "$lld" -m armelf -e entry -Ttext=0x10000000 -Tdata=0x10001000 \
        build/object-"$variant".o -o build/object-"$variant".elf
    "$objcopy" -O binary build/object-"$variant".elf build/object-"$variant".bin
done
"$lld" -m armelf -e entry -Ttext=0x10000000 -Tdata=0x20000000 \
    build/object-owned-fixture.o -o build/object-owned-fixture.elf
cmp build/object-reference.bin build/object-rebuilt.bin
cmp build/object-reference.bin build/object-direct.bin
printf 'badge-object: handwritten C API fixture, LLVM assembly fixture, deterministic rebuild and LLD relocation/layout comparisons PASS\n'
