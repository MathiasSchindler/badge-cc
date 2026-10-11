# Build, evidence and serial-bound hardware workflow

[Guide](README.md) · [Safety](safety.md) · [References](references.md)

**2026-10-08 work ordering:** the [guide](README.md#current-status) owns current
status, first cold acquisition and deferred next steps. This task is
documentation-only: do not run the commands below as part of the cleanup.
Commands and dated proof selections remain references, not blanket authority
to flash. Select current matching inputs and explicit scope before later work.

This is a command reference, not a record of new hardware tests performed
for these documents. **Source receive experiments require the explicit
experimental selection and current-code gates.** The owner has deferred RF
measurement to a separate track; `--experimental-rf-unverified` records that
working assumption, not a qualification result. The recovery
images below are reset-held diagnostics; separate guarded startup images
release PHY/radio state and have different prerequisites.

The opt-in [SOF/SIE link diagnostic](../../experimental/cyw43/r3/clock-meter/device/sof-link/README.md)
has its own MODE0-only target, strict host tests, serial-bound runner and
independent source5840/BOOTSEL recovery. Its [single physical
trial](../../experimental/cyw43/r3/clock-meter/device/sof-link/runs/sof-link-9a0_hx7b/outcome.json)
passed without reproducing zero-SOF failure; it cannot replace a fresh
ordinary MODE0 prerequisite or qualify receive/RF safety.

The latest recorded retained-state comparisons have completed batch-scoped
independent recovery. Documentation
review does not authorize flashing or resuming an interrupted experiment.
Check the [current handoff and remaining gates](handoff.md)
before selecting the next physical run.

## Host prerequisites and language policy

Use existing host C compiler, Clang `arm-none-eabi`, LLD, LLVM binary tools
and the project's USB tools. Research also uses the existing GNU Arm tools,
Ruby/shell and, for selected CPU comparisons, QEMU. Optional SDK demos have
additional SDK/CMake/Ninja/Arm GNU dependencies; those do not become
dependencies of the freestanding target.

New target logic is **freestanding C**, with no standard C library, SDK,
external target runtime, allocator or vendor service calls unless explicitly
declared for that separate image. New host logic should use **C or Python**;
Python evidence tools use the standard library. Legacy Ruby remains in
working build/capture/verification paths; it is not a target dependency or
a reason to introduce another host language.

Do not install/upgrade tools simply to read this guide. Consult the specific
module's Makefile/README and use existing smallest-scope tests. Model tests,
cross-compiles and undefined-symbol checks are distinct validations.

Prefer the C-native test for the changed component first, then the relevant
captured-code/host checks and target link. Keep build, artifact verification
and hardware preflight separate. A model pass is not permission to flash,
and a stale artifact rejection is not permission to weaken a pin.

## Reuse tests instead of adding experiment-specific runners

The active R3 workflow is split into four layers. None starts hardware:

| Layer | Entry point from the project root | Scope |
| --- | --- | --- |
| Fast C integration | `make -C experimental/cyw43/r3 ACTIVE=pll-probe active-check` | Native source/RP/USB/PMU models; no Python, Ruby, QEMU or firmware generation |
| Cortex-M3 execution | `make -C experimental/cyw43/r3/pmu-startup/pll-probe cpu-test` | One parameterized runner over the shared cold-stage harness |
| Host evidence/gates | `make -C experimental/cyw43/r3 ACTIVE=pll-probe active-host` | Shared recovery, trace/decoder, stack, workflow and request-profile checks |
| Artifact verification | `make -C experimental/cyw43/r3/pmu-startup/pll-probe/controller artifact-test` | Explicit source builds and image/layout checks; not physical qualification |

The existing `active-build`, `active-verify` and `active-preflight` still
have separate build/artifact/hardware-permission purposes. Artifact tests
which consume production builds require fresh matching artifacts; they
must not fall back to fixtures or relabel old proofs.

For a focused source change, pass `COLD_PROFILES=pmu_resource30` and
`SUPERVISOR_CASES=14` to the active check. The full default matrix remains
available. The controller's `cold-stage-test` runs selected native and M3
profiles together; older named diagnostic targets delegate to that same
runner rather than owning another implementation.

The separate vendor-reference C suite is:

```sh
make -C experimental/cyw43/r3/live-queue test-fast
make -C experimental/cyw43/r3/live-queue test-pre-rc PRE_RC_TEST_PROFILES="12 13 14"
```

It reuses the existing runtime and probe C harnesses with native-only
fixture identities, not generated firmware or captured radio state.
Compile-time variants remain where they exercise different guards.
Fixtures explicitly reject inclusion in firmware builds.

Existing Ruby image/evidence tools remain only where they already verify
pinned vendor inputs and historical records. `live-queue test-evidence` is
an explicit legacy verification step; the physical runner still requires
it before flashing. No new Ruby or Pylance infrastructure is introduced.

When extending tests:

1. Add cases to the existing owning C harness or host test class first.
2. Add a row to its profile matrix when compile-time guards differ; do not
   copy the harness, directory or runner for another experiment.
3. Keep native behavior, emulation, artifact checks and physical tests
   separate. Do not run the same suite through multiple import/re-export
   paths.
4. Introduce a new harness only for a genuinely different contract, and
   document why the existing owner cannot cover it.

## Controller receive research

<a id="cold-full-frame-profile"></a>
### Historical HOF6 cold full-frame profile

`FRESH_PSM=1` selects a separate cold family under `build/fresh/` and
`controller/build/fresh/`; the source archive is immutable
`archives/source-73fd0ced`. Defaults, prefix firmware and recovery remain
unchanged. These historical physical transfer results are **negative**:
[evidence](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/cold-fresh-transfer-2026-09-22.json).
Do not mistake a blocked offline UF2 for an executable qualified image.

The historical HOF6 qualified image was built from fresh recovered MODE0,
MODE2 and active-PHY qualification records. The commands immediately below
reconstruct that historical selection, **not** the newer HOF7 image. From
`experimental/cyw43/r3/pmu-startup/pll-probe`:

```sh
make FRESH_PSM=1 MODE=14 MACPHY_CLOCK_ENABLE=1 D11_PHYPLL_REQUEST=1 \
  ACTIVE_PHYPLL_QUALIFY=1 ACTIVE_PHYPLL_CALIBRATE=1 CALIBRATED_HANDOFF=1 \
  PSM_PRECOPY_PROBE=1 EXPERIMENTAL_RF_UNVERIFIED=1 \
  TIMING_PROOF="$PWD/runs/source-pll-2-njxjzhlg" \
  ACTIVE_PHYPLL_EVIDENCE="$PWD/runs/source-pll-14-sklt6vxo" all
```

The corresponding research runner is:

```sh
/opt/homebrew/bin/python3 run.py 9BBF88DD17D66E7D --mode 14 \
  --psm-fresh-probe --qualification runs/source-pll-0-vmeezwzc \
  --timing-proof runs/source-pll-2-njxjzhlg --macphy-clock-enable \
  --d11-phypll-request --active-phypll-qualify --active-phypll-calibrate \
  --calibrated-handoff --experimental-rf-unverified \
  --ap-session-nonce AP_SESSION_NONCE --execute
```

`AP_SESSION_NONCE` means a distinct, nonzero16-byte hex session nonce.
The AP must already be running under its own bounded lease, with this nonce.
This command supervises and recovers the **research badge**, not the AP:
retain separate AP stop/BOOTSEL cleanup. The two measured AP supervisions,
input pins and exact executed commands are preserved with
[100-TU](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-ymkp3ahj/)
and [20-TU](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-d9zl0zs_/)
evidence. Neither setup sends the AP nonce to receiver control, pauses AP
periodic traffic, or claims post-ready challenge freshness.

For the separate 2026-09-24 source`f5b89561` / RP`0ca84492` HOF7
checkpoint controls, use the exact fresh MODE0 `xcj54q5p` and MODE2
`q73l0m3h` proof directories and the [current command and bounds](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#supervised-no-observer-hardware-contrast).
`--no-observer-checkpoint withhold` never releases MAC; `release` may
add `--ap-beacon-stimulus`, which starts/stops the other badge on its
own serial and lease. The latter is **ordinary AP activity, not
READY-scheduled airtime**. The one-time 3-poll immediate A6 read
improved host timing visibility but still cannot meet the conditional
overlap gate within the observed ~3.5-ms MAC-to-no-STOP interval.
Every altered target input requires new strict physical qualification
before flash. No independent RF witness exists at present, and none
of these commands proves cold reception or Safe RX. The display-only
[GPIO monitor](../../src/tools/gpio_monitor.c) cannot substitute for
an RF witness and actively changes pad controls.

After source changes, build and physically requalify the fresh family as
required; old same-mode directories are not automatically current:

```sh
make FRESH_PSM=1 MODE=0 all
make FRESH_PSM=1 MODE=2 all
/opt/homebrew/bin/python3 run.py 9BBF88DD17D66E7D --mode 0 --fresh-family --execute
# Use the newly recovered MODE0 directory for MODE2.
# Use the newly recovered MODE2 timing for the active qualification build/run.
```

The strict active-qualification verifier, not generic runner exit status,
decides whether its deliberate diagnostic terminal is usable. Do not
rewrite failure flags. The cold family also preserves strict checks of
mode-specific include paths/toolchain flags and the fresh-profile common
identity marker.

The later HOF7 selection used recovered MODE0 `source-pll-0-oao2jmek`,
MODE2 `source-pll-2-8c6xgo56` and strictly verified active PHY
`source-pll-14-ppp2tqji`, producing source`b0671c00`/RP`458775fd`.
Two supervised v7 AP100TU trials rejected HOF7 at the RP handoff header
because the supervisor still expected HOF6. Their phase15 snapshots were
prefetched, not terminal calibration evidence; the reports contain
**no** v7 phase samples or cold frame. The RP check and native fixture now
expect HOF7. The corrected RP/source `7a318800`/`ef4981a3` has since
passed new recovered MODE0 `o7j90a3q`, MODE2 `1_3oyc44`, strict active-PHY
`3xjnddi0` and preflight. The [subsequent supervised trial](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-wvp9f997/outcome.json)
reached PSM phase10/error6 without a cold frame; AP shutdown and independent
recovery passed. See the
[current handoff](handoff.md). Do not substitute the HOF6 prerequisites,
increase timeouts or proceed to DMA publication on this evidence.
A second [20-TU trial](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-7ud5lvpn/outcome.json)
with the same receiver also timed out with no frame and fully recovered.

Software-only focused checks:

```sh
make -C controller psm-fresh-test cold-fresh-test
make psm-fresh-test
/opt/homebrew/bin/python3 -m unittest test_cold_fresh
(cd controller && /opt/homebrew/bin/python3 -m unittest test_fresh)
(cd controller && PLL_COLD_CPU_IMAGES=fresh-tests/psm_fresh_cpu.elf \
  /opt/homebrew/bin/python3 -m unittest test_builder.ColdCPUTests)
```

### Guarded fresh-frame experiment

The separate `FRESH_FRAME=1` image has passed three consecutive fresh-frame trials under
vendor-retained initialization. It is not the three-case prefix catalogue,
and the ordinary retained collector deliberately rejects its manifest.
Use [the fresh coordinator](../../experimental/cyw43/r3/rxe-observe/fresh_run.py)
with the isolated challenge AP; do not send the new radio challenge to the
receiver through USB or shared memory.

From `experimental/cyw43/r3/rxe-observe`, the frozen tested pair is:

```sh
/opt/homebrew/bin/python3 fresh_run.py f49aea13 24da697f \
  --experimental-rf-unverified --count 32
```

This command flashes and uses both serial-bound badges. It initializes the
vendor AP/station, validates five received frames, pauses periodic AP lab
traffic and waits50ms, then performs guarded takeover,
verifies the source-ready gate, generates the challenge, releases the
receiver using only the old nonce/token, and queues the AP burst.
Independent RP capture and strict C/host full-frame checks precede AP stop,
research retirement and immutable source5840 recovery. Both badges must
start in BOOTSEL. The AP accepts only one bounded burst per boot; another
trial needs a fresh boot, not a live-session reset. The fixed plain frame is
66 Ethernet bytes /88 MPDU bytes with FCS on channel1.

`--control no-burst` and `--control old-challenge` select negative controls;
they never produce positive fresh-frame claims. The current old-challenge
control has a complete stable frame and zero C errors, but rejects the
newly expected nonce. Earlier status-changing captures remain rejected;
the current PSM retries same-input snapshots within the original frame budget.
[Current proof and nine-attempt follow-up](../../experimental/cyw43/r3/rxe-observe/runs/reliable-one-shot-2026-09-22.json).
The AP is`aecf68c1`/100TU; pause and settling do not guarantee an empty
vendor queue or first-packet identity.

Software-only commands (no flashing):

```sh
make -C ../psm-rx/fresh test
FRESH_FRAME=1 /opt/homebrew/bin/python3 -m unittest discover -s device -p fresh_build_test.py
/opt/homebrew/bin/python3 -m unittest test_fresh_hardware test_fresh_protocol test_fresh_run
make -C fresh-stimulus PYTHON=/opt/homebrew/bin/python3 test verify
```

Rebuilding is an explicit separate step: `make -C device FRESH_FRAME=1 all
test verify`. Preserve the successful image/manifests before modifying
their inputs, and use generated identities afterward. Do not rebuild or
repin immutable recovery.

The earlier owner-requested documentation refresh changed four files in
the historical outside-file snapshot. The fresh runner accepts only those
four exact recorded revisions and verifies every other entry unchanged.
Its evidence says `historical_full_audit_passed=false`; the original audit
still reports those documentation differences. No source/asset mismatch
is excepted and the baseline was not repinned.

### Bounded resident research batches

The [RXE supervisor catalogue](../../experimental/cyw43/r3/rxe-observe/README.md#bounded-resident-image-batches)
now runs fixed `vendor-rxe`, `source-prefix` and `source-prefix-inhibited` cases from one retained-build
UF2. It reuses the existing runner, USB transport and frozen recovery helper;
it is not an arbitrary payload interpreter or a replacement for cold-source
qualification.

From `experimental/cyw43/r3/rxe-observe`, the verified current build can run:

```sh
/opt/homebrew/bin/python3 run.py e17ad806 1223f20d \
  --batch --retained-psm --experimental-rf-unverified \
  --cases source-prefix vendor-rxe --channel 1
```

For repeats, use `--cases source-prefix --repeat 2`. Read freshly generated
IDs from the manifest after any rebuild; old images without the catalogue
are rejected before radio activation. The default single-trial runner
remains unchanged when `--batch` is absent.
The inhibited case is an explicit tighten-only profile; the batch runner
supplies its additional collector acknowledgment. It is not selected by
default and never implies RF-silence qualification. Catalogue version2
uses two64-byte pages; its exact case descriptors are checked before radio
activation.

- Both exact serials must start in BOOTSEL. One AP image and one research
  image are flashed at the batch start.
- The AP stays running within its existing180-second lease. Active batching
  is capped at120 seconds, with a10-second AP margin and at most four trials.
- Every trial gets a fresh RP boot and radio initialization, a live catalogue
  check, a new experiment nonce and its own five-frame positive control.
- Between trials, verify the installed UF2 in BOOTSEL and reboot it with
  `picotool reboot -a`; do not clear the retired-session flag or resume
  overwritten vendor state.
- The first failure aborts the queue. Stop the AP before potentially slow
  recovery. Execute the independent source5840 baseline once at the batch
  boundary or failure; a trial alone does not acquire that recovery proof.
- Recovery overwrites the candidate. The next batch therefore needs a new
  candidate flash; there is no dual-slot loader or repinned recovery image.

Per-trial raw reports, catalogue records, proofs and timings are retained.
Batch timings use host monotonic wall time, not on-chip counter units.
The [measured comparison](../../experimental/cyw43/r3/rxe-observe/runs/resident-batch-validation-2026-09-22.json)
records two source-prefix trials in67.160s, versus115.581s for two separate
attempts (one had a preserved initial-flush failure). This small sample
shows overhead amortization, not a reliability guarantee or RF qualification.

### Existing individual Wi-Fi workflows

Run from the project root. `BOOTSEL_SERIAL`, `AP_SERIAL` and
`STATION_SERIAL` below mean the actual attached devices selected explicitly.

| Stage | Build / host tests | Existing physical runner |
| --- | --- | --- |
| Register only | `make wifi-bringup test-wifi-bringup` | `bash src/host/tools/usb_live_run.sh BOOTSEL_SERIAL wifi` |
| PIO register comparison | Runner builds its matching report image | Same runner with `wifi-pio` |
| Vendor firmware startup | `make usb-wifi-firmware test-wifi-firmware` | Same runner with `wifi-fw` |
| WPA2 association | `make usb-wifi-link test-wifi-link` | `bash src/host/tools/usb_wifi_pair_run.sh AP_SERIAL STATION_SERIAL` |
| Static-IP UDP | `make usb-wifi-udp test-wifi-udp` | Same pair runner with final argument `udp` |

The manual TFT bring-up image and USB reporting images are not interchangeable.
The runner builds/selects a matching image/collector and build fingerprint.
Firmware success, association/authorization, packet success and report
integrity are separate result fields.

See [the Wi-Fi guide](../wifi.md) and [USB guide](../usb-live.md) before
running hardware. Lab USB VID/PID `cafe:4026` is not a production allocation.
The earlier SRAM-across-BOOTSEL reporting experiment failed; use live
application USB, not assumptions about retained RAM.

## Research builds: never refresh the baseline to hide changes

**Documentation-refresh note,2026-09-22:** the owner-requested updates to
[the controller plan](../cyw43-driver.md), [USB-live guide](../usb-live.md),
[Wi-Fi asset guide](../wifi-assets.md) and [Wi-Fi roadmap](../wifi.md) differ
from the historical outside-file hash snapshot. The snapshot was not
repinned, and the current isolation check therefore reports those four
documentation mismatches. No code, vendor asset or recovery image changed.
Do not claim a passing current isolation audit or waive unrelated changes.
Resolve the documented drift explicitly before subsequent physical work
that requires a clean audit.

Research is isolated under `experimental/cyw43/`; dependencies point
outward to reused root source/vendor data. Working root builds do not
depend on experimental code. Generated assets/builds/runs belong under
their existing local directories, not system temporary directories.

The established outside-file audit covers 133 files including root
Makefile, working source and saved UDP image. Its reports describe that
historical baseline. Adding this explicitly authorized documentation does
not grant permission to overwrite assets, weaken guards or regenerate the
baseline. **Do not run `baseline` as a repair for a failed audit.**
Review any genuine mismatch and report it.

Useful existing host commands:

```sh
make -C experimental/cyw43 all test inventory audit
make -C experimental/cyw43/r2 standalone test-standalone audit
make -C experimental/cyw43/r3 MODE=0 all test audit
make -C experimental/cyw43/r3/rx-dma test audit
```

Each command addresses a different artifact/gate. The RX DMA tests do
**not** link that driver into the frozen recovery image or activate it in
the guarded startup probes. These are module references, not a sequence to
run wholesale. Do not rebuild or clean the frozen source5840
[recovery directory](../../experimental/cyw43/r3/rx-memory/build);
verify the [three pinned assets](firmware.md#frozen-recovery-assets) instead.
Exact-code/PHY/calibration tools and capture profile numbers are documented
in their topic READMEs; do not invent a new profile or arbitrary read range.

For guarded source startup, the
[PLL-probe workflow](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md)
owns the current flags, geometry, stack bounds and MODE0/MODE2 prerequisites.
Do not copy an old run directory into a freshness gate or flash a blocked
offline image. A new source-side diagnostic needs its actual implementation
handoff, tests and matching proofs, not an assumption that a requested flag
has been completed.

The earlier source handoff was saved and the worker stopped. At that
checkpoint, the pre-RC D11 read and separate terminal CC/D11 request
observations passed offline checks but had no physical qualification.
The blocked/offline artifacts have no production manifest. That handoff did
not include the PMU_MIN/resource30 request experiment. Implementation of
that separate request was subsequently started, then paused for train
arrival before a new handoff was received; no image was flashed during
that pause. Later source-only MIN30 and D11 requests were tested with fresh
proofs: MIN30 and FORCEHT readbacks pass without activation. The
MIN30 profile reuses the existing power/pre-RC boundary; cold v18 includes
71 comparisons plus chip/regulator readback, restores selectors and retains
the unchanged MIN30 observation window. RCCAL is not started.
Historical v14/v15 retain their pre-power semantics; v16 is the earlier
powered probe without the bank comparison. See the
[latest results](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md)
and do not flash an offline build product directly.

The separate explicit D11 PHYPLL opt-in now emits cold v19 and has two
verified identical-image source activation runs. It writes only D11 low
controls`0x240`, never PMU_MIN, and always terminates before RCCAL.
Its generic runner exit1 is deliberate; use the CRC-valid
`phypll_and_resources_observed` record and bound native endpoint to
interpret the activation result, not as full-startup or RX completion.
Fresh matching MODE0/MODE2 qualifications and independent recovery remain
mandatory. [Milestone and next qualification boundary](calibration.md#source-phypll-activation-observed---2026-09-21).

The later active workflow uses cold v21 and qualification extension v3.
`ACTIVE_PHYPLL_QUALIFY=1` measures the active rate, checks tables and powered
state, and exercises all8 minimum-delay arguments before its terminal stop.
`ACTIVE_PHYPLL_CALIBRATE=1` additionally requires
`ACTIVE_PHYPLL_EVIDENCE` naming the whole recovered qualification directory,
with matching source common-code identity and valid raw records/artifacts.
Older rate-only v1/v2 qualifications cannot authorize calibration.

Both qualification and continuation perform fresh active checks. The
calibration continuation has now passed twice on hardware through the
original-C composition, then verifies success-only request/IO cleanup.
It parks before inline core-reset waits, with independent recovery owning
reset. Interpret the bound `calibration_composition_observed` record,
not generic runner exit1, and do not infer persistent configured state or RX.
[Exact commands and results](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md).

The opt-in `CALIBRATED_HANDOFF=1` / `--calibrated-handoff` extends that
qualified calibration path, not the default startup. Cold v22, handoff v2
and wire v10 carry a real quiescent consumer after private-stack return,
eight distributed live checks over at least256 nominal SOF frames,
and an explicit stop/revoke before success-only cleanup. It retains the
whole recovered v3 active-evidence and current-code qualification gates.
The two verified runs use the identical source`2755f5b0` /RP`6d2058f2`
image and report `quiescent_lease_stopped`; neither enables RX.

During metering, the RP reads only the64-byte command/ACK slab; after stop
it waits for the final source commit before bulk capture. The earlier
SOF-step and incoherent-prefix failures remain unqualified. Interpret
strict raw handoff/calibration bindings separately from deliberate runner
exit1 / `measurement_verified=false`. Capture-time
`recovery_verified=false` is not the subsequent independent recovery
verdict: both source5840 and final BOOTSEL outcomes must still pass.
[Measured scope and retained failures](calibration.md#bounded-calibrated-state-handoff-verified---2026-09-21).

Extend the shared tests rather than creating a new handoff runner:
`make calibrated-handoff-test active-qualification-test` covers the RP;
`make -C controller cold-native-test cpu-test COLD_PROFILES=calibrated_handoff`
covers the source native/M3 profile, and `make host-test` checks the evidence,
authorization, decoder and stack contracts. These commands run from the
[PLL-probe directory](../../experimental/cyw43/r3/pmu-startup/pll-probe)
with the project's selected `PYTHON`. They do not flash hardware.

The [pre-RC vendor-reference profiles](../../experimental/cyw43/r3/live-queue/README.md)
are separate from both the no-UP observer and source startup. Their deliberate
post-RC park causes an expected UP timeout; only the validated committed
record establishes the measured result. A timeout alone is never success.
Valid collection can contain `calibration_completed=false`.

Host-only PSM checks, with the project's selected Python interpreter:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s experimental/cyw43/r3/psm-rx -p 'test_*.py' -v

PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s experimental/cyw43/r3/psm-rx/source -p 'test_*.py' -v
```

The first command checks pinned decoding/evidence; the second checks the
original source encoder/verifier and synthetic ISA/control model. Neither
is a complete silicon emulator, an active receiver test or RF qualification.
Different `CLOCK`/R3 capture modes may share build directories. Do not
build conflicting modes concurrently; record the resulting manifest rather
than assuming yesterday's build ID.

## Guarded handoff selection sequence

This reproduces the **existing bounded quiescent** experiment only; it does
not authorize PSM/MAC reception or DMA. Use the exact commands in the owning
[PLL-probe guide](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md),
with the existing [build](../../experimental/cyw43/r3/pmu-startup/pll-probe/build.py),
[runner](../../experimental/cyw43/r3/pmu-startup/pll-probe/run.py) and
[strict evidence checks](../../experimental/cyw43/r3/pmu-startup/pll-probe/evidence.py).
Do not manually flash an offline or historical UF2.

| Order | Current-code selection | Required predecessor |
| --- | --- | --- |
| 1 | Build/run MODE0 qualification on the research serial | Offline checks, immutable recovery assets, current device identity |
| 2 | Build/run MODE2 source timing | Recovered MODE0 directory via `--qualification` |
| 3 | MODE14 with `MACPHY_CLOCK_ENABLE=1 D11_PHYPLL_REQUEST=1 ACTIVE_PHYPLL_QUALIFY=1`, calibration/handoff off | Recovered MODE0 and MODE2; `TIMING_PROOF` at build and `--timing-proof` at run |
| 4 | Same MODE14 selectors plus `ACTIVE_PHYPLL_CALIBRATE=1 CALIBRATED_HANDOFF=1` | Whole recovered trial-bearing v3 directory in build-time `ACTIVE_PHYPLL_EVIDENCE`, matching source common-code identity and MODE0/MODE2 |

For step4, the runner opt-ins are `--macphy-clock-enable`,
`--d11-phypll-request`, `--active-phypll-qualify`,
`--active-phypll-calibrate` and `--calibrated-handoff`.
`--execute` is the explicit physical action. `SERIAL` means
`9BBF88DD17D66E7D`; `MODE0`, `MODE2` and `ACTIVE_RUN` in the module templates
mean the **absolute recovered directories printed by the corresponding new
runs**, not mode numbers or isolated JSON copies.
Active evidence is bound at build time; there is no separate runtime
active-evidence flag. Old v1/v2 rate-only records cannot authorize calibration.

The historical quiescent-handoff checkpoint's older active qualifier remained eligible
after RP-only observer fixes because the source common code and qualified
measurement implementation were unchanged and the replay gate accepted it.
That is not a general freshness bypass. On a new code revision, requalify
whatever the existing gates require; never copy identities or weaken checks.

Generic exit1 is expected for steps3/4's deliberate terminal stop, **not
automatic success**. Require strict raw qualification/calibration/handoff
records, the bound native marker and later independent source5840/BOOTSEL
recovery. For handoff, require consumer acceptance, physical ownership,
RP SOF binding, at least256 nominal frames/eight distributed checks,
within-run selected-register equality, stop sequence/ACK9, no command error,
revocation and cleanup. Do not substitute a CRC alone or a summary flag.
[Checkpoint interpretation](handoff.md#interpretation-traps).

## Physical research selection and cleanup

Known device roles:

| Serial | Usual purpose |
| --- | --- |
| **`9BBF88DD17D66E7D`** | Reviewed source/controller research device |
| **`08DA26F86E57F5EA`** | Stock reference / controlled AP; only flash when the specific paired experiment requires it |

1. Verify current BOOTSEL identity by serial and matching chip/build
   expectations, **not USB port or enumeration order**.
2. Keep a known-good image available. Flash sequentially: concurrent
   discovery/flashing has previously caused ambiguity.
3. Use the experiment's own runner, immutable manifest and fresh challenge.
   A build number copied from this guide is historical, not an override.
4. Preserve the runner's timeouts, RP watchdog, radio-off cleanup and
   explicit serial-bound BOOTSEL verification.
5. Preserve failures and raw records. Report integrity alone does not mean
   `inventory_ok`, execution proof, networking or safe RX.
6. Independently restore and execute the pinned source5840 baseline after
   the experiment, then verify final BOOTSEL. Merely reaching BOOTSEL is not
   proof that the known-good baseline was restored. Explicit resident
   batches perform this once at the boundary or first failure; do not
   assign that batch-level proof to each individual trial.

Historical reset-held runners exist, but are **not** the source5840
recovery procedure for the current guarded probe:

```sh
bash experimental/cyw43/r2/tools/standalone.sh 9BBF88DD17D66E7D
```

For current probe attempts, keep the runner's pinned recovery path through
[host_support.py](../../experimental/cyw43/r3/pmu-startup/host_support.py).
Do not invoke the memory experiment's build/run script to regenerate the
frozen recovery assets.

The two-badge [RX lab](../../experimental/cyw43/r3/rx-lab/README.md) is a
separate, intentionally transmitting vendor-reference experiment:

```sh
make -C experimental/cyw43/r3/rx-lab all test audit
ruby experimental/cyw43/r3/rx-lab/run.rb 1
```

It qualifies vendor reception, shuts down/reboots, then starts a fresh AP
session alongside the unchanged reset-held source image. Channel is limited
to 1/6/11. It has no source-RX enable option and must not leave an AP running.
`lab_ready_receiver_gated` is not `rx_pass`.

If software recovery fails, the documented physical recovery is disconnect
USB, hold **Boot**, reconnect, then install the intended RP2350 UF2.
BOOTSEL recovery replaces the RP application; returning to BOOTSEL does
not automatically restore a previous application. No permanent CYW
configuration, OTP or security setting should be changed.

## Evidence to retain

Record selected serials/roles, exact source/stock/ROM identities, controller
and RP build IDs, fresh nonce, raw summary/detail/mailbox records, CRCs,
timing units/reference, result-specific proof and final stop/recovery.
Do not collect credentials or unrelated ambient packet contents.

Repeat runs should have independent challenges and resets. Retain failed
discovery/capture layouts separately from final accepted layouts. An evidence
verifier checks consistency and pinned inputs, not authenticity of arbitrary
edited JSON. Do not relabel vendor-retained state, a software fixture or
AP submission counter as a source-only over-the-air frame.

### Logs are disposable; observations are not

The existing [R3 evidence ledger](../../experimental/cyw43/r3/references.json)
is the canonical permanent index of observations, identities and run hashes;
topic READMEs explain their meaning. Add a concise result there rather than
retaining a new verbose command log for every retry.

- Keep primary raw hardware reports, manifests, pinned inputs, recovery
  proofs and any file a verifier actually consumes.
- Preserve diagnostic failure evidence when it changes the interpretation;
  summarize the cause/outcome and link the underlying record.
- Do not routinely save successful compiler command lines or repeated
  checksum listings. If a temporary build log is useful, reuse one bounded
  file rather than creating a succession of named logs.
- Delete only inspected, unreferenced generated outputs. Never clean an
  entire `runs/` tree, remove immutable recovery assets, or repin hashes to
  conceal a mismatch.

The 2026-09-20 consolidation removed 304 redundant logs plus 33 superseded
native executables and their debug bundles (12,078,972 bytes). All 12,443
archived run files remained byte-identical; 13 explicitly referenced logs
were retained. No hardware experiment was run for this cleanup.
