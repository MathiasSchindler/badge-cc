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
