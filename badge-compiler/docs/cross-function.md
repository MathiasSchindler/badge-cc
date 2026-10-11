# PO16: costed helper sharing and specialization

The original host qualification below retained this pass explicitly.
[PO18](current-promotion.md) now promotes it as part of the independently
qualified combined default pipeline.

PO16 implements the explicit `cross-function` optimizer pass. It selects among
retaining a helper, constant specialization, straight-line inlining,
tail-forwarding elimination and identical-helper sharing. Selection uses the
**complete module's actual executable bytes**, not an LLVM inlining heuristic
or a red-square-specific answer substitution.

## Supported scope

[The pass](../src/ir_cross_function.c) uses the existing typed IR analysis and
known direct call edges. Eligible helpers are `internal`/`private`, nonvariadic,
non-address-taken, nonrecursive scalar functions. Parameters and results are
integers up to 32 bits, pointers or a void result; there are at most eight
parameters and 256 body instructions. Public/external and address-taken
functions retain their names and ABI. Quoted/unquoted global spellings are
canonicalized when checking references. Direct and mutual recursion are
excluded, as are unknown/explicit-signature call edges and specialized ABI
attributes such as `byval`, `sret` and nonstandard calling conventions.

The first bounded policy supports:

- **Constant specialization.** One literal-argument tuple is proposed per
  helper, choosing the first call with the most known integer/null arguments.
  Matching calls form a group. Constant parameters are substituted and removed
  from the signature **and every selected call**, including stack arguments.
  If all calls match, the helper is specialized in place. Otherwise a fresh
  private clone is proposed while charging the entire retained generic helper.
  Other tuples/calls stay generic. Only the selected body runs existing
  constant-fold, scalar-cleanup, SCCP and dead-instruction cleanup; unrelated
  callers/bodies are not cleaned up to manufacture a profitability win.
- **Straight-line inlining.** A single-block helper of at most sixteen
  instructions, without allocation, opaque operations or nested calls, may
  replace all its known calls. SSA names are fresh, return values retain their
  types and the unused helper is removed. Volatile reads/writes retain their
  original order and multiplicity.
- **Tail-forwarding elimination.** A two-instruction direct-call/return
  wrapper may be inlined into its callers, eliminating one call boundary while
  preserving the forwarded call. This is a call-graph simplification, **not**
  a new native tail-jump emitter or guessed frame teardown. Ordinary `tail`
  hints remain unchanged; unsupported `musttail` fails explicitly.
- **Identical-helper sharing.** Helpers with identical signatures/attributes
  and byte-identical body text can share one existing implementation, provided
  both are eligible and non-address-taken. Calls remain calls, with the same
  arguments and effects. This includes identical MMIO/barrier sequences inside
  whole helpers; arbitrary partial-sequence outlining is outside this first
  bounded policy.

Symbolic inline assembly can refer to a function without an IR operand.
Consequently unrecognized assembly templates reject the **whole module** from
cross-function changes. A small reference-free whitelist admits NOP sequences,
wait/event instructions and fixed barriers/interrupt hints, without moving or
removing them. Opaque helper bodies are not specialized or inlined; identical
helper sharing preserves their complete bodies.

LLVM IR `noinline` is honored, including attribute groups; `alwaysinline`
diagnoses as unsupported by the size-gated contract rather than being ignored.
The owned C frontend's existing unsupported GNU attribute diagnostics remain
unchanged. C inline hints are not forced-inline promises.

No assumptions are made about other translation units. Cross-module
specialization or public ABI rewriting would need a separate explicit
closed-world contract; neither is performed here.

## Economics and limits

Each eligible helper gets a bounded set of alternatives, including keeping
the current shared body. The original complete module is compiled once for
that comparison. Proposed alternatives use the PO14 backend policy
**direct/global-compact/thumb-operands/word** and the shared owned assembler.
This accounts for parameter transport, every caller, every retained generic
body, clones, instruction encodings and executable-section bytes.

Only a **strict whole-module reduction** is admitted. Final frame, callee-save
and allocation-spill counts may not grow for any surviving function; clones
must fit their generic parent's original limits. Inlining cannot hide a
caller-frame regression behind the removed helper's bytes. Smaller outgoing
argument areas are allowed. Every accepted decision records its original
helper, clone (if any) and before/after complete-module costs in IR comments.
Rejected alternatives and exhausted candidate budgets appear in metrics.

There are at most eight helper attempts per module, and a 64-million-unit
work/input budget, in addition to existing IR/backend/assembler limits.
Candidates do not recursively trigger more optimization passes. An emitted
completion marker makes repeating this bounded selection idempotent; remove
the marker to deliberately re-evaluate an edited module. Budget or
backend/assembler failure diagnoses and preserves an existing CLI output,
rather than masquerading as a rejected candidate.

This economics policy is backend-specific. Other backend selections remain
semantically valid but have **no PO16 profitability qualification**. No
whole-program time estimate is inferred from byte savings; physical timing,
flash behavior and software-delayed peripheral waits remain promotion gates.

## Qualification

[The final persistent report](../build/po16-qualified-sharing/results.json)
uses verified PO15 inputs and PO14 backend artifacts:

- **37 modules / 554 original functions / twelve images** checked.
- Sixteen modules change; **406 executable bytes** are saved across the module
  inventory, with no frame/save/spill growth.
- Twenty-two production helper specializations are admitted. Of 71
  specialization alternatives, 49 are rejected because complete-module
  output is not smaller. Generic/clone costs are never omitted.
- Fourteen modules reach the eight-helper attempt cap; the report records
  this deliberately bounded scope rather than claiming exhaustive optimization.
- Eleven images shrink, with data/BSS unchanged and physical budgets and
  ELF-to-raw loaded-byte equality verified:

| Image | Loaded bytes saved versus PO15 |
| --- | ---: |
| button_mapper | 44 |
| diagnostics | 52 |
| gpio_events | 12 |
| gpio_monitor | 4 |
| hello_idle | 0 |
| imu_demo | 80 |
| lower_mapper | 44 |
| peripheral_probe | 64 |
| screen_test | 4 |
| tilt_breakout | 56 |
| tilt_maze | 52 |
| usb_wifi_udp | 200 |

These image savings are not additive across shared module copies or milestones.
Production benefits currently come from specialization; the inlining, sharing,
tail-forwarding and profitable retained-generic clone paths have reached
semantic/economics fixtures, not a fabricated production-benefit claim.

Fourteen targeted tests cover specialization, volatile order, helper sharing,
tail forwarding, profitable clones with dynamic generic calls, signed narrow
and eight-argument ABI transport, address spelling, recursion, `noinline`,
forced-contract failures, hidden assembly references, idempotence, rejected
candidate lifetime/output rollback and resource exhaustion. Actual production
source is exercised in two independently verified call graphs:

| Reached QEMU instruction batch | Control | PO16 |
| --- | ---: | ---: |
| USB report construction/validation and CRC/error paths | 16,853 | 16,549 |
| Wi-Fi peer filtering, comparison and clearing helpers | 2,010 | 1,967 |

These are **instruction trace counts**, not badge cycles or elapsed radio
timing. The entire batch includes startup, checking and semihosting; termination
can vary by one instruction. Two persistent tests reject fabricated policy,
clone provenance, allocation facts, byte accounting and artifact hashes.

The original independent corpus capture compiled only sixteen changed modules
and relinked eleven affected images. Tail-forwarding and hidden-reference
qualification subsequently produced identical production IR. Hash-checked
reuse therefore let final qualification compile **zero** production modules
and relink **zero** images, while recording the expanded targeted tests and
new optimizer events. Earlier exploratory/qualification reports remain intact.

The latest [red-square extra oracle](../../build/red-square-po16-candidate/events.json)
has no admitted IR change. It remains **1,064 loaded bytes** under the PO14
chain, versus 560 for LLVM and 1,432 for the owned default. Shared byte-write
helpers are not blindly duplicated to try to win this particular example.

## Usage and reproduction

Append the pass after the qualified optimizer pipeline:

```sh
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function \
BADGE_CODEGEN_SCALAR_SELECTION=thumb-operands \
  bash badge-compiler/scripts/build-owned.sh all
```

Focused tests and a fresh capture:

```sh
make -C badge-compiler test-cross-function
node badge-compiler/tools/cross-function-evidence.mjs --capture \
  badge-compiler/build/po16-new-capture \
  badge-compiler/build/po15-loop-scalar/results.json
```

An optional fourth argument names a prior verified PO16 report. Reuse requires
the same baseline, byte-identical measured IR, hash-verified outputs and identical
backend/assembler/link/conversion tools. Matching image inputs allow reuse of
already verified linked outputs too. No approximate source matching is used.

Static verification reruns **no compilation, QEMU or hardware campaign**:

```sh
node badge-compiler/tools/cross-function-evidence.mjs --verify \
  badge-compiler/build/po16-qualified-sharing/results.json
```

`--verify-current` additionally checks recorded source identities. Measured IR
and optimizer events are emitted together, avoiding duplicate optimizer runs.

The owned optimizer/backend defaults and root LLVM firmware default remain
unchanged. No badge was programmed and no hardware campaign ran.
