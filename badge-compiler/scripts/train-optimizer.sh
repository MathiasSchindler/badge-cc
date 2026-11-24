#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 || ! -d "$1" ]]; then
    printf 'usage: train-optimizer.sh TRAINING_DIR LLVM_PROFDATA\n' >&2
    exit 2
fi
training=$1
profdata=$2
inputs=("$training"/*.o.owned/frontend.ll)
if [[ ! -f "${inputs[0]}" ]]; then
    printf 'train-optimizer: no .o.owned/frontend.ll inputs in %s\n' "$training" >&2
    exit 2
fi

scratch=$(mktemp -d build/optimizer-profile.XXXXXX)
profiles=()
cleanup() {
    rm -f "$scratch"/optimized.ll "$scratch"/optimizer.profdata ${profiles[@]+"${profiles[@]}"}
    rmdir "$scratch"
}
trap cleanup EXIT
for input in "${inputs[@]}"; do
    profile="$scratch/${#profiles[@]}.profraw"
    profiles+=("$profile")
    LLVM_PROFILE_FILE="$profile" ./build/badge-opt-profile-train "$input" -o "$scratch/optimized.ll"
done
"$profdata" merge -o "$scratch/optimizer.profdata" "${profiles[@]}"
mv "$scratch/optimizer.profdata" build/optimizer.profdata
