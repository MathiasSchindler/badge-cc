#!/bin/bash
set -euo pipefail

if [[ $# -ne 1 || -z "$1" ]]; then
    echo "Usage: bash src/host/tools/usb_report_run.sh BOOTSEL_SERIAL" >&2
    echo "This replaces the selected badge's application with usb_report_test.uf2." >&2
    exit 2
fi
serial=$1
timeout=${USB_REPORT_TIMEOUT_SECONDS:-30}
if [[ ! "$timeout" =~ ^([1-9][0-9]?|1[01][0-9]|120)$ ]]; then
    echo "USB_REPORT_TIMEOUT_SECONDS must be an integer from 1 to 120." >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
picotool=${PICOTOOL:-"$root/build/picotool-usb/picotool"}
host="$root/build/host-tools/usb_report_host"
firmware="$root/build/universe26/uf2/usb_report_test.uf2"

if [[ ! -x "$picotool" ]]; then
    echo "USB picotool unavailable at $picotool; run make picotool-usb." >&2
    exit 1
fi
make usb-report-test
output_dir=${USB_REPORT_OUTPUT_DIR:-"$root/build/usb-report-runs"}
mkdir -p "$output_dir"
run_dir=$(mktemp -d "$output_dir/run.XXXXXXXX")
echo "Report run: $run_dir"

if ! "$picotool" info -a --ser "$serial" >"$run_dir/device.txt" 2>&1; then
    cat "$run_dir/device.txt" >&2
    echo "Put the selected badge in BOOTSEL mode and retry." >&2
    exit 1
fi
if ! grep -q 'RP2350' "$run_dir/device.txt"; then
    echo "Selected device was not identified as RP2350; refusing to flash." >&2
    cat "$run_dir/device.txt" >&2
    exit 1
fi

"$host" prepare "$run_dir/request.bin" "$run_dir/clear.bin"
"$picotool" load -v "$firmware" --ser "$serial" >"$run_dir/flash.log" 2>&1 || {
    cat "$run_dir/flash.log" >&2
    exit 1
}
"$picotool" load -v "$run_dir/clear.bin" -t bin -o 0x2007f100 --ser "$serial" >"$run_dir/clear.log" 2>&1 || {
    cat "$run_dir/clear.log" >&2
    exit 1
}
"$picotool" load -v "$run_dir/request.bin" -t bin -o 0x2007f000 --ser "$serial" >"$run_dir/request.log" 2>&1 || {
    cat "$run_dir/request.log" >&2
    exit 1
}
"$picotool" reboot --ser "$serial" >"$run_dir/reboot.log" 2>&1 || {
    cat "$run_dir/reboot.log" >&2
    exit 1
}

deadline=$((SECONDS + timeout))
while (( SECONDS < deadline )); do
    if "$picotool" save -r 0x2007f100 0x2007f140 "$run_dir/response.bin" -t bin \
        --ser "$serial" >"$run_dir/read.log" 2>&1; then
        if "$host" verify "$run_dir/request.bin" "$run_dir/response.bin" \
            >"$run_dir/result.tmp" 2>"$run_dir/verify.log"; then
            mv "$run_dir/result.tmp" "$run_dir/result.json"
            cat "$run_dir/result.json"
            echo "Fresh report verified; badge remains in BOOTSEL for the next run."
            exit 0
        fi
    fi
    sleep 0.5
done
echo "No valid fresh report within $timeout seconds; not claiming a successful round trip." >&2
cat "$run_dir/read.log" >&2
if [[ -f "$run_dir/verify.log" ]]; then cat "$run_dir/verify.log" >&2; fi
echo "Check the badge display. Raw evidence remains in $run_dir." >&2
exit 1
