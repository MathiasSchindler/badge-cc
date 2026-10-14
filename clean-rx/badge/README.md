# Freestanding badge host for clean-rx

This opt-in RP2350 application cold-loads the source-built CYW43439 image,
receives raw 802.11 frames over Function 2, and reports evidence over the
project-owned USB control endpoint. Neither processor links libc, the Pico
SDK, a downloaded vendor executable, NVRAM, or CLM. The controller still
reads its on-chip ROM calibration tables.

This is a bounded radio test, not a WiFi connection manager. It does not
associate, implement a WPA2 handshake, or replace the default networking
targets. Host USB collection uses the repository's existing libusb dependency.

## Build and test

From the repository root:

```sh
make clean-rx-badge
make -C clean-rx/badge test
```

Outputs are under `build/clean-rx/`; the badge image and collector are under
`build/clean-rx/badge/`. The image identity includes the linked controller
binary, host sources, relevant headers, collector and build rules.

The host qualifies gSPI, holds the controller CPU in reset, clears all
512 KiB of controller RAM, downloads and reads back the executable, initializes
the mailbox, and performs the firmware's HT-clock handshake. It requests
promiscuous reception with TX disabled by default. Burst accesses split at
64-byte boundaries, including across backplane windows.

Each request is bound to a fresh nonce and the compiled build identity.
Collection validates the report and every trace page's CRC/binding. This
image selects a 2,048-byte trace; existing applications retain their
256-byte default. Loader stages have a six-million-tick aggregate deadline,
and the badge schedules independent BOOTSEL recovery before USB startup.
Reception lasts 700,000 timer ticks; enabled-TX trials also have a
200,000-tick response window. These timer counts are not independently
calibrated wall-clock or RF timing measurements.

## Authorized physical test

Only run these commands against an explicitly authorized badge. Set `SERIAL`
to its 16-character uppercase BOOTSEL serial.

```sh
PICOTOOL=build/picotool-usb/picotool
SERIAL=YOUR_BADGE_SERIAL

# Preserve the installed program before the first trial.
$PICOTOOL save -p build/clean-rx/badge/original-badge.bin --ser "$SERIAL"

$PICOTOOL load -v -x build/clean-rx/badge/clean_rx.uf2 --ser "$SERIAL"
build/clean-rx/badge/collector "$SERIAL" build/clean-rx/badge/rx.bin 1

# Wait for independent recovery and confirm the same badge is in BOOTSEL.
sleep 9
$PICOTOOL info -a --ser "$SERIAL"

# Check full MPDU FCS and match the configured SSID locally.
build/clean-rx/badge/collector --inspect \
  build/clean-rx/badge/rx.bin ssid-data/ssid.txt
```

The channel argument accepts 1..11. `--inspect` reads only the `SSID:` field;
it does not use or print the password. Binary captures contain received
network identifiers and should remain local.

Two additional collector modes require a new load/execute and recovery:

- `--tx-one-probe`: enables TX and submits one 94-byte, unencrypted, 11 Mbps
  CCK probe request for the first FCS-validated beacon's SSID. Its source MAC
  is fresh and locally administered. It records command completion and
  controller status, then looks for a valid-FCS probe response to that MAC.
  The selected beacon is not necessarily the AP in the credential file;
  inspect the RX evidence first. This is not a traffic generator.
- `--reject-tx`: leaves TX disabled and submits the same command to verify
  rejection. Collector success here requires controller error `0x81`,
  address `0x20018`, ERROR state, and no completed TX command.

`CFG_TX_ENABLE` now gates both explicit TX commands and automatic ACKs in
the controller. With TX enabled, automatic ACKs remain available for frames
addressed to its configured station MAC. A rejected TX command preserves the
ERROR state through cleanup rather than publishing RX_DONE.

Restore the saved program **without executing it**, then confirm BOOTSEL:

```sh
$PICOTOOL load -v build/clean-rx/badge/original-badge.bin --ser "$SERIAL"
$PICOTOOL info -a --ser "$SERIAL"
```

## Physical results, 2026-10-14

One RP2350-A4 badge was tested with serial-bound operations and independent
BOOTSEL checks. These are newly acquired cold-source results, not
vendor-retained initialization.

| Trial | Result |
| --- | --- |
| Initial cold boot | 18,960 executable bytes read back; RX_DONE with no controller error; 10 captures and 10 DMA completions |
| Full-frame/Function-2 test | 9 captures, 9 DMA completions, 9 Function-2 packets; host-validated FCS for the saved SRAM and Function-2 frames; local AP SSID matched on channel 1 |
| Enabled wildcard probe | Command completed; TX marker `0x5555005e`, status `0x04c10022`; no matching AP response in the extended response trial |
| Enabled SSID-directed probe | 18 captures/DMA completions/Function-2 packets; same TX marker/status; no matching AP response |
| Disabled-TX rejection, build `bd682752` | Error `0x81` at `0x20018`, controller ERROR state; no completed TX command |
| Final RX-only repeat, build `bd682752` | 12 captures, 12 DMA completions, 12 Function-2 packets; both saved frames pass host FCS checks; local AP SSID matched; no TX requested |

Evidence binaries and JSON reports are in `build/clean-rx/badge/`, including
`full-ch1.*`, `probe-retry-ch1.*`, `directed-ch1.*`, `reject-ch1.*`, and
`final-rx-ch1.*`. An initial response-window trial hit the shorter collector
deadline; that deadline was corrected to cover the loader's aggregate bound,
and the subsequent trial completed. Failed and successful reports are not
interchangeable.

The original 312,560-byte badge program was backed up, restored with write
verification, and left in BOOTSEL.

**Established:** cold source execution, local-AP beacon reception, independent
FCS verification, DMA/Function-2 transport, explicit TX command execution,
and disabled-TX rejection.

**Not established:** successful over-the-air TX, measured RF power/spectrum
or regulatory qualification, automatic ACK timing/reliability, CCMP,
association, WPA2 connectivity, large-frame transport, or long-run DMA-ring
reliability. A completed TX command is not evidence of AP reception.
