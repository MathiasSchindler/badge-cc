#!/usr/bin/env bash
set -u
set -o pipefail

if [[ $# -lt 4 || $# -gt 7 || ! $1 =~ ^[0-9A-F]{16}$ || ! $2 =~ ^[0-9A-F]{16}$ ||
      $1 == "$2" || ! $4 =~ ^[1-9][0-9]{0,5}$ || $4 -gt 100000 ||
      ! ${5:-0} =~ ^([0-9]|1[0-6])$ ||
      ! ${6:-ofdm6} =~ ^(ofdm6|1-long|(2|5\.5|11)-(long|short))$ ||
      ! ${7:-cycle} =~ ^(cycle|32|1500|1600|2304|cck-boundary|frag2|frag16)$ ]]; then
    echo "Usage: bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL NEW_PREFIX COUNT:1-100000 [DROP_ACKS:0-16 [PHY:ofdm6|1/2/5.5/11-long|2/5.5/11-short [PAYLOAD:cycle|32|1500|1600|2304|cck-boundary|frag2|frag16]]]" >&2
    exit 2
fi
cd "$(dirname "$0")/../../.." || exit 1
initiator=$1
responder=$2
prefix=$3
count=$4
drop_acks=${5:-0}
phy=${6:-ofdm6}
payload=${7:-cycle}
collector=${CLEAN_RX_COLLECTOR:-build/clean-rx/badge/collector}
picotool=${CLEAN_RX_PICOTOOL:-build/picotool-usb/picotool}
if [[ ! -x $collector || ! -x $picotool ]]; then
    echo "Build the matching collector and install its image first" >&2
    exit 1
fi
payloads=$payload
if [[ $payload == cycle ]]; then payloads=32,1500,2304; fi
if [[ $payload == cck-boundary ]]; then payloads=1500,1550,1600,1650,1700,1750,1800,1850,1900,2304; fi
fragments=1
if [[ $payload == frag2 ]]; then payloads=2304; fragments=2; fi
if [[ $payload == frag16 ]]; then payloads=2304; fragments=16; fi
rate=${phy%-*}
if [[ $phy == ofdm6 ]]; then rate=6; fi
if [[ -e $prefix-config.json || -e $prefix-init.json || -e $prefix-reply.json ]]; then
    echo "Evidence prefix already exists: $prefix" >&2
    exit 1
fi
mkdir -p "$(dirname "$prefix")" || exit 1
if [[ ${MAC_SOAK_CHALLENGE+x} ]]; then
    challenge=$MAC_SOAK_CHALLENGE
else
    challenge=$(od -An -N14 -tx1 /dev/urandom | tr -d ' \n') || exit 1
fi
if [[ ! $challenge =~ ^[0-9a-f]{28}$ ]]; then
    echo "Challenge must be exactly 28 lowercase hexadecimal digits" >&2
    exit 2
fi
printf '{"challenge":"%s","frames_per_direction":%s,"drop_acks_per_endpoint":%s,"channel":1,"phy":"%s","rate_mbps":%s,"payloads":[%s],"fragments_per_msdu":%s,"vendor_control":false}\n' \
    "$challenge" "$count" "$drop_acks" "$phy" "$rate" "$payloads" "$fragments" > "$prefix-config.json" || exit 1
reply_pid=
cleanup() {
    if [[ -n $reply_pid ]]; then
        if kill -0 "$reply_pid" 2>/dev/null; then kill "$reply_pid"; fi
        wait "$reply_pid" 2>/dev/null || true
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
start_badge "$responder" || exit 1
"$collector" "$responder" "$prefix-reply.bin" 1 \
    --mac-soak-reply "$challenge" "$count" "$drop_acks" "$phy" "$payload" > "$prefix-reply.json" 2> "$prefix-reply.stderr" &
reply_pid=$!
sleep 1
start_badge "$initiator" || exit 1
"$collector" "$initiator" "$prefix-init.bin" 1 \
    --mac-soak-init "$challenge" "$count" "$drop_acks" "$phy" "$payload" > "$prefix-init.json" 2> "$prefix-init.stderr"
init_result=$?
wait "$reply_pid"
reply_result=$?
reply_pid=
cat "$prefix-init.json" "$prefix-reply.json"
if [[ $init_result -ne 0 || $reply_result -ne 0 ]]; then
    cat "$prefix-init.stderr" "$prefix-reply.stderr" >&2
    exit 1
fi
echo "Both clean endpoints confirm exact bidirectional frame counts and payload integrity; NOT vendor comparison or RF timing"
