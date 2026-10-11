#!/usr/bin/env bash
set -euo pipefail
tools=$(cd "$(dirname "$0")/../build" && pwd)
source "$(dirname "$0")/owned-pipeline.sh"
source=
output=
compile=0
cpp=()
objects=()
link=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -c) compile=1 ;;
        -o) shift; [[ $# -gt 0 && -z "$output" ]] || { printf 'owned-cc: expected one output path\n' >&2; exit 2; }; output=$1 ;;
        -I|-D|-U) option=$1; shift; [[ $# -gt 0 ]] || { printf 'owned-cc: missing preprocessing argument\n' >&2; exit 2; }; cpp+=("$option" "$1") ;;
        -I*|-D*|-U*) cpp+=("$1") ;;
        *.c|*.S) [[ -z "$source" ]] || { printf 'owned-cc: multiple sources\n' >&2; exit 2; }; source=$1 ;;
        *.o) objects+=("$1") ;;
        -T) shift; [[ $# -gt 0 ]] || { printf 'owned-cc: missing linker script\n' >&2; exit 2; }; link+=(-T "$1") ;;
        -T*) link+=(-T "${1#-T}") ;;
        -Wl,-Map=*) link+=(-Map "${1#-Wl,-Map=}") ;;
        -Wl,--gc-sections|-Wl,--fatal-warnings|-nostdlib|-fuse-ld=lld) ;;
        -std=c11|-Wall|-Wextra|-Werror|-Oz|-ffreestanding|-fno-builtin|-fno-stack-protector|\
        -fno-unwind-tables|-fno-asynchronous-unwind-tables|-ffunction-sections|-fdata-sections|\
        --target=arm-none-eabi|-mcpu=cortex-m33|-mthumb) ;;
        *) printf 'owned-cc: unsupported argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done
[[ -n "$output" ]] || { printf 'owned-cc: missing output path\n' >&2; exit 2; }
if [[ $compile == 1 ]]; then
    [[ -n "$source" && ${#objects[@]} == 0 && ${#link[@]} == 0 ]] || { printf 'owned-cc: expected a single compilation input\n' >&2; exit 2; }
    [[ ! "$source" -ef "$output" ]] || { printf 'owned-cc: output aliases source\n' >&2; exit 2; }
    dir="$output.owned"
    mkdir -p "$dir"
    "$tools/badge-sha256" "$source" > "$dir/source.sha256"
    if [[ "$source" == *.S ]]; then
        owned_compile_assembly "$tools" "$source" "$dir/preprocessed.s" "$output" owned_exec ${cpp[@]+"${cpp[@]}"}
    else
        owned_compile_c "$tools" "$source" "$dir" "$output" owned_exec ${cpp[@]+"${cpp[@]}"}
    fi
else
    [[ -z "$source" && ${#objects[@]} -gt 0 ]] || { printf 'owned-cc: expected link objects\n' >&2; exit 2; }
    "$tools/badge-link" ${link[@]+"${link[@]}"} "${objects[@]}" -o "$output"
    undefined=$("$tools/badge-elf-info" -u "$output")
    [[ -z "$undefined" ]] || { printf 'owned-cc: unresolved symbols:\n%s\n' "$undefined" >&2; exit 1; }
fi
