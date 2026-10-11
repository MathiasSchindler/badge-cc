# D06: final-width branch and address layout

Status: implemented as **opt-in `thumb-relax`**, with offline and repeated
suite qualification. Defaults remain `direct/global-compact/thumb-layout/word`.
The original D06 milestone was offline-only. D09's subsequent
[independent campaigns](qualification.md#completed-outcomes) find shared-code
wins but resolved cross-file ABI, branch-PHI and signed-div/rem regressions.
Speed promotion is rejected; smaller text alone is not a speed win.

## Selection contract

Select the candidate with `badge-codegen --scalar-selection=thumb-relax` or
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-relax` in the owned firmware drivers.
Use a fresh build directory or force rebuilding when changing policies.
The callable backend exposes the same policy as `BADGE_SELECTION_THUMB_RELAX`.

The candidate inherits baseline allocated-register selection and the guarded
Z05 CFG layout, **not** D04 compact selection or D05 scheduling. It makes two
general changes:

- Materialize symbol addresses with relocatable MOVW/MOVT pairs when the
  symbolic addend fits signed 16 bits.
- Let the assembler choose legal narrow/wide encodings for structured CFG
  edges, returns and switch veneers instead of always requesting `.w`.

The structured emitter retains parallel PHI transfers, edge ordering and
Z05 eligibility checks. It does not rewrite emitted assembly strings,
reorder instructions, introduce scratch registers, change stack homes, or
alter arithmetic/flag selection. Other internal helper branches and guarded
literal fallbacks retain their previous explicit widths.

## Address materialization and the ABI

An eligible address is emitted as:

```asm
movw rN, #:lower16:(symbol+addend)
movt rN, #:upper16:(symbol+addend)
```

Both instructions preserve flags. The owned assembler/object reader/linker
support `R_ARM_THM_MOVW_ABS_NC` (47) and `R_ARM_THM_MOVT_ABS` (48).
Relocation sites are four-byte Thumb instructions at halfword-aligned
positions in executable sections, including positions that are 2 modulo 4.
Modifier/opcode mismatches, malformed opcodes/reserved bits and SP/PC
destinations are diagnosed.

For **REL**, both instructions encode the same signed 16-bit addend,
including MOVT; they do not independently encode its low/high halves.
Linking computes the complete ELF symbol value plus that addend, then
inserts the corresponding half. This preserves ordinary odd data addresses,
Thumb-tagged function values, carries, absolute symbols and unresolved weak
values. Removing and subsequently reapplying the function tag is incorrect
for odd function-address addends. Independent LLVM/LLD comparison caught
that error during development; the final implementation and regression
matrix use full symbol-value arithmetic.

Symbolic addends outside -32768..32767 retain the existing inline literal
island: load, guard branch, four-byte alignment, word and continuation.
Absolute modifier expressions can represent signed/unsigned 32-bit values.
No shared pool, extra register copy or spill is introduced.

The ABI reference is pinned public
[AAELF32](https://github.com/ARM-software/abi-aa/blob/bd017eeca341c06915b9a02ae111b480416d248f/aaelf32/aaelf32.rst),
commit `bd017eeca341c06915b9a02ae111b480416d248f`, SHA-256
`45b1c691a16eb0c4186562e5c49f05f6767bddcd865fa14ea6a5cc6839e6c170`.
Relevant sections are Addends and PC-bias compensation, the relocation-code
table and Thumb relocation actions by instruction type. This authored
analysis does not redistribute the source document.

## Bounded relaxation and embedded data

Instruction widths widen monotonically. Convergence also tracks each
record's before/after section and byte offset: stable opcode widths alone
do not prove that directive padding or labels are stable. Final encoding
rejects another layout change instead of publishing inconsistent bytes.
The algorithm is bounded and safe, not a globally minimum-size solver.

The production budget is 128 passes, 16 MiB of source, 500,000 parsed
records and 192 MiB of tracked allocation. Existing object/section limits
still apply. Explicit `.n` requests now diagnose an instruction that would
require four bytes rather than silently widening it. CLI failures preserve
existing output; the two-pass-budget regression verifies this behavior.

Literal addressing uses `Align(instruction_address+4,4)`. T1 requires a
low destination and a forward 0..1020 offset scaled by four; T2 permits
positive/negative offsets through 4095. Alignment, reach and branch widths
must be solved together.

Fallback words are branch-guarded and marked as data with `$d` mapping
symbols. They are not reachable as ordinary instructions; this is **not**
a claim of per-word hardware execute-never protection inside executable
text. The real candidate corpus needs no fallback words, but boundary
tests retain and verify the fallback path.

## Qualification

Evidence: [final results](../../build/datasheet/d06-results.json).
Initial pre-convergence evidence is retained separately as
[initial results](../../build/datasheet/d06-initial-results.json).

- 572 generated register/symbol/addend cases agree byte-for-byte with
  LLVM/LLD, including signed bounds, carry, odd data/function offsets,
  weak and absolute symbols. LLVM-produced objects also link identically
  with the owned linker.
- 524,288 native signed-addend/carry/register fixup cases pass, along with
  malformed-opcode/reserved-bit/SP/PC negatives.
- Exact forward/backward conditional and unconditional branch reach,
  widening cascades, alignment-induced changes, T1/T2 literal reach,
  illegal `.n` requests and bounded-failure output preservation pass.
- A directive-only regression changes alignment while instruction widths
  remain unchanged; section/position convergence detects the change.
- Determinism across all five residency policies, odd-offset addresses,
  large-offset guarded fallbacks and duplicate-option diagnostics pass.
- Native assembler/object/linker sanitizer tests and compact, scheduling
  and target-model regressions pass. Release tools build with
  `-Wall -Wextra -Werror`.
- Owned and LLVM IR execution, mixed ABI, PHIs, pressure, flags and wide
  arithmetic pass. The actual maze's owned/LLVM correctness oracle passes
  again after the final convergence hardening.
- Two initial parallel driver tests timed out while other target validation
  ran. The retained serial retry passes all 22 tests plus the owned pipeline;
  no production fallback or timeout inflation was added.

Across 45 archived modules, default C07 assembly is byte-identical and
default assembler objects match the archived pre-change assembler exactly.
Candidate release/sanitizer assembly, allocation/layout annotations and
parsed call/frame graphs agree. There are 2,693 materialized addresses,
zero fallback sites and 14,419 requests for relaxable structured branches;
requests are not counts of final narrow instructions. No additional
register-copy instructions or spills are introduced.

Fresh builds cover 37 C modules and 12 linked images. Normalized control
and candidate IR match across all modules; normalization is restricted to
source filenames and three known Wi-Fi protocol build-ID operands.
Final ELF audits decode instruction widths using mapping symbols and
validate direct branch/call targets against instruction boundaries.
They exclude linker trap-filled inter-function gaps and explicitly permit
the backend's intentional UDF trap target.

## Final linked sizes

Text bytes from fresh control/candidate images:

| Image | Control | `thumb-relax` | Change |
| --- | ---: | ---: | ---: |
| button_mapper | 15,640 | 14,152 | -9.51% |
| diagnostics | 66,264 | 59,376 | -10.39% |
| gpio_events | 12,712 | 11,508 | -9.47% |
| gpio_monitor | 9,948 | 9,056 | -8.97% |
| hello_idle | 584 | 496 | -15.07% |
| imu_demo | 33,004 | 30,544 | -7.45% |
| lower_mapper | 16,008 | 14,492 | -9.47% |
| peripheral_probe | 19,060 | 17,068 | -10.45% |
| screen_test | 6,708 | 6,116 | -8.83% |
| tilt_breakout | 26,404 | 23,228 | -12.03% |
| tilt_maze | 41,508 | 36,756 | -11.45% |
| usb_wifi_udp | 395,124 | 379,636 | -3.92% |

Data and BSS are unchanged in every comparison. The separately instrumented
maze decreases from 43,720 to 38,712 text bytes (-11.4547%), with data 16 and
BSS 160,448 bytes unchanged. Source, tool, ELF/UF2 and test-log hashes are
recorded in the results.

In the ordinary maze, the control has 440 literal words: 1,760 data bytes,
1,760 guard-branch bytes and 414 alignment-padding bytes. All three are
zero in the candidate. Address pairs themselves occupy eight bytes per
site, so removed pool overhead is not the net text saving. Final B/Bcc
counts change from 1,752 wide to 1,281 narrow plus 31 wide; 1,589 direct
branch/call instruction boundaries are validated in the candidate.
Whole-text measurements include the resulting function-placement padding.

## Alignment decision: no blanket policy

The instrumented candidate has 1,316 narrow and 31 wide B/Bcc instructions,
and 1,644 validated direct branch/call boundaries. Its static geometry has:

- 274 unaligned wide direct-target edges;
- 10 distinct unaligned wide backward targets, needing at least 20 bytes
  of naive alignment padding;
- 59 unaligned wide return continuations;
- 1,143 branch shapes with a 16-bit predecessor.

These counts are **not** dynamic `U`, `F`, `K`, folding events or proven hot
loops. See the [conditional branch predicates](branches-and-layout.md).
Padding can change branch reach, widths, fetch behavior and return
continuations. Without profile-backed target selection and coupled timing
qualification, blanket alignment is a **no-go**. Existing function
alignment is unchanged; selective hot-target alignment remains deferred.

## Shared-pool decision: defer placement

Across the archived modules, 2,693 load sites refer to 1,346 per-function
unique pool expressions. An optimistic shared-T2 bound is
`4*load_sites + 4*function_unique_expressions = 16,156 bytes`, versus
`8*load_sites = 21,544 bytes` for literal-free address pairs.

The apparent 5,388-byte opportunity omits guards, padding, splitting for
reach, alias equivalence and fetch/load/dependency costs. It is not an
implemented pool layout or a predicted final-text reduction. Sharing
requires a structured placement solution and final-byte reach/guard
validation, followed by the [memory-system](memory-system.md) and hardware
timing gates. New shared pools are **not implemented or enabled**.
Literal-free materialization is the qualified candidate; pool placement
remains an explicit future tradeoff rather than a hidden default.

## Host cost and promotion gate

Five alternating 45-input CLI batches include process startup and I/O:

| Configuration | Median batch wall time | Median maximum process RSS |
| --- | ---: | ---: |
| Current C07 control | 339.18 ms | 5,898,240 bytes |
| Current C07 candidate | 343.06 ms | 5,832,704 bytes |
| Archived assembler, control | 353.89 ms | 7,192,576 bytes |
| Current assembler, control | 352.84 ms | 7,454,720 bytes |
| Current assembler, candidate | 366.26 ms | 6,537,216 bytes |

Ranges overlap, and individual process CPU readings round to zero at the
host's 10 ms resolution. These are bounded CLI observations, not an
in-process throughput win or reliable CPU regression estimate. Peak RSS
is the maximum individual process, not a sum across the batch.

Deleting literal loads/guards and shrinking branches changes fetch,
dependencies and folding opportunities. Static savings do not establish
whole-program runtime improvement. D06 did not retry the then-unresolved
D05 collector failure blindly. D09 subsequently fixes cleanup and completes
role-swapped hardware correctness/timing with verified restoration; resolved
suite regressions reject speed promotion. Any composition with D04/D05
still requires fresh independent qualification.
