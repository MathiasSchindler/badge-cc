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
pauses PSM while preserving MAC receive enable and the outgoing hardware residual counter,
selects the incoming bank and installs its AIFS and slots before resuming.
The original fail-closed 1,023-slot guard remains active.
New-frame identity includes TID; retry/new-frame CW updates use the selected
bank. Positive TXOP limits bound the single data/SIFS/ACK exchange.

EDCA accepts the existing version-4 normal-ACK fixture and reliable
legacy-PHY management, non-QoS and bounded QoS Data/Null transmissions.
Management selects VO, non-QoS data selects BE and QoS selects by TID.
Legacy TXOP accounting uses the actual length, rate and preamble.
Protected QoS headers are constructed before encryption; controller
selection and retries do not rewrite ciphertext, PN or QoS control.

The additional default-off `CLEAN_RX_WMM=1` gate negotiates non-HT WMM
without HT capability advertisement. A usable beacon Parameter element
permits a WMM Information request with no U-APSD. Accepted association
response parameters, basic rates, short-slot policy and QoS-only ACKs
are installed together through command 16 at a quiescent RX/response boundary.
Beacon changes use the same atomic payload. Withdrawal, unsupported
parameters and information-only responses after an offer fail explicitly.
An association response omitting WMM remains a legacy association.

The host dispatches negotiated unprotected QoS into the existing per-TID
duplicate/reassembly contexts. Protected reception still authenticates
before entering those contexts. The WPA2 fixture sends a TID-0 QoS probe
after negotiation and retains its original non-QoS behavior otherwise.
This is not a general application TX queue, TSPEC implementation or
negotiated HT/Block ACK/aggregation support.

Command completion now acknowledges an applied, readback-verified policy,
not deferred queueing. Commands 8, 14, 15 and 16 retain their mailbox command
until commit; a different command while one is pending is an explicit
controller error. The receiver continues servicing retained captures while
waiting. A held complete capture is also a valid commit boundary once the
response/PHY is quiescent; its bytes and ownership are not changed.

The controller prepares software contention state before publication, pauses
PSM, rechecks the boundary, writes all response-rate/airtime records, and reads
every programmed word back. It also verifies the pinned addresses, QoS response
policy and PSM slot/AIFS/EIFS/backoff fields. Only then does it resume PSM and
acknowledge the command. A policy-data verification failure leaves PSM paused and reports
the failing word/value instead of acknowledging a partially installed policy.
Updates which do not reconfigure EDCA preserve the current hardware backoff
slots, rather than restarting from a stale software sample. PSM pause/resume
and the hardware slot register also require matching readback.

The host waits on that exact command and reports timeout, cancellation, bus,
controller or unexpected-command failures. The EDCA status reports the
selected parameters, including an actual SHM readback of programmed PSM AIFS.
It does not measure on-air spacing. `wmm_policy_committed_updates` counts
successful station-side commits; `wmm_policy_queued_updates` remains an equal
compatibility alias in matching current builds. Historical evidence below
predates this stronger completion contract.

## Readback-verified policy commit campaign (2026-10-18)

This implements an activation **prerequisite**, not actual HT activation.
There is still no live aggregate/Immediate BA station path, qualified
receive-size/protection implementation or real HT association advertisement.
The unconditional mandatory-MAC fallback remains in place. No readiness
override or new way to advertise the target capability IE was added.

The former mailbox success meant that a policy was queued. The station could
therefore publish its accepted profile or send its next frame before the
controller applied that policy. The stronger completion contract above removes
that gap for the existing legacy/WMM and diagnostic response/EDCA policies.
It does not supply the missing HT activation transaction or its lifecycle.

### Software validation

Three isolated controller/badge profiles pass `all test`: default, full
HT-data/extended-RX/response/EDCA/WMM, and finite aggregate interface-3/handoff-2.
Their existing native and ASan/UBSan suites pass. All six firmware ELFs have
no undefined symbols.

New response-table tests cover all 4,095 nonempty basic-rate masks with and
without HT responses, independent airtime vectors, write-all-before-read order,
all 336 dropped-write/corrupted-read cases, invalid-input nonpublication, and
preservation of SHM outside the response tables, including capture storage.
Host command tests cover immediate/delayed completion, unexpected replacement,
controller failure despite an apparent acknowledgement, timeout, cancellation,
bus failure and timer wrap.

The first link exposed compiler-generated `__aeabi_memcpy` from contention
structure assignments. Reusing the existing byte-copy helper removed that
dependency; no libc or new runtime implementation was added. Initial failed
link logs are retained.

Builds are `build/clean-rx-d2-policy-{default,wmm,handoff}/` and
`build/clean-rx/badge/d2-policy-{default,wmm,handoff}/`. Logs are
`build/clean-rx/badge/d2-policy-{default,wmm,handoff}-{controller,badge}.log`.
Use the existing controller/badge `all test` commands with these isolated
directories and the corresponding build flags; the finite handoff flags are
unchanged from the [interface investigation](AMPDU_INTERFACE_INVESTIGATION.md).

### Final radio results

Final WMM build `fbfe4a23`, default build `8c4605c1`:

| Prefix under `build/clean-rx/badge/` | Result |
| --- | --- |
| `d2-policy-final-wpa2-A` | Independent vendor AP: WMM negotiated, one committed policy, pending zero, four-way handshake and one protected echo at both endpoints |
| `d2-policy-final-wrong-A` | WMM policy committed, but wrong password rejected; no AP authorization or protected delivery |
| `d2-policy-final-default-wpa2-A` | Legacy/non-WMM association, zero WMM commits, successful handshake and protected echo |
| `d2-policy-final-ack-ab-m0`, `...-r1` | Applied policies and one local launch each; no fresh matching HT receive or returned ACK |
| `d2-policy-final-ack-ab-m0-r2` | Exact HT receive and returned ACK; BE/TID 0/MCS0 |
| `d2-policy-final-ack-ba-m7` | Exact HT receive and response start, but no fresh returned ACK |
| `d2-policy-final-ack-ba-m7-r1` | Exact HT receive and returned ACK; VO/TID 6/MCS7 |

All five final HT trials have distinct challenges, zero controller/response
errors, a readback-valid EDCA selection and no pending sender policy.
Only **2/5** complete the exchange; **3/5** retain an exact HT MPDU.
Failed trials remain in the evidence. These are normal-ACK diagnostic fixtures,
not BA, measured SIFS, independent HT interoperability, or reliability/size/
protection qualification. No cause for the radio misses is inferred from this
small matrix.

`d2-policy-evidence-assertions.log` independently checks the final JSON results,
matching peer identities/builds, commit/compatibility counts, authorization and
all five HT outcomes. Initial WMM positive/negative trials are retained under
`d2-policy-wpa2-A-r1` and `d2-policy-wpa2-wrong-A-r0`. The earlier
`d2-policy-wpa2-A-r0` failed before AP readiness because the original badge
application was booted rather than the vendor test image; it is not an RF
or policy result. The user returned both badges to BOOTSEL before the correct
bounded vendor AP image was installed.

### Flash preservation

Before the first write, both complete 16-MiB flashes were saved, verified by
picotool and independently read again for byte comparison. All test writes
belong to that bounded campaign. Afterward, both originals were restored,
independently read back and compared across all 16,777,216 bytes. Both badges
remain in BOOTSEL.

- A (`08DA26F86E57F5EA`): `d2-policy-start-A-full.bin` and
  `d2-policy-restored-A-full.bin`; SHA-256
  `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B (`9BBF88DD17D66E7D`): `d2-policy-start-B-full.bin` and
  `d2-policy-restored-B-full.bin`; SHA-256
  `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

Paths are under `build/clean-rx/badge/`; backup and restore logs use
`d2-policy-backup-*` and `d2-policy-restore-*`. No hardware writes remain
pending, and no full D2/HT activation claim is made.

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

Earlier fixed-fixture controller text/BSS sizes were 38,488/4,896 bytes by default and
41,688/6,416 bytes for the gated build. Both are freestanding builds with
no new dependencies.

## Earlier fixed-fixture paired hardware matrix

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

- RF qualification of runtime AP policy changes and error/recovery behavior
  beyond the bounded non-HT WMM trials below.
- General QoS TX queueing/admission behavior, HT protection and independently
  measured SIFS/ACK timing.
- Negotiated protected aggregates, required A-MPDU reception, immediate
  Block ACK sessions/windows, bounded receive reordering and recovery.
- Independent HT peer association/framing with explicit legacy fallback.

## AP-driven non-HT WMM integration

Matching controller/badge flags are `CLEAN_RX_HT_DATA=1`,
`CLEAN_RX_HT_RX=1`, `CLEAN_RX_HT_RESPONSE=1`, `CLEAN_RX_EDCA=1`
and `CLEAN_RX_WMM=1`. The HT data flag retains the separate diagnostic;
the WMM station itself uses only legacy PHY rates and QoS-only responses.
Default builds continue to omit both HT and WMM advertisement.

Native ASan/UBSan tests cover the exact WMM request IE, no HT advertisement,
response acceptance/omission/rejection, combined-policy encoding/decoding,
unchanged output on invalid input, runtime parameter/rate/slot updates,
withdrawal and association teardown. MAC tests cover all TIDs' TX
selection, unsupported headers, all legacy rate/preamble airtimes, and
interleaved per-TID station reassembly with exact F2 length and FCS.
CCMP tests decrypt the QoS probe independently through the crypto API and
verify byte-identical encrypted bodies/PN across Retry updates.

The opt-in controller initially linked against an implicit
`__aeabi_memclr4` from an aggregate zero-initializer. Removing that
unnecessary initialization, rather than introducing a runtime dependency,
kept the build freestanding. The candidate EDCA state is fully initialized
by the existing configure function before use.

### Independent vendor AP trials

Build `5501529f` ran as station on A against the unmodified vendor AP on B.

- `build/clean-rx/badge/d2-wmm-open-2-*`: open authentication and association
  succeeded in one attempt per phase; WMM was offered and accepted,
  one combined policy was queued, and controller readback confirmed EDCA
  enabled. The independent AP also confirmed the station connection.
- `build/clean-rx/badge/d2-wmm-wpa2-1-*`: WMM association and the WPA2
  handshake succeeded. The station validated one protected echo; the
  independent vendor AP reported authorization, one protected RX and one
  protected TX. The station's protected probe uses the new QoS/TID-0
  header and CCMP nonce/AAD. Both bounded helpers exited successfully.
  The generated AP credential file was private and deleted.

These are bounded non-HT WMM exchanges, not measured EDCA/SIFS timing,
runtime-beacon RF qualification, long-duration reliability, HT protection,
Block ACK, A-MPDU or reordering qualification. D2's second checklist
point remains incomplete.

### Regression and final build boundaries

The same WMM build passed the existing clocked HT/EDCA fixture for
BE/TID-0/MCS0 from A to B and VO/TID-6/MCS7 from B to A. Each used one
actual launch, an exact fresh receive and a returned ACK. Independent
`zlib.crc32` checks of the saved binary MPDUs and returned ACKs passed,
including the expected TID. Evidence prefixes are
`d2-wmm-ht-be-mcs0-ab-*` and `d2-wmm-ht-vo-mcs7-ba-*` under
`build/clean-rx/badge/`.

Default build `75f9beb7` passed the legacy OFDM6/cycle control with
16 exact deliveries per direction, one deliberately suppressed ACK per
endpoint, zero corruptions and zero duplicate deliveries. Actual attempt
counts were 17 and 23, not merely 16 local command completions.
Evidence prefix: `build/clean-rx/badge/d2-wmm-default-soak-*`.

After these RF trials, an additional QoS TX guard enforces the exact
2,304-byte MSDU limit plus the protected frame's 16-byte CCMP envelope,
and rejects truncated protected frames. Native positive/negative boundary
tests cover that guard. Final default `c9823311` and WMM `dbd7c67a`
builds pass all native suites and freestanding builds; the existing
HT/EDCA-without-WMM profile also passes. All six resulting ELFs have no
undefined symbols. The RF trials above used the preceding builds, not
these final guard-tightened binaries.

### Full-flash preservation

Fresh full 16-MiB backups were saved and verified before any writes:
`build/clean-rx/badge/d2-wmm-start-{A,B}-full.bin`. After all trials, both
entire originals were restored with verification. Independent full
readbacks `d2-wmm-restored-{A,B}-full.bin` compare byte-for-byte with those
backups. Restore logs are `d2-wmm-restore-{A,B}.log` in the same directory.
All USB flash operations were serialized. Both badges remain in BOOTSEL;
no AP helper is left running.
- Resolution and all-rate qualification of receive corruption and broader
  contention/operand-visibility failures.

D1 remains complete only at its previously documented conservative
control/framing scope. D2 remains incomplete; HT station advertisement
remains disabled, and WMM requires its explicit opt-in build.

## Protected A-MSDU receive integration

The next D2 increment connects the existing whole-body A-MSDU parser to
software CCMP and the actual protected station receive path. It does not
implement A-MPDU reception, immediate Block ACK or reordering, and does
not enable HT advertisement.

### Implementation and admission

- Ordinary CCMP payloads retain the 2,304-byte MSDU bound. Unfragmented
  three-address QoS A-MSDUs permit a 3,839-byte body. With the 26-byte
  header, 8-byte CCMP header, 8-byte MIC and 4-byte FCS, the encrypted
  MPDU is at most 3,885 bytes, within the extended 3,896-byte capture.
- The station enables aggregate admission only with extended RX and a
  retained WMM association. Protected bytes bypass unauthenticated MAC
  delivery and remain unchanged until CCMP processing.
- After MIC verification, the complete aggregate must validate and every
  subframe DA must equal the local station MAC. Existing source-address,
  LLC/SNAP-as-DA, length and padding checks apply to protected aggregates.
  Only after all subframes pass does the code commit PN state and enter
  authenticated per-TID duplicate processing.
- The existing CCMP AAD does not authenticate the A-MSDU-present bit.
  Therefore MIC success alone is not aggregate admission. A negative test
  flips that bit on a valid ordinary encrypted LLC/SNAP payload, repairs
  the outer FCS, and requires explicit format rejection without delivery,
  PN commitment or duplicate-cache entry. This is not SPP negotiation.
- Matching the protected reply marker in an early subframe is provisional
  until the entire aggregate validates. A malformed last subframe cannot
  partially increment delivery counters. Successful delivery and terminal
  failure wipe plaintext; replay and Retry duplicates do not deliver again.
- Station detail offsets 784/788 retain newly delivered protected aggregate
  and subframe counts, exposed as `protected_amsdu_frames` and
  `protected_amsdu_subframes`. They are not A-MPDU or BA counters.

### Native evidence and build boundaries

Independent AES-128-CCM vectors were generated with Node's built-in
OpenSSL-backed `node:crypto`, not the firmware implementation. Key bytes
are 0..15, plaintext byte `i` is `(17*i+3)&255`, and the fixed QoS
header/PN are recorded in `aggregate_crypto()` in `wpa2_test.c`.
Ciphertext SHA-1 and the complete 8-byte tag match at lengths
15, 16, 17, 2,304, 2,305 and 3,839. Sentinel guards and invalid-MIC wiping
are checked independently of the round-trip tests.

The ASan/UBSan protected receive suite covers all 16 TIDs, full-size
two-subframe aggregates, replay, a newer-PN Retry duplicate, sequence
4095-to-0 wrap, malformed final subframes, invalid DA/SA, LLC-as-DA,
MIC/header/key-ID/policy faults, disabled admission and plaintext wiping.
Authenticated MAC tests now admit fully validated aggregates while still
rejecting fragmented aggregates.

Extended loader and strict-loader tests send the exact 3,885-byte encrypted
MPDU through three physical F2 fragments, with exact and rounded physical
lengths and MCS0/MCS7 metadata. They check complete ciphertext/FCS,
unchanged station protected bypass and a single logical credit, then run
the real protected receive function. Positive delivery produces one
aggregate, two subframes and one reply; bad-MIC and valid-MIC malformed-last-
subframe controls produce no delivery or PN commitment. The strict-loader
build retains its intentionally disabled credit-write path.

All controller and badge native suites and freestanding builds pass for:

| Profile | Badge build | Badge BSS |
| --- | --- | --- |
| Default | `2b8bee11` | 221,320 bytes |
| Extended HT/response/EDCA, WMM station disabled | `be7d73e3` | 225,808 bytes |
| Extended HT/response/EDCA plus WMM station | `326a4152` | 272,024 bytes |

All six controller/badge ELFs have no undefined symbols. No runtime,
allocator or dependency was added. The additional loader-test crypto
linkage is native-test-only.

### Bounded RF regression failures

Both trials used final WMM build `326a4152` and an unmodified independent
vendor AP. Neither trial reached encrypted data processing:

- `build/clean-rx/badge/d2-amsdu-wpa2-regression-*`: station A/AP B.
  Four authentication attempts, four ACK timeouts, no accepted
  authentication response and no association. Station reports 42 bad-FCS
  events. The AP reports no retained associated or authorized peer.
- `build/clean-rx/badge/d2-amsdu-wpa2-reverse-*`: station B/AP A.
  Authentication/association each completed in one attempt. WMM was offered
  and accepted, one combined policy was queued and EDCA readback enabled.
  No EAPOL M1 was delivered; WPA timed out before M2, with 68 bad-FCS
  events. The independent AP did not retain a stable associated peer.

Both station collectors and AP helpers exited unsuccessfully. These
failures are retained, not retried into a passing result. They neither
exercise nor qualify protected aggregate reception and do not establish
the cause of the missed replies. Native crypto/transport success must not
be presented as an OTA substitute. Protected A-MSDU OTA qualification,
HT protection, hardware A-MPDU reception, immediate BA agreement/window
handling and bounded reordering remain open.

### Aggregate-increment flash preservation

Fresh complete 16-MiB backups were saved and verified before any writes:
`build/clean-rx/badge/d2-amsdu-start-{A,B}-full.bin`. After both bounded
trials, the entire originals were restored with `--ignore-partitions` and
verified. Independent complete reads
`d2-amsdu-restored-{A,B}-full.bin` compare byte-for-byte with their respective
fresh backups. Backup, restore and readback logs use the corresponding
`d2-amsdu-*` prefixes in the same build directory. All flash operations
were serialized. Both badges are left in BOOTSEL, both AP helpers have
exited, and the private generated credential file/directory were removed.
