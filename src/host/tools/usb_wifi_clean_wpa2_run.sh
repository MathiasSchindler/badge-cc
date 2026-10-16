#!/usr/bin/env bash
set -u

if [[ $# -lt 3 || $# -gt 4 || ! $1 =~ ^[0-9A-F]{16}$ || ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ||
      ( $# -eq 4 && $4 != --wrong-password ) ]]; then
    echo "Usage: bash usb_wifi_clean_wpa2_run.sh AP_SERIAL CLEAN_STATION_SERIAL EVIDENCE_PREFIX [--wrong-password]" >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 1
ap_serial=$1
station_serial=$2
prefix=$3
mkdir -p "$(dirname "$prefix")" || exit 1
private=$(mktemp -d "${prefix}.private.XXXXXX") || exit 1
credentials=$private/credentials
wrong=$private/wrong
ap_pid=
cleanup() {
    if [[ -n $ap_pid ]]; then
        if kill -0 "$ap_pid" 2>/dev/null; then kill "$ap_pid"; fi
        wait "$ap_pid" 2>/dev/null || true
    fi
    rm -f -- "$private/credentials" "$wrong"
    rmdir -- "$private"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
start_badge() {
    local serial=$1
    for ((attempt=0;attempt<30;attempt++)); do
        if build/picotool-usb/picotool reboot --ser "$serial" > "$prefix-boot-$serial.log" 2>&1; then
            sleep 0.5
            return 0
        fi
        sleep 0.1
    done
    cat "$prefix-boot-$serial.log" >&2
    return 1
}
start_badge "$ap_serial" || exit 1
build/host-tools/usb_wifi_link_host --ap-only "$ap_serial" --credentials-out "$credentials" \
    > "$prefix-ap.jsonl" 2> "$prefix-ap.stderr" &
ap_pid=$!
for ((i=0;i<100;i++)); do
    if grep -q '"test":"ap-ready"' "$prefix-ap.jsonl"; then break; fi
    if ! kill -0 "$ap_pid" 2>/dev/null; then break; fi
    sleep 0.1
done
bssid=$(sed -n 's/.*"test":"ap-ready".*"bssid":"\([^"]*\)".*/\1/p' "$prefix-ap.jsonl")
if [[ ! $bssid =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]]; then
    echo "Vendor WPA2 AP did not become ready" >&2
    exit 1
fi
if [[ ${4:-} == --wrong-password ]]; then
    IFS= read -r ssid < "$credentials" || exit 1
    (umask 077; printf '%s\n%s\n' "$ssid" "deliberately-wrong-password" > "$wrong") || exit 1
    credentials=$wrong
fi
start_badge "$station_serial" || exit 1
build/clean-rx/badge/collector "$station_serial" "$prefix-station.bin" 1 \
    --ap-wpa2 "$bssid" "$credentials" > "$prefix-station.json" 2> "$prefix-station.stderr"
station_result=$?
wait "$ap_pid"
ap_result=$?
ap_pid=
station_mac=$(sed -n 's/.*"local_mac":"\([^"]*\)".*/\1/p' "$prefix-station.json")
ap_peer=$(sed -n 's/.*"test":"ap-observation".*"peer":"\([^"]*\)".*/\1/p' "$prefix-ap.jsonl")
cat "$prefix-station.json"
tail -n 1 "$prefix-ap.jsonl"
if [[ ${4:-} == --wrong-password && $station_result -eq 1 && $ap_result -eq 1 &&
      -n $station_mac && $station_mac == "$ap_peer" ]] &&
    grep -q '"error":0' "$prefix-station.json" &&
    grep -q '"associated":true,"wpa2_authorized":false' "$prefix-station.json" &&
    grep -q '"m2_sent":[1-9]' "$prefix-station.json" &&
    grep -q '"authorized":false,"protected_rx":0,"protected_tx":0' "$prefix-ap.jsonl"; then
    echo "WPA2: wrong-password control associated and sent M2, but neither endpoint authorized or exchanged protected traffic"
elif [[ ${4:-} != --wrong-password && $station_result -eq 0 && $ap_result -eq 0 &&
        -n $station_mac && $station_mac == "$ap_peer" ]]; then
    echo "WPA2: both endpoints confirm authorization and protected traffic for the same station"
else
    cat "$prefix-station.stderr" "$prefix-ap.stderr" >&2
    exit 1
fi
