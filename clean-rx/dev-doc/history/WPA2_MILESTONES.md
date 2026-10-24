# WPA2 milestone history

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## E. Complete WPA2 operation

Scope: complete WPA2-Personal with CCMP for the supported station profile.
WPA2-Enterprise/EAP and obsolete cipher compatibility are separate projects.

### E1. Robust four-way handshake

- Recorded completed criterion: Harden timers, bounded retransmissions, duplicate handling, replay
  counters and AP compatibility while retaining MIC/ANonce/RSN validation.
- Recorded completed criterion: Support the documented passphrase and raw 256-bit PSK provisioning
  formats without logging secrets or retaining stale provisioning state.
- Recorded completed criterion: Verify wrong credentials, malformed key data, altered MIC/nonce/RSN,
  unexpected transitions and duplicate M3 without key reinstallation.
- Recorded completed criterion: Complete 10 consecutive fresh protected connections against the
  controlled AP, followed by the additional AP configurations from B3.
  Record association failures separately from handshake failures.

#### E1 implementation and qualification, 2026-10-23

The portable supplicant now owns an eight-second absolute deadline and four
500-ms local retransmissions per M2/M4 phase. Polling continues even while a
controller submission is pending. Duplicate input cannot extend either budget.
M1 retransmissions may advance replay with the same ANonce; M3 retransmissions
must authenticate and preserve normalized key contents even when their replay
counter advances. Neither reinstalls PTK/GTK nor resets TX/RX packet numbers.
Zero or changed nonces, unsupported/malformed pinned RSN, stale replay, changed
keys, malformed key data and wrong MICs fail explicitly. Timeout/transport/
departure failures wipe software keys and fragment plaintext.

Passphrase and case-insensitive raw 64-hex-digit PSK inputs use the same owned,
private, two-line file reader. Invalid inputs wipe the PMK output. A fresh
OS-random SNonce and request/BSSID/channel binding retain one-use provisioning.
The exclusive mode-600 `--derive-psk` command lets the actual hardware runner
alternate both provisioning paths without logging secret values.

Native ASan/UBSan tests cover independent vectors, min/max lengths, every bad
raw-hex position, timer wrap, absent M1, lost M3/M4, duplicate retry budgets,
equal/advanced/stale counters, MIC/nonce/RSN/GTK rejection, and unchanged keys/
PNs under accepted M3 retransmissions. Host tests invoke the real collector
for private-file formats, malformed/NUL input, permissions and exclusive output.
The archive oracle checks independent AP/station raw counters and identities;
its matrix tests reject missing, reused and wrong-target sessions.

The Pico vendor peer adds opt-in protected mode 6 without changing shared
vendor/platform sources. It reads back channel, nmode, CCMP and WPA2-PSK auth,
provisions a raw PMK rather than a public-nonce-derived password, and echoes
only an exact, authorized, addressed 38-byte application challenge.

Initial hardware campaign `build/clean-rx/e1-20261023/hardware/matrix01-*`
rejected all 13 initial fixture trials before AP readiness. The new AP consumed
its secret too late: not-ready USB report polling intentionally stalls, and
the shared USB stall path wipes unconsumed provisioning. This was not an
over-the-air handshake failure. Consumption now occurs immediately on taking
the submitted request, before clock calibration or firmware loading can poll
USB. A native test exercises the actual provisioning/submit/not-ready-stall
sequence and proves early consumption, one-use binding and invalid-input
wiping. AP operation codes localize subsequent configuration/data failures.
Both borrowed originals were fully restored, read back and compared after
that failed campaign.

The qualification matrix separately requires ten controlled channel-1
connections, then three channel-6 and three channel-11 connections, alternating
passphrase/raw PSK. Three additional wrong-password controls must never
authorize or exchange protected traffic. One failed row fails the campaign;
retained successful rows are not spliced across campaigns.

Campaign `matrix02-*` then reached authentication, association and M2 in all
16 positive trials, but every M3 failed the old GTK parser's reserved-byte
check (`gtk_kde` examples `f60218`, `fe0218`, `2d0218`). The independently
authenticated/unwrapped KDE had the correct 24-byte length and key-ID byte
`02`; only its reserved byte was nonzero. The public
[hostap reference receiver](https://w1.fi/cgit/hostap/plain/src/rsn_supp/wpa.c)
(`wpa_supplicant_pairwise_gtk`) masks the key ID and skips the reserved byte
rather than rejecting it. The parser now ignores reserved bits/bytes only
after MIC and wrap-integrity verification, preserving length/uniqueness,
pinned RSN and GTK checks. Duplicate M3 is reauthenticated/unwrapped and
compared against the installed GTK/key ID, not a ciphertext fingerprint, so
changing ignored reserved fields cannot cause a false reinstall or failure.
Native tests reproduce byte `f6`, all reserved flag bits, and authenticated
changed GTK/key-ID rejection with unchanged PNs on accepted retransmissions.
All three wrong-password controls associated and submitted M2, then rejected
a new ANonce without authorizing or exchanging protected data. The oracle
accepts that explicit terminal rejection, not only timeout/MIC/departure.
Both originals were again fully restored/read back/compared after `matrix02`.

Campaign `build/clean-rx/e1-20261023/hardware/matrix03-*` passed all 19 trials:
ten consecutive controlled channel-1 protected connections, followed by three
channel-6 and three channel-11 protected connections, then one wrong-password
control per profile. All 16 positives independently verified their protected
echo and AP authorization; eight used passphrases and eight raw PSKs.
All three negatives associated/submitted M2 but installed no key, sent no M4,
and exchanged no protected data at either endpoint. The matrix retained five
timer-generated retry events (not necessarily all submitted on air).
No association/handshake/transport failure occurred in a positive trial.
Station build `5265041c` and Pico AP `93bf26c0` were consistent throughout.
The exact AP image and matching collector were preserved in
`build/clean-rx/e1-20261023/qualified03-peer/`.

The strengthened oracle re-read every raw pair and reproduced the successful
matrix byte-for-byte. Native negative tests reject missing/reused sessions,
wrong devices, changed builds, mismatched raw counters/configuration and
invalid private credential files, including exact maximum-length files.
The AP collector's no-report timeout now explicitly fails without writing
uninitialized archive buffers; this is a host error-path guard, not a radio
or handshake change.

Both borrowed full originals were restored and compared after `matrix03`:
badge `08DA26F86E57F5EA`, 16 MiB, SHA-256
`02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`;
Pico `6572BE3E4636AF51`, 4 MiB, SHA-256
`f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486`.
The other badge was not modified. E1 is qualified for these bounded legacy
connections; the E2/E3/B3 mesh/persistence and A/B2 reliability gates remain open.

The final host-only missing-report guard changed the aggregate AP build ID to
`e08283ff`, without changing radio/handshake behavior. The additional
`hardware/control04-*` trial passed with station `5265041c`: one M2/M3/M4,
one independently verified protected echo, no timer-generated retries.
Its final full 16 MiB/4 MiB readbacks matched the same original hashes above;
both devices were restored again after this last control.

These are bounded legacy-client connections against one vendor AP
implementation. They do not close B3's mesh/persistence requirements, E2 rekey/
group traffic, E3/B2 hour-long operation, or negotiated HT qualification.

### E2. Group traffic and rekey

- Recorded completed criterion: Implement GTK-backed broadcast/multicast decryption, key-index selection
  and replay state, including GTK replacement and permitted overlap.
- Recorded completed criterion: Implement the group-key handshake and pairwise rekey with bounded
  retries, correct key activation and no PN reset under retransmission.
- Recorded completed criterion: Verify real broadcast/multicast traffic and at least three forced group
  rekeys and three pairwise rekeys during traffic, without plaintext leakage,
  duplicate delivery, incorrect key reuse or unrecovered link loss.

**2026-10-23 implementation:** four GTK slots select CCMP keys by the on-air
KeyID, maintain independent replay state for 16 QoS TIDs plus non-QoS, initialize
and monotonically advance RSC floors, and allow a two-second previous-slot
overlap. Same-key reinstallations preserve PNs; aliasing a live GTK into a
different slot is rejected. Expiry and terminal failure wipe retired keys.
Authenticated group traffic is decrypted separately from pairwise fragment/
reorder state; plaintext data is rejected and counted.

The RSN group-key exchange validates MIC, AES-wrap integrity, pinned RSN when
present, unique GTK KDE, RSC, replay and duplicate identity. G2 uses a protected
pairwise envelope. The authenticator owns G1 retransmissions; duplicate
responses have the existing eight-second/four-retry bounds, rather than an
invented third handshake message or an idle-link timeout.

Encrypted pairwise M1 starts a fresh SNonce/PTK epoch while old-key traffic can
continue. A secret HMAC-based per-session nonce sequence is seeded from the PMK
and initial OS-random SNonce, advances on fresh M1 only, and rejects wrap.
The PMK therefore remains private in RAM until explicit teardown/failure.
M2 and M4 use the new KCK but old data-key envelopes; accepted M3 commits the
new TK and its fresh PN epoch exactly once. The previous PTK/PN state remains
available only for bounded M3/M4 loss recovery until new-key data proves
completion, then is wiped. Same-key GTK PN state survives pairwise rekey.
Concurrent group/pairwise key exchanges are explicitly rejected; this is not
WPA3, enterprise, PMF, or HT qualification.

Native ASan/UBSan tests cover three group replacements, broadcast/multicast on
all 17 replay lanes, RSC carry/selection, overlap expiry, encrypted G1/G2,
same-key PN preservation, and three encrypted four-way rekeys during traffic.
They exercise lost M4 with old-key encrypted M3 retransmission, fresh nonces,
old/new MIC and envelope ownership, duplicate PN preservation, deadline wrap,
malformed/MIC/replay rejection and key/plaintext wiping. These are NOT RF.

Station mode 34 and AP mode 7 add a separate bounded protected-traffic fixture.
Mode 32 is already B1 lifecycle; the first `traffic01` attempt exposed that
mode collision and was rejected, not counted as security qualification.
A compile-time mode guard and a negative oracle fixture now prevent its reuse.
The AP final snapshot uses the same atomic report/trace publication as E1.
The E2 verifier binds raw identities, security readbacks, challenge tokens,
counts, duration and controller exits. Full E2 acceptance requires at least
three group plus three pairwise rekeys, each with its own subsequent protected
traffic proof; `--traffic-only` explicitly leaves the milestone unqualified.

**Hardware investigation and result:** `traffic02` independently received
26 protected unicast echoes, two broadcasts and three multicasts before the
old six-second whole-loader budget expired. E2 now explicitly sets a bounded
55-second budget, with native tests crossing six/35 seconds and checking its
exact expiration. `traffic03` then completed 35.000066 seconds with 346 echoes
and 70 group receptions, but correctly failed the host acceptance gate:
the shared association-evidence predicate still allowed only modes 16/18.
The protected-mode predicate now also covers mode 34; native tests assert
that 34 passes valid captured association evidence while B1 mode 32 does not.
The failed runs remain retained, not reclassified or spliced into a pass.

Campaign `build/clean-rx/e2-20261023/hardware/traffic04-*` passed the complete
35-second traffic-only control with station build `7904df1d` and Pico AP
`b91f9a24`: 348 independently echoed protected unicast challenges, 17 broadcast
and 52 multicast receptions. The AP submitted 68 broadcast and 68 multicast
challenges; this is group-delivery evidence, not a lossless-link claim.
Both endpoints retained authorization, no WPA or transport error occurred,
and plaintext data was not accepted. The separate `traffic04-e1-*` connection
also passed M1/M2/M3/M4 and a fresh protected echo with these same builds.

Both AP interval probes actually returned `BCME_UNSUPPORTED` (-23).
No group or pairwise RF rekey occurred. Running the full E2 oracle without
`--traffic-only` was explicitly verified to reject this archive because
GTK rekey control is unavailable. At that point E2's final RF checkbox remained
open: three forced rotations of each kind required a controllable authenticator.
The native rotations above are not substituted for this hardware requirement.

Full 16 MiB badge and 4 MiB Pico originals were restored/readback-compared
after every campaign, including the last `traffic04`/E1 control. Their SHA-256
hashes match the originals recorded under E1 above. The final campaigns
restore only the preflight-bounded flashed prefix, then read and compare the
entire flash; a full-image restore is retained as an explicit failure fallback.
The other badge was untouched. No commit or push was performed.

**2026-10-23 forced-rekey follow-up:** AP mode 8 now provides an explicit,
host-owned single-station authenticator rather than relying on the unsupported
vendor interval variables. Association/CCMP remain in the vendor AP, but its
PMK submission is deliberately suppressed for this profile. A private
OS-random seed drives fresh ANonces and GTKs; the RP2350 validates response
MICs/replay and installs GTK/PTK once after G2/M4. Port authorization follows
the first validated M4. Cached EAPOL retries use bounded budgets without
regenerating nonces or reinstalling keys. Terminal failure/teardown wipes
private state. The wire key layout follows the documented Broadcom
`wl_wsec_key` structure; no proprietary forcing API or firmware patch is used.

Initial hardware probes found `wpaie` GET unsupported and AP BSS-info without
RSN IEs. Those probes were removed, not replaced with a success-shaped
fallback. The explicit PSK-CCMP fixture uses the bundled firmware's observed
22-byte RSN (`0x000c` capabilities); M3's initial mismatch correctly failed
the supplicant's pinned-RSN check. The station now archives its public
observed RSN separately, and the full oracle requires that exact profile.
Unicast/group rate readbacks retain the vendor `0xc0000000` override flags
while verifying a 1 Mbps rate, following the existing rate-mask convention.

Group-first hardware attempts (`rekey06`, `rekey08`) completed one GTK
rotation but timed out before a pairwise M2 amid substantial receive/FCS
loss. A PTK-first alternating fixture passed `rekey09`. That diagnostic
pass predates the final stronger oracle: it is not substituted for the
final runs. The cause of the group-first RF failure is not established,
and that ordering remains outside the qualified RF scope. Native tests
exercise group-first rotation, old-key encrypted M3 retry after lost M4,
lost G2, per-epoch data proof, single installation and backend/MIC/timeout
failure wiping using the actual station state machine.

The final oracle additionally binds AP M2/M4/G2 and installed-epoch counters,
group challenge submission after each new GTK, protected station reception
after each new PTK, authenticator/controller status and pinned-rate readbacks.
Both endpoints must independently prove progress; plaintext application-frame
observations fail full acceptance. `--rekey` selects this mode and the full
gate; `--group-traffic` stays explicitly non-qualifying.

Final station `3b5748a1` and Pico AP `b40abc4f` passed all three retained
35-second RF runs under `build/clean-rx/e2-finish/hardware/`:

| Run | Channel | Unicast echoes | Broadcast RX | Multicast RX | Group / pairwise rekeys |
| --- | ---: | ---: | ---: | ---: | ---: |
| `rekey10` | 1 | 338 | 67 | 2 | 3 / 3 |
| `rekey11` | 6 | 339 | 68 | 10 | 3 / 3 |
| `rekey12` | 11 | 338 | 68 | 10 | 3 / 3 |

Every installed station epoch has subsequent protected traffic proof. The AP
independently confirms nine G2s, twelve M2/M4 pairs including the three initial
connections, nine post-GTK group probes and nine post-PTK protected receptions.
It receives/echoes the same 1,015 unicast challenges and submits 203 frames of
each group class; only 22 multicast receptions occur, so no lossless-link
claim is made. Zero plaintext application observations and zero terminal
WPA/transport errors occur. `rekey11-e1` passes a separate fresh E1 RF
regression with these exact builds.

Full 16 MiB badge and 4 MiB Pico backups/readbacks compare identically after
every follow-up RF campaign. An initial restoration-helper CLI ordering error
was corrected immediately and the originals then fully verified before any
further test. The other badge remains untouched. Station, Pico AP and default
badge AP builds/native tests all pass with ASan/UBSan. E2's bounded legacy RF
checkbox is now complete; this does not close E3, HT/BA, multi-station,
arbitrary-order/authenticator, persistent-link or hour-soak qualification.

### E3. Sustained protected operation

- Recorded open criterion: Integrate secure key teardown on disconnect, reset, cancellation and
  failure; reject unsupported security modes explicitly.
- Recorded open criterion: Verify CCMP MIC rejection, replay rejection and PN exhaustion behavior
  for unicast and group traffic, including supported traffic classes.
- Recorded open criterion: Pass B2's 60-minute soak under WPA2 with rekeys and idle periods.
  Require protected traffic evidence at both endpoints, not just authorization.


