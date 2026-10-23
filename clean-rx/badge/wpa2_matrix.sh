#!/usr/bin/env bash
set -u
set -o pipefail
if [[ $# -ne 3 || ! $1 =~ ^[0-9A-F]{16}$ || ! $2 =~ ^[0-9A-F]{16}$ ||
      $1 == "$2" || -z $3 ]]; then
    echo "Usage: bash clean-rx/badge/wpa2_matrix.sh AP_SERIAL STATION_SERIAL NEW_PREFIX" >&2
    exit 2
fi
cd "$(dirname "$0")/../.." || exit 1
ap=$1; station=$2; prefix=$3
for file in "$prefix"-*; do
    if [[ -e $file || -L $file ]]; then echo "Evidence prefix already exists: $file" >&2; exit 1; fi
done
child=
cleanup() {
    if [[ -n $child ]]; then kill "$child" 2>/dev/null || true; wait "$child" || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
channels=(1 6 11)
for ((i=1;i<=19;i++)); do
    if [[ $i -gt 16 ]]; then
        channel=${channels[$((i-17))]}
        mode=(--wrong-password)
    else
        if [[ $i -le 10 ]]; then channel=1
        elif [[ $i -le 13 ]]; then channel=6
        else channel=11
        fi
        mode=()
        if [[ $((i%2)) -eq 0 ]]; then mode=(--raw-psk); fi
    fi
    bash clean-rx/badge/wpa2_trial.sh "$ap" "$station" "$prefix-$i" \
        --peer-ap "$channel" "${mode[@]}" &
    child=$!
    if ! wait "$child"; then echo "E1 trial $i failed; retaining evidence and continuing matrix" >&2; fi
    child=
done
node clean-rx/badge/wpa2_verify.cjs --matrix "$prefix" "$station" "$ap" > "$prefix-matrix.json"
