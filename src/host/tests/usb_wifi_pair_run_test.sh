#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
scratch=$(mktemp -d "$root/build/host-tools/wifi-pair-test.XXXXXXXX")
trap 'rm -f "$scratch/picotool" "$scratch/collector" "$scratch/image.uf2" "$scratch/commands" "$scratch/output"; rmdir "$scratch"' EXIT
export PAIR_TEST_LOG="$scratch/commands"
cat >"$scratch/picotool" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >>"$PAIR_TEST_LOG"
case "$1" in
    info) echo RP2350 ;;
    load) [[ "$2" == -v && "$3" != -x ]] ;;
    reboot) ;;
    *) exit 2 ;;
esac
EOF
cat >"$scratch/collector" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'collect %s %s\n' "$1" "$2" >>"$PAIR_TEST_LOG"
exit "${PAIR_TEST_RESULT:-0}"
EOF
chmod +x "$scratch/picotool" "$scratch/collector"
printf 'fixture\n' >"$scratch/image.uf2"
export PICOTOOL="$scratch/picotool"
runner="$root/src/host/tools/usb_wifi_pair_run.sh"
for order in forward reverse; do
    ap=9BBF88DD17D66E7D
    station=08DA26F86E57F5EA
    if [[ $order == reverse ]]; then ap=08DA26F86E57F5EA; station=9BBF88DD17D66E7D; fi
    : >"$PAIR_TEST_LOG"
    bash "$runner" --prebuilt "$ap" "$station" "$scratch/image.uf2" "$scratch/collector" >"$scratch/output"
    [[ $(grep -c '^load -v ' "$PAIR_TEST_LOG") == 2 ]]
    [[ $(grep -c '^reboot --ser ' "$PAIR_TEST_LOG") == 2 ]]
    [[ $(sed -n '5p' "$PAIR_TEST_LOG") == "reboot --ser $ap" ]]
    [[ $(sed -n '6p' "$PAIR_TEST_LOG") == "reboot --ser $station" ]]
    [[ $(sed -n '7p' "$PAIR_TEST_LOG") == "collect $ap $station" ]]
    [[ $(grep -c '^info ' "$PAIR_TEST_LOG") == 4 ]]
done
if PAIR_TEST_RESULT=7 bash "$runner" --prebuilt "$ap" "$station" "$scratch/image.uf2" "$scratch/collector" >"$scratch/output" 2>&1; then
    echo 'Collector failure was incorrectly accepted' >&2; exit 1
else
    [[ $? == 7 ]]
fi
if bash "$runner" --prebuilt "$ap" "$ap" "$scratch/image.uf2" "$scratch/collector" >"$scratch/output" 2>&1; then
    echo 'Identical serials were incorrectly accepted' >&2; exit 1
else
    [[ $? == 2 ]]
fi
echo 'wifi pair runner: explicit prebuilt inputs, both flash/reboots before collection, reversed roles, recovery and failure propagation PASS'
