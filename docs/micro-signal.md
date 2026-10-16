# Micro Signal

A standalone button messenger for the GitHub Universe 2026 badge and Pimoroni
Tufty2350. Both devices run the **same UF2**. The application lives in
[src/tools/micro-signal.c](../src/tools/micro-signal.c).

On startup it cold-loads the source-built `clean-rx` firmware onto the CYW43439,
starts continuous reception on **channel 6**, and sends broadcast button events
at the physically verified **11 Mbps CCK** rate. No AP, credentials, IP stack,
Pico SDK, target libc, vendor Wi-Fi executable, or USB connection is required.

**Experimental:** the cold-start timeout has been repaired, but multi-minute
tests still reproduce intermittent Function-2 receive framing failures.
An RF-free transport test now reproduces failures without radio operation.
Two recent five-minute runs on two Universe badges passed, but do not resolve
the transport failure. This is not yet a qualified
indefinitely-running messenger.

## Build and install

Use the repository's normal host C compiler and LLVM ARM tools:

```sh
make micro-signal test-micro-signal
```

The result is `build/universe26/uf2/micro-signal.uf2`. The current build is
**63,488 bytes (62 KiB)**, containing a **31,572-byte** flash image, including
the **20,484-byte** source-built Wi-Fi controller firmware. Size optimization,
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

### Two-Universe investigation and RF-free reproducer

Further testing used Universe serials `08DA26F86E57F5EA` and
`9BBF88DD17D66E7D`; the latter replaces the Tufty in this pair. The original
program from the second badge is preserved separately as
`build/clean-rx/badge/original-universe-9BBF88DD17D66E7D.bin`.

The new transport-only profile cold-loads a separate controller executable.
It performs PMU/HT and SDIO initialization, but **does not initialize D11/PHY
or transmit radio frames**. Mailbox command 7 is compiled only into that test
controller; it can publish 1..16 deterministic packets through the production
SDIO sender. The RP2350 checks the SDPCM sequence, exact length and every
payload byte, and verifies a 96-byte backplane staging write/readback between
batches. The current host requests four packets per batch, exposing the stall
in the first batch instead of waiting for the old variable-burst schedule.
Payload lengths cover 8..504 bytes. The TFT is initialized once, not
updated per packet. The production image contains none of this test protocol.

```sh
make micro-signal-transport-test
build/picotool-usb/picotool load -v build/universe26/uf2/micro-signal-transport-test.uf2 --ser 08DA26F86E57F5EA
build/picotool-usb/picotool load -v build/universe26/uf2/micro-signal-transport-test.uf2 --ser 9BBF88DD17D66E7D
build/picotool-usb/picotool reboot --ser 08DA26F86E57F5EA
build/picotool-usb/picotool reboot --ser 9BBF88DD17D66E7D
build/micro-signal/collect 08DA26F86E57F5EA 9BBF88DD17D66E7D build/micro-signal/sdio-isolation --transport 60
```

As with the radio self-test, durations are 30..600 seconds and cleanup requests
BOOTSEL. A passing transport trial requires at least 1,000 validated packets
per badge, continuing progress and correct USB identity/CRC bindings. It sets
`transport_confirmed`, never `bidirectional_button_event_confirmed`. In this
mode the legacy `tx_frames`/`rx_events` JSON counters mean completed/accepted
synthetic transport packets, not radio transmissions or button events.
**The current four-packet-burst profile is a failing reproducer, not an expected
passing acceptance test.**

Diagnostic traces now preserve the full failed receive buffer, advertised SPI
status, fault-time DMA/FIFO registers, and the last 13 receive attempts.
The strict transport loader stops **before** read-frame termination or interrupt
acknowledgement, preserving the original fault rather than modifying it during
recovery. Normal production recovery is unchanged. Native tests verify both
profiles and all 497 synthetic payload lengths, with fixed payload test vectors.

Important observations, including earlier exploratory profiles:

| Trial | Result |
| --- | --- |
| RF-free, single packet per batch, original DMA control | Failed after 20.950 s and 6,742 accepted packets; packet 6,743 had a valid header but its final four payload bytes were zero instead of the expected data |
| Same captured fault | DMA status 0 `0x30000170` (stopped), status 1 `0x20000170` (data FIFO underrun), SDIO underrun interrupt; zero previous resynchronizations |
| RF-free, strict variable bursts, before any recovery writes | Failed after 6.258 s and 927 accepted packets; SDIO reported FIFO data but SPI exposed no packet |
| Same pristine stall | DMA status 0 `0x100001e0` (active), producer `0x10`, status 1 zero (no error); queued SRAM headers for packets 928 and 929 were intact across the ring's 31-to-0 boundary |
| Final retained profile, original DMA control | Failed after 6.383 s and 998 accepted packets; DMA active at descriptor 5, producer at 9, active descriptor 7, no DMA error, FIFO data available but no SPI packet |

The pristine stall repeated at the same accepted-packet count under multiple
experimental configurations. It has **no DMA underrun yet**, so underrun is
not established as the initiating fault in every failure. Conversely, the
single-packet corruption captured an underrun without preceding recovery.
The final retained profile reproduces the stall at a different packet count
and away from the end-of-table descriptor; an immediate 31-to-0 ring crossing
is therefore not required.
This isolates a real failure in the controller/host SDIO/SPI transport without
requiring RF, PSM reception, button traffic or per-packet display updates.
It does not yet identify a single root cause or prove that every earlier OTA
failure has exactly the same cause.

Watermark programming, alternate DMA burst/address-extension controls, forced
HT, exact rather than rounded DMA byte counts, pre-poll interrupt acknowledgement,
and split Function-2 reads did not establish a fix. Split reads produced
read-out-of-sync and are not supported by this path. **All these experimental
behavior changes were reverted**; normal DMA control remains `0x000a0801`.
The retained changes are the isolated test profile, fault evidence and tests.

Two further real-radio trials with the original transport settings passed:

| Trial | Duration | Universe A: TX / unique RX / F2 | Universe B: TX / unique RX / F2 |
| --- | ---: | ---: | ---: |
| `universe-pair-capture` | 300.001 s | 1,383 / 466 / 6,690 | 1,398 / 461 / 6,728 |
| `universe-pair-repeat` | 300.018 s | 1,380 / 466 / 6,590 | 1,396 / 460 / 6,658 |

Both trials had zero errors, drops and resynchronizations. These successes do
not invalidate the failing RF-free reproducer or qualify indefinite operation.
There is no demonstrated universal 125-second timer limit. The unresolved work
is to distinguish short-frame/FIFO framing from queue publication and ring-wrap
ownership using the preserved pre-recovery state.

After testing, the ordinary 63,488-byte messenger UF2 was reinstalled on both
Universe badges. Separate 31,572-byte flash readbacks matched the built binary
byte for byte. Both were left in BOOTSEL, not running the stress test.

Additional local evidence prefixes in `build/micro-signal/` are
`sdio-single-baseline`, `sdio-single-watermark`, `sdio-single-dma32`,
`sdio-bursts-dma32`, `sdio-bursts-forced-ht`, `sdio-first-resync`,
`sdio-exact-length`, `sdio-irq-ack`, `sdio-pristine-stall` and `sdio-chunk64`.
The final retained-profile reproduction is `sdio-final-reproducer`.
Captures may contain ambient radio frames and should remain local.

### Auditing transport assumptions

A follow-up audit reduced the stall to **three accepted packets out of a
single four-packet request**. It does not require hundreds of packets, a
second batch, ring wrap, descriptor reuse, or a long-running timer.
The retained default transport profile now reproduces this smaller case.

The old variable schedule sent single packets through sequence 497, then
pairs through 995. Its first three-packet batch completed at sequence 998;
the next batch stalled. This explains why changing payload lengths still
failed after 998 deliveries: that number identified the queueing schedule,
not a special payload or cumulative byte offset.

| Discriminating trial | Accepted before fault | Observation |
| --- | ---: | --- |
| Fixed 106-byte SDPCM, original variable bursts | 998 | Same trigger as varied lengths |
| Fixed 108-byte SDPCM, original variable bursts | 998 | Four-byte alignment did not remove it |
| Fixed 108-byte SDPCM, three packets from startup | 3 | Failure moved to the first few packets |
| Publish each three-descriptor batch with one pointer update | 3 | Individual producer-doorbell writes were not required |
| Remove staging write/readback between batches | 3 | Interleaved staging RAM traffic was not required |
| GPIO-clocked Function-2 reads instead of PIO reads | 3 | Reproduced with an independent, slower packet-read implementation |
| Original varied lengths, one six-packet batch | 3 | Restarting a second batch was not required |
| Original varied lengths, one four-packet batch | 3 | Smallest single-batch failure tested; current retained profile |

The GPIO experiment replaced only packet reads; Function-0/1 register access
still used PIO. The staging experiment retained mailbox/register operations.
Neither result excludes every possible SPI timing or register-access problem.

In the four-packet trial, DMA status 0 was `0x20000020` (idle, current
descriptor 2), producer `0x40` (four descriptors posted), and status 1
`0x40` (no DMA error, active descriptor 4). SDIO still reported FIFO data,
but SPI reported no packet. This is not a DMA-stopped/underrun state.
The six-packet variant instead remained active at descriptor 2, with active
descriptor 4 and producer 6. Both preserve the state before recovery.

Fault-time readback verified the queued fourth packet's SDPCM header and the
first six descriptors. The six-descriptor varied-length capture had valid descriptor
byte counts 64, 100, 140, 176, 212 and 248, SOF/EOF flags, and the expected
separate buffer addresses. No descriptor had yet been reused or reached EOT.
This rules out overwrite-on-ring-wrap as a necessary explanation of this
particular failure; it does not qualify every ring ownership case.

The collector now emits `transport_core_N`, `transport_slot3_N`, and
`transport_ring_prefix_N` alongside `transport_history_N`. The core array
contains core control/status, CMD53 read count, read-out-of-sync count,
read-wait count, read-termination count, SDIO clock status and ChipCommon clock
status. The slot-3 array is the first 32 bytes of the fourth buffer; the ring
prefix contains the first six 16-byte descriptors. These fixed prefixes are
especially useful for the new startup reproducer, not a dynamic identification
of the next descriptor in every possible long run. Transport output no longer
mislabels those bytes as PSM snapshots.

The audit also checked public contracts rather than changing registers blindly:

- The [public SPI driver](https://github.com/georgerobotics/cyw43-driver/blob/main/src/cyw43_spi.c)
  requests the advertised byte count and clocks a four-byte-rounded transfer.
  Native loader tests now enforce the exact command and read shape for short,
  odd, aligned, application-sized and maximum-sized packets.
- [Linux DMA reclamation](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmsmac/dma.c)
  uses STATUS0's current-descriptor pointer for transmitted-buffer reclamation.
  Switching to STATUS1's active/prefetch pointer without qualification would
  not be a justified ownership fix.
- A four-byte Function-1 read-frame count was **not** evidence of a required
  SPI trailer. Requesting an extra word beyond the advertised packet produced
  read-out-of-sync and DMA FIFO underrun after the first delivery. This also
  demonstrates why an underrun alone does not prove producer starvation.
- Adding eight-byte DMA padding produced a zero-length advertised packet after
  the first delivery. Adding descriptor completion interrupts did not prevent
  the three-packet stall. Neither behavior change was retained.
- FIFO diagnostic-pointer reads returned zero and were not interpreted as a
  verified empty-FIFO measurement. Those experimental accesses were removed.

**No production transport behavior was changed by this follow-up audit.**
All experimental fixed lengths, alternate clocks, padding, publication order
and completion flags were reverted. The smaller failing profile, exact read
tests and additional read-only fault evidence remain. A queue-depth/FIFO
framing or endpoint-completion problem is now a better target than ring wrap,
but its root cause and a sustained-reliability repair are still unproven.

Local evidence prefixes are `assumptions-fixed106`, `assumptions-fixed108`,
`assumptions-burst3`, `assumptions-atomic3`, `assumptions-no-staging`,
`assumptions-bitbang-f2`, `assumptions-extra-word`, `assumptions-ioc`,
`assumptions-dma-pad8`, `assumptions-burst3-descriptors`,
`assumptions-minimal-variable`, `assumptions-six-at-once`, and
`assumptions-four-at-once`.

### Size and producer-pacing experiments

Further RF-free trials used four packets per request on both Universe badges.
Sizes below are **whole SDPCM packets**, not payload lengths. Fixed sizes were
temporary experiments; the retained profiles use all 497 payload lengths.

| SDPCM size / producer | Result on both badges |
| --- | --- |
| 32 bytes, unpaced | Stalled after two accepted packets |
| 64, 128 or 192 bytes, unpaced | Stalled after three accepted packets |
| 252, 256 or 512 bytes, unpaced | Passed separate 30-second trials with thousands of exact packets |
| Varied lengths, 500,000 controller ticks after each publication | Passed 60.017 seconds; 8,552 / 8,559 accepted packets |
| Retained paced build, varied lengths | Passed 300.052 seconds; 45,083 / 45,112 accepted packets, no errors, drops, resyncs or five-second progress stalls |
| Retained unpaced build, immediately afterward | Both accepted only three packets; collector stopped after 4.184 seconds with the original stalled DMA/FIFO state |

There is therefore **no universal three-packet capacity limit**, and neither
four-byte nor 64-byte alignment alone explains the stall. Pacing demonstrates
that the short packet formats can be delivered; it is not a qualified repair
and does not establish which FIFO/endpoint interaction is wrong.

Additional negative controls:

- A forced raw Function-2 read after the missing advertisement did not recover
  the fourth packet; it returned a few garbage bytes followed by zeros and
  set data-unavailable/underflow status. A fully buffered packet behind a stale
  availability flag is not established.
- Moving the frame buffers to `0x40000` reproduced the three-packet stall.
- Limiting publication using DMA consumer pointers did not establish host
  consumption credits. A two-pending gate still stalled; a one-pending gate
  deadlocked after posting only three descriptors, a different failure from
  the original four-posted case.
- Forcing HT in the SDIOD core's own clock-control register stuck on readback
  (`0x000b0040` to `0x000b0042`) but still stalled after three deliveries.
- Watermark 32 read back correctly, but the attempted separate device-control
  enable bit `0x10` did **not** stick. That experiment explicitly failed its
  readback check before publishing packets; it must not be counted as a
  successfully enabled watermark configuration or a transport trial.
- Public DMA header definitions encode prefetch control zero as **zero**
  prefetch, not four descriptors. Changing that setting based on the latter
  assumption would not be justified.

All those behavior experiments were reverted. The normal controller, loader
and messenger retain their original production settings.

For repeatable comparisons, there are now two isolated build targets:

```sh
make micro-signal-transport-test micro-signal-transport-paced-test
```

The first remains the unpaced failing reproducer. The second builds
`build/universe26/uf2/micro-signal-transport-paced-test.uf2` with a separate
controller object directory and a bounded 500,000-controller-tick delay after
each packet publication, including the last packet in a request. It performs
the same exact payload checks and staging transfers. Use the same flash and
`collect ... --transport SECONDS` commands shown above, substituting the paced
UF2 when needed. **Neither profile initializes or tests the radio.**

The controller reports its actual pacing interval in the test-only mailbox
substage field. The host copies it into USB evidence word 12; the collector
emits `transport_delay_ticks_N` and rejects unknown or mismatched profiles.
Zero means unpaced, 500,000 means paced, and `0xffffffff` means the host has
not yet obtained a profile. A paced pass must not be reported as an unpaced
transport fix.

Pacing also exposed and corrected a test-harness assumption: the last packet
can arrive **before** the controller changes its request command back to 2.
The host now allows its own request to remain pending for a bounded
100,000-RP2350-tick completion wait, rejecting unrelated commands and timing
out explicitly. Previously this legitimate ordering produced a false failure.
This correction affects only the transport test, not the production app.

Local evidence prefixes are `fourth-raw-read`, `fourth-relocated-buffers`,
`fourth-small32`, `fourth-aligned64`, `fourth-aligned128`,
`fourth-aligned192`, `fourth-boundary252`, `fourth-aligned256`,
`fourth-large512`, `fourth-two-descriptor-gate`,
`fourth-one-descriptor-gate`, `fourth-paced32`, `fourth-paced-variable`,
`fourth-watermark-enabled`, `fourth-sdiod-forceht`, `fourth-paced-retained`
and `fourth-unpaced-retained`.

Both transport builds, ordinary production and the radio self-test build
successfully. Native messenger/PSM tests, normal and strict loader tests, and
ASan/UBSan runs of the messenger and both loader profiles pass. The ordinary
31,572-byte production binary remains byte-identical to the pre-investigation
reference. Its 63,488-byte UF2 was restored on both exact Universe serials;
independent flash readbacks in `fourth-production-SERIAL.bin` matched the
built binary. Both badges were left in BOOTSEL. The paced firmware is not
installed as a production workaround.

### Separating queue stalls from tail-word underruns

Further controlled tests distinguish two failure modes; they do not yet prove
two independent hardware root causes.

| Comparison | Observation |
| --- | --- |
| Paced producer, host waits for command completion before draining any packets | Same three-of-four stall, despite all publications being spaced |
| Unpaced publications, 500,000-tick backoff only in the controller's idle polling loop | Same three-of-four stall |
| Two outstanding packets, using real host acknowledgements instead of DMA pointers | Underrun after 523 accepted packets |
| One outstanding packet, varied lengths, no sleeps | Passed 60.017 s and 36,060 / 36,437 packets; a longer run failed at 78.849 s after 47,067 accepted packets |
| Same one-packet window, immediate post-read error checks | Failed at 129.327 s; packet 78,311 had a valid 520-byte header but its final four bytes were zero |
| Fixed 520-byte packets, one-packet window | Passed 60.023 s with 21,867 / 22,106 accepted packets |

Pacing is therefore not simply a required interval between publications.
Real host credits remove the immediate multi-packet trigger, but **one packet
outstanding alone is not a sustained-reliability fix**. The long-run failures
also demonstrate why a single passing minute is insufficient.

The strict loader previously checked underflow only when packet availability
was also asserted, and did not inspect status immediately after a packet read.
It now stops on underflow even without an advertised packet, and checks status
again immediately after the physical read, before returning a packet to the
application. This catches the error before a test host acknowledges that packet.
It still performs no termination or interrupt-acknowledgement recovery writes.
The final profile retains the original advertised status in `rx_status` and
the failing post-read status in `actual` (trace bytes 48 and 56). Earlier
exploratory captures may contain the post-read status in both fields.
Normal production receive/recovery behavior is unchanged.

#### A fast reproducer for the corrupt tail

Both the earlier packet 6,743 underrun and the new packet 78,311 underrun
occurred on a **483-to-520-byte SDPCM transition**. Alternating just these two
sizes, with a genuine one-packet host-credit window and no sleeps, makes the
tail corruption much quicker to reproduce:

| DMA burst / trial | Result |
| --- | --- |
| 64 bytes, first alternating trial | A accepted 365 packets; packet 366 lost its final four bytes |
| 32 bytes, first comparison | Passed 60.005 s; 22,952 / 22,957 accepted packets |
| 64 bytes, repeated with retained profile | B accepted 269 packets; packet 270 had the same fault |
| 32 bytes, retained profile | Passed 300.045 s; 125,696 / 126,944 accepted packets; no errors, drops, resyncs or five-second stalls |
| 64 bytes, final-source repeat | B accepted 685 packets before the same fault |
| 32 bytes, final-source smoke test | Passed 30.021 s; 11,137 / 11,258 accepted packets |
| 64 bytes, split-status capture check | B accepted 1,261 packets; packet 1,262 lost the same final four bytes |

The actual DMA-control readbacks are `0x000a0801` for 64-byte bursts and
`0x00060801` for 32-byte bursts. All other controller settings and the packet
shapes/window are identical between these two retained profiles.

Fault-time backplane reads checked the complete intended source packet in
controller SRAM. For packet 366, the independently generated expected CRC and
SRAM CRC were both `f3278836`, but the received CRC was `cca4c352`. The source
tail was `f17f74dcb42403272489391194bfa9b1`; the received tail was
`f17f74dcb42403272489391100000000`. DMA reported data FIFO underrun, with no
previous receive resynchronization. This localizes the observed data loss
after preparation of the source buffer, rather than to the payload generator.
The exact silicon/clock-domain mechanism is still unproven.
All four alternating-pattern corruption captures used ring slot 13, buffer
`0x33600`. The pattern period and ring positions are correlated, so this is
not proof of a bad RAM location. Varying buffer placement and initial FIFO
phase remains a useful next discriminator.

**The 32-byte setting is not a general transport fix.** Separate varied-length,
four-at-once trials still stalled after three packets with both 32-byte
(`0x00060801`) and 16-byte (`0x00020801`) bursts. Those experimental changes to
the default profile were reverted. Production retains `0x000a0801`; the
32-byte comparison is isolated to its explicitly named test build.

#### Retained comparison profiles and evidence

```sh
make micro-signal-transport-window-test micro-signal-transport-window32-test
```

Both new UF2s use the alternating 483/520-byte pattern and a one-packet window.
The first is a **failing corruption reproducer** with 64-byte bursts. The second
uses 32-byte bursts for comparison. The original transport and paced targets
continue to use all 497 payload lengths with no host-credit limit. All four
profiles are RF-free and use the same `collect ... --transport SECONDS` command.

The test-only controller reports its window and pattern in otherwise unused
calibration words. The host validates them, and acknowledges a packet only
after its complete payload has passed validation. The controller's credit wait
is bounded to 20,000,000 polling iterations and explicitly reports firmware
error `0x8a` on exhaustion. Compile-time assertions bind the acknowledgement,
window and pattern fields to mailbox offsets 36, 40 and 44; production mailbox
layout/semantics are unchanged.

Additional JSON fields identify the actual comparison:

- `transport_window_N`: zero means unrestricted, one means stop-and-wait.
- `transport_pattern_N`: zero means varied; one means alternating 483/520-byte
  whole packets (465/502-byte payloads).
- `transport_dma_control_N`: the actual core register readback.

The collector rejects mismatched window, pattern, pacing or DMA profiles.
USB evidence words 8, 9 and 10 carry pattern, DMA control and window,
respectively, only in transport mode.

`transport_last_buffer_N` uses the formerly spare trace bytes 96..127.
Its eight words contain the last posted buffer's address and DMA byte count,
SRAM CRC, received-buffer CRC, and the SRAM tail's final 16 bytes. Capture
validates the descriptor's buffer address and bounds before reading it.
These values are valid only when the diagnostic capture-error word is zero.
The last posted buffer is the faulting packet in the one-packet comparison,
but is not necessarily the packet being received in an unrestricted queue.
A zero receive buffer from a no-packet timeout must not itself be interpreted
as proof of corrupted wire data.

Native tests cover window limits 0..16, wraparound and future acknowledgements,
both payload patterns, and strict pre-/post-read faults, including a vanished
availability flag and an all-ones bus response. Normal/strict loader tests,
ASan/UBSan checks and all four transport builds pass.

Local evidence prefixes are `fifo-paced-deferred-drain`,
`fifo-host-credit2-corrected`, `fifo-host-credit1`, `fifo-idle-backoff`,
`fifo-window-retained`, `fifo-postread-unpaced`, `fifo-window-postread`,
`fifo-fixed520-window`, `fifo-alternating483-520`, `fifo-alternating-dma32`,
`fifo-transition-retained64`, `fifo-transition-retained32`,
`fifo-unpaced-dma32`, `fifo-unpaced-dma16`, `fifo-final64`, `fifo-final32`
and `fifo-status-split64`.
The preliminary `fifo-host-credit2` trial used a wrong test-only acknowledgement
offset and is excluded from hardware conclusions.

Afterward, both exact Universe serials received the unchanged production
messenger. Independent 31,572-byte flash readbacks in
`fifo-production-SERIAL.bin` matched the built binary and the preceding
production reference. Both were left in BOOTSEL. The normal UF2 remains
63,488 bytes; neither the host-credit protocol nor the experimental burst
setting was added to production.
