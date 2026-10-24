# clean-rx cleanup milestones

## Goal

Make the CYW43439 controller source self-contained and clearly separate
ordinary firmware, development tools, native tests and diagnostic firmware.
This is structural cleanup, not a feature-parity or radio-reliability claim.
Existing failures, qualification limits and experimental evidence remain valid
and must be preserved.

The initial audit found external type/byte-helper includes, native tests beside
firmware, host-only cryptography in the controller source directory, mixed
runtime/fixture headers, and ACK suppression in the default controller image.
The controller builds checked remained freestanding, without libc.

## Required boundaries

- Everything needed to compile and link the controller is under `src/`,
  including minimal types, byte helpers, startup and linker inputs. A host
  compiler and binary utilities are build prerequisites, not target libraries.
- Controller code and its build must not reference the repository's general
  `src/` tree, badge implementation, peer fixtures or development tools.
- Host code may consume controller ABI definitions and portable runtime code.
  Dependencies must not point back from the controller to its host consumers.
- Native tests may use libc. Controller firmware, including diagnostic
  firmware variants, must remain freestanding and must not link libc.
- Error reporting, ownership checks, bounded waits and necessary recovery are
  runtime responsibilities. Synthetic traffic and deliberate fault injection
  are test responsibilities.
- Diagnostic variants must be explicitly selected and identifiable. Ordinary
  builds must not silently enable experimental behavior.
- Preserve current interfaces and behavior during organizational changes.
  Intentional behavior changes require their own acceptance criteria.

## Verification policy

Before each milestone, record the relevant source revision, tool versions,
build flags and profile identities. Use isolated build directories so a
campaign's existing artifacts are not overwritten.

For behavior-preserving changes, compare controller binary hashes and emitted
PSM bytes against fresh baselines built with the same tools and flags.
Compare each supported profile separately; a matching default image is not
evidence for experimental variants. Investigate differences rather than
updating expected hashes without explanation.

Native tests and builds are not RF qualification. Changes to timing,
instruction layout, memory layout, command behavior or hardware sequencing
require appropriate hardware requalification before claiming retained
hardware behavior. Physical campaigns require explicit authorization and the
existing backup, recovery and readback-verification procedures.

Historical manifests and evidence must not be rewritten to match new paths or
build identities. Relevant qualification procedures remain in
[development documentation](dev-doc/README.md) and [the roadmap](../todo.md).

## Milestones

### C01 - Establish the inventory and reproducible baseline

Completed 2026-10-24. See the [inventory and baseline record](dev-doc/C01_BASELINE.md)
and [repeatable capture tool](tools/c01-baseline.cjs). Coverage gaps are explicit;
completion is not a claim that every retained experiment was executed.

- [x] Classify every file as controller runtime, shared ABI/portable runtime,
  host runtime, native test, development tool or firmware-side diagnostic.
- [x] Trace actual compiler inputs, linked objects and external includes for
  the default controller and supported diagnostic profiles.
- [x] Inventory profile flags and their owning build commands, including
  consumers in `badge/`, `messenger/`, `peer/` and `vendor-burst/`.
- [x] Record binary/PSM hashes, section sizes and test outcomes for the chosen
  baseline matrix. Distinguish executed tests from disabled/skipped paths.
- [x] Identify active experiments versus historical variants; do not remove
  either merely because they are disabled by default.

**Acceptance:** every retained profile has an owner, purpose, reproducible
build command and explicit validation coverage or documented coverage gap.

### C02 - Make controller source independent

**Prerequisite:** C01.

Completed 2026-10-24. See the [implementation and isolation checks](dev-doc/C02_ISOLATION.md).
All 74 distinct selected flag sets built from production sources alone and
matched reconstructed C01 controller/PSM bytes; the seven frozen C01 profiles
also matched their recorded hashes. Native-test dependencies remain for C03.

- [x] Replace general-tree type and byte-helper dependencies with
  controller-owned, minimal equivalents under `src/`.
- [x] Reconcile the existing local `types.h` with the needs of native and
  RP2350 consumers without assuming a 32-bit host pointer or `size_t`.
- [x] Remove external include paths and prerequisites from the controller
  build, separating native-test dependencies where necessary.
- [x] Compile and link from a copy of only `src/`, with the rest of the
  repository unavailable.
- [x] Check linked images for undefined symbols and unintended runtime or
  library dependencies.

**Acceptance:** the isolated source tree builds all retained controller
profiles without external source files, libc, SDKs or downloaded executables.
Organizational changes preserve baseline controller and PSM bytes.
On-chip ROM calibration-table use remains an explicitly documented hardware
dependency, not a claim of independence from the silicon.

### C03 - Separate native tests and integration tests

**Prerequisites:** C01, C02.

Completed 2026-10-24. See the [test guide](tests/README.md) and
[validation record](dev-doc/C03_TEST_SEPARATION.md). All seven native sources
are relocated, named diagnostic profiles execute their required checks,
and all 74 production-only selections retain their controller/PSM bytes.

- [x] Move native test sources out of `src/` into a dedicated test directory.
- [x] Separate controller unit/model tests from badge/peer integration tests.
  In particular, make dependencies on station RX and vendor-peer protocols
  explicit rather than hiding them in controller test recipes.
- [x] Give test-only helpers and PSM simulation a test-owned location.
- [x] Keep production builds independent of test files and test libraries.
- [x] Provide named test profiles for copy-scope, early RX and recovery.
  Report disabled coverage separately from tests that actually execute.
- [x] Update all callers, build prerequisites and directly related guides.

**Acceptance:** production builds require no test tree; all relocated tests
retain their assertions and sanitizer coverage, and profile-specific tests
run with their required flags.

### C04 - Separate portable runtime, ABI and fixture helpers

**Prerequisites:** C02, C03.

- [ ] Split mixed headers such as `ht20.h`, `unicast.h` and `frame_limits.h`
  into cohesive runtime operations, shared contracts and test fixtures.
- [ ] Keep controller-required shared contracts within the self-contained
  `src/` tree; make host consumers import them in that direction only.
- [ ] Move synthetic frame generators, expected-pattern helpers and soak
  fixtures to test/development ownership.
- [ ] Move host-only cryptography out of the controller source tree after
  checking all badge, collector and peer consumers.
- [ ] Preserve command values, structure sizes, offsets, memory bounds and
  version identifiers with explicit compatibility checks.

**Acceptance:** controller headers do not import fixtures or host code;
host consumers still build and test against unchanged wire contracts.
No implementation is duplicated merely to satisfy directory boundaries.

### C05 - Isolate firmware-side tests and experimental profiles

**Prerequisites:** C03, C04.

- [ ] Extract synthetic SDIO traffic into an explicit diagnostic variant.
- [ ] Isolate ACK suppression and other deliberate fault injection from the
  ordinary runtime command path.
- [ ] Separate passive runtime observability from active experimental
  behavior; retain necessary fault evidence and recovery safeguards.
- [ ] Give diagnostic code a distinct location and select it through explicit
  source lists or narrowly scoped hooks, not implicit host/tool dependencies.
- [ ] Preserve access to existing experiments through named profiles and
  document which commands each profile supports.
- [ ] Make ordinary builds reject diagnostic-only commands explicitly using
  the established error-reporting mechanism.

**Acceptance:** ordinary firmware cannot generate synthetic test traffic or
activate fault injection. Diagnostic variants remain freestanding and
identifiable. Removing default diagnostic behavior is an intentional
behavior change with tests, documented compatibility impact and a separate
hardware acceptance gate.

### C06 - Decompose controller orchestration and PSM composition

**Prerequisites:** C04, C05.

- [ ] Extract cohesive responsibilities from `main.c`: transport/DMA,
  station/MAC coordination, command dispatch and diagnostic publication.
- [ ] Give mutable state explicit ownership and keep orchestration small;
  avoid replacing one large file with hidden cross-module globals.
- [ ] Separate ordinary PSM program composition from experimental fragments
  in `psm_ack.h` and `ht_observer.h`.
- [ ] Preserve PSM label ordering, instruction addresses, hook targets and
  bounded control flow during organizational moves.
- [ ] Keep assertions covering memory overlap, ABI geometry and emitted
  instruction constraints close to their owning contracts.

**Acceptance:** organizational changes preserve binary/PSM baselines across
the retained profile matrix. Any unavoidable differences receive explicit
instruction/layout analysis and the required requalification rather than
being treated as harmless file moves.

### C07 - Separate development tooling and centralize profile ownership

**Prerequisites:** C03, C05, C06.

- [ ] Clearly distinguish host firmware from collectors, campaign runners,
  evidence verifiers and independent peer fixtures.
- [ ] Keep development tools outside controller source and out of ordinary
  firmware build prerequisites.
- [ ] Centralize named controller profile definitions within `src/`; make
  external runners select them instead of maintaining divergent flag lists.
- [ ] Preserve existing campaign identities and invocation behavior, or
  document intentional migration steps and validate replacement commands.
- [ ] Ensure builds and offline tests never flash devices or start RF
  experiments implicitly.

**Acceptance:** each profile has one authoritative definition, tools remain
usable against their intended firmware variants, and a controller build
requires no collectors, USB libraries, peer SDKs or campaign infrastructure.

### C08 - Enforce boundaries and complete documentation

**Prerequisites:** C02 through C07.

- [ ] Add automated checks for controller includes/build inputs escaping
  `src/`, test-code dependencies and unexpected linked runtime symbols.
- [ ] Make isolated-source builds and the retained profile/test matrix
  repeatable checks rather than manual audit steps.
- [ ] Verify ordinary builds exclude diagnostic behavior and diagnostic
  profiles exercise their selected code instead of reporting disabled paths.
- [ ] Update architecture, usage and development guides with the actual
  layout, dependency direction, profile selection and validation commands.
- [ ] Document unresolved reliability and feature gaps separately from
  structural-cleanup completion.
- [ ] Run the final offline matrix and record any separately authorized
  hardware requalification results with their limits.

**Acceptance:** boundary violations fail checks; a new contributor can build
the standalone controller, run native tests and select diagnostic firmware
without reconstructing flags or dependencies from historical experiments.
Cleanup completion does not imply vendor feature parity or RF reliability.
