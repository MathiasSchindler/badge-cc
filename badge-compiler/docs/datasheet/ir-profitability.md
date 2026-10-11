# D07: target-informed IR profitability

Status: **implemented and offline-qualified**, with opt-in
`target-strength-reduce`. This is one bounded opportunity, not a new general
optimizer or a default promotion. D09's subsequent
[independent repeated suite qualification](qualification.md#completed-outcomes)
finds no resolved speed benefit or regression. The pass remains opt-in;
instruction reduction does not establish speed-default eligibility.

## Selected opportunity and usage

The fresh corpus contains unsigned power-of-two division/remainder that
still lowers to UDIV and, for remainder, MLS. Replace unflagged i32
`udiv x, 2^k` with `lshr x, k`, and `urem x, 2^k` with
`and x, 2^k-1`, for k = 0..31.

These identities apply to the complete unsigned 32-bit domain, including
zero and high-bit-set inputs. Divisor 1 becomes a zero shift or zero mask;
the source SSA value is retained, preserving poison propagation. Negative
literal spelling of unsigned 2^31 is accepted by typed bit-pattern semantics.

From the workspace root:

```sh
# Isolated pass on supported canonical IR.
badge-compiler/build/badge-opt --passes=target-strength-reduce \
  INPUT.ll -o OUTPUT.ll

# Recommended candidate pipeline; force/fresh-build policy rules still apply.
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,target-strength-reduce,aggregate-memory,wide-division \
  bash badge-compiler/scripts/build-owned.sh

# Native, lowered-byte, LLVM and ARM execution tests.
make -C badge-compiler test-target-strength
```

`BADGE_TEST_MODE=owned` runs the native/owned tests without LLVM or QEMU.
The registry, callable pipeline, CLI, metrics, capture and firmware drivers
share the same independently selectable pass. Driver provenance includes
the new implementation and shared model. The default C06 pipeline remains
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`, with
`direct/global-compact/thumb-layout/word` backend policies.

## Correctness and scope

The pass uses the existing typed instruction/operand analysis and canonical
token replacement. It requires an i32 local SSA dividend and a literal
nonzero power-of-two divisor. It changes an operation and its immediate,
not block structure, definitions, operand-use count or execution location.

- Signed arithmetic, other widths, nonconstant/nonpower-of-two divisors,
  direct undef/poison dividends, flags and metadata remain unchanged.
- In particular, `udiv exact` is retained; exactness/poison constraints are
  not silently dropped. A local value produced by `freeze` may participate.
- Existing producing overflow flags remain on their instructions. The new
  unsigned shift/mask adds no overflow promise or invalid shift count.
- PHIs, calls, volatile/MMIO operations, address expressions and their
  ordering remain intact. There is no speculation, motion or inlining.
- One SSA definition replaces one definition. There are no additional
  live values, new lifetime spans or address temporaries.

The transformation scan/constant-shift work is capped at 50,000,000 units.
It uses the existing analysis/parser/allocation bounds, including 16 MiB
input, and the existing verified pass boundaries. Budget exhaustion is an
explicit diagnostic; the eight-unit test preserves an existing destination.

## Shared model, lowering and lost fusion

[Implementation](../../src/ir_target_strength.c) queries the
[D03 shared model](target-cost-model.md) for ordinary ALU, divide, MAC and
MOVW/MOVT-pair costs. It uses baseline Thumb lowering, not instruction
counts guessed from the number of IR lines:

1. The old sequence includes divisor materialization and UDIV; remainder
   additionally needs MLS.
2. A shift is a legal immediate form. A mask uses a modified immediate when
   legal, otherwise explicit MOVW or MOVW/MOVT materialization followed by
   register AND.
3. Matching adjacent div/rem in either order is priced as **one** divide
   plus MLS, not two divides. The replacement is priced as both operations;
   losing C07 quotient sharing is included.
4. Accept only when the candidate's operation/materialization bytes do not
   exceed the baseline and its conditional issue upper bound is strictly
   below the baseline issue lower bound.

The old byte budget deliberately omits quotient-copy overhead; reported
operation savings exclude frame transfers, prologues, function-placement
padding and downstream fetch changes. ALU and MOV-pair model queries use
their actual four/eight-byte widths. Unsupported/unknown required model
dimensions do not grant a profitability win.

Divide issue estimates are conditional on nonexceptional dynamic operands.
The model does not resolve a zero dividend's timing, memory waits or whole
program behavior. Correctness at zero is independently tested; the cost
gate is not an unconditional cycle guarantee.
Branch folding/history and final-address alignment penalties also remain
unmodeled at C06; the local operation budget is not a whole-block prediction.

Pressure evidence has two levels: no added IR definitions/live spans by
construction, then actual allocated frame/load/store/spill checks.
Address and multiply/add shapes are not changed, avoiding lost address or
multiply-add fusion. Backend allocation policy is unchanged, but assignments
may improve when division's r4 scratch requirement disappears.

This candidate does **not** compose compact selection, scheduling or final
layout, and does not revive Z04's rejected `loop-cost`. Signed division
expansions, general reciprocal multiplication, expression reuse, induction
work, motion and bounded inlining require separate pressure/fusion/lowered
code evidence before expanding this pass.

## Qualification

[Machine-readable results](../../build/datasheet/d07-results.json) retain
source/tool/IR/assembly/ELF/UF2/log hashes, model events and limitations.

- 149 native sanitizer fixtures cover all 32 powers, both operations, both
  div/rem orders, widths, signed/nonpower/zero/variable cases, exact flags,
  metadata, poison/undef/freeze, PHIs and byte-stable idempotence.
- Release/sanitizer IR agrees; transformed IR passes LLVM verification.
- An independent LLVM-compiled caller uses volatile variable divisors for
  reference quotient/remainder, avoiding reference constant-strength
  reduction. Each image checks 2,048 values per power, including zero,
  maximum unsigned, divisor boundaries and the high bit, against four
  separately lowered functions.
- Five residency policies and both legacy/baseline-layout scalar selections
  execute under QEMU. Every matrix's final linked payload is no larger than
  its control. LLVM lowering of the transformed IR executes too.
- Each image additionally checks 512 call/volatile/loop-PHI cases with eight
  values live across a call and zero/one/many loop-count inputs.
- Owned-only tests, shared-model/metrics/default-pipeline/combiner
  regressions, pipeline failure propagation and seven build-driver tests
  pass. Release tools build with `-Wall -Wextra -Werror`.
- Owned/LLVM execution routes cover wide arithmetic, pressure and mixed
  AAPCS calls. Actual maze fingerprint, calibration, rendering, hazards and
  progression checks pass.

Across 45 archived inputs, default optimizer IR **and** default metrics
reports are byte-identical to pre-D07 tool snapshots. All 37 fresh frontend
inputs also reproduce the archived optimizer's default output. Projected
candidate IR equals the fresh candidate after normalization restricted to
source filenames and three known Wi-Fi protocol build-ID operands.

## Actual machine effects

The 37-module candidate changes 38 operations across nine modules:
21 unsigned divisions and 17 remainders. Three div/rem groups are charged
once. There are 110 unsigned div/rem candidates; 30 lack the required
local/literal shape and 42 are not nonzero powers of two.

| Static emitted operation | Control | Candidate |
| --- | ---: | ---: |
| UDIV | 107 | 72 |
| MLS | 46 | 29 |
| Direct private-frame loads | 5,237 | 5,233 |
| Direct private-frame stores | 4,221 | 4,217 |
| Annotated inline-assembly spills | 15 | 15 |
| Selected multiply-add fusions | 131 | 131 |

Call graphs, outgoing argument budgets and existing stack uncertainties
are preserved across 552 functions. No frame grows; `clear_gpio_edges`
in the GPIO monitor decreases from 40 to 32 bytes including saves.
These are local/static checks, not an invented callback, recursion or
interrupt stack bound.

The model records 84 conditional baseline issue units and 196 operation
bytes saved over unique module sites. These sums are not dynamic counts
or predictions of runtime/full-image savings.

## Final linked sizes

Six fresh images are smaller; six are unchanged. Data/BSS is unchanged in
all twelve:

| Image | Control text | Candidate text | Change |
| --- | ---: | ---: | ---: |
| button_mapper | 15,640 | 15,640 | 0 |
| diagnostics | 66,264 | 66,168 | -96 |
| gpio_events | 12,712 | 12,676 | -36 |
| gpio_monitor | 9,948 | 9,916 | -32 |
| hello_idle | 584 | 584 | 0 |
| imu_demo | 33,004 | 33,004 | 0 |
| lower_mapper | 16,008 | 16,008 | 0 |
| peripheral_probe | 19,060 | 19,060 | 0 |
| screen_test | 6,708 | 6,708 | 0 |
| tilt_breakout | 26,404 | 26,376 | -28 |
| tilt_maze | 41,508 | 41,496 | -12 |
| usb_wifi_udp | 395,124 | 395,052 | -72 |

The instrumented maze changes from 43,720 to 43,708 text bytes, with data
16 and BSS 160,448 unchanged. Only two qualifying unsigned divides occur
in the maze module. Its many signed divisions remain unchanged; this is a
small general improvement, not a solution to the measured physics gap.

Five alternating 37-input full-C06 CLI batches have median wall times
500.58 ms control and 499.92 ms candidate, including startup/I/O.
Median maximum individual process RSS is 35,504,128 versus 39,387,136
bytes. Summed CPU readings are 70/80 ms at 10 ms per-process resolution;
do not interpret the nearly equal wall medians or coarse CPU sums as a
throughput improvement. Raw runs and scope are retained.

## Promotion decision

Keep the pass **explicit-only**. D07 itself modified no badges while the
collector cleanup timeout was unresolved. Correctness, machine-code
and size gates pass. D09 subsequently fixes teardown and completes repeated
role-swapped hardware timing with verified restoration, finding no resolved
suite speed benefit or regression. Static divider removal alone does not
establish an actual maze or corpus runtime speedup.
