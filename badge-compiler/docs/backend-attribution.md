# PO02: fixed-IR backend attribution

PO02 isolates a concrete general mechanism before PO03/PO04 implementation.
LLVM is an investigative reference, not a specification or performance ceiling.
No optimization/default promotion is made here.

## Evidence and controls

- [Qualified attribution report](../build/po02-release-attribution/results.json):
  11 C modules, all function disassemblies, frame categories, IR/emission
  mappings, source/tool/object/image hashes and ranked opportunities.
- [Qualified fixed-IR badge campaign and full-flash preservation](../../build/application-benchmark-runs/run.PnNksy/results.json).
- [Focused attribution and hybrid semantic tests](../build/po02-tests.log).
- [Legacy/application protocol and preservation tests](../build/po02-protocol-tests.log).
- [PO01 ordinary Clang comparison](application-measurements.md), a separate
  experiment, not the denominator used below.
- [Commands and reproduction](pipeline-optimization.md#po02-implementation-and-reproduction).

Both C backends consume **the same owned optimized IR bytes**, after stripping
only filename metadata. All 11 module hashes match. LLVM/Clang 22.1.8 uses
`-Oz`, Cortex-M33 Thumb, `-Xclang -disable-llvm-passes -x ir`. LLVM's normal
IR optimizer is disabled; necessary target legalization, machine combines,
allocation, placement and its assembler remain enabled. The ABI is the same
freestanding Arm EABI/AAPCS interface. The owned backend uses unchanged C06 and
`direct/global-compact/thumb-layout/word`.

The owned assembler handles original assembly inputs; both images use the
owned linker and the existing benchmark linker script. Thus this measures the
C backend boundary, not different preprocessors, frontend optimizers or linkers.
The wrapper first executes the normal owned pipeline and retains every stage;
it then replaces only the selected C backend object. No fallback accepts an
unsupported backend or failed compilation.

The trace backend is an isolated `CG_ATTRIBUTION_TRACE` build. It annotates IR
line boundaries, frame slots/private objects/parameter status, register
assignments and PHI buffers. **All 11 trace objects equal normal owned objects
byte for byte.** Trace mode explicitly rejects scheduled emission, avoiding
false IR boundaries for instructions moved across markers.

The report binds capture-time hardware provenance and current analysis hashes
separately. The analysis tool was corrected after hardware capture to handle
local disassembly labels, exact Thumb halfwords, LLVM's non-frame use of r7,
and pre-emission allocator metadata. Only that analysis tool is permitted to
drift; production/compiler source drift is rejected. Earlier analysis captures
are superseded, not speed evidence. `--verify` checks artifact hashes and
recomputes traces, inventories and machine counts.

## Hardware crossover

The hybrid passes the actual application's five-seed Cortex-M33 semantic
tests before programming badges. On hardware, two seeds and two swapped
physical roles each yield **four rounds, eight reports and 336 raw samples**.
All CRC/version/build/nonce/order/work-count checks, fingerprints and guards
match, including goal/hazard encounters. Clock settings match and remain
unchanged during timing.

| Reached workload | Owned/fixed-IR LLVM median ratio | Four-observation range |
| --- | ---: | ---: |
| Generation | 5.730x | 5.729-5.730x |
| Input and physics | 3.503x | 3.449-3.559x |
| RAM rendering | 4.508x | 4.501-4.515x |
| Actual display transfer | 3.845x | 3.845-3.845x |
| Reached frame path | 3.886x | 3.873-3.899x |
| Diagnostics/graphics | 4.256x | 4.255-4.257x |

These are geometric means of four paired median ratios, **not object-size
ratios**. Units remain timer0 raw ticks, not calibrated microseconds.
For dense physics the medians are approximately **77,036-77,037 ticks LLVM**
and **274,144-274,168 owned** for 64 steps. Dense frame work takes
**106,654 LLVM** versus **413,078-413,123 owned** for 16 scripted steps.
Display transfer takes **67,713-67,714 LLVM** versus **260,373-260,384 owned**
for the dense seed's four scripted region operations.

The linked hybrid has 15,184 text bytes, versus 70,812 owned; both have 8 data
bytes and 167,800 BSS bytes. These are benchmark images, not ordinary maze
sizes. The remaining 3.5-5.7x measured gap establishes a backend opportunity.
Different ratios versus PO01 do not establish exact additive frontend/backend
percentages: machine passes, representations and layout interact, and RAM
rendering is slightly faster from owned IR through LLVM than from Clang's
ordinary pipeline for these inputs.

## Per-function machine expansion

Machine counts come from disassembled objects. Marked literal data is excluded;
linked symbol presence/extent, pool bytes and instruction bytes are separate.
Static calls, branches, copies, push/pop, loads/stores and extensions are
retained for every function, not merely the selected rows below.

| Function | Executable bytes owned/LLVM | Static instructions owned/LLVM | Direct frame load/store instructions owned/LLVM |
| --- | ---: | ---: | ---: |
| `generate_maze` | 6,096 / 1,004 | 1,901 / 382 | 116+86 / 6+8 |
| `collide_wall` | 1,628 / 368 | 501 / 125 | 35+24 / 0+0 |
| `screen_pixel` | 406 / 92 | 131 / 31 | 3+3 / 0+0 |
| `badger_gfx_clear` | 122 / 18 | 40 / 8 | 1+1 / 0+0 |
| `lcd_write_byte` | 144 / 28 | 46 / 13 | 0+0 / 0+0 |
| `badger_gfx_text` | 772 / 174 | 237 / 70 | 24+19 / 8+6 |

Only owned r7 accesses and LLVM sp accesses are direct-frame counts. LLVM r7
is often an ordinary global-data base, **not** a frame pointer; no `.setfp`
appears in these objects. Indirect accesses through object pointers and incoming
stack arguments are not guessed into this table. Static counts do not reveal
loop visit frequency or dynamically retired instructions.

### Frame traffic is not all spilling

- `generate_maze` owns a 1,088-byte frame plus 32 saved-register bytes. Its DFS
  stack/candidate/private objects require substantial storage; LLVM's annotated
  frame adjustment is 1,028 bytes, so blindly removing the frame is not legal.
- Its 116 direct loads include 70 scalar/parameter-home accesses, 45 PHI-buffer
  accesses and one PHI-edge-home access. Its 86 direct stores are 41 scalar
  homes plus 45 PHI-buffer stores. Private objects are predominantly accessed
  indirectly and stay in total memory counts, not the direct-frame category.
- Allocator metadata says 304 allocated values and 41 unallocated/spilled
  values. That is a **value assignment count**, not 41 dynamic evictions.
  LLVM explicitly annotates two spill stores and six reload loads here;
  its other stack accesses include real objects/arguments, not implicit spills.
- `badger_gfx_clear` has **zero unallocated allocator values**, yet one PHI
  temporary store and load. This is transport for a register-resident PHI,
  not evidence of register-pressure spilling.
- `collide_wall`'s direct stores include an entry parameter home, 12 scalar
  homes and 11 PHI-buffer transfers; its loads include 24 scalar homes and 11
  PHI-buffer transfers. LLVM annotates no spills/reloads.
- Parameter entry/incoming arguments, by-value/private object reservations,
  PHI-edge homes/buffers, callee-save push/pop and inline-assembly spill metadata
  are separate surfaces. The traced functions above report zero asm spills.
  Generic volatile IR and MMIO driver operations remain explicitly identified;
  they must not be removed to improve a benchmark.

## Trace to the emission decisions

### Repeated comparison-to-boolean-to-branch

In the actual graphics clear loop, owned optimized IR contains:

```llvm
%v2 = icmp ult i32 %".badge.opt.1", 76800
br i1 %v2, label %b2, label %b4
```

The trace maps it to:

```asm
mov r0, r5
movw r1, #11264
movt r1, #1
cmp r0, r1
mov.w r0, #0
it lo
movlo r0, #1
ubfx r0, r0, #0, #1
mov r4, r0
mov r0, r4
cmp r0, #0
bne.w .Lcg_edge5
b.w .Lcg_1_b4
```

The compare path in [codegen.c](../src/codegen.c) materializes a canonical
boolean, [cg_save/cg_normalize](../src/codegen_thumb.c) normalizes i1, register
residency copies it into a saved register, and the conditional branch reads
it back and compares it with zero. The same adjacent structure appears
48 times in generation, 12 in collision, four in `screen_pixel`, and seven
in text drawing. Inventory counts are candidates, not certified single-use
fusion eligibility.

For this particular clear loop, `draw_board` runs four times in the RAM
workload, giving **4 x 76,801 comparison visits**. This is analytically known
from the real bounded clear loop, not measured retired-instruction sampling.
A legal direct compare/branch path could remove seven instructions per visit
in this shown sequence, or **2,150,428 instruction visits** across four clears,
without changing the pixel count. That is a testable mechanism hypothesis,
not a predicted tick saving or a certified optimizer result.

### Homes, address generation, aggregates and calls

- Global symbol materialization in [cg_symbol](../src/codegen_thumb.c) emits a
  literal load, branch-over-pool and pool word. Generation contains 82 such
  skip branches. `getelementptr` uses existing constant/shifted address selection,
  but symbol reloads and operand copies remain. D06's blanket alternatives
  already regressed; size alone does not justify promoting another one.
- Narrow operations use `ubfx`, `sxtb`/`sxth` and i1 normalization. Remove only
  proven redundant operations; C integer promotion and ABI extension are real
  requirements. Pointer/address and wide arithmetic legality differs.
- Generation's real 16-byte zeroing maps to a word loop, while a four-byte
  under-aligned object maps to a byte loop. Both execute freestanding expansion.
  Smaller aligned forms may be candidates, but overlap/alignment/volatile
  semantics must be proven; they are not the first selected change.
- `lcd_write_byte` has four required volatile IR operations, zero direct frame
  load/stores and zero allocator spills, yet 46 instructions against LLVM's 13.
  Its frame reservation/32-byte callee-save sequence, operand copies, constants
  and helper calls are costs independent of spilling. A full panel transfer
  reaches this function twice per pixel through `badger_display_write`.
  Future leaf/call economics must preserve all GPIO operations and ABI rules.

## Ranked opportunities and bounded first change

Ranking combines **reached workload ticks, known iteration counts and the
explainable mechanism**. It is not a per-function CPU sampling profile or a
ranking by the largest object. The transport/compiler harness can be large
statically without contributing its validation work to the timed interval.

1. **PO03 first: adjacent single-use i32 `icmp` plus conditional `br`.**
   Broadly reached in generation, RAM loops, collision and frames. Eliminates
   demonstrated canonicalization/copies/second compare without changing C
   frontend semantics or allocating more long-lived registers.
2. **PO04: scalar-home/PHI transport.** Significant in generation/collision,
   but requires precise liveness, edge-parallel-copy and scratch proofs.
   Do not substitute a blanket register-pressure heuristic.
3. **PO07: leaf/call/prologue and operand-copy economics.** High-frequency
   display path, including no-spill functions. Preserve callback/AAPCS/volatile
   semantics and independently measure the shared mechanism.
4. **Separate selection investigations:** global-address pools, narrow
   normalization and aggregate memory. Important static examples exist, but
   prior regressions and legality differ; reject them as blanket first changes.

### First-change legality and negative cases

The initial implementation should accept only a same-block, adjacent supported
i32 comparison whose i1 result has **exactly one semantic use**, the following
conditional branch. Count uses over the function, including PHI-edge operands.
Preserve signed/unsigned/equality predicates and existing successor/PHI
parallel-copy logic. Emit a local conditional veneer so distant successors
remain handled by the normal branch machinery. Treat APSR as live until that
local conditional is consumed; never put flag-clobbering edge copies before it.
Keep operand scratch/clobber rules and inherited frame metadata consistent.

Reject or retain the old path for multiple uses, nonadjacent consumers,
cross-block conditions, wide/narrow comparisons, unsupported predicates,
inline assembly, scheduling interactions, malformed input and conditions used
as actual stored/call/returned booleans. Volatile loads feeding the operands
must execute exactly once in their original order; no load folding/reordering
is part of this change. This is a semantic rule, not a source-name/seed rule.

Use the existing per-instruction selection tables and register-work budget:
one bounded function use-count scan plus linear adjacent-pair checks, charging
each step to existing work limits. No unbounded CFG path exploration, extra
global fixed-point loop or heuristic new compiler configuration is required.
Keep the baseline selectable and test budget exhaustion explicitly.

The measurable experiment must verify:

- The shown sequence loses the intended boolean normalization/copies/second
  compare; no compensating spills, extra PHI copies or accesses appear.
- Negative cases and every predicate behave identically under both target
  semantic routes, with bidirectional ABI, stack/frame and full-corpus gates.
- Same two seeds, both physical roles, raw clocks/nonces/guards and preservation
  qualify ordinary application timings. Static elimination does not promote a
  speed default; use PO09's independent repeat/role gates.

## Limits and flash restoration

No on-target instruction-retirement/per-function profiler is claimed.
Reported ticks are observed workload timings; instruction counts are static;
known loop visit counts are analytical. Setup/checksums/watchdog/USB service
remain outside intervals. Frames omit physical I2C acquisition and live pacing.
Visual/electrical panel output, physical motion and controlled cold-XIP remain
unqualified. The benchmark uses its existing isolated 64 KiB stack linker,
not a whole-game production stack certification.

Fresh complete 16 MiB originals were restored and independently reread:

| Badge | Verified original/restored SHA-256 |
| --- | --- |
| `9BBF88DD17D66E7D` | `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff` |
| `08DA26F86E57F5EA` | `8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d` |

Both remain in BOOTSEL. No compiler default or benchmark-specific lowering
was added; implementing the selected optimization belongs to PO03.

## PO03 follow-up

[PO03 implements and qualifies the selected compare/branch optimization](compare-branch.md)
as the independent opt-in `thumb-branch` selector. Replaying this report's
exact IR preserves all 11 baseline object hashes and removes the predicted
seven emitted instructions per selected pair. Its ordinary-Clang hardware
comparison is a new experiment, not a replacement for this fixed-IR report.
The original attribution evidence and capture-time provenance remain intact.
