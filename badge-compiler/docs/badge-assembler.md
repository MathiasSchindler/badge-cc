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

- [x] Specify RP2350 security/privilege applicability using pinned Arm/RP2350
  documentation; support named MRS/MSR registers needed by actual firmware.
- [x] Add CPSID/CPSIE and relevant exception/debug instructions such as
  SVC/BKPT, with immediate and operand validation.
- [x] Test PRIMASK/BASEPRI/FAULTMASK/CONTROL and stack-register forms separately;
  reject reserved or unsupported combinations instead of treating names as
  arbitrary encodings.
- [x] Validate encodings against LLVM and independent decoded expectations.
- [x] Add runtime tests that restore state and retain USB recovery.
- [x] Document that interrupt masking and system registers are per-core:
  disabling interrupts on one core does not lock the other core.
- [x] Preserve existing DMB/DSB/ISB behavior; test accepted barrier operands
  and restrictions rather than adding mnemonic-only acceptance.

**Exit:** supported operations have exact operand contracts, ABI/clobber
guidance and bounded physical checks for privilege-sensitive behavior.

**Status: implementation, host/emulator and physical Secure core-0
qualification complete.** A boot-only control and the restoring system probe
passed on badge `9BBF88DD17D66E7D` using an explicitly approved temporary flash
report sector. Both reports matched fresh nonces; the probe returned status 0,
observed SVC IPSR 11 and restored PRIMASK/BASEPRI/FAULTMASK/CONTROL from
`[0,0,0,0]` to `[0,0,0,0]`. The full original 16 MiB flash, including the report
sector, was restored and read back byte-for-byte. Badge `08DA26F86E57F5EA`
was not modified. This does not qualify Non-secure bank accesses, privilege
transitions, core 1, multicore synchronization or physical BKPT/debug behavior.

#### Instruction contract

| Operation | Accepted forms |
| --- | --- |
| MRS | `mrs Rd, name`; `Rd` is r0-r12 or lr |
| MSR | `msr name, Rn`; `Rn` is r0-r12 or lr |
| CPSID/CPSIE | `i`, `f`, or `if`; 16-bit, outside IT |
| SVC/BKPT | One absolute immediate 0-255; 16-bit |
| DMB/DSB/ISB | Existing no-operand or `sy` form; 32-bit, unchanged |

MRS/MSR do not accept `.n` or `.w`; they have a fixed 32-bit encoding, matching
LLVM's spelling contract. CPS/SVC/BKPT reject wide requests; the owned assembler
also permits explicit `.n`. CPS has no mode operand, `a` mask or comma-separated
mask list on this M-profile target. SVC may appear only in the last IT slot;
BKPT is deliberately restricted to outside IT rather than exposing its special
unconditional-in-IT behavior. MRS/MSR conditional forms must match an IT slot.
No implicit barriers or interrupt-state changes are inserted.

Named read registers:

- `apsr`, `iapsr`, `eapsr`, `xpsr`, `ipsr`, `epsr`, `iepsr`.
- `msp`, `psp`, `msplim`, `psplim`.
- `primask`, `basepri`, `basepri_max`, `faultmask`, `control`.
- `msp_ns`, `psp_ns`, `msplim_ns`, `psplim_ns`, `primask_ns`, `basepri_ns`,
  `faultmask_ns`, `control_ns`, `sp_ns`.

Named writes support the stack, mask and control names above; flag writes use
explicit `apsr_nzcvq`, `apsr_g`, or `apsr_nzcvqg`. Bare `apsr` and other PSR-view
writes are outside this bounded contract, not silently treated as equivalent.
`basepri_max` reads BASEPRI; its write raises masking only when the existing
mask is zero or the new nonzero value is a stronger priority threshold.
`sp_ns` accesses the Non-secure stack selected for the relevant mode.
Numeric SYSm values, invented `_s` aliases, reserved encodings,
`basepri_max_ns`, and v8.1-M PAC keys are rejected.

#### Target, security and ABI applicability

The reference is the locked [RP2350 inventory](datasheet/rp2350-source.json):
RP-008373-DS-3 / build `80d627281ed5`, physical pages 125-126, section 3.7.4.5.
Instruction fields and access rules are checked against the pinned
[Armv8-M manual catalog](datasheet/reference-sources.json), DDI0553B.y /
ID09082024: C2.4.25 BKPT (physical page 579), C2.4.43 CPS (606),
C2.4.125 MRS (755-759), C2.4.126 MSR (760-764), C2.4.242 SVC (947).
Additional implementation guidance uses Arm's
[CMSIS M-profile intrinsics](https://github.com/ARM-software/CMSIS_6/blob/d4a772e65df07a79ffe69c169a72d9bde4e5e7a6/CMSIS/Core/Include/m-profile/cmsis_gcc_m.h),
locked to commit `d4a772e65df07a79ffe69c169a72d9bde4e5e7a6`.
No external firmware dependency is introduced.

Each RP2350 Cortex-M33 has its own register state, exception handling and
interrupt masks. **Disabling interrupts on one core does not lock the other.**
These instructions are not multicore synchronization; BA05 handles exclusive
operations, alongside an explicit runtime locking design.

Unsuffixed banked names select the current Security state; `_ns` names access
the Non-secure bank from Secure state and are read-as-zero/write-ignored from
Non-secure state. Privilege rules still apply: privileged stack/mask accesses
are not a way for unprivileged Thread mode to obtain privilege or disable
interrupts. CONTROL writes can irreversibly drop privilege until an exception
handler restores it, or select a different stack. The assembler validates
syntax/encoding, not dynamic privilege, stack validity or exception setup.
Changing stack limits/pointers requires established aligned RAM and exception
stack capacity; writing the current MSP inside an ordinary C function is unsafe
unless the existing call-frame assumptions remain valid.

PRIMASK masks configurable exceptions, not NMI/HardFault; FAULTMASK has stronger
masking and Security-state-dependent HardFault behavior. Use it only for a
bounded critical window. BASEPRI is a priority threshold, not a boolean.
Restore the saved mask, rather than blindly enabling interrupts afterward.
Use ISB after CONTROL changes; memory ordering barriers and compiler memory
clobbers are separate requirements. BKPT without a suitable debugger/monitor
can fault; SVC requires a valid vector and exception handler. Their acceptance
does not grant a C ABI for the handler or syscall.

Inline assembly should use `volatile`, explicit register inputs/outputs and
`"memory"` for critical-section boundaries. APSR writes need `"cc"`; SVC/BKPT
need the clobbers of their handler/debug protocol. MRS needs an output operand,
MSR an input; writing APSR is not an ordinary GPR-only operation. Hand-written
subroutines must preserve AAPCS callee-saved registers and 8-byte call-stack
alignment. The probe preserves r4-r11/lr and saves/restores masks and flags;
it writes stack/control values back unchanged, never drops privilege.

#### Qualification and reproduction

`test-assembler-system` covers each supported named access with every allowed
GPR, independent expected halfwords and LLVM bytes; all 256 SVC and BKPT
immediates; CPS masks; malformed operands, access directions, widths and IT
placement; output preservation; existing SY barriers; and four maintained
LLVM-linked oracle fixtures. The QEMU MPS2 AN505 Cortex-M33 probe executes
PRIMASK/BASEPRI/BASEPRI_MAX/FAULTMASK transitions, PSR writes, same-value
stack/control writes and SVC entry/return, then restores saved state. Both
assemblers produce identical runtime binaries and exit successfully.
This is core-instruction evidence, not RP2350 peripheral, timing, Non-secure
transition or core-1 runtime qualification.

```sh
make -C badge-compiler test-assembler-system
node badge-compiler/tools/system-instructions.mjs --verify \
  badge-compiler/build/ba03-system/results.json
```

Set `LLVM_CLANG`, `LLVM_LLD`, `LLVM_OBJCOPY`, and `QEMU_ARM` if necessary.
The separate, explicitly opt-in physical runner is:

```sh
# Replaces application AND final 4 KiB flash sector; restores full flash.
# Requires explicit permission and a fresh evidence directory.
node badge-compiler/tools/system-instructions.mjs --hardware-flash-report \
  BOOTSEL_SERIAL badge-compiler/build/NEW-BA03-HARDWARE
```

The runner first checks that each ELF has a global Thumb reset entry matching
the loaded vector table and that its flash writer has a RAM execution address
and flash load image. Missing entries are fatal linker errors. It compares
LLVM/owned-assembler binaries before flashing and saves verified full flash.
Only the byte-identical owned-assembler image needs physical execution.

It runs a boot/transport control before the full probe. Each run uses a fresh
128-bit nonce. Firmware copies its RAM writer during startup, disables local
interrupts around ROM flash operations, and stores an 84-byte version-3 report
in a 256-byte page at `0x10fff000`, within the final 4 KiB sector of the
explicitly checked 16 MiB flash. The ROM `flash_op` API checks bounds and
permissions; the SRAM writer reconnects/exits XIP, erases/programs only that
sector, flushes the cache and restores command XIP before returning to flash.
No Pico SDK firmware dependency is introduced.

The watchdog is armed before flash writes and sensitive instructions. A
pending report is committed before the probe, followed by the final report.
Fault reports include IPSR, CFSR, HFSR, SFSR and the last probe stage. After
BOOTSEL recovery, the host reads the report from flash and requires the exact
size/version/nonce, completed publication stage, scheduled watchdog, expected
exception, zero result and restored register state. Missing/stale/pending
reports, USB recovery failure and flash restoration mismatch are errors.
The `finally` path restores and verifies the entire preserved flash, including
the report sector. This transport is opt-in diagnostic firmware, not a
production logging facility.

#### Empty-mailbox root cause

The RP2350 bootrom erases SRAM0-SRAM9, XIP cache and USB RAM before entering
Non-secure BOOTSEL (RP-008373-DS-3, physical pages 340-341, section 5.2.2).
Consequently a RAM mailbox cannot survive that return path. Reboot flag
`0x100` is `NO_RETURN_ON_SUCCESS`, not a warm-reboot RAM-retention flag
(physical page 364, section 5.4.8.24). Automatic USB recovery never proved that
the old response had been published.

An independent boot-only control still returned an empty RAM mailbox after
fixing another genuine defect: the original reset function was local, so
`-e ba03_reset` produced a warning and a zero ELF entry. Fixing the entry
alone did not fix the transport. Picotool also rejects peripheral scratch
register ranges as unmapped, so this runner does not use them as a transport.

The two original retained failed campaigns are
`build/ba03-hardware/results.json` and `build/ba03-hardware-retry/results.json`;
their `restored` states record independently checked manual restoration after
the original runner encountered USB re-enumeration. They are not successful
runtime reports. `build/ba03-hardware-diagnosis/results.json` preserves the
corrected-entry boot-only RAM control, which also failed and was automatically
restored. The successful control/probe and verified full restoration are in
`build/ba03-hardware-flash-report/results.json`. These captures are immutable;
their source/tool hashes describe the revisions executed, not subsequent
host-only validation/CLI cleanup.

Final host qualification passes 36 combined system/listing/crossover/report
tests, the sanitized assembler matrix and four LLVM oracle fixtures.
[BA03 production capture](../../compile-graph/build/ba03-verified/report.json)
retains all 18 assembly and 16 linked controls. All four production image
binaries are byte-identical to BA02: owned/LLVM red square 624/568 B and
tilt maze 19,600/8,704 B. Physical diagnostic changes do not enter these
production builds.

### BA04 — assembly ergonomics and compatibility

**Priority: medium.** Focus on actual RP2350 assembly inputs.

- [x] Add numeric local labels and forward/backward references, including
  repeated labels and diagnostics for unresolved references.
- [x] Support the target-appropriate `.arch` spelling without enabling
  unrelated architectures or silently changing the target profile.
- [x] Add LDR `=constant`/`=symbol` pseudo-operations and explicit `.ltorg`/
  `.pool` handling with deduplication, alignment and relocation tests.
- [x] Define pool placement deliberately: never insert executable fallthrough
  into literal data or silently introduce an unrequested control-flow change.
- [x] Test 16/32-bit literal reach, PC alignment and interacting branch
  relaxation at exact positive/negative limits.
- [x] Evaluate external conditional-branch relocations only with matching
  owned-linker support and out-of-range diagnostics.
- [x] Keep macro systems, broad GNU compatibility and general CFI/unwind
  generation out of scope unless actual inputs justify them.

**Exit:** accepted conveniences preserve semantics and fail explicitly when
the specified layout cannot be represented.

**Status: implemented and host/emulator-qualified.** No physical execution or
badge flashing is required for these syntax/layout conveniences.

#### Numeric labels and target spelling

Decimal numeric labels in `0..4294967295` may be repeated, including several
labels before an instruction on one line. `Nf` selects the next definition and
`Nb` the previous definition in source-record order, not a global symbol named
`Nf` or `Nb`. Numeric references work in branches, data and modifier expressions,
and in aliases/differences under the existing affine-expression contract.
Cached expressions and `.equ` aliases retain the correct reference site.
Missing directions and numeric overflow are explicit errors. Numeric labels
are local: they cannot be declared global, assigned or given named symbol
metadata. Named-label duplicate/redefinition rules are unchanged.

`.arch armv8-m.main` is accepted as an assertion of the existing RP2350
Cortex-M33 profile. It does not switch architecture or disable the target's
DSP support. Other architecture names, extension suffixes and modes remain
rejected. CPU/DSP attributes and the existing `.cpu cortex-m33` contract
remain unchanged.

#### Explicit literal pools

`ldr[.n/.w] Rd, =expression` loads a 32-bit pool word into r0-r12 or lr.
Normal IT spelling/placement rules apply. Expressions use the existing
absolute/affine data contract: signed/unsigned 32-bit constants, symbols plus
addends, numeric references and representable differences. Symbolic words
retain ordinary ABS32/REL32 relocations; an alias to an undefined symbol is
still outside the existing alias contract. `.` and numeric references are
evaluated at the load site, not at the later pool directive.

`.ltorg` and `.pool` are operand-free synonyms. Each flushes only the current
section's pending literals, in first-use order, with four-byte alignment and
zero padding. Equal 32-bit constants (including `-1`/`0xffffffff`) and equal
resolved symbol/addend expressions share a word within that pool. Distinct
symbol identities are not merged merely because their current addresses
coincide. Pools remain independent across sections and flushes. An empty
directive emits nothing.

Placement is deliberately stricter than general GNU/LLVM assembly:

- Every pending pool must have an explicit flush; no pool is appended at EOF
  or inserted automatically to repair an out-of-range load.
- A nonempty pool requires a preceding unconditional non-IT branch/return
  (`b`, `bx`, PC-returning `pop`/`ldm`, or `tbb`/`tbh`). A label or another
  instruction reopens the potential fallthrough path. Unknown/conditional
  exits are conservatively rejected. This is a local placement guard, not
  whole-program control-flow proof.
- Pools cannot interrupt IT blocks. No branch around data is synthesized.
- Literal instructions still select/check the existing 16/32-bit encodings:
  narrow uses low registers and aligned forward displacement `0..1020`;
  wide uses signed `-4095..4095`, relative to aligned `PC+4`.
  Four-byte-aligned pool words therefore have a positive wide limit of
  4092, and 4096 is rejected. Explicit `.n` must fit; layout relaxation and
  pool padding must converge before serialization.

The owned pseudo-operation always performs a pool load, including for small
constants. LLVM may replace those with MOV/MVN/MOVW. This is an intentional
representation difference, not a value/flag-semantics difference; both forms
are executed in the emulator. Use an explicit MOV/MOVW when that encoding is
wanted. The owned assembler does not add a hidden instruction optimization.
Non-encodable constants and ordinary symbol/addend pool loads are compared
byte-for-byte with LLVM. LLVM can retain duplicate complex difference
expressions which the owned pool deduplicates; the relative-word test checks
both resulting loads against the exact linked value through both linkers.
BA02 listings expose pool data, separate zero-alignment ranges,
relocations and actual instruction reach without inventing source/IR context.

```asm
.syntax unified
.arch armv8-m.main
.thumb
    ldr r0, =0x12345678
    ldr r1, =0x12345678   @ shares the pool word
    b 1f
    .ltorg
1:
    bx lr
```

#### Conditional-branch evaluation and qualification

LLVM external conditional branches require `R_ARM_THM_JUMP19` (51), unlike
the existing CALL/JUMP24 records. The owned object reader/linker do not support
that relocation. BA04 therefore keeps external conditional branches rejected,
rather than emitting objects that cannot be linked. The regression explicitly
checks LLVM's relocation and rejection/output preservation at both owned
boundaries. Same-section conditional branches remain supported and range
checked. Adding JUMP19, veneers or linker relaxation is a separate coordinated
object/linker feature, not mnemonic-only acceptance.

```sh
make -C badge-compiler test-assembler-ergonomics
```

Set `LLVM_CLANG`, `LLVM_LLD`, `LLVM_OBJCOPY` and `QEMU_ARM` as needed. This
target performs no hardware access. Eighteen BA04 tests cover repeated and
cross-section numeric references, contextual aliases, all supported load
destinations, explicit widths/IT, deduplication, pool isolation, relocation
linking, placement rejection, atomic object/listing preservation, exact
1020/1024 and 4092/4096 pool boundaries, signed direct-literal limits and
interacting branch relaxation. Cortex-M33 QEMU executes both assembler routes,
including the intentional small-constant representation difference.

Qualification passes 54 combined BA04/system/listing/crossover/Compile Graph
tests, three focused existing final-layout tests, the sanitized native matrix
(44 positive, 117 negative, 9 unsupported, 24 mutations) and all five
maintained LLVM oracle fixtures. The new fixture links identically through
both assemblers and both linkers.

[BA04 production capture](../../compile-graph/build/ba04-qualified/report.json)
retains all 18 assembly and 16 linked controls. Its explicit `thumb-constants`
policy matches BA03; all four production binaries are byte-identical:
owned/LLVM red square 624/568 B and tilt maze 19,600/8,704 B. Reproduce that
matching-policy comparison with:

```sh
BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants \
  node compile-graph/capture.mjs --capture compile-graph/build/NEW-BA04
```

Compiler defaults and optimization policies are not changed by BA04.

### BA05 — multicore synchronization instruction coverage

**Status: instruction implementation, host/emulator qualification and
physical two-core exit criterion complete.**

- [x] Specify and encode relevant LDREX/STREX byte/halfword/word forms and
  CLREX, including register overlap, alignment and addressing restrictions.
- [x] Evaluate Armv8-M acquire/release forms against the exact RP2350 profile,
  adding only documented, needed forms.
- [x] Document barriers and the distinction between local/global exclusive
  monitors, normal SRAM, device memory and peripheral registers.
- [x] Establish which memory regions support the intended synchronization;
  do not imply that exclusives make arbitrary MMIO atomic.
- [x] Compare exclusives with RP2350 SIO hardware spinlocks, FIFO and event
  mechanisms in runtime examples; these peripherals need protocols, not new
  CPU opcodes. SEV/WFE encoding already exists.
- [x] Test two-core contention, ordering, progress and bounded timeout/error
  paths **on hardware**. The full bounded protocol passed on the selected A4
  badge with verified original-flash restoration. This is functional evidence,
  not one-way timing or lock fairness.
- [x] Keep C atomic lowering, shared-memory ownership, cache/memory placement
  and interrupt interactions as explicit compiler/runtime prerequisites.

**Exit:** instruction encoding tests pass and a documented two-core protocol
has functional physical evidence, with both cores' clocks/configuration bound.
**Satisfied:** both encoding and physical bounded-protocol gates pass.

#### Instruction contract

The RP2350 Armv8.0-M Mainline Cortex-M33 supports these admitted forms:

| Operation | Syntax | Addressing |
| --- | --- | --- |
| Exclusive word load/store | `ldrex Rt,[Rn,#imm]`; `strex Rd,Rt,[Rn,#imm]` | Optional immediate, unsigned `0..1020`, multiple of four |
| Exclusive byte/halfword | `ldrexb/ldrexh Rt,[Rn]`; `strexb/strexh Rd,Rt,[Rn]` | Base only; explicit zero offset is equivalent |
| Clear local reservation | `clrex` | No operands |
| Acquire load/release store | `lda/ldab/ldah Rt,[Rn]`; `stl/stlb/stlh Rt,[Rn]` | Base only |
| Acquire/release exclusive | `ldaex/ldaexb/ldaexh Rt,[Rn]`; `stlex/stlexb/stlexh Rd,Rt,[Rn]` | Base only |

All are fixed 32-bit instructions. As with LLVM for these mnemonics, `.n`
and `.w` suffixes are rejected. Valid condition suffixes require the matching
IT slot; these instructions neither write APSR nor transfer control.
Data and status registers are r0-r12/lr; bases are r0-r14, including sp but
not pc. A store-exclusive status output must differ from both its value
register and base. A load destination may equal its base, and a store value
may equal its base. Register offsets, shifted indexes, writeback,
post-indexing, negative immediates and nonzero offsets on base-only forms
fail explicitly. Doubleword exclusive, A-profile and Armv8.1-M forms remain
unsupported. Instructions require no new relocations or ELF attributes.

The assembler checks the immediate, not the runtime base: the effective
address must be naturally aligned for the access width. Exclusive pairs
must use matching address, width, Security and privilege state. Store status
zero means success; nonzero means failure and requires retry or an explicit
error. Byte/halfword loads zero-extend. CLREX permits abandoning a sequence.
The exact contract is grounded in the pinned Armv8-M ARM DDI0553B.y,
physical PDF pages 593, 647-651, 687-689, 896-900 and 924-926; these include
the admitted v8.0-M forms, not blanket support for that manual's newer ISA.

#### Memory and ordering

RP2350 datasheet sections 2.1.6-2.1.6.2 (physical PDF pages 26-28) specify
global exclusive support only for **main SRAM**
`0x20000000 <= address < 0x20082000`, with the complete access inside that
range. The reservation granule is 16 naturally aligned bytes. Core 0,
core 1 and DMA writes participate; unrelated data in the same granule can
invalidate a reservation. Isolate heavily contended lock/handshake words.

Each core also has its local monitor. CLREX clears the executing core's
local state, not RP2350's global reservation. Exceptions/context switches
and conflicting accesses can defeat an exclusive sequence; bounded retries
must tolerate this. Never depend on a particular number of successful retries.
Normal stores from the other core or DMA can invalidate a reservation; a
normal store by the reserving core does not clear its global reservation.

When the MPU is disabled, initialize `ACTLR.EXTEXCLALL` (bit 29,
`0xe000e008`) on **each** participating Cortex-M33. The RP2350 register
description (physical PDF page 165) specifies reset zero and external
exclusives allowed with no MPU. Local exclusive success alone does not
qualify global-monitor participation. Diagnostic initialization preserves
other ACTLR bits, checks privilege/MPU state and verifies readback after
barriers; core 0 restores its original ACTLR on return or fault.

**Do not perform exclusive accesses on MMIO, ROM, XIP flash/cache SRAM or
SRAM aliases outside the supported range.** Outside supported SRAM, the
system can report exclusive failure yet still perform the write. Such a
loop is neither atomic nor a safe read-only test of feature support.
Device registers need their documented read/write/atomic-alias protocol.

LDA/LDAEX order subsequent memory operations after the acquire; STL/STLEX
order preceding operations before the release. Plain LDREX/STREX do not
supply those ordering semantics: a conventional lock needs `dmb sy` after
successful acquisition and before the releasing store. DMB orders accesses;
DSB additionally waits for completion where required, and ISB synchronizes
instruction/context changes. None prevents ownership races by itself.
SEV wakes a possible waiter; WFE is not an acquire barrier, a semaphore or
a reliable message count. Always recheck the condition and retain a timeout
mechanism that works even if no event arrives.

#### Bounded protocol and alternatives

[synchronization_probe.S](../tests/fixtures/synchronization_probe.S)
implements an acquire-exclusive/release-exclusive SRAM lock with a caller
supplied attempt budget, including zero-budget failure. Release uses STL
and SEV. Failure abandons the reservation with CLREX. It returns zero on
acquisition, one on timeout; the caller must not enter the critical section
on failure. Acquire/release mailbox helpers and a RAM-resident final park
routine are also explicit low-level functions.

[synchronization_badge.c](../tests/fixtures/synchronization_badge.c) is a
freestanding diagnostic, not an application runtime or a C11 atomic library:

- It selects shared `clk_sys = clk_ref = 12 MHz badge XOSC`, divisors one,
  and records the actual mux/divisor/status, CPUID, CONTROL, PRIMASK, MPU,
  ACTLR and VTOR values from both cores.
- It uses the documented section 5.3 FIFO echo handshake to launch core 1
  with a separate 4 KiB stack/vector table. FIFO polling, retries and error
  handling are bounded. FIFO registers carry messages; they are not lock
  words, and FIFO transfer alone does not replace SRAM publication ordering.
- Core 0 initially holds the lock. Core 1 must time out with a bounded
  attempt budget. An explicit cross-core ordinary write then invalidates a
  pending core-0 LDREX; the failed STREX must leave the competing value intact.
- Both cores attempt 1,024 protected updates. Each acquisition checks the
  payload checksum invariant, and the report requires exactly 2,048 updates,
  checksum 2,098,176, no per-core errors, and full progress from each core.
- Handshake words occupy separate 16-byte granules. Polling uses acquire,
  publication uses release/SEV; no unbounded WFE wait is used for progress.
  Core 1 parks in SRAM and is held in reset before flash programming removes
  XIP access. The ROM BOOTSEL recovery watchdog is scheduled before the probe.

SIO spinlocks can instead protect short critical sections via their special
read-to-claim/write-to-release semantics, followed/preceded by acquire/release
barriers. They are not a reason to add an instruction or use LDREX on SIO.
The datasheet section 3.1.4 (physical PDF pages 38-39) documents separate
Secure/Non-secure spinlock banks and **RP2350-E2**: certain SIO writes can
spuriously release spinlocks on A2. This fixture deliberately uses SRAM
exclusives rather than assuming those locks are safe on every revision.
FIFO provides four ordered words in each direction; SEV/WFE provides
notification. Both need an ownership/publication/error protocol.

No fairness, throughput or one-way timing claim follows from these tests.
The diagnostic does not enable DMA, interrupts or the MPU, nor qualify
Non-secure execution, other clocks, caches, RTOS context switching or
interrupt-context locking. A production lock must specify memory placement,
DMA ownership and interrupt policy: an interrupt on the lock-owning core
must not spin waiting for that same lock. `volatile` alone is not an atomic
or ordering primitive. C atomic lowering and its language-level memory-model
contract remain a separate compiler prerequisite.

#### Qualification and recovery status

Host qualification covers independent fixed halfwords, LLVM agreement for
register fields/permitted overlaps and every aligned word immediate,
listing verification, atomic output preservation on rejected inputs,
all byte/halfword/word forms under Cortex-M33 QEMU, CLREX invalidation,
zero-budget/contended timeout and lock recovery. Physical-image builds use
both assemblers and compare complete binaries, validate entry/vectors/stack
and confirm that the park routine executes in SRAM.

The final host run passes **11 BA05 tests**, **65 combined** synchronization,
BA03/BA04, listing, crossover and Compile Graph regressions with no skips,
the sanitized native matrix (**44 positive, 140 negative, 9 unsupported,
24 mutations**) and **six** maintained LLVM oracle fixtures.
A fresh `compile-graph/build/ba05-host-qualified` capture under the same
explicit `thumb-constants` policy as BA04 preserves all four firmware
binaries byte-for-byte: owned red square 624 bytes, owned maze 19,600 bytes,
LLVM red square 568 bytes and LLVM maze 8,704 bytes.
Compiler defaults, C atomic lowering and application behavior are unchanged.

```sh
make -C badge-compiler test-assembler-synchronization \
  LLVM_CLANG=/opt/homebrew/opt/llvm/bin/clang \
  LLVM_LLD=/opt/homebrew/bin/ld.lld \
  LLVM_OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy
```

With explicit permission, physical attempts selected only
`9BBF88DD17D66E7D`; badge `08DA26F86E57F5EA` was untouched.
The boot-only controls passed with fresh nonces. The initial full probe
reported a clock setup error before launching core 1; XOSC was disabled
at firmware entry. XOSC initialization was then added using the existing
badge startup sequence. A static PSM alias-address error was also corrected.
Neither correction establishes the cause of the remaining hang.
Two subsequent attempts did not automatically return to BOOTSEL. The user
recovered the selected badge manually; their recovered flash reports contain
only the pre-probe checkpoint (`progress = 0x102`, result `0xffff0002`).
At that point there was no passing two-core contention/ordering/progress
report. Physical retries were stopped rather than treating the pending
report as success. Startup, clock switching and recovery were subsequently
qualified with durable phase diagnostics before returning to contention.

That isolated investigation is now **physically qualified**. The first
ladder passed launch/park and the single-core probe, then failed cross-core
invalidation (STREX status zero, payload overwritten with `0xdeadbeef`).
The MPU was disabled and diagnostic startup lacked `ACTLR.EXTEXCLALL`.
After documented per-core initialization, the fresh
`build/ba05-startup-extexclall/results.json` campaign passed its boot control
and all seven stages: inherited-clock core launch, single-core exclusives,
cross-core invalidation, XOSC enable, safe clock switching, and
watchdog-only recovery with inherited and switched clocks.
The competing store now yielded STREX status **1**, preserving
`0x12345678`. Both cores recorded ACTLR `0 -> 0x20000000`; core 0 restored
zero before publication. No full contention worker is compiled into these
stage images. Those results satisfied the startup prerequisite, not the
full-protocol exit criterion by themselves.

The separately authorized full campaign
`build/ba05-contention-qualified/results.json` then passed its boot control
and bounded two-core synchronization probe. Both cores completed exactly
**1,024** updates, the counter was **2,048**, the checksum **2,098,176**,
and all per-core errors and fault registers were zero. The deliberately
held lock timed out on both cores before release and successful work;
the competing write defeated STREX (status **1**) without changing its
`0x12345678` payload. Both cores reported the expected privileged,
interrupt-unmasked, MPU-disabled configuration and enabled external
exclusives, with shared system/reference XOSC selection and divisors one.
Core 1 parked in SRAM and was held in reset before publication. Core 0's
ACTLR restoration readback and mask/control preservation checks passed.
Automatic BOOTSEL recovery and full original-flash readback verification
passed. **The BA05 exit criterion is now satisfied.**

The executed owned-assembled firmware matched the complete LLVM-assembled
image byte-for-byte, SHA-256
`90d00553fefea70182ee157736979a3fa0605c0aab0367cfd2fc7162f4bce812`.
This is functional evidence for the specified bounded protocol on the
selected badge, not fairness, performance, DMA/interrupt/MPU/Non-secure
qualification or C atomic lowering.

Report version **6** remains 256 bytes. Words 56/57 now identify ACTLR
(not the old version-5 CCR fields). In ladder modes, words 24/25 hold
core-0 original/restored ACTLR and word 26 holds core-1 original ACTLR when
launched. Full-protocol words 24-26 retain checksum/progress semantics.
Validators reject changed unrelated bits, missing external exclusives or
failed restoration. Host tests exercise the actual C setup/restoration and
clock-control functions, readback/privilege/MPU errors, report mutations and
all seven byte-identical LLVM/owned image pairs.

See the [root hardware runtime guide](../../docs/rp2350-runtime.md)
for exact clock snapshots, watchdog-only evidence and remaining boundaries.
This diagnoses the isolated invalidation failure and qualifies the corrected
startup; it does not retroactively locate the earlier uninstrumented hangs.

Evidence is retained in ignored
`build/ba05-hardware-flash-report/results.json`,
`build/ba05-hardware-xosc/results.json` and
`build/ba05-hardware-psm/results.json`, plus the failed
`build/ba05-startup-ladder/results.json` and passing
`build/ba05-startup-extexclall/results.json` and
`build/ba05-contention-qualified/results.json`. All campaigns' original
16 MiB flash was restored and readback-verified against SHA-256
`91f0dfce98cb2bea2fa0bbb11a27ac04f59dce17a3b4b960517d0fef157a14cd`.
Both badges are left in BOOTSEL. Reports bind sources, images, nonces and
restoration; later source edits do not retroactively qualify those attempts.

The [runner](../tools/synchronization.mjs) uses the existing full-flash
backup, temporary final 4 KiB report sector and restoration mechanism.
Hardware invocation requires new evidence directories and explicit permission:

```sh
node badge-compiler/tools/synchronization.mjs --build NEW_IMAGE_DIRECTORY
# Requires permission; replaces the application and final flash sector.
node badge-compiler/tools/synchronization.mjs --hardware-flash-report \
  BOOTSEL_SERIAL NEW_EVIDENCE_DIRECTORY
# Isolated startup/recovery only; stops at the first failure.
node badge-compiler/tools/synchronization.mjs --hardware-startup-ladder \
  BOOTSEL_SERIAL NEW_LADDER_DIRECTORY
# If automatic recovery fails, manually enter BOOTSEL, then restore:
node badge-compiler/tools/synchronization.mjs --restore EVIDENCE_DIRECTORY
```

Recovery is explicitly reported as incomplete if BOOTSEL or full readback
verification fails. The restore command only targets the recorded serial and
hash-checked full-size backup belonging to that campaign. Mocked host tests
verify serial isolation, backup drift rejection and corrupt readback rejection;
they do not claim physical recovery on a hung core.

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
