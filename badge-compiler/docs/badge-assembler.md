# RP2350 assembler roadmap

## Goal and boundaries

Make the owned assembler a reliable, inspectable tool for the RP2350's **two
Cortex-M33 cores**: precise Thumb encodings, useful bare-metal instruction
coverage and evidence that explains how assembly becomes firmware.

This plan targets the Arm cores, not the RP2350's alternative RISC-V cores.
PIO programs and CYW43439 controller firmware use different instruction sets
and are not Cortex assembler extensions. Support one explicit RP2350 target
profile rather than a general catalogue of Arm CPUs.

Instruction encoding is not compiler optimization. Register allocation,
inlining and scheduling remain compiler responsibilities. The assembler must
preserve flags, memory-access widths, MMIO ordering, explicit NOPs and delay
sequences. No hidden peephole optimization or automatic delay shortening.
An assembler feature is not qualified until its object and linker consumers
also understand the resulting records.

## Baseline and current fix

The identical-input investigation used nine C translation units from red
square and maze, with assembly produced by each compiler: 18 inputs.

- Sixteen had identical allocated-section bytes and alignment.
- One diagnostic input used equivalent but different relocations: LLVM
  referenced section symbols plus addends, while owned referenced local
  symbols. The affected function and strings linked to identical bytes.
- LLVM's maze assembly was rejected at `subw sp, sp, #1044`.
- Common-LLD controls produced identical loaded images for owned red square
  (624 bytes), owned maze (19,600 bytes), and LLVM red square (568 bytes).
- The existing three-fixture assembler oracle passed.

Object-file size includes symbol tables and metadata, not just badge bytes.
Raw relocatable bytes must not be compared as if they were final addresses.
These results are scoped observations, not proof that all encodings match.

### BA00 — valid stack-pointer immediate arithmetic

**Implemented and host-qualified.**

- [x] Permit SP as the destination of ADDW/SUBW only when the base is SP.
- [x] Retain SP/PC destination rejection for MOVW/MOVT and PC-base rejection
  for these plain-immediate ADDW/SUBW forms; ADR remains a separate encoding.
- [x] Add zero/max-imm12, ordinary destination/SP base, exact 1044-byte
  adjustment, invalid-register and out-of-range regression cases.
- [x] Extend the common-linker LLVM oracle with legal SP forms.
- [x] Assemble the complete unchanged LLVM maze assembly successfully.
- [x] Link all LLVM-generated C modules through both assemblers with common
  startup objects, linker script and LLD; require identical loaded bytes.
- [x] Run sanitized native assembler tests and the existing oracle.

Qualification assembled all nine unchanged LLVM C-module assembly inputs.
Common-LLD red-square and maze controls were byte-identical at 568 and 8,704
loaded bytes respectively. The sanitized suite passed 36 positive cases,
87 negative cases, 12 explicitly unsupported inputs and 24 mutations; all
three existing oracle fixtures passed, including the new SP encodings.
Local capture: [BA00 results](../build/assembler-ba00/results.json).
No badge flashing or hardware timing claim is involved.

## Milestones

Each milestone is independently reviewable. Do not mark a feature complete
merely because a mnemonic is accepted.

### BA01 — maintained identical-assembly crossover

**Implemented and host-qualified.** Turn the current investigation into reusable controls.

- [x] Add a maintained test/capture route that assembles the exact same input
  with owned and LLVM assemblers in both compiler-generated directions.
- [x] Bind source assembly, tool versions, target options and outputs to hashes.
- [x] Compare section flags/alignment, decoded instruction bytes, literals,
  symbols and relocations; normalize equivalent relocations by target section
  and effective addend without hiding unresolved differences.
- [x] Compare linked binaries under a common linker, then test relevant
  controls under the owned linker to expose consumer incompatibilities.
- [x] Separate allocated content, padding, relocation fields, nonloaded ELF
  metadata and final loaded-image extent.
- [x] Integrate results into Compile Graph without confusing assembler and
  frontend/backend changes.
- [x] Cover branch/literal range boundaries, Thumb symbol bits, IT state,
  output preservation on failure and layout convergence limits.

**Exit:** both demo campaigns reconcile completely, with explicit unsupported
inputs and no success-shaped fallback or unexplained loaded-byte differences.

### BA01 implementation and qualification

[assembler-crossover.mjs](../tools/assembler-crossover.mjs) is invoked by
[Compile Graph capture](../../compile-graph/capture.mjs). It reads ELF32
sections, symbols and REL records directly; normalizes ABS32, REL32, PREL31,
Thumb call/jump and MOVW/MOVT target/addend fields; and rejects unknown
normalization types explicitly. Non-relocation allocated bytes, section
layout and public symbols must match. Disassembly is retained independently.
Local symbol differences remain visible in the evidence and UI.

Mapping symbols distinguish Thumb-code ranges from data/literals, with
unmapped bytes explicit. Code ranges include alignment NOPs: exact per-line
padding attribution remains BA02, not an inferred claim here. Linked section
bytes, extent gaps, static RAM and nonallocated ELF metadata are separate.

The two-demo campaign covers **18 same-input pairs**, **four producer/image
combinations**, **two linkers**, and **16 linked binaries**. Every assembler
pair is byte-identical under each common linker:

| Assembly producer | Image | LLD pair | Owned-linker pair |
| --- | --- | ---: | ---: |
| Owned | Red square | 624 bytes | 624 bytes |
| Owned | Maze | 19,600 bytes | 19,600 bytes |
| LLVM | Red square | 568 bytes | 568 bytes |
| LLVM | Maze | 8,704 bytes | 8,704 bytes |

Two LLVM object inputs (diagnostic hardware and maze) differ at unresolved
relocation fields but normalize identically. LLVM-generated I2C and maze
local aliases have STT_OBJECT in LLVM objects and STT_NOTYPE in owned
objects; these metadata differences are retained, not erased.

LLD versus owned-linker bytes differ for the LLVM maze despite equal loaded
size. This is a separately reported linker observation; BA01 does not infer
a cause or claim cross-linker equivalence. Both assemblers produce identical
bytes under each of those linkers. No new hardware behavior claim is made.

The sanitized assembler suite and three existing oracle fixtures pass.
Targeted final-layout checks cover 572 signed MOVW/MOVT cases, branch
thresholds/widening cascades, literal PC alignment/reach and the two-pass
layout-budget failure with preserved output. New tests cover normalization,
symbol contracts, unsupported types, malformed ELF and evidence tampering.

Generate a new immutable report from the project root:

```sh
mkdir -p compile-graph/build
BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants \
  node compile-graph/capture.mjs --capture compile-graph/build/ba01
node compile-graph/capture.mjs --verify compile-graph/build/ba01
COMPILE_GRAPH_REPORT="$PWD/compile-graph/build/ba01" \
  make -C badge-compiler test-assembler-crossover
```

Set `LLVM_BIN` or individual LLVM tool variables, including `LLVM_LLD`,
as needed. Unsupported assembler input or normalization fails the capture
with diagnostics and logs, never a complete partial report. Verification
recomputes the crossover from archived objects and binaries without tools or
the original checkout. Local qualification evidence:
[BA01 report](../../compile-graph/build/ba01-production/report.json).

### BA02 — listings and source-address evidence

**Priority: high.** Improve insight before expanding instruction coverage.

- [x] Emit an optional structured listing containing input line, section,
  offset, selected width, exact bytes, alignment fill and relocation records.
- [x] Explain automatic branch widening and literal-range decisions.
- [x] Preserve exact input provenance and distinguish instructions from data.
- [x] Expose listings in Compile Graph with address/byte/region navigation.
- [x] Introduce a bounded source-location mapping format; then evaluate
  numbered `.file`/`.loc` and a minimal DWARF line table for interoperability.
- [x] Wire compiler emission and object/linker retention so mappings survive
  optimization and final placement; mark missing/ambiguous mappings explicitly.
- [x] Verify that enabling listings/debug information does not change loaded
  firmware bytes, layout or compiler admission.

**Exit:** every emitted byte is accounted for and mappings are independently
checked. Source-line proximity must not be presented as semantic equivalence.

**Status: complete with a deliberately bounded mapping contract.** Listings
are sidecars, not changes to executable sections or compiler policy.

```sh
badge-compiler/build/badge-asm --listing output.listing.json input.s -o output.o
# For assembly produced by the existing attribution build:
badge-compiler/build/badge-asm --listing trace.listing.json \
  --ir-source optimized.ll trace.s -o trace.o
```

Schema 1 includes the original input bytes as JSON byte-codepoints (decode
`source` as Latin-1), physical assembly lines, contiguous emission ranges,
instruction widths, exact final serialized section bytes, NOBITS reservations,
relocations and generated attributes/address-significance metadata. ELF headers,
symbol/string tables, relocation containers and file padding are container
overhead, not additional assembler emission ranges. Every emitted section byte
is independently checked against the object, including serialized addrsig index
remapping. Branch/literal decisions include final PC base, signed displacement,
selected reach and explicit-width requests; alignment decisions include
zero-byte fills and maximum-skip rejections. The existing 16 MiB input,
192 MiB assembly allocation and 64 MiB listing limits fail explicitly.

The bounded IR mapping uses `@ attribution-ir N` comments, with positive
physical lines in the supplied IR file; malformed/out-of-range markers fail.
Only instruction ranges receive the current emission context. Compile Graph
archives the actual optimized IR and existing attribution-build assembly.
**That trace currently inhibits some newer peepholes:** it is not substituted
into production firmware. A trace section is admitted for production address
mapping only when its bytes, layout, canonical relocations and public symbols
match the production object. Differing sections receive
`trace-differs-from-production`, null addresses and no production IR origin.
Markers describe producer emission context, not exact semantic ownership after
folding; unmarked prologues, transformations and generated data remain unmapped.

Both linker maps join object section offsets to VMA/LMA. Missing or ambiguous
contributions stay unmapped, and SHF_MERGE sections are excluded because their
input offsets may be non-linear. Verification checks mapped nonrelocation bits
against the actual linked binary and checks VMA/LMA against ELF load segments.
The HTML listing follows the selected function, offers section/line/address
search, assembly-line jumps, IR-context jumps, bounded tables and raw downloads.
Archived reports verify without the original checkout.

**DWARF evaluation:** production LLVM assembly currently supplies no numbered
`.file`/`.loc` locations; owned optimized IR does not preserve precise C debug
locations. Emitting a DWARF line table from these artifacts would invent
correspondence. Numbered `.file`/`.loc` and DWARF generation therefore remain
unsupported, rather than silently ignored. A future interoperability extension
needs frontend/optimizer debug-location retention and independent debug/loaded
byte controls. BA02's checked format is exact assembly provenance plus gated IR
emission context; it does not claim C-line coverage.

Listing generation/assembly errors preserve existing outputs. Each output is
written atomically using the shared host writer; the listing is published first,
so an object I/O failure can leave a new sidecar beside the previous object.
Treat object/listing pairs as valid only after successful exit and verification.
Input/object/IR/incbin aliases are rejected.

Qualification: sanitized assembler cases, LLVM oracle fixtures, final branch/
literal/MOVW-MOVT layout tests, listing tamper/alias/budget tests and report tests
pass. All 18 demo inputs produce byte-identical objects with/without listings;
all 16 crossover firmware binaries retain BA01's results. Owned/LLVM loaded
sizes remain red square **624/568 B**, tilt maze **19,600/8,704 B**.

```sh
LLVM_BIN=/opt/homebrew/opt/llvm/bin LLVM_LLD=/opt/homebrew/bin/ld.lld \
  BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants \
  node compile-graph/capture.mjs --capture compile-graph/build/ba02
COMPILE_GRAPH_REPORT="$PWD/compile-graph/build/ba02" \
  make -C badge-compiler test-assembler-crossover
```

### BA03 — system registers, exceptions and interrupt control

**Priority: high.** Enable precise bare-metal operations on either core.

- [ ] Specify RP2350 security/privilege applicability using pinned Arm/RP2350
  documentation; support named MRS/MSR registers needed by actual firmware.
- [ ] Add CPSID/CPSIE and relevant exception/debug instructions such as
  SVC/BKPT, with immediate and operand validation.
- [ ] Test PRIMASK/BASEPRI/FAULTMASK/CONTROL and stack-register forms separately;
  reject reserved or unsupported combinations instead of treating names as
  arbitrary encodings.
- [ ] Validate encodings against LLVM and independent decoded expectations.
- [ ] Add runtime tests that restore state and retain USB recovery.
- [ ] Document that interrupt masking and system registers are per-core:
  disabling interrupts on one core does not lock the other core.
- [ ] Preserve existing DMB/DSB/ISB behavior; test accepted barrier operands
  and restrictions rather than adding mnemonic-only acceptance.

**Exit:** supported operations have exact operand contracts, ABI/clobber
guidance and bounded physical checks for privilege-sensitive behavior.

### BA04 — assembly ergonomics and compatibility

**Priority: medium.** Focus on actual RP2350 assembly inputs.

- [ ] Add numeric local labels and forward/backward references, including
  repeated labels and diagnostics for unresolved references.
- [ ] Support the target-appropriate `.arch` spelling without enabling
  unrelated architectures or silently changing the target profile.
- [ ] Add LDR `=constant`/`=symbol` pseudo-operations and explicit `.ltorg`/
  `.pool` handling with deduplication, alignment and relocation tests.
- [ ] Define pool placement deliberately: never insert executable fallthrough
  into literal data or silently introduce an unrequested control-flow change.
- [ ] Test 16/32-bit literal reach, PC alignment and interacting branch
  relaxation at exact positive/negative limits.
- [ ] Evaluate external conditional-branch relocations only with matching
  owned-linker support and out-of-range diagnostics.
- [ ] Keep macro systems, broad GNU compatibility and general CFI/unwind
  generation out of scope unless actual inputs justify them.

**Exit:** accepted conveniences preserve semantics and fail explicitly when
the specified layout cannot be represented.

### BA05 — multicore synchronization instruction coverage

**Priority: medium; coordinate with runtime design.**

- [ ] Specify and encode relevant LDREX/STREX byte/halfword/word forms and
  CLREX, including register overlap, alignment and addressing restrictions.
- [ ] Evaluate Armv8-M acquire/release forms against the exact RP2350 profile,
  adding only documented, needed forms.
- [ ] Document barriers and the distinction between local/global exclusive
  monitors, normal SRAM, device memory and peripheral registers.
- [ ] Establish which memory regions support the intended synchronization;
  do not imply that exclusives make arbitrary MMIO atomic.
- [ ] Compare exclusives with RP2350 SIO hardware spinlocks, FIFO and event
  mechanisms in runtime examples; these peripherals need protocols, not new
  CPU opcodes. SEV/WFE encoding already exists.
- [ ] Test two-core contention, ordering, progress and bounded timeout/error
  paths. Do not claim one-way timing or lock fairness from static bytes.
- [ ] Keep C atomic lowering, shared-memory ownership, cache/memory placement
  and interrupt interactions as explicit compiler/runtime prerequisites.

**Exit:** instruction encoding tests pass and a documented two-core protocol
has functional physical evidence, with both cores' clocks/configuration bound.

### BA06 — targeted integer DSP and saturation

**Priority: later, driven by workloads.**

- [ ] Inventory reached integer workloads before choosing additional packed
  arithmetic, multiply/accumulate, saturation and bit-manipulation forms.
- [ ] Prioritize missing SSAT/USAT and selected packed DSP forms only when
  useful; existing SMULL/UMULL and partial DSP support are not new features.
- [ ] Specify lane widths, signedness, overflow, APSR Q/GE effects, register
  restrictions and clobbers for every form.
- [ ] Test extreme operands and flags against independent semantics and LLVM
  encoding; use execution checks where practical.
- [ ] Coordinate separate compiler selectors with legality and whole-image
  profitability tests; assembler acceptance is not automatic vectorization.

**Exit:** each admitted form has a beneficiary or explicit low-level use case
and a complete encoding/semantics contract.

### BA07 — single-precision FPU support

**Priority: separate gated project, not incidental ISA completion.**

Follow the existing [FPU decision](datasheet/fpu-decision.md).

- [ ] Specify softfp-style internal hardware arithmetic while preserving the
  base calling convention; treat hard-float as a separate interoperability
  decision.
- [ ] Define FPU access/security initialization and barriers on **each core**
  that executes floating instructions.
- [ ] Define FP register allocation, callee preservation, FPSCR semantics and
  interrupt/lazy-stacking stack budgets.
- [ ] Add needed FPv5 single-precision encodings and correct object attributes;
  do not advertise hardware double precision.
- [ ] Complete constants, floating IR optimization and code generation,
  object/linker compatibility and mixed-integer/FP call tests.
- [ ] Test NaNs, infinities, signed zero, subnormals, rounding and conversions;
  do not enable contraction/reassociation without explicit policy.
- [ ] Measure actual workloads before claiming size/speed benefits.

**Exit:** an end-to-end requirement is satisfied on hardware; accepting
`vadd.f32` alone is not completion.

## Qualification policy

- Prefer exact encoding tests and the smallest targeted suite first.
- Preserve rejected-output files and test malformed operands and boundaries.
- Use LLVM as an independent reference, not the sole semantic specification.
- Measure loaded bytes, static RAM and stack separately. Do not equate ELF
  size, instruction count or issue rate with runtime performance.
- Hardware testing is required for privilege, synchronization and FPU state,
  not for every metadata-only change. One successful cold boot per badge is
  sufficient unless failures require further investigation.
- Document unsupported forms rather than silently dropping annotations.

Existing checks, from the repository root:

```sh
make -C badge-compiler build/badge-asm build/badge_asm_test
(cd badge-compiler && ./build/badge_asm_test)
(cd badge-compiler && bash tests/asm_oracle.sh)
```

Set `LLVM_CLANG`, `LLVM_LLD` and `LLVM_OBJCOPY` to the desired installed tools
for the oracle. Broader opcode families must add their own targeted tests.
