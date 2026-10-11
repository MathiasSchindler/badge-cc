# D04: compact selection without allocation changes

D04 implements and qualifies the **selection-only** candidate
`thumb-compact`. It is opt-in and size-oriented: repeated hardware
measurements show both gains and a signed-div/rem regression. The qualified
`thumb-layout` speed default remains unchanged. Allocation economics are
deferred, not silently folded into this candidate.

D09's [fresh independent qualification](qualification.md#completed-outcomes)
confirms suite improvements but a resolved signed-div/rem regression.
Speed promotion remains rejected; the historical D04 measurements below
are retained as separate evidence.

## Contract and integration

The new scalar policy inherits allocated-register operands and guarded branch
layout. It is supported consistently by the C API, CLI, owned pipeline/build
driver, benchmark validation and hardware comparison tool:

```sh
badge-compiler/build/badge-codegen --scalar-selection=thumb-compact \
  INPUT.ll -o OUTPUT.s
BADGE_CODEGEN_SCALAR_SELECTION=thumb-compact \
  bash badge-compiler/scripts/build-owned.sh
make -C badge-compiler test-compact
```

Use a fresh directory or force rebuilding when changing policy. No low-register
preferences, moves, spills, saved registers, PHI reservations, scratch masks,
live ranges or home allocation are introduced. Frame/call graphs and all
allocation/selection/layout annotations agree with the control on the 45
qualified modules. The selector changes encodings, not SSA transformations.

Implementation is in [structured scalar selection](../../src/codegen_select.c)
and [Thumb helpers](../../src/codegen_thumb.c). It does not parse/rewrite emitted
assembly or recognize workload names.

### Legal forms and flag boundaries

- Low-register ADD/SUB: imm3, destructive imm8 and three-register forms.
  ADD immediate width eligibility reuses the
  [D03 helper](../../src/target_cost.c); subtraction has the corresponding
  operand restrictions. Negative constants select the inverse operation only
  when the unsigned magnitude is eligible.
- Destructive register ADD: flag-preserving narrow ADD, including ordinary
  high-register operands, excluding SP/PC.
- Low-register destructive AND/OR/XOR/MUL and register shifts. Commutative
  operations may exchange sources when the destination already equals the
  other source; they do not add a preparatory move.
- Low-register immediate shifts of 1..31; zero retains the existing
  move/no-op handling. Other operand combinations retain wide lowering.
- Frame-relative aligned word loads/stores with low value registers and
  offsets 0..124, and equivalent eligible ordinary-memory forms.
  Byte fallback accesses narrow only within their immediate reach.
- Low-register byte/halfword normalization uses UXTB/UXTH instead of UBFX.
  These substitutions preserve APSR.

Flag-setting arithmetic forms are selected **only at explicit outside-IT
scalar boundaries with dead APSR**. The backend materializes IR comparisons
as values and emits each compare/IT or carry-producing/consuming window
contiguously. The compact helper is not called inside these windows.
Wide ADD/SUB carry chains, comparison/IT materialization, constants and
conditional moves keep their existing flag semantics. Memory and extension
substitutions do not change flags and are safe even with live APSR.

The existing assembler's Thumb legality checks provide prior art for the
operand restrictions. Source references are the D01
[ADD and instruction-semantics facts](instruction-semantics.md), the pinned
Armv8-M reference edition, and
[flags/IT guidance](registers-and-abi.md). Exhaustive independent LLVM encoding
comparisons supplement, rather than replace, that legality evidence.
Ordinary loads/stores keep their access width, alignment requirements and
ordering, including volatile accesses. Literal pools, instruction scheduling,
new branch alignment and allocator economics remain separate milestones.

## Correctness and preservation gates

- 24,826 native sanitizer register/boundary cases: all R0..R13 combinations
  across nine register operations, frame reach boundaries and normalization.
  Invalid SP combinations are rejected. High-register and non-destructive
  ineligible cases retain wide fallback.
- Owned and LLVM assemblers produce identical text bytes; LLVM symbol sizes
  and disassembly verify explicit narrow/wide boundaries.
- Immediate boundary tests cover zero, imm3/imm8/imm12 transitions, negative
  constants and shifts, across all five residency policies.
- Release/sanitizer output agrees for all 45 archived real modules.
  Implicit defaults remain byte-identical to qualified archived assembly.
- Fresh control/candidate corpus: 37 modules and 12 linked images. Optimized
  IR agrees after removing only source filenames and exactly three known
  USB protocol build-ID operands in the UDP image.
- Both owned-IR and LLVM-IR execution routes pass, including wide arithmetic,
  comparisons/IT, raw PHIs, scratch/register pressure, memory tails/guards,
  volatile/call barriers and bidirectional mixed AAPCS calls.
- The actual maze correctness oracle passes calibration, fingerprint,
  rendering, hazards and progression under owned and LLVM compilation.

Portable driver tests verify policy forwarding, provenance, duplicate/invalid
diagnostics and timing-report compatibility. The normal test target includes
the compact boundary suite. Oracle mode uses the project's existing LLVM
tools; owned mode uses only the owned assembler/linker/ELF inspector. The
owned suite also passed with all LLVM command paths deliberately nonexistent.
No new dependency or downloaded package is required.

## Linked bytes

Fresh linked text measurements (including image padding/metadata):

| Image | Control bytes | Compact bytes | Change |
| --- | ---: | ---: | ---: |
| button_mapper | 15,640 | 14,668 | -6.21% |
| diagnostics | 66,264 | 61,944 | -6.52% |
| gpio_events | 12,712 | 12,012 | -5.51% |
| gpio_monitor | 9,948 | 9,320 | -6.31% |
| hello_idle | 584 | 556 | -4.79% |
| imu_demo | 33,004 | 30,788 | -6.71% |
| lower_mapper | 16,008 | 15,024 | -6.15% |
| peripheral_probe | 19,060 | 17,956 | -5.79% |
| screen_test | 6,708 | 6,304 | -6.02% |
| tilt_breakout | 26,404 | 24,324 | -7.88% |
| tilt_maze | 41,508 | 38,300 | -7.73% |
| usb_wifi_udp | 395,124 | 379,536 | -3.95% |

Data/BSS sizes are unchanged in every comparison. The separately instrumented
maze uses the actual `TILT_MAZE_COMPARE` macro: linked text drops from 43,720
to 40,364 bytes (-7.68%), with data 16 and BSS 160,448 bytes unchanged.
Its [candidate UF2](../../../build/d04-compact-maze/universe26/uf2/tilt_maze.uf2)
is not a promoted default or the application currently left on either badge.

## Repeated hardware qualification

The existing P00 collector ran four fresh campaigns with seed 3276377845:
control, candidate, repeated control, repeated candidate. Each campaign swaps
physical compiler roles, validates all fingerprints/work/guards, records clocks
and timer overhead, and retains seven raw samples per workload/compiler/round.
The eight rounds contain 1,344 workload timings, plus overhead measurements.
Comparisons use the existing role-paired median/geometric-mean method; LLVM
drift remains separately visible. Controls and raw ranges, not a percentage
threshold alone, determine whether an effect is resolved.

| Workload | First candidate change | Repeat |
| --- | ---: | ---: |
| arithmetic | +0.001% | 0.000% |
| strings | -0.912% | -0.912% |
| memory | -0.793% | -0.814% |
| control-flow | -2.552% | -2.548% |
| cross-file-abi | -0.351% | -0.351% |
| graphics | -0.143% | -0.135% |
| kernel-arithmetic | -3.845% | -3.845% |
| kernel-branch-phi | approximately 0% | +0.049% |
| kernel-cross-file-call | -1.763% | -1.815% |
| kernel-unsigned-divrem | -1.380% | -1.388% |
| kernel-signed-divrem | **+1.826%** | **+1.892%** |
| kernel-ordinary-memory | 0.000% | 0.000% |

The signed-div/rem regression is real enough to reject speed promotion:
first-role control samples 4,597..4,607 versus candidate 4,683..4,694 ticks,
with the same effect on the other badge. Repeated control drift there is
only -0.0435%. Correctness/work fingerprints still agree. No fabricated cycle
correction, workload-specific exception or speculative allocator adjustment
is introduced. Encoding/fetch/branch-layout interactions require a separate
mechanism experiment before attributing the cause; D03's model is not a summed
runtime oracle. Small near-zero changes are not universal speed claims.

These are compiler-corpus timings, not new measurements of the actual maze's
physics or full RAM drawing. The previous maze runtime numbers remain historical
Z05 evidence; smaller maze text alone does not establish a maze speedup.

Both badges' original complete 16 MiB flash images were backed up before the
campaign and restored afterward, with independent full-flash hash verification.
They remain in BOOTSEL. Raw flash snapshots stay local and are not publication
artifacts.

## Decision and follow-up

### Host cost and work bounds

Five alternating 45-input CLI batches measured median wall totals of 374.6 ms
for the control and 375.0 ms for compact selection. The ranges overlap widely
(313.8..521.3 ms and 346.9..386.6 ms); no resolved wall-time regression is
claimed. Per-process CPU times rounded to zero at the tool's 10 ms resolution,
which means **unresolved**, not free compilation. Median maximum individual
process RSS was about 5.88 MB versus 5.87 MB.

These measurements include process startup and input/output I/O and are not
Z06-style in-process API measurements. The candidate adds only constant
register/opcode checks per emission. Its ADD cost query is restricted to
magnitudes <=255, so it introduces no modified-immediate search or new
allocation/liveness analysis. Existing analysis limits remain in force.

The selection-only candidate is useful when its size tradeoff is explicitly
chosen. It is **not speed-default-qualified**. No allocator change is justified
by static low-register counts alone, particularly with a repeated runtime
regression already present; forcing more low registers could add moves/spills
or change scratch/PHI/frame constraints. That remains a separate candidate if
future effect/layout measurements justify it.

Local machine-readable evidence:
[D04 results](../../build/datasheet/d04-results.json) and
[hardware results](../../build/datasheet/d04-hardware-results.json).
The report retains source/input/output hashes, linked sizes, preservation
checks, hardware provenance and restoration status. D05/D06 may investigate
dependency/layout effects without fitting this kernel or changing the default.
