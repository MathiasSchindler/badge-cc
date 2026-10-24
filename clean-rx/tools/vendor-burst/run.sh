#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 || $# -gt 7 || ! $1 =~ ^[0-9A-F]{16}$ ||
      ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ]]; then
    echo "Usage: bash clean-rx/tools/vendor-burst/run.sh VENDOR_SERIAL OBSERVER_SERIAL PREFIX [PRIVATE_SSID_FILE [EARLY_BYTES [HANDOFF_CASE [VARIABLE_PAIR_0_TO_7]]]]" >&2
    exit 2
fi
if [[ $# == 7 && ! $7 =~ ^[0-7]$ ]]; then
    echo "VARIABLE_PAIR_0_TO_7 must be a digit 0..7" >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
build=${CLEAN_RX_REFERENCE_BUILD:-build/clean-rx/vendor-burst}
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
source clean-rx/tools/usb-run.sh
trap 'usb_stop' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
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
usb_control run 15 "$picotool" info --ser "$1" >> "$prefix-ready-$1.log" 2>&1
usb_control run 15 "$picotool" info --ser "$2" >> "$prefix-ready-$2.log" 2>&1
usb_control run 300 "$picotool" load -v "$build/vendor.uf2" --ser "$1" > "$prefix-load-vendor.log" 2>&1
usb_control run 300 "$picotool" load -v "$build/observer.uf2" --ser "$2" > "$prefix-load-observer.log" 2>&1
usb_boot "$1" "$prefix-boot-vendor.log"
usb_boot "$2" "$prefix-boot-observer.log"
usb_pair_ready "$1" "$2" "$prefix-ready-$1.log" "$prefix-ready-$2.log"
usb_control run 70 "$build/collector" "$1" "$2" "$prefix" "$credentials" "$early" ${6+"$6"} ${7+"$7"} \
    > "$prefix.jsonl" 2> "$prefix.stderr"
