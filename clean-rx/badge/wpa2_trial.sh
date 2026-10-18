#!/usr/bin/env bash
set -u
set -o pipefail

if [[ $# -lt 3 || $# -gt 4 || ! $1 =~ ^[0-9A-F]{16}$ ||
      ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ||
      ( $# -eq 4 && $4 != --wrong-password ) ]]; then
    echo "Usage: bash clean-rx/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL NEW_PREFIX [--wrong-password]" >&2
    exit 2
fi
cd "$(dirname "$0")/../.." || exit 1
ap_serial=$1
station_serial=$2
prefix=$3
collector=${CLEAN_RX_COLLECTOR:-build/clean-rx/badge/collector}
ap_host=build/host-tools/usb_wifi_link_host
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
if [[ ! -x $collector || ! -x $ap_host || ! -x $picotool ]]; then
    echo "Build the matching clean collector, vendor AP host and picotool first" >&2
    exit 1
fi
if [[ -e $prefix-ap.jsonl || -e $prefix-station.json || -e $prefix-timing.log ]]; then
    echo "Evidence prefix already exists: $prefix" >&2
    exit 1
fi
mkdir -p "$(dirname "$prefix")" || exit 1
private=$(mktemp -d "${prefix}.private.XXXXXX") || exit 1
ap_pid=
cleanup() {
    if [[ -n $ap_pid ]]; then wait "$ap_pid" || true; fi
    rm -f -- "$private/credentials" "$private/wrong"
    rmdir -- "$private"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
start_badge() {
    local serial=$1 attempt
    for ((attempt=0;attempt<30;attempt++)); do
        if "$picotool" reboot --ser "$serial" \
            >> "$prefix-boot-$serial.log" 2>&1; then return 0; fi
        sleep 0.1
    done
    echo "Badge not available in BOOTSEL: $serial" >&2
    return 1
}
# Boot both USB applications before starting the vendor AP's bounded window.
start_badge "$station_serial" || exit 1
start_badge "$ap_serial" || exit 1
sleep 0.5
SECONDS=0
"$ap_host" --ap-only "$ap_serial" --credentials-out "$private/credentials" \
    > "$prefix-ap.jsonl" 2> "$prefix-ap.stderr" &
ap_pid=$!
for ((attempt=0;attempt<200;attempt++)); do
    if grep -q '"test":"ap-ready"' "$prefix-ap.jsonl"; then break; fi
    sleep 0.05
done
bssid=$(sed -n 's/.*"test":"ap-ready".*"bssid":"\([^"]*\)".*/\1/p' "$prefix-ap.jsonl")
if [[ ! $bssid =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]]; then
    echo "Vendor WPA2 AP did not become ready" >&2
    exit 1
fi
printf 'ap_ready_seconds=%s\n' "$SECONDS" > "$prefix-timing.log"
credentials=$private/credentials
if [[ ${4:-} == --wrong-password ]]; then
    IFS= read -r ssid < "$credentials" || exit 1
    (umask 077; printf '%s\n%s\n' "$ssid" deliberately-wrong-password > "$private/wrong") || exit 1
    credentials=$private/wrong
fi
printf 'station_start_seconds=%s\n' "$SECONDS" >> "$prefix-timing.log"
"$collector" "$station_serial" "$prefix-station.bin" 1 --ap-wpa2 "$bssid" "$credentials" \
    > "$prefix-station.json" 2> "$prefix-station.stderr"
station_result=$?
printf 'station_done_seconds=%s\nstation_exit=%s\n' "$SECONDS" "$station_result" >> "$prefix-timing.log"
wait "$ap_pid"
ap_result=$?
ap_pid=
printf 'ap_done_seconds=%s\nap_exit=%s\n' "$SECONDS" "$ap_result" >> "$prefix-timing.log"
station_mac=$(sed -n 's/.*"local_mac":"\([^"]*\)".*/\1/p' "$prefix-station.json")
ap_peer=$(sed -n 's/.*"test":"ap-observation".*"peer":"\([^"]*\)".*/\1/p' "$prefix-ap.jsonl")
if [[ ! $station_mac =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ || $station_mac != "$ap_peer" ]]; then
    echo "Independent AP/station peer identity was not established" >&2
    exit 1
fi
if [[ ${4:-} == --wrong-password ]]; then
    if [[ $station_result -eq 1 && $ap_result -eq 1 ]] &&
        grep -q '"error":0' "$prefix-station.json" &&
        grep -q '"associated":true,"wpa2_authorized":false' "$prefix-station.json" &&
        grep -q '"m2_sent":[1-9]' "$prefix-station.json" &&
        grep -q '"authorized":false,"protected_rx":0,"protected_tx":0' "$prefix-ap.jsonl"; then
        echo "Wrong-password control: associated and sent M2; neither endpoint authorized or exchanged protected traffic"
        exit 0
    fi
elif [[ $station_result -eq 0 && $ap_result -eq 0 ]]; then
    echo "Both endpoints confirm WPA2 authorization and protected traffic for the same station"
    exit 0
fi
cat "$prefix-station.stderr" "$prefix-ap.stderr" >&2
echo "WPA2 trial failed; timing and both endpoints' evidence retained at $prefix" >&2
exit 1
