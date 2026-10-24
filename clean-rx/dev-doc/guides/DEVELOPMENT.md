# Firmware development and offline acceptance

## Prerequisites

Controller builds need Make, Clang with the ARM target, LLD, `llvm-objcopy`
and `llvm-size`. Full offline acceptance additionally needs `llvm-nm`,
a native C compiler supporting address/undefined-behavior sanitizers,
Node.js with `node:test`, Git and the historical C01/C05 commits.
The local checks use Node 26 and LLVM 22 on macOS; no package installation,
device discovery or downloaded firmware is part of acceptance.
A shallow checkout missing historical commits fails explicitly.

Neither controller-only builds nor complete controller acceptance need
libusb, a peer SDK, collectors or connected hardware. Host application and
collector builds have their own documented dependencies.

## Build and select firmware

From the repository root:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx/runtime \
  PROFILE=runtime SELECTION=ht-data
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx/scope \
  PROFILE=diagnostic SELECTION=copy-scope
make -C clean-rx/tests BUILD=../../build/clean-rx/tests test
make -C clean-rx/tests BUILD=../../build/clean-rx/profile-tests \
  test-copy-scope test-early-rx test-recovery
```

`PROFILE` chooses runtime/diagnostic/SDIO behavior; `SELECTION` chooses
feature flags from [the sole catalogue](../../src/profiles.mk). Unknown names
and incompatible diagnostic gates fail. Output includes `profile.txt`
with mode, selection and compiler flags. Match those choices in the host.
`EXTRA_CFLAGS` remains available for deliberate custom experiments.

Copying only `clean-rx/src/` is sufficient for production builds: run
`make CC=clang BUILD=/absolute/new/output` in the copied directory.
No sibling test/tool/host/general-source directory is required.
Native models are separately owned by [tests/](../../tests/README.md).

## One acceptance command

```sh
make -C clean-rx check BUILD=../build/clean-rx/acceptance-NEW
# Equivalent from repository root:
node clean-rx/tools/check.cjs build/clean-rx/acceptance-NEW
```

Use a new evidence directory each time. Existing directories and the
immutable C01 baseline are never overwritten. `manifest.json` starts as
`in-progress`, records stage commands/logs and input hashes, and becomes
`complete` only after every stage and final policy check succeeds.
Failures record `failed` and return nonzero; inspect the named stage log.
Changing validation inputs during the run also fails.

Stages:

1. All cleanup/tooling regression tests, including injected violations.
2. Documentation links, Markdown anchors and reference/development boundaries.
3. Standalone current-source versus C01 comparisons: 74 diagnostic flag
   sets and 66 reference campaign selections.
4. Six ordinary runtime controller/PSM/mutable-storage comparisons with C05.
5. Seven-profile diagnostic capture, checked against immutable bytes,
   PSM addresses and explicit native coverage expectations.
6. Runtime and diagnostic native suites, then copy-scope, early-RX and
   ordered/prefix recovery profiles with sanitizers.

For a faster tooling-only check:

```sh
make -C clean-rx check-tools
```

The full check is the acceptance gate; the fast check is not its substitute.
Firmware `all` does not invoke the checker: keeping this development gate
outside `src/` preserves standalone production builds without Node/Git/tests.
Run these checks locally; no CI service or JavaScript framework is required.
All JavaScript development tools use only Node's built-in modules, without
npm packages or an installation step.
Acceptance input hashes include Markdown as well as production/test/tool
inputs, so documentation drift during a run also fails.

## Documentation boundary and review

- `clean-rx/doc/` is a reference for the source-built firmware and CYW43439
  as implemented: interfaces, layouts, chip resources and current behavior.
  Current input rejection and unsupported behavior are reference facts;
  do not remove them to imply capabilities that do not exist.
- `clean-rx/dev-doc/` owns development procedures, tests, qualification,
  history, campaign results, hypotheses, bugs, open questions and requested
  changes. The [cleanup roadmap](../../cleanup.md) is also a development
  document, outside the reference tree.
- Source interfaces for diagnostic firmware may be described in `doc/`.
  Development-tool usage, experiment recipes and results may not.

Within `dev-doc/`, use the [role-based index](../README.md):
`guides/` for reusable current instructions, `issues/` for current problem
summaries, `experiments/` for observations under recorded conditions, and
`history/` for completed implementation and historical qualification criteria.
The functional and cleanup roadmaps own their respective active checklists.
Do not copy campaign narratives or active TODO lists between these roles.

Run the documentation-only check from the repository root:

```sh
make -C clean-rx check-docs
```

The dependency-free [checker](../../tools/documentation.cjs) scans Markdown below
`clean-rx/`, excluding build output. It checks the repository's single-line
inline/reference links and images, HTML link attributes and explicit anchors.
Fenced code, inline code and comments are excluded. Indented text is checked,
including list continuations; use fences for code examples containing links.
ATX and setext headings use lowercase, punctuation-stripped anchor names
with duplicate suffixes; common formatting and HTML entities are supported.
It is a lightweight check for these conventions, not a complete Markdown
renderer: review multiline links, complex nesting or unusual heading markup
manually rather than expanding it into a parsing framework.
Local links must resolve inside the repository; external URLs are not fetched.
Root-relative local paths are interpreted from the repository root.

Reference links may target `doc/` or controller/host implementation under
`src/`, `host/`, `badge/` and `messenger/`, but not Markdown outside `doc/`.
Links to development records, this roadmap, tests, tools, other non-reference
locations, and equivalent GitHub blob/tree URLs are rejected.
Use public manufacturer URLs for chip documents not tracked in the repository,
rather than ignored local downloads.

Automated link checks do **not** prove that prose has the right scope.
Before accepting a reference edit, review:

1. Does each statement describe current firmware/chip behavior rather than
   past work, plans, hypotheses or an unresolved investigation?
2. Are feature limitations stated as interface constraints, without promises,
   qualification claims or misleading omissions?
3. Are trial counts, failures, build IDs, experiment commands, test procedures
   and proposed fixes recorded in `dev-doc/` instead?
4. Do headings, links and examples remain correct after any material is moved?

The documentation checker and its mutation tests are development tooling
only. They do not enter controller build prerequisites or access hardware.

## Enforced boundaries and coverage

The [shared checker](../../tools/controller-boundaries.cjs) rejects:

- Escaped or symlinked inputs, libc/computed includes, and test/host/tool
  dependencies, even in inactive preprocessor branches.
- Unexpected evaluated source/object/Make/header inputs, external include
  flags, library links and assembler/linker-script file imports.
- Compiled dependencies outside `src/`, diagnostic imports in runtime,
  or omitted selected diagnostic implementations.
- Undefined symbols, runtime-library/compiler helpers, missing entry
  symbols and global exports outside the reviewed controller interface.

These policies intentionally require review when the production source
list, link contract or exported interface changes. They are not an
allow-everything catalogue regenerated from the image under test.

Mutation tests prove negative cases fail; ordinary tests execute the
actual dispatch guard in both behavior modes and exercise unsuppressed
versus diagnostic ACK paths. Required copy-scope/early-RX/recovery checks
must report `passed`, not merely exit successfully with a disabled path.
The historical seven-profile total remains 24 passed, six explicitly
disabled and twelve not selected. Those gaps are not renamed as coverage.

## Hardware is a separate gate

No acceptance stage flashes, boots, discovers or contacts devices, starts
collectors, or runs RF campaigns. Native models and byte identity are not
RF/timing evidence. New hardware qualification requires explicit scope,
backup/restore safeguards and independently retained evidence.
The one-device C04 authorization has expired; C05 onward have no new
hardware qualification. See [remaining limits](../issues/README.md).

