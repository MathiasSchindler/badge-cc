# Bound pre-command SHM and dual-receiver rejection, 2026-10-25

## Outcome

With the [diagnostic initialization repair](HT_CANDIDATE_TRACE_2026-10-25.md),
all three reports were valid, but the **first normal B-to-A control failed**.
Both active badge A and passive Pico retained bound cause-2 rejections.
The campaign stopped immediately; the second directional and deliberate
bad-FCS controls were not run.

None of the 38 incorrect active MAC bytes or 46 incorrect passive MAC bytes
equals its own pre-command snapshot byte. Thus those incorrect bytes are not
simply unchanged bytes from the recorded preimage. The receivers share nine
incorrect bytes but differ at 45 MAC offsets. Both retain the requested FCS,
which fails CRC over their respective damaged bodies.

**The probe has not passed its positive/negative RF calibration.** Its added
PSM timing may affect reception. These records do not prove complete fixture
identity, general copy freshness, TX/RF corruption or a product repair.
No D2 work package is closed; negotiated HT remains disabled.

All three full original flashes were restored and independently verified by
size, SHA-256 and every byte, then checked in exact-serial BOOTSEL.

## Authorization and method

After the first campaign stopped on invalid diagnostic publication, the user
explicitly authorized a **new** campaign on the same three devices, channel 6,
MCS7 and one-shot ACK60. The limit remained twelve attempts: two directional
normal controls, one bad-FCS control, then at most nine normal follow-ups.
Stop on control/collection failure or first useful spontaneous rejection.
No earlier unused slots were silently resumed.

Roles: badge B `9BBF88DD17D66E7D` transmitted to badge A
`08DA26F86E57F5EA`; Pico `6572BE3E4636AF51` received passively.
Collectors retained the 200-ms receive lead and 25-second process bounds.
No credentials, network association or external timing equipment were used.

Fresh preflash full readbacks matched the protected originals. Fresh matched
`PROFILE=diagnostic SELECTION=ht-response` images enabled rejected-copy,
startup-baseline and per-candidate traces. Normal badge build `71074bdd`,
Pico `5c8ca066`; bad-FCS badge build `daa02766` was archived but not installed.
Normal controller SHA-256:
`3b41c4b7fbb7a5515124f56969676522099eea3f87d827f39ace794bcfaf68fd`.
Exact source, dirty diff, flags, images, collectors, oracle and tool hashes
were archived before flashing. The earlier failed archive was not changed.

| Attempt | Direction | Challenge | Target delivery / returned ACK | Passive complete fixture |
| --- | --- | --- | --- | --- |
| 1 | B to A | `799b897c36d1b7a427412c3c0007` | No / no | No |

All reports are 2092 bytes with valid frozen snapshots, error 0, firmware
error 0 and expected build identities. The active and passive copy/candidate
records each commit once and agree on their own cause, rejection TSF and
length. All attempts, including this failed control, remain retained.
Eleven slots remained unused.

## Observations

Both rejected records have count 66, HT-SIG `073c0007fc03`, cause 2,
matching repeated SHM reads and no correlated F2. Neither complete MAC
identity validates. The raw start/status/copy operands are observations;
undocumented hardware semantics are not inferred from them.

| Field | Active badge A | Passive Pico |
| --- | --- | --- |
| Candidate sequence | 11 | 229 |
| Pre-command TSF | 700666 | 2114725 |
| Bound rejection TSF | 700685 | 2114744 |
| Local start-to-rejection interval | 19 us | 19 us |
| Pre-command raw RXE length | 948 | 20 |
| Incorrect MAC bytes | 38/60 | 46/60 |
| Incorrect MAC bytes equal to own preimage | 0 | 0 |
| Unchanged raw offsets, including HT-SIG | 7, 58 | 2, 7 |
| Calculated body CRC | `0x63bf83cd` | `0x0f32225c` |
| Retained/requested FCS | `0x641252fb` | `0x641252fb` |

The 19-us intervals come from each device's own unsynchronized TSF. They
are neither independent EOF timestamps nor SIFS measurements. Early raw
RXE fields need not describe the final packet, so their length values are
not labelled a proven candidate identity or copy endpoint.

Expected MAC60:

```text
88002c0002799b897cb602799b897c3602799b897c7690b70700799b897c36d1b7a427412c3c0007799b897c36d1b7a427412c3c0007799bfb521264
```

Active retained MAC60:

```text
88002ce6b507168db127c84f8923503f985a0e7cc7fac08638475380a3cfa7543d5d7ae24e7e1bde639b897c36d1b7a427412c3c0007799bfb521264
```

Passive retained MAC60:

```text
88002c00a2789b897cb60279bb9ca5fc446fc2ddcae7c08638475320cf112a38305d7ae24e4a591b9f8720902c70d5daf0716b161bcba894fb521264
```

The shared incorrect MAC offsets are `22..26` and `33..36` (inclusive).
The pre-command snapshots are different existing receive contents, not zeros.
Both incorrect-byte comparisons are against each receiver's own snapshot,
not against the other receiver or an initial boot baseline.

## Interpretation and limits

The new diagnostic no longer fails publication after explicit initialization
of its own SHM. Its snapshot/binding path was exercised in both receive roles,
but there was no passing positive control or byte-exact negative calibration.
The initial failure could include probe perturbation; it cannot be pooled
into earlier baseline-image reliability results.

For these retained records, every incorrect MAC byte differs from the
pre-command preimage. That narrows unchanged-buffer explanations, not every
possible intervening/partial copy or shared receiver defect. SHM reads are
sequential, and equal/different bytes do not establish hardware completion.
Shared incorrect runs and the matching requested FCS do not prove the
intended full frame was transmitted or identify the transmitter as faulty.
The source Pico is a second device, not an independent receiver implementation.
No speculative buffer clearing, forced copy, PHY tweak or HT advertisement
was added as a supposed product fix.

The next discriminator needs independently calibrated HT observation and
probe-perturbation controls, not another uncontrolled repetition. General
variable-length RXE completion remains unsupported, blocking safe aggregate
and station BA integration. Physical SIFS still requires independent equipment.

## Independent audit and restoration

The immutable archive is `build/d2-candidate-fixed-20261025/`.
`harness/audit.cjs` independently replays all three raw reports through the
archived native collectors, recomputes requested bytes/FCS, validates both
candidate/rejection bindings and per-byte comparisons, recomputes shared
incorrect offsets and verifies the first-control stopping condition.
It also verifies the inactive transmitter/response fields of the passive
Pico, prior evidence seals, both full preflash and restoration readbacks,
and separate final exact-serial BOOTSEL logs.

`audit.json` records three valid reports, two bound candidate records,
zero passing positive controls, zero negative controls, no unchanged
incorrect preimage bytes, and verified restoration. `sealed-evidence.json`
protects the source/image/report/audit archive. This is the 26th attempt
across separately authorized D2 copy investigations, not a reliability rate.
