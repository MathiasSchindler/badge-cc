# PO08: assembler/linker control boundaries

PO08 is complete. It maintains equivalence and regression checks for the
RP2350 Cortex-M33 pipeline; it does not introduce pooling, relaxation or a
new compiler policy. LLVM is a comparison tool, not a required layout.

## Maintained contract

[`boundary-evidence.mjs`](../tools/boundary-evidence.mjs) consumes a verified,
full-corpus PO00 stage capture. It reassembles all 37 actual C-generated
assembly inputs and both preprocessed assembly sources with LLVM's assembler.
For each of twelve production images it links three object pools with both
the owned linker and LLD:

1. Original owned objects.
2. Ordinary whole-source Clang objects.
3. LLVM-reassembled copies of the exact owned assembly.

Every pair uses the identical linker script, object order, entry and
section-GC flags. Commands, inputs, maps, executables, load images and tool
hashes are archived. Capture refuses an existing output directory; failures
remain failed reports rather than success-shaped partial results.

The checked ELF32 ARM decoder compares allocated sections, public definitions,
instruction/data bytes and canonical relocations. Relocation fields and local
section references are normalized for assembler bookkeeping differences;
file symbols and incidental local mapping symbols are not loaded effects.
Malformed ranges, strings, symbol/relocation shapes and unsupported formats
are rejected explicitly.

The independent linked-value audit checks each retained relocation against
its input symbol and implicit addend. It covers ABS32, REL32, PREL31, Thumb
calls/jumps and MOVW/MOVT fixups, including branch displacement and weak
resolution. Nonrelocation input bytes must survive unchanged. Retained and
discarded nonmerge section identities must match across linkers, as must
function name/address/size/binding tuples.

Mergeable strings and fixed-entry constants are compared by **resolved
payload**, not by pointer addresses or string ordering. Interior string
references are checked through their terminating NUL. MOVW/MOVT address pairs
are decoded without destroying either half during the audit. Every synthetic
merged output entry must belong to the exact input-value set. All loaded
output contributions and remaining alignment gaps are accounted for;
unsupported or unexplained fill bytes fail.

NOLOAD inputs are checked separately, including production PROGBITS inputs
placed in NOBITS outputs. Output section addresses and segment load addresses
must fit the script's physical MEMORY regions. Entry resolution, flash/RAM,
scratch-bank and report-region budgets are checked, not inferred from total
file size.

## Actual-source results

Final evidence:
[`po08-qualified-boundaries-2/results.json`](../build/po08-qualified-boundaries-2/results.json),
using [`po08-stages/results.json`](../build/po08-stages/results.json).
The independently selected PO07 control uses SSA expression lowering,
`direct/global-compact/thumb-calls/word`, and the unchanged default optimizer
list. PO06 is not combined and none of these opt-in settings becomes a default.

| Check | Result |
| --- | --- |
| Exact assembly inputs | 39, including both assembly sources |
| Allocated input sections / relocations | 1,308 / 4,291 |
| Fresh linker runs | 72: 12 images x 3 pools x 2 linkers |
| Owned-object owned/LLD loaded images | Equal in all 12 |
| Same-assembly owned/LLD loaded images | Equal in all 12 |
| Reassembled versus original owned loaded images | Equal in all 12 |
| Function layout records compared per linker | 739 owned; 571 ordinary LLVM; 739 reassembled |
| Owned live relocation instances | 2,787 ABS32; 24 REL32; 2,754 Thumb calls |
| Ordinary LLVM merged-reference instances | 392 independently checked per linker |
| Largest audited branch displacement | 133,020 bytes |
| Owned mergeable allocated input sections | Zero |

Owned object files occupy 1,326,312 bytes versus 804,376 bytes for LLVM's
reassembly. This is object bookkeeping, not a firmware-size win: all twelve
loaded images are identical. The fresh maze loaded image is 34,900 bytes
under both assemblers and linkers. This exact-input boundary measurement is
not a comparison against historical PO07 firmware builds.

Ordinary LLVM objects produce eleven differing loaded byte layouts between
the linkers. All resolved values, nonrelocation code/data and function
layouts pass the audit. Only two images change load length:

| Image | Owned linker bytes | LLD bytes | Interpretation |
| --- | ---: | ---: | --- |
| button_mapper | 2,908 | 2,908 | Mergeable string placement |
| diagnostics | 16,900 | 16,880 | Mergeable data placement/pooling; 20 bytes smaller |
| gpio_events | 2,420 | 2,420 | Mergeable string placement |
| gpio_monitor | 2,420 | 2,420 | Mergeable string placement |
| hello_idle | 200 | 200 | Identical loaded bytes |
| imu_demo | 5,808 | 5,808 | Mergeable string placement |
| lower_mapper | 2,972 | 2,972 | Mergeable string placement |
| peripheral_probe | 4,500 | 4,500 | Mergeable string placement |
| screen_test | 1,764 | 1,764 | Mergeable string placement |
| tilt_breakout | 5,244 | 5,244 | Strings and 8-byte constants |
| tilt_maze | 8,724 | 8,724 | Strings and 4-byte constants |
| usb_wifi_udp | 255,120 | 254,940 | Strings and 4-byte constants; 180 bytes smaller |

These are **same LLVM objects** across linkers, not owned-versus-LLVM compiler
benchmarks. LLD gains no loaded bytes on owned objects because those objects
do not request mergeable sections. Matching its ordering would not optimize
the owned production pipeline. D06's shared-pool, alignment and relaxation
deferrals therefore remain in place.

## Validation and limits

Five focused maintained tests cover malformed objects, changed instructions
and fixups, corrupt resolved payloads, NOLOAD/GC/budgets, LLVM duplicate and
interior string references, fixed constants, loaded coverage and corrupted
persisted reports. Combined with the reused pipeline-stage suite, fourteen
tests pass. The final integration capture and persistent verifier also pass:
unit tests alone are not a substitute for running all actual-source routes.

Existing ASan/UBSan assembler, object and linker suites pass, including exact
branch boundaries, exhaustive MOV addend checks, weak/strong/common and
KEEP/GC behavior, RAM load addresses, negative scripts and atomic output
preservation. LLVM assembler/object/linker differential oracles pass. Native
tests cover relocation kinds absent from this corpus; they are semantic
fixtures, not selected performance evidence.

Earlier auditor probes remain archived under the `po08-boundaries*` and
`po08-qualified-boundaries` build directories. They exposed auditor mistakes
around NOLOAD, nonallocated bookkeeping, paired-MOV reads and function-helper
scope; those were corrected before final qualification. They are not hidden
compiler failures or accepted fallback routes.

This is a host boundary audit, not proof of application timing, interrupt
safety, cold/peripheral behavior or whole-program stack bounds. Both badges
were untouched and remain restored to their previously hash-verified original
images in BOOTSEL. No new speed improvement is claimed.

The current merge audit supports byte strings and fixed-entry constants;
unsupported global merge references, non-byte strings or ambiguous MOV pairs
fail explicitly. Alignment-fill auditing is scoped to the current production
scripts, including their four-byte tail alignment and explicit flash-end
fill. No veneer or linker-relaxation support is invented. GC agreement is not
proof that discarded code is unreachable under every possible environment.
PO08 explains the assembler/linker reports, not all earlier frontend/IR
differences and rejected crossovers in PO00.

## Reproduce

From the workspace root, with the compiler tools built and LLVM/LLD on PATH:

```sh
PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
  node badge-compiler/tools/boundary-evidence.mjs --capture \
  badge-compiler/build/po08-new-boundaries \
  badge-compiler/build/po08-stages/results.json

node badge-compiler/tools/boundary-evidence.mjs --verify \
  badge-compiler/build/po08-new-boundaries/results.json

make -C badge-compiler test-boundary-evidence
```

To generate new inputs, use the maintained PO00 full-corpus capture with the
same explicit policies. Verification hashes the source capture and evidence
tools and recomputes the object, linked-value, layout and physical-budget
checks; stale evidence must be recaptured, not edited into qualification.
