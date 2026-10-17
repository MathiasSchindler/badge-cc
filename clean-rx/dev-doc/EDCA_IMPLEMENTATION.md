# Bounded EDCA implementation and qualification

This is development evidence, not a claim that D2 is complete.
The current interface is documented in [HT20](../doc/HT20.md).

## Implementation scope

The controller accepts four WMM-format AC records through default-off
`CLEAN_RX_EDCA=1` command 15. The beacon parser and controller share
`clean_edca_decode` in [mac.c](../src/mac.c). Parsing accepts the WMM
exponent field range; execution additionally rejects ECWmax above 10
because the existing PSM counter guard permits at most 1,023 slots.
Malformed record sets do not partially replace their output.

Four contention contexts retain their own CWmin/CWmax, random draw, slots
and AIFSN. TID 0..7 selects BE/BK/BK/BE/VI/VI/VO/VO. Categories requiring
admission are downgraded through VO, VI, BE and BK; no admitted category
means explicit failure, not a fabricated success.

There is one outstanding controller TX, not four simultaneously queued
ACs. Consequently this is not a general four-queue EDCA scheduler with
virtual-collision arbitration. It does not establish a TSPEC admission
session, aggregate frames or send a multi-frame TXOP.

The controller applies parameters at an idle RX/response boundary. NAV
and bad-reception deferral propagate across contexts. Category switching
pauses the MAC/PSM, preserves the outgoing hardware residual counter,
selects the incoming bank and installs its AIFS and slots before resuming.
The original fail-closed 1,023-slot guard remains active.
New-frame identity includes TID; retry/new-frame CW updates use the selected
bank. Positive TXOP limits bound the single HT-data/SIFS/ACK exchange.

Enabling the policy restricts TX to the existing fixed, unprotected
version-4 normal-ACK fixture with TID 0..7. It does not enable HT/WMM
association advertisement or general QoS TX.

Command completion acknowledges deferred queueing. The EDCA status reports
the selected parameters, including an actual SHM readback of programmed
PSM AIFS. It does not measure on-air spacing.

## Native verification

Default and gated controller builds pass the existing ASan/UBSan MAC and
PSM suites. Additional cases cover:

- All eight TIDs, all four ACs, AIFS, retry CW saturation/reset and TXOP
  boundaries.
- Per-bank retry independence and preserved slots/random/CW/NAV/EIFS
  through category switching.
- ACM downgrade and all-categories-unavailable rejection.
- Invalid/duplicate/reserved AC records and atomic rejection.
- Unsupported contention exponents and disabled/profile/command-bit gates.
- PSM AIFSN 2..15 with 9- and 20-microsecond slots, exact simulated trigger
  boundaries and 16-bit clock wrap.

Default and gated badge sanitizer suites also pass: loader/fragmentation,
strict FIFO fault handling, secret lifecycle, WPA2/CCMP and per-TID state,
HT descriptors/responses, WMM parsing and A-MSDU validation.
These are native checks, not RF timing or negotiated-association evidence.

Final controller text/BSS sizes are 38,488/4,896 bytes by default and
41,688/6,416 bytes for the gated build. Both are freestanding builds with
no new dependencies.

## Paired hardware matrix

Two USB-connected RP2350 badges were used:

- A: `08DA26F86E57F5EA`
- B: `9BBF88DD17D66E7D`

Their RP2350 A4 revision does not identify the Wi-Fi chip version/revision,
which remains unestablished.

Final gated badge build: `fe68acca`. Controller flags:
`CLEAN_RX_HT_DATA=1`, `CLEAN_RX_HT_RX=1`,
`CLEAN_RX_HT_RESPONSE=1`, `CLEAN_RX_EDCA=1`.

The fixture supplied synthetic AP-format parameters, not parameters
negotiated with an independent AP:

| AC | TID | AIFSN | CWmin | CWmax | TXOP units | PSM AIFS readback |
| --- | --- | --- | --- | --- | --- | --- |
| BE | 0 | 3 | 15 | 1023 | 0 | 37 us |
| BK | 1 | 7 | 15 | 1023 | 0 | 73 us |
| VI | 4 | 2 | 7 | 15 | 94 | 28 us |
| VO | 6 | 2 | 3 | 7 | 47 | 28 us |

Every cell used a new nonce, channel 1, short slots, one 60-byte QoS MPDU,
and at most one actual TX attempt. All attempted cells are retained.

| Direction / MCS | BE | BK | VI | VO |
| --- | --- | --- | --- | --- |
| A to B / MCS0 | RX + returned ACK | RX + returned ACK | RX + returned ACK | RX + returned ACK |
| A to B / MCS7 | RX + returned ACK | RX + returned ACK | RX + returned ACK | RX + returned ACK |
| B to A / MCS0 | RX + returned ACK | RX + returned ACK | RX + returned ACK | RX + returned ACK |
| B to A / MCS7 | RX + returned ACK | RX + returned ACK | Bad-FCS capture; ACK timeout | RX + returned ACK |

Totals: 16 actual launches, 16 correct selected-policy/readback reports,
15 fresh complete receptions and 15 independently checked returned ACKs.
No controller errors or latched DCF range faults occurred in this matrix.
An independent Python `zlib.crc32` check verified the FCS, TID and
nonce-derived source address of all 15 successful saved MPDUs, plus the FCS
of all 15 returned ACKs.

The failed B-to-A VI/MCS7 cell retained a 66-byte PLCP/MPDU candidate with
valid matching HT-SIG profile/length, terminal PHY 2, and independently
invalid MPDU FCS. Both SHM copies agree. It differs from the fixture by
200 bits and fails the strict nonce/frame predicate, so it is not positive
frame-identity evidence. The failure is retained, not retried away or
included among successful deliveries. It does not establish an EDCA-specific
cause or resolve the earlier HT receive corruption.

Evidence paths relative to the repository root:

- `build/clean-rx/badge/d2-edca-final-{ab,ba}-tid{0,1,4,6}-mcs{0,7}-*`
  contains configuration, both bound binary/JSON reports, stderr, boot logs
  and complete run output for every final cell.
- `build/clean-rx/badge/d2-edca-be-mcs0-*` is the successful preliminary
  build `143c9c98`, before adding direct AIFS readback. It is not included
  in the final 16-cell matrix.

The default build `3e83ea0c` then passed a bounded legacy OFDM6 control:
three frames per direction cycling 32-, 1,500- and 2,304-byte payloads,
three ACKed deliveries per endpoint, zero corruption and no firmware errors.
Evidence: `build/clean-rx/badge/d2-edca-legacy-control-*`.
This is not the sustained milestone-A acceptance soak.

## Flash preservation

Fresh, verified full 16-MiB backups were taken before any test writes:
`build/clean-rx/badge/d2-edca-start-{A,B}-full.bin`.
After the gated matrix and default legacy control, both entire original
flashes were restored. Independent full reads
`build/clean-rx/badge/d2-edca-restored-{A,B}-full.bin` compare byte-for-byte
equal to the respective fresh backups. Both badges were left in BOOTSEL.
USB backup/restore operations were serialized after concurrent device
enumeration made the initial A backup attempt unavailable; no write
preceded A's successful backup.

## Reproduction

Build the controller and badge with matching flags and an explicit
`CONTROLLER_BIN`, as in the public build instructions, adding
`-DCLEAN_RX_EDCA=1` to both builds. Install the matching image only after
backing up the borrowed badges.

Use `CLEAN_RX_COLLECTOR` to select an isolated matching collector for
either the HT or legacy soak harness. For example, from the repository root:

```sh
CLEAN_RX_COLLECTOR=build/clean-rx/badge/ht-ack/collector \
HT_PROBE_TID=4 bash clean-rx/badge/ht_probe.sh \
  08DA26F86E57F5EA 9BBF88DD17D66E7D build/clean-rx/badge/NEW-EDCA-TRIAL 7 --ack
```

`HT_PROBE_TID` accepts 0..7 and is valid only for the normal-ACK fixture.
Omitting it leaves nonce-derived TIDs, which can exceed the EDCA profile's
accepted range. The collector requires the expected selected AC, TID,
CWmin/CWmax, AIFS readback and TXOP as well as a fresh returned ACK.

## Remaining D2 gates

- Actual AP-derived policy installation, association-response negotiation
  and runtime beacon/parameter updates.
- General QoS TX queueing/admission behavior, HT protection and independently
  measured SIFS/ACK timing.
- Negotiated protected aggregates, required A-MPDU reception, immediate
  Block ACK sessions/windows, bounded receive reordering and recovery.
- Independent HT peer association/framing with explicit legacy fallback.
- Resolution and all-rate qualification of receive corruption and broader
  contention/operand-visibility failures.

D1 remains complete only at its previously documented conservative
control/framing scope. D2 remains incomplete; HT/WMM station advertisements
remain disabled.
