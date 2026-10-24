# C05: explicit diagnostic firmware profiles

C05 changes the default controller policy. The old default included active
ACK suppression; the new `runtime` profile cannot arm it. This is not a
claim of feature parity, RF timing, or improved on-device reliability.

## Building and identifying profiles

Run from the repository root, with Clang and LLVM tools on PATH and an
explicit output directory:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx/runtime
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx/diagnostic PROFILE=diagnostic
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx/sdio-test PROFILE=sdio-test
```

The `diagnostic` and `sdio-test` Make targets are shortcuts using
`BUILD/profiles/<name>`. Detailed existing `EXTRA_CFLAGS` remain available;
this milestone does not replace the experiment catalogue.

| Profile | ID | Commands and behavior |
| --- | --- | --- |
| `runtime` (default) | 0 | Existing ordinary transport, station, TX, response/EDCA/WMM policy commands, subject to their existing build and state gates. HT command 12 admits normal version 1 only when HT data is enabled. |
| `diagnostic` | 1 | Legacy command admission, including ACK suppression (9), HT probe (11), diagnostic versions of HT data (12), and Block ACK experiment (17), with all original feature/state/packet checks retained. |
| `sdio-test` | 2 | Synthetic F2 traffic only; bypasses radio startup. Its command 7 generates a bounded batch of 1..16 pattern packets. Pacing/window/transition/DMA/large-packet options remain available. |

Command 7 in radio firmware still means ordinary station configuration;
it is not globally banned because the SDIO test variant reuses its number.
Legacy `PROFILE=diagnostic` plus `CLEAN_SDIO_SELF_TEST=1` also remains an
explicit transport-test selection, for reproducible historical builds.

Every controller build emits `profile.txt` with its name, ID and complete
compiler flags. The ELF has an absolute `clean_rx_profile_id` symbol
(`llvm-nm <image.elf>`). Keep these beside the raw binary: mailbox offsets,
magic values and raw-image layout have not acquired a new identification
field. A raw image alone has no newly added runtime-queryable profile ID.
The flags stamp includes the profile, and content comparison forces
recompilation even with Make versions using whole-second timestamps.
Unknown profiles, conflicting definitions, active experiments in runtime,
and disabled/stray SDIO test selections fail the build explicitly.

## Ownership and runtime safeguards

All controller inputs still live inside [src/](../src/), without libc,
host code, fixtures or general-tree includes. Diagnostic implementation
belongs to [src/diagnostic/](../src/diagnostic/):

- `sdio_run.inc` owns the synthetic traffic loop; `sdio_test.h` owns its
  patterns and test-window helpers.
- `ack_command.inc` owns command 9; `ack_configure.inc` owns suppression
  arming, limits, epoch and readback checks.
- `psm_ack_test.h` owns the exact bounded-suppression instruction fragment.
- `ht_command.h` owns canonical diagnostic packet encoders and admission.
  Host fixtures explicitly consume this same codec rather than copying it.

The narrow include hooks are selected by the firmware profile.
`firmware_command.h` defines runtime command admission; `ht_payload.h`
retains arbitrary valid normal HT v1 payloads, not just fixture bytes.
Clocked diagnostic HT is also compiled out of runtime.

Runtime rejects commands 9, 11 and 17, and non-v1 command-12 packets,
through existing firmware error `0x85`, recording the mailbox command
address and rejected value. This happens even when TX is disabled.
Other validation errors retain their normal handling.

Runtime PSM jumps directly from `ACK_PREPARE` to the ordinary ACK/CTS
preamble. The 47 former suppression slots are inert branches, preserving
later instruction addresses without retaining a suppression implementation.
Even nonzero, corrupt or stale suppression SHM words cannot drop ACKs,
change their suppression counters, or activate the old suppression fault
path. The diagnostic profile emits the original instructions unchanged.

Station resets still clear and verify shared evidence, with zero
suppression limits. Passive snapshots, ACK deadline/arithmetic evidence,
fault publication, watchdogs, and ordinary RX/response recovery remain.
Runtime can retain passive observer and genuine rearm/reset/recovery
options; `profile_checks.h` instead requires diagnostics for injection,
parking, synthetic HT/probe/aggregate traffic and copy/DMA/stream-layout
experiments. Existing inter-feature assertions remain in force.

## Callers and compatibility

- The diagnostic badge application defaults explicitly to
  `PROFILE=diagnostic`, including its host tests/collector. When supplying
  `CONTROLLER_BIN`, build it first with matching flags/profile.
- Reference/vendor-burst controller and stimulus builds opt into diagnostics.
  Their wrapper selections and experiment flags have not changed.
- Messenger explicitly selects runtime and uses normal HT v1 admission.
- Root synthetic-SDIO recipes explicitly select `sdio-test`; ordinary root
  controller/native-test builds retain the runtime default.
- Named native copy-scope, early-RX and recovery profiles explicitly select
  diagnostics. Default native tests now exercise unsuppressed runtime ACKs.

This breaks callers that previously built the default controller and then
sent diagnostic commands, or enabled active experiments only through flags.
Select a diagnostic profile for those campaigns. Ordinary legacy TX,
normal HT v1, passive evidence and shared wire layouts are preserved.

The C01 baseline JSON is immutable. Baseline capture and isolation
comparisons now explicitly select **legacy diagnostic behavior**, including
the historical profile named `default`; that name does not mean the new
runtime default. Their manifests identify this distinction. Standalone
copying includes diagnostic subdirectories and Make includes.

## Offline acceptance

Reproduce with fresh evidence directories:

```sh
node --test clean-rx/tools/*.test.cjs
make -C clean-rx/tests BUILD=../../build/clean-rx/c05-runtime-NEW test
make -C clean-rx/tests BUILD=../../build/clean-rx/c05-diagnostic-NEW PROFILE=diagnostic test
make -C clean-rx/tests BUILD=../../build/clean-rx/c05-profiles-NEW \
  test-copy-scope test-early-rx test-recovery
node clean-rx/tools/c01-baseline.cjs build/clean-rx/c05-matrix-NEW
node clean-rx/tools/c02-isolation.cjs build/clean-rx/c05-isolation-NEW
```

The C05 tests execute the actual dispatch-guard source against all 256
command bytes and all 256 version bytes in both profiles, verifying
rejection and exact error fields. Native tests cover arbitrary normal HT
payloads at all eight MCS values, disabled/clocked/corrupt/version controls,
and runtime PSM behavior with valid and corrupt suppression controls.
Diagnostic tests retain bounded suppression and fault assertions at all
12 legacy response rates. These are software/model tests, not RF evidence.

Profile tests also build all three freestanding variants, check undefined
symbols and identity, switch profiles rapidly in one output directory,
reject incompatible flags, and reproduce runtime without even copying the
diagnostic directory. Existing C04 tests retain ABI/layout checks; their
original-body check excludes the intentionally profile-gated payload
validator, which has dedicated C05 behavior tests instead.

Local evidence lives under `build/clean-rx/c05/`, with diagnostic matrix
and isolation manifests in adjacent `c05-matrix-verified` and
`c05-isolation-verified` directories. All 74 retained flag selections
(66 reference resolver entry points) reproduce C01 controller and PSM bytes
under explicit diagnostic selection. The 19 cleanup tooling tests pass,
as do runtime/diagnostic native suites, named diagnostic native profiles,
badge and messenger builds/tests, reference/handoff builds/tests, peer
tests, root transport tests/collector and all five root SDIO variants.
Runtime early-prefix recovery also remains buildable and model-tested
without diagnostic inputs; the distinct passive host-PHY-reset
configuration remains buildable too. Their existing mutual-exclusion
checks are unchanged. Historical coverage remains 24 passed,
six disabled and twelve not selected across seven profiles; runtime checks
are separate rather than misreported as historical binary equality.

## Separate hardware acceptance gate: NOT RUN

C05 changes ordinary ACK-path timing and command admission deliberately.
The earlier C04 receive-only device check does not qualify it, and its
one-time hardware authorization has ended. No C05 device flashing,
execution or RF campaign is authorized or performed here.

Before qualifying C05 on hardware, obtain new authorization and retain
backup/restore safeguards. Test the new runtime profile for receive/F2
integrity, normal legacy/HT v1 TX, ACK/CTS response timing, station policy,
passive deadline/fault evidence and recovery. Confirm diagnostic-command
errors without traffic injection. Separately qualify diagnostic ACK-loss,
HT/aggregate and SDIO campaigns only within their authorized scope.
Record matched host/controller profile, flags, hashes and independent
on-device evidence; native/byte-identity results do not substitute for it.
