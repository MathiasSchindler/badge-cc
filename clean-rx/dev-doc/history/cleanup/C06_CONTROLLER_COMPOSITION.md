# C06: controller and PSM composition

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

C06 reorganizes the controller without changing emitted firmware or PSM
instructions. It does not add vendor-feature parity, repair RF reliability
gaps, or qualify the C05 runtime behavior on hardware.

## Controller responsibilities

[`main.c`](../../../src/main.c) is the explicit composition root. Its main loop
shows startup, poll preparation, a command snapshot, command admission,
station polling, command dispatch, capture and shutdown. Implementation
fragments live in [`runtime/`](../../../src/runtime):

| Responsibility | Components |
| --- | --- |
| Shared storage and assertions | `storage.inc`, `state.h`, `contracts.h` |
| D11 access and startup | `d11_io.inc`, `d11_start.inc`, `startup.inc` |
| Receive transport, DMA and SDIO F2 | `transport.inc` |
| Station ACK/response, policy, RX and TX | `station_ack.inc`, `station_response.inc`, `station_policy.inc`, `station_rx.inc`, `station_tx.inc` |
| Receive extraction and recovery | `receive.inc`, `recovery.inc` |
| Passive evidence publication | `publication_phy.inc`, `publication_tx.inc`, `publication_ack.inc`, `publication_mac.inc`, `publication_rx.inc`, `publication_psm.inc` |
| Poll/dispatch/capture lifecycle | `poll_prologue.inc`, `command_guard.inc`, `poll_station.inc`, `commands.inc`, `capture.inc`, `shutdown.inc` |

These are deliberately **unity-composed implementation fragments**, not
independent object modules. Function bodies, definition order and the
main-loop stages retain their previous order. The compiler still sees one
controller translation unit; no new external mutable globals or link-time
interfaces are introduced. Existing diagnostic hooks remain explicitly
profile-gated under [`diagnostic/`](../../../src/diagnostic).

This preserves the compiler's inlining and allocation decisions. Splitting
objects or introducing pointer-based state views would be a different,
behavior-sensitive change, not an incidental file move.

## Mutable-state ownership

[`state.h`](../../../src/runtime/state.h) defines owner-qualified lvalue namespaces.
Their actual scalar/array declarations retain their original C types and
separate allocations in [`storage.inc`](../../../src/runtime/storage.inc).

| Namespace | Ownership and lifetime |
| --- | --- |
| `TRANSPORT(field)` | Static DMA producer/consumer, F2 indices/credits/sequences and capture preparation/publication bookkeeping |
| `STATION(field)` | Static station flags, contention/EDCA and pending policy, address/frame scratch, timing/ACK windows, TX identity, ACK-test state and recovery attempt evidence |
| `PUBLICATION(field)` | Static sampled response evidence and TX diagnostic record |
| `AGGREGATE(field)` | Gated static aggregate SHM snapshot, outcomes and copy/advance bookkeeping, declared beside its implementation in `receive.inc` |
| `LOOP(field)` | Automatic main-loop flags, channel, enable/calibration status, poll/scanner/target bookkeeping and observer stop timing |

Accesses use the owner namespace rather than unqualified mutable globals.
The command snapshot `cmd` is a local stage input. These namespaces make
shared ownership visible; they do not claim separate-TU encapsulation.
Ordinary helper parameters and existing value contracts remain unchanged.

Flat storage is intentional. An intermediate pointer-view implementation
took backing addresses earlier and changed Clang's deferred static-storage
emission, BSS ordering and ARM instructions. A pointer-based automatic loop
context also reordered two zero stores in the copy-scope image. Neither
difference was accepted: direct owner-qualified static and automatic
lvalues restored exact controller bytes.

## PSM composition

[`psm_ack.h`](../../../src/psm_ack.h) retains shared instruction helpers and the
original label enumeration. `ACK_PROGRAM` composes these fragments in order:

1. [`response.h`](../../../src/psm/response.h): ordinary ACK/CTS response path.
2. [`contention.h`](../../../src/psm/contention.h): station contention.
3. [`rx_evidence.h`](../../../src/psm/rx_evidence.h): receive/evidence handling.
4. [`tx_cleanup.h`](../../../src/psm/tx_cleanup.h): TX completion and cleanup.
5. [`extensions.h`](../../../src/psm/extensions.h): selected extension programs.

[`aggregate_hooks.h`](../../../src/psm/aggregate_hooks.h) supplies ordinary
fallback hooks or selects diagnostic aggregate instructions.
[`ht_observer.h`](../../../src/ht_observer.h) retains configuration and bounds
contracts, selecting passive observer/rearm composition under
[`psm/`](../../../src/psm). Copy, advance, stream and aggregate experimental
instruction fragments live under [`diagnostic/`](../../../src/diagnostic).
Runtime and genuine passive recovery still build without that directory.

Label ordering, numeric instruction tuples, instruction addresses, branch
targets and existing bounds are unchanged. Controller overlap/mailbox/FIFO
geometry assertions are in
[`contracts.h`](../../../src/runtime/contracts.h); public ABI and emitted-program
constraints remain with their existing contracts.

## Build dependencies

Controller builds track the runtime headers/fragments and PSM headers.
Native and affected host-application builds track the extracted PSM
headers too. The composition test verifies that touching a transport
fragment rebuilds `main.c` and touching a PSM fragment rebuilds
`psm_ucode.c`.

Rapid profile switching exposed a coupled build problem on Make versions
with whole-second timestamp comparisons: rebuilding objects alone could
leave the ELF, binary or profile sidecar stale. A changed flag selection
now forces the complete output chain. Existing rapid-switch profile tests
verify this behavior.

## Offline verification

Two complementary baselines are required:

- The immutable C01 baseline covers legacy **diagnostic** behavior, not
  the post-C05 runtime default.
- C05 revision `fb493e321ddf23c4a7caebd26c5e9b8202498233` covers ordinary
  runtime behavior, including its deliberately changed command/ACK policy.

Results:

- All 74 retained diagnostic flag selections, representing 66 reference
  resolver entry points, reproduce C01 controller and PSM bytes from a
  standalone source copy.
- Six ordinary runtime selections reproduce C05 controller/PSM bytes and
  mutable-symbol addresses/sizes: default runtime, HT data, WMM/EDCA,
  passive observer, early-prefix recovery and passive host-PHY reset.
  Owner prefixes are normalized only for symbol-name comparison.
- A fresh seven-profile capture reproduces the frozen controller/PSM
  hashes. Historical test coverage remains 24 passed, six disabled and
  twelve not selected.
- All 23 cleanup-tool tests pass. C06 tests compare extracted function
  bodies/order and storage types, enforce owner-qualified state access,
  and compare every PSM instruction tuple, enumerated label and hook
  across 15 selections, including two aggregate variants.
- Existing ABI checks retain 284 constants, 236 layout properties and
  the eight-word manifest across 14 selections.
- Runtime/diagnostic native suites and named copy-scope, early-RX and
  recovery suites pass with address/undefined-behavior sanitizers.
- Badge, messenger, reference and handoff builds/tests, peer native tests,
  root transport tests/collector and all five root SDIO variants pass.

ELF/map symbol names and source metadata can change; this is not a claim
that ELF files are identical. The loaded controller/PSM bytes and checked
mutable-storage locations are identical, with no instruction differences
accepted or baseline rewritten.

Reproduce with fresh evidence directories:

```sh
node --test clean-rx/tools/*.test.cjs
node clean-rx/tools/c06-runtime.cjs build/clean-rx/c06-runtime-NEW
node clean-rx/tools/c02-isolation.cjs build/clean-rx/c06-isolation-NEW
node clean-rx/tools/c01-baseline.cjs build/clean-rx/c06-matrix-NEW
make -C clean-rx/tests BUILD=../../build/clean-rx/c06-runtime-tests-NEW test
make -C clean-rx/tests BUILD=../../build/clean-rx/c06-diagnostic-tests-NEW \
  PROFILE=diagnostic test
make -C clean-rx/tests BUILD=../../build/clean-rx/c06-profile-tests-NEW \
  test-copy-scope test-early-rx test-recovery
```

Local evidence is in `build/clean-rx/c06/`, `c06-runtime`,
`c06-isolation-final` and `c06-matrix-final` under `build/clean-rx/`.
The verifiers refuse to reuse evidence directories.

## Hardware gate remains separate

No device flashing, device execution or RF campaign was performed for
C06. The earlier one-device C04 authorization has ended. Byte identity
does not replace the unperformed
[C05 hardware acceptance gate](../../guides/HARDWARE_TRIALS.md#runtime-profile-acceptance),
which still requires new authorization and backup/restore safeguards.


