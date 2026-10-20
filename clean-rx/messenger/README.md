# Direct HT20 button messenger

A standalone, dependency-free C/nolibc RP2350 application for two Universe
badges. Both cold-load the current source-built CYW43439 controller.
No controller changes, vendor executable, AP, SSID or credentials are needed.
The host-side diagnostic uses the repository's existing libusb dependency;
neither processor links libc or an SDK.

This is an **experimental direct radio link**, not an HT-associated station
or a completed D2 milestone. All messages and application acknowledgements
use HT20 mixed-mode, one stream, BCC, 800-ns GI, **channel 1 / MCS0**.
The generic command-12 API sends short, unaggregated broadcast Data frames.
Its raw path does not provide a qualified contention/backoff implementation.
Use this low-duty-cycle lab demo only where that is appropriate.

## Using the badges

Install role 1 on badge A and role 2 on badge B. They start automatically
when powered; USB control or an attached computer is not needed afterward.
They discover the other role through periodic state messages. This is not
authenticated pairing: another copy of this demo on the same channel can
interfere.

The five front controls are LEFT, RIGHT, ENTER, UP and DOWN
(GPIOs 7, 9, 10, 11 and 6). The existing shared 20-ms debounce helper
registers both presses and releases, including simultaneous buttons.

The TFT shows:

- Local and remote held-button states.
- **LAST REMOTE**, which retains the most recently received rising-edge
  button mask even after release.
- Received messages and acknowledged outgoing messages, including heartbeats.
- Failed/unconfirmed messages, queue drops, radio boot count (including
  the initial boot) and the last radio fault.
- Peer online status, expiring after six seconds without a valid peer message.

Traffic is **unencrypted and unauthenticated**. Do not send sensitive data.
It is neither IP networking nor a general multi-peer protocol.

## Build and install

From the repository root, with LLVM tools on `PATH`:

```sh
make -C clean-rx/messenger pair test
```

The default build folder is `build/clean-rx/messenger/`.
Both images embed the same controller built with
`CLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1`. Aggregate observer/advance,
station association and hardware-response experiments are not enabled.
The linker checks that each RP2350 image has no unresolved symbols.

With both badges in BOOTSEL, replace the serials below as necessary:

```sh
build/picotool-usb/picotool load -u -v \
  build/clean-rx/messenger/role1/messenger.uf2 --ser 08DA26F86E57F5EA
build/picotool-usb/picotool reboot -a --ser 08DA26F86E57F5EA
build/picotool-usb/picotool load -u -v \
  build/clean-rx/messenger/role2/messenger.uf2 --ser 9BBF88DD17D66E7D
build/picotool-usb/picotool reboot -a --ser 9BBF88DD17D66E7D
```

Save any software you want to preserve before flashing. For the recorded
installation, the user explicitly permitted replacing the installed software
and leaving this demo installed.

Optional diagnostics:

```sh
build/clean-rx/messenger/collector status 08DA26F86E57F5EA
build/clean-rx/messenger/collector press 08DA26F86E57F5EA 4
build/clean-rx/messenger/collector pair-test 08DA26F86E57F5EA 9BBF88DD17D66E7D
build/clean-rx/messenger/collector bootsel 08DA26F86E57F5EA
```

`press` injects a button-state **message**, not a simulated electrical GPIO.
Masks are LEFT=1, RIGHT=2, ENTER=4, UP=8, DOWN=16, released=0.
It does not change physical button readings. The next heartbeat reports
the actual held state again.

`pair-test` verifies 16 alternating messages followed by four pairs of
simultaneously queued opposite-direction messages: **24 exchanges**, 12
per direction. Fresh tokens bind the source event, received full-frame
FCS/HT metadata and application acknowledgement. It fails rather than
turning a timed-out exchange into a pass. It requires matching installed
build IDs and different badge roles.

USB is used only to request diagnostics or enqueue test events. All message
delivery and acknowledgement travel over HT20. The USB test does not
forward packets between the badges.

## Protocol and failure limits

An MPDU is 80 bytes, including its software FCS: a three-address broadcast
Data header, LLC/SNAP with experimental EtherType `0x88b5`, a versioned
button-state/ACK payload and canonical zero padding.
The payload carries role, a 32-bit ROM-derived boot session, sequence,
button bitmap, event origin and optional USB test token.
The controller validates the descriptor, HT-SIG and MPDU before transmission.
RX requires matching HT20 metadata plus a canonical complete body and FCS.

An eight-event queue feeds one outstanding state message. Retransmissions
retain the event identity; the receiver suppresses duplicates within its
current session history but re-acknowledges them. ACKs bind the sender's boot
session, sequence, buttons, origin and token. These are delayed application
messages, **not IEEE 802.11 ACK/SIFS or Block ACK responses**.
Each message has at most six attempts with role/sequence-dependent intervals.
Heartbeats run approximately every three seconds when the queue is idle.

`FAILED` means delivery was **not confirmed**; a receiver may have delivered
a message whose ACK was lost. A full queue explicitly increments `Q DROPS`.
Events are not durable across RP2350 reboots. This does not promise reliable
or exactly-once delivery across receiver resets.

Bus/controller errors power-cycle and reload the radio after a one-second
pause. Fifteen seconds without valid peer traffic also requests a full
radio restart. These application-level restarts are visible and counted;
they do not repair or qualify the underlying RXE problem. Fault code 2
includes timeout/quiet-timeout cases. The last fault remains visible after
recovery. A serviced eight-second watchdog enters BOOTSEL if the RP2350
stops making progress; the USB command can also request BOOTSEL.

## Recorded hardware validation, 2026-10-20

Evidence is retained under `build/clean-rx/messenger/`:

- ASan/UBSan tests: 128 both-role/button-mask/timer-wrap round trips,
  descriptor validation, byte corruption, stale/mismatched ACKs, lost-ACK
  duplicate suppression, retry exhaustion, queue bounds, reboot sessions,
  debounce/release, simultaneous outstanding messages and persistent press
  history. The existing controller tests also pass in the exact app profile.
- Initial app: **16/16** fresh bidirectional exchanges passed.
- Final app's first campaign **failed** before its first test event was
  confirmed: badge B initially received no peer messages. The failure,
  nine accumulated unconfirmed messages (five A, four B), and B's quiet-timeout
  radio restart are retained. Do not discard this startup failure.
- After recovery: **24/24**, then **24/24**, complete sequential/simultaneous
  exchanges passed, with maximum observed per-case completion times of
  2,087 and 3,120 ms respectively. The original failed campaign is separate.
- The user physically tested buttons in both directions and confirmed both
  screens were readable. Both USB snapshots independently show physical
  (`origin=1`) press/release traffic rather than injected test messages.
- Subsequent unattended heartbeats continued in both directions, including
  an additional one-minute observation with no new failures or radio boots.
  Final recorded boot counts were A=1 and B=2; the earlier failures remained
  visible at A=5 and B=4, with zero queue drops.

Both badges were verified-flashed and **left running the demo**, as requested.
The retained RF results demonstrate this small application's operation;
they do not qualify sustained Wi-Fi reliability, aggregation, negotiated
HT association, protection or MAC timing. See the
[HT20 implementation record](../dev-doc/HT20_IMPLEMENTATION.md).
