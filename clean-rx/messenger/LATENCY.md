# Messenger timing: measurement and reduction

Hardware measurements on 2026-10-20, using the two USB-connected Universe
badges. Both endpoints use the same unchanged source-built HT20 controller.
These are **application measurements**, not RF edge timestamps or an HT MAC
qualification.

## What is measured

Every non-heartbeat event retains sender-local timestamps for:

1. First observed GPIO change (physical events only).
2. Debounced enqueue, or scheduled USB-test enqueue.
3. First and last TX selection.
4. First and last controller command completion.
5. Validated application ACK reception, or retry exhaustion.

The receiver independently records first validated delivery, first ACK
selection and command completion, and completion of the display frame
containing that event. Duplicates do not replace the original receive time.
The ACK still binds session, sequence, button state, origin and test token.

**RTT** is sender first TX selection to validated application ACK receipt,
using one local clock. It includes staging/readback, controller work,
both radio paths, receiver processing, the deliberate ACK guard and
Function-2 delivery. Controller command completion is not an RF timestamp.
**Queue time** is enqueue to first TX selection.
**Receive-to-display** is first receiver delivery to completion of its
corresponding LCD transfer. It is not panel scanout or visible-pixel timing.
No subtraction of unsynchronized clocks and no RTT/2 one-way estimate is used.

USB test events are armed 200,000 local ticks in the future. The host then
does no collection for 500 ms. Thus ordinary first-attempt exchanges happen
without trace collection on their critical path; subsequent retries can
overlap polling. Raw timestamp records, retry counts, outcome flags and
the complete received packet are retained. Hardware benchmark events bypass
physical debounce; the 20-ms physical debounce is separately native-tested,
not claimed as an electrically measured button-edge latency.

## Important timebase correction

The original application named TIMER0 reads `now_us`, but inherited the ROM's
clock setup. USB SOF calibration measured:

| Profile | A ticks / 32 USB ms | B ticks / 32 USB ms |
| --- | ---: | ---: |
| Original-policy baseline | 15,086 | 15,253 |
| Final first run | 32,001 | 32,001 |

The baseline's raw tick was approximately **2.1 microseconds**, not one.
The baseline summary converts each badge's intervals using its own SOF
calibration. Earlier nominal "20 ms" ACK waits were actually about 42 ms;
nominal retry and display delays were likewise longer.

The improved app reuses the existing, unmodified
[crystal-reference helper](../../src/arch/rp2350/nametag_clock.c) to establish
XOSC/12 TIMER0 ticks while preserving CPU clock frequency. Each timing
campaign checks both clocks against 32 USB SOF intervals. The final repeat
calibration and exact raw values are retained in its log. This is a
timebase sanity check, not precision oscillator metrology.

## Measured comparison

Baseline: 40 scheduled events, 20 in each direction, after startup recovery.
Final: two matched 40-event campaigns, 20 per direction in each run.
Quantiles use nearest rank; at these sample counts p99 is usually the
maximum and is not a stable population-tail estimate.
First-attempt RTT populations are 15/19 events for baseline A-to-B/B-to-A,
20/16 for the first final run, and 19/20 for its repeat; retried events
remain in the all-confirmed metrics and totals.

| Metric | Baseline A to B / B to A | Final first run A to B / B to A |
| --- | --- | --- |
| First-attempt RTT median | 47.51 / 47.98 ms | **7.02 / 7.17 ms** |
| First-attempt RTT p95 | 124.60 / 149.63 ms | **17.97 / 19.39 ms** |
| First-attempt RTT maximum | 124.60 / 149.63 ms | 18.49 / 19.39 ms |
| Receiver ACK-selection median | 42.01 / 42.46 ms | **1.04 / 1.07 ms** |
| Receive-to-display median | 328.82 / 349.90 ms | **65.53 / 65.90 ms** |
| Receive-to-display maximum | 584.82 / 591.16 ms | 242.23 / 114.46 ms |
| All-confirmed RTT maximum, including retries | 2139.02 / 1241.14 ms | **18.49 / 225.55 ms** |
| Messages needing retries | 5 / 1 | 0 / 4 |
| Confirmed events | 20/20 / 20/20 | 20/20 / 20/20 |

Final repeat first-attempt RTT medians were **6.93 / 6.86 ms**;
display medians **65.00 / 65.89 ms**. One of the 40 repeat events retried;
all 40 were confirmed, with maximum RTT 226.47 ms.
Neither final campaign added failed messages, queue drops or radio boots.
Both badges stayed at one radio boot. A's earlier startup heartbeat failure
remained counted; it was not reset to improve the report.

The corrected timer makes physical debounce a true 20 ms. For physical
events, local first-observed-change-to-ACK can be calculated from its
retained timestamps, but the RF campaigns above use injected events.
Queueing, loss and display work mean a button-to-visible-response bound
cannot be inferred from these median RTTs.

## Changes retained

- Establish a true microsecond timer; do not silently reinterpret raw ticks.
- Reduce the application ACK guard to **1 ms**. This is not an IEEE ACK/SIFS
  timing claim.
- Reduce retransmission spacing to **207..218.6 ms**, retaining six attempts,
  role/sequence jitter, strict correlation and explicit exhaustion.
- Transfer the screen in **four-column** chunks, returning to radio polling
  between chunks. Defer a new paint while an exchange/ACK is pending.
  Once started, a display frame finishes cooperatively.
- Redraw dynamic canvas regions rather than clearing the full canvas for
  every update. Retain the last physical press even after release.

The first strip implementation incorrectly treated the shared column-major
canvas as row-major and visibly garbled both screens. Its radio-only
40/40 result is **not** a valid display result and is excluded above.
The fix follows the existing LCD stream order and has a native pixel-for-pixel
test covering all 76,800 pixels in both partial and full-frame modes.
The user confirmed readable screens and bidirectional button updates after
that correction. The final scheduler uses that verified transfer layout.

## Failures retained, not a reliability claim

Evidence folders under `build/clean-rx/messenger-timing/`:

- `baseline/`: rejected non-microsecond clock probes, interrupted campaigns,
  and the complete `latency-patient-40.jsonl` baseline.
  A host timeout had to be extended to tolerate full radio reloads. The
  diagnostic `collector-patient` uses the installed baseline protocol/build
  ID with the longer host wait; the badge image and timing policy are unchanged.
- `optimized/`: the garbled-screen intermediate build and its 40/40 radio
  results. These do not qualify display latency or a usable demo.
- `display-fixed/`: a **25/40** campaign during asymmetric receive trouble;
  seven A-to-B events were not delivered and eight B-to-A events were
  delivered without confirmed ACKs. Radio restarts and additional failed
  messages remain visible. Do not merge its successes into the final cohort
  and omit its failures.
- `final/`: two complete **40/40** timing campaigns, raw packets and
  independently checked summaries, plus a passing 24-exchange
  sequential/simultaneous regression test.

The measured improvement is repeatable in those final runs, not proof that
the shorter retry horizon or underlying RX is universally reliable.
Startup failures, asymmetric reception and recovery remain open D2 work.
No counts, native FCS outcomes or collector thresholds were waived.

## Reproduce

```sh
make -C clean-rx/messenger BUILD=../../build/clean-rx/NEW_TIMING pair test
# Flash role1 and role2 as described in README.md, then:
build/clean-rx/NEW_TIMING/collector latency-test SERIAL_A SERIAL_B 40 > timing.jsonl
node clean-rx/messenger/latency.js timing.jsonl
```

For a separate original-policy comparison, use `BASELINE=1` and a **different
build folder**. This preserves inherited timer behavior, a 20,000-tick ACK
guard, long retry intervals and full-frame blocking updates. Always calibrate
the resulting ticks; do not label them microseconds directly. Default
`BASELINE=0` selects the improved application. Keep archived RF build folders
immutable.

The USB trace is 512 bytes. At byte 256 it holds twelve little-endian
32-bit sender words: token, sequence, observed-change time, enqueue time,
first/last TX selection, first/last command completion, terminal time,
attempt count, outcome (1=ACK, 2=exhaustion), flags (bit 0=first command
completed). At byte 304 are nine receiver words: token, source session,
sequence, first receive, first ACK selection/completion, display completion,
flags (receive/ACK-selected/ACK-completed/display-completed), duplicate count.
Clock calibration is at bytes 352/356. Intervals are unsigned, wrap-safe local
differences; the analysis rejects reversed/ambiguous intervals and verifies
packet metadata, token/session/sequence and software FCS independently.
