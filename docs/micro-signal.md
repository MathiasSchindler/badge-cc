# Micro Signal

A standalone button messenger for the GitHub Universe 2026 badge and Pimoroni
Tufty2350. Both devices run the **same UF2**. The application lives in
[src/tools/micro-signal.c](../src/tools/micro-signal.c).

On startup it cold-loads the source-built `clean-rx` firmware onto the CYW43439,
starts continuous reception on **channel 6**, and sends broadcast button events
at the physically verified **11 Mbps CCK** rate. No AP, credentials, IP stack,
Pico SDK, target libc, vendor Wi-Fi executable, or USB connection is required.

**Experimental:** the cold-start timeout has been repaired, but multi-minute
tests still reproduce intermittent Function-2 receive framing failures. One
five-minute run passed; its repeat failed. This is not yet a qualified
indefinitely-running messenger.

## Build and install

Use the repository's normal host C compiler and LLVM ARM tools:

```sh
make micro-signal test-micro-signal
```

The result is `build/universe26/uf2/micro-signal.uf2`. The current build is
**63,488 bytes (62 KiB)**, containing a **31,588-byte** flash image, including
the **20,500-byte** source-built Wi-Fi controller firmware. Size optimization,
LTO and section garbage collection are enabled. The framebuffer uses RAM,
not UF2 payload space.

Put both boards into BOOTSEL, then copy this UF2 to each board's USB drive.
Alternatively, with this checkout's USB-enabled picotool:

```sh
build/picotool-usb/picotool load -v build/universe26/uf2/micro-signal.uf2 --ser 08DA26F86E57F5EA
build/picotool-usb/picotool load -v build/universe26/uf2/micro-signal.uf2 --ser D41777A92225F996
build/picotool-usb/picotool reboot --ser 08DA26F86E57F5EA
build/picotool-usb/picotool reboot --ser D41777A92225F996
```

The normal application deliberately has **no USB interface and no automatic
BOOTSEL timeout**. The USB drives disappear while it runs. It continues without
a USB host, as long as the boards have power.

## Buttons and screen

After `LOADING RADIO`, the screen shows `READY - BUTTONS 1 TO 5`.

| Number in message | Universe label | Tufty2350 label | GPIO |
| --- | --- | --- | ---: |
| 1 | Left | A | 7 |
| 2 | Right | B | 9 |
| 3 | Enter | C | 10 |
| 4 | Up | Up | 11 |
| 5 | Down | Down | 6 |

Press and release a front button. The sending board shows `BUTTON X SENT`;
the other board shows `BUTTON X PRESSED`. Either board can send. `TX` counts
local events whose three radio transmissions completed; `RX` counts unique
accepted peer events. **SENT does not mean DELIVERED**: there is no application
acknowledgment.

Each button is debounced for 20,000 inherited timer ticks. Holding a button
does not repeat it; a debounced release allows another press. Simultaneous
presses enter an eight-event FIFO. If it fills, excess presses are discarded
with an `INPUT QUEUE FULL` warning, rather than generating unbounded traffic.

If the radio receive queue fills, new frames are dropped without overwriting
pending buffers. `RX D n R m` reports dropped radio frames and SPI receive-frame
resynchronizations, respectively. These are separate from the button FIFO.
Invalid SPI lengths or underflow can terminate the affected read frame and
resume reception, with the recovery counted on screen. Three consecutive
resynchronizations without a valid packet exhaust the recovery budget; another
fault stops with `RADIO ERROR`. Malformed SDPCM packet headers remain fatal.

Hold the rear **HOME/BOOT** input until the board returns to BOOTSEL.
RESET alone restarts the installed app. If the app is unresponsive, use the
board's physical BOOTSEL procedure (hold BOOT while resetting or reconnecting).
Radio failures stop further radio activity and show `RADIO ERROR`, followed by
the loader error code, last address and diagnostic value. Codes are:
1 bus, 2 timeout, 3 cancelled, 4 readback, 5 controller, 6 input/ROM query.
RESET retries the cold boot; HOME recovery remains available.

## Radio protocol and limitations

[micro-signal.h](../src/tools/micro-signal.h) defines an experimental,
vendor-specific 802.11 Action frame: 88 MPDU bytes including FCS, preceded by
the six-byte 11 Mbps CCK PLCP header. Its private experimental OUI bytes are
`02:00:00`; the body contains `MSIG`, version 1, button 1-5, a boot-session
token, a 32-bit event sequence, the 64-bit chip identity, and the literal
`BUTTON X PRESSED` payload.

Every press is transmitted three times with bounded spacing and per-board
jitter. Receivers check the FCS and exact protocol shape, reject their own
identity, and suppress repeats/out-of-order events. The four-peer cache uses
sender identity, boot session and sequence, including sequence wrap. The
session token comes from the ROM's boot-random information, allowing a sender
to restart its sequence after reboot without being mistaken for a duplicate.
Evicting a peer loses that peer's duplicate history.

This is **unencrypted and unauthenticated**, broadcasts the chip identity,
and is not replay protection. Anyone nearby can receive or forge events.
Three repeats improve the chance of delivery but do not guarantee it.
It does not associate with an AP or implement Wi-Fi Direct, WPA2 or IP.
Keep both boards on the same channel; other RF rates remain unqualified.

The diagnostic loader's aggregate timeout is renewed only at each new main-loop
operation boundary, never inside a stuck radio transaction. Cold boot and
individual TX commands retain their deadlines, while normal reception is
intended to run indefinitely. Multi-hour soak testing remains outstanding.

## Validation and bounded hardware test

Native tests cover all five button payloads, PLCP/FCS, every single-byte
corruption of the MPDU, malformed lengths and fixed fields, duplicate/self
rejection, sender reboot sessions, sequence wrap, cache eviction, debounce,
held/released buttons, simultaneous presses and timer wrap. The SDIO model
exercises 10,000 producer/consumer steps, ring wrap, full-queue backpressure,
ordered delivery and saturating counters. Loader tests cover the reported
invalid lengths 0 and `0x551`, bounded FIFO resynchronization, stalled/empty
FIFO discrimination, recovery-budget reset, cancellation and bus failures.
These also pass AddressSanitizer and UndefinedBehaviorSanitizer.
The PSM test pins the original microcode image CRC after restoring the old
counter instruction, checks the replacement against an existing long-counter
instruction, and executes the actual flush branches in a bounded native model.
It covers both pending bits, delayed completion and permanent failure.

A separate, larger **test-only** image enables USB telemetry and arms a BOOTSEL
watchdog. USB requests feed that watchdog only within a bounded timer window;
the collector requests BOOTSEL when finished. With no collector it recovers
automatically. The normal production image has neither USB nor this watchdog.

```sh
make micro-signal-self-test
build/picotool-usb/picotool load -v build/universe26/uf2/micro-signal-self-test.uf2 --ser 08DA26F86E57F5EA
build/picotool-usb/picotool load -v build/universe26/uf2/micro-signal-self-test.uf2 --ser D41777A92225F996
build/picotool-usb/picotool reboot --ser 08DA26F86E57F5EA
build/picotool-usb/picotool reboot --ser D41777A92225F996
build/micro-signal/collect 08DA26F86E57F5EA D41777A92225F996 build/micro-signal/paired
```

For a **one-minute sustained-traffic regression**, append `--soak` to the
collector command, or `--soak 300` for five minutes. Explicit durations must be
30..600 seconds. The test image generates further synthetic button events.
The collector requires continuing peer-sequence progress near the end,
rejects five-second sequence stalls after initial startup, checks correct
sender/session bindings, and requires at least 64 published SDIO frames
per device, so a stalled receiver cannot pass merely because it worked early
in the run. Its wall-clock deadline is independent of the badge timer.
The test-only watchdog can be fed for at most 600,000,000 inherited timer
ticks, rather than the previous 60,000,000-tick window.

Only this optional collector requires host libusb development files and
`pkg-config`. It selects both USB devices by their exact serial numbers,
validates the live records' CRCs and chip identities, checks that each received
sender/session matches the other board's current boot, and requests BOOTSEL
on completion. Output files are the prefix plus `-SERIAL.bin`, 64 bytes each.
Redirect stdout to a JSON file as well to retain the extended diagnostics.
These include best-effort live PSM/SHM/RXE snapshots, loader resynchronizations,
first sequence-stall times, and firmware fault details. TX staging failures
instead preserve the full expected and observed 96-byte buffers, their CRCs
and a fresh read's CRC. This extra read is diagnostic only: a mismatch still
aborts before TX, without retrying the write or sending questionable bytes.
Do not press physical buttons during this deterministic test.

On the attached Universe badge and Tufty2350, this test verified **both
directions**: each completed six frame transmissions for synthetic buttons
1 and 5, and each accepted exactly two matching peer events, with sequence 2
and button 5 last. Repeated frames did not cause duplicate deliveries; neither
device reported radio or USB errors. The same application framing, cold loader,
TX, RX and display update paths run in the test image; only USB instrumentation,
automatic synthetic presses and recovery scheduling are test-only.

Actual physical button operation and visual TFT appearance still need the
owner's extended check after the receive-path repair. The owner confirmed
initial button/display operation on both boards before reporting the old
build's failure after about ten seconds.

### Sustained-RX repair

The old SDIO TX queue reset its DMA engine every 31 published frames, even if
the host had not drained the queue. DMA startup also enabled the engine before
clearing its old producer pointer. The repaired controller uses an end-of-table
descriptor and a 32-slot circular queue, checks the hardware consumer before
reusing a buffer, and clears the producer pointer before enabling DMA.
`RX_ACTIVE` is published only after initialization and MAC enable finish.
Malformed-frame recovery uses the SPI read-frame terminate operation, not a
reset of a live DMA queue.

Two fresh paired cold-boot tests passed after the repair: first 30 seconds,
then **60.036 seconds**. In the minute-long run:

| Device | TX frames | Unique peer events | SDIO frames published | Drops | Resyncs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Universe | 252 | 86 | 1,086 | 0 | 0 |
| Tufty2350 | 258 | 84 | 692 | 0 | 0 |

Neither device reported radio or USB errors. Reception continued near the end
of the minute, across at least 33 and 21 full SDIO ring traversals respectively.
The earlier short test was insufficient to qualify sustained reception.

### Further startup and multi-minute investigation

Twenty paired baseline cold starts reproduced two startup failures across
40 radio boots, one on each board. These reported firmware `0x71`, reason 3,
PSM state 15, before READY (application error 5, not receive error 6).
The frozen PSM snapshot and immediate ARM probe both read RXCTL `0x0804`;
a probe after a bounded 2,000,000-controller-tick wait read `0x0800`.
The pending flush bit really cleared later: the old 255-iteration PSM wait
was prematurely declaring a fault.

PC 81 now loads the same signed-immediate `-1` / 65,535-iteration countdown
already used at PC 105. Instruction count, branch destinations, completion
predicates, fault branch and outer ARM deadline are unchanged. The startup
path does not automatically retry failed boots. Two subsequent batches of
20 paired starts passed: **80/80 controlled radio boots** after this change.
This is a measured improvement, not proof that every future boot must pass.

Longer tests exposed additional problems that the old minute-long tests missed:

| Build / trial | Elapsed | Result |
| --- | ---: | --- |
| PSM fix, original bulk SPI access | 98.166 s | Universe TX staging readback error 4; Tufty's peer sequence had stopped earlier |
| Added full staging/live PSM diagnostics | 159.824 s | Universe staging mismatch reproduced; a fresh read returned the same bad CRC |
| Explicit SPI word-access flag, first run | 300.023 s | Passed both directions; no errors, drops, resyncs or five-second stalls |
| Same build, repeat after 40 more cold radio boots | 124.895 s | Tufty receive error 6; malformed SDPCM first word `0xfc1c013f` |
| App-only slower SPI experiment | 65.667 s | Receive error 6 again, with more resyncs and queue drops; experiment reverted |

The bulk backplane helper had omitted the `0x8000` SPI word-access flag used
by scalar I/O and the existing firmware loader. Reads and writes now set it;
native tests enforce it across 64-byte/window boundaries and interleaved
staging/mailbox transfers. This is an API correction, not evidence that the
remaining receive problem is solved.

In the successful five-minute run:

| Device | TX frames | Unique peer events | SDIO frames published | Drops | Resyncs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Universe | 1,383 | 469 | 5,271 | 0 | 0 |
| Tufty2350 | 1,405 | 459 | 4,160 | 0 | 0 |

The repeat still failed despite no five-second peer stall beforehand.
Its malformed header had an invalid length/complement pair; strict validation
correctly rejected it. Increasing recovery budgets or silently dropping such
headers is not a fix. The remaining fault is localized to SDIOD/SPI Function-2
delivery, but its exact cause is not established. DMA/FIFO ownership and
framing need further investigation. Multi-hour qualification remains open.

Evidence is under `build/micro-signal/`: `startup-baseline-*`,
`startup-late-*`, `startup-fixed-*`, `receive-fixed-five-minutes*`,
`receive-autopsy-five-minutes*`, `word-access-cold-*`,
`word-access-five-minutes*`, `word-access-repeat-five-minutes*`, and
`conservative-five-minutes*`. Failed runs are retained and are not counted
as successful qualification.
