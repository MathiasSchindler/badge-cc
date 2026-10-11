#!/bin/bash
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
mkdir -p build/host-tools
test_dir=$(mktemp -d "$root/build/host-tools/usb-report-test.XXXXXXXX")
mock="$root/src/host/tests/usb_report_picotool_mock.sh"
export PICOTOOL="$test_dir/picotool"
cp "$mock" "$PICOTOOL"
chmod +x "$PICOTOOL"
export USB_REPORT_MOCK_DIR="$test_dir"
export USB_REPORT_OUTPUT_DIR="$test_dir/mock-runs"
export USB_REPORT_TIMEOUT_SECONDS=1
runner=src/host/tools/usb_report_run.sh

USB_REPORT_MOCK_MODE=valid bash "$runner" USBREPORTTESTONLY >"$test_dir/valid.log" 2>&1
grep -q '"verified":true' "$test_dir/valid.log"
grep -q 'load -v .*usb_report_test.uf2' "$test_dir/calls.txt"
grep -q 'load -v .*request.bin -t bin -o 0x2007f000 --ser USBREPORTTESTONLY' "$test_dir/calls.txt"
grep -q '^reboot --ser USBREPORTTESTONLY$' "$test_dir/calls.txt"
grep -q '^save -r 0x2007f100 0x2007f140' "$test_dir/calls.txt"

if USB_REPORT_MOCK_MODE=stale bash "$runner" USBREPORTTESTONLY >"$test_dir/stale.log" 2>&1; then
    echo "ERROR: stale response accepted" >&2
    exit 1
fi
grep -q 'STALE OR INVALID RUN' "$test_dir/stale.log"
if grep -q '"verified":true' "$test_dir/stale.log"; then exit 1; fi

: >"$test_dir/calls.txt"
if USB_REPORT_MOCK_MODE=absent bash "$runner" USBREPORTTESTONLY >"$test_dir/absent.log" 2>&1; then
    echo "ERROR: missing device accepted" >&2
    exit 1
fi
if grep -q '^load' "$test_dir/calls.txt"; then
    echo "ERROR: attempted load after failed preflight" >&2
    exit 1
fi
if bash "$runner" >"$test_dir/usage.log" 2>&1; then
    echo "ERROR: allowed unspecified device selection" >&2
    exit 1
fi

echo "usb_report_run: selected-device preflight, flash/request/reboot/read, fresh report and stale rejection PASS"
echo "Mock evidence: $test_dir"
