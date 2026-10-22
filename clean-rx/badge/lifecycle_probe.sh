#!/usr/bin/env bash
set -u
set -o pipefail
if [[ $# -ne 4 || ! $1 =~ ^[0-9A-F]{16}$ || ! $2 =~ ^[0-9A-F]{16}$ ||
      $1 == "$2" || ! $4 =~ ^[0-5]$ ]]; then
    echo "Usage: bash clean-rx/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX CYCLES:0-5" >&2
    exit 2
fi
cd "$(dirname "$0")/../.." || exit 1
station=$1 ap=$2 prefix=$3 cycles=$4
collector=${CLEAN_RX_COLLECTOR:-build/clean-rx/badge/collector}
peer=${CLEAN_RX_PEER_COLLECTOR:-build/clean-rx/peer/collector}
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
for suffix in station.json station.bin ap.jsonl ap.bin pair.json station.stderr ap.stderr; do
    if [[ -e $prefix-$suffix ]]; then echo "Evidence prefix already exists: $prefix" >&2; exit 1; fi
done
for tool in "$collector" "$peer" "$picotool"; do
    if [[ ! -x $tool ]]; then echo "Missing matching firmware collector/tool: $tool" >&2; exit 1; fi
done
mkdir -p "$(dirname "$prefix")" || exit 1
ap_seconds=12 station_seconds=7
if (( cycles )); then ap_seconds=$((cycles * 13 + 20)); station_seconds=$((ap_seconds - 4)); fi
challenge=$(od -An -N14 -tx1 /dev/urandom | tr -d ' \n') || exit 1
if [[ ! $challenge =~ ^[0-9a-f]{28}$ ]]; then echo "Challenge generation failed" >&2; exit 1; fi
ap_pid= station_pid=
cleanup() {
    local pid
    for pid in "$station_pid" "$ap_pid"; do
        if [[ -n $pid ]] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid"
            wait "$pid" 2>/dev/null
        fi
    done
}
trap cleanup EXIT
trap 'exit 130' INT TERM
"$picotool" reboot --ser "$ap" > "$prefix-ap-boot.log" 2>&1 || exit 1
sleep 0.7
"$peer" "$ap" "$prefix-ap.bin" --lifecycle-ap "$challenge" "$ap_seconds" "$cycles" \
    > "$prefix-ap.jsonl" 2> "$prefix-ap.stderr" &
ap_pid=$!
ready=0
for ((i=0; i<200; ++i)); do
    if grep -q '"test":"b1-ap-ready"' "$prefix-ap.jsonl"; then ready=1; break; fi
    if ! kill -0 "$ap_pid" 2>/dev/null; then break; fi
    sleep 0.05
done
if (( !ready )); then echo "AP failed to become ready; retain AP logs" >&2; exit 1; fi
bssid=$(sed -n 's/.*"bssid":"\([0-9a-f:]*\)".*/\1/p' "$prefix-ap.jsonl")
if [[ ! $bssid =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]]; then echo "Invalid AP BSSID" >&2; exit 1; fi
"$picotool" reboot --ser "$station" > "$prefix-station-boot.log" 2>&1 || exit 1
sleep 0.7
station_ok=0 ap_ok=0
"$collector" "$station" "$prefix-station.bin" 1 --station-lifecycle "$bssid" "$station_seconds" \
    > "$prefix-station.json" 2> "$prefix-station.stderr" &
station_pid=$!
if wait "$station_pid"; then station_ok=1; fi
station_pid=
if wait "$ap_pid"; then ap_ok=1; fi
ap_pid=
if (( !station_ok || !ap_ok )); then echo "B1 trial failed; retain both endpoint logs" >&2; exit 1; fi
node clean-rx/badge/lifecycle_verify.cjs "$prefix" "$cycles" > "$prefix-pair.json"
