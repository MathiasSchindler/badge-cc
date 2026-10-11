# D08: double coprocessor decision

**NO-GO for strict automatic C `double` substitution. DEFER an explicitly
named, relaxed-semantics freestanding API until a real beneficiary exists.**
DCP is independent of the FPU feature decision and is not enabled by D08.

## Applicability and evidence

RP2350 DCP assists double-precision operations with **complete CPU plus
coprocessor sequences**, including register transfers and CPU mantissa
multiplication. Port 4 addresses the current security-state instance;
port 5 addresses the Non-secure instance but can be used only from Secure
code. See [coprocessor facts](coprocessors.md), RP2350 section 3.6.2 and
[the ledger](fact-ledger.json), `RP2350-DCP-001` through `003`.

Standard sequences use nearest-even rounding but flush subnormal inputs
and outputs to zero. Faster divide/sqrt variants may be incorrectly rounded.
These facts prohibit treating the published throughput figure as a strict,
single-instruction `double` implementation.

[D08 evidence](../../build/datasheet/d08-results.json) finds no float/double
types in the 37 optimized control modules. The owned assembler rejects CDP;
the compiler does not provide end-to-end floating lowering. This is a parser
boundary probe, not execution or validation of a DCP algorithm. D02 captured
CPACR=`0x0000c000`, NSACR=`0x80` in all 12 historical environments: those
records establish port 7 permissions, not CP4 access. CPACR_NS and live DCP
engagement state were not captured; production entry state is unresolved.

## Correctness and dependency-free option

An optional future API must advertise nearest-even/flush-to-zero explicitly,
define signed zero, infinity/NaN payload behavior and exceptions, and state
whether fast inaccurately rounded variants are allowed. Do not silently
weaken generic C/IR semantics or mix relaxed and strict callers. Cover add,
subtract, multiply, divide and square root separately, including overflow,
underflow, cancellation and exceptional operands. A permitted semantic
subset is an API contract, not a success-shaped fallback.

Use original, reviewed freestanding implementations based on the documented
interface; do not redistribute licensed canned source sequences or add an
SDK/libm dependency. Enforce register-pair legality, ABI saves, control/state
access and negative diagnostics in owned encoding/linking.

Specify who owns each security-state instance. An interrupt using an engaged
instance needs the documented context save/restore protocol; blanket
interrupt masking is not a free or acceptable default. Nested interrupts,
Non-secure calls and second-core use require an explicit ownership analysis,
not assumptions that one instance is globally shared or globally independent.
Stack/call cost and failure/fault handling must remain visible.

## Costs and reopening gates

Price complete operators: CPU arithmetic, transfers, readiness stalls,
marshalling, save/restore and special-case branches. Ideal CDP/MCR/MCRR
interface occupancy and MRC/MRRC result delay are not complete-operation
latencies. Compiler call overhead can outweigh the accelerator benefit.
No runtime gain for this integer corpus is demonstrated.

Reopen for one named operation in a reached double workload with approved
relaxed semantics. Compare original helpers to an independent oracle under
that same contract, with subnormal, halfway-rounding, NaN, infinity and
signed-zero cases and interrupt/state stress. Verify base-PCS interoperability,
negative access cases, frame budgets and owned output preservation; benchmark
the full call and context path on badges under D09. Strict generic lowering
would additionally require a complete strict-semantic implementation, not
just these canned nearest-even/FTZ paths.
