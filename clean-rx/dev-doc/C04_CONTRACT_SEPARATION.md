# C04: portable operations, contracts and host fixtures

Date: 2026-10-24.

This implements [cleanup milestone C04](../cleanup.md#c04---separate-portable-runtime-abi-and-fixture-helpers).
It is an organizational change, not a feature-parity or RF reliability fix.
No command, version, memory bound, wire layout or existing profile behavior
is intentionally changed.

## Ownership and dependency direction

| Location | Responsibility |
| --- | --- |
| [frame_limits.h](../src/frame_limits.h) | Frame/transport capacities, runtime buffer geometry and existing build gates |
| [rx_contract.h](../src/rx_contract.h) | RX layout manifest, diagnostic SHM/SRAM/report regions, aggregate and streaming evidence bounds |
| [ht_contract.h](../src/ht_contract.h) | HT command/version values, staging geometry and diagnostic wire records |
| [unicast_contract.h](../src/unicast_contract.h) | Unicast/response/EDCA/ACK command values, records and region-overlap assertions |
| [ht20.h](../src/ht20.h) | Portable HT rates, airtime, PHY controls, signal encoding/validation and RX metadata/decoding |
| [unicast.h](../src/unicast.h) | Portable response policy/rate preparation, TX ownership and deadlines |
| [ht_command.h](../src/ht_command.h) | Exact finite diagnostic command codecs/admission, explicitly selected by controller orchestration and matching host consumers |
| [tests/fixtures/](../tests/fixtures/) | Host-only HT builders, pattern/trace/evidence matching, returned-ACK checks and all soak/fragment campaign helpers |
| [host/](../host/) | Shared software WPA2 cryptography for badge, collectors and independent peer |

All controller-required contracts and codecs stay inside `src/`.
Controller headers import neither fixtures nor host code. Portable HT,
unicast and contract headers also do not import the diagnostic command
codec layer. Host consumers import controller contracts in one direction.

The old soak and crypto paths are removed, not retained as forwarding
headers that disguise host ownership. Includes, source lists, prerequisite
lists and build-identity inputs are updated for badge, peer, collectors,
vendor-burst, messenger and controller integration tests.

### Diagnostic command encoding is not duplicated

Existing controller command admission reconstructs exact probe, stimulus
and handoff frames, and checks canonical A-MSDU/ACK payload bytes. Those
encoders are therefore part of the existing diagnostic wire contract,
not host-only test helpers. They remain shared in `ht_command.h`; moving
them out of the standalone tree would introduce a test dependency, and
copying them would create two implementations of the same contract.

Host-only HT packet generators and expected/evidence checks move to
`tests/fixtures/ht20.h`. Soak generation, nonce patterns, reassembly checks
and campaign status live entirely in `tests/fixtures/mac_soak.h`.
Returned-ACK evidence checking moves to `tests/fixtures/unicast.h`.
The invariant check for the ACK-suppression status record stays with the
portable operations used by the isolated PSM unit suite.

This is not C05: default ACK suppression, existing SDIO test traffic and
profile-gated synthetic diagnostic command behavior are deliberately
unchanged. C05 must isolate those behaviors and define their rejection
policy separately. No firmware-side experiment is silently removed here.

## Compatibility checks

[c04-contracts.test.cjs](../tools/c04-contracts.test.cjs) reconstructs
hash-checked C01 headers and compares:

- Every original object-like constant in the three split headers,
  including commands, versions, addresses, lengths, sizes and magic values.
- Every original structure's size, alignment and every field offset.
- All eight layout-manifest words.
- Default/seven frozen selections plus handoff guard/variable, streaming
  phase and finite DMA/SHM/Block-ACK selections.

The compiler evaluates the expressions for the controller's Cortex-M3
data model. These are actual values/layouts, not source-spelling or
whole-firmware-hash proxies. Separately, operation-body comparisons check
that no inline operation is lost, duplicated or changed during extraction.
Moved crypto and soak code differ only in includes and the already
established C02 byte-helper namespace substitution.

Every controller header's dependency trace must stay inside `src/`.
The seven split headers compile independently without libc; relocated
host crypto also passes freestanding M3/M33 syntax checks. Existing PSM
fragment headers retain their established composition/include-order
requirements; C04 does not claim to make those fragments standalone.

The baseline inventory recognizes the new owners. The C02 verifier no
longer expects crypto inside its production-only copy. Its reference
guard now permits dependency-only Makefile changes while comparing
profile-resolver inputs to the hash-checked original owner. Regression
tests reject changed flags, recursive selections and wrapper names.
No frozen record is rewritten to accommodate the move.

## Reproduction

From the repository root, using fresh evidence/build directories:

```sh
node --test clean-rx/tools/c01-baseline.test.cjs \
  clean-rx/tools/c02-isolation.test.cjs clean-rx/tools/c03-tests.test.cjs \
  clean-rx/tools/c04-contracts.test.cjs
make -C clean-rx/tests BUILD=../../build/clean-rx/c04-native-NEW \
  test test-copy-scope test-early-rx test-recovery
node clean-rx/tools/c01-baseline.cjs build/clean-rx/c04-matrix-NEW
node clean-rx/tools/c02-isolation.cjs build/clean-rx/c04-isolation-NEW
make -C clean-rx/badge BUILD=../../build/clean-rx/c04-badge-NEW all test
make -f clean-rx/peer/Makefile ht-peer ht-peer-test
make -C clean-rx/messenger BUILD=../../build/clean-rx/c04-messenger-NEW all test
make -f clean-rx/vendor-burst/Makefile \
  REF_BUILD=build/clean-rx/c04-reference-NEW reference reference-test
make -f clean-rx/vendor-burst/Makefile \
  REF_BUILD=build/clean-rx/c04-handoff-NEW reference-handoff
```

## Completed offline results

- All 17 capture/isolation/layout/contract tests pass. C04 compares 284
  constants, 236 layout properties and the eight-word manifest across
  14 selections, alongside implementation and boundary checks.
- All 74 isolated production selections retain byte-identical controller
  and PSM images; the seven frozen image hashes are unchanged.
- The seven-profile native matrix retains 24 passed, six disabled and
  twelve not-selected outcomes, plus both runtime/header-order checks.
- Default and all three named native diagnostic profiles pass with their
  existing sanitizer settings and explicit disabled-path reporting.
- Badge, peer, messenger and both reference/handoff development builds,
  their collectors and their native tests pass.

Local evidence is under `build/clean-rx/`, with the
`c04-matrix-20261024/manifest.json`,
`c04-isolation-20261024/manifest.json` and corresponding
`c04-native`, `c04-badge`, `c04-peer`, `c04-messenger`, `c04-reference`
and `c04-handoff` logs/build directories.
The isolation manifest SHA-256 is
`0cead839087e13d17c4df2d51f66203dae518a7fbf4f7069088172073b88d7ea`.
The frozen C01 JSON still hashes to
`3c2941a6b6fc684722b3ffbc9896ad28dddb6b04adf60146a74cca5de4454bf2`.

No hardware was accessed, flashed or used for RF execution. Native
models and successful builds do not qualify physical timing, over-the-air
reliability, or vendor feature parity.
