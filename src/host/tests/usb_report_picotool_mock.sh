#!/bin/bash
set -euo pipefail

: "${USB_REPORT_MOCK_DIR:?test directory required}"
printf '%s\n' "$*" >>"$USB_REPORT_MOCK_DIR/calls.txt"
if [[ "${*: -2:1}" != --ser || "${*: -1}" != USBREPORTTESTONLY ]]; then
    echo "Mock refuses any device except USBREPORTTESTONLY" >&2
    exit 1
fi
case "$1" in
    info)
        if [[ "${USB_REPORT_MOCK_MODE:-}" == absent ]]; then
            echo "No accessible RP-series devices in BOOTSEL mode were found." >&2
            exit 1
        fi
        echo "type: RP2350"
        ;;
    load)
        if [[ "$3" == *.uf2 ]]; then
            [[ -f "$3" ]] || exit 1
        elif [[ "$3" == */request.bin && "$7" == 0x2007f000 ]]; then
            cp "$3" "$USB_REPORT_MOCK_DIR/request.bin"
        elif [[ "$3" == */clear.bin && "$7" == 0x2007f100 ]]; then
            [[ -f "$3" ]] || exit 1
        else
            echo "Unexpected mock load request" >&2
            exit 1
        fi
        ;;
    reboot)
        [[ -f "$USB_REPORT_MOCK_DIR/request.bin" ]] || exit 1
        ;;
    save)
        [[ "$3" == 0x2007f100 && "$4" == 0x2007f140 ]] || exit 1
        build/host-tools/usb_report_test --fixture "$USB_REPORT_MOCK_DIR/request.bin" \
            "$5" "${USB_REPORT_MOCK_MODE:-valid}"
        ;;
    *)
        echo "Unexpected mock command" >&2
        exit 1
        ;;
esac
