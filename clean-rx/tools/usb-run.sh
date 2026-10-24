#!/usr/bin/env bash

usb_process=clean-rx/tools/usb-process.cjs
usb_ready_tool=${CLEAN_RX_USB_READY:-build/clean-rx/tools/usb-ready}
if [[ ! -x $usb_ready_tool ]] || ! command -v node >/dev/null; then
    echo "Build USB readiness tooling: make -f clean-rx/tools/Makefile (Node is also required)" >&2
    exit 1
fi

usb_command=(node "$usb_process" run)
usb_control_pid=
usb_control() {
    local result=0
    node "$usb_process" "$@" &
    usb_control_pid=$!
    wait "$usb_control_pid" || result=$?
    usb_control_pid=
    return "$result"
}

usb_boot() {
    if usb_control boot 30 "$picotool" "$1" >> "$2" 2>&1; then return 0; fi
    echo "USB boot failed for $1; see $2" >&2
    return 1
}

usb_ready() {
    if usb_control ready 15 "$usb_ready_tool" "$1" >> "$2" 2>&1; then return 0; fi
    echo "USB application readiness failed for $1; see $2" >&2
    return 1
}

usb_pair_ready() {
    usb_ready "$1" "$3" && usb_ready "$2" "$4" &&
        usb_control run 2 "$usb_ready_tool" "$1" >> "$3" 2>&1 &&
        usb_control run 2 "$usb_ready_tool" "$2" >> "$4" 2>&1 || {
            echo "USB pair readiness failed; retain $3 and $4" >&2
            return 1
        }
}

usb_stop() {
    local pid running
    for running in $(jobs -pr); do
        for pid in "${usb_control_pid:-}" "$@"; do
            if [[ $running == "$pid" ]] && kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid"; fi
        done
    done
    for pid in "${usb_control_pid:-}" "$@"; do
        if [[ -n $pid ]]; then wait "$pid" 2>/dev/null || true; fi
    done
}

usb_wait_pair() {
    local first=$1 second=$2 allowed=${3:-0} first_done=0 second_done=0
    usb_first_result=0 usb_second_result=0
    while (( !first_done || !second_done )); do
        if (( !first_done )) && ! kill -0 "$first" 2>/dev/null; then
            wait "$first" || usb_first_result=$?
            first_done=1
            if (( usb_first_result != 0 && usb_first_result != allowed )); then
                echo "USB endpoint $first failed with exit $usb_first_result; stopping pair" >&2
                if (( !second_done )); then usb_stop "$second"; fi
                return 1
            fi
        fi
        if (( !second_done )) && ! kill -0 "$second" 2>/dev/null; then
            wait "$second" || usb_second_result=$?
            second_done=1
            if (( usb_second_result != 0 && usb_second_result != allowed )); then
                echo "USB endpoint $second failed with exit $usb_second_result; stopping pair" >&2
                if (( !first_done )); then usb_stop "$first"; fi
                return 1
            fi
        fi
        if (( !first_done || !second_done )); then sleep 0.05; fi
    done
}
