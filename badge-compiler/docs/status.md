# Badge compiler pipeline status

This is the current inventory of compilation stages, external tools and
project-owned alternatives. The [plan, contracts and test results](implementation.md)
describe how we develop and validate them.

Keep the stage IDs and ordering stable. Update the implementation/status
columns as replacements become available; do not turn this page into a
chronological work log.

## Scope and status meanings

Target: freestanding C11 plus required extensions, RP2350 Cortex-M33, Thumb,
little endian, Arm EABI. Both non-Wi-Fi applications and RP2350 Wi-Fi host code
using the unchanged vendor radio firmware blob are in scope. SDK applications
and replacement firmware for the processor inside the radio are out of scope.

- **Owned:** implemented by the project.
- **Owned subset:** implemented and tested for our documented input contract,
  not a general replacement for every feature of the external tool.
- **External:** no project-owned implementation of this stage yet.

An owned source file or configuration is not the same as an owned tool that
processes it. For example, we own the linker scripts, but still use LLD.

## Main path: C source to UF2

These are logical stages. Clang normally combines several into one invocation;
ASTs, IR and assembly need not be saved as intermediate files.

| ID | Stage | Input -> output | LLVM/reference implementation | Project-owned alternative | Status |
| --- | --- | --- | --- | --- | --- |
| C01 | Preprocessing | `.c` + headers + definitions -> preprocessed `.i` | Clang preprocessor: `clang -E` | [badge-cpp](../src/badge_cpp.c): includes, macros, conditional directives, target predefined macros and line tracking | Owned subset |
| C02 | C lexical analysis | Preprocessed C -> tokens for the C parser | Clang lexer | Shared [C lexer](../src/c_lexer.h), used by `badge-cpp` and `badge-parse`; parser classifies keywords, validates numeric spelling and maps preprocessing line markers. Literal decoding/typing remains C04 | Owned subset |
| C03 | Parsing | Tokens -> syntax tree / AST | Clang parser | [badge-parse](../src/badge_parse.c): scoped typedef-aware declarations, declarators, expressions, statements, initializers, attributes and inline assembly; `badge-c-ast-v1` JSON | Owned subset |
| C04 | Semantic analysis | AST -> type-checked AST; declaration/name resolution and diagnostics | Clang semantic analysis | [badge-sema](../src/badge_sema.c): target types/layouts, scopes/linkage, integer constants, conversions, expression/statement/initializer checks; `badge-c-typed-ast-v1` JSON | Owned subset |
| C05 | IR generation | Typed AST -> frontend LLVM IR (`.ll`; optional external `.bc` serialization) | Clang IR generator: `-emit-llvm` | [badge-ir](../src/badge_ir.c): shared owned parser/checker, scalar/aggregate lowering, initialization, relocation-free zero-storage canonicalization, control flow, volatile access and soft-float AAPCS signatures | Owned subset |
| C06 | IR optimization / legalization | Frontend IR -> backend-ready IR | LLVM passes inside Clang; standalone `opt` in the oracle | [badge-opt](../src/badge_opt.c) and [named passes](../src/ir_passes.c): integer constant folding/propagation, wide carrier memory scalarization and owned wide division/remainder helpers; extensible [pass API](../src/ir_pass.h) | Owned subset |
| C07 | Target code generation | IR -> Cortex-M33 machine instructions / optional assembly `.s` | LLVM Arm backend inside Clang; standalone `llc` in the oracle | [badge-codegen CLI](../src/badge_codegen.c) and [callable backend](../src/codegen.h): integer/pointer selection, AAPCS and parallel PHIs; qualified direct/global-compact/thumb-widths/word [current defaults](current-promotion.md), explicit conservative controls and opt-in [P02 local residency](implementation.md#p02-block-local-register-residency) | Owned subset |
| C08 | Assembly / instruction encoding | Assembly or backend machine instructions -> encoded bytes and fixups | LLVM MC / Clang integrated assembler; `clang -x assembler -c` for saved `.s` | [badge-asm](../src/badge_asm.c) and [Thumb encoder](../src/badge_thumb.h): Cortex-M33 integer/DSP subset, directives, relaxation, IT blocks, data/branch fixups and metadata; calls C09 directly | Owned subset |
| C09 | Relocatable object writing | Encoded code/data + symbols + relocations -> ELF `.o` | LLVM MC ELF object writer, used by Clang's integrated assembler and `llc -filetype=obj` | [badge-object](../src/badge_object.c) and [C API](../src/badge_object.h): ELF32 Arm EABI5 sections, symbols, Thumb marking, REL tables and metadata; versioned text manifest input | Owned subset |
| C10 | Linking | `.o` files + linker script -> executable `.elf` + `.map` | LLD (`ld.lld`), driven by Clang with `-fuse-ld=lld` | [badge-link](../src/badge_link.c): parses the subset used by our flash/report scripts, resolves symbols, collects live sections, lays out VMA/LMA, applies Arm REL fixups and writes ET_EXEC/map | Owned subset |
| C11 | Flash-image extraction | Executable ELF -> raw flash `.bin` | `llvm-objcopy -O binary` | [elf2bin](../src/elf2bin.c): allocated section contents at flash load addresses, RAM initializers, gap filling | Owned subset |
| C12 | UF2 encoding | Raw `.bin` + flash base + family ID -> `.uf2` | No LLVM tool used | Existing [bin2uf2](../../src/host/tools/bin2uf2.c) | Owned |

The `.bin` starts at `0x10000000`. UF2 uses the RP2350 Arm Secure family ID
`0xe48bff59`. The vendor radio binary is payload data, not C or LLVM IR to
compile.

## Which pipeline actually runs?

### Root Makefile default

The [root Makefile](../../Makefile) still uses:

```text
.c + headers
  -> Clang preprocessing/frontend/optimization/backend/assembler/object writer
  -> LLD
  -> llvm-objcopy
  -> our bin2uf2
  -> .uf2
```

The existing `RP2350_OBJCOPY` override can select our `elf2bin`. Having an
alternative implemented does not silently change the default build.

### Tested route using our replacements

The [oracle](../tools/oracle.sh) also produces an `ours-cpp.uf2` for each application:

```text
.c + headers
  -> our badge-cpp
  -> .i
  -> Clang (-x cpp-output: no second C preprocessing pass)
  -> .o
  -> LLD
  -> .elf
  -> our elf2bin
  -> .bin
  -> our bin2uf2
  -> .uf2
```

Here C01, C11 and C12 are project-owned. C02-C10 on the **code-generation
path** still depend on LLVM.
The oracle's baseline and other comparison routes retain Clang preprocessing;
its Makefile ELF-to-binary step uses our extractor.

The oracle additionally runs our C02/C03 implementation on both preprocessors'
`.i` files, saving `ours.ast.json` and `preprocessed.ast.json`. Their
`*.structure.ast.json` variants omit source locations and must match.
These are checked syntax-AST boundaries, not a replacement code-generation
route: our parser does not type-check, emit IR or call Clang.

It also runs `badge-sema` on both `.i` files. The checker reuses C03 through
the shared [frontend interface](../src/c_frontend.h), annotates its in-memory tree
and saves `ours.typed.ast.json` / `preprocessed.typed.ast.json` plus their
location-free structural variants. It does not import JSON or invoke LLVM.
`sema-comparisons.tsv` records acceptance and matching typed trees.
C01-C05 now have owned implementations. This older `ours-cpp` comparison
route deliberately continues to use Clang's own C02-C05; the owned frontend
route below instead connects our parser/checker to IR generation.

### Tested C09 object-writing boundary

For every C/assembly object, the oracle also decodes the LLVM reference
object into `object.bobj`, then uses `badge-object` to write a new `object.o`.
The `object` relink route uses these owned objects, LLD, our extractor and
UF2 encoder. `object-comparisons.tsv` records the 38 object captures;
`comparisons.tsv` records the 11 complete-image comparisons.

This tests C09 independently of C08/C07. LLVM still provides the encoded
instructions and initial reference containers; our writer reconstructs ELF
tables, symbol/section indices and relocation references rather than copying
the file. The C API/manifest can also accept fresh encoded bytes, demonstrated
by the handwritten API fixture. It is not an assembler, backend or linker,
and is not yet a drop-in replacement inside Clang's integrated assembler.

### Tested C08/C09 assembly boundary

The oracle also assembles every captured `native.s` / `preprocessed.s` with
`badge-asm`, encoding bytes and fixups itself and calling our C09 writer.
The `asm` route links these fresh objects with LLD, extracts with `elf2bin`
and encodes UF2 with `bin2uf2`. `asm-comparisons.tsv` records all 38 units;
all 11 images match the baseline. `asm.uf2` is saved for each application.
Vendor `.incbin` payloads are checked unchanged.

On this route C08, C09, C11 and C12 are owned. LLVM still supplies C
preprocessing/frontend/optimization/backend, handwritten `.S` preprocessing
and C10 linking. This comparison route deliberately retains Clang's frontend.
See the [assembler contract and tests](implementation.md#sixth-replacement-c08-thumb-assembly-and-instruction-encoding)
for explicit ISA/directive limits, assertion-only conditionals and
cantunwind-only exception records.

### Tested C08-C12 owned downstream route

The `linker` oracle route feeds C08/C09 objects directly into `badge-link`,
then our extractor and UF2 encoder. It parses the existing flash/report
linker scripts; their memory constants are not duplicated in built-in
profiles. `linker.uf2` is produced for all 11 applications.

All 11 images and public symbols match an independent LLD reference with
optional input merging disabled. Our initial linker does not pool/reorder
mergeable strings: ten images therefore differ from the ordinary optimized
baseline, deliberately and explicitly recorded. `linker-comparisons.tsv`
records the required unmerged-reference comparisons. No image is flashed.

On this route **C08-C12 are project-owned**. C compilation still uses LLVM
through C07, and this comparison route retains external handwritten `.S`
preprocessing. The fully owned route below replaces those stages too.
See the [linker contract](implementation.md#seventh-replacement-c10-linking) for
script syntax, relocation/ABI limits, diagnostics and validation.

### Tested C01-C05 and C08-C12 owned route

The oracle produces `owned-ir.uf2` for all 11 applications:

```text
.c + headers -> badge-cpp -> .i
  -> badge-ir (shared badge-parse + badge-sema) -> .ll
  -> LLVM opt -> LLVM Arm backend -> .s
  -> badge-asm / badge-object -> .o
  -> badge-link -> .elf -> elf2bin -> .bin -> bin2uf2 -> .uf2
```

`badge-ir` neither invokes Clang nor imports AST JSON. Both preprocessors'
inputs produce 72 verified modules and 36 matching location-independent
owned-IR pairs; `ir-comparisons.tsv` records these checks. All firmware
links have no unresolved symbols, and all 187 ELF extractions match LLVM.
Independent LLVM assembly/decoding confirms all 38 owned-route objects'
allocated bytes, canonical fixups and public symbols.
`ir-linker-comparisons.tsv` requires all 11 images/public-symbol sets to match
LLVM assembly followed by unmerged LLD linking of the same LLVM-backend
assembly, independently validating the new downstream route.

Focused sanitizer, LLVM-verifier, runtime differential and bidirectional
mixed-frontend ABI fixtures validate the emitter. Runtime fixtures retain
the Arm frontend's types/ABI but are retargeted for host execution; they
deliberately avoid pointer-containing records and host/target pointer-width
assumptions. This is not on-device timing or execution evidence.

Owned frontend output need not match Clang's code: one complete image is
byte-identical and ten differ. Aggregate flash size is 294,528 versus
293,520 bytes (+0.34%); screen test is 1,680 versus 1,676 bytes.
No image was flashed. Production defaults remain unchanged.
See the [C05 contract](implementation.md#eighth-replacement-c05-llvm-ir-generation)
for explicit exclusions and validation boundaries.

### Tested C01-C05 and C07-C12 owned route

The oracle also produces `backend.uf2` for all 11 applications:

```text
C -> badge-cpp -> badge-ir -> LLVM opt -> badge-codegen -> badge-asm/badge-object
  -> badge-link -> elf2bin -> bin2uf2 -> UF2
```

All 36 C units lower through the native backend. All 11 image/public-symbol
sets match independent LLVM assembly plus unmerged LLD for our assembly.
This comparison route retains LLVM C06 and Clang assembly preprocessing;
the fully owned route below no longer needs either.

Focused fixtures execute on QEMU's Cortex-M33, including cyclic PHIs,
large frames, wide integer arithmetic and bidirectional mixed Clang/owned
ABI calls. This is CPU execution evidence, not RP2350 peripheral or hardware
timing validation. The initial allocator spills SSA values to the stack:
larger code and frames are expected and are not claimed as optimized.
Aggregate flash is 842,684 versus the ordinary baseline's 293,520 bytes
(+187.10%); screen test is 10,044 versus 1,676 bytes. The largest static
local-frame-plus-save allocation is 1,752 bytes, excluding outgoing calls,
nested frames and interrupts. This is separate from C10's small pooling cost.
Floating/general wide operations and unsupported IR/ABI forms are diagnosed.
See the [C07 contract](implementation.md#ninth-replacement-c07-cortex-m33-code-generation).
No badge was flashed; production defaults remain unchanged.

### Tested fully owned C01-C12 and A01-A02 route

```text
C -> badge-cpp -> badge-ir -> badge-opt -> badge-codegen -> badge-asm/badge-object
  -> badge-link -> elf2bin -> bin2uf2 -> UF2
.S -> badge-cpp --assembly -> badge-asm/badge-object -> badge-link
```

**There are no missing main compilation stages for the documented corpus.**
All 36 C units and both assembly units compile using owned tools; all 11
firmware UF2s build. The oracle produces `owned.uf2` and independently checks
IR validity, all allocated object bytes/fixups/public symbols, and all linked
images/public symbols against LLVM assembly plus unmerged LLD.

For a build that does **not run LLVM target tools or oracle tools**, use:

```sh
bash badge-compiler/scripts/build-owned.sh
```

This reuses the root Makefile's sources/flags/linker scripts, saves
intermediates under `build/owned/`, and uses owned ELF symbol/size reporting.
It defaults to four parallel jobs; set `BADGE_BUILD_JOBS=1` for serial
comparisons or another integer up to 16 for bounded parallelism. The stage
ordering within each compilation remains unchanged. The host
[badge-sha256](../src/badge_sha256.c) helper preserves source/tool manifests without
per-source scripting-runtime startup.
Wi-Fi selects the UF2 recipe rather than the unrelated host USB collector.
A native host C compiler, host libraries, Bash, Make and standard utilities
are still needed to bootstrap the tools and package assets; vendor radio
firmware remains supplied payload. Default production builds are unchanged.

C06 is deliberately small: named, independently selectable passes provide
future optimization extension points, not LLVM-level optimization quality.
The fully owned aggregate flash payload is **2,158,480 bytes** versus
293,520 for ordinary LLVM (+635.38%); all images fit linker regions.
The largest static frame plus saves is **5,312 bytes**, exceeding the 4 KiB
SCRATCH_Y region where the stack starts, even before nested calls/interrupts.
Stack safety, peripheral timing and physical badge execution are **not
certified**; do not treat these builds as hardware-ready.
See the [C06 contract and pass development guide](implementation.md#tenth-replacement-c06-owned-ir-passes).
The proposed bounded optimization release is tracked in [plan-c06.md](plan-c06.md);
M00-M09 are complete under the approved measured-size re-scope.
M09 enables `mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-division`.
Z04 subsequently substitutes qualified `scalar-cleanup` in the same slot;
the current default is
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`.
Other standalone optimizations remain explicit-only. The individual pass
records below describe their historical opt-in qualification.
D07's separate opt-in `target-strength-reduce` now adds shared-model-gated
unsigned i32 power-of-two div/rem reduction, with materialization/fusion
accounting and allocated code/frame checks. No defaults change; actual
suite timing in D09 establishes no resolved speed benefit. See
[IR profitability and qualification](datasheet/ir-profitability.md).
D08's [six independent decisions](datasheet/README.md#d08-independent-decisions)
are evidence-qualified without production changes. Scalar SMULL selection,
selected-function placement and an explicit GPIO-helper pilot are bounded
follow-ups, not enabled features. FPU/DCP support and RCP hardening remain
separate; no speed, panel-timing or whole-program stack certification is added.
D09's [independent qualification](datasheet/qualification.md) passes five
fresh offline profiles and ten repeated hardware campaigns. Collector teardown
is fixed with observed selected-device removal; two full original flash images
are restored and verified. D04/D06 are rejected for speed promotion, D05
remains opt-in pending production-runtime coverage, and D07 has no resolved
suite speed benefit. Defaults and the production coverage limits above remain.

**M01 measurement:** [native IR metrics](../tools/badge_ir_metrics.c), ELF `--metrics`
and a [host-only benchmark](../tools/benchmark-c06.mjs) report all 36 modules/11 images,
pass outcomes, linked flash/vendor/static section sizes, qualified per-call
stack paths, provenance and build resources. See
[methods and qualified results](implementation.md#c06-m01-reproducible-measurements).
Six deterministic default/equivalent builds preserve all 182 saved artifacts
and have zero code/known-stack deltas. Residual C06 output contains 2,850
allocas; screen/diagnostics/IMU direct-call estimates are 1,488/4,280/4,144
bytes, excluding interrupts. Unknown Wi-Fi paths remain explicit, and its
largest frame remains 5,312 bytes. Owned/LLVM-QEMU tests and sanitized reports
for all modules/images pass. This enables no new optimizations, does not
certify stack/runtime behavior, and performs no hardware upload.

**M02 structured IR/verifier:** [ir_analysis.h](../src/ir_analysis.h) and
[ir_analysis.c](../src/ir_analysis.c) add typed functions/blocks/instructions/operands,
indexed definitions/uses, CFG edges (including multiplicity), immediate
dominators and conservative effects. The owned subset verifier diagnoses
reference/type/call/terminator/PHI/dominance errors before each pass and after
the final rewrite; `--verify-only` checks and round-trips without changing
text. No optimization/default pass changed. On 2026-10-08 all 72 C05/C06
boundary files round-tripped under sanitizers, the complete 36-module/11-image
LLVM-free build preserved all 179 immediate pre-M02 artifact hashes, and
owned-only/oracle/metrics gates passed. A fresh physical differential run
matched all six modules and recovered both badges to BOOTSEL. See the
[supported contract, API and resource/semantic limits](implementation.md#c06-m02-structured-views-and-owned-verification).
M02 itself changes no target size or stack demand.

**M03 worklist DCE:** [ir_dead_instructions.c](../src/ir_dead_instructions.c) adds
opt-in `dead-instructions`, using indexed def-use counts and an explicit
pure, nontrapping whitelist. It removes unused folded freezes and cascading
dead expressions; retains memory, allocation, calls/assembly, control,
division/remainder, opaque annotations and dead SSA cycles. With
`constant-fold,aggregate-memory,wide-division,dead-instructions`, fixed-input
three-run snapshots remove 1,007 instructions and 25,248 aggregate nonvendor
bytes (1,995,340 -> 1,970,092; 1.265%). Every image shrinks; ten maximum frames
shrink and one stays unchanged. The largest frame falls from 5,312 to 5,136
bytes, still above the production 4 KiB stack; most entry-call bounds remain
unknown. Sanitizer fixtures/corpus, LLVM verification/target ABI execution,
owned-only/oracle/metrics suites and all six physical comparison modules
pass, with both badges recovered to BOOTSEL. The unchanged default pipeline
preserves all 179 artifact hashes. See
[measurements, pass selection and limits](implementation.md#c06-m03-worklist-dead-instruction-elimination).
M09 still owns default enablement; subsequent M04 results follow.

**M04 trivial scalar promotion:** [ir_scalar_promotion.c](../src/ir_scalar_promotion.c)
adds opt-in `scalar-promotion`, with M02 recording each alloca's allocated
type. It promotes direct, ordinary same-type scalar accesses confined to one
block, or initialized by one preceding entry-block store. It preserves
escaping/indirect, volatile/opaque, mismatched, dynamic/nonpositive,
aggregate, nonentry and CFG-dependent slots, including potentially
loop-carried read-before-store cases. Initial reads that can be safely
promoted retain `undef`, never an invented zero.

The fixed-input M03-only/M04-plus-M03 three-run comparison promotes **459**
slots, removing **846 loads / 463 stores** and saving **52,232 nonvendor bytes**
(1,970,092 -> 1,917,860; **2.651% incremental**). All 11 images shrink; nine
maximum frames shrink and two stay unchanged. The largest falls **5,136 ->
5,112 bytes**, still unsafe relative to 4 KiB, with most entry bounds unknown.
All production promotions use the single-block mode; the strict
single-entry-store mode has separate branch/PHI/loop/i64 fixtures. Full
sanitizer/corpus, LLVM/target/source/ABI and metrics gates pass. The six-module
physical differential run matches LLVM and recovers both badges to BOOTSEL.
All 179 default artifacts remain byte-identical. See
[contract, ablation and qualification](implementation.md#c06-m04-trivial-scalar-promotion).
M04 itself does not provide general mem2reg; subsequent M05 results follow.

**M05 scalar mem2reg:** [ir_mem2reg.c](../src/ir_mem2reg.c) adds opt-in `mem2reg`,
sharing M04's [eligibility/erasure contract](../src/ir_promotion.h). It builds
dominance frontiers, inserts live-in-pruned scalar PHIs, renames through the
dominator tree iteratively, and removes trivial generated/existing pure
scalar PHIs with an indexed worklist. It preserves undef/poison semantics,
parallel edge multiplicity and opaque/escaping/unsupported memory roots.

The fixed-input three-run M04/M05 comparison removes **2,417 more slots,
6,535 loads and 3,937 stores**, retaining **1,544 generated PHIs**. Nonvendor
bytes fall **1,917,860 -> 1,724,460** (-193,400; **10.084% incremental**).
All 11 images and maximum frames shrink. The largest frame falls **5,112 ->
3,504 bytes**, crossing the local-frame target, but ten entry-call bounds
remain unknown and production stack/interrupt/timing/RF safety is not
certified. Across M03-M05 the unchanged-current-input default comparison
saves 270,880 bytes (13.576%), not yet the proposed 20% release improvement.
Sanitizer/corpus, LLVM/typed loop/irreducible/parallel-edge/ABI and metrics
gates pass; all six physical comparison modules match and both badges
recover to BOOTSEL. Default artifacts still match all 179 prior hashes.
See [methods, resource limits and residual coverage](implementation.md#c06-m05-pruned-scalar-mem2reg).
M06 results follow; M09 still owns combined default qualification.

**M06 SCCP/CFG cleanup:** [ir_sccp.c](../src/ir_sccp.c) adds opt-in `sccp` with
executable-edge integer propagation, constant branches/switches, unreachable
block deletion, exact parallel-edge PHI repair and cascading trivial-PHI
cleanup. It preserves unknown/undef/poison conditions, flags and opaque
effects, skips block-address modules, and reparses/verifies between existing
folding/DCE cleanup steps. Defaults are unchanged.

The fixed-input three-run corpus comparison removes **41 blocks / 47 edges**
and reduces instructions **30,410 -> 30,369**. Nonvendor linked bytes fall
**1,724,460 -> 1,723,800** (-660; **0.0383% incremental**); all 11 images
shrink, but frames and memory/call/volatile totals do not change. Combined
M03-M06 saving versus the current default is **13.609%**, not yet 20%.

Sanitizer/LLVM fixtures, all 36 corpus transformations/72 IR boundaries,
Cortex-M33 source/raw-IR execution and mixed ABI gates pass. Controlled
fixed-seed, role-swapped physical M05/M06 comparisons match all six modules
and recover both badges. Runtime changes are **-0.0101% to +0.3845%**:
no meaningful speedup in these workloads. Host memory/CPU cost increases;
no noisy wall-time improvement is claimed. See
[full contract, measurements and USB recovery history](implementation.md#c06-m06-sparse-conditional-constant-propagation).
M07 qualification follows; M09 still owns default qualification.

**M07 instruction combining:** [ir_instruction_combine.c](../src/ir_instruction_combine.c)
adds opt-in `instruction-combine`: type-correct neutral arithmetic and
proven integer cast chains, indexed alias/use worklists and verified DCE
cleanup. Compatible flags are removed only with a neutral-operation proof;
`nsw i1` multiplication, opaque instructions, unsafe cast chains, direct
undef/poison forwarding and pointer/int cancellation are excluded.
Global/literal pointer casts retain indirect-call verification behavior.
No division replacement, reassociation or memory/CFG elimination is added.

The three-run fixed-input corpus comparison removes **31 instructions** and
**996 linked bytes** (0.0578% incremental): nonvendor bytes fall
**1,723,800 -> 1,722,804**. Ten images shrink, `hello_idle` is unchanged.
Representative C07 functions emit fewer real loads/stores and smaller local
frames; `gpio_monitor`'s maximum frame falls 320 -> 312 bytes, but the corpus
maximum remains 3,504 bytes. Combined M03-M07 savings are **13.659%**, not 20%.

All 132 sanitizer/LLVM/idempotence fixtures, 36 corpus transformations/72 IR
boundaries and Cortex-M33/mixed ABI execution gates pass. Two controlled,
fixed-seed, role-swapped hardware comparisons match all six modules and
recover both badges. Runtime changes remain within **-0.0306% to +0.0668%**:
no meaningful speedup in these workloads. All **228** prior default artifact
hashes still match. See
[rules, per-rule reachability, C07 and runtime results](implementation.md#c06-m07-bounded-instruction-combining).
M08 qualification follows; default policies remain unchanged.

**M08 basic-block CSE:** [ir_local_cse.c](../src/ir_local_cse.c) adds opt-in
`local-cse`: exact collision-safe typed expression keys, same-block earlier
representatives, indexed aliases and verified DCE cleanup. Arithmetic,
comparisons, casts and GEPs include semantic flags/types and preserve operand
order. No loads/calls, cross-block value numbering, independent freezes,
opaque metadata, direct undef/poison or trapping division are eliminated.
The additional 50M-work budget and existing arena/text limits diagnose
atomically.

Three deterministic builds per configuration remove **206 instructions**:
40 arithmetic, 1 comparison, 38 casts and 127 addresses. Nonvendor bytes
fall **1,722,804 -> 1,716,284** (-6,520; **0.3785% incremental**). Ten
images shrink; all memory/call/volatile totals stay unchanged. Sixty-one
local frames shrink and none grows; the largest frame including saves
falls **3,504 -> 3,496** bytes. Combined M03-M08 savings are **13.9854%**,
not 20%; unresolved call-stack paths remain.

Both 92-fixture sanitizer/LLVM/idempotence suites (normal and forced hash
collisions), owned-only variants, 8,000 repetitions, atomic failures,
36 corpus transformations/72 LLVM boundaries, Cortex-M33 source/raw IR and
mixed ABI gates pass. All **228** default artifact hashes remain unchanged.

Two repeated fixed-seed, role-swapped hardware comparisons match all six
modules and recover both badges. Arithmetic is **7.17-7.18% faster** and
strings **5.94-6.04% faster**, but memory is **7.64-7.65% slower**.
Inspection finds fewer memory-workload instructions/accesses and a smaller
frame, unchanged byte/CRC helper assembly, but changed flash placement.
Layout/XIP effects are plausible, not proven. This repeatable >5% regression
is an explicit M09 default-qualification blocker, not a reason to silently
enable or claim a universal speedup. See
[contract, C07 inspection and repeated timing evidence](implementation.md#c06-m08-basic-block-common-subexpression-elimination).
M09's qualified default release follows; backend follow-ups remain pending.

**M09 qualified defaults:** [ir_passes.c](../src/ir_passes.c) now selects
`mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-division`.
Both hardware modes inherit that policy; `badge-opt --default-passes`
publishes it for exact provenance. Explicit selections, including the old
three-pass route, remain available. Root LLVM compiler selection, clocks,
production stack regions and application sources are unchanged.

The approved first release replaces the proposed 20% size target with
measured C06 gains. Three deterministic builds per configuration reduce
nonvendor bytes **1,995,340 -> 1,748,444** (-246,896; **12.3736%**).
All 11 images and maximum frames shrink; the largest frame including saves
falls **5,312 -> 3,672 bytes**. Ten entry-call bounds remain unresolved:
this is not production stack/interrupt/Wi-Fi readiness certification.

Final-pipeline corpus and hardware ablations qualify both selected
optimizations. Scalar promotion is redundant with full mem2reg. Standalone
late DCE saves more bytes but repeatedly slows strings **22.37-22.43%**;
SCCP's cleanup candidate has the same slower timing context. They and CSE
remain explicit-only, while M05/M07's internal cleanup stays enabled.
Layout/cache contributions remain plausible, not proven.

Version-2 fixed-seed, role-swapped hardware tests make the actual default
**20.51-36.17% faster** than the legacy default across all six workloads.
The approved timing-only fingerprint samples every fourth pixel after full
drawing; the separate full-frame correctness run still covers all 76,800
pixels. Safety limits are unchanged; mixed versions are rejected.
All completed runs match LLVM and recover both badges.

Full oracle/unit/metrics and owned-only suites, 36 sanitizer transforms/
72 LLVM boundaries, independent 38-object/11-executable/UF2 checks and
host/protocol tests pass. The broader LLVM-optimized-IR branch of `oracle.sh`
hits a pre-existing C07 byte-string escape restriction before owned C06;
it is explicitly recorded, not treated as a successful full run.
See [release choices, all ablations and evidence](implementation.md#c06-m09-qualified-default-release).
P00's separate [isolated-kernel measurement campaign](implementation.md#p00-isolated-kernel-baseline)
is now complete: six kernels, seven samples of 64 calls x 128 iterations,
with checksums/guards outside timing, full per-call output checks and retained
whole-module tests. Two fixed-seed campaigns each pass compiler role swaps;
all twelve results match LLVM and both badges recover to BOOTSEL. The
[controlled report comparator](../tools/compiler-timing-compare.mjs) separates
candidate/owned-baseline change from LLVM drift and rejects incompatible
provenance, work, clocks or results. Normal/timing sanitizer, protocol/USB/
runner guards and firmware builds pass. No optimization, clock or safety
bound is changed by P00.

P01's [direct Thumb-2 frame addressing](implementation.md#p01-direct-thumb-2-frame-addressing)
is complete and opt-in (`BADGE_CODEGEN_FRAME_ADDRESSING=direct`), independently
recorded from C06 selections. All 36 modules retain identical C06 inputs and
frame/call models; 113,016 address instructions are removed and aggregate
nonvendor bytes fall by 463,264 (26.50%). Encoding, both-mode sanitizers,
large-frame/mixed/all-owned ABI and optimizer execution pass. Two role-swapped
hardware campaigns match all twelve LLVM results and show 38.92-49.36% lower
kernel times and 35.57-48.93% lower whole-module times against frozen P00
controls. Ordinary full-frame diagnostics pass and both badges are restored
to BOOTSEL. Defaults, clocks, stack limits and unresolved stack bounds are
unchanged.

P02's [block-local residency](implementation.md#p02-block-local-register-residency)
is implemented opt-in (`BADGE_CODEGEN_REGISTER_RESIDENCY=local`), using `r0`
and already-saved `r8` while retaining all stack homes and scratch/ABI policy.
Four-mode sanitizers, mixed/all-owned ABI, pressure/assembly/wide/PHI fixtures,
all 38 independent LLVM encodings and two role-swapped hardware campaigns
pass correctness. It avoids 7,212 emitted reloads and 6,963 stores and saves
27,716 nonvendor bytes (2.16%) versus P01, with identical C06 IR and frames.
Runtime is mixed: strings improve about 36%, several kernels improve 1-4.6%,
but whole arithmetic regresses about 7% and some workloads have smaller
regressions. The reproducible tradeoff is documented; no default is changed.
Ordinary diagnostics pass and both badges are restored to BOOTSEL.

P03's [cross-block allocation](implementation.md#p03-cross-block-register-allocation)
is implemented opt-in (`BADGE_CODEGEN_REGISTER_RESIDENCY=global`). Bounded
CFG liveness/interference colors 32-bit scalars/pointers into r8/r10/r11;
excess pressure keeps stack homes and PHI edge copies handle cycles/critical
edges. Six-mode sanitizers, mixed/all-owned ABI, indirect calls, assembly,
wide/narrow/irreducible fixtures, all 38 LLVM encodings and two role-swapped
hardware campaigns pass. C06 IR, local frames and outgoing arguments remain
identical; r10/r11 users add eight saved bytes, with a 3,680-byte maximum
frame including saves. Unresolved stack bounds remain unresolved.
Nonvendor bytes shrink another 42,332 (3.37%) versus P02; all measured whole
modules improve 1.82-13.40% and kernels 6.74-21.44%. Repeat variation is below
0.027%. Defaults stay unchanged. Ordinary diagnostics are restored and both
badges are in BOOTSEL.

P04's [native scalar selection](implementation.md#p04-native-scalar-selection)
is implemented and hardware-qualified opt-in
(`BADGE_CODEGEN_SCALAR_SELECTION=thumb`; default `legacy`).
Legal immediate/GEP forms, adjacent single-use MLA and matching signed/
unsigned div/rem reuse preserve unflagged semantics, dependencies and
hardware division. Twelve-mode sanitizers, LLVM/raw-IR and mixed-ABI
execution, full oracle, owned-only and 24 driver/report tests pass.
Fresh legacy output matches P03 exactly; all 36 release/sanitizer modules
and 38 independently encoded objects agree. IR, frames, save masks and call
models are unchanged; max frame remains 3,680 bytes and unresolved stack
bounds remain unresolved. Nonvendor bytes shrink another 13,468 (1.11%).
Two complete role-swapped campaigns match all twelve LLVM results and
improve whole modules 1.39-6.25% and kernels 1.11-12.65% versus P03, with
repeat variation below 0.036%. One incomplete collector-timeout campaign
was rejected; a fresh retry passed. Defaults stay unchanged, ordinary
diagnostics are restored and both badges are in BOOTSEL.

P05's [word memory selection](implementation.md#p05-word-memory-selection) is
implemented and hardware-qualified opt-in
(`BADGE_CODEGEN_MEMORY_SELECTION=word`; default `byte`). Only constant-size
nonvolatile copy/fill intrinsics with proven local stack-object extent and
alignment use word chunks plus exact byte tails. Unknown/aligned parameters,
MMIO, volatile and lost proofs retain byte accesses. All 24 policy sanitizer
combinations, raw-IR guards/lengths/nonzero fills/alignment, mixed ABI, full
oracle, owned-only and 25 driver/report tests pass. Fresh byte output matches
P04 exactly; all 36 release/sanitizer modules and 38 independent encodings
agree. IR, frames, saves and call models stay unchanged; max frame remains
3,680 bytes and unresolved stack bounds remain unresolved. Seven production
modules reach 73 word copies and 52 fills; nonvendor bytes shrink another
336 (0.028%). Two complete role-swapped campaigns match all twelve LLVM
results; cross-file ABI elapsed time improves 24.82-24.84%, while most other
intervals are unchanged or within small measured variation. Repeat variation
is below 0.036%. Defaults remain unchanged, ordinary diagnostics are restored
and both badges are in BOOTSEL.

P06's [loop-invariant motion](implementation.md#p06-loop-invariant-motion) registers
explicit-only C06 `loop-invariant`. Natural loops reuse existing unique
preheaders and dominating outside operands; bounded topological motion
preserves CFG/PHIs/names. A nontrapping unflagged arithmetic/cast/address
whitelist rejects memory/calls/assembly and unproved shifts/division/
overflow/poison flags. The 50M-unit work bound fails explicitly.

Z04 adds separate opt-in `loop-cost`: only one available costly i32
constant multiplication/division/remainder per loop, with existing
dominating outside-loop live results counted toward its conservative budget. The old
loop-motion pass remains outside the default. See the
[Z04 contract](maze-optimization.md#z04-candidate-contract) for offline
evidence. Hardware finds a 0.143/0.144% graphics regression, so this
pipeline is not promoted.

Z04's selected `scalar-cleanup` candidate instead forwards identity
equality/nonzero tests of extended booleans through the shared typed
combiner. It removes 2,202 tests in 35 corpus modules without changing
frame demand or memory/call ordering. The 308-case combiner suite and
independent LLVM/QEMU checks pass. Both complete physical repeats improve
all twelve workloads (2.9-32.1% fewer ticks). The user confirmed gameplay
and 35,198 / 4,268 owned drawing/physics ticks, then approved promotion.
The new shared CLI/API default uses `scalar-cleanup`; the preceding
`instruction-combine` profile stays available explicitly.
Original P06 qualification: 38 sanitizer/LLVM/idempotence/scaling/resource fixtures, zero-trip/nested/
irreducible/volatile/trap C/raw-IR execution, mixed ABI, full oracle,
final owned-only and 25 driver/report tests pass. Fresh P05 bytes match
exactly; all 36 optimizer/backend sanitizer modules and 38 independent
encodings agree. Input/C05 hashes, instruction/opcode/volatile counts,
frames/saves/calls remain unchanged. P06 hoists 281 instructions in 23
modules with 13 extra nested moves; nonvendor bytes grow 148 (0.0123%)
and global stack-backed used values increase by 68. Max frame stays 3,680
bytes and unresolved bounds remain unresolved.
Two role-swapped campaigns match all twelve LLVM results: arithmetic
improves 12.595%, graphics 2.283-2.284%, but whole memory regresses
0.771-0.774%. Repeat variation reaches 0.063%; no isolated cause is assigned
to the regression. P06 stays opt-in with mixed runtime results. Defaults
remain unchanged, ordinary diagnostics are restored and both badges are
in BOOTSEL. P00-P06 are complete within their documented scopes.

**Physical Wi-Fi qualification:** a subsequent explicitly authorized
[two-badge smoke test](implementation.md#physical-wifiudp-smoke-test) used an isolated
64 KiB stack and unchanged vendor assets. Cooperative CRC/display servicing,
isolated native primitives and authorized 20M startup/40M smoke lifetime
budgets repaired startup/progress. Idle-only USB pacing repaired an
enumeration regression; proactive validated-neighbor ARP refresh repaired
cache-expiry packet loss without extending TTL or response deadlines.
Final owned `475fbeeb` passed 200/200 echoes twice with zero losses/drops,
complete validated datasets and BOOTSEL recovery. Normal LLVM `b53793f3`
also passed on repeat; its first dataset was strictly rejected for an
intermittent cross-peer echo-counter inconsistency, still unresolved.
Reversed physical roles fail association under both compilers and remain
unqualified. Production stack/default lifetime and C06 pass policy are
unchanged; the current application inputs are not comparable with the
retained historical 182-artifact/M01 baselines. See the linked record for
exact roles, evidence, limits and coverage gaps.

**Automatic compiler differential demo:** the optional
[two-badge runner](implementation.md#automatic-two-badge-compiler-comparison) builds
LLVM and fully owned non-Wi-Fi images from identical modular sources.
Arithmetic, strings, memory, control flow, cross-file ABI and framebuffer
graphics results matched in three physical runs with fresh shared seeds.
Live USB verifies source identity, nonce, ordered modules, case counts and
CRCs before comparing six lanes per module; both badges recover to BOOTSEL.
Sanitizer/protocol/mock-runner and existing USB/UDP regression gates pass.
This is an isolated 64 KiB-stack hardware regression tool, not universal C
or physical-display certification. It supplements M02's independently
completed analysis/verifier gates rather than replacing them.

**Behavior-preserving plumbing:** C06 now separates its CLI, linked
[core](../src/ir_core.c), [registry/default policy](../src/ir_passes.c) and three pass
implementations. Pass roles are explicit; registration alone does not
enable a pass. Scratch allocations are reclaimed per pass, and indexed
constant/name lookup replaces repeated linear searches. [target.h](../src/target.h)
shares narrow C05/C06/C07 contracts; [owned-pipeline.sh](../scripts/owned-pipeline.sh)
shares owned execution without coupling it to LLVM reference generation.
`make -C badge-compiler test-owned` runs LLVM-free sanitizer/unit fixtures;
`test-oracle` retains mandatory LLVM comparisons and target execution.
No new optimizations or structured IR analyses were enabled by that refactor;
the subsequent M02 addition above provides analyses/verification separately.
Validation on 2026-10-07 passed owned/oracle suites, Cortex-M33 execution and
the full LLVM-free build. All 182 saved frontend/C06 IR, assembly, object,
ELF/bin/UF2 and vendor-payload artifacts are byte-identical to the prior
build. All 36 production modules reproduce release IR/assembly under
sanitizers; 38 objects independently match LLVM decoding and all 11 UF2s
decode correctly. Code size/stack output and the hardware-tested screen
image are unchanged; no additional flashing was performed.

**Physical smoke test (2026-10-07):** the user authorized flashing the fully
owned screen-test demo and confirmed the complete expected image on the
attached RP2350 badge. A fresh LLVM-free build produced the oracle-matching
20,668-byte flash payload; all 81 UF2 blocks were checked before upload.
The conservative direct-call stack bound was 1,512 bytes, not a measured
high-water mark. This validates boot/startup and this display demo only;
other images and broader stack/timing safety remain unverified. Production
defaults are unchanged. See the [hardware test record](implementation.md#physical-screen-test-smoke-test).

## Other inputs that join the main path

| ID | Input / stage | External tools | What we own | Status |
| --- | --- | --- | --- | --- |
| A01 | Handwritten `.S` -> preprocessed assembly | Clang preprocessor (`clang -E`) | [badge-cpp](../src/badge_cpp.c) `--assembly`: existing macro/include/conditional engine, `__ASSEMBLER__`, and token-adjacency-preserving output; tested on Picobin and vendor assets | Owned subset |
| A02 | Preprocessed assembly -> `.o` | Clang integrated assembler / LLVM ELF writer | [badge-asm](../src/badge_asm.c) with [badge-object](../src/badge_object.c), tested on Picobin and vendor assets | Owned subset; joins at C10 |
| I01 | Startup/vector table -> compiled object | Same C01-C09 stages as application C | [start.c](../../src/arch/rp2350/start.c): vectors, data initialization, BSS clearing, `bare_main` entry | Owned runtime source, not a separate compiler replacement |
| I02 | Generated headers | Make recipes, shell utilities and host tools; not a LLVM compilation stage | Existing build-ID/header generation; `badge-cpp` consumes the headers | Owned build recipes |
| I03 | Vendor Wi-Fi assets -> embedded section data | Native host C compiler builds the packer; reference route uses Clang via A01-A02 | [wifi_pack_assets](../../src/host/tools/wifi_pack_assets.c), packaging metadata and loader code; fully owned A01-A02 embed `.incbin` payloads | Owned packaging; vendor payload remains external and unchanged |

Only the vendor-blob Wi-Fi scenario needs I03. Preserve asset bytes, provenance
and [distribution boundaries](../../docs/wifi-assets.md).

## Oracle and inspection tools, not mandatory compilation stages

| Tool | Use | Project-owned alternative |
| --- | --- | --- |
| `clang -Xclang -ast-dump=json` | Inspect parser/type-checker results | Our parser/checker emit separate versioned syntax/typed ASTs; neither tool imports Clang JSON |
| `clang -Xclang -fdump-record-layouts-complete` | Inspect target record sizes, alignment and member offsets | Our checker computes record layouts in its type table |
| `llvm-as` | Text LLVM IR -> bitcode | None; optional serialization boundary |
| `llvm-dis` / `opt -S` | Bitcode -> text LLVM IR | None; inspection/round-trip boundary |
| `opt -passes=verify` | Validate LLVM IR | None |
| `llvm-readobj` | Inspect ELF headers, sections, symbols and relocations | Our extractor and shared object decoder/linker read their documented ELF subsets; none is a general inspector |
| `llvm-objdump` | Disassemble objects and show relocations | None |
| `llvm-nm -u` | Check for unresolved symbols | [badge-elf-info](../src/badge_elf_info.c) `-u` for supported ELF32 Arm executables |
| `llvm-size` | Report code/data/BSS sizes | [badge-elf-info](../src/badge_elf_info.c) reports allocated read-only/writable/NOBITS byte counts, not general LLVM size modes |
| `llvm-objcopy --dump-section` | Inspect embedded vendor payload sections | None; our `elf2bin` only implements whole flash-image extraction |
| Clang `-dM` / `-M` | Capture reference macro definitions and dependencies | No dump/dependency mode in `badge-cpp` |

These remain useful oracles even after we own a stage. Their presence in tests
does not mean our replacement delegates its implementation to them.

## Build, validation and execution boundary

- [Make](../Makefile) builds the owned **host tools** using a native C compiler
  (`cc` by default); this is separate from compiling code for the badge.
- `make -C badge-compiler test-oracle` runs focused owned-tool tests and LLVM
  comparisons; `bash badge-compiler/tools/oracle.sh` tests both application scenarios.
- `bash badge-compiler/scripts/build-owned.sh` builds source-to-UF2 without LLVM target
  tools; it is separate from the LLVM-dependent validation suites.
- The [current test record](implementation.md#tested-baseline-2026-10-07) covers all
  36 C translation units and 11 firmware images. Supported preprocessing and
  extraction, parsing, semantic-analysis, IR-generation, owned IR passes, assembly, object-writing and linking contracts
  are documented there; C08/C09 additionally cover the two assembly units.
- Neither the compiler nor the oracle flashes hardware. Loading UF2 via
  BOOTSEL or `picotool` is a separate deployment action. The boot ROM is
  external; the application startup source is ours.

Correct program behavior, ABI, boot/layout constraints and hardware interaction
are the acceptance criteria for future compiler stages. Matching LLVM's
optimization quality, IR text or instruction sequences is not required.
