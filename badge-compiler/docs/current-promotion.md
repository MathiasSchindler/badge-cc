# Owned compiler default promotions

## November default update

The owned compiler now defaults to `ssa` and
`direct/global-compact/thumb-constants/word`. The IR pipeline adds
`global-memory`, `sret-forward`, `compact-inline`, `low-bits`, `linear-bias`,
`small-aggregate`, `dead-instructions`, and `divrem-pairs`, in that order,
after the prior eight passes. This fills the 16-pass limit; use
`badge-opt --default-passes` to query the full list. The top-level firmware
build still defaults to Clang. `loop-bounds`, `loop-cost`, profile-guided
`hot-inline`, SRAM placement, and GPIOC are not enabled by this update.

The combined profile shrank **all 33 verified firmware images**
(27 applications and six microbenchmarks), saving **282,608 loaded flash bytes**
in aggregate. The IR-only profile grew `micro-calls` by 8 bytes, so it was not
promoted on its own. The implicit and explicit combined profiles produced
29 byte-identical images; the only differing bytes in the other four encoded
regenerated build IDs.

| Firmware | Previous default (bytes) | New default (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 23,804 | 16,104 |
| IMU Demo | 18,692 | 12,240 |
| Diagnostics | 41,560 | 30,056 |

Every measured QEMU workload reached fewer instructions with matching
functional results. The four-level Maze physics probe dropped from
41.72 million to 21.20 million instructions, and live full-screen IMU
pixel generation dropped from 157.66 million to 78.91 million. These are
instruction counts, not physical elapsed times. Across 1,532 matched
functions, the largest local frame plus register-save area fell from
2,816 to 2,768 bytes; 56 functions grew individually, by at most 16 bytes.
Local frames do not establish worst-case call-chain stack use.

One RP2350 badge ran each of the application and general suites in
old/new/new/old boot order. All **18 workloads** were faster: the slowest
candidate sample was faster than the fastest control sample in every
workload. Results and clock settings matched within each suite. The table
shows the full observed warm-timer sample ranges across both boots of each
profile, not just the medians.

| Workload | Previous default (ticks) | New default (ticks) | Median time reduction |
| --- | ---: | ---: | ---: |
| Maze generation | 41,907-41,956 | 27,307-27,326 | 34.8% |
| Maze physics and scripted input | 61,735-61,806 | 25,448-25,484 | 58.8% |
| Maze RAM rendering | 101,536-101,566 | 64,226-64,244 | 36.8% |
| Maze display transfer | 134,245-134,270 | 112,633-112,665 | 16.1% |
| Reached Maze frame | 195,262-195,362 | 152,258-152,297 | 22.0% |
| Production Diagnostics graphics | 142,589-142,636 | 96,628-96,643 | 32.2% |

The twelve general workloads took 12.6-52.1% less time by median. These
results are against the previous owned default, not LLVM or the older
two-badge role-swapped gate. The campaign did not test radio, GPIOC, or
cold-XIP timing. The badge's original 16 MiB flash was restored, verified
against an independent full readback, and left in BOOTSEL.

To reproduce the previous owned defaults, build in a fresh output directory
with:

```sh
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function \
BADGE_CODEGEN_SCALAR_SELECTION=thumb-widths \
  bash badge-compiler/scripts/build-owned.sh
```

### Unsigned power-of-two follow-up

`divrem-pairs` now also strength-reduces unflagged unsigned i32 division and
remainder by literal powers of two, without adding a pipeline entry. The
candidate accounts for already-shared quotient/remainder work. It is
rejected for a function if final code bytes, frame, register saves, or
spills increase. The standalone `target-strength-reduce` pass is still
available for isolation.

Against the previous sixteen-pass default, 17 of 33 verified images shrank,
saving 780 loaded flash bytes total. The other 16 stayed the same size;
no image grew. Of 129 matched modules, 23 shrank in executable size (548
bytes total). Maze shrank from 16,104 to 16,092 bytes. The QEMU arithmetic
oracle passed all 32 power-of-two divisors; the production four-level Maze
probe matched all digests and reached 12 fewer instructions
(21,196,879 to 21,196,867). These results do not establish a physical speedup.

In the standalone replay, SCCP did not grow executable code, but its earlier
string-timing caveat still needs a physical retest. Local CSE and broader
loop hoisting grew some modules; `loop-bounds` and profile-free `hot-inline`
did not improve this corpus. They are not promoted by this update.

### Short Thumb instruction and tiny-leaf follow-up

Five instruction selections are enabled in the default `thumb-constants`
backend. The following one-at-a-time measurements use the same 129 archived
optimized IR modules. Bytes are executable section sizes before linking.

| Selection | Net bytes saved | Modules smaller / larger |
| --- | ---: | ---: |
| `RSB` for an encodable constant minus a value | 286 | 36 / 0 |
| `BIC` when the complement of an AND mask is encodable | 220 | 25 / 0 |
| `UBFX` for a single-use logical shift and contiguous low mask | 116 | 16 / 0 |
| `MLS` for an adjacent, single-use product subtracted from an accumulator | 52 | 4 / 3 |
| `TST` for a single-use mask compared for equality or inequality with zero | 354 | 47 / 3 |
| Combined selector | 1,018 | 78 / 0 |

The selector does not fuse multi-use intermediates or change the order of
volatile operations. Masked comparisons are not fused in functions containing
inline assembly. Independent LLVM assembly and a Cortex-M33 execution test
check the encodings and results.

The first full build lost inlining of a tiny Nametag helper: the new
encoding made the inlined module two bytes larger at the admission point,
and leaving the calls in increased reached work. The default
`compact-inline` policy now has one exception per module: it may spend at
most two encoded bytes to eliminate all of at least two calls to a pure
one-argument i32 leaf with at most four arithmetic instructions. Frame,
register-save and spill growth still reject the candidate, and a
size-saving candidate takes precedence. Only one module used the
exception in this replay, eliminating five calls while preserving the
firmware sizes of the selector-only candidate.

The final from-source build saved **1,416 loaded flash bytes** across 24 of
33 verified images; none grew. All six microbenchmark payloads were
byte-identical. No matched function's measured local frame plus save area
grew; these measurements do not establish a worst-case call-chain bound.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 16,092 | 16,040 |
| IMU Demo | 12,240 | 12,204 |
| Diagnostics | 30,000 | 29,920 |
| Compiler Test | 16,856 | 16,820 |

With matching results, the four-level Maze QEMU probe reached
21,196,267 instructions instead of 21,196,867, and the IMU renderer
reached 669,523 instead of 671,235. All three IMU pixel paths also reached
fewer instructions. The production Nametag blend probe went from
1,291,423 to 1,283,294 instructions, and glyph coverage from 56,925
to 55,901; these totals include the unchanged oracle harness. These are
offline instruction counts, not physical timing or GPIOC qualification.

### Constant arithmetic and bit masks

Six more selections are active under `thumb-constants`. The fixed-IR
ablation compiled the same 129 optimized modules with each change alone.
The control reproduced the previous executable sizes in every module.

| Selection | Net bytes saved | Modules smaller / larger |
| --- | ---: | ---: |
| `UBFX` for a contiguous low mask not encodable by `AND` or `BIC` | 272 | 16 / 0 |
| Shifted `ADD` for multiplication by `2^k + 1` | 52 | 7 / 0 |
| Shifted `RSB` for multiplication by `2^k - 1` | 168 | 17 / 0 |
| Shifted `SUB` or `RSB` for an adjacent scaled subtraction | 12 | 3 / 0 |
| Register `BIC` or `ORN` for a single-use inverted operand | 8 | 2 / 0 |
| `CMN` for equality or inequality with an encodable negated constant | 6 | 1 / 0 |
| Combined | 498 | 36 / 0 |

Scaled subtraction requires a nonconstant unscaled operand: the wider
candidate increased two objects when it forced an immediate into a
register. Standalone `MVN` selection was dropped because XOR with all
ones already encoded as one instruction and the substitution saved no
bytes. The focused C/IR oracle checks modular arithmetic, reversed
subtraction, shared operands, and ordered comparisons; emitted encodings
also match LLVM's assembler.

All 33 from-source UF2 builds verified. **24 shrank and none grew**,
saving **652 loaded flash bytes**. All six microbenchmark payloads
were byte-identical. No local frame or register-save area grew among
1,532 matched functions; that is not a call-chain stack bound.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 16,040 | 16,020 |
| IMU Demo | 12,204 | 12,192 |
| Diagnostics | 29,920 | 29,884 |
| Compiler Test | 16,820 | 16,796 |

The four-level Maze probe matched all four digests (21,196,267 to
21,196,263 reached instructions). The IMU renderer changed from 669,523
to 669,355 instructions, and all three pixel paths improved with matching
digests: live full-screen generation reached 78,592,462 instead of
78,889,670. Nametag's blend probe was unchanged; the glyph-coverage
probe went from 55,901 to 55,389 instructions. These are Cortex-M33 QEMU
counts. This round did not flash or time a physical badge.

### Direct integer casts and wide comparisons

The default `thumb-constants` backend now lowers these integer operations
directly. The per-rule ablation holds the input IR fixed across 129 modules
and uses the corrected peephole in both the control and each variant.

| Selection | Net executable bytes saved | Modules smaller / larger |
| --- | ---: | ---: |
| Truncate into an 8- or 16-bit destination with `UXTB`/`UXTH` | 2,524 | 91 / 0 |
| Read the low word of an i64 truncation directly into the destination | 24 | 4 / 0 |
| Forward normalized narrow values through zero extensions up to i32 | 1,010 | 40 / 4 |
| Store the resident low word directly when zero-extending to i64 | 28 | 4 / 0 |
| Sign-extend a resident byte or halfword directly when widening to i64 | 10 | 1 / 0 |
| Compare high words first for 64-bit equality or inequality | 8 | 1 / 0 |
| Compare high words first for unsigned 64-bit ordering | 48 | 3 / 0 |

Wide comparisons use a one-instruction `IT EQ` block: the low-word compare
runs only when the high words compare equal. Signed 64-bit ordering still
uses its previous signed-high/unsigned-low lowering. The cast paths keep
the existing scalar home, register and ABI contracts.

The peephole now recognizes `UXTB.W` and `UXTH.W` as flag-neutral writes to
their destination registers. This preserves safe constant shrinking and
address folding without carrying facts through an overwritten base. By
itself this saved six bytes in three modules. Before that fix, five of the
combined candidate's objects grew; the final combined selector saved
**3,646 executable bytes across 94 modules**, with no object growth.

All 33 firmware images were rebuilt and verified: **25 shrank**, saving
**4,928 loaded flash bytes**, and none grew. All six microbenchmark payloads
were byte-identical. Neither local frames nor register-save areas grew in
1,532 matched functions; this is not a worst-case stack bound.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 16,020 | 15,832 |
| IMU Demo | 12,192 | 12,120 |
| Diagnostics | 29,884 | 29,568 |
| Compiler Test | 16,796 | 16,524 |

The four-level Maze QEMU replay matched all digests and reached
21,096,547 instructions instead of 21,196,263. IMU live full-screen pixel
generation went from 78,592,462 to 76,402,838 instructions; the error-screen
and dirty-region paths improved too. The exact Nametag blend and coverage
objects passed their reference probes, saving 16,258 and 2,048 reached
instructions respectively. LLVM encoding checks, signed and unsigned
boundary oracles, and the Maze gameplay and collision checks passed.
No physical timing or flash deployment was performed in this round.

### Wide integer arithmetic

Six more rules are enabled under `thumb-constants`. This ablation uses
129 fixed optimized IR modules; the control reproduced the previous
executable sizes. No module grew under any retained rule.

| Selection | Net executable bytes saved |
| --- | ---: |
| Shifts below 32: skip the unused count registers and combine the cross-word shift with `ORR` | 26 |
| Shifts of 32 or more: load only the word that contributes to the result | 40 |
| Signed power-of-two division: generate the negative rounding bias from its sign | 48 |
| Wide negation: use `RSBS`, `MVN`, and carry into the high word | 6 |
| Constant bitwise operations: leave identity halves alone and encode other halves directly where possible | 4 |
| Wide multiply: accumulate the cross products before `UMULL` writes the result registers | 6 |
| Combined | 134 |

The candidate for adding a 32-bit constant to an i64 value saved no
object bytes and was dropped. The retained fast paths pass independent
LLVM encoding checks and target execution at signed boundaries, around
the 32-bit word boundary, and with carry/borrow and nontrivial high words.
The existing mixed-compiler AAPCS and wide division/remainder oracles
also pass.

Three of 33 verified firmware images shrank by **140 loaded flash bytes**
in total. None grew, no frame or save area grew among 1,532 matched
functions, and all six microbenchmark payloads were byte-identical.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Breakout | 10,536 | 10,488 |
| IMU Demo | 12,120 | 12,072 |
| Compiler Test | 16,524 | 16,480 |

The Tilt Breakout probe ran 4,096 paddle updates against an independent
integer reference. With matching position and velocity on every update,
the production function and its two division helpers reached **32,465,694
instructions instead of 35,284,242 (8.0% fewer)**, excluding the unchanged
reference harness. The IMU renderer reached 669,277 instead of 669,349
instructions. Maze and the three IMU pixel paths matched their prior
digests and instruction counts. These are Cortex-M33 QEMU measurements,
not elapsed time on the badge.

### Constant-divisor specialization

The default `wide-division` pass now uses shifts and masks for unsigned
power-of-two divisors, an exact arithmetic shift for exact signed division
by a positive power of two, and a sign-derived bias for signed remainders
and larger signed powers of two. The existing direct backend path still
handles ordinary signed division by smaller positive powers of two.

Other known positive divisors below `2^31` use a short helper. If the
dividend magnitude fits in one word, a native `UDIV` is sufficient.
Otherwise, native 32-bit division handles the high word and a 32-step
restoring loop handles the low word. A shifted remainder is always less
than twice the divisor, so it fits in 32 bits. The loop reuses the quotient
bit for subtraction, which the backend selects as `MLS`, and folds adjacent
shifts into `ORR`. Nonnegative signed dividends skip the sign corrections.
Variable divisors, large non-powers of two, and unsupported signed cases
retain the general helper.

All stages matched an independent reference on **4,096 Tilt Breakout
paddle updates**. Instruction counts exclude the unchanged reference
harness.

| Stage | Breakout production QEMU instructions | Loaded flash (bytes) |
| --- | ---: | ---: |
| Previous default | 32,465,694 | 10,488 |
| Word-divisor helper | 3,551,184 | 10,124 |
| Fused restoring loop | 2,944,784 | 10,112 |
| Signed fast entry (final default) | 2,801,468 | 10,084 |

All 33 firmware images verified. Breakout saved **404 bytes**, and no image
grew. Maze and IMU Demo payloads were byte-identical to the previous
default. No measured local frame plus save area grew among matched
functions. The boundary oracle exercises the native and restoring paths,
signed minima, carry across words, and both sides of the divisor cutoff;
the existing LLVM verifier, mixed AAPCS and general division/remainder
oracles also pass. These are Cortex-M33 QEMU instruction counts, not badge
timings.

### Runtime-divisor specialization

The default general divider now checks for a nonzero word divisor below
`2^31` and shares the constant-divisor helper. For larger divisors it
returns zero if the numerator is smaller. Otherwise it aligns the divisor
using the difference in leading-zero counts and runs one step per possible
quotient bit, at most 33 for this path. Operands with the same bit length
and a numerator at least as large as the divisor return one without a loop.
The divide-by-zero trap is retained.

The backend lowers i64 leading-zero count and variable left shift directly.
It also shares one condition across both words of a wide select, and
extracts the sign bit directly for `icmp slt i64 x, 0`. The latter two changes
improved size and instruction counts after the divider itself was working.

Eight Cortex-M33 QEMU workloads each check 256 independent integer
results. Counts cover the quotient/remainder functions and helpers only.

| Runtime operand group | Previous instructions | Final instructions |
| --- | ---: | ---: |
| Dividend fits in one word, word divisor | 2,043,648 | 19,114 |
| Wide dividend, word divisor | 2,043,648 | 172,032 |
| Wide dividend, very small divisor | 2,043,648 | 172,032 |
| Numerator below divisor | 2,022,177 | 15,104 |
| Similar-width large operands | 2,019,840 | 52,330 |
| Large dividend with a wide quotient | 2,037,840 | 730,727 |
| Signed division | 2,057,385 | 314,956 |
| Signed and unsigned remainder | 2,046,294 | 335,597 |

Dispatch alone slightly increased the wide-quotient workload; it was not
adopted without alignment. A leaner normalized helper lost much of the
small-divisor speedup. Removing the immediate quotient-one exit, adding
a divide-by-one check, or branching separately for signed corrections
did not justify their size/speed trade-offs. The final object containing all
four runtime operations is **1,266 bytes versus 1,038**: this is a speed
default, not a claim that every future image using runtime division shrinks.

All 33 production and microbenchmark UF2s verified. Three images saved
**196 loaded flash bytes** total; none grew. Both Nametag images saved
80 bytes each, with unchanged blend and glyph-coverage probe results and
counts. Tilt Breakout saved 36 bytes (10,084 to 10,048), and its 4,096
checked paddle updates used 2,769,047 production instructions instead
of 2,801,468. Maze and IMU Demo were byte-identical to the previous default.
No measured local frame plus save area grew among matched functions.

Randomized signed and unsigned quotient/remainder checks, all 64 valid
variable shift counts, leading-zero boundaries including zero, LLVM
assembly, and mixed-compiler ABI tests pass. No physical badge was
reflashed or timed.

### Address and register-copy folding

The default `thumb-constants` backend folds an in-place address add
into its transfer when the old register is dead or the load replaces it.
It also removes temporary moves used only as a store value, an address base,
or a load result. Access widths and memory order are unchanged. The bounded
overwrite check also recognizes byte/halfword loads and zero extensions that
replace the old value without reading it; opaque assembly and uses of the
old value still block the rewrite.

| Fixed-IR selection | Executable bytes saved | Modules smaller / larger |
| --- | ---: | ---: |
| In-place address add folding | 644 | 54 / 0 |
| Store-value copy removal | 562 | 51 / 0 |
| Address-base copy removal | 428 | 55 / 0 |
| Load-result copy removal | 18 | 4 / 0 |
| Combined, with expanded overwrite proof | 1,686 | 102 / 0 |

The expanded proof saved 48 bytes beyond the same four folds with the
previous overwrite rules. Negative displacements, adding two immediate
offsets, and alternate signed indexed encodings were tested separately
but saved no executable bytes in these 129 modules; they were dropped.

The base-copy fold exposed an inlining size tie for Breakout's timer
helper, costing eight register-save bytes if the call was retained.
`compact-inline` can now remove repeated calls to a no-argument leaf
that returns one word load at the same encoded size. The load may use a
literal address, and the existing frame, save, and spill gates still
apply. This kept the timer read inlined without adding a save area or
changing the number or order of volatile reads.

**26 of 33 verified firmware images** shrank, saving **2,528 loaded flash
bytes**. No image, object, or measured local frame or register-save area
grew among the 1,531 matched functions. All six microbenchmark payloads
were byte-identical.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 15,832 | 15,800 |
| Tilt Breakout | 10,048 | 10,016 |
| IMU Demo | 12,072 | 12,044 |
| Diagnostics | 29,568 | 29,496 |

The four-level Maze replay reached 21,095,461 instead of 21,096,547
instructions. The IMU renderer went from 669,277 to 667,995, and
Breakout's 4,096 checked paddle updates reached 2,761,085 instead of
2,769,047 production instructions. Results matched; all three IMU pixel
paths and both Nametag font probes also had unchanged instruction counts.
Independent LLVM encoding and Cortex-M33 memory checks, mixed AAPCS,
and Maze gameplay and collision oracles passed. No badge was reflashed
or physically timed.

### Immediate stack adjustments and paired stack transfers

The default `thumb-constants` selector adjusts SP directly for local
frames and outgoing arguments. It uses a 16-bit immediate through
508 bytes, `ADDW`/`SUBW` through 4,092 bytes, and retains the register
sequence outside those ranges. These forms preserve flags and stack
alignment. The stack reporter models both new encodings, including
outgoing bytes at each call site.

The backend also combines adjacent, ascending word transfers to direct
stack homes into `LDRD` or `STRD` when at least one original instruction
was wide. It leaves pairs of narrow transfers alone, and does not pair
non-stack accesses, accesses that straddle an unsupported displacement,
or loads that overlap the base or each other's destinations.

| Fixed-IR candidate | Executable bytes saved | Modules smaller / larger |
| --- | ---: | ---: |
| Narrow local frame adjustment | 7,440 | 112 / 0 |
| Wide immediate local frame adjustment | 120 | 11 / 0 |
| Narrow outgoing argument adjustment | 8,910 | 65 / 0 |
| Wide immediate outgoing argument adjustment | 0 | 0 / 0 |
| Adjacent stack loads | 204 | 25 / 0 |
| Adjacent stack stores | 220 | 26 / 4 |
| Combined | 16,888 | 112 / 0 |

The exact-size control reproduced all 129 baseline object sizes. Large
outgoing blocks did not occur in this corpus. Store pairing alone grew
four objects slightly; the combined default grew none, both on identical
IR and after rebuilding from source.

**26 of 33 verified firmware images** shrank, saving **21,948 loaded
flash bytes** total. All six microbenchmark payloads were byte-identical.
No measured local frame or register-save area grew among 1,531 matched
functions; this is not a worst-case call-chain stack bound.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 15,800 | 15,204 |
| Tilt Breakout | 10,016 | 9,708 |
| IMU Demo | 12,044 | 11,252 |
| Diagnostics | 29,496 | 27,520 |

Maze's four-level replay matched its digests, with 21,054,047 reached
instructions instead of 21,095,461. IMU full-screen pixel generation
matched its digest and reached 71,818,750 instead of 76,402,838
instructions (6.0% fewer); the other pixel paths and renderer improved
too. Nametag's blend and coverage oracles matched and did less work.
Breakout passed 4,096 checked paddle updates with 24,116 fewer production
instructions. LLVM encoding checks cover the stack immediate boundaries;
stack-balance, memory, mixed AAPCS, and gameplay checks pass. These are
Cortex-M33 QEMU results, not physical timings.

### Call and return frame simplification

Five backend changes are active under `thumb-constants`. Outgoing stack
arguments use the narrow SP-relative store when the register and offset
fit. A directly returned word or i64 call result can stay in the ABI
return registers instead of making a round trip through a local register
or stack home. This is not tail-call elimination: the callee still returns
to the caller, and outgoing arguments and saved registers are restored.

Functions with no outgoing stack adjustment can use SP for their frame
when all their frame references have legal SP-relative forms. When a
frame pointer is still needed, the explicit SP reset at return is omitted
only for compiler-generated bodies; inline assembly retains the old path.

| Fixed-IR candidate | Executable bytes saved | Modules smaller / larger |
| --- | ---: | ---: |
| Narrow outgoing argument stores | 1,970 | 62 / 0 |
| Direct word return forwarding | 1,030 | 34 / 0 |
| Direct i64 return forwarding | 8 | 1 / 0 |
| SP-relative frames across calls | 1,144 | 94 / 0 |
| Omit redundant epilogue SP reset | 872 | 101 / 0 |
| Combined | 4,534 | 104 / 0 |

The fixed-IR control reproduced all 129 baseline object sizes. In the first
full rebuild, cheaper calls caused Nametag's color helper to stop inlining.
For one tiny pure integer helper per module, `compact-inline` now allows one
Thumb halfword per removed call, capped at eight bytes total, without
relaxing the frame, save, or spill gates. Leaf reads still require an exact
size tie. Only one measured module used more than the previous two-byte
allowance; the fix restored both Nametag probes to their previous counts.

The final build saved **6,188 loaded flash bytes** across **26 of 33
verified images**. No image or object grew. Of 1,531 matched functions,
214 had a smaller local frame or register-save total, and none grew.
All six microbenchmark payloads were byte-identical.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 15,204 | 15,048 |
| Tilt Breakout | 9,708 | 9,612 |
| IMU Demo | 11,252 | 11,076 |
| Diagnostics | 27,520 | 27,184 |

With matching outputs, the four-level Maze replay used 54,206 fewer
Cortex-M33 QEMU instructions (21,054,047 to 20,999,841). Full-screen IMU
pixel generation used 1,976,802 fewer (2.75%); the other two pixel modes
and the renderer improved as well. All 4,096 Breakout paddle updates
matched, with 32,078 fewer instructions in the replay. Independent LLVM
encoding, mixed-compiler AAPCS, and Maze gameplay and collision checks
passed. No physical badge was flashed or timed.

### Comparison and branch lowering

The default `thumb-constants` selector uses two narrow moves under `ITE`
when a materialized comparison result is in a low register. Standalone
i32 equality with zero can instead use `CLZ` and a shift; masked comparisons
retain their `TST` path. An i64 comparison whose result is already zero or
one does not need another normalization.

Signed i64 ordering uses low-word subtraction and high-word
subtract-with-borrow. A branch can consume those flags directly rather
than materializing a boolean. For signed comparisons against zero where
only the sign matters, the fused branch compares only the high word.

| Fixed-IR candidate | Executable bytes saved | Modules smaller / larger |
| --- | ---: | ---: |
| Narrow conditional moves for boolean results | 2,424 | 92 / 0 |
| Signed wide subtract-with-borrow comparison | 192 | 1 / 0 |
| Remove redundant wide boolean normalization | 8 | 2 / 0 |
| Fuse a single-use wide comparison with its branch | 434 | 3 / 1 |
| Direct zero equality with `CLZ` | 114 | 16 / 0 |
| High-word-only sign test, incremental after combining | 32 | - |
| Final combined | 2,962 | 92 / 0 |

The control reproduced all 129 archived object sizes. A standalone i32
sign-bit candidate saved no production code and was dropped. The initial
wide branch cost one extra instruction per reached signed check in
Breakout's divider. Using only the high word for that check removed the
regression; the final combination grew no objects.

**26 of 33 verified firmware images** shrank, saving **4,596 loaded flash
bytes**. No image, object, or measured local frame or register-save area
grew. All six microbenchmark payloads were byte-identical.

| Firmware | Previous flash (bytes) | New flash (bytes) |
| --- | ---: | ---: |
| Tilt Maze | 15,048 | 14,952 |
| Tilt Breakout | 9,612 | 9,524 |
| IMU Demo | 11,076 | 11,000 |
| Diagnostics | 27,184 | 27,012 |

Matching four-level Maze replays reached 20,997,685 instructions instead
of 20,999,841. Breakout matched its reference on all 4,096 paddle updates
and reached 11,943 fewer instructions. IMU's renderer and three pixel
modes and both Nametag probes had matching outputs and unchanged counts.
LLVM encoding checks exercise signed and unsigned predicates across word
boundaries. Mixed AAPCS, branch/PHI, and Maze gameplay oracles passed.
These are Cortex-M33 QEMU results; no badge was reflashed or timed.

### Ten follow-up experiments: text, loops, and control flow

The control was the comparison-and-branch default above. The standalone
backend ablation reproduced all 129 control object sizes. The application
prototypes deliberately change the shape of temporary source copies, not
the checked-in firmware; they measure the opportunity before attempting
a reusable transform.

| Opportunity | Checked result and disposition |
| --- | --- |
| Repeated text scale | Cloning the IMU helper for scales 1, 2, and 3 reached 64,567,943 instead of 69,841,948 full-screen production instructions, at +248 executable bytes. Default `cross-function` now tries a bounded repeated-literal group and checks its bytes, frames, saves and spills with the current backend as well as the original cost profile. It admitted 31 calls in Diagnostics, but did not admit the IMU specialization. |
| Hoist formatting | A source-shape prototype prepared text once before the pixel loops. The complete live-screen replay, including preparation and its harness, reached 41,062,609 instead of 70,650,997 instructions; the error-screen and dirty-region digests also matched. The prototype uses 45 extra BSS bytes. No compiler hoist is enabled: the transform needs a proof that display calls and escaped sensor pointers cannot invalidate the snapshot. |
| Bounded decimal quotient/remainder | `bounded-decimal` applies an exact reciprocal only when an unsigned bound and matching quotient/remainder pair are proved. Boundary and exhaustive 16-bit target checks passed. The final exact IMU object grew eight executable bytes and the live screen reached 72,204,259 instructions instead of 69,841,948, so the pass stays explicit. |
| Split an interval at a call | The build-time `BADGE_EXPERIMENT_CALL_SPLIT=1` candidate saved 14 bytes in one object, and its exact report fixture reached 218 fewer instructions out of about 1.48 million. It added eight local-frame bytes and did not qualify as a default. |
| Private record return | A packed register-return prototype for the Maze fire rectangle grew the application object by 212 executable bytes and reached 21,009,892 instead of 20,981,843 instructions. All four digests matched. No compiler ABI change is enabled. |
| Pointer induction | A source-shape hazard scan reached 95,722 fewer instructions in the four-level replay but nearly doubled that function's stack traffic (105,426 to 198,706 accesses). The explicit `pointer-induction` pass checks final code, frame, saves and spills. It proposed six Maze recurrences but admitted none, and produced no changes across the 129-module replay. |
| Split collision rejection from the resolver | A source-shape prototype passed all four Maze digests and reached 20,094,649 instead of 20,981,843 instructions, saving 32 executable bytes in its application object. Generic automatic partial inlining is not enabled; the compiler still needs call-site CFG/PHI and memory-effect proofs. |
| Sibling branch | Unrestricted selection increased 13 objects in the isolated replay. Direct tail branches now require an explicit IR `tail` marker, register-only arguments with no use of r3, a single return and no escaping local object. Mixed-compiler execution and stack reporting passed. Ordinary C calls do not change ABI. |
| Dense switch table | The aggressive version selected 15 tables and grew executable code by 328 bytes across 14 modules. The default only selects a table if its worst-case veneers and guard fit the estimate for the already-folded comparison chain. No production switch qualified in this corpus; the dense shared-target fixture passes for holes, defaults and PHI edges. |
| Identical return tails | The default shares only a matching return-value move that falls through to the epilogue. This avoids adding an executed branch. The fixed-IR replay saved 166 bytes in 31 modules at 45 sites, with no growth. |

The two explicit IR experiments can be applied to saved optimized IR with
`badge-opt --passes=bounded-decimal` or `--passes=pointer-induction`; do not
append either to the already full sixteen-pass default.

The final default saved **352 loaded flash bytes across 18 of 33 verified
images** and 290 executable object bytes, without image, object, or
measured local frame/save-area growth. Maze was 14,952 to 14,944 bytes;
Diagnostics was 27,012 to 26,916 bytes. The final Maze, IMU pixel and Nametag
probes matched their prior outputs and instruction counts. The broader
test selection passed 84 of 85 cases; the graphics-clear instruction-count
assertion also failed on a freshly built, unchanged HEAD. Focused new
tests, native optimizer checks, mixed ABI, and Maze gameplay checks passed.
No badge was flashed or physically timed for these experiments.

### IMU labels and automatic Maze guard splitting

The control is the final owned default from the preceding round. IMU Demo
now prepares the ID and six formatted readings once before the initial
full redraw or the six dirty rectangles. This is a firmware change, not a
compiler hoist across sensor or display calls. The renderer-only and
transfer-only paths do not prepare labels. The exact-object Cortex-M33
oracle compares nine digests, including fresh samples, read failures,
and dirty rectangles. Both owned and Clang firmware build successfully.

The single-badge owned/new/new/owned comparison includes label preparation
inside the pixel timing window. Clock and output records match, and the
seven-sample ranges from both repetitions do not overlap for these phases:

| IMU phase | Baseline median (timer ticks) | Cached median (timer ticks) | Reduction |
| --- | ---: | ---: | ---: |
| Full live screen | 1,053,367 | 622,688-622,689 | 40.89% |
| Pixel replay with failed read | 259,601 | 187,973-187,974 | 27.59% |
| Six dirty rectangles | 72,905 | 45,047-45,048 | 38.21% |

Renderer-only and display-transfer timings do not show a resolved change.
The production fatal-error screen uses a separate, unchanged draw path.
Loaded flash grows from 11,000 to 11,160 bytes; the label arrays use 45
bytes (48 bytes of linked BSS including alignment). The pixel function's
local frame and saved-register area shrink from 24 + 32 to 0 + 24 bytes.

The new explicit `guard-split` pass moves an acyclic, read-only entry guard
from a private helper with one direct register-only call in a loop. It
repairs successor PHIs, recomputes live argument-only bounds in the
resolver, and leaves the expensive body out of line. It requires smaller
final code and no frame or save-area growth. It allows up to four
additional statically spilled values in the caller only, so it is **not
part of the default pipeline**. Apply it to saved optimized IR with
`badge-opt --passes=guard-split input.ll -o output.ll`.

Only Maze changed among 129 archived optimized modules. Its production
UF2 payload shrinks from 14,944 to 14,912 bytes. The four-level Cortex-M33
replay matches every digest, reaches 20,094,652 instead of 20,981,843
instructions (-4.23%), and makes 2,088,389 instead of 2,258,296 stack
accesses (-7.52%). The caller has four statically spilled values instead
of one; its local frame and save area remain 8 + 24 bytes.

On the badge, the same owned/new/new/owned order gave matching outputs and
clocks. The physics/input phase fell from 24,925-24,928 to 22,608-22,611
median timer ticks (-9.3%). The reached frame fell from 149,102-149,105 to
148,392-148,397 ticks (-0.47%). Both improvements have nonoverlapping
sample ranges across repetitions; the other phases did not resolve a
change.

### Faster host compilation without changing firmware

Profitability checks still require the same final code bytes, frames,
register saves, and spills. The owned compiler now does less work to get
those facts:

1. Check assembly record prefixes before parsing allocation and frame facts.
2. Reuse exact whole-module costs by IR text and scalar policy. The cache
   is local to one optimizer invocation and capped at 64 entries and 8 MiB.
3. On a whole-module miss, reuse emitted code for unchanged functions
   when their local label namespace matches. Callers that consult other
   functions are regenerated against the current module's callee facts. The
   function cache is capped at 4 MiB of input and assembly text. Final
   assembly and relaxation still run over the complete module.
4. Validate the assembly and ELF layout for size-only admission without
   materializing the ELF or its relocation payload.
5. Keep the parsed, verified module between passes that emit identical IR,
   and discard their scratch edits. Changed IR is still verified. The shell
   driver also avoids redundant path-helper subprocesses; the separate
   inspection stages and their output files remain available.

The pass order, backend policies, and supported IR contract are unchanged.

Paired macOS measurements on an Apple M4 Max used the existing compiler
executables (host tools were not rebuilt inside the timed intervals).
Five samples per optimizer and three independent builds per configuration
gave these medians:

| Host workload | Before | After |
| --- | ---: | ---: |
| Optimize Display | 56 ms | 26 ms |
| Optimize IMU Demo | 685 ms | 244 ms |
| Optimize Tilt Maze | 929 ms | 235 ms |
| Optimize Diagnostics | 2,598 ms | 429 ms |
| Clean Maze + IMU + Diagnostics (`-j5`) | 2.97 s | 0.78 s |
| Rebuild after a Maze-only change (`-j5`) | 1.12 s | 0.39 s |

The old and new compilers emitted byte-identical optimized IR for all
129 archived inputs and byte-identical UF2s for the three measured
firmwares. No badge timing or firmware change is implied.

The next host-speed round indexes helper references once per IR revision,
walks only the body of each reachable function for recursion checks, and
reuses the analysis after rejected trials. The paired fixed-input replay
remains byte-identical across all 129 modules and reduces aggregate
optimizer CLI time from 7.72 s to 7.40 s (-4.1%). The Maze median falls
from 222 ms to 210 ms and Diagnostics from 425 ms to 414 ms. These are
host compilation times, not firmware execution times.

During backend admission and ordinary codegen, frame and instruction
rewrites now append unchanged assembly spans directly. Frame base
substitution copies spans rather than formatting each character, and
operand peepholes check the opcode prefix before scanning operands.
Common load/store scans use a line-sized buffer instead of handing the
scanner the remaining function text. Policies, instruction selection,
and output limits are unchanged.

Against the preceding host-speed round, the 129-input replay emitted
identical optimized IR and cut aggregate optimizer CLI time from
7.68 s to 6.68 s (-13.0%). Standalone codegen medians fell from 13.31
to 9.76 ms for IMU, 20.54 to 14.87 ms for Maze, and 24.33 to 17.76 ms
for Diagnostics. The corresponding optimizer medians fell from
260 to 208 ms, 225 to 183 ms, and 449 to 377 ms.
All 129 saved optimized inputs also emitted byte-identical assembly.
Five paired `-j5` builds of Maze, IMU, and Diagnostics went from 785
to 700 ms median wall time, with nonoverlapping clean-build samples and
byte-identical UF2 payloads.

The next round replaces exhaustive Thumb modified-immediate search with
direct encoding. It also skips expression-cache construction for plain
decimal literals, fast-paths ordinary register spellings, looks up
mnemonics by binary search, and reuses static section directives across
relaxation passes. Relocations, conditional instruction checks, and
layout-dependent expressions still use the full assembler.

The native oracle checks all 4,096 encodings plus one-bit perturbations
against the exhaustive result. All 129 archived optimizer inputs and 129
archived assembler inputs produced byte-identical IR and ELF objects.
Aggregate optimizer CLI time fell from 6.83 s to 6.56 s (-4.1%).
Diagnostics optimization fell from 371 to 347 ms; standalone assembly
for that module fell from 7.29 to 6.36 ms. Aggregate assembler CLI time
was effectively flat across the archive.
The three fresh firmware images were again byte-identical. Clean
parallel build medians were 689 and 662 ms; the ranges overlap, so
this round does not establish an end-to-end build speedup.

The following round gates constant and scalar-width scans on the actual
mnemonic, parses each candidate immediate once, and inlines the
register-analysis work counter while retaining the same limit and error.
Admission now reuses functions with calls when their codegen did not
consult another function's linkage or finalized register summary.
`fastcc` linkage checks and register-summary dependencies still force
regeneration. A separate assembler operand-cache experiment did not
resolve a gain and was not retained.

All 129 archived optimizer, codegen, and assembler inputs still produce
byte-identical IR, assembly, and ELF objects. Aggregate optimizer CLI
time drops from 6.36 s to 6.00 s (-5.6%). Optimizer medians drop from
193 to 183 ms for IMU, 178 to 163 ms for Maze, and 343 to 323 ms for
Diagnostics. These are host timings, not runtime changes.
Fresh Maze, IMU, and Diagnostics UF2s were byte-identical. The clean
parallel build medians were 652 and 621 ms, with overlapping ranges;
the one-file rebuild also had no resolved change.

### Five microbenchmark follow-ups

These default `thumb-constants` changes were measured cumulatively in
the order below, using the exact linked objects and the established
five-seed Cortex-M33 instruction workload. The larger 256-input oracle
checks the same objects against native results without changing the
counted workload. No hardware elapsed times were measured.

| Candidate | Witness | Loaded flash | Reached instructions |
| --- | --- | ---: | ---: |
| Map high registers to available low registers in a bounded frame-free leaf; remove ABI copies only when their register is free | Arithmetic | 212 → 204 B | 475 → 465 |
| Rotate a proven positive, single-backedge counter loop without changing the live counter | Indexed memory | 240 → 240 B | 710 → 620 |
| Fuse branchless one-bit feedback with the shifted XOR when countdown flags overwrite the new flags and the temporary values are dead | CRC32 | 224 → 216 B | 1,485 → 1,325 |
| Use narrow immediate loads in the existing branch-free, flag-independent leaf rewrite | Fixed point | 200 → 196 B | 50 → 50 |
| Skip a dead input shift on the last pass through a bounded, forward-branch loop | Control | 220 → 220 B | 606 → 586 |

Helper calls remain at 220 B and 80 instructions. Arithmetic, control,
helper calls, and fixed point now tie LLVM for loaded flash. The remaining
gaps are CRC32 (8 B) and indexed memory (20 B). Against fresh HEAD control
builds, **18 of 33 verified UF2 payloads** shrink by **488 bytes** total, and
**52 of 129 objects** save **670 executable bytes**. No image or object
grows, and none of the 1,534 matched functions gains frame bytes,
saved-register bytes, or allocation spills.

### Expanded RP2350 microbenchmark corpus

Five additional standalone kernels exercise rotate/XOR, bitfield insert,
packed signed dot product, saturation and paired div/rem. They use the
same startup, LLVM crossover and 256-seed native-reference check as the
original six. This round compares against an eleven-program control,
with the added assembler encodings but without the new selectors.

| Kernel | Owned flash before → after | LLVM flash | Owned reached instructions before → after | LLVM instructions |
| --- | ---: | ---: | ---: | ---: |
| Rotate/XOR | 180 → 176 B | 176 B | 25 → 15 | 15 |
| Bitfield insert | 180 → 172 B | 172 B | 30 → 15 | 15 |
| Packed dot | 224 → 180 B | 184 B | 105 → 20 | 25 |
| Saturate | 204 → 196 B | 172 B | 51 → 38 | 15 |
| Div/rem | 184 → 184 B | 188 B | 35 → 30 | 35 |
| CRC32 | 216 → 212 B | 208 B | 1,325 → 1,320 | 1,635 |
| Indexed memory | 240 → 236 B | 220 B | 620 → 620 | 765 |

The default backend uses `ROR` (including shifted `EOR`), `BFI`, and
`SBFX` for exact single-use scalar patterns. `SMUAD` requires a proof that
the signed lane sum cannot set sticky Q; arbitrary packed inputs fall
back. `SSAT` and `SMLATT` are available for explicit assembly and
same-source LLVM crossover. The C clamp uses signed field extraction
and an ordered negative-immediate `CMN` when NZCV is equivalent; the
extra sticky-Q effect of an automatic `SSAT` is not silently introduced.
Shared div/rem keeps the quotient in its allocated register, and the
CRC and indexed-memory improvements remove stale saves or enable narrow
ones without increasing stack use.

All 38 application and microbenchmark UF2s verify. Of the 33 comparable
payloads, nine shrink, saving 112 loaded bytes, and none grows. The
129-module fixed-IR replay cuts 92 executable bytes in nine objects,
without object, frame, save or spill growth. Reached instructions are
from Cortex-M33 QEMU, not physical badge timings.

### Closing more microbenchmark size gaps

Four more changes are enabled in the default `thumb-constants` backend:

| Kernel | Owned flash before → after | LLVM flash | Owned reached instructions before → after | LLVM instructions |
| --- | ---: | ---: | ---: | ---: |
| Saturate | 196 → 184 B | 172 B | 38 → 35 | 15 |
| Indexed memory | 236 → 232 B | 220 B | 620 → 620 | 765 |
| CRC32 | 212 → 208 B | 208 B | 1,320 → 1,315 | 1,635 |
| Fixed point | 196 → 192 B | 196 B | 50 → 50 | 50 |

The small symmetric clamp uses sign extension to test signed-byte range
and an IT block to select an endpoint. It does not write sticky Q.
Cortex-M33 execution checks Q both set and clear, and the selector rejects
a return path with a volatile store. LLVM's `SSAT` can set Q on the same
inputs, so the compiler does not substitute it silently.

Leaf remapping revisits narrow adds and moves only when a flag overwrite
is proved. Branch-free leaves use an eligible narrow multiply. The CRC
return complements directly into `r0` on the closed feedback path.
Arithmetic, switch control, helper calls, rotate, bitfield insert, packed
dot, and div/rem produce the same measurements as before.

The 256-seed oracle passed for all 22 owned and LLVM kernel objects.
Eleven of 38 verified UF2 payloads shrink by 52 bytes total. Nine of 134
objects save 34 executable bytes. No image, object, matched frame,
save area or spill count grows. No physical badge timing was taken.

### Sixteen-kernel Cortex-M33 comparison

Five standalone kernels extend the corpus. They were compared with Clang
`-Oz` using identical preprocessor tokens, startup objects and a 256-seed
native reference. The original five inputs still define the counted QEMU
workload.

| Kernel | Owned flash before → after | LLVM flash | Owned instructions before → after | LLVM instructions |
| --- | ---: | ---: | ---: | ---: |
| Byte reversal | 184 → 168 B | 168 B | 35 → 10 | 10 |
| Rotated halfword add | 172 → 172 B | 172 B | 15 → 10 | 10 |
| Parity | 200 → 196 B | 196 B | 50 → 40 | 40 |
| Nibble lookup | 248 → 184 B | 188 B | 93 → 30 | 20 |
| Wide sum | 264 → 188 B | 192 B | 180 → 45 | 45 |

The default backend selects `REV` for the complete single-use permutation
and rotated `UXTAH` for a proven halfword extract feeding an add. A
straight-line shifted-XOR chain can exchange the ABI argument register
with a scratch register after proving the scratch's first use fully
overwrites it. Small masked switches returning nibble constants use a
packed constant instead of a branch tree. A bounded unsigned sum leaf
uses the Cortex-M33 carry flag instead of allocating 64-bit stack pairs.

The first broader register-swap candidate changed inlining and grew two
GPIO images. Restricting the rule to its shifted-XOR witness preserves
the gain without those regressions. All 43 UF2s verify with no growth
among 38 comparable images or 134 matched objects, and no measured
frame, save-area, or spill count grows. The nibble lookup is smaller
than LLVM but reaches more instructions; no physical timing is claimed.

### Composed RP2350 kernel comparison

The suite now includes five small multi-operation kernels with realistic
loop, helper, and data-access interactions. The same optimized IR also
goes through LLVM's backend to distinguish lowering gaps from source
optimization. All 21 kernels match the native reference on 256 seeds
under both compilers.

| Kernel | Owned flash before → after | LLVM flash | Owned instructions before → after | LLVM instructions |
| --- | ---: | ---: | ---: | ---: |
| RGB565 blend span | 508 → 492 B | 384 B | 1,422 → 1,325 | 851 |
| Sparse scan | 356 → 344 B | 292 B | 912 → 849 | 770 |
| Ring copy and checksum | 296 → 296 B | 256 B | 1,143 → 1,143 | 728 |
| Fixed-point filter | 380 → 348 B | 276 B | 2,384 → 1,944 | 895 |
| Byte parser | 308 → 304 B | 272 B | 487 → 487 | 383 |

The default backend folds a proven unsigned-distance diamond into a
predicate and reverse subtract, folds small left shifts into indexed
loads, honors explicit narrow zero-extended call results, and uses
`SMULL` when a mask proves that a nominally unsigned operand fits in
the signed range. It also removes a redundant spill reload while keeping
the store and subsequent indirect access, and proves flag liveness across
flag-neutral arithmetic to use narrower setup instructions. The
`low-bits` pass can remove a mask whose same-block users all apply
submasks. A broader reload rewrite initially altered IMU inlining and
increased stack use; the final rule is limited to a narrow
store/address-reload/value-reload/store sequence and excludes byte stores.
The byte-store candidate had saved four flash bytes and 39 reached
instructions in the ring kernel, but its repeated on-badge median rose
from 681 to 698 ticks. It was not promoted.

All 48 UF2s verify. Of 43 comparable images, 24 shrink, saving 1,448
loaded bytes. Of 139 matched objects, 72 shrink, saving 1,002
executable bytes. No compared image, object, matched frame, save area,
or spill count grows. The instruction counts above are QEMU counts.

The physical A/B/B/A replay used the exact owned kernel objects and an
unchanged timing harness, with 64 calls per protocol case, seven samples
per image, a native batch digest, and matching clock snapshots.

| Kernel | Baseline median ticks | Final median ticks | Change |
| --- | ---: | ---: | ---: |
| RGB565 blend | 4,804 | 4,628 | -3.7% |
| Sparse scan | 12,484 | 11,272–11,273 | -9.7% |
| Fixed-point filter | 1,595 | 1,318 | -17.4% |

The ring, parser, and unchanged arithmetic control had overlapping
sample ranges. These are on-badge comparisons against the previous
owned backend, not against LLVM. The badge's original 16 MiB flash
was restored and independently read back after each campaign.

### Low-word division and blend call setup

The default backend computes only the low result for a single-use,
positive-power-of-two `sdiv i64` immediately truncated to `i32`.
It still adds the sign bias and propagates the carry across the input
pair, so signed rounding is unchanged. The wide-arithmetic oracle
also checks extreme signed inputs with divisors 2, 4096, and 2^31.
Two narrow peepholes fold a right shift into in-place logical arithmetic
and avoid reloading an unchanged first argument immediately before
setting up the second argument of a direct call.

| Kernel | Owned flash before → after | LLVM flash | Owned reached instructions before → after | LLVM instructions |
| --- | ---: | ---: | ---: | ---: |
| Blend span | 492 → 488 B | 384 B | 1,325 → 1,303 | 851 |
| Fixed-point filter | 348 → 336 B | 276 B | 1,944 → 1,784 | 895 |

All 21 exact-object comparisons match native results for 256 inputs.
No other microbenchmark changes. IMU Demo shrinks from 11,132 to
11,076 loaded bytes and matches all nine rendering and refreshed-input
digests. Across 48 verified UF2s, three images save 72 loaded bytes;
five of 144 matched objects save 76 executable bytes. No image, object,
local frame, save area, or spill count grows.

On the badge, the exact-object A/B/B/A replay reduces median timer
ticks from 4,628 to 4,575 for blend span (-1.1%) and from 1,318 to
1,232 for filter update (-6.5%). Both improvements have nonoverlapping
sample ranges. All native batch digests and clock snapshots match, and
the other timed workloads show no resolved change. LLVM is the
flash-size and QEMU reference above, not the physical timing control.

### Indexed memory, call residency, and wide arithmetic

The Thumb backend now folds single-use byte addresses into aligned scalar
stores and scaled loads. The allocator keeps the base and index live at
the access, including across calls when a store follows a call. Direct calls
with simple scalar arguments and an explicit narrow `zeroext` result no
longer reserve scratch registers they do not use; an immediately following
indexed store can consume that result in `r0`. Shift folding looks across a
bounded scalar window for masks that need a register, without preempting
contiguous bitfield extraction. The broader shift rule was rejected after it
grew other kernels.

The backend can keep short-lived wide products and sums in a register pair
through a signed low-word reduction. A single-use signed widening multiply
followed by a wide add can use `SMLAL`. The filter still uses `SMULL`: the
product also feeds its sign check, so the selector does not recompute it.
The signed boundary oracle verifies both forms.

| Kernel | Owned flash before → after | LLVM flash | Owned QEMU instructions before → after | Badge median ticks before → after |
| --- | ---: | ---: | ---: | ---: |
| Blend span | 488 → 428 B | 384 B | 1,303 → 1,094 | 4,575 → 3,903 (-14.7%) |
| Ring checksum | 296 → 292 B | 256 B | 1,143 → 1,104 | 681 → 678 (-0.4%) |
| Fixed-point filter | 336 → 320 B | 276 B | 1,784 → 1,584 | 1,232 → 1,054 (-14.4%) |

Blend reached 460 B after indexed memory and 432 B after call transport;
the narrower shift fold brought it to 428 B. All 21 kernels match native
results for 256 deterministic inputs, and the nine production IMU
digests match. The exact-object A/B/B/A timing compared the previous and
new owned builds, not LLVM. The three timed controls showed no resolved
change.

All 48 UF2s verified: 25 shrank by 4,832 loaded bytes in total and none
grew. In 142 matched C objects, 102 shrank and none grew. Stack use is not
uniformly lower: some production functions trade up to 8 bytes of additional
local or saved-register stack for smaller code. Blend's frames are unchanged
and the filter's local frame drops from 56 to 48 B.

### Closing the LLVM gap with signed range proofs

The backend forwards a single-use indexed word load directly to the first
scalar call argument. A small additive index used only by an aligned store
after a call can be folded into the byte displacement instead of spilled.
Two-argument, straight-line leaves can retain their ABI inputs in low
registers and write their last arithmetic result directly to `r0`. A narrow
truncation is omitted only when its input is already known to have zero upper
bits. Read-only table bases are proposed at a dominating preheader and
encoded before admission; the blend proposal was rejected for spill growth.

The wide-division pass recognizes a bounded signed update from a read-only
little-endian table. It proves the guarded table index, input interval,
loop-carried state range, and maximum product before replacing the sign
diamond and wide division. For a power-of-two divisor `D` and product `p`,
the signed round-to-nearest result is the arithmetic shift of
`p + D/2 - (p < 0)`. Other gain ranges, biases, mutable or volatile tables,
and unproven loops retain the original arithmetic.

| Kernel | Previous owned flash | New owned flash | LLVM `-Oz` flash | QEMU instructions, owned / LLVM | Badge ticks, owned / LLVM |
| --- | ---: | ---: | ---: | ---: | ---: |
| RGB565 blend span | 428 B | **396 B** | 384 B | 947 / 851 | **3,215** / 3,319 |
| Fixed-point filter | 320 B | **256 B** | 276 B | **545** / 895 | **478** / 640 |

This time the badge A/B/B/A comparison used the exact LLVM objects as the
control, with identical harnesses, inputs, and clocks. Blend took 3.1% less
time and the filter 25.3% less time than LLVM. LLVM remained faster on the
other four timed kernels. All 21 kernels matched native results for 256
inputs. Sixteen of 48 verified firmware images shrank by 252 loaded bytes
total; none grew. No matched object's executable bytes or measured function
stack use grew, and all nine production IMU digests matched.

A separate source-level experiment streamed blended pixels into the
checksum instead of storing a span. It matched every effective length and
alpha combination and loaded **332 B** with the same boot objects. This is
evidence for a future loop-fusion pass, not a change to the benchmark or a
transformation the compiler performs automatically.

### Private-span fusion and larger kernels

The loop-scalar pass can substitute an in-order reduction for a temporary
span written by one loop and consumed by the next. It requires a proven
common trip count, a private stack array, and exactly one nonvolatile write
and read per element. A two-lane producer keeps its original order, including
an odd tail. When both lanes and the tail match after renaming their local
values and adjusting the index, the pass can also fold them back into one
loop. Final encoded size, frame, saves, and spills gate both changes.

The original blend source now loads in 332 B without a source rewrite.
Fusion with the original two-lane producer reached 388 B and 778 QEMU
instructions, down from 396 B and 947 instructions. Folding the repeated
lanes into one trades 17 reached instructions for another 56 saved bytes.
The larger tile pipeline also fuses, though LLVM still emits a smaller
helper and caller from the same IR. The stencil's proven narrow unsigned distances now use
subtract/conditional-negate instead of branch diamonds, saving 44 loaded
bytes and 1,336 reached instructions. Record dispatch uses the new rotated
`UXTAB` selector; the assembler checks its encoding against LLVM.

| Kernel | Loaded bytes, owned / LLVM `-Oz` | QEMU instructions, owned / LLVM | Badge median ticks, owned / LLVM |
| --- | ---: | ---: | ---: |
| RGB565 blend span | 332 / 384 | 795 / 851 | 2,879 / 3,303 |
| Tile pipeline | 392 / 356 | 3,951 / 3,087 | 46,269 / 28,502 |
| Stencil tile | 456 / 344 | 14,795 / 7,300 | 8,961 / 3,753–3,754 |
| Record dispatch | 504 / 428 | 3,881 / 2,435 | 2,524–2,525 / 1,680–1,681 |

The badge comparison alternates the exact LLVM and owned objects twice,
with shared harness, inputs, and clocks. Each timing uses the same batch
size for both compilers within a row; batch sizes differ across rows.

With the same optimized IR, LLVM needs no explicit frame accesses in the
stencil, versus 37 in owned code. It can reuse a row pointer and address
neighbors at constant offsets. Record dispatch still has 27 owned frame
accesses versus seven in LLVM's same-IR build. These are static instruction
sites, not executed stack-traffic counts. On larger applications the loaded
flash is 10,996 versus 5,784 B for IMU Demo, 14,732 versus 8,692 B for Tilt
Maze, and 26,680 versus 16,864 B for Diagnostics. Those application
comparisons are for size only.

All 24 kernel objects matched native results for 256 inputs each. All 51
UF2s verified. Among 48 existing images, only blend changed, saving 64
bytes. None of the matched objects or measured function stack frames grew.
All nine production IMU digests matched.

### Shared neighbor addresses and loop register residency

The loop-scalar pipeline finds groups of at least three nearby nonvolatile
byte loads whose addresses use one base and one index plus small constants.
It anchors the shared pointer at the lowest offset and leaves the loads in
their original order. Code generation models the shared pointer as live at
each load, prioritizes that pointer inside loops, and encodes the remaining
offsets directly. When the function's calls and operands do not require
`r6` as scratch, the allocator can use it and account for a folded load
extension as a single register value. The existing final-byte, local-frame,
save-area, and spill admission checks still apply.

| Stencil tile stage | Loaded bytes | QEMU reached instructions | Static frame accesses |
| --- | ---: | ---: | ---: |
| Previous owned default | 456 | 14,795 | 37 |
| Address sharing with spare register | 424 | 14,380 | 29 |
| Shared-base priority and accurate scratch masks | 404 | 12,255 | 20 |
| LLVM `-Oz` | 344 | 7,300 | 0 on the same owned IR |

Accounting for the folded extensions before allocation further reduces
the number of spilled values, from 16 to 12, with the same linked size and
reached instruction count. The stencil local frame drops from 104 to 88 B
without changing its 24-byte save area. The other 23 kernel measurements,
including record dispatch, are unchanged. Dispatch instead needs improved
handling of live ranges across calls and a multiway branch.

On the badge, the exact old and new owned objects took 8,961 and 7,194
median timer ticks respectively for the stencil (-19.7%). The five
unchanged control objects showed shifts of at most 0.6% in the linked
harness. Those are not changes to their generated code. All six Maze
replay workload timing ranges overlap, including reached frame
(144,282 versus 144,274 median ticks), and their correctness results match.

Of 51 verified firmware images, 14 shrank by 696 loaded bytes in total and
none grew. Diagnostics drops from 26,680 to 26,640 B; the normal Tilt Maze
image is unchanged. The separate Maze application replay image saves 24 B.
None of the 147 matched objects increased in executable size or per-function
local-plus-save stack. All 24 exact kernel objects match native outputs on
256 inputs each, and all nine production IMU digests match.

### Maze generation and RAM fills

The default `divrem-pairs` pass can bring signed i32 operations using the
same runtime divisor and dividend together. It accepts a duplicate integer
extension when the source and extension match and the duplicate has one use.
It preserves intervening effects and flagged operations. The existing Thumb
selector can then use one `SDIV` and one `MLS`. `small-aggregate` removes
nonvolatile inline fills of small fixed stack buffers only when same-block
scalar stores completely cover the buffer before an intervening operation
can observe it. Both IR changes use the existing final encoding, stack and
spill admission checks.

The Thumb backend also recognizes closed, fixed-trip halfword fill leaves
in the word-memory profile. For eligible long fills it uses a halfword
to reach word alignment if necessary, writes aligned word pairs, and
preserves the final halfword when needed. Volatile stores are not matched.
The Cortex-M33 test covers both halfword alignments, odd and even lengths,
and canaries before and after the output.

| Default changes applied in order | Maze fixture reached instructions |
| --- | ---: |
| Prior default | 31,850,143 |
| Signed div/rem sharing | 31,847,284 |
| Also remove overwritten fills | 31,741,462 |
| Also pair aligned RAM stores | 29,667,838 |

These are emulator instruction counts, not hardware timer readings. All 51
rebuilt UF2s passed payload verification. Compared to the prior default,
15 images shrink by 1,288 loaded bytes total; no image grows. Tilt Maze
loads 14,588 bytes instead of 14,732. Across 147 matched objects, 34 shrink
by 984 executable bytes total, none grow, and no measured local-frame plus
save area grows.

On the badge, a four-boot comparison of the first two changes moved Maze
generation from 25,256-25,257 to 22,887-22,889 median timer ticks per batch
(-9.4%). The separate diagnostics/graphics phase took approximately 0.12%
longer. A second four-boot comparison isolated word stores: drawing to RAM
dropped from 54,608 to 37,005-37,007 ticks (-32.2%), and reached frames from
144,131-144,137 to 139,731-139,736 ticks (-3.1%). The other workloads' sample
ranges overlapped in the second comparison. Clocks and correctness digests
matched in both comparisons, and each full-flash restoration was verified
byte for byte. See the [generation capture](../../build/rp2350-ten-timing/capture/capture.json)
and [RAM-store capture](../../build/rp2350-ten-fill-timing/capture/capture.json).

The other candidates did not change defaults. The single-call split changed
no Maze module and grew one Wi-Fi function's frame plus saves by eight bytes.
Broadening address rematerialization also left Maze unchanged and grew
several objects. Existing guard splitting remained opt-in: it saved 32
production object bytes and 4.3% of reached Maze instructions, but retains
its exceptional spill allowance. The existing pointer induction pass saved
four executable bytes in IMU Demo only. A four-way source-shape unroll saved
373,806 reached instructions but added 144 linked text bytes. Passing
hoisted pixel state through calls added 1,075,389 instructions and 36 linked
text bytes. A pure cross-unit inline pilot using the LLVM oracle added
17,545 instructions and 12 linked text bytes. The last three probes do not
add automatic optimizer passes.

### Cross-application size and code generation

Five changes are enabled in the owned defaults:

- `wide-division` cancels an exact positive factor in a signed wide
  scaled-square quotient when bounded SSA and branch facts prove both the
  original product fits signed 64 bits and the square fits signed 32 bits.
  Tilt Breakout's exact paddle replay agrees on all 32,769 inputs, reaching
  2,964,764 instead of 21,489,035 QEMU instructions including its caller.
  The function body alone reaches 1,827,434 instead of 2,357,289 instructions;
  the larger replay difference includes calls into the removed helper.
- The frontend marks C string literals `unnamed_addr`. The backend marks
  eligible complete, byte-aligned literals for exact pooling. The linker
  shares only byte-identical whole sections in the same script selection;
  named constants, writable data and ordinary input sections retain their
  addresses. `--no-literal-pool` preserves unmerged reference comparisons.
- `global-memory` also forwards a same-width ordinary store to a private
  global into a nearby load when no intervening operation can write or
  observe ordered memory. It leaves the original store in place, stops at
  unknown effects, and admits the result against final Thumb encoding and
  stack measurements.
- `small-aggregate` packs adjacent big-endian byte stores to a fixed local
  buffer into a native `REV16` and aligned halfword store when final
  code and stack allow it. The display region helper shrinks from 240 to
  176 bytes with unchanged source. This does not widen volatile transfers.
- The Thumb backend forwards registers and both outgoing stack words
  directly in a narrow class of private context-prepending wrappers.
  Diagnostics' two wrappers drop from 56 to 36 bytes each. Mixed
  Clang/owned ABI replay checks both stack words and the context pointer.

All 51 RP2350 UF2 payloads verified. Against the preceding defaults, 25
images shrink by 2,520 loaded bytes in total and none grows. Tilt Breakout
loads 9,076 instead of 9,480 bytes, Diagnostics 26,276 instead of 26,504,
IMU Demo 10,876 instead of 10,996, Boat 15,152 instead of 15,236, and
Tilt Maze 14,516 instead of 14,588. No matched object grew in executable
size or measured function frame-plus-saves across 147 objects. See the
[per-image and per-object comparison](../../build/rp2350-more-verified/comparison.json).

The paired single-badge IMU and Maze runs matched all output digests and
clock snapshots. Maze sample ranges overlap on all six workloads. IMU
full and dirty pixel generation took 0.010% and 0.087% more ticks
respectively; the other IMU sample ranges overlap. These are
previous-owned versus new-owned comparisons, not LLVM comparisons.
The paddle replay was not timed on silicon.

These experiments did not change defaults:

| Opportunity | Measured result |
| --- | --- |
| MMIO address reuse | Local CSE shrank I2C pin initialization by four code bytes, but grew 32 object texts and 37 modules' measured frame/save usage; the broader pass stays explicit. |
| Private aggregate scalarization | Replacing the IMU projection helper's vector parameter with three scalars in an experiment-only source copy saved 424 bytes in the replay firmware and 1,192 reached instructions. All nine IMU digests matched. This is not a generalized mixed-field pass. |
| Aligned RAM load pairs | The frame classifier's 64-bit-load prototype passed 240 classifications, but did not reach `LDRD` and grew from 260 to 304 code bytes and from 211,795,821 to 279,917,661 QEMU instructions including the caller. |
| Packed dispatch keys | An experiment-only source copy with nine combined USB request predicates shrank the Wi-Fi Link handler by 26 bytes and passed the USB protocol test. There is no generalized CFG transform. |
| Cold outlining | Extracting two repeated Diagnostics action sequences passed the app's host test and saved four object text bytes after charging the new helper. No machine outliner was added. |

## PO18: historical cached qualification and default promotion

PO18 promotes the independently measured **combined PO11-PO17 policy**.
The root firmware Makefile still defaults to LLVM; this changes the owned
compiler's shared API/CLI defaults and drivers that leave policies unspecified.

## Declared policies

| Policy | Previous owned default | Promoted owned default |
| --- | --- | --- |
| Frontend expression lowering | `ssa` | `ssa` |
| Optimizer | `mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory` | Previous pipeline plus `loop-scalar,cross-function` |
| Backend | `direct/global-compact/thumb-calls/word` | `direct/global-compact/thumb-widths/word` |

`thumb-widths` inherits effects, finalized frames, control and operand
selection. It does not enable the rejected compact flag-setting arithmetic,
local scheduling, arbitrary outlining or a new allocator. The loop/helper
passes retain their bounded PO14 final-byte/frame admission contracts.
Older policies remain explicitly selectable.

The standalone optimizer's default is now target-costed, not a promise to
optimize arbitrary LLVM IR independently of the badge backend. In particular,
forced `alwaysinline` and `musttail` contracts diagnose explicitly. The owned
C frontend already excludes those contracts. A directly related regression
test checks the diagnostic and output preservation, and verifies that the
previous explicit six-pass pipeline remains available for standalone IR.
Backend/work-budget failures are errors, not silent profitability fallbacks.

## Physical evidence

The [capture runner](../tools/current-promotion.mjs) declares the PO18
control and full candidate before capture. It runs two suites, two runtime
seeds, two repetitions and both physical role assignments per campaign:
**16 campaigns, 32 rounds and 64 raw reports**. No historical profile matrix
is repeated. All raw semantic results, USB records, clocks/XIP settings,
nonces and board assignments match; there are no transport retries or
resolved regressions.

The exact integer-rational gate includes sample spreads, same-board repeated
control drift and paired LLVM drift. A regression in any observation rejects
speed promotion; averages cannot conceal it. Every listed workload improves
outside that gate for both seeds, repetitions and physical roles.

| Reached application workload | Elapsed-time reduction versus previous owned default |
| --- | ---: |
| Maze generation | 27.85-27.87% |
| Physics and scripted input | 26.90-27.49% |
| RAM rendering | 13.54-13.79% |
| Display transfer | 44.26-44.27% |
| Reached maze frame | 39.59-41.07% |
| Production diagnostics/graphics | 15.29-15.45% |

The general timing suite also qualifies all twelve workloads. Reductions
range from 4.95% for a branch/PHI observation to 47.60% for a cross-file-call
observation; signed/unsigned division, ordinary memory, arithmetic, strings,
control flow, graphics and ABI paths all improve. These are **combined**
benefits against the old owned default, not additive per-pass percentages
or a claim that the owned compiler universally beats LLVM.

Measurements are warm-workload timer ticks, not controlled cold-XIP timings
or arbitrary interrupt/peripheral certification. Scripted input is not a new
physical IMU test. Existing mixed-ABI and resource evidence is reused.

## Delay and I/O qualification

The shared display's unserviced initialization previously relied on compiler-
sensitive loop counts. Both entry points now use the existing **150,000-tick
ROM-inherited running timer** after reset and sleep-out. Native sanitizer
tests verify the exact minimum, unsigned wraparound and callback cancellation.
This firmware mitigation is separate from the compiler-speed comparison:
both timed profiles use the same driver and exclude initialization delays.

The owner confirmed the complete screen-test pattern, then removed **all**
power and confirmed cold-boot drawing with both profiles on both badges,
including the physical-role swap. USB reports alone were not treated as
optical proof. These checks reach unserviced timer initialization and the
actual LCD command/pixel strobes.

The freestanding LCD retains three explicit NOPs between WR low/high and two
after high. At the supported maximum 150 MHz the three low-period NOPs alone
take at least 20 ns; the high period also includes the following instructions
and stores. No compiler transformation may remove these volatile assembly
effects or reorder volatile GPIO transactions. This is functional validation
at the captured clock settings, **not oscilloscope certification of every
panel waveform or permission to overclock**. I2C and radio wire deadlines
already use timers or hardware PIO; USB ownership keeps its explicit twelve
NOPs and memory clobber. Live USB initialization/reporting/teardown is reached
throughout the campaign.

The compact red-square startup does not establish its own timer contract.
Instead its separate reset/sleep-out primitive now retains six explicit
NOPs per iteration: even the shorter four-million-iteration wait supplies
24 million cycles, at least **160 ms at 150 MHz**, irrespective of loop
optimization. Both LLVM and owned production assembly retain that sequence;
the native/target oracle still verifies exact drawing and GPIO operations.
This changes demo safety, not the measured compiler workloads.

## Reuse, production sizes and resources

The optional [image cache](../tools/compiler-image-cache.mjs) keys source,
headers, assembly, scripts, linker scripts, build settings and tool binary
hashes. Runtime seeds and board roles do not affect image identity. Missing,
extra or corrupt outputs cause explicit errors; there is no stale-image
fallback. The experiment builds **six immutable images once**, reusing them
on **26 subsequent configuration requests**.

PO17's 37-module/560-function corpus is reused. All 37 new default backend
emissions equal the qualified explicit assembly, except that the changed
display source is compared with its separately built, cold-boot-tested
candidate. The default optimizer output for that source equals its explicit
eight-pass output. No production object is reassembled during finalization:
36 unchanged objects and the already built display object are reused.
Only eleven affected images are relinked; idle is reused unchanged.
The emission/equality/relink commands total approximately 0.41 seconds,
excluding the earlier image builds and physical flash preservation.

| Production image | PO17 candidate loaded bytes | Promoted, timer-safe loaded bytes |
| --- | ---: | ---: |
| button mapper | 9,220 | 9,176 |
| diagnostics | 41,580 | 41,536 |
| GPIO events | 7,672 | 7,628 |
| GPIO monitor | 6,064 | 6,020 |
| idle | 388 | 388 |
| IMU demo | 22,484 | 22,440 |
| lower mapper | 9,424 | 9,380 |
| peripheral probe | 11,292 | 11,248 |
| screen test | 4,212 | 4,168 |
| tilt breakout | 15,344 | 15,300 |
| tilt maze | 23,824 | 23,780 |
| USB/Wi-Fi/UDP | 326,236 | 326,192 |

Loaded ELF/raw bytes and physical linker budgets pass; data/BSS do not grow.
Final frame/save/outgoing reservations do not grow for surviving display
functions, and unchanged objects retain the existing allocation/call evidence.
This is not an invented whole-program stack bound: indirect calls,
interrupts and recursion retain their recorded uncertainties.

The separately mitigated compact red-square now measures **992 owned bytes
versus 568 LLVM bytes**, reusing identical startup/boot objects. Its delay
routine grows from 30 to 40 owned bytes and from 10 to 20 LLVM bytes;
alignment absorbs two of the ten added bytes in each loaded image.
No new red-square hardware-size record is claimed.

## Persistent verification and restoration

- [Final default/resource/provenance report](../build/po18-qualified-defaults/results.json).
- [Raw matched hardware capture](../build/po18-current-promotion/results.json).
- [Owner-confirmed cold-boot observations](../build/po18-optical/results.json).
- [Persistent verifier](../tools/current-promotion-evidence.mjs).

```sh
node badge-compiler/tools/current-promotion-evidence.mjs \
  --verify badge-compiler/build/po18-qualified-defaults/results.json
make -C badge-compiler test-current-promotion
```

Ten focused cache/policy/protocol/mock-runner checks passed before flashing.
Default wiring subsequently passed sanitizer optimizer/backend APIs and
40 focused Node selectors, including metrics/API/driver equality and the new
timing, standalone-contract and provenance checks. Historical exhaustive
encoding/ABI matrices were not rerun after host-only/documentation edits.

Both hardware sessions preserved complete 16 MiB originals. Every restoration
was independently read back and SHA256-verified, and both badges were left
in BOOTSEL. The original hashes are
`8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d`
and `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff`.
