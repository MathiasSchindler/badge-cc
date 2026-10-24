# Controller native tests

Run from the repository root:

```sh
# Default unit/model tests plus badge integration tests:
make -C clean-rx/tests test

# Controller-only unit/model tests; no badge, peer or general source required:
make -C clean-rx/tests unit-test

# Badge/station integration coverage:
make -C clean-rx/tests integration-test

# Explicit diagnostic profiles, each in its own build subdirectory:
make -C clean-rx/tests test-copy-scope test-early-rx test-recovery
```

`HOST_CC` selects the native compiler. `BUILD` selects the native output
directory, relative to `clean-rx/tests/` unless absolute; its default is
`build/clean-rx/tests` relative to the repository root. Named profile targets
use `BUILD/profiles/<profile>/` and replace `EXTRA_CFLAGS` with their reviewed
profile flags rather than mixing in unrelated overrides.

All C tests retain `-Wall -Wextra -Werror` and address/undefined-behavior
sanitizers. `test_flags` tracks native compiler/flag changes, separately
from the controller's `build_flags`, so changing profiles rebuilds every
affected native executable.

## Ownership

| Directory | Suites | Dependencies |
| --- | --- | --- |
| [unit/](unit/) | Runtime byte/type compatibility, emitted PSM model, Block ACK and recovery model | Controller source, standard native headers/libraries and test-owned support only |
| [support/](support/) | Independent bitwise CRC reference | Test code only; no firmware dependency on it |
| [integration/](integration/) | MAC/station/soak suite, copy-scope capture/protocol suite, early-RX peer protocol suite | Controller code plus badge/peer fixture headers and general diagnostics explicitly listed in the test Makefile |
| [fixtures/](fixtures/) | Host-only HT packet builders, expected-pattern/evidence checks, returned-ACK checks and soak/fragment campaigns | Native tests and RP2350 development applications; never imported by controller source |

Fixture consumers include the badge diagnostic application, vendor-burst
campaigns and independent peer monitor. This is development ownership, not
a requirement that every consumer be a native test. Host software WPA2
cryptography is shared separately under [host/](../host/).

Controller-required command codecs remain in
[ht_command.h](../src/ht_command.h). The controller validates some finite
diagnostic commands by reconstructing their canonical bytes. Host fixture
code reuses those codecs, not a second implementation. The portable HT
header and ABI headers do not import that command layer. Isolating active
diagnostic behavior from ordinary firmware remains C05; this move does
not change which commands existing profiles accept.

Run the boundary, implementation-preservation and ABI compatibility checks:

```sh
node --test clean-rx/tools/c04-contracts.test.cjs
```

They compare constants, every structure field offset, alignment and size,
and the layout manifest with hash-checked C01 headers, including extended
RX, handoff, streaming and aggregate-delivery selections. See
[the C04 record](../dev-doc/C04_CONTRACT_SEPARATION.md).

The MAC suite contains both portable-MAC checks and badge station/fixture
checks. It is conservatively classified as integration in full, rather than
pretending it is controller-only or dropping its existing checks. It keeps
all assertions and independent general-tree CRC comparisons. Other native
tests belonging to badge applications, the independent vendor peer and the
messenger remain owned by those applications' Makefiles.

PSM simulators remain in their existing test translation units, now under
unit/integration ownership. They are not runtime modules and are never
linked into controller firmware.

## Profiles and coverage

| Target | Selection | Executed checks and limits |
| --- | --- | --- |
| `test` | Caller-supplied `EXTRA_CFLAGS`, empty by default | Runtime in both header orders, PSM, Block ACK, recovery executable and MAC/station integration |
| `test-copy-scope` | Scope-on observer selection matching C01 `copy-scope` | Default suites plus emitted copy-scope model and capture/protocol checks |
| `test-early-rx` | Early-FIFO passive recovery selection, without ordered-flush/prefix diagnostic | Default suites plus early-RX admission/protocol checks; ordered recovery reports disabled |
| `test-recovery` | Ordered-flush and prefix selection matching C01 `early-prefix` | Default suites plus early-RX; ordered-flush/prefix recovery checks actually execute |

The defaults and constraints are in [profiles.mk](profiles.mk). The test
profile selections are checked against the frozen C01 definitions by
[the test-layout checks](../tools/c03-tests.test.cjs); they do not change
the firmware profile definitions owned by existing application/experiment
builds. Profile centralization for production callers remains C07.

Low-level `copy-scope-test` and `early-rx-test` targets still accept explicit
`EXTRA_CFLAGS`. They preserve experiment runners' exact selected profiles;
prefer the `test-*` named profiles when no custom experiment is intended.
The recovery executable explicitly reports disabled when ordered flush is
off. That is not an executed recovery pass. An unselected focused test is
not coverage merely because another suite succeeded.

## Compatibility and production builds

Existing `make -C clean-rx/src ... test`, `runtime-test`, `copy-scope-test`
and `early-rx-test` invocations forward to this Makefile with an absolute
output path and unchanged `EXTRA_CFLAGS`. Unit/integration and named profile
targets can also be forwarded. This preserves vendor-burst and documented
historical runners without changing their experiment flags or output paths.
Native executable build rules themselves now belong to this Makefile;
tools requesting an executable directly must use `-C clean-rx/tests`.

The controller's ordinary `all` target does not include this Makefile or
depend on the test directory, test sources, fixtures or native libraries.
Both a production-only copy and a unit-only copy are validated separately.

These are native/model/integration tests, not RF emulation, physical timing
qualification, or permission to flash devices. See
[the C03 validation record](../dev-doc/C03_TEST_SEPARATION.md).
