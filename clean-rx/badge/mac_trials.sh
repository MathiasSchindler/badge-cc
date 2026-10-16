#!/usr/bin/env bash
set -u
set -o pipefail

if [[ $# -ne 3 || ! $1 =~ ^[0-9A-F]{16}$ || ! $2 =~ ^[0-9A-F]{16}$ || $1 == "$2" ]]; then
    echo "Usage: bash clean-rx/badge/mac_trials.sh AP_SERIAL CLEAN_STATION_SERIAL NEW_EVIDENCE_PREFIX" >&2
    exit 2
fi
cd "$(dirname "$0")/../.." || exit 1
ap=$1
station=$2
prefix=$3
summary=$prefix-summary.jsonl
if [[ -e $summary ]]; then
    echo "Evidence summary already exists: $summary" >&2
    exit 1
fi
for tool in build/picotool-usb/picotool build/host-tools/usb_wifi_link_host build/clean-rx/badge/collector; do
    if [[ ! -x $tool ]]; then echo "Missing test tool: $tool" >&2; exit 1; fi
done
mkdir -p "$(dirname "$prefix")" || exit 1
: > "$summary" || exit 1
failures=0

start_badge() {
    local serial=$1 log=$2 attempt
    for ((attempt=0;attempt<30;attempt++)); do
        if build/picotool-usb/picotool reboot --ser "$serial" > "$log" 2>&1; then
            sleep 1
            return 0
        fi
        sleep 0.1
    done
    echo "Badge did not enter application mode: $serial" >&2
    return 1
}

open_trial() {
    local trial=$1 pid attempt bssid result
    start_badge "$ap" "$trial-boot-$ap.log" || return 1
    build/host-tools/usb_wifi_link_host --ap-only "$ap" --open \
        > "$trial-ap.jsonl" 2> "$trial-ap.stderr" &
    pid=$!
    for ((attempt=0;attempt<100;attempt++)); do
        if grep -q '"test":"ap-ready"' "$trial-ap.jsonl"; then break; fi
        if ! ps -p "$pid" > /dev/null; then break; fi
        sleep 0.1
    done
    bssid=$(sed -n 's/.*"test":"ap-ready".*"bssid":"\([^"]*\)".*/\1/p' "$trial-ap.jsonl")
    if [[ ! $bssid =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]]; then
        echo "Open AP did not become ready" >&2
        wait "$pid" || true
        return 1
    fi
    if ! start_badge "$station" "$trial-boot-$station.log"; then
        wait "$pid" || true
        return 1
    fi
    build/clean-rx/badge/collector "$station" "$trial-station.bin" 1 --ap-assoc "$bssid" \
        > "$trial-station.json" 2> "$trial-station.stderr"
    result=$?
    if ! wait "$pid"; then result=1; fi
    return "$result"
}

for mode in wpa2 wrong-password open; do
    case $mode in wpa2) count=10;; wrong-password) count=2;; open) count=3;; esac
    for ((i=1;i<=count;i++)); do
        trial=$prefix-$mode-$i
        if [[ $mode == open ]]; then
            open_trial "$trial" > "$trial-run.log" 2>&1
        else
            options=()
            if [[ $mode == wrong-password ]]; then options=(--wrong-password); fi
            bash src/host/tools/usb_wifi_clean_wpa2_run.sh "$ap" "$station" "$trial" \
                "${options[@]}" > "$trial-run.log" 2>&1
        fi
        result=$?
        python3 - "$trial" "$mode" "$i" "$result" <<'PY' | tee -a "$summary"
import json
import sys
from pathlib import Path

prefix, mode, index, result = sys.argv[1:]
row = {"trial": int(index), "mode": mode, "runner_exit": int(result), "valid": False}
try:
    station = json.loads(Path(prefix + "-station.json").read_text())
    for key in ("build", "error", "associated", "wpa2_authorized", "auth_attempts",
                "assoc_attempts", "m2_sent", "protected_echoes", "mac_response_other",
                "mac_published_invalid"):
        row[key] = station[key]
    observations = [json.loads(line) for line in Path(prefix + "-ap.jsonl").read_text().splitlines()]
    ap_errors = [item for item in observations
                 if item.get("test") == "wifi-link" and item.get("error") != "OK"]
    if ap_errors:
        row["ap_error"] = ap_errors[-1]["error"]
    peers = [item for item in observations if item.get("test") == "ap-observation"]
    if not peers:
        raise ValueError("Missing independent AP observation")
    peer = peers[-1]
    matched = station["local_mac"] == peer["peer"] and peer["association_observed"]
    valid = (int(result) == 0 and station["error"] == 0 and station["associated"]
             and matched and not ap_errors)
    if mode == "wpa2":
        valid = valid and station["wpa2_authorized"] and station["protected_echoes"] > 0
        valid = valid and peer["authorized"] and peer["protected_rx"] > 0 and peer["protected_tx"] > 0
    elif mode == "wrong-password":
        valid = valid and station["m2_sent"] > 0 and not station["wpa2_authorized"]
        valid = valid and station["protected_echoes"] == 0 and not peer["authorized"]
        valid = valid and peer["protected_rx"] == 0 and peer["protected_tx"] == 0
    else:
        valid = valid and peer["security"] == "open"
    row["valid"] = bool(valid)
    if not valid:
        row["failure"] = "Endpoint status, identity or expected exchange was not confirmed"
except (OSError, ValueError, KeyError, TypeError) as error:
    row["failure"] = str(error)
print(json.dumps(row, separators=(",", ":")))
sys.exit(0 if row["valid"] else 1)
PY
        if [[ $? -ne 0 ]]; then failures=$((failures + 1)); fi
    done
done
printf 'MAC connection regressions: %s/15 failed; not the 100000-frame OTA soak or RF timing qualification\n' "$failures"
[[ $failures -eq 0 ]]
