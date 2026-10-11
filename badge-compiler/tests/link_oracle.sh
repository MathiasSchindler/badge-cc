#!/usr/bin/env bash
set -euo pipefail
: "${LLVM_CLANG:=clang}"
: "${LLVM_LLD:=ld.lld}"
: "${LLVM_OBJCOPY:=llvm-objcopy}"
: "${LLVM_NM:=llvm-nm}"

for fixture in link-owned-fixture link-prel-fixture link-weak-fixture link-weak-addend-fixture object_oracle_fixture; do
    if [[ "$fixture" == object_oracle_fixture ]]; then
        "$LLVM_CLANG" --target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
            -c tests/fixtures/object_oracle_fixture.S -o "build/$fixture.o"
    fi
    ./build/badge-link -T tests/fixtures/link_oracle_fixture.ld --no-gc-sections \
        -Map "build/$fixture-link.map" -o "build/$fixture-link.elf" "build/$fixture.o"
    "$LLVM_LLD" -T tests/fixtures/link_oracle_fixture.ld --no-gc-sections \
        -o "build/$fixture-reference.elf" "build/$fixture.o"
    "$LLVM_OBJCOPY" -O binary "build/$fixture-reference.elf" "build/$fixture-reference.bin"
    ./build/elf2bin -O binary "build/$fixture-link.elf" "build/$fixture-link.bin"
    cmp "build/$fixture-reference.bin" "build/$fixture-link.bin"
    "$LLVM_NM" -g --defined-only -P "build/$fixture-reference.elf" | LC_ALL=C sort > "build/$fixture-reference.symbols"
    "$LLVM_NM" -g --defined-only -P "build/$fixture-link.elf" | LC_ALL=C sort > "build/$fixture-link.symbols"
    cmp "build/$fixture-reference.symbols" "build/$fixture-link.symbols"
done
printf 'badge-link: C API/LLVM fixtures match LLD flash bytes and public symbols, including all five fixups and RAM/common layouts PASS\n'
