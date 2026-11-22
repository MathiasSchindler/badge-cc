# Compile Graph

A static, interactive comparison of the owned compiler and LLVM, following
real firmware through preprocessing, ASTs, lowering, optimization, assembly
and linking. Functions and structural regions are discovered from artifacts;
no manually defined program segments are required.

## Capture and view

Requires Node.js, Make, a host C compiler, and LLVM's `clang`, `llvm-objcopy`,
`llvm-nm`, `llvm-size`, `llvm-objdump` and `ld.lld`. Clang must support Cortex-M33 and
have LLD available. There are no npm dependencies.

From the repository root:

```sh
mkdir -p compile-graph/build
node compile-graph/capture.mjs --capture compile-graph/build/demo
node compile-graph/capture.mjs --verify compile-graph/build/demo
python3 -m http.server 8000 --directory compile-graph/build/demo
```

Open <http://localhost:8000>. The report contains HTML, JavaScript, CSS, JSON
and the captured artifacts; it needs only a static file server. It can be
copied as a directory and viewed independently of the original checkout.

The default capture builds **red square and tilt maze**, including all their
C translation units and linked firmware. It does not flash or access badges.
The output directory must be new; existing reports are never overwritten.
Other standalone Makefile targets can be supplied:

```sh
node compile-graph/capture.mjs --capture compile-graph/build/diagnostics diagnostics
```

Use `LLVM_BIN` to select a tool directory, or `LLVM_CLANG`, `LLVM_OBJCOPY`,
`LLVM_NM`, `LLVM_SIZE` and `LLVM_OBJDUMP` for individual executables. The
identical-assembly crossover also accepts `LLVM_LLD` for standalone LLD.
owned route inherits compiler defaults and accepts existing `BADGE_*`
policy overrides. For the opt-in constant-selection policy:

```sh
BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants \
  node compile-graph/capture.mjs --capture compile-graph/build/constants
```

Choose up to four comma-separated LLVM IR function names for changed-pass
traces. Remarks and the fixed-IR comparison are captured without this option.

```sh
COMPILE_GRAPH_LLVM_TRACE=generate_maze,collide_wall \
  node compile-graph/capture.mjs --capture compile-graph/build/maze-optimizer tilt-maze
```

Open **Optimized IR** and select a function to filter its remarks and backend
metrics. The trace picker loads a bounded excerpt on demand; the full trace can
be large because LLVM includes module events even with a function filter.

## Six small RP2350 programs

For a narrower comparison than red square or maze, capture six independent UF2
images: arithmetic recurrence, branch/switch control, CRC32, indexed
read-only data, helper calls, and signed fixed-point scaling. Each links one C
translation unit with a shared assembly reset, harness, and RP2350 boot metadata.
`micro_kernel` receives the seed across a separate assembly call, so neither C
compiler can precompute the answer from the startup seed. No display, radio,
SDK, or library code is linked.

```sh
COMPILE_GRAPH_LLVM_TRACE=micro_kernel,mix \
  node compile-graph/capture.mjs --capture compile-graph/build/micro-demo \
  micro-arithmetic micro-control micro-crc micro-memory micro-calls micro-fixed-point
node compile-graph/check-micro.mjs compile-graph/build/micro-demo \
  > compile-graph/build/micro-demo/micro-results.json
python3 -m http.server 8000 --bind 127.0.0.1 --directory compile-graph/build/micro-demo
```

For a control and candidate, capture into separate directories with the same
six targets. Set `BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants` to measure
the opt-in backend instead of the qualified default. The LLVM flags are
unchanged.

The check also requires `qemu-system-arm` and can use `HOST_CC`, `LLVM_CLANG`,
`LLVM_LLD`, `LLVM_OBJDUMP`, and `QEMU_ARM`. It checks identical C tokens and
boot input bytes/relocations, verifies every UF2 block against its flash
payload, and runs the exact captured C objects on Cortex-M33 QEMU against
native results for five seeds. The JSON distinguishes raw flash, UF2 file
size, linked C function extents, decoded instructions and literal bytes,
and remaining flash bytes (read-only data and alignment). Reached instruction
counts exclude the common test harness and are **not physical timing**.
In the firmware, the shared harness uses `0x12345678` and writes the result
at `0x20000000`, then the completion word `0x444f4e45` at `0x20000004`.
There is deliberately no screen or USB output. Physical boot is not part
of the offline check.

The helper-call case isolates two differences: LLVM `-Oz` inlines `mix`,
and even on identical optimized IR its Thumb backend can use shifts as part
of a binary instruction. The opt-in owned selector now folds adjacent,
single-use 32-bit shifts into eligible arithmetic and logical operations.
With that change, the helper-call image's C functions shrink from 92 to 80
bytes and reach 245 rather than 290 QEMU instructions across the five inputs.
LLVM still reaches 80 instructions in the ordinary, inlined build. The
arithmetic case also improves (58 to 54 function bytes, 1,070 to 985 reached
instructions); the other four cases are unchanged.

### Follow-up: switch, literals, and indexed loads

The opt-in `thumb-constants` backend compares small switch cases directly
and folds branch pairs where the existing range and PHI-edge checks permit.
It can replace two-word materializations with a Thumb-16 PC-relative load
when a bounded tail pool, including alignment, does not grow code. It also
folds single-use byte-offset addresses into aligned indexed loads, including
adjacent sign or zero extensions. Low-bit masks and `0 - x` use direct
Thumb extract and negate forms. Division is allowed in the existing
stack-free leaf rewrite; it is not replaced by a shift.

Against the preceding opt-in capture:

| Program | Linked C function bytes | Reached QEMU instructions |
| --- | ---: | ---: |
| Arithmetic | 54 to 52 | 985 to 815 |
| Control | 90 to 80 | 901 to 684 |
| CRC32 | 70 to 64 | 2,290 to 1,970 |
| Indexed memory | 70 to 64 | 950 to 790 |
| Helper calls | 54, unchanged | 80, unchanged |
| Fixed point | 54 to 38 | 80 to 55 |

The 37-module fixed-IR comparison saved 2,446 executable bytes. No module or
measured stack resource grew, and the qualified default output was identical.
Fewer QEMU instructions do not establish a hardware speedup, especially where
a literal load replaces immediate instructions and introduces an XIP access.

## Explore

- **Project selector:** view one linked program at a time; translation units
  are filtered using its real linker inputs, including shared modules.
- **Firmware cards:** loaded binary extent, static RAM and section-layout gaps.
- **Function inventory:** name-matched functions ranked by object-code byte delta.
- **Milestones:** source, preprocessing, parser/typed AST, initial/optimized IR,
  assembly and object code, with downloadable original artifacts.
- **Automatic regions:** source and preprocessed function ranges,
  blocks/loops/conditionals, IR basic blocks and assembly labels.
  Each side has independent navigation.
- **Optimizer snapshots:** owned pass-prefix replays, verified to finish with
  the exact production optimized IR.
- **LLVM optimization diagnostics:** successful, missed and analysis remarks
  filtered by the selected function, with source locations when present.
  The line-table diagnostic object is compared with production allocated
  sections, relocations and public symbols. The status and raw YAML remain
  available; absence of remarks is not evidence of no optimization.
- **LLVM changed-pass trace:** optional, function-filtered excerpts from the
  actual Clang invocation, with the full artifact downloadable. Module-wide
  events are marked and are not attributed to an individual function.
- **Same-IR backend comparison:** owned optimized IR through both backends,
  disabling LLVM IR passes for the crossover. Inspect decoded instruction and
  literal bytes, direct frame accesses, register copies and calls in the
  Optimized IR tab. These are static counts, not dynamic spills or time.
- **Linked output:** disassembly, sections and symbols retained in the report.
- **Provenance:** commands, tool hashes, compiler policy and artifact SHA-256s.
- **Preprocessing insights:** distinguish line-marker/whitespace presentation
  from ordered token differences, with exact change groups, context and
  source conditional-compilation boundaries. Token alignment has a bounded
  work budget and explicitly reports when it cannot complete.
- **AST outlines:** hide serialization IDs and offsets while retaining
  compiler-specific node kinds, names, operators and available type evidence.
- **IR inventories:** compare static operation counts for the selected
  function or entire translation unit.
- **BA01 assembler crossover:** both assembly producers through both
  assemblers, under LLD and the owned linker. Project cards show loaded-byte
  controls; Object code shows normalized relocation/encoding evidence and
  retains local symbol-metadata differences.
- **BA02 assembler listings:** assembly/object milestones expose exact bytes,
  instruction widths, data, NOBITS, padding, relocations and branch/literal
  decisions for both assembly producers. Filter by section, function, line or
  address; jump to assembly or verified owned IR emission context and download
  the complete sidecar. Link-map placements are checked against ELF load
  segments and binary bytes, with relocation immediate fields masked.

Allocated section bytes are compared exactly except relocation immediate
fields, which are independently decoded to target sections/symbols and
effective addends. Unknown relocation types fail explicitly. Section
layout/alignment and public symbols must match. Code/data mapping-symbol
ranges include code padding; BA02 listings distinguish actual instructions,
alignment fill, data and generated metadata. Nonloaded metadata is reported separately.
Cross-linker differences are observations, never assembler failures.

Listings do not modify production objects: each capture verifies a second
assembly without listings byte-for-byte. Merged sections, discarded inputs and
ambiguous placements remain unmapped. Owned IR context comes from the existing
attribution build, admitted only for sections whose bytes, relocations and
public symbols match production. That trace can inhibit newer peepholes, so
some sections deliberately have no production IR mapping. Production LLVM
assembly currently has no source-location markers. Neither side invents
C-source lines or equates producer emission context with semantic ownership.
See the [BA02 contract](../badge-compiler/docs/badge-assembler.md#ba02--listings-and-source-address-evidence).

The analysis reuses the existing
[invocation parser and IR counters](../badge-compiler/tools/pipeline-stages.mjs)
rather than inventing another firmware build pipeline. The compiler wrapper
records the root Makefile's actual invocations.

## Interpretation

This first version compares **end-to-end pipelines**, not fixed-IR backends.
Clang's AST is both parsed and typed, so its two AST views share an artifact.
LLVM stage dumps are separate reruns with the production compilation flags;
they are not internal snapshots of the exact same compiler invocation.

Functions correspond by exact name within a translation unit. Source ranges
use Clang AST offsets; archived preprocessed inputs are parsed for their own
physical ranges. AST node counts and source/IR/assembly text sizes are
not comparable units or optimization scores. Missing functions are not
automatically classified as inlined or eliminated. Blocks are not paired by
label or order. Highlighting shows unique line text, not semantic differences.

For the current two-demo capture, the maze and its supporting C modules have
identical ordered preprocessing tokens despite different spacing and line
markers. Red square has 14 additional LLVM tokens: a `noinline` declaration
guarded by `#ifdef __clang__`. This is a real compiler-dependent input policy,
not evidence that one preprocessor optimized the code better. The token
comparison uses the same owned lexer for both expanded inputs; it establishes
lexical equality, not semantic equivalence.

Object-code sizes may include functions discarded at link time. Loaded
firmware bytes are distinct from ELF file size and UF2 packaging. Static RAM
does not include dynamic stack. Neither static counts nor size prove runtime
performance, correctness or hardware timing.

`--verify` checks archived artifact hashes and recomputes automatic AST, IR,
assembly and object regions without invoking either compiler. Failed captures
retain an `in-progress` or `failed` report and diagnostics instead of presenting
partial success.

## Tests

```sh
node --test compile-graph/tests/*.test.mjs
```

Include the real-demo accounting and relocation tests after capturing:

```sh
COMPILE_GRAPH_REPORT=compile-graph/build/demo \
  node --test compile-graph/tests/*.test.mjs
```

Those integration checks expect the default two-demo capture. The
`thumb-constants` campaign additionally checks its known image-size baseline.
