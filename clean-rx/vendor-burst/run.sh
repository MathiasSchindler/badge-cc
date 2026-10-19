#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 || $# -gt 4 || ! $1 =~ ^[0-9A-F]{16}$ ||
      ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ]]; then
    echo "Usage: bash clean-rx/vendor-burst/run.sh VENDOR_SERIAL OBSERVER_SERIAL PREFIX [PRIVATE_SSID_FILE]" >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
build=build/clean-rx/vendor-burst
picotool=build/picotool-usb/picotool
prefix=$3
credentials=${4:-ssid-data/ssid.txt}
mkdir -p "$(dirname "$prefix")"
if compgen -G "$prefix*" > /dev/null; then
    echo "Use a new evidence prefix; existing trials are never overwritten" >&2
    exit 1
fi
for serial in "$1" "$2"; do
    ready=0
    for ((i=0;i<100;i++)); do
        if "$picotool" info --ser "$serial" > "$prefix-ready-$serial.log" 2>&1; then
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
"$build/collector" "$1" "$2" "$prefix" "$credentials" \
    > "$prefix.jsonl" 2> "$prefix.stderr"
