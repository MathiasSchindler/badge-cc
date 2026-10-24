# HT20 peer profiles and station policy

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## Association-response profiles and runtime HT/WMM updates

The 2026-10-18 implementation adds independent, atomic association-response
HT/WMM parsing and fixes a real refresh-propagation bug:
`clean_station_refresh` copied HT/WMM-only beacon changes into its argument
but returned zero when ERP/basic-rate/slot fields were unchanged.
`station_receive` refreshed a temporary AP record and copied it back only
when that return value was nonzero, so those updates were discarded.
Refresh now compares every represented HT/WMM field, not just the WMM
parameter-set nibble or legacy fields. Same-counter AC changes and
parameter-set wrap are detected.

Accepted association-response optional IEs have their own snapshot. The
parser retains the existing address, subtype, fragment, FCS, AID, status
and complete IE-boundary gates. It commits the AID and snapshot only after
the entire successful response validates. Missing response elements remain
missing instead of inheriting the discovery beacon. Malformed/duplicate
optional elements mark HT fallback without preventing the existing legacy
association. Auth responses and rejected/malformed association responses do
not modify the saved output.

Subsequent matching beacon updates preserve the association snapshot and
increment a saturating HT/WMM change counter. The compact report uses
response capabilities/MCS constrained by the latest beacon and current
operation/protection/EDCA observations. It never increases the response MCS
set or adds 40-MHz capability. Source 0 means discovery, 1 means accepted
response, and 2 means response plus subsequent beacon changes. Departure
handling clears the retained association profile and counter.
The report is now 48 bytes at detail 1040..1087; byte 19 carries source
and bytes 44..47 carry the counter. Saved association evidence and the
packet-length field remain separate.

This does **not** activate HT/WMM advertisements or install AP-derived
command-15 policy into the legacy station. That would restrict its existing
legacy/WPA2 TX path to the clocked diagnostic fixture. Actual negotiated
HT/QoS enablement remains behind the outstanding MAC/receive gates.
Unexecutable WMM contention exponents also produce explicit WMM fallback.

Native ASan/UBSan cases cover:

- Successful response HT/WMM parsing and feeding its AC records into the
  shared EDCA engine.
- Every truncated IE boundary with reconstructed valid FCS, allowing only
  complete optional-element boundaries; wrong subtype, protected flags,
  RA/TA/BSSID, fragmentation and AID failures.
- Atomic rejection/status handling, unchanged outputs for authentication,
  absent response capabilities, duplicate HT/WMM and unsupported execution
  exponents.
- WMM-only changes, changed ACs with an unchanged counter, parameter-count
  wrap, MCS intersection, protection/channel fallback, removed capabilities,
  bad-FCS refresh atomicity, counter saturation and departure clearing.
- Existing open/WPA2 association-request identity and preserved association
  evidence after report publication.

Default and HT/extended-RX/response/EDCA badge suites pass, as do both
freestanding builds with no undefined symbols. Final badge builds are
`438812da` (default, text 79,728/BSS 221,312 bytes) and `e3bb50ff`
(gated, text 83,496/BSS 225,800 bytes). The compiled `run` stack frame is
904 bytes in both profiles. Explicit project byte-copy helpers avoid
compiler-generated `__aeabi_memcpy` dependencies from structure assignment.
Controller source/binaries were not changed for this increment.

Hardware attempts against two nodes matching the configured local SSID
were retained, without displaying or provisioning its password:

| Badge build / channel | Observation |
| --- | --- |
| Default / 1 | AP discovered; one request ACK observed, no validated authentication response; association not reached |
| Default / 11 | No usable target AP discovery; host input error, no controller error |
| Gated / 1 | AP discovered; four request ACKs observed, no validated authentication response; association not reached |

All three report `ht_advertised:false`, profile source 0 and zero runtime
association updates. These failures are not successful association,
negotiated-HT qualification or RF validation of the new source-1/2 paths.
They do not establish the cause of the missing authentication responses.
Evidence: `build/clean-rx/badge/d2-association-{default-ch1,default-ch11,edca-ch1}.*`.

Before writing badge A, its complete flash was freshly saved and verified
as `build/clean-rx/badge/d2-association-start-A-full.bin`. After all trials,
the full original flash was restored; the independent full 16-MiB readback
`build/clean-rx/badge/d2-association-restored-A-full.bin` matches exactly.
Badge B was not modified. Badge A remains in BOOTSEL.

## Subsequent AP reply investigation and bounded RX staging

The [AP authentication investigation](../experiments/rx/AP_AUTHENTICATION.md)
records independent monitor evidence, negative radio/rate controls, the
eight-slot station RX FIFO and revised service diagnostics. Legacy fixtures
pass, but local-AP reply delivery remains unresolved. The final gated matrix
passed 10/16 cells; identical-payload old/new replays passed 2/4 each and
retain independently invalid, repeat-identical SHM captures. Neither
negotiated HT operation nor receive reliability is qualified.

The subsequent
[matched-amplitude and rejected-authentication controls](../experiments/rx/AP_AUTHENTICATION.md#follow-up-amplitude-controls-and-rejected-authentication-capture)
retain the original default amplitude. They add exact challenge replay and
pre-host rejection evidence, but do not resolve receive reliability or
complete negotiated HT operation.

## Independent capability/MCS/airtime pieces (2026-10-18)

The first D2 roadmap item is now split into independently testable pieces.
The raw mixed-format engine and its airtime calculations already existed;
this change completes a conservative PHY candidate/configuration layer,
without declaring the mandatory live MAC ready.

- [ht20.h](../../src/ht20.h) serializes the unadvertised HT20/800-ns/BCC/one-stream
  target capability IE. It disables optional PHY modes and encodes the
  minimum A-MSDU/A-MPDU target, conservative 16-us spacing, MCS0..7, a
  defined equal TX/RX MCS set and 65-Mbps maximum RX rate.
  Layout, rate units, SMPS and spacing encodings were checked against the
  pinned public HT definitions linked above; no external code was copied.
- `clean_ht20_select_mcs` handles all sparse masks and explicit ceilings.
  `clean_ht20_configure` validates exact per-frame MCS/length/mask inputs
  atomically, then derives controls, symbol count, PHY rate and airtime.
  [main.c](../../src/main.c) now uses the result in actual HT template TX,
  including its existing completion/ACK/drain deadlines. Register
  readbacks, HT-SIG guards and legacy-format programming remain intact.
- [ht_station.h](../../badge/ht_station.h) computes an explicit candidate from
  AP capabilities, operation, basic MCS, WMM and protection. Reserved SMPS
  and A-MPDU-parameter encodings fail HT planning without disabling legacy
  association. Local HT-data/extended-RX-disabled builds add fallback bit
  128, in addition to the unconditional mandatory-MAC bit 32.
- [station.h](../../badge/station.h) shares one response/runtime constrained
  profile between the candidate API and the live peer report. Beacons
  cannot enlarge the accepted association's MCS set or 40-MHz capability;
  changed channel, missing operation, unsupported basic MCS and departure
  invalidate/recompute the candidate without activating HT.
- [collector.c](../../tools/badge/collector.c) exposes the unadvertised target IE
  and candidate MCS mask/selection, always with HT inactive. Finite
  probe/data/ACK records also describe the requested format, width, GI,
  streams, rate, symbols, calculated airtime and L-SIG length.

The target is **not** a claim that its full MAC receive obligations can
currently be met. No association builder calls the capability serializer.
Association-frame identity/IE tests still require no HT capability or
operation advertisement. A candidate MCS is not a negotiated rate.
HT mixed-format means an HT PPDU with its legacy-compatible preamble;
it does not establish reliable interleaving of legacy and HT station traffic.

Validation: controller/badge builds and native suites passed in default,
HT-data/extended-RX, HT-data-only, HT/response/EDCA/WMM and finite one-copy
aggregate profiles. ASan/UBSan suites passed; both freestanding ELFs in
each profile have no undefined symbols. New tests cover all 2,048
mask/ceiling pairs and 2,048 exact-MCS/mask combinations, invalid-input
output preservation, every supported length at every MCS, independent
88-byte airtime/rate vectors, exact target-IE bytes and parsing, malformed
reserved encodings, sparse response/beacon intersections, constrained
ceilings, runtime channel/operation/basic-MCS changes and association reset.
Default/partial builds explicitly suppress candidates. Existing
legacy-association, protected RX, aggregate and response regressions pass.

Reproduction uses separate build directories, for example:

```
FLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1'
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-d2-phy-plan-ht EXTRA_CFLAGS="$FLAGS" all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-phy-plan-ht \
  CONTROLLER_BIN=../../build/clean-rx-d2-phy-plan-ht/cyw43_rx_fw.bin \
  EXTRA_CFLAGS="$FLAGS" all test
```

Logs are `build/clean-rx/badge/d2-phy-plan-{default,ht,tx-only,gated,handoff}-{controller,badge}.log`.
There were **no hardware writes or new OTA qualification** in this step.
Earlier radio evidence remains scoped to the tested raw/diagnostic paths.
Actual association activation and independently qualified mixed traffic
remain unchecked in the root roadmap.

## Readback-verified station policy commits (2026-10-18)

The existing controller policy commands now complete only after application
and readback, not on deferred queueing. The station therefore does not publish
an accepted WMM/runtime profile or advance to its next command on queueing
alone. Pending commands are single-flight; readback, timeout, cancellation and
unexpected-command failures are explicit. PSM is paused while the response
tables, pinned addresses, QoS policy and contention controls are verified.
Held capture data is not rewritten by policy installation.

The [policy commit campaign](../experiments/mac/EDCA_AND_WMM.md#readback-verified-policy-commit-campaign-2026-10-18)
records three build/test profiles, exhaustive table/readback faults,
independent WPA2 positive/negative/legacy controls and all five final clocked
HT trials. Two HT exchanges completed; three did not. No SIFS, receive-size,
protection or live BA qualification follows from those results.

This closes a prerequisite in the root activation item, **not that item**.
No actual HT station/controller activation command or HT association offer
was added. Mandatory live aggregate delivery/Immediate BA and qualification
remain required; the unconditional mandatory-MAC fallback is unchanged.


