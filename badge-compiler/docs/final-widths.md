# PO17: selective final-width selection

PO17 implements the explicit backend policy `thumb-widths`, inheriting
`thumb-operands` and using the verified PO16 optimized IR. It reuses D04's
operand restrictions and D06's owned-assembler final layout, **not** their
historical profiles wholesale. Defaults were unchanged during this host
qualification; [PO18](current-promotion.md) subsequently qualifies hardware
timing and promotes the combined chain.

## Selection contract

[The Thumb helpers](../src/codegen_thumb.c) select narrower encodings only
without new register copies, spills, frame changes or instruction scheduling:

- A destructive register ADD uses flag-preserving Thumb-16 ADD when its
  existing destination equals either source. Ordinary high registers are
  allowed; SP/PC are excluded. Non-destructive ADD, SUB, shifts, logical
  operations and multiply retain the PO14 arithmetic forms. In particular,
  PO17 does **not** enable D04's flag-setting compact multiply/shift forms.
- Aligned word frame loads/stores use Thumb-16 only for low value registers
  and actual legal offsets: 0..124 from the retained R7 frame base, or
  0..1020 from SP. Existing final-frame calculation resolves incoming
  stack-argument offsets before making that choice.
- Direct byte/halfword/word accesses with existing low base/value registers
  use their narrow forms. High registers retain wide forms; byte fallbacks
  narrow only within their original reach. Alignment, access width,
  multiplicity and volatile order are unchanged.
- Low-register byte/halfword normalization uses flag-preserving UXTB/UXTH
  instead of UBFX. Other integer widths and high registers retain UBFX.
- Structured CFG edges, switches and shared-return branches omit forced
  `.w`. The owned assembler chooses widths from **final byte distances**,
  including alignment and embedded data, and retains its convergence,
  range, explicit-narrow and resource-budget diagnostics.
- PO14's flag-preserving MOVW/MOVT symbol materialization remains unchanged.
  Large symbolic addends still use branch-guarded literal islands; PO17
  lets the assembler select both their load width and guard branch width
  using final PC alignment and reach. High destinations retain legal wide
  literal loads. `$d` mapping symbols still identify embedded data.

All arithmetic substitutions preserve APSR, including live carry and
compare/IT windows: there is **no guessed flag-deadness permission**. Inline
assembly, barriers and interrupt hints are emitted unchanged. The constant
availability analysis recognizes the new forms, invalidating their written
registers without incorrectly discarding unrelated constants.

Register placement, scratch reservations, PHI transport and tail sharing
remain the existing qualified choices. A new move solely to reach a low
register would consume the two bytes saved by narrowing a single operation,
so this first policy keeps the original placement rather than buying an
unproved saving. Existing shared epilogues and PO16 forwarding decisions are
retained; shorter return branches are selected from their real distances.
No arbitrary block reordering, tail outlining, literal sharing or native
tail-frame reuse is added. Those are not prerequisites for this measured win.

The callable API exposes `BADGE_SELECTION_THUMB_WIDTHS`. CLI, owned pipeline,
build driver, benchmark report validation and comparison runner accept the
same policy and record it in provenance. Existing selections and implicit
defaults retain their behavior.

## Qualification and economics

[The persistent final report](../build/po17-qualified-widths/results.json)
uses hash-verified PO16 inputs and immutable baseline objects/images:

- **37 modules / 560 emitted functions / twelve ordinary images**.
  The 560-function inventory is PO16's finalized inventory, including its
  admitted clones, not an additional PO17 duplication.
- Thirty-six modules change; **35,070 executable bytes** are saved across
  the module inventory. These savings are not summed across duplicated
  occurrences in linked applications.
- Allocation/spill annotations, frame sizes, saved-register bytes,
  outgoing argument areas, function inventories and call graphs agree
  exactly with the control. Inline-assembly effect annotations agree too.
- All twelve linked images shrink. Data/BSS are unchanged. Every raw image
  is reconciled with its ELF flash-load segments and physical memory budgets.
  Object bytes are remeasured after owned assembly; loaded sizes include
  alignment and padding rather than adding nominal instruction savings.

| Image | PO16 loaded bytes | PO17 loaded bytes | Saved |
| --- | ---: | ---: | ---: |
| button_mapper | 10,536 | 9,220 | 1,316 |
| diagnostics | 47,116 | 41,580 | 5,536 |
| gpio_events | 8,696 | 7,672 | 1,024 |
| gpio_monitor | 6,900 | 6,064 | 836 |
| hello_idle | 436 | 388 | 48 |
| imu_demo | 24,932 | 22,484 | 2,448 |
| lower_mapper | 10,772 | 9,424 | 1,348 |
| peripheral_probe | 12,808 | 11,292 | 1,516 |
| screen_test | 4,764 | 4,212 | 552 |
| tilt_breakout | 17,888 | 15,344 | 2,544 |
| tilt_maze | 27,432 | 23,824 | 3,608 |
| usb_wifi_udp | 342,912 | 326,236 | 16,676 |

The [targeted tests](../tests/width-selection.test.mjs) compare 13,754 emitted
register/frame/normalization cases byte-for-byte with LLVM's assembler and
reject flag-setting substitutions. Reached execution covers volatile
byte operations, opaque assembly/barriers, signed division/remainder,
wide carry chains, comparisons/selects and global-compact/local/stack
residency. Bidirectional LLVM/owned calls cover signed narrow values and eight
arguments. An independent LLVM helper verifies the large-addend literal
pointer value. Existing exact branch/literal boundary tests check forward
and backward limits, widening cascades, PC alignment and illegal explicit
narrow requests.

Two actual production-source call graphs independently verify behavior:

| Reached QEMU batch | Control code bytes | PO17 code bytes | Control instructions | PO17 instructions |
| --- | ---: | ---: | ---: | ---: |
| USB reports / CRC / rejection paths | 5,198 | 4,424 | 16,549 | 16,549 |
| Wi-Fi peer filtering / comparison / clearing | 12,286 | 10,848 | 1,923 | 1,923 |

These batches use the complete default optimizer plus PO15 and PO16. The
Wi-Fi input and optimizer chain differ from PO16's earlier cross-function-only
test, so its instruction counts must not be compared with that earlier row.
Counts include startup, checking and semihost termination; a one-instruction
termination variation is allowed, not a speed-regression allowance.

Six targeted tests, two retained boundary tests and two persistent
verification/tamper tests pass. Driver forwarding/failure tests, default
equivalence and the shared backend API contracts also pass. Verification
rejects policy, ABI/allocation, input identity, object/image-byte and artifact
hash tampering.

### Efficient capture

The first independent capture generated candidate assembly and objects once
for all 37 modules and linked twelve affected images. It rebuilt **no**
production frontend, optimizer input or control image.

Final literal-boundary hardening generated identical production assembly.
The final report records regeneration plus exact, hash-checked reuse of all
37 objects and twelve images: **zero additional production assembly runs or
relinks**. Reuse requires the same verified baseline, newly generated
byte-identical assembly and identical assembler/link/conversion tools.
Earlier reports remain intact. Static verification runs no compiler, QEMU
or hardware campaign.

## Red-square extra oracle

[The separately reconciled extra oracle](../../build/red-square-po17-candidate/size-attribution.json)
uses unchanged C/optimized IR and archived startup/PICOBIN objects:

| Policy | Loaded bytes |
| --- | ---: |
| LLVM | 560 |
| Owned default | 1,432 |
| PO14 through PO16 candidate | 1,064 |
| PO17 candidate | **984** |

PO17 saves another 80 bytes, without application/startup rewrites. Its current
984 bytes are: vectors 64, PICOBIN 40, startup/fault halt 24, main GPIO/application
sequencing 396, command/byte transport 258, pixel fill 62, delays 30,
LCD configuration 102 and alignment 8. Each function's disassembly bytes and
constant data reconcile with the final image. No claim of beating LLVM
on this example is made.

## Size is not physical speed

The equal reached instruction counts are consistent with encoding changes,
not evidence of shorter badge execution time. Changed fetch addresses and
instruction widths can change RP2350 flash/XIP cache-line crossings, fetch
bandwidth, branch fetch behavior and cold/warm execution. Narrow instructions
need not execute faster. Historical D04/D06 speed regressions are not erased
by a newer size result.

Consequently PO17 is **host-qualified as a size candidate**, not promoted
or physically speed-qualified. No clock, flash configuration, scheduling,
software delay or GPIO strobe contract was changed. PO18 must qualify
affected flash/timing paths and cold boot before speed/default promotion,
using only the required baseline/candidate combination. No badge was
programmed or hardware campaign run for PO17.

## Usage

```sh
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function \
BADGE_CODEGEN_SCALAR_SELECTION=thumb-widths \
  bash badge-compiler/scripts/build-owned.sh all
```

Focused tests, a fresh capture and static verification (from the project root):

```sh
make -C badge-compiler test-width-selection
node badge-compiler/tools/width-selection-evidence.mjs --capture \
  badge-compiler/build/po17-new-capture \
  badge-compiler/build/po16-qualified-sharing/results.json
node badge-compiler/tools/width-selection-evidence.mjs --verify \
  badge-compiler/build/po17-qualified-widths/results.json
```

An optional fourth capture argument names a prior verified PO17 report for
exact-assembly/object/image reuse. `--verify-current` additionally validates
recorded source hashes; it does not silently recapture changed sources.
