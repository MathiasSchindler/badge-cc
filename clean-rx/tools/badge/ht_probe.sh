#!/usr/bin/env bash
set -u
set -o pipefail

if [[ ( $# -ne 4 && $# -ne 5 && $# -ne 6 ) ||
      ! $1 =~ ^[0-9A-F]{16}$ || ! $2 =~ ^[0-9A-F]{16}$ ||
      $1 == "$2" || ! $4 =~ ^[0-7]$ || ( $# -eq 6 && $6 != --extended-rx ) ]]; then
    echo "Usage: bash clean-rx/tools/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX MCS:0-7 [MPDU_BYTES:28-2332 [--extended-rx]]" >&2
    echo "The extended RX build also accepts the fixed 3869-byte A-MSDU fixture" >&2
    echo "Use --ack for the clocked 60-byte normal-ACK fixture; --ack-raw retains the raw-TX control" >&2
    echo "Use --ampdu-probe for one diagnostic aggregate of three 80-byte MPDUs (no BA)" >&2
    echo "HT_PROBE_CHANNEL selects channel 1..11 for controlled comparisons (default 1)" >&2
    echo "HT_PROBE_TID pins the normal-ACK fixture TID to 0..7" >&2
    echo "HT_PROBE_CHALLENGE replays a matching 28-hex-digit challenge unchanged" >&2
    exit 2
fi
cd "$(dirname "$0")/../../.." || exit 1
tx=$1 rx=$2 prefix=$3 mcs=$4
channel=${HT_PROBE_CHANNEL:-1}
if [[ ! $channel =~ ^([1-9]|10|11)$ ]]; then
    echo "HT_PROBE_CHANNEL must be 1..11" >&2
    exit 2
fi
collector=build/clean-rx/badge/ht-probe/collector
mpdu=88 mode=probe
clocked=0
if [[ $# -eq 5 && $5 == --ampdu-probe ]]; then
    mpdu=80 mode=ampdu-probe
    collector=build/clean-rx/badge/d2-live/collector
elif [[ $# -eq 5 && ( $5 == --ack || $5 == --ack-raw ) ]]; then
    mpdu=60 mode=ack
    collector=build/clean-rx/badge/ht-ack/collector
    if [[ $5 == --ack ]]; then clocked=1; fi
elif [[ $# -ge 5 ]]; then
    if [[ ! $5 =~ ^[0-9]{2,4}$ ]] ||
       { (( 10#$5 < 28 || 10#$5 > 2332 )) &&
         ! { [[ $# -eq 6 ]] && (( 10#$5 == 3869 )); }; }; then
        echo "HT data MPDU bytes must be 28..2332, or 3869 with --extended-rx" >&2
        exit 2
    fi
    mpdu=$((10#$5)) mode=data
    collector=build/clean-rx/badge/ht-data/collector
    if [[ $# -eq 6 ]]; then collector=build/clean-rx/badge/ht-rx/collector; fi
fi
collector=${CLEAN_RX_COLLECTOR:-$collector}
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
if [[ ! -x $collector || ! -x $picotool ]]; then
    echo "Build the matching isolated ht-$mode collector and install its image first" >&2
    exit 1
fi
for suffix in config.json tx.bin rx.bin tx.json rx.json pair.json tx.stderr rx.stderr; do
    if [[ -e $prefix-$suffix ]]; then
        echo "Evidence prefix already exists: $prefix" >&2
        exit 1
    fi
done
mkdir -p "$(dirname "$prefix")" || exit 1
if [[ ${HT_PROBE_CHALLENGE+x} ]]; then
    challenge=$HT_PROBE_CHALLENGE
    if [[ ! $challenge =~ ^[0-9a-f]{28}$ ]] ||
       (( 16#${challenge:26:2} != mcs )); then
        echo "HT_PROBE_CHALLENGE must be 28 lowercase hex digits with the requested MCS" >&2
        exit 2
    fi
    if [[ $mode != probe ]] &&
       (( 16#${challenge:22:2} + 256 * 16#${challenge:24:2} != mpdu )); then
        echo "HT_PROBE_CHALLENGE length does not match the requested fixture" >&2
        exit 2
    fi
else
    challenge=$(od -An -N13 -tx1 /dev/urandom | tr -d ' \n') || exit 1
    challenge=$challenge$(printf '%02x' "$mcs")
fi
if [[ $mode != probe ]]; then
    challenge=${challenge:0:22}$(printf '%02x%02x%02x' "$((mpdu & 255))" "$((mpdu >> 8))" "$mcs")
fi
if [[ ! $challenge =~ ^[0-9a-f]{28}$ ]]; then
    echo "Challenge generation failed" >&2
    exit 1
fi
if [[ ${HT_PROBE_TID+x} ]]; then
    if [[ $mode != ack || ! $HT_PROBE_TID =~ ^[0-7]$ ]]; then
        echo "HT_PROBE_TID requires --ack/--ack-raw and a TID in 0..7" >&2
        exit 2
    fi
    byte=$((16#${challenge:12:2}))
    if [[ ${HT_PROBE_CHALLENGE+x} ]] && (( (byte & 15) != HT_PROBE_TID )); then
        echo "HT_PROBE_CHALLENGE TID does not match HT_PROBE_TID" >&2
        exit 2
    fi
    challenge=${challenge:0:12}$(printf '%02x' "$(((byte & 240) | HT_PROBE_TID))")${challenge:14}
fi
if [[ $mode == ack ]]; then
    byte=$((16#${challenge:12:2}))
    if [[ ${HT_PROBE_CHALLENGE+x} ]]; then
        if (( (byte & 15) > 7 )); then
            echo "HT_PROBE_CHALLENGE requires an ACK fixture TID in 0..7" >&2
            exit 2
        fi
    else
        challenge=${challenge:0:12}$(printf '%02x' "$((byte & 247))")${challenge:14}
    fi
fi
subframes=1
if [[ $mode == ampdu-probe ]]; then subframes=3; fi
printf '{"challenge":"%s","mcs":%s,"channel":%s,"mpdu_bytes":%s,"tx_frames":1,"aggregate_subframes":%s,"vendor_peer":false,"ht_qualified":false}\n' \
    "$challenge" "$mcs" "$channel" "$mpdu" "$subframes" > "$prefix-config.json" || exit 1
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
start_badge() {
    local serial=$1 attempt
    for ((attempt=0;attempt<30;attempt++)); do
        if "$picotool" reboot --ser "$serial" > "$prefix-boot-$serial.log" 2>&1; then
            sleep 0.5
            return 0
        fi
        sleep 0.1
    done
    echo "Badge not available in BOOTSEL: $serial" >&2
    return 1
}
wait_bootsel() {
    local serial=$1 attempt
    for ((attempt=0;attempt<100;attempt++)); do
        if "$picotool" info --ser "$serial" > "$prefix-ready-$serial.log" 2>&1; then
            return 0
        fi
        sleep 0.1
    done
    echo "Badge not ready in BOOTSEL before starting either collector: $serial" >&2
    return 1
}
wait_bootsel "$tx" && wait_bootsel "$rx" || exit 1
start_badge "$rx" || exit 1
args=("$challenge" "$mcs")
if [[ $mode == data ]]; then args+=("$mpdu"); fi
"$collector" "$rx" "$prefix-rx.bin" "$channel" "--ht-$mode-rx" "${args[@]}" \
    > "$prefix-rx.json" 2> "$prefix-rx.stderr" &
rx_pid=$!
sleep 0.2
start_badge "$tx" || exit 1
tx_role="--ht-$mode-tx"
if [[ $clocked == 1 ]]; then tx_role=--ht-ack-clocked-tx; fi
"$collector" "$tx" "$prefix-tx.bin" "$channel" "$tx_role" "${args[@]}" \
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
pair_mode=ht-pair
if [[ $mode == ampdu-probe ]]; then
    if jq -e '.ba_experiment.enabled == true' "$prefix-rx.json" >/dev/null; then
        if ! jq -ne --slurpfile tx "$prefix-tx.json" --slurpfile rx "$prefix-rx.json" \
            '$rx[0].ba_experiment.errors==0 and
             $rx[0].ba_experiment.attempts==1 and $rx[0].ba_experiment.started==1 and
             $rx[0].ba_experiment.bitmap>0 and $rx[0].shm_terminal.valid and
             $tx[0].ba_experiment.returned_ba==true and
             $tx[0].ba_experiment.returned_bitmap==$rx[0].ba_experiment.bitmap and
             $rx[0].delivered_subframe_bitmap==$rx[0].ba_experiment.bitmap' >/dev/null; then
            echo "Diagnostic BA failed: retained delivery and a matching returned BA are required; local TX counters are insufficient" >&2
            exit 1
        fi
    fi
    jq -n --slurpfile tx "$prefix-tx.json" --slurpfile rx "$prefix-rx.json" \
        '{mode:"ampdu-probe-pair",channel:$tx[0].channel,mcs:$tx[0].mcs,
          local_tx_submission:$tx[0].local_tx_submission,
          fresh_radio_capture:$rx[0].fresh_radio_capture,
          retained_subframe_bitmap:$rx[0].retained_subframe_bitmap,
          fixture_bad_fcs_mask:$tx[0].fixture_bad_fcs_mask,
          multi_mpdu_radio_capture:$rx[0].multi_mpdu_radio_capture,
          all_subframes_retained:($rx[0].retained_subframe_bitmap==7),
          dma_experiment_enabled:$rx[0].dma_experiment_enabled,
          shm_delivery_enabled:$rx[0].shm_delivery_enabled,
          shm_terminal:$rx[0].shm_terminal,
          ba_experiment:$rx[0].ba_experiment,
          compressed_ba_return:$tx[0].ba_experiment,
          delivered_subframe_bitmap:$rx[0].delivered_subframe_bitmap,
          delivered_subframes:$rx[0].delivered_subframes,
          actual_multi_mpdu_delivery:($tx[0].local_tx_submission and
                                     $rx[0].actual_multi_mpdu_delivery),
          all_subframes_delivered:($tx[0].local_tx_submission and
                                  $rx[0].all_subframes_delivered),
          immediate_ba:false,sifs_timing_qualified:false,ht_qualified:false}' \
        | tee "$prefix-pair.json"
    exit "$?"
fi
if [[ $mode == data ]]; then pair_mode=ht-data-pair; fi
if [[ $mode == ack ]]; then
    python3 - "$prefix" <<'PY' | tee "$prefix-pair.json"
import json
import sys
with open(sys.argv[1] + "-tx.json") as file:
    tx = json.load(file)
with open(sys.argv[1] + "-rx.json") as file:
    rx = json.load(file)
print(json.dumps(dict(mode="ht-ack-pair", channel=tx["channel"], mcs=tx["mcs"], mpdu_bytes=60,
                     fresh_ht_rx=rx["fresh_ht_rx"],
                     ack_responses_started=rx["ack_responses_started"],
                     fresh_return_ack=tx["fresh_return_ack"],
                     sifs_timing_qualified=False, ht_interoperability_qualified=False)))
PY
    exit "$?"
fi
printf '{"mode":"%s","channel":%s,"mcs":%s,"mpdu_bytes":%s,"fresh_ht_rx":true,"tx_over_air_confirmed":true,"ht_interoperability_qualified":false}\n' \
    "$pair_mode" "$channel" "$mcs" "$mpdu" | tee "$prefix-pair.json"
