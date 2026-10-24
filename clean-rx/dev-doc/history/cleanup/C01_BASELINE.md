# C01: source inventory and offline baseline

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

Date: 2026-10-24.

This implements the inventory/baseline milestone in
[the cleanup plan](../../../cleanup.md#c01---establish-the-inventory-and-reproducible-baseline).
No firmware source, build recipe, ABI, command or PSM behavior is changed.
It does not implement C02 source isolation or qualify RF behavior.

## Authoritative records and reproduction

- [Capture tool](../../../tools/c01-baseline.cjs): Node standard library only;
  uses the existing Make recipes and host/LLVM tools.
- [Capture-tool tests](../../../tools/c01-baseline.test.cjs).
- [Frozen initial capture](../../c01-baseline.json): per-file classifications,
  source hashes, literal includes, build owners, flag defaults/references,
  reference recipes, actual controller dependencies/objects, artifact and PSM
  hashes, sizes and individual test outcomes.

Run from the repository root, using a **new** evidence directory:

```sh
node --test clean-rx/tools/c01-baseline.test.cjs
node clean-rx/tools/c01-baseline.cjs build/clean-rx/c01-local-NEW
```

An optional second argument writes a new JSON record outside the evidence
directory. Existing evidence directories and records are rejected, never
cleaned or overwritten. Do not replace the frozen initial capture when
implementing later milestones.

The capture builds seven controller profiles with `CC=clang`, separate
absolute `BUILD` directories and explicit `EXTRA_CFLAGS`. It builds native
tests through their existing recipes and runs each selected executable
individually. No flashing, collector invocation, network access or RF
campaign is performed.

Each profile retains build output, dependency files, linker map, symbol and
section listings, controller ELF/binary, objects, extracted linked PSM bytes
where present, and individual test logs. The manifest records exact command
arguments and compiler flags, not just profile labels. The JSON record is
portable; its raw-evidence directory is local and ignored by Git. Transfer
that directory separately when raw evidence is needed.

The inventory covers Git-tracked and non-ignored untracked files under
`clean-rx/`; ignored generated output is excluded. Its own not-yet-created
JSON record is necessarily absent from the initial inventory. Documentation
and build infrastructure are classified separately rather than being called
firmware. Mixed files have multiple roles. These are file-level ownership
classifications, not claims that every function in a file has the same role.

## Ownership and file inventory

The JSON record lists every inventoried file individually, including its
hash. The following explains the ownership decisions behind those entries.

### Controller source directory

| Files under `src/` | Role and boundary |
| --- | --- |
| `main.c` | Controller orchestration plus embedded firmware diagnostics and fault injection |
| `pmu.c`, `radio_phy.c`, `startup.S`, `cyw43.ld`, `types.h` | Controller runtime/startup/link layout |
| `cyw43_hw.h` | Controller hardware definitions and host-visible mailbox/capture contracts |
| `mac.c`, `mac.h`, `block_ack.c`, `block_ack.h` | Portable runtime used by controller and host consumers; being compiled does not imply every function survives linker GC |
| `sdio_ring.h`, `phy_rates.h`, `tx_diag.h` | Portable transport/rate/diagnostic contracts and helpers |
| `frame_limits.h`, `unicast.h` | Mixed runtime geometry, shared ABI, configuration and diagnostic/test contracts |
| `ht20.h` | Mixed runtime calculations/validation, shared ABI and development fixture generation |
| `psm_ucode.c`, `psm_ack.h`, `ht_observer.h` | Controller PSM program and diagnostic/experimental composition |
| `copy_scope.h`, `rx_recovery.h`, `early_rx.h`, `observer_dma.h` | Experimental firmware-side capture/recovery/DMA helpers or program fragments |
| `sdio_test.h` | Synthetic transport firmware-test patterns |
| `mac_soak.h` | Shared campaign fixture/evidence helpers; not an ordinary controller input |
| `wpa2_crypto.c`, `wpa2_crypto.h` | Portable host-side cryptography, not in the controller source list |
| `psm_ucode_test.c`, `mac_test.c`, `block_ack_test.c`, `rx_recovery_test.c`, `copy_scope_test.c`, `early_rx_test.c` | Native tests, not controller-linked test executables |
| `Makefile` | Mixed controller/native-test build infrastructure |

### Other directories

| Location | Runtime ownership | Tests and development ownership |
| --- | --- | --- |
| `badge/` | RP2350 `main.c`, loader/assets, station/HT/WPA2/network/RX headers and implementation | `*_test.c` are native tests; `collector.c`, shell runners and JS verifiers are development tools. `frames.h` and station headers also expose fixture/evidence helpers: host ownership is not controller purity. |
| `messenger/` | RP2350 application, link state machine and screen helpers | Collector, latency analysis and `link_test.c`; role/baseline switches select host behavior, not different controller capabilities |
| `peer/` | Independent vendor-firmware RP2350 peer; network/authenticator implementations | Peer headers also define campaign protocols, evidence and fixtures; collector/runners are tools and `*_test.c` are native tests |
| `vendor-burst/` | Diagnostic RP2350 vendor/stimulus/observer/monitor programs, not ordinary controller runtime | Protocol/app headers are shared diagnostic fixtures; collector/runner and native protocol/monitor tests are separate consumers |
| `tools/` | No target runtime | C01 capture and its native Node tests |
| `doc/`, `dev-doc/`, Markdown files elsewhere | No target runtime | Interface guides, development/history records, roadmap and baseline evidence |
| Every `Makefile` | No target runtime | Build infrastructure; dependencies must be assessed per target |

## Actual controller inputs and boundary violations

All seven selected profiles compile these controller objects:

```text
startup.o
main.o
pmu.o
radio_phy.o
psm_ucode.o
mac.o
block_ack.o
```

The linker uses `src/cyw43.ld` and garbage-collects unused sections.
The capture retains the actual link command, map and symbol listing, so a
compiled object is not mistaken for proof of linked/executed functionality.

Compiler-generated dependency traces identify two external controller
headers: repository `src/shared/stdint.h` and `src/shared/bytes.h`. The
controller Makefile also adds `-I../../src/shared` and names an external
type header as an object prerequisite. This violates the intended source
boundary, although the helpers themselves are freestanding, not libc.

Native tests have a wider boundary:

- PSM/MAC and diagnostic tests link the general-tree
  `src/tools/diagnostics_logic.c` as specified in their recipes.
- MAC tests import `badge/station_rx.h`.
- Copy-scope and early-RX tests import `vendor-burst/protocol.h`.
- Host compiler/SDK dependency metadata can appear in native dependency
  traces. It is not a controller dependency or a target libc link.

Literal includes are inventoried even in files not selected by the matrix.
Actual controller dependency traces are profile-specific; test traces cover
each selected test translation unit, while the build logs retain its full
link recipe. External helper source/header hashes are recorded separately.

All selected controller images linked without undefined symbols under the
existing freestanding/no-standard-library flags. The source still reads
on-chip ROM calibration tables; this is a hardware dependency, not an
external source/library dependency.

## Profile catalogue and lifecycle

The frozen record inventories all **68 retained `reference*` Make targets**
with owning file/line, root-relative invocation, purpose, literal recipes
and explicit coverage gaps. This includes wrapper aliases, controller/test
helpers and conditional test recipes, not 68 distinct firmware binaries.
Recipes are retained as source text, not represented as fully expanded
commands. Follow recursive aliases and Make conditionals in the owning
Makefile; arbitrary override combinations are not exhaustively validated.

| Owner | Selection and reproducible entry point | C01 coverage |
| --- | --- | --- |
| `src/Makefile` | `make -C clean-rx/src CC=clang all test`; `EXTRA_CFLAGS` selects raw controller variants | Default and selected controller variants captured; `src/` local build output is not used by the capture |
| `badge/Makefile` | `make -C clean-rx/badge all test`; default controller inherits `EXTRA_CFLAGS`; `CONTROLLER_BIN` selects a prebuilt image | Consumer inventoried; host/collector and full integration suite not rebuilt |
| `messenger/Makefile` | `make -C clean-rx/messenger pair test`; fixed HT data/extended-RX flags; `ROLE` and `BASELINE` are application switches | Matching controller flags captured; application/collector tests not run |
| `peer/Makefile` | `make -f clean-rx/peer/Makefile ht-peer ht-peer-test`; `HT_PEER_PICO=0` or `1` | Vendor peer has no source-controller image; neither host-board variant built here |
| `vendor-burst/Makefile` | `make -f clean-rx/vendor-burst/Makefile reference reference-test`, plus named `reference-*` wrappers | Default observer, scope-on and early-prefix controller selections captured; full endpoint pairs/collectors not built |
| `doc/HT20.md` | Explicit matched controller/host `EXTRA_CFLAGS` for HT data, response, EDCA and WMM | Combined WMM/EDCA controller captured; intermediate combinations remain coverage gaps |
| `src/main.c`, `src/sdio_test.h` | `CLEAN_SDIO_SELF_TEST` and pattern/window/pacing/DMA switches | Large-pattern/window-8 controller captured; transport hardware behavior and other switch combinations not tested |

Flag defaults and flag-bearing Make/script/documentation lines are captured
with their owners. Documentation occurrences are references, not automatically
supported profiles: older development records describe historical source
states and frozen campaigns.

Lifecycle distinctions:

- **Current ordinary interfaces/work:** cold initialization, raw transport,
  bounded station/MAC operation, host lifecycle/security work, and the
  messenger's HT payload use. Reliability and feature gaps remain tracked in
  [the roadmap](../../../../todo.md).
- **Retained diagnostic entry points:** observer/handoff/copy-scope,
  early-FIFO/recovery, stimulus/monitor, fault-injection and SDIO variants.
  Existing Make targets remain available; disabled-by-default does not mean
  obsolete, production-ready, or hardware-qualified.
- **Historical evidence:** saved build IDs, register/gain trials, failed
  hypotheses, withdrawn changes and older images in development records.
  Preserve them as evidence, not as current source claims or configurations
  to silently re-enable.
- No named variant is deleted, retired or declared reliable by C01.
  Human prioritization of future experiments remains separate from inventory.

## Initial measured baseline

Source revision: `b77cb5bb01550021f8e3c8b981e2f9d15ac72d53`, with C01
documentation/tool additions in the working tree. The JSON captures the
working-tree status and exact file hashes, so the commit alone is not
presented as a complete identity.

Host: macOS/arm64; Node 26.7.0; target LLVM/Clang 22.1.8. Full native
compiler, linker, Make and binary-tool versions are in the record.
Every image below has zero `data` bytes. Sizes are LLVM's `text`/`bss`
accounting, not total hardware SRAM usage.

| Profile | Text bytes | BSS bytes | Native coverage |
| --- | ---: | ---: | --- |
| `default` | 45,364 | 4,928 | PSM, MAC, Block ACK pass; recovery disabled |
| `ht-data` | 47,176 | 6,424 | PSM, MAC, Block ACK pass; recovery disabled |
| `wmm-edca` | 48,868 | 6,448 | PSM, MAC, Block ACK pass; recovery disabled |
| `sdio-self-test` | 3,080 | 16 | PSM model, MAC, Block ACK pass; recovery disabled; these are not SDIO hardware tests |
| `observer` | 64,664 | 6,424 | PSM, MAC, Block ACK pass; recovery disabled |
| `copy-scope` | 66,076 | 6,440 | PSM, MAC, Block ACK and copy-scope pass; recovery disabled |
| `early-prefix` | 67,908 | 6,440 | PSM, MAC, Block ACK, ordered-flush/prefix recovery and early-RX pass |

The record contains full SHA-256 values for controller binaries/ELFs, linked
PSM bytes, compiled objects and maps. The SDIO variant intentionally bypasses
radio startup: its PSM object is compiled but its PSM program is not linked.
Its PSM hash is therefore explicitly absent, not an empty-image hash.

Copy-scope and early-RX tests are marked not-selected outside their dedicated
profiles. The recovery executable's explicit disabled message is recorded as
**disabled**, not passed; the `early-prefix` profile actually executes its
ordered-flush and prefix checks. Individual native test logs are retained.

This matrix is intentionally representative, not all flag combinations.
Native tests do not execute the controller's `main.c` on silicon, qualify
timing/recovery/RF behavior, validate host-board overrides, or prove negotiated
HT/aggregation feature parity.

## Gates for subsequent milestones

- C02 must eliminate the recorded controller external includes without
  silently changing integer/pointer types across controller/native consumers.
- C03 must preserve the selected tests, integration dependencies and explicit
  disabled/not-selected distinctions.
- Compare each later profile against its own binary and PSM hashes, not only
  the default image. Explain intentional differences and requalify hardware
  where required by [the cleanup verification policy](../../../cleanup.md#verification-policy).
- Do not use this offline baseline as permission to flash or operate radios.


