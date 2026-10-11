# D08: redundancy coprocessor decision

**NO-GO as a performance optimization. DEFER instrumentation to a separate,
explicitly requested hardening project.** This is a feature-scope decision,
not a vulnerability audit or a security certification.

## Applicability and evidence

RP2350 RCP on port 7 provides fault-detection, validation and canary mechanisms.
It is not an arithmetic accelerator and its presence does not prove program
integrity. See [coprocessor facts](coprocessors.md), RP2350 section 3.6.3,
[the ledger](fact-ledger.json) (`RP2350-RCP-001`,
`RP2350-COPROC-TIMING-001`) and
[errata/open questions](errata-and-open-questions.md). E26 qualifies delay
variants; chip-ID revision 3 does not distinguish A3/A4, so applicability
cannot be resolved merely from that revision field.

[D08 evidence](../../build/datasheet/d08-results.json) records D02's 12
historical environments: CONTROL=0, CPACR=`0x0000c000`, NSACR=`0x80`.
These are port 7 permissions, not evidence of actual RCP instrumentation,
initialized state or permissions on ports 0/4/10/11. They also do not prove
current playable firmware state; CPACR_NS was not captured.

The owned assembler rejects MCR, preserving the old output. The port-7
parser fixture only tests unsupported instruction handling; its operands
are **not** claimed to encode a valid RCP operation. No RCP code is executed
or enabled during this decision.

## Potential beneficiaries and correctness prerequisites

Only a stated fault/threat model can identify useful validation boundaries:
for example an integrity-sensitive transition or a specifically required
redundant check. Adding operations everywhere cannot establish coverage,
fail-safe handling or immunity from fault injection. D08 identifies no
reached performance beneficiary and makes no claim about security defects
in existing firmware.

A future project must define required invariants, permitted fault model,
coverage and independent failure responses before choosing operations.
Specify runtime/security access, initialization and context ownership,
interrupt behavior, ABI effects and legal instruction sequences. Resolve
applicable errata using authoritative silicon/source evidence. Never remove
required checks or delays to manufacture a speed gain, or silently recover
from a failed validation as though it succeeded.

## Freestanding option, cost and reopening gate

If requested, implement original explicit wrappers/instrumentation in the
owned compiler/firmware with documented legal encodings and visible failure
paths; no SDK or standard library is required. Preserve an uninstrumented
performance control, while recognizing it is not an equivalent hardened
build. Avoid an automatic generic pass until its semantic/security contract
and coverage are reviewed.

Transfers, state management, checks, delays, register pressure and failure
paths generally add code and time. A timing table's interface occupancy is
not the total protection overhead, and a smaller sequence is not proof of
equal protection. E26 delay behavior must be included where relevant.

Reopen only as an independently scoped hardening milestone with threat-model
review, instruction/state tests, controlled fault tests and explicit
acceptance for detected/undetected faults. Require ABI/interrupt/stack and
negative-diagnostic coverage plus retained measured overhead. Security
claims require that project's own evidence and review; ordinary D09
performance tests cannot supply them. Until then preserve all current
compiler/runtime behavior and keep RCP separate from integer speed work.
