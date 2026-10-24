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
- `doc/` describes the firmware and CYW43439 as they are, without development
  history, plans or investigations. Development procedures, tests, bugs,
  hypotheses, open questions and feature requests belong in `dev-doc/`.
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

## Completed milestones

C01-C09 were completed on 2026-10-24. The summaries below describe delivered
work; detailed implementation and validation records remain linked.

### C01 - Establish the inventory and reproducible baseline

Inventoried source ownership, dependencies, profiles and experiments.
Captured immutable controller/PSM hashes, geometry and native-test outcomes,
distinguishing executed, disabled and unselected coverage.
See [the baseline record](dev-doc/history/cleanup/C01_BASELINE.md) and
[capture tool](tools/c01-baseline.cjs).

### C02 - Make controller source independent

Replaced external types/byte helpers and removed production dependencies
outside `src/`. All 74 selected flag sets build from a standalone source copy
without libc or undefined symbols and retain C01 controller/PSM bytes.
On-chip ROM calibration tables remain a documented silicon dependency.
See [the isolation record](dev-doc/history/cleanup/C02_ISOLATION.md).

### C03 - Separate native tests and integration tests

Moved all seven native sources out of `src/`, separating controller models
from badge/peer integration and test-owned helpers. Preserved assertions,
sanitizers and named copy-scope, early-RX and recovery coverage; production
builds need no test tree.
See [the test guide](tests/README.md) and
[validation record](dev-doc/history/cleanup/C03_TEST_SEPARATION.md).

### C04 - Separate portable runtime, ABI and fixture helpers

Separated portable operations, shared ABI contracts, diagnostic codecs,
synthetic fixtures and host-only crypto without duplicating implementations.
Updated consumers and checked unchanged command values, layouts, bounds and
versions. See [the contract/fixture record](dev-doc/history/cleanup/C04_CONTRACT_SEPARATION.md).

### C05 - Isolate firmware-side tests and experimental profiles

Introduced explicit runtime, diagnostic and SDIO-test behavior profiles.
Ordinary firmware rejects diagnostic commands and cannot activate synthetic
traffic or ACK suppression; passive evidence and recovery remain available.
This deliberately changed runtime behavior. Implementation and offline tests
are complete; [hardware acceptance](dev-doc/guides/HARDWARE_TRIALS.md#runtime-profile-acceptance)
remains unperformed. See [the profile record](dev-doc/history/cleanup/C05_DIAGNOSTIC_PROFILES.md).

### C06 - Decompose controller orchestration and PSM composition

Extracted controller responsibilities and ordinary/diagnostic PSM composition,
with explicit state-owner namespaces and local geometry assertions.
Deliberately retained unity compilation and flat storage: 74 diagnostic
selections match C01 bytes; six runtime selections match C05 bytes and mutable
storage layouts. Namespaces do not provide separate-module encapsulation.
See [the composition record](dev-doc/history/cleanup/C06_CONTROLLER_COMPOSITION.md).

### C07 - Separate development tooling and centralize profile ownership

Centralized named selections in `src/profiles.mk`, moved desktop tools under
`tools/`, and added firmware-only application targets. Preserved campaign
targets/flags, documented tool-path migration and removed collector source
from firmware identities. Validated affected host consumers and retained
controller/PSM baselines.
See [the tooling/profile record](dev-doc/history/cleanup/C07_TOOL_AND_PROFILE_OWNERSHIP.md).

### C08 - Enforce boundaries and complete documentation

Added shared boundary enforcement, mutation tests and repeatable offline
acceptance. The final run passed 32 tooling tests, 74 diagnostic comparisons,
six runtime comparisons, seven historical profiles and native suites.
Historical coverage remains 24 passed, six disabled and twelve unselected;
the C01 baseline is unchanged. Updated contributor/reference documentation.
See [the acceptance record](dev-doc/history/cleanup/C08_OFFLINE_ACCEPTANCE.md).

Repeat with fresh evidence:

```sh
make -C clean-rx check BUILD=../build/clean-rx/acceptance-NEW
```

**C01-C08 structural cleanup is complete.** Remaining
[feature/reliability limits](dev-doc/issues/README.md) and the separate C05 hardware
acceptance gate remain open. No new hardware/RF qualification was performed.

### C09 - Formalize and check the documentation boundary

Documented reference/development ownership and human content-review criteria,
including this roadmap's development role. Added
[Markdown validation](tools/documentation.cjs) and six
[regression tests](tools/c09-documentation.test.cjs) for links, heading
anchors, HTML anchors and prohibited reference-to-development links.
Reviewed all four reference guides against the scope policy and replaced
the ignored local datasheet link with public manufacturer documentation.

At C09 completion, documentation checks passed for 36 Markdown files and
402 local links, and all 38 tooling tests passed. Rerun
`make -C clean-rx check-docs` for the current tree. The full acceptance runner also invokes the
documentation check and hashes Markdown inputs. Checks use only Node's
built-in modules, with no npm packages or framework. No controller inputs
or hardware behavior changed.
See [contributor guidance](dev-doc/guides/DEVELOPMENT.md#documentation-boundary-and-review)
for setup, scope and review criteria. This is not a new full firmware-matrix
or hardware qualification result.

## Follow-up milestones

C11-C13 are not complete. Keep checks local and dependency-free; no CI
milestone is planned. C10 was withdrawn rather than renumbering later work.
RX reliability, general HT/A-MPDU support and hardware qualification remain
separate functional work, not acceptance criteria for these cleanup tasks.

### C11 - Add a repeatable host-consumer compatibility gate

**Prerequisites:** C07, C08.

- [ ] Turn the affected badge, messenger, peer and reference application
  builds/native tests into one documented, repeatable offline gate.
- [ ] Include desktop collector builds and existing offline verifier
  self-tests against the intended shared contracts and profile selections.
- [ ] Record commands, toolchain/dependency versions, selected profiles
  and failures; do not count missing SDKs or libraries as successful skips.
- [ ] Keep this gate separate from standalone controller acceptance and
  firmware prerequisites. Use existing local build/test tools without
  introducing a framework, package manager or CI dependency.

**Acceptance:** a clean environment with documented dependencies can run
the consumer gate without hardware. Consumer build/test failures return
nonzero, while controller-only builds and acceptance still require no
application SDKs, collectors or USB libraries.

### C12 - Define acceptance for intentional firmware changes

**Prerequisites:** C01, C05, C08.

- [ ] Distinguish historical reconstruction, behavior-preserving comparisons
  and acceptance of an explicitly intended new firmware behavior.
- [ ] Define versioned, reviewed expectations for changed bytes/layouts,
  recording source revision, toolchain, flags and the reason for each change.
- [ ] Require behavioral regression tests and instruction/memory/PSM analysis
  appropriate to the change, plus separately authorized hardware
  requalification where required.
- [ ] Preserve C01/C05 historical evidence and coverage distinctions.
  Never regenerate expected values from the candidate merely to make it pass.
- [ ] Test rejection of unexplained differences and selection of the correct
  expectations for each profile and acceptance mode.

**Acceptance:** an approved intentional change can be validated reproducibly
without rewriting historical evidence or disabling compatibility checks.
Unapproved differences still fail; offline acceptance never implies RF
qualification.

### C13 - Strengthen runtime component contracts

**Prerequisites:** C06, C08, C12.

**Optional deeper refactoring:** pursue only where concrete ownership or
testability benefits justify the risk; file size alone is not a reason.

- [ ] Inventory implicit state, ordering and lexical control-flow dependencies
  between unity-composed runtime fragments, especially command dispatch
  and station TX.
- [ ] Define narrow inputs, outputs, state ownership and error propagation
  for selected responsibilities; add focused tests around those contracts.
- [ ] Extract cohesive helpers or test seams where they remove real coupling.
  Do not mechanically split files, duplicate logic or add pointer contexts
  merely to simulate encapsulation.
- [ ] Retain freestanding isolation, bounded behavior, observability,
  diagnostic gating and existing wire contracts.
- [ ] Prefer byte/storage-layout preservation. Handle any intentional
  differences through C12 rather than labeling them harmless reorganization.

**Acceptance:** selected components have explicit, tested contracts and
demonstrably reduced implicit coupling. Retained profiles pass the relevant
controller and consumer gates; byte/layout changes receive documented
analysis and any required separate hardware qualification.
