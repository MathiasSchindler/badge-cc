# PO10: small-function attribution and production inventory

**Status: implemented and qualified; host-only.** No compiler transformation,
default, application algorithm or badge flash changes are made by PO10.

The maintained [capture/verifier](../tools/small-function-attribution.mjs) reuses
PO00 invocation/tool resolution, PO02 fixed-IR flags, disassembly and emission
tracing, and the existing C06 assembly/frame inspector. It is not a second
compiler pipeline or a demo recognizer.

## Qualified result

[The qualified capture](../build/po10-qualified-small-functions-v2/results.json)
accounts for all six red-square functions and inventories all **37 modules,
554 functions and twelve-image source coverage** from the qualified PO06+PO07
corpus. The corpus is reused, not rebuilt. Its input IR/assembly hashes agree
with the [promoted-default replay](../build/po09-promoted-replay/results.json).
The actual source files are hash-checked and archived with those module inputs.

| Route | Loaded bytes | C function bytes | Constant objects | Startup/metadata/inter-symbol bytes |
| --- | ---: | ---: | ---: | ---: |
| Owned optimized IR -> owned backend | 1,432 | 1,198 | 102 | 132 |
| **The exact same optimized IR -> LLVM backend, normal IR passes disabled** | **560** | **326** | **102** | **132** |
| Owned initial IR -> LLVM `default<Oz>` -> LLVM backend | 612 | 378 | 102 | 132 |
| Clang initial IR -> explicit LLVM `default<Oz>` -> LLVM backend | 560 | 326 | 102 | 132 |
| Archived ordinary Clang `-Oz` image | 560 | 326 | 102 | 132 |

The **entire 872-byte observed gap is reproduced on identical optimized IR**,
with a common owned linker and identical startup objects. LLVM routes use
LLVM's assembler; a maintained same-assembly control also assembles the fixed-IR
LLVM output with our assembler and verifies **identical loaded bytes**.
This supports prioritizing the backend, rather than blaming preprocessing or
requiring a frontend rewrite to obtain comparable code sizes.

Equal size is **not** byte identity. The three 560-byte LLVM routes have
different binary hashes. No new execution, cold-boot or speed qualification
is claimed for crossover images; the archived ordinary images retain their
previous physical evidence.

LLVM optimization of owned initial IR does not improve this size result:
it removes the separate `write_byte` function and grows the combined image
to 612 bytes. Clang's initial IR includes the guarded `noinline` policy from
the source; owned initial IR does not. The 52-byte difference is evidence
for investigating sharing/inlining and attributes, not a uniquely isolated
frontend-quality penalty. No controlled attribute-only ablation was performed.
PO16 must compare whole-image costs rather than assume LLVM's inlining policy
is always best.

The signed stage deltas are:

- Fixed-IR backend: **+872 bytes** for owned.
- LLVM optimizer recovery on owned initial IR: **-52 bytes** relative to the
  fixed-IR comparison, because this recovery route is larger.
- Frontend/attribute route difference: **+52 bytes**.
- Explicit pipeline versus archived Clang reference: **0 bytes**.

They sum to 872, but are order-dependent crossover differences, not four
independent causal percentages.

## All six helper gaps

| Function | Owned | Fixed-IR LLVM | Backend gap |
| --- | ---: | ---: | ---: |
| `delay_cycles` | 100 | 10 | 90 |
| `write_byte` | 134 | 32 | 102 |
| `command` | 92 | 24 | 68 |
| `commands` | 214 | 40 | 174 |
| `fill` | 104 | 36 | 68 |
| `bare_main` | 554 | 184 | 370 |
| **Total C functions** | **1,198** | **326** | **872** |

Each function has an ELF-symbol byte ledger containing disassembled
instruction categories, literal/data bytes and explicit undecoded bytes.
**Undecoded function bytes are zero in every captured route.**
The ledgers reconcile with full loaded ELF/raw binary bytes; constant objects
and startup/padding are kept outside the C-function comparison.

Grouped static byte observations for the fixed-IR comparison:

| Category | Owned | LLVM | Difference |
| --- | ---: | ---: | ---: |
| Saves/restores, SP/frame-base arithmetic and frame accesses | 136 | 12 | +124 |
| Constant materialization, PC-relative loads and literal data | 404 | 134 | +270 |
| Register copies | 168 | 12 | +156 |
| Branches/returns and predicate/flag control | 256 | 36 | +220 |
| Extensions | 36 | 2 | +34 |
| Calls | 60 | 54 | +6 |
| Non-frame memory instructions | 66 | 38 | +28 |
| NOP/WFI/barrier instructions, including possible padding nops | 14 | 16 | -2 |
| Other computation | 58 | 22 | +36 |
| **Total** | **1,198** | **326** | **+872** |

These categories do not claim that every instruction in a group is redundant,
that a load/store is nonvolatile, or that byte savings predict elapsed time.
Frame-base classification uses owned `r7` or LLVM SP/explicit frame-base
assembly metadata; a free LLVM `r7` pointer is not assumed to be a frame.

Byte-preserving PO02 tracing reproduces the archived owned object exactly.
The trace records:

- `delay_cycles`: an 8-byte local frame, 24 saved bytes, and one private
  store/reload pair associated with the assembly operation.
- `write_byte`, `command`, `fill`, `bare_main`: zero local bytes, but
  24/24/32/24 saved bytes respectively.
- `commands`: an 8-byte frame, 32 saved bytes, a scalar home store/reload,
  byte-load/extension transport and an unfused pointer branch predicate.
- Branch chains and repeated address materialization in the helpers' own
  IR-to-assembly operations, rather than guessed from binary size alone.

Scalar home accesses are not automatically called allocator eviction spills.
Natural-loop legality, flag hazards, live ranges and MMIO timing margins
remain proofs for PO11-PO15.

## General production inventory

This is the archived, ordinary production corpus, **excluding the six demo
functions**. Categories overlap and must not be added.

| Candidate pattern | Functions | Modules |
| --- | ---: | ---: |
| No ordinary calls, including assembly-sensitive leaf candidates | 222 | 36 |
| Assembly-bearing | 14 | 13 |
| Supported NOP-text candidates | 3 | 2 |
| Volatile operations | 89 | 26 |
| Constant pointer conversions | 105 | 30 |
| Adjacent pointer comparison/branch pairs | 44 | 17 |
| Backward textual control edges | 232 | 34 |
| Lexically call-live scalar candidates | 257 | 33 |
| Zero local frames with register saves | 294 | 35 |

The complete per-function inventory includes source paths, frame/saved/outgoing
bytes, frame uncertainties and up to twelve examples per pattern.
Specific ordinary examples include:

- NOP candidates in shared display `delay_cycles`/`lcd_write_byte` and USB
  `arm_buffer`.
- GPIO/register-address work in button mapping, shared display, diagnostics
  and board bring-up.
- Pointer branches in ROM chip identification and touch initialization.
- Leaf/frame candidates and call-live work across ordinary applications,
  platform drivers, shared code and USB.

Backward textual edges may include acyclic backwards jumps; lexical
call-live candidates may overestimate actual CFG liveness. These are search
inventories, not optimizer legality results. An empty assembly constraint
string does not prove arbitrary assembly preserves registers. Memory
barriers and unknown assembly remain explicitly outside NOP-based reasoning.

## Delta-focused gates established by PO10

The report persists the qualification contract for later implementation:

1. Targeted mechanism tests first: zero unnecessary eligible leaf-frame work,
   zero proved-NOP live-value spills, zero redundant supported branch-only
   boolean round trips. Preserve negative/effect/ABI/resource cases.
2. One necessary 37-module/twelve-image corpus, mixed-ABI, frame/call/spill
   and production-benefit gate when compiler semantics/emission change.
3. Hardware baseline and candidate only, with the declared two seeds,
   two repeats, board-role swaps, exact noise/control-drift comparisons and
   full-flash restoration. Interactions require separate qualification where
   they change emission.
4. Audit delay/strobe contracts when affected. Faster instructions must not
   silently invalidate required hardware waits.
5. Build once per source/tool/header/flags/resolved-policy/link-script image
   identity; runtime seeds/repeats/roles are not build keys. Hash-verify reuse.
   Reuse byte-identical qualified images instead of reflashing solely to
   reconfirm unchanged bytes.

This is a persisted acceptance contract, **not** an implementation of PO18's
future hardware runner or image cache, and not automatic default promotion.
PO10 itself performs no corpus rebuild, hardware campaign or timing estimate.

## Reproduction and persistence

From the project root:

```sh
export PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH
make -C badge-compiler all
node badge-compiler/tools/small-function-attribution.mjs \
  --capture badge-compiler/build/po10-new-capture
node badge-compiler/tools/small-function-attribution.mjs \
  --verify badge-compiler/build/po10-new-capture/results.json
node badge-compiler/tools/small-function-attribution.mjs \
  --verify-current badge-compiler/build/po10-new-capture/results.json

PO10_REPORT=badge-compiler/build/po10-new-capture/results.json \
  node --test badge-compiler/tests/small-function-attribution.test.mjs
```

Capture requires the existing red-square report, PO09 corpus and promoted
replay at their documented default paths. It rejects changed inputs, tools,
source identities, incomplete inventories and existing output directories.
It archives inputs and executable bytes before generating outputs, executes
owned tools from those copies, and builds a **capture-local** attribution
binary through the existing Makefile recipe. Shared compiler/trace binaries
and historical reports are not overwritten.

LLVM executable bytes and versions are archived, but invocation uses the
original driver paths for dynamic-library resolution and driver-name dispatch.
OS/LLVM dynamic libraries are not a self-contained archived runtime.
`--verify` checks the persisted artifacts and recomputes ledgers/inventories;
`--verify-current` additionally rejects source/executable drift.

The qualification passed **seven PO10 tests**, including actual-report
hash/inventory/contract/stage/assembler-control tamper rejection, and **five focused existing
PO02 helper tests**. The editor adapter did not discover Node tests, so the
maintained Node runner was used. Two failed host-only development captures
remain archived: LLVM driver-name resolution and an absolute-path verification
bug. Neither is accepted evidence; the qualified capture passes independently.
