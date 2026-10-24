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
feature flags from [the sole catalogue](../src/profiles.mk). Unknown names
and incompatible diagnostic gates fail. Output includes `profile.txt`
with mode, selection and compiler flags. Match those choices in the host.
`EXTRA_CFLAGS` remains available for deliberate custom experiments.

Copying only `clean-rx/src/` is sufficient for production builds: run
`make CC=clang BUILD=/absolute/new/output` in the copied directory.
No sibling test/tool/host/general-source directory is required.
Native models are separately owned by [tests/](../tests/README.md).

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
2. Standalone current-source versus C01 comparisons: 74 diagnostic flag
   sets and 66 reference campaign selections.
3. Six ordinary runtime controller/PSM/mutable-storage comparisons with C05.
4. Seven-profile diagnostic capture, checked against immutable bytes,
   PSM addresses and explicit native coverage expectations.
5. Runtime and diagnostic native suites, then copy-scope, early-RX and
   ordered/prefix recovery profiles with sanitizers.

For a faster tooling-only check:

```sh
make -C clean-rx check-tools
```

The full check is the acceptance gate; the fast check is not its substitute.
Firmware `all` does not invoke the checker: keeping this development gate
outside `src/` preserves standalone production builds without Node/Git/tests.
Automation can run the same command and retain its manifest and stage logs.

## Enforced boundaries and coverage

The [shared checker](../tools/controller-boundaries.cjs) rejects:

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
hardware qualification. See [remaining limits](STATUS.md).
