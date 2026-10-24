# C07: tool and profile ownership

## Ownership boundaries

| Location | Responsibility |
| --- | --- |
| `src/` | Standalone CYW43439 controller, shared contracts and authoritative build selections; no libc, collectors, USB libraries, peer SDKs or campaign infrastructure |
| `host/` | Portable code used by RP2350 firmware, currently WPA2 crypto; not controller input |
| `badge/`, `messenger/` | RP2350 applications and their application-native tests |
| `peer/` | Independent vendor-firmware peer, authenticator/network runtime, shared evidence contracts and peer-native tests; not source-controller input |
| `vendor-burst/` | Diagnostic RP2350 reference/stimulus/observer/monitor applications and their models |
| `tests/` | Controller-native unit/integration tests and explicit shared test fixtures |
| `tools/{badge,messenger,peer,vendor-burst}/` | Desktop collectors, hardware campaign runners and offline evidence verifiers for each application |
| `tools/*.cjs` | Offline cleanup/profile/evidence tooling and its tests |

Collectors retain the same desktop libraries and shared contracts, but
are no longer colocated with application firmware. Peer runtime headers
used to correlate independent evidence stay with the peer; synthetic
shared test fixtures stay in `tests/fixtures/`. Neither is copied into
the controller.

## One controller-selection catalogue

[`src/profiles.mk`](../src/profiles.mk) is the authoritative Make-only
catalogue. It has no recipes, external includes or tool invocations.
[`firmware.mk`](../src/firmware.mk) selects two independent dimensions:

- `PROFILE=runtime|diagnostic|sdio-test` retains the C05 behavior gates
  and profile IDs. The default is still `runtime`.
- `SELECTION=<name>` selects feature flags from the catalogue. The default
  is `default`, with no extra feature flags. Unknown names fail explicitly.

Selections include `ht-data`, `ht-response`, `wmm-edca`, passive observer
and reset, `copy-scope`, `early-rx`, `early-prefix`, SDIO variants and the
reference campaign selections. Diagnostic features still require an
appropriate `PROFILE`; selecting a name does not bypass the C05 checks.
`EXTRA_CFLAGS` remains an explicit experiment escape hatch.

Examples, with paths relative to each Make invocation:

```sh
# Ordinary controller only; no test/tool/USB dependencies.
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx/runtime \
  PROFILE=runtime SELECTION=ht-data

# Explicit diagnostic selection and its native model.
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx/scope \
  PROFILE=diagnostic SELECTION=copy-scope
make -C clean-rx/tests BUILD=../../build/clean-rx/scope-tests \
  PROFILE=diagnostic SELECTION=copy-scope test copy-scope-test
```

Controller build flags and the profile sidecar record the selection.
Changing selection, including switching between synonymous names,
updates the sidecar and forces the existing complete rebuild chain.
The native named-profile entry points still replace caller extra flags
with their reviewed selection, as before.

Messenger, badge, native named tests and root SDIO recipes now consume
the catalogue. JavaScript evidence runners query it through Make rather
than keeping another flag list. Frozen historical records and independent
test expectations are not rewritten.

Reference Make targets keep their existing names, role switches and
parameter overrides. Their wrapper recipes select named catalogue entries;
the separate source-built stimulus controller uses its own catalogue
entry. `REF_NEXT_FIELD`, variable/stream knobs, recovery/injection knobs
and stimulus/handoff/guard defaults remain Make-evaluated. Direct
`REF_FLAGS` experiments remain supported; named wrappers retain their
previous precedence over caller `REF_FLAGS`.

Use the existing campaign target, for example:

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-on
```

This selects both controller flags and the appropriate RP2350 roles.
Selecting only a controller entry is not a replacement for role selection.

## Firmware-only versus aggregate builds

Existing aggregate targets retain their behavior: `all` in badge/messenger,
`ht-peer` and `reference` build both firmware and desktop collectors.
New explicit targets allow firmware builds without desktop-tool inputs:

| Makefile | Firmware only | Desktop tools |
| --- | --- | --- |
| `badge/Makefile` | `firmware` | `tools` |
| `messenger/Makefile` | `firmware` | `tools` |
| `peer/Makefile` (from repository root) | `ht-peer-firmware` | `ht-peer-tools` |
| `vendor-burst/Makefile` (from repository root) | `reference-firmware` | `reference-tools` |

Firmware build IDs no longer hash collector source. They still bind
firmware sources, shared headers, build configuration and controller/
vendor assets as applicable. Collectors consume the same generated
firmware build ID. Moving files and changing Makefiles naturally changes
build IDs: rebuild matching firmware/collectors rather than mixing new
collectors with old installations. Wire formats, challenge/session
identities, output filenames, campaign flags and evidence rules are
unchanged.

## Tool-path migration

Tool paths intentionally move; old script paths are not compatibility
wrappers. Migrate invocations as follows:

| Previous path | Replacement |
| --- | --- |
| `clean-rx/badge/collector.c`, badge `*.sh` and `*_verify.cjs` | Same filenames under `clean-rx/tools/badge/` |
| `clean-rx/messenger/collector.c` | `clean-rx/tools/messenger/collector.c` |
| `clean-rx/peer/collector.c`, `run.sh` | Same filenames under `clean-rx/tools/peer/` |
| `clean-rx/vendor-burst/collector.c`, `run.sh` | Same filenames under `clean-rx/tools/vendor-burst/` |

Build entry points and output binary locations are unchanged. Shell
runners resolve the repository root from their new location. Internal
runner/verifier calls, Make recipes and current usage guides use the new
paths. Earlier development reports remain historical records.

For example, an **offline** verifier self-test is now:

```sh
node clean-rx/tools/badge/qualification_verify.cjs --self-test
```

Hardware runners remain separate explicit commands, never prerequisites
of firmware builds or offline tests. They still require matched installed
images, deliberate device arguments and separate authorization.

## Offline acceptance

- All 27 cleanup-tool tests pass, including named-versus-explicit image
  equality, parameterized reference selections, firmware-only dry runs,
  relocated-script syntax checks and native selection forwarding.
- All 74 retained diagnostic flag sets and 66 campaign resolver entry
  points reproduce C01 controller/PSM bytes from a standalone source copy.
  The catalogue lists 70 reference Make entries because two new
  firmware/tool leaves join the original 68; campaign selections remain 66.
- Six ordinary runtime profiles retain C05 controller/PSM bytes and
  mutable-storage addresses/sizes.
- Seven diagnostic profiles retain frozen hashes and historical native
  coverage: 24 passed, six disabled and twelve not selected.
- Runtime/diagnostic and named native suites, badge/messenger builds and
  tests, reference/handoff builds/tests, relocated peer collector and peer
  native tests, root transport tests/collector and all five SDIO variants
  pass.

Reproduce offline evidence with new output directories:

```sh
node --test clean-rx/tools/*.test.cjs
node clean-rx/tools/c02-isolation.cjs build/clean-rx/c07-isolation-NEW
node clean-rx/tools/c06-runtime.cjs build/clean-rx/c07-runtime-NEW
node clean-rx/tools/c01-baseline.cjs build/clean-rx/c07-matrix-NEW
make -C clean-rx/tests BUILD=../../build/clean-rx/c07-tests-NEW test
make -C clean-rx/tests BUILD=../../build/clean-rx/c07-profiles-NEW \
  test-copy-scope test-early-rx test-recovery
```

Local evidence is under `build/clean-rx/c07/`, with adjacent
`c07-isolation`, `c07-runtime` and `c07-matrix` manifests.
No devices were flashed or exercised, and no RF campaign was run.
The separate, unperformed C05 hardware acceptance gate is unchanged;
C07 does not establish feature parity or radio reliability.
