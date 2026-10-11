#!/bin/bash
set -euo pipefail
prebuilt=0
if [[ ${1:-} == --prebuilt ]]; then
    if [[ $# != 5 || ! -f "$4" || ! -x "$5" ]]; then
        echo "Usage: bash src/host/tools/usb_wifi_pair_run.sh --prebuilt AP_SERIAL STATION_SERIAL IMAGE_UF2 COLLECTOR" >&2
        exit 2
    fi
    image_path="$(cd "$(dirname "$4")" && pwd)/$(basename "$4")"
    collector_path="$(cd "$(dirname "$5")" && pwd)/$(basename "$5")"
    prebuilt=1
    set -- "$2" "$3" udp
fi
if [[ $# -lt 2 || $# -gt 3 || ! "$1" =~ ^[0-9A-Fa-f]{16}$ || ! "$2" =~ ^[0-9A-Fa-f]{16}$ ||
      ( $# -eq 3 && "$3" != udp ) ]]; then
    echo "Usage: bash src/host/tools/usb_wifi_pair_run.sh AP_SERIAL STATION_SERIAL [udp]" >&2
    exit 2
fi
ap=$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')
station=$(printf '%s' "$2" | tr '[:lower:]' '[:upper:]')
if [[ "$ap" == "$station" ]]; then
    echo "AP and station serials must be different." >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
picotool=${PICOTOOL:-"$root/build/picotool-usb/picotool"}
image=usb_wifi_link
collector=usb_wifi_link_host
recovery_attempts=70
if [[ ${3:-} == udp ]]; then
    if (( ! prebuilt )); then make usb-wifi-udp; fi
    image=usb_wifi_udp
    collector=usb_wifi_udp_host
    recovery_attempts=150
else
    make usb-wifi-link
fi
if (( ! prebuilt )); then
    image_path="build/universe26/uf2/$image.uf2"
    collector_path="build/host-tools/$collector"
fi
mkdir -p build/usb-live-runs
run_dir=$(mktemp -d "$root/build/usb-live-runs/pair.XXXXXXXX")
echo "Paired Wi-Fi run: $run_dir"
shasum -a 256 "$image_path" "$collector_path" >"$run_dir/inputs.sha256"
printf 'AP=%s\nSTATION=%s\n' "$ap" "$station" >"$run_dir/roles.txt"
for serial in "$ap" "$station"; do
    "$picotool" info -d --ser "$serial" >"$run_dir/$serial.device.txt" 2>&1 || {
        cat "$run_dir/$serial.device.txt" >&2; exit 1;
    }
    if ! grep -q RP2350 "$run_dir/$serial.device.txt"; then
        echo "Selected device is not RP2350: $serial" >&2; exit 1
    fi
done
result=0
for serial in "$ap" "$station"; do
    if ! "$picotool" load -v "$image_path" --ser "$serial" \
        >"$run_dir/$serial.flash.log" 2>&1; then
        cat "$run_dir/$serial.flash.log" >&2
        result=1
        break
    fi
done
if (( result == 0 )); then
    for serial in "$ap" "$station"; do
        if ! "$picotool" reboot --ser "$serial" >"$run_dir/$serial.start.log" 2>&1; then
            cat "$run_dir/$serial.start.log" >&2
            result=1
            break
        fi
    done
fi
if (( result == 0 )); then
    "$collector_path" "$ap" "$station" >"$run_dir/reports.jsonl" 2>"$run_dir/host.log" || result=$?
    cat "$run_dir/reports.jsonl"
    cat "$run_dir/host.log" >&2
fi
for serial in "$ap" "$station"; do
    recovered=0
    for (( attempt=0; attempt<recovery_attempts; attempt++ )); do
        if "$picotool" info -d --ser "$serial" >"$run_dir/$serial.recovery.log" 2>&1; then
            recovered=1
            break
        fi
        sleep 0.5
    done
    if (( ! recovered )); then
        echo "BOOTSEL recovery missing for $serial; use the physical Boot button." >&2
        result=1
    else
        echo "$serial is back in BOOTSEL."
    fi
done
if (( result != 0 )); then
    echo "Paired Wi-Fi experiment did not pass; see $run_dir." >&2
    exit "$result"
fi
echo "Matching association reports from both badges and BOOTSEL recovery verified."
if [[ ${3:-} == udp ]]; then
    echo "Bidirectional UDP echo completed; measured loss and RTT samples are in the JSON results."
fi
