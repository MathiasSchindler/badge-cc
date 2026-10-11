#!/bin/bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 || ! "$1" =~ ^[0-9A-Fa-f]{16}$ ||
      ( $# -eq 2 && "$2" != wifi && "$2" != wifi-pio && "$2" != wifi-fw ) ]]; then
    echo "Usage: bash src/host/tools/usb_live_run.sh BOOTSEL_SERIAL [wifi|wifi-pio|wifi-fw]" >&2
    exit 2
fi
serial=$1
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
picotool=${PICOTOOL:-"$root/build/picotool-usb/picotool"}
image=usb_live_report
collector=usb_live_host
count=3
if [[ ${2:-} == wifi-fw ]]; then
    make usb-wifi-firmware
    image=usb_wifi_firmware
    collector=usb_wifi_firmware_host
    count=1
else
    make usb-live-report
fi
mkdir -p build/usb-live-runs
run_dir=$(mktemp -d "$root/build/usb-live-runs/run.XXXXXXXX")
echo "Live USB run: $run_dir"
"$picotool" info -d --ser "$serial" >"$run_dir/device.txt" 2>&1 || {
    cat "$run_dir/device.txt" >&2
    exit 1
}
if ! grep -q 'RP2350' "$run_dir/device.txt"; then
    echo "Selected device is not RP2350; refusing to flash." >&2
    exit 1
fi
"$picotool" load -v -x "build/universe26/uf2/$image.uf2" --ser "$serial" \
    >"$run_dir/flash.log" 2>&1 || {
    cat "$run_dir/flash.log" >&2
    exit 1
}
result=0
mode=()
if [[ $# -eq 2 ]]; then mode=("$2"); fi
"build/host-tools/$collector" "$serial" "$count" "${mode[@]}" >"$run_dir/reports.jsonl" 2>"$run_dir/host.log" || result=$?
cat "$run_dir/reports.jsonl"
cat "$run_dir/host.log" >&2
recovered=0
for (( attempt=0; attempt<30; attempt++ )); do
    if "$picotool" info -d --ser "$serial" >"$run_dir/recovery.log" 2>&1; then
        recovered=1
        break
    fi
    sleep 0.5
done
if (( ! recovered )); then
    echo "BOOTSEL recovery not observed. Use the physical Boot button." >&2
    cat "$run_dir/recovery.log" >&2
    exit 1
fi
echo "Selected badge is back in BOOTSEL."
if (( result != 0 )); then
    echo "Live USB report validation failed; see $run_dir." >&2
    exit "$result"
fi
echo "$count fresh report(s) and BOOTSEL recovery verified."
if [[ $# -eq 2 ]]; then
    echo "Radio success is reported separately by probe_ok or firmware_ok in each JSON record."
fi
