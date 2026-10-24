# C02: self-contained controller source

Date: 2026-10-24.

The controller's production build now requires only `clean-rx/src/`, a
Clang-compatible ARM compiler, LLVM linker/binary tools and Make. It no
longer includes or names prerequisites from the general repository source
tree. This implements [cleanup milestone C02](../cleanup.md#c02---make-controller-source-independent),
not the test/tool reorganization in later milestones.

## Changes and dependency boundary

- [types.h](../src/types.h) is the single local type definition header.
  Compiler-provided underlying types preserve exact fixed-width types and
  use the actual target widths for `uintptr_t`, `intptr_t` and `size_t`.
  Native 64-bit hosts are not forced into the controller's 32-bit pointer
  model. Identical typedefs remain compatible with standard native headers
  and the general-tree type definitions used by RP2350 host consumers.
- [bytes.h](../src/bytes.h) owns the minimal copy, zero, equality and
  little-endian word operations. Copy remains nonoverlapping; zero-length
  ranges are untouched; word access retains volatile byte operations for
  unaligned addresses. Helpers use the `clean_bytes_` namespace so consumers
  can also include the general-tree helpers without duplicate definitions.
- Controller and portable crypto implementation files now include local
  headers. Host-only cryptography remains in its existing location until
  the planned ownership cleanup.
- The [Makefile](../src/Makefile) removes the external production include path
  and external object prerequisite. Local header dependencies trigger
  rebuilds. Native test targets still explicitly depend on general
  diagnostics and badge/peer fixtures; separating them belongs to C03.
- Runtime diagnostics, fault injection, PSM composition, commands, structure
  layouts and hardware sequencing are unchanged.

The hardware dependency on on-chip ROM calibration tables remains. This
milestone does not claim independence from the silicon, remove that table
use, or qualify radio behavior.

## Repeatable isolation and comparison

From the repository root, choose a **new** evidence directory:

```sh
node --test clean-rx/tools/c01-baseline.test.cjs clean-rx/tools/c02-isolation.test.cjs
node clean-rx/tools/c02-isolation.cjs build/clean-rx/c02-local-NEW
make -C clean-rx/src BUILD=../../build/clean-rx/c02-runtime-NEW runtime-test
```

The [isolation verifier](../tools/c02-isolation.cjs):

1. Reconstructs pre-cleanup production sources and their two external helper
   headers from the revision in the frozen C01 record. Every reconstructed
   input must match its recorded source hash; it never rewrites the C01
   record or historical evidence.
2. Copies only current production source/header/startup/linker/build inputs
   into a separate temporary tree outside the repository. Native test files,
   badge/peer code, tools and general-tree helper files are absent there.
3. Resolves every retained reference wrapper's default observer/stimulus
   controller flags using Make itself. The temporary resolver retains the
   original variable assignments and recursive wrapper recipes but replaces
   endpoint build/test leaves with flag output. It does not run collectors,
   build SDK peers, flash badges or transmit.
4. Deduplicates flag sets and builds both reconstructed and isolated trees.
   The seven C01 matrix profiles are included independently. Compiler-generated
   dependency traces must stay inside the isolated production source tree.
5. Compares complete controller binary and linked PSM hashes per flag set.
   The seven frozen matrix profiles must also match their recorded C01 hashes.
   SDIO-only firmware must explicitly omit the PSM rather than supplying an
   empty-image hash.
6. Checks undefined symbols and unexpected libc entry points, retains link
   logs/maps and dependency/symbol evidence, and checks local headers/crypto
   with freestanding Cortex-M3 and Cortex-M33 syntax probes.
7. Removes its specifically created temporary source tree, retaining evidence
   under the requested output directory.

The optional second argument writes a new JSON record. Existing output
directories and records are rejected. A failed run records its failure and
retains available build logs, rather than issuing a success result.

This checks named wrappers with their current default parameters, including
their stimulus-controller selections. It is not an exhaustive Cartesian
product of arbitrary user-provided flags, host-board overrides or runtime
commands. The owning reference Makefile must still match its C01 hash; a
future catalogue change requires review instead of silently comparing a
different experiment set.

## Validation and retained evidence

The completed isolation run resolves 66 reference entry points (the 68 C01
catalogue entries minus the controller/test helper targets) and checks **74
distinct flag sets**, including the seven frozen matrix profiles. All 148
before/after controller builds pass, with identical binary/PSM bytes for
each pair, no external isolated-source compiler dependencies and no
undefined or checked libc symbols. Both target syntax probes pass.

The retained local manifest is
`build/clean-rx/c02-isolation-validation-20261024/manifest.json`, SHA-256
`963d399ad0cb92d7753455492a17fe96793f3f8a2c8c514fc8aa9ef4d539696a`.
It records the 30 production input files and their hashes, resolved flag
selections, commands, dependency traces and per-profile hashes. Raw evidence
is local; transfer the evidence directory separately when needed.

- All ten capture/isolation-tool tests pass.
- The seven-profile native capture passes its existing PSM/MAC/Block ACK,
  copy-scope and early-prefix checks. Recovery-disabled and not-selected
  cases retain the distinctions established in C01.
- [runtime_test.c](../src/runtime_test.c) runs with both standard-header-first
  and local-header-first inclusion, under address/undefined-behavior
  sanitizers. It checks native pointer/type compatibility, empty ranges,
  copy/zero/equality bounds and unaligned little-endian word values.
- Badge host integration, crypto, verifier and script checks pass through
  `make -C clean-rx/badge ... test`, selecting the candidate controller
  through an explicit `CONTROLLER_BIN`.
- The freestanding RP2350 badge host and collector build successfully with
  that image. Independent peer native tests and messenger link tests pass
  in separate build directories.

The C01 frozen record remains the comparison authority. Raw C02 build
evidence is generated under `build/clean-rx/`; it is not automatically
committed. Native tests and identical emitted bytes do not constitute new
physical qualification or permission to operate radios.
