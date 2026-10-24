# C03: native test and integration ownership

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

Date: 2026-10-24.

This implements [cleanup milestone C03](../../../cleanup.md#c03---separate-native-tests-and-integration-tests).
No production C/assembly/header/linker input, firmware interface or PSM
instruction is changed.

## Changes

Seven native test source files formerly in `src/` now live under
[tests/](../../../tests/README.md):

- Unit/model: `runtime_test.c`, `psm_ucode_test.c`, `block_ack_test.c`,
  `rx_recovery_test.c`.
- Integration: `mac_test.c`, `copy_scope_test.c`, `early_rx_test.c`.

All existing test bodies/assertions are retained. The runtime suite adds
empty-input and standard-vector checks for a test-owned bitwise CRC helper.
The PSM suite uses that helper instead of linking the general diagnostics
implementation solely for its image CRC assertion. The Block ACK suite
uses the controller-local byte helper rather than a general-tree helper.
Include paths change to reflect the new locations.

Unit/model code and its native build require only controller source and
test-owned code. PSM simulation stays in the relocated native test files,
not the firmware. The MAC suite remains intact as integration because it
also tests station RX and soak/fragment fixtures. Copy-scope and early-RX
retain their vendor-peer protocol checks. Their general diagnostics and
badge/peer headers are explicit integration build inputs.

The production Makefile has no native executable rules or native compiler
configuration. Explicit test targets only forward to the test-owned
Makefile; `all` neither includes nor evaluates test build dependencies.
Legacy forwarding preserves exact experiment flags and output locations,
so vendor-burst and historical commands remain usable.

## Callers and named profiles

The current [baseline capture](../../../tools/c01-baseline.cjs) builds controller
images through `src/Makefile` and native executables through `tests/Makefile`.
It records separate commands, relocated test sources and test dependency
traces. It runs both runtime/header-order variants for every selected
profile. Frozen C01 evidence is not rewritten.

Named `test-copy-scope`, `test-early-rx` and `test-recovery` targets provide
the required flags and isolated build subdirectories. Low-level focused
targets remain available for experiment-specific flags.

Default and early-RX runs explicitly report the ordered-flush recovery
model disabled. The recovery profile enables both ordered flush and prefix
recovery and must execute those assertions. C01 matrix manifests continue
to distinguish passed, disabled and not-selected tests.

## Verification

```sh
node --test clean-rx/tools/c01-baseline.test.cjs \
  clean-rx/tools/c02-isolation.test.cjs clean-rx/tools/c03-tests.test.cjs
make -C clean-rx/tests BUILD=../../build/clean-rx/c03-local-NEW \
  test test-copy-scope test-early-rx test-recovery
node clean-rx/tools/c01-baseline.cjs build/clean-rx/c03-matrix-NEW
node clean-rx/tools/c02-isolation.cjs build/clean-rx/c03-isolation-NEW
```

The test-layout checks build unit/model suites from a temporary tree
containing only production source and unit/support tests. No badge,
integration, vendor-peer or general-tree code is copied. They also check
named flags against C01 and verify that a production dry run neither
enters the test directory nor uses native sanitizers/diagnostics.

The C02 isolation verifier independently builds all retained controller
selections from a production-only tree, with no test files or test tree.
It compares controller/PSM bytes to reconstructed C01 source and checks
the seven frozen profiles directly against their recorded hashes.

The seven-profile native comparison retains the existing 24 passed,
six disabled and twelve not-selected baseline outcomes, with two runtime
header-order checks additionally run for each profile. Relocation does
not turn disabled/skipped behavior into a qualification pass.

Named-profile runs and legacy `src/` forwarding are checked separately.
Compiler/flag changes are tracked for all native executables, including
focused suites; no flags are shared implicitly with a previous build.

Completed results:

- All 13 Node capture/isolation/test-layout checks pass, including unit-only
  builds and recompilation after native flag changes.
- All three named profiles pass. Recovery actually prints its
  emitted-program/ordered-write/prefix-deadline success result; the other
  profiles retain explicit disabled recovery output.
- All 74 production-only flag selections match reconstructed C01
  controller/PSM bytes; all seven frozen hashes and native outcome
  classifications are preserved.
- The seven relocated test bodies match their pre-move bodies after the
  documented include/helper substitutions; only the two new runtime CRC
  assertions are added.
- Legacy combined `all test runtime-test` and caller-flagged
  `copy-scope-test`/`early-rx-test` invocations pass.

Local evidence: `build/clean-rx/c03-matrix-final-20261024/manifest.json`,
`build/clean-rx/c03-isolation-20261024/manifest.json`,
`build/clean-rx/c03-profiles-20261024.log`, and the
`c03-compat*-20261024.log` files in that same build directory.
The isolation manifest SHA-256 is
`d4ad92b309227396447d1ed32b4a1e87ae62e2df4a9b5b270d9f5362d9fb4467`.
These generated files are not substituted for, or written over, the frozen
C01 record.

This is offline structural validation. It does not add physical/RF
qualification, change reliability claims, or authorize hardware access.


