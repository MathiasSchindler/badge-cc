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
