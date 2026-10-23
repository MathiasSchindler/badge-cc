#!/usr/bin/env bash
set -u
set -o pipefail
if [[ $# -lt 3 || $# -gt 4 || ! $1 =~ ^[0-9A-F]{16}$ ||
      ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" || ! ${4:-3} =~ ^([1-9]|10)$ ]]; then
    echo "Usage: bash clean-rx/badge/qualification_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX [ROUNDS:1-10]" >&2
    exit 2
fi
cd "$(dirname "$0")/../.." || exit 1
station=$1 ap=$2 prefix=$3 rounds=${4:-3}
if [[ -e $prefix-matrix.json ]]; then echo "Matrix already exists: $prefix" >&2; exit 1; fi
for channel in 1 6 11; do
    for ((round=1;round<=rounds;++round)); do
        for suffix in station.json station.bin ap.jsonl ap.bin pair.json station.stderr ap.stderr ap-boot.log station-boot.log; do
            if [[ -e $prefix-ch$channel-$round-$suffix ]]; then
                echo "Trial evidence already exists" >&2; exit 1
            fi
        done
    done
done
child=
cleanup() {
    if [[ -n $child ]] && kill -0 "$child" 2>/dev/null; then
        kill "$child"; wait "$child" 2>/dev/null
    fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM
failed=0
for channel in 1 6 11; do
    for ((round=1;round<=rounds;++round)); do
        bash clean-rx/badge/lifecycle_probe.sh "$station" "$ap" \
            "$prefix-ch$channel-$round" 0 --qualification "$channel" &
        child=$!
        if ! wait "$child"; then failed=1; fi
        child=
    done
done
node clean-rx/badge/qualification_verify.cjs "$prefix" "$rounds" "$station" "$ap" \
    > "$prefix-matrix.json" || exit 1
if (( failed )); then echo "Failed trial retained; matrix is not qualified" >&2; exit 1; fi
