# C08: enforced boundaries and final offline acceptance

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

The C01-C08 structural cleanup is complete. This record is offline
acceptance, not vendor-feature parity or RF/reliability qualification.
Current contributor instructions are in
[development and validation](../../guides/DEVELOPMENT.md); unresolved behavior
is tracked separately in [feature and reliability limits](../../issues/README.md).

## Enforcement

[`controller-boundaries.cjs`](../../../tools/controller-boundaries.cjs) is shared
by current-source isolation builds and diagnostic baseline capture. It
checks:

- Source includes, including inactive branches, stay within the standalone
  tree; test/fixture/host/tool inputs and symlinks are rejected.
- Evaluated Make sources, objects, headers and included Makefiles are
  contained, with the reviewed source and link contracts.
- Compiler flags retain freestanding isolation; library links, external
  include flags and assembler/linker file imports fail.
- Actual compiler dependency output remains internal. Runtime has no
  diagnostic implementation dependencies; selected diagnostic/SDIO
  implementations must appear.
- Linked entry symbols are present, undefined symbols are absent, and
  global exports stay within the reviewed interface. Runtime-library and
  compiler-helper symbols are rejected.

The checker stays outside controller production prerequisites. A copied
`src/` still builds without Node, Git, tests or any host/development code.
Policies are explicit review points, not allowlists regenerated from the
binary under test.

Five C08 regression tests inject escaped/dormant/libc/computed includes,
test headers, symlinks, assembler/linker imports, external Make/header
inputs, extra source/object/library/compiler inputs, runtime diagnostic
dependencies, omitted diagnostics and unexpected/undefined symbols.
They also make selected copy/early-RX/recovery coverage disabled or absent
and verify rejection. Reused evidence directories cannot produce a success
manifest.

## Repeatable acceptance

[`clean-rx/Makefile`](../../../Makefile) exposes the gate:

```sh
make -C clean-rx check BUILD=../build/clean-rx/acceptance-NEW
make -C clean-rx check-tools
```

`check-tools` is the fast tooling-only subset. The complete gate delegates
to [`check.cjs`](../../../tools/check.cjs), records commands and per-stage logs,
and validates counts, bytes, PSM geometry and required executed coverage.
It hashes production/test/tool inputs and relevant general-tree native
dependencies, rejects input drift and verifies the frozen baseline before
and after execution. A failed stage or policy check records failure and
returns nonzero. An interrupted `in-progress` run is not acceptance.

## Final local results

The final command was:

```sh
make -C clean-rx check BUILD=../build/clean-rx/c08-final
```

Evidence: `build/clean-rx/c08-final/manifest.json`, status `complete`,
hardware `not-run`, all seven stages passed, 186 validation inputs hashed.

| Stage | Result |
| --- | --- |
| Tooling and mutation tests | 32 passed, zero failed |
| Standalone diagnostic isolation | 74 flag sets and 66 reference selections match C01 controller/PSM bytes |
| Ordinary runtime comparison | Six profiles match C05 controller/PSM bytes and mutable-storage addresses/sizes |
| Historical diagnostic capture | Seven profiles retain frozen binary hashes, PSM address/size/content and expected coverage |
| Runtime native suite | Passed |
| Diagnostic native suite | Passed |
| Named native profiles | Copy-scope, early-RX and ordered/prefix recovery passed |

The historical coverage total remains 24 passed, six disabled and twelve
not selected. Required selected models must execute; historical disabled
cases are not counted as passes. Existing behavior tests exercise the real
dispatch guard across all command/version bytes and distinguish runtime
unsuppressed responses from diagnostic suppression.

The C01 baseline SHA-256 remains:

```text
3c2941a6b6fc684722b3ffbc9896ad28dddb6b04adf60146a74cca5de4454bf2
```

C08 changes development checks and documentation, not controller source,
wire layouts, PSM instructions or firmware behavior. Desktop collector
builds and hardware campaigns are not prerequisites of the acceptance
command.

## Hardware and reliability limits

No C08 device discovery, flashing, booting, collector execution or RF
experiment was performed. No hardware requalification result is claimed.
The prior one-device C04 receive-only test does not qualify C05's deliberate
runtime behavior changes; that authorization has ended.

The [C05 hardware gate](../../guides/HARDWARE_TRIALS.md#runtime-profile-acceptance)
remains unperformed. New authorization, matched images and independent
on-device evidence with backup/restore safeguards are required before
running it. Missing replies, corrupt captures, general HT/A-MPDU and
SIFS-timed Block ACK gaps remain separate work, not unfinished file cleanup.


