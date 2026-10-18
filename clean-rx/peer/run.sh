#!/usr/bin/env bash
set -u
set -o pipefail

if [[ $# -ne 4 || ! $1 =~ ^[0-9A-F]{16}$ || ! $2 =~ ^[0-9A-F]{16}$ ||
      $1 == "$2" || ! $4 =~ ^[0-7]$ ]]; then
    echo "Usage: bash clean-rx/peer/run.sh VENDOR_TX_SERIAL CLEAN_RX_SERIAL NEW_PREFIX MCS:0-7" >&2
    exit 2
fi
cd "$(dirname "$0")/../.." || exit 1
tx=$1 rx=$2 prefix=$3 mcs=$4
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
tx_collector=${CLEAN_VENDOR_PEER_COLLECTOR:-build/clean-rx/peer/collector}
rx_collector=${CLEAN_RX_COLLECTOR:-build/clean-rx/badge/d2-controlled/collector}
for executable in "$picotool" "$tx_collector" "$rx_collector"; do
    if [[ ! -x $executable ]]; then echo "Missing executable: $executable" >&2; exit 1; fi
done
for suffix in config.json tx.bin rx.bin tx.json rx.json pair.json tx.stderr rx.stderr boot-tx.log boot-rx.log; do
    if [[ -e $prefix-$suffix ]]; then echo "Evidence prefix already exists: $prefix" >&2; exit 1; fi
done
mkdir -p "$(dirname "$prefix")" || exit 1
challenge=$(od -An -N13 -tx1 /dev/urandom | tr -d ' \n') || exit 1
challenge=$challenge$(printf '%02x' "$mcs")
if [[ ! $challenge =~ ^[0-9a-f]{28}$ ]]; then echo "Fresh challenge generation failed" >&2; exit 1; fi
printf '{"challenge":"%s","mcs":%s,"channel":1,"vendor_peer":true,"frames":8,"ethernet_bytes":62,"ht_qualified":false}\n' \
    "$challenge" "$mcs" > "$prefix-config.json" || exit 1
rx_pid=
cleanup() {
    if [[ -n $rx_pid ]]; then
        if kill -0 "$rx_pid" 2>/dev/null; then kill "$rx_pid"; fi
        wait "$rx_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
for serial in "$tx" "$rx"; do
    ready=0
    for ((attempt=0;attempt<10;attempt++)); do
        if "$picotool" info --ser "$serial" > "$prefix-ready-$serial.log" 2>&1; then
            ready=1
            break
        fi
        sleep 0.2
    done
    if [[ $ready -ne 1 ]]; then
        echo "Badge not ready in BOOTSEL: $serial" >&2
        exit 1
    fi
done
"$picotool" reboot --ser "$rx" > "$prefix-boot-rx.log" 2>&1 || exit 1
sleep 0.5
"$rx_collector" "$rx" "$prefix-rx.bin" 1 --vendor-ht-rx "$challenge" "$mcs" \
    > "$prefix-rx.json" 2> "$prefix-rx.stderr" &
rx_pid=$!
# The clean radio cold-initializes before entering its four-second RX window.
sleep 6
"$picotool" reboot --ser "$tx" > "$prefix-boot-tx.log" 2>&1 || exit 1
sleep 0.5
"$tx_collector" "$tx" "$prefix-tx.bin" --tx-multicast "$challenge" \
    > "$prefix-tx.json" 2> "$prefix-tx.stderr"
tx_result=$?
wait "$rx_pid"
rx_result=$?
rx_pid=
cat "$prefix-tx.json" "$prefix-rx.json"
if [[ $tx_result -ne 0 || $rx_result -ne 0 ]]; then
    cat "$prefix-tx.stderr" "$prefix-rx.stderr" >&2
    exit 1
fi
python3 - "$prefix" <<'PY' | tee "$prefix-pair.json"
import json
import sys
prefix = sys.argv[1]
with open(prefix + "-tx.json") as file:
    tx = json.load(file)
with open(prefix + "-rx.json") as file:
    rx = json.load(file)
confirmed = (tx["stage"] == 5 and tx["frames_submitted"] == 8
             and tx["error"] == 0 and rx["fresh_ht_rx"]
             and tx["requested_rate"] == (tx["readback_rate"] & ~0xc0000000)
             == (0x01010000 | rx["mcs"]))
print(json.dumps(dict(vendor_peer=True, fresh_ht_rx=confirmed,
                     tx_over_air_confirmed=confirmed, frames=rx["fresh_frames"],
                     sequence_bitmap=rx["sequence_bitmap"],
                     ht_interoperability_qualified=False)))
sys.exit(0 if confirmed else 1)
PY
