#!/usr/bin/env bash
set -u
set -o pipefail

if [[ $# -lt 3 || ! $1 =~ ^[0-9A-F]{16}$ ||
      ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ]]; then
    echo "Usage: bash clean-rx/tools/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL NEW_PREFIX [--peer-ap CHANNEL:1,6,11] [--raw-psk|--wrong-password] [--group-traffic|--rekey]" >&2
    exit 2
fi
cd "$(dirname "$0")/../../.." || exit 1
ap_serial=$1
station_serial=$2
prefix=$3
shift 3
channel=1
peer=0
credential_mode=passphrase
group_traffic=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --group-traffic|--rekey)
            if [[ $group_traffic -ne 0 ]]; then echo "Traffic modes are mutually exclusive" >&2; exit 2; fi
            group_traffic=1
            if [[ $1 == --rekey ]]; then group_traffic=2; fi
            shift ;;
        --peer-ap)
            if [[ $peer -ne 0 || $# -lt 2 || ! $2 =~ ^(1|6|11)$ ]]; then
                echo "Expected a single --peer-ap CHANNEL:1,6,11" >&2; exit 2
            fi
            peer=1; channel=$2; shift 2 ;;
        --raw-psk|--wrong-password)
            if [[ $credential_mode != passphrase ]]; then
                echo "Credential modes are mutually exclusive" >&2; exit 2
            fi
            credential_mode=${1#--}; shift ;;
        *) echo "Unknown WPA2 trial argument: $1" >&2; exit 2 ;;
    esac
done
if [[ $group_traffic -ne 0 && ( $peer -ne 1 || $credential_mode == wrong-password ) ]]; then
    echo "Traffic modes require --peer-ap and correct credentials" >&2; exit 2
fi
collector=${CLEAN_RX_COLLECTOR:-build/clean-rx/badge/collector}
ap_host=${CLEAN_RX_AP_COLLECTOR:-build/host-tools/usb_wifi_link_host}
if [[ $peer -eq 1 ]]; then ap_host=${CLEAN_RX_AP_COLLECTOR:-build/clean-rx/peer/collector}; fi
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
source clean-rx/tools/usb-run.sh
if [[ ! -x $collector || ! -x $ap_host || ! -x $picotool ]]; then
    echo "Build the matching clean collector, vendor AP host and picotool first" >&2
    exit 1
fi
for suffix in ap.jsonl ap.stderr ap.bin station.json station.bin station.stderr timing.log pair.json \
              boot-"$ap_serial".log boot-"$station_serial".log; do
    if [[ -e $prefix-$suffix || -L $prefix-$suffix ]]; then
        echo "Evidence prefix already exists: $prefix-$suffix" >&2; exit 1
    fi
done
mkdir -p "$(dirname "$prefix")" || exit 1
private=$(mktemp -d "${prefix}.private.XXXXXX") || exit 1
ap_pid=
station_pid=
cleanup() {
    usb_stop "$station_pid" "$ap_pid"
    rm -f -- "$private/credentials" "$private/wrong" "$private/raw"
    rmdir -- "$private"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# Boot both USB applications before starting the vendor AP's bounded window.
usb_boot "$station_serial" "$prefix-boot-$station_serial.log" &&
    usb_boot "$ap_serial" "$prefix-boot-$ap_serial.log" || exit 1
usb_pair_ready "$station_serial" "$ap_serial" \
    "$prefix-ready-$station_serial.log" "$prefix-ready-$ap_serial.log" || exit 1
SECONDS=0
if [[ $peer -eq 1 ]]; then
    ap_mode=--protected-ap; ap_seconds=16
    if [[ $group_traffic -ne 0 ]]; then ap_mode=--protected-traffic-ap; ap_seconds=45; fi
    if [[ $group_traffic -eq 2 ]]; then ap_mode=--protected-rekey-ap; fi
    "${usb_command[@]}" "$((ap_seconds + 50))" "$ap_host" "$ap_serial" "$prefix-ap.bin" "$ap_mode" "$channel" "$ap_seconds" "$private/credentials" \
        > "$prefix-ap.jsonl" 2> "$prefix-ap.stderr" &
else
    "${usb_command[@]}" 100 "$ap_host" --ap-only "$ap_serial" --credentials-out "$private/credentials" \
        > "$prefix-ap.jsonl" 2> "$prefix-ap.stderr" &
fi
ap_pid=$!
ready_test=ap-ready
if [[ $peer -eq 1 ]]; then ready_test=e1-ap-ready; fi
if [[ $group_traffic -ne 0 ]]; then ready_test=e2-ap-ready; fi
for ((attempt=0;attempt<200;attempt++)); do
    if grep -q "\"test\":\"$ready_test\"" "$prefix-ap.jsonl"; then break; fi
    if ! kill -0 "$ap_pid" 2>/dev/null; then break; fi
    sleep 0.05
done
bssid=$(sed -n "s/.*\"test\":\"$ready_test\".*\"bssid\":\"\([^\"]*\)\".*/\1/p" "$prefix-ap.jsonl")
if [[ ! $bssid =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]]; then
    echo "Vendor WPA2 AP did not become ready" >&2
    exit 1
fi
printf 'ap_ready_seconds=%s\n' "$SECONDS" > "$prefix-timing.log"
credentials=$private/credentials
if [[ $credential_mode == wrong-password ]]; then
    IFS= read -r ssid < "$credentials" || exit 1
    (umask 077; printf '%s\n%s\n' "$ssid" deliberately-wrong-password > "$private/wrong") || exit 1
    credentials=$private/wrong
elif [[ $credential_mode == raw-psk ]]; then
    usb_control run 10 "$collector" --derive-psk "$credentials" "$private/raw" || exit 1
    credentials=$private/raw
fi
printf 'station_start_seconds=%s\n' "$SECONDS" >> "$prefix-timing.log"
if ! kill -0 "$ap_pid" 2>/dev/null; then echo "AP exited before station; retain AP logs" >&2; exit 1; fi
station_mode=--ap-wpa2
if [[ $group_traffic -ne 0 ]]; then station_mode=--ap-wpa2-rekey; fi
"${usb_command[@]}" 75 "$collector" "$station_serial" "$prefix-station.bin" "$channel" "$station_mode" "$bssid" "$credentials" \
    > "$prefix-station.json" 2> "$prefix-station.stderr" &
station_pid=$!
allowed=0
if [[ $credential_mode == wrong-password ]]; then allowed=1; fi
usb_wait_pair "$station_pid" "$ap_pid" "$allowed" || exit 1
station_result=$usb_first_result ap_result=$usb_second_result
station_pid= ap_pid=
printf 'station_done_seconds=%s\nstation_exit=%s\n' "$SECONDS" "$station_result" >> "$prefix-timing.log"
printf 'ap_done_seconds=%s\nap_exit=%s\n' "$SECONDS" "$ap_result" >> "$prefix-timing.log"
if [[ $peer -eq 1 ]]; then
    if [[ $group_traffic -ne 0 ]]; then
        verification=()
        if [[ $group_traffic -eq 1 ]]; then verification=(--traffic-only); fi
        node clean-rx/tools/badge/wpa2_rekey_verify.cjs "$prefix" "$station_serial" "$ap_serial" "${verification[@]}" \
            > "$prefix-pair.json"
        exit $?
    fi
    node clean-rx/tools/badge/wpa2_verify.cjs "$prefix" "$channel" "$credential_mode" \
        "$station_serial" "$ap_serial" > "$prefix-pair.json"
    exit $?
fi
station_mac=$(sed -n 's/.*"local_mac":"\([^"]*\)".*/\1/p' "$prefix-station.json")
ap_peer=$(sed -n 's/.*"test":"ap-observation".*"peer":"\([^"]*\)".*/\1/p' "$prefix-ap.jsonl")
if [[ ! $station_mac =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ || $station_mac != "$ap_peer" ]]; then
    echo "Independent AP/station peer identity was not established" >&2
    exit 1
fi
if [[ $credential_mode == wrong-password ]]; then
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
