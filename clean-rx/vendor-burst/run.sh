#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 || $# -gt 7 || ! $1 =~ ^[0-9A-F]{16}$ ||
      ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ]]; then
    echo "Usage: bash clean-rx/vendor-burst/run.sh VENDOR_SERIAL OBSERVER_SERIAL PREFIX [PRIVATE_SSID_FILE [EARLY_BYTES [HANDOFF_CASE [VARIABLE_PAIR_0_TO_7]]]]" >&2
    exit 2
fi
if [[ $# == 7 && ! $7 =~ ^[0-7]$ ]]; then
    echo "VARIABLE_PAIR_0_TO_7 must be a digit 0..7" >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
build=${CLEAN_RX_REFERENCE_BUILD:-build/clean-rx/vendor-burst}
picotool=build/picotool-usb/picotool
prefix=$3
credentials=${4:-ssid-data/ssid.txt}
early=${5:-mixed}
if [[ $early != mixed && $early != 128 && $early != 256 && $early != 512 ]]; then
    echo "EARLY_BYTES must be mixed, 128, 256 or 512" >&2
    exit 2
fi
mkdir -p "$(dirname "$prefix")"
if compgen -G "$prefix*" > /dev/null; then
    echo "Use a new evidence prefix; existing trials are never overwritten" >&2
    exit 1
fi
for serial in "$1" "$2"; do
    ready=0
    for ((i=0;i<100;i++)); do
        if {
            printf 'BOOTSEL readiness attempt %u\n' "$((i+1))"
            "$picotool" info --ser "$serial"
        } >> "$prefix-ready-$serial.log" 2>&1; then
            ready=1
            break
        fi
        sleep 0.1
    done
    if [[ $ready != 1 ]]; then echo "Badge not ready in BOOTSEL: $serial" >&2; exit 1; fi
done
"$picotool" load -v "$build/vendor.uf2" --ser "$1" > "$prefix-load-vendor.log" 2>&1
"$picotool" load -v "$build/observer.uf2" --ser "$2" > "$prefix-load-observer.log" 2>&1
"$picotool" reboot --ser "$1" > "$prefix-boot-vendor.log" 2>&1
"$picotool" reboot --ser "$2" > "$prefix-boot-observer.log" 2>&1
sleep 0.5
"$build/collector" "$1" "$2" "$prefix" "$credentials" "$early" ${6+"$6"} ${7+"$7"} \
    > "$prefix.jsonl" 2> "$prefix.stderr"
