# Tilt Maze

## Build and install

Run `make tilt-maze` (or `make`) and install
`build/universe26/uf2/tilt_maze.uf2` using the normal BOOTSEL procedure.
This separate Cortex-M33 image uses project-owned freestanding C with no
libc, Pico SDK, operating system, or external runtime dependencies.

Run `make test-tilt-maze` for host tests with address/undefined-behavior
sanitizers. The host test executable uses the host C library; the firmware
does not.

## Controls

- Hold the badge with its screen facing up, roughly horizontal.
- Press Enter and remain still for 32 samples (about 320 ms).
  The average X/Y acceleration becomes the neutral holding pose.
- Tilt right/left to accelerate right/left. Lower the near edge toward
  yourself to accelerate down the screen; lower the far edge to accelerate up.
- Return to neutral to coast and slow down. Tilt against motion to brake.
- Reach the green `G` marker from the blue start to clear the current level.
- After clearing a level, Enter generates the next, harder maze, retaining
  calibration. Level advancement waits for a button press.
- During play, Enter restarts and recalibrates the same maze.
- Home resets progression to a newly generated level 1 and the start screen.
- Touching a black pit (purple rim) or an orange fire bar returns the marble
  to the current level's start. There are unlimited retries; the timer
  continues, and neutral calibration is retained.

Calibration rejects excessive inclination, a screen-down pose, or X/Y
movement exceeding 1,000 raw units across the calibration window. A visible
prompt explains how to retry. Buttons use the existing 20 ms debounce helper.
The per-level timer starts after calibration, freezes on completion, resets
on advancement or manual restart, and saturates at 9,999 seconds. The retry
counter also saturates at 9,999 without limiting retries. There are no lives,
radio features or chasing monsters.

## Levels and layout

During play, a 14-pixel header shows level, seconds and retries. There is no
bottom status bar or permanent title. Instructions appear in a dismissible
start/calibration overlay; the cleared-level header says `CLEAR ENTER NEXT`.

Mazes are generated with an iterative randomized depth-first traversal, not
external libraries or allocation. A timer-seeded xorshift generator chooses
passages, and subsequent levels/whole-game resets advance its state. The
mazes have three-pixel walls rather than filled wall tiles. Every cell is
connected, with one path between any two cells.

| Level | Grid | Cell size | Difficulty |
| --- | --- | --- | --- |
| 1 | 8 by 6 | 38 by 36 pixels | Wide corridors, goal at most 10 passages away, no hazards |
| 2 | 12 by 8 | 25 by 27 pixels | Longer route, pits in side branches |
| 3 | 15 by 10 | 20 by 21 pixels | Narrower corridors, pits and fire |
| 4 onward | 19 by 13 | 16 by 16 pixels | Tight corridors, progressively longer target routes and denser hazards |

The board occupies 300-304 by 208-216 pixels, centered below the compact
header. Corridor narrowing and hazard density are bounded to keep later
levels physically navigable; level numbering saturates at 9,999.

The generator reserves the complete start-to-goal path and places hazards
only in other cells. Thus every level has a safe, unobstructed route; hazards
punish wrong turns without making an unlucky random level impossible.
Pits trigger on circle contact; fire bars trigger on circle/rectangle
contact. These checks run on every collision substep to prevent tunneling.

## Implementation

The game is in [tilt_maze.c](../src/tools/tilt_maze.c). It reuses the shared
integer types, internal open-drain I2C and LSM6DS3 drivers, display driver,
graphics canvas/font, hardware timer/button helpers, and diagnostic integer
square-root, decimal-formatting and debounce functions.

The marble retains its four-pixel radius and the original rolling physics.
Position and velocity use Q8 fixed point. Accelerometer input has a
short low-pass filter and neutral dead zone; tilt drives acceleration rather
than position. Friction gradually removes momentum.

Physics runs in fixed 10 ms steps, with four collision substeps and a
1.5-pixel-per-tick per-axis speed limit. Circle-versus-rectangle collision
resolves penetration and removes inward normal velocity while preserving
tangential motion. Collision corners use the existing integer square root,
not floating-point math. At most five physics ticks are caught up after a
stall; excess backlog is discarded. The elapsed timer follows timer ticks,
not the number of executed physics steps.

Rendering targets 50 Hz. A static 153,600-byte RGB565 canvas stores the board
and labels, while the marble is composed during region transfer. Ordinary
movement transfers only the union of its old/new bounds, restoring the
underlying board. Large jumps, including hazard respawns, transfer the two
small bounds separately. The header updates only when time, phase or retries
change. Full-screen transfers occur on startup, level changes, instruction
overlay changes or fatal sensor errors. Display/sensor traffic can reduce
actual cadence.

Full-screen transfers finish with `badger_display_end()` to send display-on
and enable the backlight after initialization, including on startup errors.
Partial transfers use `badger_display_end_update()` and leave that state alone.

The gyroscope is not used: acceleration alone supplies the board-plane
gravity components. Rapid translation/shaking affects the input; subtracting
the neutral pose is a gameplay calibration, not full orientation estimation.

## Validation boundary

Host tests cover calibration/rejection, axis signs, inertia/braking, wall
sliding, corner/inside-wall resolution, speed limits, collision stress,
maze connectivity/reciprocal passages/tree structure across 1,200 generated
level/seed combinations, bounded geometry and difficulty, hazard-free
solution paths, multiple complete physics-driven routes, level progression,
pit/fire contact and respawn semantics, timer states, dirty-region bounds,
background restoration, display/backlight enablement on the initial full
frame and error rendering.

The sensor wiring and axis mapping were physically established by earlier
IMU work. The original game's display activation and rolling physics were
confirmed on badges. The procedural levels, new layout and hazards still
require physical testing, as do timer accuracy and display cadence.
Sensor initialization/read failures stop the game with an explicit screen
error; they never substitute neutral input.

## Compiler comparison

On 2026-10-09 the maze compiled through the complete owned C01-C12 pipeline,
without LLVM target compilation/linking/extraction tools, as well as the
normal LLVM build. Compiler defaults were unchanged:
`mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-division`;
materialized frame addresses, stack register residency, legacy scalar
selection and byte memory selection. LLVM was Homebrew Clang 22.1.8 at `-Oz`.

### Output size

The uninstrumented game, built from the same source with no gameplay changes:

| Metric, bytes | LLVM | Owned |
| --- | ---: | ---: |
| Text/readonly data | 8,684 | 119,576 |
| Initialized RAM data | 16 | 160,456 |
| Zero-initialized RAM (`.bss`) | 160,440 | 0 |
| Raw flash payload (`.bin`) | 8,700 | 280,032 |
| UF2 file including block overhead | 17,408 | 560,128 |

The flash payload is 32.19 times larger; text/readonly data is 13.77 times
larger. This is not a 32-fold RAM increase: both reserve about 160 KiB of
static application RAM.

The principal size finding is a frontend/backend mismatch:
[ir_global()](../badge-compiler/src/badge_ir.c) emits zero-filled byte-array
constants even for uninitialized writable globals.
[cg_global()](../badge-compiler/src/codegen.c) selects `.bss` only for a
literal `zeroinitializer`. Consequently the 153,600-byte canvas and other
zero-initialized globals are stored in flash and copied into RAM at boot.
Proper zero-initializer recognition is the first size improvement to pursue.

This finding describes the original baseline. The general compiler correction
and new measurements are recorded in the [follow-up](#compiler-improvement-follow-up).

Code size is separately excessive. Owned `generate_maze` alone occupies
20,494 bytes. The checked stack/materialized/byte backend defaults produce
heavy stack traffic, repeated address formation and byte-wise ordinary-RAM
access. Existing direct-addressing, register-residency and word-selection
options are candidates for controlled follow-up experiments; they were not
silently enabled, and the compiler was not patched for this comparison.

### Correctness and measured performance

`make -C badge-compiler test-tilt-maze` builds an oracle fixture including the
real maze source, graphics and shared integer helpers. Both owned and LLVM
ARM executables run under QEMU and pass deterministic fingerprint
`0x7789aa8c`, calibration, full/partial display transfer, hazard respawn,
timer retention, safe-route and level-advancement checks. LLVM/LLD is the
independent test oracle; production owned UF2s use the owned linker.
The fixture uses the MPS2 mapped 1 MiB RAM region because the ordinary
small-fixture SRAM mapping cannot hold this game's framebuffer. The initial
small-mapping fault was a test-harness limitation, not a compiler defect.

An optional `TILT_MAZE_COMPARE` build runs a fixed level-4 maze at startup:
one warm-up plus three identical samples of 64 deterministic physics ticks
and one complete board draw into RAM. Input/filter assignments and the
physics loop are timed together; generation, fingerprints, panel transfers
and IMU access are outside the measured intervals. Drawing includes the
HUD, with a one-glyph `L`/`O` compiler-label difference.
Each sample's maze/state and 18,080 below-HUD framebuffer pixels are hashed.
A mismatch against the host/LLVM reference stops with an explicit error.

The HUD shows `L P<number>` or `O P<number>`, where P is mean timer ticks per
physics step. The initial instruction overlay shows mean `DRAW TICKS`.
After the benchmark both badges use the same maze seed for gameplay.
These flags are off for normal firmware. Use separate build directories
when changing flags; Make does not track command-line flag changes.

The following instrumented images were flash-verified and started:

| Compiler | Badge chip/USB serial | Raw flash bytes | UF2 bytes |
| --- | --- | ---: | ---: |
| LLVM (`L`) | `9BBF88DD17D66E7D` | 9,272 | 18,944 |
| Owned (`O`) | `08DA26F86E57F5EA` | 285,748 | 571,904 |

The user confirmed both games work, with the owned version noticeably
sluggish. Reported mean RAM drawing times were **7,718 ticks LLVM versus
118,417 ticks owned: 15.34 times slower**. Physics counters were not supplied.
This is one two-device pairing, not a role-swapped or clock-calibrated
campaign. The result measures this drawing workload, not overall FPS,
display-transfer latency or all possible game states.

Machine-readable sizes, hashes, tool settings and observed timings are
saved in [the comparison results](../build/maze-compiler-comparison.json).

### Reproduce

From the project root:

These commands build with the current compiler defaults into fresh directories,
not into the archived baseline image directories. The original baseline used
materialized/stack/legacy/byte policies and the earlier zero-storage lowering.

```sh
make -C badge-compiler all test-tilt-maze
make tilt-maze test-tilt-maze

# Uninstrumented owned build.
make BUILD_DIR=build/maze-current-owned \
  RP2350_CC='bash badge-compiler/scripts/owned-cc.sh' \
  RP2350_OBJCOPY="$PWD/badge-compiler/build/elf2bin" \
  RP2350_NM="$PWD/badge-compiler/build/badge-elf-info" \
  RP2350_SIZE="$PWD/badge-compiler/build/badge-elf-info" tilt-maze

# Instrumented LLVM and owned images, in separate directories.
make BUILD_DIR=build/maze-current-llvm-compare \
  TILT_MAZE_FLAGS=-DTILT_MAZE_COMPARE tilt-maze
make BUILD_DIR=build/maze-current-owned-compare \
  TILT_MAZE_FLAGS='-DTILT_MAZE_COMPARE -DTILT_MAZE_COMPILER_OWNED' \
  RP2350_CC='bash badge-compiler/scripts/owned-cc.sh' \
  RP2350_OBJCOPY="$PWD/badge-compiler/build/elf2bin" \
  RP2350_NM="$PWD/badge-compiler/build/badge-elf-info" \
  RP2350_SIZE="$PWD/badge-compiler/build/badge-elf-info" tilt-maze
```

Flash only explicitly selected badges in BOOTSEL, using `picotool load -v -x`
with `--ser` and the appropriate UF2. Firmware remains freestanding and
dependency-free. Owned display initialization can take longer because its
existing cycle-delay loops are compiled into less efficient instructions.

### Compiler improvement follow-up

This records the first follow-up. The subsequent
[Z01-Z05 optimization plan and results](../badge-compiler/docs/maze-optimization.md)
qualify the current direct/global-compact/thumb-layout/word defaults.
Z04 adds qualified typed boolean expression cleanup to the C06 default,
without game-source changes. Its user-confirmed owned readings were
35,198 RAM drawing ticks and 4,268 physics ticks per step, versus Z03's
49,159 / 4,930; LLVM remains 7,718 and 377 respectively.
Its uninstrumented owned payload was 42,024 bytes, versus 48,672.
See [Z04 qualification](../build/maze-opt-z04-results.json) and the
[restored instrumented owned UF2](../build/maze-opt-z04-cleanup-maze/universe26/uf2/tilt_maze.uf2).
Z05 adds general CFG/epilogue branch layout without changing PHI semantics,
instruction widths, literal pools or firmware dependencies. Both role-swapped
hardware repeats improve all twelve workloads, and the user confirmed normal
gameplay and approved promotion. Latest owned readings are **31,342 drawing
ticks / 4,240 physics ticks**, reductions of **10.95% / 0.66%** from Z04.
LLVM remains **7,718 / 377**. The uninstrumented owned payload is **41,480
bytes**, and the instrumented payload is **43,736 bytes**. See
[Z05 qualification](../build/maze-opt-z05-results.json) and the
[default instrumented owned UF2](../build/maze-opt-z05-default-maze/universe26/uf2/tilt_maze.uf2).
Select `thumb-registers` explicitly for the preceding Z04 control.
Earlier measurements below remain unchanged for comparison.

The first follow-up canonicalized relocation-free all-zero static storage and
promoted the broadly qualified direct/global/thumb/word backend policies.
There are no new maze-specific lowering rules and no game-source changes in
this follow-up. Constants, explicit sections, real relocations and volatile
hardware access preserve their existing semantics.

| Uninstrumented maze metric | Original owned | Improved owned | LLVM reference |
| --- | ---: | ---: | ---: |
| Text/readonly bytes | 119,576 | 55,712 | 8,684 |
| Initialized data bytes | 160,456 | 16 | 16 |
| Zero-initialized RAM bytes | 0 | 160,440 | 160,440 |
| Raw flash payload bytes | 280,032 | 55,728 | 8,700 |
| UF2 bytes | 560,128 | 111,616 | 17,408 |

Owned flash payload is **80.10% smaller**, and code/readonly bytes are
**53.41% smaller**. Static RAM is not reduced; zero-filled objects no longer
consume flash-backed initialization data. The resulting payload remains
6.41 times LLVM's size.

Broad frontend/backend tests, independent LLVM/QEMU checks and all eleven
default applications plus the Wi-Fi host image pass. Two complete
role-swapped physical campaigns match LLVM for six modules and six kernels.
Against a fresh same-source conservative backend control, every measured
workload takes fewer owned ticks (reductions of 8.79-45.83%). These timings
are not the maze's drawing benchmark. See the
[compiler qualification](../badge-compiler/docs/implementation.md#current-default-promotion-and-zero-storage)
for paired comparisons, remaining LLVM gaps and failed collector attempts.

Both games were restored with flash verification and application reboot:

| Compiler | Badge serial | Instrumented raw flash bytes | UF2 bytes |
| --- | --- | ---: | ---: |
| Original LLVM control (`L`) | `9BBF88DD17D66E7D` | 9,272 | 18,944 |
| Improved owned (`O`) | `08DA26F86E57F5EA` | 58,472 | 117,248 |

The improved owned image is
[the separate comparison UF2](../build/compiler-improved-maze/universe26/uf2/tilt_maze.uf2).
The original baseline images and
[baseline results](../build/maze-compiler-comparison.json) are retained unchanged.
New measurements are in
[the improvement results](../build/maze-compiler-improvements.json).

The user confirmed both restored games display and play correctly and reported:

| Maze startup measurement | LLVM (`L`) | Improved owned (`O`) |
| --- | ---: | ---: |
| Mean RAM drawing ticks | 7,719 | 56,202 |
| Mean physics ticks per step (`P`) | 377 | 5,403 |

Owned drawing uses **52.54% fewer ticks** than its original 118,417-tick
measurement: **2.11 times faster**. The remaining gap is **7.28 times LLVM**
for drawing and **14.33 times LLVM** for physics. Original owned physics
readings were not supplied, so a physics improvement against that baseline
cannot be quantified.

These are raw timer ticks for the same optional startup benchmark, from
one fixed device pairing rather than a role-swapped maze campaign. Drawing
excludes panel transfers and physics excludes IMU access; neither ratio
represents overall gameplay FPS.
