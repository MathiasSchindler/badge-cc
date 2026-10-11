# Red-square firmware: current bytes and conceivable minima

Measured on 2026-10-11 from the freshly compiled owned image:
**992 loaded bytes**, using the promoted SSA/eight-pass/
direct/global-compact/thumb-widths/word policy. That image was flash-verified
and started on badge `08DA26F86E57F5EA`. Its visible output was not separately
confirmed during this deployment.

The component table below retains that **promoted-policy baseline**, not the
latest opt-in candidate. The subsequent
[RS06 counted-delay campaign](#rs06-implementation-and-qualification)
reaches **624 loaded bytes**, with a **34-byte fill**, retaining the
**20-byte command wrapper** and matching LLVM's **40-byte interpreter**
and **20-byte initialization-delay helper**.
Both badges passed RS06 functional testing and one cold boot each.
Electrical timing is unmeasured.

The program draws a black 320 x 240 background and a centered 64 x 64 RGB565
red square. It uses no target libc, SDK, framebuffer, USB, IMU or radio code.
The [source](../../src/tools/red_square.c) uses specialized LCD helpers rather
than linking the full shared display driver.

## Component table

The last column distinguishes a **known encoding size**, a **demonstrated
upper bound on the minimum**, and an **unqualified possibility**. We have not
proved the globally smallest working firmware, so an exact minimum for every
algorithm cannot honestly be supplied.

Unless explicitly identified as a safety tradeoff, the requirements are:
cold boot without retained panel state, full LCD configuration, correct GPIO
transactions, sufficient initialization waits, and the current fault/interrupt
guards. "Necessary" refers to the work, not necessarily a separate function.

| Component | Bytes as-is | Absolutely necessary? | Lowest conceivable byte size / evidence |
| --- | ---: | --- | --- |
| Core exception vector table | 64 | Yes under the current safety contract: all 16 slots and fault vectors are retained. | **64** with that layout. **8** for only initial SP and reset PC is a theoretical safety-reduced alternative, not a qualified usable vector table. |
| RP2350 PICOBIN boot metadata | 40 | Recognizable boot metadata is necessary for this flash-boot route; the current two-block arrangement is not proved minimal. | **20** for a single block is a format/layout candidate, not a tested bootable image. **40** is the demonstrated arrangement. |
| Startup and fault halt | 24 | A valid entry, stack and safe execution state are necessary. The exact instruction sequence is not inherently mandatory. | Exact minimum **unknown; at most 24** with the retained guards. Dropping interrupt masking, VTOR setup or fault handling would change the safety contract, not just improve the compiler. |
| GPIO setup and application sequencing (`bare_main`) | 396 | Yes: configure pins/power, initialize the panel, select windows, draw, enable the backlight and stop. | Exact minimum **unknown; at most 184** is demonstrated by LLVM on the same current C program. Shared addresses, masks and register values may permit further reductions. |
| LCD byte transport (`write_byte`) | 74 | The byte transaction and strobe timing are necessary; a callable helper is not. | As a separate symbol, **0** if folded into other code; the work remains elsewhere. When retained as a helper, **32** is demonstrated by LLVM; the absolute minimum is unknown. |
| Single-command helper (`command`) | 56 | Command/data selection and sending the command are necessary; this separate wrapper is not. | **0** as a separate wrapper if merged with transport/interpreter code. **20** is now demonstrated by the owned compiler with the wrapper retained and unchanged stack demand; LLVM demonstrates **24**. The absolute minimum remains unknown. |
| Command-table interpreter (`commands`) | 128 | Some way to emit configuration/window transactions is necessary; a table interpreter is not the only implementation. | **0** as a separate interpreter if replaced by direct transactions, which add code elsewhere. **40** is demonstrated by both LLVM and the opt-in owned compiler with this interpreter retained. |
| Repeated-pixel fill loop (`fill`) | 62 | Pixel generation and transfer are necessary for this full-background/full-square contract; a separate function is not. | **0** as a separate symbol if integrated into sequencing. **34** is demonstrated by the opt-in owned compiler with the helper retained, beating LLVM's **36**. A nonzero amount of loop/transfer code must still exist. |
| Reset and sleep-out delays (`delay_cycles`) | 40 | Yes: initialization waits must not disappear just because generated code becomes faster. | Exact minimum **unknown; at most 20** is demonstrated by both LLVM and the opt-in owned compiler with the current six-NOP-per-iteration minimum-cycle primitive. **0** as a separate helper is possible only if the waits are implemented elsewhere. |
| LCD configuration and window tables | 102 | The configuration values and windows are necessary for the retained display contract; literal tables are not inherently necessary. | **102** if the exact existing byte tables are retained. **0 data bytes** if all values are encoded/generated in instructions, but instruction size grows; no total-size win is established. |
| Alignment padding | 6 | Only the padding actually required by the selected section/instruction layout is necessary. | **0** is conceivable with a different compatible layout. It is not guaranteed independently of all other component sizes. |
| **Total loaded image** | **992** | A complete working combination is necessary; individual row minima are not independent. | The absolute minimum is **unknown**. **568 bytes** is demonstrated by the current LLVM build with the same source, startup and delay safety. Do **not** sum the hypothetical zero-cost helpers into a claimed working minimum. |

The 8-byte-vector and 20-byte-PICOBIN possibilities are **not approved size
optimizations**. They require boot-ROM/layout investigation and independent
hardware qualification; the vector reduction also sacrifices the currently
retained fault-vector coverage. This document does not authorize either change.

## What accounts for the compiler gap?

All six owned C functions reconcile to **756 bytes of instructions**, with
**zero literal-pool bytes**. The remaining 236 bytes are vectors, boot metadata,
startup, LCD tables and alignment.

The current LLVM image is **568 loaded bytes**, versus our **992**:

| Generated C component | Owned | LLVM | Owned excess |
| --- | ---: | ---: | ---: |
| GPIO/application sequencing | 396 | 184 | 212 |
| Byte transport | 74 | 32 | 42 |
| Command wrapper | 56 | 24 | 32 |
| Table interpreter | 128 | 40 | 88 |
| Pixel fill | 62 | 36 | 26 |
| Initialization-delay helper | 40 | 20 | 20 |
| **Generated C subtotal** | **756** | **336** | **420** |

The complete image gap is **424 bytes**: 420 bytes of generated C plus four
bytes of additional alignment. Startup, vectors, boot metadata and table
contents are identical in size.

Within our generated C, constant/address materialization accounts for
**324 instruction bytes**, including **240** in `bare_main`. These are
instructions such as building MMIO addresses and GPIO masks, not a separate
324-byte constant table that can simply be deleted. Reusing existing register
values, choosing cheaper immediate forms and reducing transport/loop overhead
are general compiler opportunities.

LLVM's sizes demonstrate achievable outputs, **not theoretical floors or a
requirement to copy LLVM's implementation**. A better shared transport or
different register assignment could beat an individual LLVM component.

## Compiler improvement milestones: ToDos

The six milestones are **not all complete**. RS02-RS06 have user-approved
functional hardware qualification. RS01's sequencing target remains open.
Each aims
to match or exceed LLVM quality through reusable compiler improvements, not
hand-written replacements, source-name recognition, fixed-answer substitution
or fixtures that only work for this demo. Red-square is an extra size oracle;
ordinary badge programs must establish generality.

### Shared acceptance contract

- Compare identical source and explicit target settings with the current
  owned baseline and LLVM. Use identical IR/backend crossovers where necessary
  to distinguish frontend, optimizer, allocation and encoding costs.
- The per-component byte targets below are current LLVM reference sizes,
  not proved minima. Aim for equal or smaller loaded code without a major
  runtime, RAM, stack, compilation-cost or hardware-timing downside.
  If helpers are merged, cloned or inlined, attribute their work across all
  callers and charge constants, padding and retained generic bodies.
  Disappearing symbols alone do not satisfy a target.
- Reuse existing typed analysis, effect models, allocator, target-cost and
  assembler helpers. Extend their shared contracts rather than layering a
  special red-square emission path over them.
- Preserve C semantics, AAPCS, volatile access width/order/multiplicity,
  assembly clobbers, fault guards and explicit timing minima. Test boundaries,
  alternate constants/counts, signedness, alignment and register pressure.
- Use targeted host/LLVM/QEMU checks first, then the affected ordinary corpus,
  linked-image/resource and mixed-ABI checks. Reuse hash-verified unchanged
  inputs and images; do not repeat a historical full matrix after a passing
  documentation or host-only change.
- Qualify newly affected physical timing paths with bounded baseline/candidate
  measurements and the existing exact noise gates. Promote generally qualified
  improvements consistently through API, CLI and build drivers; retain or
  mitigate candidates with major downsides.

All documented RP2350 Arm Cortex-M33 facilities are available for investigation:
Thumb-16/Thumb-2 forms, legal addressing modes, flag/IT selection, DSP/bitfield
instructions, hardware division, memory placement and peripheral capabilities.
Availability, privilege, ABI, memory effects and actual byte/runtime economics
must be verified for this target, not inferred from the instruction name.
PIO, DMA, timer or SIO-based alternatives are also permitted where useful,
but offloading arbitrary C MMIO loops changes observable behavior. Such routes
need an explicit reusable platform operation/intrinsic with initialization,
ownership, timing and fallback contracts, plus total setup/code/RAM costs.
They must be reported separately from unchanged-source compiler improvements.

### RS01 - GPIO/application sequencing

**Baseline/target:** 396 owned bytes; match or beat the current 184-byte LLVM
reference while retaining GPIO setup and complete application behavior.

- [ ] Attribute repeated address/mask materialization, call-live values and
  sequencing overhead in the actual emitted code and unrelated MMIO drivers.
- [ ] Improve bounded constant/address synthesis and reuse across proven CFG
  edges and call-clobber boundaries; evaluate loop strength reduction and
  shared register bases against actual allocation/encoding costs.
- [ ] Select cheaper legal immediate, shift, bitfield and addressing forms
  without combining, removing or reordering volatile transactions.
- [ ] Qualify on GPIO setup, diagnostics and ordinary control-heavy code;
  report complete-image bytes, reached runtime and frame/save/spill deltas.

#### RS01 implementation and qualification progress

The explicit `thumb-constants` backend inherits the promoted `thumb-widths`
policy. It is available through the public backend API, CLI and
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants` build setting. It does not
recognize this program, GPIO addresses or particular table contents.

Implemented general mechanisms:

- Reuse the existing modified-immediate encoder to select flag-preserving
  `MOV.W`/`MVN.W` instead of an eight-byte `MOVW`/`MOVT` pair when cheaper.
- Keep bounded block-local constant/address availability in functions that
  contain opaque assembly. Clear facts at opaque assembly, calls, labels and
  unknown effects; do not carry them through unproved CFG/call boundaries.
  Counted assembly boundaries preserve the exact user text, including blank
  lines and comments that resemble compiler annotations.
- Fold adjacent constant-to-temporary-to-destination transport only when an
  immediately following materialization overwrites the temporary before use.
- Use narrow `MOVS` only immediately before a generated `CMP` which overwrites
  NZCV. This is not general permission to change flags.
- Select flag-neutral shifted adds for single-use i32 power-of-two
  multiply/add pairs under global residency, avoiding the multiplier register
  and its materialization.
- Compare i32 values directly from allocated registers, using narrow or
  modified-immediate `CMP` forms when legal. Other types retain the existing
  normalization/sign-extension path.
- Forward a single-use boolean PHI into unconditional predecessor edges when
  its block contains only that PHI and its conditional branch. Constant
  incoming values select an edge; variable incoming values become predecessor
  conditions and can use existing compare/branch fusion. Reject effectful
  joins, additional PHI uses, ambiguous predecessor lists and destinations with
  PHIs. Perform this before allocation so unnecessary boolean transport and
  PHI scratch storage disappear, rather than carrying flags across CFG edges.
- Remove branch-only trampoline blocks only when all references are ordinary
  branch targets and the destination has no PHIs. Retain entry blocks, self
  loops and PHI predecessor identities. Lower literal i1 branches directly
  without materializing and comparing the already-known condition.
- Extend address reuse to flag-neutral modified-immediate `ADD.W`/`SUB.W`
  deltas, reusing the target-cost helper. This covers offsets such as 65,536
  bytes without extending any register's lifetime.
- Use narrow small-argument materialization before a direct AAPCS call, across
  at most eight flag-neutral register/constant moves. AAPCS does not consume
  incoming NZCV. Do not cross comparisons, carry consumers, IT, assembly
  annotations or other unknown effects.
- Extend the NZCV-dead proof across at most eight neutral instructions and
  64 text lines, including unconditional generated CFG edges. A generated
  `CMP` or direct AAPCS call must kill incoming flags before any consumer.
  Conditional branches, IT, carry consumers, assembly and unknown effects end
  the proof; cycles exhaust the budget rather than being assumed safe.
  This permits narrow small constants and small `ADD`/`SUB` immediates in more
  ordinary control code. Already flag-setting `ADDS`/`SUBS` may also use legal
  narrow forms, which preserve the complete NZCV result.
- Replace a generated conditional-edge/unconditional-branch pair with one
  inverse conditional branch only when its destination is proved within a
  conservative **240-byte** span. Count each ordinary instruction as at most
  four bytes; refuse spans crossing assembly, expandable incoming-frame
  annotations or unmodelled directives. Keep the local-edge label and retain
  the original long-range sequence when the bound cannot be proved.
- Fold a shifted address add into an indexed word load/store when the address
  temporary is overwritten before any later use. Restrict shifts to 0-3,
  require distinct temporary/base/index registers, and keep base/index values
  unchanged until the memory operation. Search at most 16 intervening lines
  and 64 lines for the overwrite; reject labels, calls, opaque assembly,
  partial-register writes and unknown effects. Retain exactly one memory
  transaction at its original position. Charge widening a narrow memory
  instruction to Thumb-2, so the net saving is two or four bytes.
  Run this before constant selection so the shorter stream can participate
  in the existing bounded flag proof.

RS01 fixed-IR results before the RS02 extension below, compared with the
deployed/promoted baseline:

| Component | Promoted owned | RS01 candidate | LLVM |
| --- | ---: | ---: | ---: |
| GPIO/application sequencing | 396 | 244 | 184 |
| Byte transport | 74 | 56 | 32 |
| Command wrapper | 56 | 44 | 24 |
| Table interpreter | 128 | 112 | 40 |
| Pixel fill | 62 | 52 | 36 |
| Initialization-delay helper | 40 | 32 | 20 |
| **Generated C instructions** | **756** | **540** | **336** |
| **Complete loaded image** | **992** | **772** | **568** |

These changes save **152 bytes (38.4%)** in sequencing and **220 loaded bytes
(22.2%)** overall. Relative to the first host-qualified candidate, sequencing
improves from 322 to 244 bytes and the image from 868 to 772 bytes.
This address-folding round improves the preceding 248/776-byte candidate to
244/772 bytes. The candidate still exceeds the RS01 sequencing target by
**60 bytes**, and the complete LLVM image by **204 bytes**. No work was moved
to hidden helpers; vectors, boot metadata, startup and the 102 table bytes
remain unchanged. Alignment is two bytes, versus six in the promoted baseline.

The frozen production corpus covers **37 modules and 559 functions**, saving
**44,020 executable bytes**; all **12 production images** shrink. Examples:
GPIO monitor 6,020 -> 4,972, diagnostics 41,536 -> 35,596, and maze
23,780 -> 20,040 loaded bytes. This round adds 16 executable bytes of savings
to the preceding corpus result; the address-lifetime proof intentionally
rejects cases needing broader liveness reasoning.
Frame, save and outgoing-argument bytes do not
grow; calls, initialized data and BSS remain unchanged. These are static
resource checks, not measured dynamic stack high-water marks.

Validation includes native ASan/UBSan assertions, 169 owned/LLVM encoding
comparisons, reached flag/value checks, all 32 multiply/add shifts in both
operand orders, signed/unsigned comparison boundaries, volatile-RAM joins,
loops and opaque assembly across three residency modes. The independent LLVM
panel oracle also executes the candidate, checking GPIO/application sequencing
and every pixel transaction. The six explicit delay NOPs remain present.
Additional reached tests cover forwarded PHIs, multi-use/effectful joins which
must not be forwarded, trampoline removal, loop PHIs which must retain their
predecessor identities, literal false branches, and carry-preserving large
address deltas. Candidate and control execute these under three residency
modes with an independent LLVM-built caller and expected-value oracle.
The branch-span test checks both the accepted 240-byte conservative bound
and the first rejected case above it. Arithmetic runtime checks compare all
four NZCV flags and results against explicit wide instructions at signed and
unsigned boundaries. The complete runtime assembly is also byte-compared
against LLVM, not just the 169 constant encodings. Carry checks use the
Cortex-M33 register-operand `ADC` form.
Indexed-address tests execute word loads and stores for all four legal shift
values and compare the resulting assembly bytes with LLVM. Negative cases
retain temporaries that remain live, alias the base, feed the stored value,
cross a label/opaque assembly, require a shift above three, or are only
partially overwritten.
Six targeted tests, fifteen driver/default tests, the pipeline test and the
persistent report's recomputation/tamper test pass.

The capture reused the already-qualified optimized IR, verified unchanged
control assembly, compiled each module once per backend policy, assembled
only candidate objects and reused startup/linker inputs. Its recorded
codegen/assembly/link/inspection commands took approximately **1.28 seconds**
on this host; this excludes tests and compiler rebuilding and is not a
controlled compile-time speedup measurement.

**RS01 remains incomplete and is not promoted.** Remaining work includes
more general boolean/PHI forwarding and branch layout in the GPIO loop, costed shared bases
and call-live allocation, and proven CFG/call-boundary reuse. Loop strength
reduction is not implemented by this local encoding candidate. Reached
production runtime and physical GPIO/LCD timing still need qualification
before default promotion. Shorter setup code alone does not prove electrical
timing safety. Neither badge was changed for this candidate; the original
component table still describes the deployed 992-byte image.

The [current report](../build/rs01-address-selection/results.json)
includes source/tool snapshots, hashes, exact input IR, logs, assembly, linked
images and resource facts. Validation logs are copied into the evidence
directory so subsequent test runs cannot overwrite them.
The [preceding 776-byte report](../build/rs01-edge-selection/results.json)
has an append-only
final-validation record includes the strengthened independent runtime-encoding
oracle, test sources and native test binary; finalization verified the
backend binary was unchanged and reused the captured corpus without rebuilding.
The [preceding 808-byte report](../build/rs01-flow-selection/results.json) and
the [preceding 868-byte report](../build/rs01-constant-selection-final/results.json)
are retained and still verify. Verify the current report without rebuilding
or touching hardware:

```sh
node badge-compiler/tools/constant-selection-evidence.mjs --verify \
  badge-compiler/build/rs01-address-selection/results.json
RS01_REPORT="$PWD/badge-compiler/build/rs01-address-selection/results.json" \
  node --test badge-compiler/tests/constant-selection-evidence.test.mjs
```

### RS02 - Byte transport

**Baseline/target:** 74 owned bytes; match or beat the 32-byte LLVM reference
for the same byte transaction and strobe contract.

- [x] Separate register preservation, address/mask setup, byte normalization
  and volatile-store costs; identify avoidable work in general small MMIO leaves.
- [x] Improve scratch-safe leaf allocation, constant sharing and direct
  load/store selection while honoring incoming ABI values and assembly effects.
- [x] Evaluate legal SIO register aliases or explicit bulk-transfer contracts
  only when their effects are proved equivalent or deliberately exposed by a
  reusable platform API; do not silently substitute hardware offload.
- [x] Test byte boundaries, high/low-register pressure and clobbers; qualify
  reached LCD/other affected I/O timing without shortening required strobes.
  Completion uses the explicitly approved functional-hardware scope below;
  electrical pulse widths and margins were not measured.

#### RS02 implementation and host qualification

RS02 extends the existing explicit `thumb-constants` candidate rather than
creating another largely identical backend policy. Qualified `thumb-widths`
defaults remain unchanged. The original host campaign did not flash either
badge; the subsequent physical campaign is recorded below.

The general selector recognizes **linear scalar leaf store streams**, not
LCD helpers or SIO addresses. It accepts a void leaf with zero or one i32
argument, single-use literal `inttoptr` addresses, aligned i32 stores,
fallthrough-only unconditional edges and known operand-free NOP assembly.
It rejects other calls, opaque assembly, flag consumers, computed or escaping
addresses, multiple arguments, wider values and non-linear control flow.
Accepted addresses must be above 65,535, word-aligned and within a 124-byte
span. This restriction avoids adding base setup to already cheap small
addresses and keeps every low-register word-store offset directly encodable.

For an accepted stream, `r1` is provably free of incoming arguments, allocated
values and hidden instruction scratch uses. The selector materializes one
common base in `r1`, selecting an aligned modified-immediate base when cheaper,
otherwise the lowest address. It emits each original store with its own
offset and original value **at the original position**. Small literals use
`MOVS` because the entire admitted leaf has no NZCV consumers, and AAPCS does
not preserve flags on return. Existing constant reuse retains repeated values.
Unused pointer temporaries disappear from the emitted body, allowing the
existing finalized-frame pass to remove their callee-saved register traffic.

The byte bus still uses the same four ordered i32 SIO writes: clear the data
mask, set the argument value, raise WR, lower WR. The three NOPs before lowering
WR and the two after it are retained exactly. No SIO alias substitution,
packed store, DMA, PIO, source modification or transaction merging is used.

| Component | Promoted owned | After RS01 | RS02 combination | LLVM |
| --- | ---: | ---: | ---: | ---: |
| Byte transport | 74 | 56 | **32** | 32 |
| Transport saved-register bytes | 4 | 4 | **0** | 0 |
| GPIO/application sequencing | 396 | 244 | 244 | 184 |
| Generated C instructions | 756 | 540 | **516** | 336 |
| Complete loaded image | 992 | 772 | **748** | 568 |

RS02 saves **24 bytes** beyond RS01 in the byte helper and complete image.
The retained helper now **matches the 32-byte LLVM target**. Work has not moved
to callers or tables; other helper sizes, vectors, startup, boot metadata,
configuration data and two alignment bytes are unchanged.

Seven targeted tests pass, including native sanitizer and inherited RS01
tests, independent LLVM assembly-byte comparisons, the instrumented full
panel/pixel oracle, and direct absolute-RAM execution of the admitted store
stream. Store offsets are checked in order. Argument cases include 0, 1, 255,
256, `0x80000000` and `0xffffffff`; an LLVM-built caller and an assembly
sentinel wrapper verify preservation of r4-r11. Offset 124 is admitted and
128 falls back. Two-argument functions, ordinary calls and opaque memory
assembly also retain the previous lowering and execute correctly.

The reached RAM/ABI oracle executes **1,119 -> 987 instructions** under QEMU,
an **11.8%** reduction for the combined candidate versus `thumb-widths`.
This is not an isolated RS02 speedup or a measurement of LCD electrical timing.
The instrumented panel oracle replaces MMIO with observer calls, so it checks
application transactions but does not itself exercise this leaf selector;
the direct RAM oracle and captured real helper assembly cover that path.

The frozen 37-module / 559-function production corpus remains at **44,020
executable bytes saved** by the combined candidate relative to the promoted
baseline. RS02 adds **no further production-corpus byte reduction**: those
modules do not meet this bounded leaf shape. All twelve images retain their
RS01 sizes, with no additional frame/save/outgoing growth, calls or data/BSS.
This is deliberately limited admission, not a claim to have optimized all
MMIO drivers. Fifteen driver/default tests, the pipeline test and persistent
RS01/RS02 evidence/tamper tests also pass.

The [RS02 report](../build/rs02-byte-transport/results.json) includes the
preceding RS01 report, source/tool snapshots, identical input IR, function
resource facts, complete-image accounting, copied validation logs and a final
strengthened transaction-order test record. Its captured tool commands took
approximately **1.74 seconds**, excluding tests and compiler rebuilding.
Final validation reused the unchanged backend and corpus rather than building
the firmware again.

The original immutable host report retains its
`host-qualified-hardware-pending` status. The subsequent physical result is
stored separately rather than rewriting historical evidence.

```sh
node badge-compiler/tools/byte-transport-evidence.mjs --verify \
  badge-compiler/build/rs02-byte-transport/results.json
node --test badge-compiler/tests/byte-transport-evidence.test.mjs
```

#### RS02 physical acceptance - 2026-10-11

**RS02 is complete under the user's explicitly approved functional-hardware
acceptance scope. Electrical timing remains unmeasured.** No oscilloscope or
logic analyzer was available; successful display operation is not a waveform
measurement or a quantified timing-margin claim.

The [hardware campaign](../build/rs02-hardware/results.json) used the exact
cached **748-byte RS02** and current timing-hardened **568-byte LLVM** images.
No recompilation or relinking was needed. Both 16 MiB original flash contents
were saved, read-verified and hashed before writing either badge. UF2 loads
were write-verified and compiler roles were swapped between the two boards.

| Round | Badge 08DA26F86E57F5EA | Badge 9BBF88DD17D66E7D | User-observed result |
| --- | --- | --- | --- |
| 1 | Owned RS02 | LLVM control | Both correct initially and after three cold boots each |
| 2 | LLVM control | Owned RS02 | Both correct initially and after three cold boots each |

The user confirmed **12 cold boots total**, a centered red square on black,
and no failures in either round. This qualifies the compact transport's actual
display initialization and rendering on both physical boards; it does not
claim physical coverage of arbitrary other peripherals or store streams.

Independent inspection of the linked images verifies the same WR pulse
instruction shape: three NOPs, the original deassertion store, two NOPs and
`BX LR`. The owned store uses `[r1, #24]` with base `0xd0000000`; LLVM uses
`[r1]` with base `0xd0000018`, accessing the same SIO register. This establishes
preservation of the explicit strobe path, not absolute nanosecond timing:
flash fetch behavior, write completion and electrical margins were not
measured. Initialization still retains the six-NOP delay contract.

The reusable [hardware runner](../tools/byte-transport-hardware.mjs) records
backups, image hashes, write verification, role swaps, observations and explicit
acceptance. Its offline verifier checks ELF/raw/UF2 byte agreement, helper size,
strobe instructions, observation counts and scope. A tamper regression test
rejects altered images, roles, backup hashes, cold-boot counts and false
electrical-qualification or default-promotion claims.

At the end of RS02, the user requested leaving the **round-2 comparison pair**:
LLVM on `08DA26F86E57F5EA`, owned RS02 on `9BBF88DD17D66E7D`.
That deployment is superseded by the RS03 campaign below.
The originals were preserved, not restored. To restore them later, place both
badges in BOOTSEL and use the runner's `--restore`; it writes and hash-verifies
complete flash readbacks and leaves both in BOOTSEL.

**No default promotion:** `thumb-constants` includes unfinished RS01 work,
and this targeted qualification does not establish the entire combination's
production runtime or every affected peripheral's electrical timing.

```sh
node badge-compiler/tools/byte-transport-hardware.mjs --verify
node --test badge-compiler/tests/byte-transport-hardware.test.mjs
```

### RS03 - Command wrapper

**Baseline/target:** 56 owned bytes; match or beat the 24-byte LLVM reference,
charging any work moved into callers or the byte-transport helper.

- [x] Attribute save/restore, argument transport and repeated constants around
  the wrapped call in this helper and ordinary wrapper/callback code.
- [x] Improve costed wrapper inlining, call-aware residency or parameter
  forwarding using proven call edges and whole-module costs.
- [x] Investigate native tail-call/frame reuse only for genuinely eligible
  call/return paths; this wrapper's post-call DC store must not be discarded
  to manufacture tail-call eligibility.
- [x] Preserve public/address-taken/indirect-call ABI behavior and ordered
  pre/post-call effects; test both owned/LLVM ABI directions and pressure cases.

#### RS03 implementation and host qualification

The explicit `thumb-constants` candidate now lowers general **ordered scalar
call wrappers**. This is an instruction selector, not command-name/address
recognition, a source rewrite, forced inlining or a helper-specific calling
convention. The optimized public entry remains callable by ordinary LLVM or
owned callers, including through a function pointer.

Admission requires a void, stack-free scalar wrapper with exactly one i32
argument, used only by one direct void call that forwards it unchanged.
The remaining instructions must be single-use literal `inttoptr` addresses,
aligned i32 stores of one common constant, forward fallthrough-only branches,
and a final void return. There must be stores **both before and after** the
call. Addresses must be above 65,535 and within a 124-byte span. Unknown
assembly, indirect callees, multiple calls/arguments, computed addresses,
conditional control, escaping/shared pointers, narrow arguments and returned
values retain the existing lowering.

The selector preserves the common base in **r4**, leaves incoming **r0**
untouched through the pre-call stores, and forwards it directly to the
original callee. Constant values use caller-saved **r1**, rematerialized after
the opaque call. Nothing assumes that the callee preserves r0-r3, r12 or
NZCV. The existing frame finalizer saves/restores r4 and LR with 8-byte stack
alignment. No register-allocation policy or public ABI changes are needed.
The wrapper has no internal flag consumers; AAPCS does not require incoming
or outgoing NZCV preservation.

Keeping the store value in another callee-saved register was deliberately
avoided: its preservation and alignment would grow saved-register space.
The selected rematerialization strategy matches LLVM's byte target **without
that RAM cost**. A tail call is not legal here because the original post-call
DC store must execute; it is retained at its original position.

| Wrapper cost | After RS02 | RS03 |
| --- | ---: | ---: |
| Push/pop instructions | 8 | 4 |
| Incoming/outgoing argument copies | 4 | 0 |
| Address setup | 16 | 4 |
| Constant value setup | 8 | 8 |
| Two ordered stores | 4 | 4 |
| Original direct call | 4 | 4 |
| **Complete command helper** | **44** | **24** |
| Saved-register space | 8 | 8 |
| Local frame / outgoing argument space | 0 / 0 | 0 / 0 |

| Component | Promoted owned | RS02 combination | RS03 combination | LLVM |
| --- | ---: | ---: | ---: | ---: |
| Command wrapper | 56 | 44 | **24** | 24 |
| Byte transport | 74 | 32 | 32 | 32 |
| GPIO/application sequencing | 396 | 244 | 244 | 184 |
| Generated C instructions | 756 | 516 | **496** | 336 |
| Complete loaded image | 992 | 748 | **728** | 568 |

The full **20-byte** improvement stays in the wrapper: callers, byte
transport, other helpers, startup, vectors, boot metadata, tables and two
alignment bytes are unchanged. Initialized data and BSS remain zero.

Eight targeted tests pass, including the inherited sanitizer/flag/CFG,
transport and complete panel-oracle tests. The new direct-RAM wrapper oracle
tests seven unrelated literal values (including 0, 255, 256, the high bit and
all ones), six incoming values, offsets 124/128, and all listed fallback
shapes. A callee observer checks the pre-call store and absence of the
post-call store, then deliberately clobbers all volatile registers and NZCV.
The caller checks the final post-call store, forwarded value, callee-entry
stack alignment and preservation of r4-r11.

LLVM-built and owned-built callers/observers run with both owned policies and
LLVM-built wrapper IR: six linked execution configurations cover the ABI in
both directions, public/address-taken calls and fallback behavior. Every
owned emitted encoding is compared with independent LLVM assembly. Per-function
executable bytes, frame/save/outgoing costs and call targets cannot grow in
the wrapper test. A non-modified-immediate RAM base costs eight setup bytes,
so its otherwise equivalent mask wrapper is 28 bytes; the real MMIO wrapper's
four-byte base materialization reaches **24 bytes**.

The combined wrapper oracle executes **7,959 -> 7,580 instructions** under
QEMU, **4.8% fewer** than `thumb-widths`. This includes the harness and fallback
cases, as well as inherited RS01 mechanisms; it is not an isolated RS03
hardware speed measurement.

The frozen **37-module / 559-function** production corpus and all twelve images
retain their RS02 sizes and resource costs. The combined candidate still saves
**44,020 executable bytes** against the promoted baseline; RS03 does not add
production-corpus reductions because these modules do not meet the bounded
wrapper shape. Fifteen driver/default tests, the pipeline test and four
persistent host/physical-evidence and tamper tests also pass.

The [RS03 report](../build/rs03-command-wrapper/results.json) includes exact
unchanged input IR, control/candidate assembly, linked images, component and
resource checks, immutable source/tool/log snapshots and predecessor
nonregression. Captured tool commands took approximately **1.96 seconds**,
excluding tests and compiler rebuilding. An append-only final-validation
record adds the strengthened per-function byte checks; the backend binary
was unchanged and the corpus was not recaptured.

**The initial RS03 compiler/whole-image/ABI gates and 24-byte target were
host-qualified without flashing that 728-byte combination.** The subsequent
20-byte extension and its independent physical campaign are recorded below.
Defaults remain `thumb-widths`; broader wrapper shapes and final combination
promotion are separate work.

```sh
node badge-compiler/tools/command-wrapper-evidence.mjs --verify \
  badge-compiler/build/rs03-command-wrapper/results.json
node --test badge-compiler/tests/command-wrapper-evidence.test.mjs
make -C badge-compiler test-command-wrapper
```

#### RS03 internal-call preservation and physical acceptance

The wrapper now **beats LLVM: 20 bytes versus 24**, without growing the
8-byte saved-register space or adding local/outgoing stack slots. The complete
image is **724 bytes**, four smaller than the initial RS03 result.

The improvement is a general, conservative **finalized-machine register
summary**. After final selection, constant/address folding, branch reduction
and frame generation, the backend scans the complete function's emitted
instructions. It records every explicitly mentioned register, not just writes:
reads also block reuse. This deliberately overestimates clobbers but includes
hidden temporaries, argument moves, loads/stores, normalization and frame
instructions that an IR-only summary could miss.

Only recognized machine operations and generated local branches contribute a
usable summary. Opaque assembly or unknown instructions invalidate it.
Direct calls contribute the summary of an already-finalized **module-internal**
callee; an unavailable, public/replaceable, indirect or recursive call makes
the summary unavailable. No function reordering or speculative second lowering
pass is used. Forward definitions can therefore miss this optimization:
definition order affects opportunity, **not correctness**.

An eligible ordered wrapper can retain its common literal in **r2** only when
its internal callee's complete summary does not mention r2. Otherwise the
existing r1/rematerialization path remains. External/public ABI behavior is
unchanged; this does not declare r2 callee-saved or impose a private calling
convention on unknown code.
Reuse is deliberately restricted to r2: r12 is not eligible, since an
AAPCS-compliant linker veneer may overwrite it even for an internal call.

The real byte helper's finalized mask is **11**: r0, r1 and r3 are mentioned,
r2 is untouched. The linked wrapper becomes:

```asm
push.n {r4, lr}              // 2 bytes
mov.w r4, #0xd0000000        // 4 bytes
mov.w r2, #0x10000000        // 4 bytes, only once
str.n r2, [r4, #32]          // 2 bytes, original pre-call store
bl write_byte               // 4 bytes
str.n r2, [r4, #24]          // 2 bytes, original post-call store
pop.n {r4, pc}               // 2 bytes
```

Only the second four-byte mask setup disappears. Store order, call target,
incoming value, byte-helper instructions/strobe NOPs and other component
sizes remain unchanged; the saving is not moved into callers or the callee.
Generated C instructions are **492 bytes**; boot/startup/tables are unchanged
and alignment remains two bytes.

**Nine targeted tests pass.** The added preservation oracle covers simple and
transitive internal callees, arbitrary RAM addresses/literals, public callees,
a real machine-generated r2 division temporary, opaque register-changing
assembly, recursion and forward definitions. Both LLVM and owned callers
execute the wrappers with seven boundary inputs and r4-r11 sentinels.
Independent LLVM assembly agrees with every tested owned encoding, and
frame/save/outgoing demand and call edges cannot grow. The original
clobbering-callee/ordered-effect/public-address-taken tests still pass.

The new combined preservation oracle executes **5,996 -> 5,208 instructions**
against `thumb-widths`, **13.1% fewer**. It includes the harness and inherited
candidate improvements; it is not an isolated 24-to-20-byte speed measurement
or MCU wall-clock timing. The earlier wrapper oracle remains 7,959 -> 7,580
because its unknown callee must keep the conservative path.

The [new host report](../build/rs03-call-preservation/results.json) verifies the
exact 20-byte linked helper, r2 proof, whole image and nonregression against
the original RS03 report. The frozen 37 modules / 559 functions and twelve
production images remain unchanged versus initial RS03, including all
frame/save/outgoing/call/data/BSS facts. The combined corpus saving remains
44,020 executable bytes. Captured tool commands took approximately **1.84
seconds**, excluding tests and rebuilding. Fifteen driver/default tests, the
pipeline test and host/physical-evidence tamper tests also pass.

##### Physical campaign - 2026-10-11

The user explicitly authorized badge testing and subsequently approved
**functional qualification with electrical timing left unmeasured**.
The [physical report](../build/rs03-reuse-hardware/results.json) binds the exact
cached 724-byte candidate and 568-byte LLVM control to the immutable host
report. Both current full 16 MiB flashes were saved, verified and hashed
before loading either image; UF2 writes were verified.

| Round | Badge 08DA26F86E57F5EA | Badge 9BBF88DD17D66E7D | User confirmation |
| --- | --- | --- | --- |
| 1 | Owned 724-byte candidate | LLVM control | Both pass initially and three cold boots each |
| 2 | LLVM control | Owned 724-byte candidate | Both pass initially and three cold boots each |

All **12 cold boots** and both initial display checks passed. The user chose
to leave the round-2 pair installed: **owned 724 bytes on 9BBF88DD17D66E7D,
LLVM 568 bytes on 08DA26F86E57F5EA**. The older RS02 pair is preserved in this
campaign's complete flash backups, not automatically restored.

The shared hardware verifier checks image/provenance hashes, ELF/raw/UF2
agreement, the unchanged byte-helper strobe path, role swaps, observations
and the explicitly limited acceptance scope. Pulse widths, setup/hold margins,
ringing and voltage levels were **not measured**; waveform equipment and the
exact LCD timing specification would be needed for those claims.
The immutable host report correctly retains its pre-hardware status; the
later physical result is separate.

**Defaults remain unchanged.** This compact-program qualification does not
promote the broader `thumb-constants` combination or finish RS01's size target.
To restore the immediate pre-campaign full flashes later, place both badges
in BOOTSEL and run the shared hardware tool with `--rs03 --restore`.

```sh
node badge-compiler/tools/command-wrapper-evidence.mjs --verify \
  badge-compiler/build/rs03-call-preservation/results.json
node badge-compiler/tools/byte-transport-hardware.mjs --rs03 --verify
RS03_REPORT="$PWD/badge-compiler/build/rs03-call-preservation/results.json" \
HARDWARE_REPORT="$PWD/badge-compiler/build/rs03-reuse-hardware/results.json" \
  node --test badge-compiler/tests/command-wrapper-evidence.test.mjs \
    badge-compiler/tests/byte-transport-hardware.test.mjs
```

### RS04 - Table interpreter

**Baseline/target:** 128 owned bytes; match or beat the 40-byte LLVM reference
without precomputing this particular LCD command stream.

- [x] Attribute pointer/count PHIs, nested-loop branches, byte loads,
  call-live state and stack traffic in the emitted interpreter.
- [x] Improve general pointer induction, post-increment/address selection,
  narrow-value propagation and counted-loop control with correct zero-count
  behavior, wraparound rules and flag liveness.
- [x] Evaluate bounded partial evaluation for known read-only input only through
  general typed transformations, charging unrolled code, retained tables,
  caller growth and remaining generic entry points.
- [x] Qualify on parsers, byte streams and unrelated table-driven badge logic,
  including empty records, varying lengths, opaque calls and pressured loops.

#### RS04 implementation and qualification

The general interpreter now occupies **40 bytes**, matching LLVM and reducing
the previous RS03 result by **72 bytes**. This is reusable compiler selection,
not recognition of LCD function names, addresses or table contents. The source,
table bytes, call order, volatile accesses and explicit NOPs are unchanged.

| Component | RS03 | RS04 | LLVM |
| --- | ---: | ---: | ---: |
| GPIO/application sequencing | 244 | 244 | 184 |
| Byte transport | 32 | 32 | 32 |
| Command wrapper | 20 | 20 | 24 |
| Table interpreter | 112 | **40** | **40** |
| Pixel fill | 52 | **42** | 36 |
| Initialization-delay helper | 32 | 32 | 20 |
| **Generated C** | **492** | **410** | **336** |
| Alignment padding | 2 | 4 | 2 |
| **Complete loaded image** | **724** | **644** | **568** |

Vectors, PICOBIN, startup and the 102 table bytes remain unchanged. Data and
BSS remain zero. The interpreter's private frame shrinks **8 -> 0 bytes** and
saved-register area **32 -> 16 bytes**, so its own stack demand falls
**40 -> 16 bytes**. Outgoing stack arguments remain zero and both callback
edges are retained. The coupled pixel-fill improvement is general loop
selection; RS05's 36-byte target is still open.

The opt-in `thumb-constants` policy adds:

- Precise scalar callback scratch constraints, keeping argument-only values
  separate from values that must survive a call.
- Byte-GEP and unsigned pointer-comparison operands emitted directly in
  allocated registers; canonical narrow-value and pointer/PHI affinities.
- Moving a plain guarded countdown decrement to its uniquely entered latch
  only when its sole use is the corresponding backedge PHI. Exit uses,
  overflow-flagged subtraction and body assembly retain conservative lowering.
- Delaying an adjacent pure byte-pointer advance until after the load that
  consumes the old pointer. Volatile/unsupported load shapes are not moved.
- Bounded low-register allocation including `r6`. Entire functions must satisfy
  the scalar ABI/operand checks, contain PHIs and ordinary direct callbacks,
  and need neither private objects, spills nor high allocated registers.
  A failed low-bank trial retries the conservative allocator once.
  Assembly, switches, intrinsics, byval/wide/complex calls and constant
  expressions do not enter this register extension; PHI-cycle scratch uses
  nonallocated `r12` within qualified functions, never across a call.
- A single-use entry byte-GEP index may stay in its incoming ABI register
  through a uniquely entered, bounded chain of unconditional entry blocks.
- Forwarding a loaded byte directly into `r0` across a proven neutral pointer
  advance for a marked one-argument callback, without retaining a volatile
  value across that callback.
- Narrow register arithmetic only when flags are killed before observation
  and frame-base replacement cannot invalidate the encoding. Forward
  zero-test branches use `CBZ`/`CBNZ` only within a conservative encoding span
  and when both successors kill NZCV before use.

Typed table partial evaluation and unrolling were considered but are not
needed to meet the target; they remain deferred rather than silently adding
specialized callers or duplicating this particular stream. The generic entry
point and original tables remain intact.

**Host qualification:** eleven targeted tests, fifteen driver/default tests,
the pipeline/failure-propagation test and five evidence/tamper tests pass.
Eight byte-stream variants cover empty input/records, counts through 255,
high-bit bytes, indirect/opaque/four-argument callbacks, long branch spans,
volatile data, live exit values and flagged decrements. Callbacks clobber
`r0-r3`, `r12` and flags; sentinels check `r4-r11` and call alignment.
LLVM/owned callers and callees are exercised in both directions, including
four/eight-argument, wide and byval fallbacks. Pointer offset/wrap boundaries,
private stack addressing and independent LLVM instruction encodings pass;
CBZ displacement 126 is accepted and 128 rejected.

The final reached-oracle trace falls **244,997 -> 221,894 instructions**.
This includes the test harness and conservative variants, not isolated
interpreter cycles or measured hardware timing.

The frozen **37 modules / 559 functions / 12 images** retain identical default
control assembly. Every function's frame, saved-register and outgoing areas
are non-growing, with identical call edges. Module and complete-image bytes
are non-growing versus RS03. Combined savings against the promoted baseline
are **51,188 executable bytes**, **7,168 more than RS03**; all twelve images
remain smaller than the promoted baseline. Capture commands total about
**2.08 seconds**, excluding qualification tests and builds.

- [Immutable host report and final validation](../build/rs04-byte-streams/results.json).
- [Final targeted test log](../build/rs04-byte-streams/final-validation/targeted-tests.log).
- [Physical acceptance and flash provenance](../build/rs04-hardware/results.json).

**Physical qualification:** both full 16 MiB pre-campaign flashes were
preserved before either write. The exact cached owned/LLVM images were
flash-verified and started. The user confirmed correct displays and **one
cold boot per badge**, reported no failures and explicitly accepted the
functional-only scope with electrical timing unmeasured. There was no role
swap or repeated three-boot campaign:

| Badge | Installed image | Functional result |
| --- | --- | --- |
| `9BBF88DD17D66E7D` | Owned RS04, 644 bytes | Correct display; one cold boot passed |
| `08DA26F86E57F5EA` | LLVM, 568 bytes | Correct display; one cold boot passed |

These supersede the RS03 comparison pair. RS02/RS03 backups and their
historical twelve-boot evidence are unchanged. The new runner's `--rs04`
campaign uses one round/one cold boot per badge; old campaigns retain their
original verification requirements. Restore the immediate pre-RS04 pair only
if explicitly wanted, with both badges in BOOTSEL:

```sh
node badge-compiler/tools/byte-transport-hardware.mjs --rs04 --restore
```

**RS04 meets its compiler, whole-image, parser/ABI and user-approved
functional-hardware gates.** Broad compiler defaults and the root firmware
Makefile are unchanged; electrical timing compliance is not claimed.

### RS05 - Pixel fill

**Baseline/target:** 62 owned bytes; match or beat the 36-byte LLVM reference
for the same ordered pair of byte writes per pixel.

- [x] Attribute loop control, pixel splitting, argument copies and call-live
  state; separate pure arithmetic from the observable transfer operations.
- [x] Improve pressure-aware invariant placement, repeated-argument transport
  and decrement/branch selection without overflow or zero-trip mistakes.
- [x] Evaluate general repeated-value/loop specialization and bounded unrolling
  against whole-module bytes, execution cost, frame demand and flash behavior.
- [x] Test varying colors/counts, zero and boundary counts, ordinary RAM fills
  and other streaming loops. Wider stores, DSP packing, DMA or PIO must not
  replace byte MMIO transfers without a proved or explicit platform contract.

### RS05 implementation and qualification

The opt-in `thumb-constants` policy reduces the generic fill helper
**42 -> 34 bytes**, beating LLVM's **36-byte** reference. The complete linked
image shrinks **644 -> 636 bytes**. No pixel-source, address or function-name
recognition is involved.

| Component | RS04 | RS05 | LLVM |
| --- | ---: | ---: | ---: |
| GPIO/application sequencing | 244 | 244 | 184 |
| Byte transport | 32 | 32 | 32 |
| Command wrapper | 20 | 20 | 24 |
| Table interpreter | 40 | 40 | 40 |
| Pixel fill | 42 | **34** | 36 |
| Initialization-delay helper | 32 | 32 | 20 |
| **Generated C** | **410** | **402** | **336** |
| Alignment padding | 4 | 4 | 2 |
| **Complete loaded image** | **644** | **636** | **568** |

The eight-byte improvement removes two argument copies and selects narrow
shift/extract instructions. An adjacent pure immediate shift or mask may
feed `r0` directly only for a marked direct one-argument scalar callback.
Indirect, opaque and multi-argument calls retain conservative lowering.
Low-register masks 255/65535 use flag-neutral `UXTB`/`UXTH`; narrow immediate
shifts require NZCV to be killed before observation. Frame-base register
`r7` is excluded to preserve legal encodings after frame finalization.
Pixel/counter state stays in preserved registers: fill frame **0**, saves
**16**, outgoing arguments **0**, and callback edges are unchanged.

Invariant placement and countdown selection already supplied by RS04 remain
intact. General repeated-value specialization and bounded unrolling were
evaluated but are unnecessary to beat the target; duplicating transfers would
increase code without an established whole-image benefit. The exact ordered
byte pair, volatile MMIO, explicit NOPs, zero-trip behavior, delay primitive,
tables, startup, vectors, PICOBIN and zero data/BSS are retained. No wider MMIO,
DSP packing, DMA or PIO substitution is made.

**Host qualification:** twelve targeted tests, fifteen driver/default tests,
pipeline checks and six evidence/tamper tests pass. Streaming variants cover
ordinary, indirect, opaque, halfword-extract, live-result and four-argument
callbacks with varying colors and counts. LLVM/owned callers, clobbering
callbacks, preserved-register sentinels and call alignment are checked.
Bounded ordinary RAM fills exercise unsigned counts 0, 1, 2, `0x7fffffff`,
`0x80000000` and `0xffffffff` without impractically long runs. Independent
encodings, shift endpoints, flag consumers and live extraction carry values
are tested. No arithmetic moves an observable transfer or live call state.

The frozen **37 modules / 559 functions / 12 images** preserve identical
default control assembly and non-growing frames, saves, outgoing areas,
module/image bytes and identical call edges versus RS04. Combined savings
against the promoted baseline reach **51,290 executable bytes**, **102 more
than RS04**; all twelve images remain smaller. Capture commands total about
**1.90 seconds**, excluding builds and qualification tests.

- [Immutable host report and final validation](../build/rs05-pixel-fill/results.json).
- [Final targeted test log](../build/rs05-pixel-fill/final-validation/targeted-tests.log).
- [Physical acceptance and preserved flash provenance](../build/rs05-hardware/results.json).

**Physical qualification passed.** Both full 16 MiB pre-campaign flashes were
preserved before either write. The cached owned/LLVM images were flash-verified
and started. The user confirmed correct displays and **one cold boot per
badge**, reported no problems and explicitly accepted functional-only
qualification while keeping this comparison pair:

| Badge | Installed image | Functional result |
| --- | --- | --- |
| `9BBF88DD17D66E7D` | Owned RS05, 636 bytes | Correct display; one cold boot passed |
| `08DA26F86E57F5EA` | LLVM, 568 bytes | Correct display; one cold boot passed |

The physical report and its tamper tests pass. Historical campaigns and
backups remain unchanged. To restore the immediate pre-RS05 flashes only
when explicitly wanted, put both badges in BOOTSEL and run:

```sh
node badge-compiler/tools/byte-transport-hardware.mjs --rs05 --restore
```

**RS05 meets its general compiler, whole-image, streaming/ABI and user-approved
functional-hardware gates.** Compiler defaults and the root firmware Makefile
are not promoted; electrical timing is unmeasured.

### RS06 - Initialization-delay helper

**Baseline/target:** 40 owned bytes; match or beat the 20-byte LLVM reference
while retaining the current six explicit NOPs per executed iteration.

- [x] Attribute unnecessary leaf saves, copies, constant setup and loop
  control around operand-free assembly in general counted loops.
- [x] Improve safe no-clobber leaf lowering, unsigned countdown selection and
  final instruction widths without enabling unsafe flag/scheduling profiles.
- [x] Prove exact iteration semantics for zero, one, ordinary and boundary
  counts; retain assembly order, clobber handling and output-preserving errors.
- [x] Preserve the shorter wait's 24-million-explicit-cycle minimum at at most
  150 MHz. A timer-based alternative requires an explicit running-timer/platform
  contract and separate firmware qualification, not silently treating the
  C loop count as elapsed time or assuming minimal startup initialized a timer.

### RS06 implementation and qualification

The opt-in `thumb-constants` policy now selects a **20-byte**, stack-free
initialization-delay helper, matching LLVM. It shrinks **32 -> 20 bytes**
from RS05 (40 bytes under the promoted policy), linking a **624-byte**
complete image instead of 636.

| Component | RS05 | RS06 | LLVM |
| --- | ---: | ---: | ---: |
| GPIO/application sequencing | 244 | 244 | 184 |
| Byte transport | 32 | 32 | 32 |
| Command wrapper | 20 | 20 | 24 |
| Table interpreter | 40 | 40 | 40 |
| Pixel fill | 34 | 34 | 36 |
| Initialization-delay helper | 32 | **20** | **20** |
| **Generated C** | **402** | **390** | **336** |
| Alignment padding | 4 | 4 | 2 |
| **Complete loaded image** | **636** | **624** | **568** |

The improvement is a general closed counted-leaf selection, not a replacement
delay fixture or recognition of source names, constants or the six-NOP string.
It admits a single scalar i32 argument, void return, a bounded linear entry
chain, one two-input counter PHI, plain subtract-by-one, a zero equality or
inequality guard, an assembly-only body/backedge and a void exit. PHI order
and guard polarity may vary. Every instruction and block must fit the proof.

Existing assembly effect analysis must establish operand-free, register-
neutral, non-control-flow assembly with no flag reads or memory accesses.
Currently that analyzer proves NOP-only text. Declared memory barriers and
flag clobbers remain represented in their original assembly order; no memory
operation is present to reorder. Opaque assembly, additional calls, live exit
results, overflow-flagged arithmetic, wider/byval parameters, extra state or
unsupported CFGs retain normal lowering. Bodies above the conservative
60-NOP forward-branch admission limit also retain normal lowering.

The sole live counter stays in incoming **r0**. An initial `CBZ` handles zero
without decrementing or executing assembly; the body executes every original
NOP, followed by `SUBS` and a backward `BNE`. The original terminal decrement
is dead under this void-exit proof. There are no entry/PHI copies, saves or
private frame: delay stack demand falls **4 -> 0 bytes**. This uses existing
frame finalization and finalized-register summaries; other function resources
and all call edges remain unchanged. No unsafe scheduling/flag profile,
timer assumption, platform substitution or default change is introduced.

The shorter initialization wait still executes **4,000,000 iterations x 6
explicit NOPs = at least 24,000,000 cycles**, or **160 ms at at most 150 MHz**,
excluding loop overhead. The six-million-iteration wait likewise retains
36 million explicit NOP cycles. Source waits, MMIO strobe NOPs, table bytes,
startup, vectors and PICOBIN are unchanged; data/BSS remain zero.

**Host qualification:** thirteen targeted tests, fifteen driver/default tests,
pipeline/failure-propagation checks and seven evidence/tamper tests pass.
General leaf variants cover one, two, six, seven and sixty NOPs, inverted
guards, reversed PHIs and declared memory/flag clobbers. Sixty-one NOPs,
opaque assembly, live exit values, flagged decrements and real callbacks
exercise conservative fallbacks. Unsupported operands/clobbers produce
explicit diagnostics and preserve existing output files.

LLVM/owned callers and generated/LLVM callees are exercised in both directions,
with preserved-register sentinels. Independently encoded assembly agrees.
QEMU traces count the actual reached NOP instructions for counts
0, 1, 2, 7, 31, 127, 128 and 255, proving zero-trip and exact ordinary
iteration behavior rather than checking only successful return. Independent
decoding of emitted Thumb code checks every count 0 through 255 and bounded
prefixes at `0x7fffffff`, `0x80000000` and `0xffffffff`, including counter
values and NOP counts, without running billions of iterations.
The reached-oracle trace falls **137,899 -> 118,216 instructions**, including
the harness and fallback variants; this is not measured electrical timing.

The frozen **37 modules / 559 functions / 12 images** preserve identical
default control assembly, non-growing frame/save/outgoing areas, identical
call edges and non-growing module/image bytes versus RS05. This corpus has
no additional matching leaves, so its combined promoted-baseline savings
remain **51,290 executable bytes**: the new twelve-byte saving is in the
separately measured red-square image, not falsely added to the corpus total.
Capture commands total about **1.42 seconds**, excluding builds/tests.

- [Immutable host evidence and final validation](../build/rs06-counted-delay/results.json).
- [Final targeted test log](../build/rs06-counted-delay/final-validation/targeted-tests.log).
- [Physical acceptance and preserved flash provenance](../build/rs06-hardware/results.json).

**Physical qualification passed.** After resolving USB/BOOTSEL availability
before any write, both full 16 MiB pre-campaign flashes were preserved.
The cached owned/LLVM images were flash-verified and started. The user
confirmed correct displays and **one cold boot per badge**, with no failures,
and explicitly accepted functional-only qualification while keeping this pair:

| Badge | Installed image | Functional result |
| --- | --- | --- |
| `9BBF88DD17D66E7D` | Owned RS06, 624 bytes | Correct display; one cold boot passed |
| `08DA26F86E57F5EA` | LLVM, 568 bytes | Correct display; one cold boot passed |

The physical verifier also checks both images' 20-byte delay symbols and
six explicit NOP encodings. Physical provenance/tamper tests pass. Earlier
campaigns and backups remain intact. Restore the immediate pre-RS06 pair
only when explicitly wanted, with both badges in BOOTSEL:

```sh
node badge-compiler/tools/byte-transport-hardware.mjs --rs06 --restore
```

The later [maze deployment](../../build/maze-rs06-hardware/results.json)
supersedes these installed red-square images with owned/LLVM maze builds,
after preserving both complete RS06-era flashes. The historical RS06
qualification report remains unchanged.

**RS06 meets its general compiler, whole-image, iteration/ABI, minimum-delay
and user-approved functional-hardware gates.** Electrical timing is unmeasured
and broad defaults/root firmware Makefile are unchanged. RS02, RS04 and RS06
match LLVM; RS03 and RS05 beat it. RS01 sequencing remains 244 versus 184 bytes,
leaving a **56-byte complete-image gap** (54 generated-C bytes and two padding
bytes). Broad promotion and global-minimum claims remain out of scope.

### RF01 - Behavior-preserving backend refactoring

After RS06 and the maze deployment, the first bounded refactoring round
extracts the existing CFG/preselection transformations into
[codegen_cfg.c](../src/codegen_cfg.c) and shares function finalization between
ordinary lowering and counted leaves. No selection rules, allocator behavior,
ABI, assembly order, delay semantics or defaults change.

The exact comparison campaign covers **46 IR inputs / 16 scalar policies /
942 outcomes**, including **46 attribution comparisons** and six explicit
negative cases. The frozen corpus's twelve images and the separately linked
red-square image are byte-identical. Fresh from-C builds also reproduce the
**624-byte red-square binary** and **19,600-byte owned maze binary**; the maze
UF2 is identical to the one already installed. Red-square helper sizes remain
**32 / 20 / 40 / 34 / 20 bytes**, with unchanged frames, call edges and NOPs.
Historical qualification records are untouched and neither badge is reflashed.

Twenty targeted/attribution tests, fifteen driver/default tests, seven
historical evidence/tamper tests, pipeline checks and native optimizer tests
pass. The native codegen test's historical fixed-prologue assertion still
fails at line 386, identically on frozen RS06 and the refactored backend.
That pre-existing failure is explicitly recorded, not waived into a passing
full-suite claim or fixed as part of this refactor.

- [Architecture and remaining refactoring scope](implementation.md#backend-phase-ownership-after-rf01).
- [Exact assembly/image comparison report](../build/rf01-backend/results.json).
- [Validation logs, rebuilt bytes and baseline-failure evidence](../build/rf01-backend/validation.json).

### Completion record

- [ ] RS01 meets its general compiler and whole-image acceptance gates.
- [x] RS02 meets its general compiler and user-approved functional-hardware
  acceptance gates. Electrical timing is explicitly unmeasured; no promotion.
- [x] RS03 meets its general compiler, whole-module and ABI host acceptance
  gates, beats LLVM at 20 bytes and passes role-swapped functional/cold-boot
  qualification. Electrical measurements and broad default promotion remain open.
- [x] RS04 meets its general compiler, whole-image and parser/loop acceptance
  gates at 40 bytes, with one confirmed cold boot on each badge. Electrical
  timing remains unmeasured; no default promotion.
- [x] RS05 meets its general compiler and streaming/I/O acceptance gates,
  beats LLVM at 34 bytes, and passes one cold boot per badge. Electrical
  timing is unmeasured; no default promotion.
- [x] RS06 meets its general compiler and minimum-delay acceptance gates
  at 20 bytes, retaining six NOPs per iteration and passing one cold boot
  per badge. Electrical timing is unmeasured; no default promotion.
- [ ] Qualify the final combination, publish byte/time/resource/compile-cost
  attribution and promote it when no major unresolved downside remains.

## Timing, memory and packaging

- The shorter initialization delay executes four million iterations with six
  explicit NOPs each: at least 24 million cycles, or **160 ms at at most
  150 MHz**, excluding loop overhead. The six NOPs occupy 12 instruction bytes
  but execute repeatedly. The LCD transport also retains its explicit strobe
  NOPs. Removing them is not an acceptable generic size optimization.
- Initialized MCU data and BSS are **zero**. Function calls still use stack
  RAM; this does not mean the program uses no RAM.
- No red-square bitmap is stored. The program emits 76,800 black pixels and
  4,096 red pixels, and the LCD retains the result in its own GRAM.
- The promoted-policy baseline raw loaded binary is **992 bytes**. Its UF2 file is **2,048 bytes**:
  four 512-byte transport blocks with 256-byte payload capacity each.
  Packaging overhead is not executing firmware.
- The image fits in one **4 KiB flash erase sector**. Reducing loaded bytes
  below 992 does not necessarily reduce the number of sectors touched.

## Evidence

- [Current owned byte attribution](../../build/red-square-owned-current/size-attribution.json).
- [Current owned disassembly](../../build/red-square-owned-current/disassembly.txt).
- [Current owned firmware](../../build/red-square-owned-current/universe26/uf2/red_square.uf2).
- [Current LLVM metrics](../build/po18-red-square/llvm.metrics.json).
- [Startup](../../src/arch/rp2350/start_red_square.S) and
  [linker contract](../../src/arch/rp2350/memmap_red_square.ld).
- [PO18 qualification](current-promotion.md) and the
  [original cold-boot-tested size experiment](../../docs/red-square.md).

The original experiment's **560-byte LLVM** result predates the extra explicit
delay NOPs. It remains a historical physical measurement, not the size of the
current timing-hardened source.
