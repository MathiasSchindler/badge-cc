#!/usr/bin/env bash
set -u
set -o pipefail
network=0
qualification=0 channel=1
if [[ $# -eq 5 && $5 == --network ]]; then network=1; fi
if [[ $# -eq 6 && $5 == --qualification && $4 == 0 && $6 =~ ^(1|6|11)$ ]]; then
    qualification=1 channel=$6
fi
if [[ ( $# -ne 4 && $network -ne 1 && $qualification -ne 1 ) || ! $1 =~ ^[0-9A-F]{16}$ ||
      ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ||
      ( $network -eq 0 && ! $4 =~ ^[0-5]$ ) ||
      ( $network -eq 1 && ! $4 =~ ^[0-9]{2,4}$ ) ]]; then
    echo "Usage: bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX CYCLES:0-5" >&2
    echo "B2: bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX SECONDS:10-3900 --network" >&2
    echo "B3: bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX 0 --qualification CHANNEL:1,6,11" >&2
    exit 2
fi
cd "$(dirname "$0")/../../.." || exit 1
station=$1 ap=$2 prefix=$3 cycles=$4
if (( network )) && (( 10#$4 < 10 || 10#$4 > 3900 )); then
    echo "B2 duration must be 10..3900 seconds" >&2; exit 2
fi
collector=${CLEAN_RX_COLLECTOR:-build/clean-rx/badge/collector}
peer=${CLEAN_RX_PEER_COLLECTOR:-build/clean-rx/peer/collector}
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
source clean-rx/tools/usb-run.sh
for suffix in station.json station.bin ap.jsonl ap.bin pair.json station.stderr ap.stderr ap-boot.log station-boot.log; do
    if [[ -e $prefix-$suffix ]]; then echo "Evidence prefix already exists: $prefix" >&2; exit 1; fi
done
for tool in "$collector" "$peer" "$picotool"; do
    if [[ ! -x $tool ]]; then echo "Missing matching firmware collector/tool: $tool" >&2; exit 1; fi
done
mkdir -p "$(dirname "$prefix")" || exit 1
ap_seconds=12 station_seconds=7
if (( network )); then ap_seconds=$((10#$4)); station_seconds=$ap_seconds
elif (( qualification )); then ap_seconds=16 station_seconds=10
elif (( cycles )); then ap_seconds=$((cycles * 13 + 20)); station_seconds=$((ap_seconds - 4)); fi
challenge=$(od -An -N14 -tx1 /dev/urandom | tr -d ' \n') || exit 1
if [[ ! $challenge =~ ^[0-9a-f]{28}$ ]]; then echo "Challenge generation failed" >&2; exit 1; fi
ap_pid= station_pid=
cleanup() {
    usb_stop "$station_pid" "$ap_pid"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
usb_boot "$station" "$prefix-station-boot.log" &&
    usb_boot "$ap" "$prefix-ap-boot.log" || exit 1
usb_pair_ready "$station" "$ap" "$prefix-ready-$station.log" "$prefix-ready-$ap.log" || exit 1
ap_args=(--lifecycle-ap "$challenge" "$ap_seconds" "$cycles")
station_flag=--station-lifecycle ready_test=b1-ap-ready
if (( network )); then
    ap_args=(--network-ap "$challenge" "$ap_seconds")
    station_flag=--station-network ready_test=b2-ap-ready
elif (( qualification )); then
    ap_args=(--qualification-ap "$challenge" "$ap_seconds" "$channel")
fi
"${usb_command[@]}" "$((ap_seconds + 50))" "$peer" "$ap" "$prefix-ap.bin" "${ap_args[@]}" \
    > "$prefix-ap.jsonl" 2> "$prefix-ap.stderr" &
ap_pid=$!
ready=0
for ((i=0; i<200; ++i)); do
    if grep -q "\"test\":\"$ready_test\"" "$prefix-ap.jsonl"; then ready=1; break; fi
    if ! kill -0 "$ap_pid" 2>/dev/null; then break; fi
    sleep 0.05
done
if (( !ready )); then echo "AP failed to become ready; retain AP logs" >&2; exit 1; fi
bssid=$(sed -n 's/.*"bssid":"\([0-9a-f:]*\)".*/\1/p' "$prefix-ap.jsonl")
if [[ ! $bssid =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]]; then echo "Invalid AP BSSID" >&2; exit 1; fi
if ! kill -0 "$ap_pid" 2>/dev/null; then echo "AP exited before station; retain AP logs" >&2; exit 1; fi
station_ok=0 ap_ok=0
"${usb_command[@]}" "$((station_seconds + 25))" "$collector" "$station" "$prefix-station.bin" "$channel" "$station_flag" "$bssid" "$station_seconds" \
    > "$prefix-station.json" 2> "$prefix-station.stderr" &
station_pid=$!
usb_wait_pair "$station_pid" "$ap_pid" || exit 1
station_ok=1 ap_ok=1 station_pid= ap_pid=
if (( !station_ok || !ap_ok )); then echo "Station trial failed; retain both endpoint logs" >&2; exit 1; fi
if (( network )); then
    node clean-rx/tools/badge/network_verify.cjs "$prefix" "$station_seconds" > "$prefix-pair.json"
else
    verify_args=("$prefix" "$cycles")
    if (( qualification )); then verify_args+=("$channel"); fi
    node clean-rx/tools/badge/lifecycle_verify.cjs "${verify_args[@]}" > "$prefix-pair.json"
fi
