# Spontaneous HT rejection with the calibrated probe, 2026-10-25

## Outcome

The fifth normal ACK60/MCS7/channel-6 attempt reproduced a B-to-A delivery
failure with no returned ACK. Badge A retained a bound rejection using the
corrected, previously calibrated diagnostic. Its two 66-byte reads agree,
but **54 of the 60 retained MAC bytes differ from the intended fixture**.
This is substantially different from the preceding byte-exact
[deliberately bad-FCS control](HT_REJECT_BAD_FCS_2026-10-25.md).

The rejection metadata and copy record bind to each other, **not to a
verified complete current-frame MAC identity**. Six consecutive payload
bytes match the new challenge, but the header identity does not. Neither
general SHM freshness nor TX/RF versus shared RX failure is established.

The campaign stopped immediately after attempt five of the authorized
twelve. All three full original flash images were restored and independently
verified byte-for-byte and by hash; all three devices were left in BOOTSEL.
There were no collection or restoration errors. No firmware change or
additional RF attempt followed the failure.

## Method and provenance

The user authorized up to twelve fresh attempts, alternating B-to-A and
A-to-B with a passive Pico, stopping on the first delivery failure, useful
bound rejection or collection error. Existing one-shot traffic, channel,
MCS, 200-ms receive lead and collection bounds were retained.

Devices: badge A `08DA26F86E57F5EA`, badge B `9BBF88DD17D66E7D`,
Pico `6572BE3E4636AF51`.

The exact calibrated **normal** images were reused:

| Image | Identity |
| --- | --- |
| Badge host | Build `6ee74aa8` |
| Passive Pico host | Build `7c9a28a9` |
| Controller SHA-256 | `c5768bf3180d5b11939da3cf51e8c5a32a5a0e600b5774ee152b609db351088d` |

`CLEAN_RX_HT_REJECT_TRACE=1` remained enabled;
`CLEAN_RX_HT_REJECT_BAD_FCS` remained disabled. The bad-FCS transmitting
image was not loaded. Preflight checked image equality, current source
equality to the calibration snapshots, preceding evidence seals and fresh
full-flash reads against the protected originals.

The [initial probe/ACK allocation collisions](HT_REJECT_COPY_2026-10-25.md)
were already repaired. Those defective prototype records remain excluded;
the new spontaneous record does not retroactively qualify them.

## Attempts

| Attempt | Direction | Fresh challenge | Target delivery / returned ACK | Passive full fixture |
| --- | --- | --- | --- | --- |
| 1 | B to A | `f3455189891a16c33c34223c0007` | Yes / yes | No |
| 2 | A to B | `c4faa4f53013b51e1e2e693c0007` | Yes / yes | Yes |
| 3 | B to A | `6d5a409d4bc287762c85793c0007` | Yes / yes | Yes |
| 4 | A to B | `1c33c9ba57f9065c8f5c353c0007` | Yes / yes | Yes |
| 5 | B to A | `71bc037e43ba72ae2e25d53c0007` | No / no | No |

The first four target receivers delivered complete fixture/FCS matches
through F2 and returned ACKs. Their rejection records remained entirely zero.
The fifth active receiver has the only committed rejection; its passive
Pico record remained entirely zero. All Pico snapshots confirm observer-only
mode 25, zero TX submissions/attempts and zero configured responses.

These five attempts bring this D2 copy investigation to sixteen attempts
across its separately authorized campaigns. A first-failure stopping campaign,
with one submitted frame per command and uncontrolled RF conditions, is not
a reliability acceptance rate.

## Frozen active rejection

Badge A retained count 1, cause 2, TSF `983719`, RXE count 66 and PHY value 2.
The frozen HT-SIG is `073c0007fc03`, matching MCS7/length 60.
Initial and final RXE tuples agree: `(66,0x402b,0x0840)`.
The copy record binds to that same cause, TSF and length.

The stream threshold is 128; before and after raw operands are both
`[0x0800,0x0800,0x0f40,0x0000,0x8000]`. These are the same operand values
as the controlled negative probe, not proof of the same transfer behavior.
Their completion semantics remain unqualified.

Expected normal MAC60:

```text
88002c000271bc037ec30271bc037e430271bc037e0310c7020071bc037e43ba72ae2e25d53c000771bc037e43ba72ae2e25d53c000771bc128adfde
```

Retained MAC60, identical in both reads:

```text
ad980f1545f38ab1defa50a88943bbbf4a14ebcfd7871ab40b15820745fc955530b5f7d97b6d31408badd93643ba72ae2e2592c65d45b4148aa94adf
```

The independent comparison finds:

- Offsets 0 through 43 and 50 through 59 differ: 54/60 bytes.
- Only offsets 44 through 49 match, the six-byte run `43ba72ae2e25`
  from the new challenge. Offsets are zero-based within the MAC MPDU.
- Expected fixture FCS: `0xdedf8a12`.
- Software CRC of the retained 56-byte body: `0x402b4e0d`.
- Retained FCS: `0xdf4aa98a`, invalid for that retained body.
- `record_correlated:true`, but `rejection.identity_correlated:false`;
  general `shm_copy_freshness_proven:false`.

The fresh partial payload match is worth retaining, but is not a complete
frame-provenance oracle. A stable mixed/stale SHM read, receive corruption
and a different eligible candidate cannot be distinguished by this record
alone. A valid-length rejected byte array is not proof of those bytes on air.

### Contrast with the controlled negative

The deliberately corrupted control retained all 66 programmed bytes exactly,
including its single changed FCS bit. Its software CRC versus retained FCS
XOR was exactly `0x80000000`; its MAC identity matched. Its RXE tuple was
`(66,0x4062,0x0840)`.

The spontaneous case is therefore not another observed FCS-only mismatch.
The differing raw RXE status values are retained without assigning
undocumented bit meanings. Calibration proves the active hook can retain a
complete matching negative fixture; it does not make this different,
identity-damaged body fresh by implication.

### Last-candidate and passive limitations

The active receiver's separate last-candidate trace contains two candidates
and ends with SIG `076000df1803`, not the requested SIG. The transmitter's
later trace also contains that SIG. These rolling traces must not replace
the frozen rejection's own SIG and metadata.

The conservative archived diagnosis remains `no_correlated_capture`: there
is no complete fixture identity, correlated F2 or delivered frame on this
attempt. That verdict does not mean no eligible HT rejection was recorded.

The Pico retained no candidate, F2 fixture or rejection on attempt five.
It also retained none on attempt one, **despite badge A delivering that
normal frame and returning its verified ACK**. Thus passive silence is
demonstrably insufficient to conclude that a normal transmission was absent.
No independent RF timestamp or receiver-implementation oracle closes that
gap in this campaign.

## TX comparison and timing interpretation

All five normal TX snapshots have one submitted attempt, valid DCF traces,
zero guard latch/final slots, controls `[0x0042,0x1c02,0x000b,0x000f]`,
unchanged HT-SIG template CRC policy and reported PHY error `0000`.
The PHY, IQ and IHR diagnostic arrays are identical across all five;
radio register `0x34` varies even among successful trials. Its value in
attempt five also occurs in successful attempt three.

| Snapshot | MAC TX duration (us) | Reported MAC RX turnaround (us) | ACK result | First PHY debug |
| --- | --- | --- | --- | --- |
| Normal 1 | 89 | 66 | ACKed | `00000046` |
| Normal 2 | 79 | 66 | ACKed | `00000046` |
| Normal 3 | 89 | 66 | ACKed | `00000046` |
| Normal 4 | 89 | 66 | ACKed | `00000046` |
| Spontaneous failure | 89 | 403 | Timeout | `00000006` |
| Controlled bad-FCS probe | 93 | 394 | Timeout | `00000046` |

The longer reported turnaround is **not an isolated measurement of when
hardware RX first became usable**. In the
[command path](../../src/runtime/commands.inc), the reliable TX branch enables
RX and waits for ACK/timeout before writing the final `mac_resume_us`
timestamp. The collector subtracts `mac_complete_us` from that timestamp.
The controlled no-ACK probe exhibits a similar longer interval. These
numbers alone therefore do not establish a slow-RX-return cause.

Likewise, `00000006` versus `00000046` is one observed raw difference, not
a decoded cause: the byte-exact bad-FCS control also timed out with
`00000046`. TX-side submission, template readback and ordinary status values
still do not prove a correct transmitted waveform.

## Evidence audit and restoration

Evidence root: `build/d2-spontaneous-20261025/`.
It retains the authorized bounded harness, immutable calibrated images and
source snapshots, backup records, exact configurations/challenges, all raw
reports/native JSON, pair verdicts, diagnoses, process exits, early-stop
outcome, restoration records and sealed manifest.

The independent audit passes:

- Five attempts, fifteen complete 2,092-byte raw reports and fifteen exact
  native-collector replays, checked against the archived independent Node
  oracle.
- Four target delivery/ACK pairs, three passive full-fixture observations,
  nine entirely zero receiver trace records and one bound active rejection.
- Disabled FCS injection, fresh identities, alternating directions and
  immediate first-failure stopping, with seven unused authorized slots.
- Fresh full preflash reads and full restored-image readbacks matching all
  three protected originals, including sizes, hashes and private permissions.
- Final per-device and all-device BOOTSEL checks; preceding archives remain
  unchanged.

Original sizes/hashes are retained in the
[initial provenance record](HT_REJECT_COPY_2026-10-25.md#provenance-audit-and-restoration).
The new archive was sealed only after campaign exit and completed restoration.

## Remaining causal boundary

The corrected, calibrated instrument has now captured a spontaneous
rejection during a retained delivery failure. That closes the immediate
evidence gap left by the passing zero-state controls, not D2 acceptance.

The next discriminating work is per-byte receive-copy provenance, rather
than treating another stable read as fresh. An ownership-reviewed frozen
pre-receive SHM baseline could test partial/unchanged buffer content without
forcing a copy; independent receiver/waveform evidence is still needed to
separate TX/RF from shared source-built RX behavior. Any new instrumentation
or hardware campaign needs its own bounded authorization. Passive rejection
capture, complete spontaneous-frame provenance and D2 work package 1 remain
open.
