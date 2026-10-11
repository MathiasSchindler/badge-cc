# CYW43439 controller-firmware research plan

Original plan: **2026-09-15**. Status bridge consolidated **2026-10-08**.
The proposal below is historical, not the current test procedure or task list.
Start with the [current source-radio handoff](cyw43/handoff.md),
[technical guide](cyw43/README.md) and [workflow](cyw43/workflow.md).

Despite this document's filename, its subject is code running **inside the
CYW43439**, not the RP2350-side host driver. The working
[host-driver and UDP experiments](wifi.md) remain a separate development
track. Controller-firmware research is not a prerequisite for continuing
four-player networking or improving the existing UDP implementation.

## Current research status - 2026-10-08 documentation checkpoint

The [technical guide](cyw43/README.md#current-status) owns the consolidated
status, based on physical records through 2026-09-24. Source execution,
implemented RX-focused initialization/calibration and bounded quiescent
handoff have hardware evidence. Original PSM code has captured three
consecutive fresh complete frames after **vendor-retained** initialization;
the cold-source path still has no accepted frame.

The [active milestone](cyw43/README.md#active-milestone-cold-source-acquisition-of-a-fresh-frame)
is cold-source acquisition of one fresh complete frame through the existing
bounded scratch-evidence path. General receiver lifecycle, live DMA,
persistent RX-compatible TX inhibition and broader qualification are
[next steps](cyw43/README.md#next-steps-after-first-acquisition), not additions
to first-frame scope. Documentation work stops before implementation/testing.

Keep candidate inhibits, independent bounds, identity gates and recovery.
The [RF-unverified experimental policy](cyw43/safety.md#experimental-receive-policy---owner-direction-2026-09-21)
permits separately selected bounded experiments, not qualified RX-only claims.
Pinned ROM configuration/table data remain dependencies of the original
freestanding C controller code; no clean-room or independent-data claim is made.
Use the [handoff](cyw43/handoff.md) and [workflow](cyw43/workflow.md), not this
historical plan, to select an image or command.

## Current baseline

The RP2350 code already owns radio reset, gSPI/PIO transfers, firmware
download/readback, control commands, Ethernet framing, ARP and static-IP
UDP. The two tested 2026 badges have completed WPA2-authorized
associations and bidirectional UDP tests with USB reporting and recovery.
Three baseline sessions completed 600/600 echo transactions using vendor
CYW43439 firmware; this is the working host/networking track, not the
source-radio result.
See the [recorded data-plane results](wifi.md#static-ip-udp-echo-and-measurements).

The build does **not** compile the downloaded radio firmware from source.
It extracts `wifi_firmware.bin` from a pinned vendor data header, embeds
the bytes in the RP2350 image, and downloads them into CYW43439 RAM. Board
NVRAM and CLM regulatory data are supplied separately. The
[asset packaging and provenance contract](wifi-assets.md) remains
authoritative for those files and their hashes.

```text
RP2350: project-owned host/application code
                  |
          project-owned gSPI
                  |
CYW43439: downloaded RAM firmware (vendor code today)
          internal ROM, MAC microcode and radio hardware
```

For this working vendor-firmware network, the no-SDK/no-libc claim applies
to the RP2350 executable. It does not establish the ownership or runtime
dependencies of the opaque code executing inside the radio.

## Define the intended independence

The following levels and milestones preserve the original planning model.
They do not override the current source-execution boundary or workflow above.

| Level | Deliverable | Dependencies that remain |
| --- | --- | --- |
| A: current baseline | Project-owned RP2350 host driver and networking | Vendor radio firmware, ROM, microcode and configuration |
| B: custom extension | Our controller-side diagnostic or feature code inside a patched vendor image | Most vendor firmware and its runtime, ROM, microcode and configuration |
| C: source-built RAM image | An independently built downloadable program, initially only a diagnostic | ROM boot behavior; any deliberately reused ROM services or microcode must be listed |
| D: independent Wi-Fi implementation | Source-owned radio control and the necessary Wi-Fi behavior | Immutable hardware/ROM boundary and retained calibration/data dependencies must still be stated |

A source-built replacement for the downloaded file is not automatically
a replacement for all proprietary code in the chip. Conversely, a tiny
program that runs without the vendor RAM image is a useful milestone even
if it cannot receive or transmit Wi-Fi.

Freestanding C and avoiding libc are requirements for our new code, but
they are not the principal difficulty. The hard parts are the boot
contract, undocumented interfaces, real-time MAC behavior, PHY setup and
calibration.

## Relevant prior art

The following upstream material was inspected on 2026-09-15:

- [Nexmon](https://github.com/seemoo-lab/nexmon) explicitly lists the
  CYW43439/Pico W target with firmware `7.95.49 (2271bb6 CY)`, including
  monitor/radiotap support.
- Its [matching patch directory](https://github.com/seemoo-lab/nexmon/tree/master/patches/bcm43439a0/7_95_49_2271bb6)
  provides a concrete starting point for firmware-side extensions.
- The [target definitions](https://github.com/seemoo-lab/nexmon/blob/master/firmwares/bcm43439a0/7_95_49_2271bb6/definitions.mk)
  identify an ARMv7-M target, 512 KiB RAM, a ROM mapping and microcode-related
  locations. Treat these as external reference assumptions until verified
  against our exact image and hardware.
- [pico-nexmon](https://github.com/seemoo-lab/pico-nexmon) includes
  controller-console and custom-IOCTL examples for the CYW43439.
  Its host examples use the Pico SDK; that does not require us to replace
  our project-owned RP2350 transport with the SDK.

The version string matches what our badges report, but **version-string
equality is not binary or ROM compatibility proof**. Pin and compare exact
inputs before using any hook, symbol address or layout assumption.
Nexmon support is also not a claim that these features have been tested
on our badges.

Adopting a patch framework would be a new, explicit research-build
dependency. Studying it does not justify silently importing it into the
existing no-dependency host build or claiming a clean-room implementation.
Review modification/distribution rights and retain applicable notices
before shipping patched images or ROM-derived material.

## Isolation and recovery rules

- Keep existing default, `wifi-bringup`, `usb-wifi-firmware`,
  `usb-wifi-link` and `usb-wifi-udp` targets working and unchanged in purpose.
- Use separate opt-in research targets, output directories, image
  manifests, build fingerprints and test/report identifiers. This original
  plan did not implement build commands; use the current workflow for the
  subsequently implemented isolated research targets.
- Do not overwrite the approved generated `wifi_firmware.bin`, modify the
  pinned vendor headers, or relax their production-style validation to
  admit arbitrary experimental images. Use a distinct image name and
  loader path for each experiment.
- Reuse proven transport and USB helpers where appropriate, without making
  the working networking targets depend on research-only code.
- Select a badge explicitly by serial. Start with one research device;
  retain a known-good image and use the other badge as a reference peer
  only when a milestone needs one.
- Begin with RAM-only changes. Do not write OTP or other permanent
  configuration. Do not assume every operation on a radio is reversible
  merely because its usual firmware download goes to RAM.
- Preserve RP2350 watchdog/BOOTSEL recovery, bounded radio operations and
  unconditional radio-off cleanup. The controller must not be able to
  leave the RP2350 waiting indefinitely for an undocumented response.
- Keep RF transmission out of early proof-of-execution tests. Preserve
  board calibration and regulatory data; transmission requires an
  understood, compliant initialization path.

## Milestones

### R0: Establish the execution contract

Produce a compatibility and ownership inventory:

1. Record badge serial, observed radio identity, firmware version and exact
   image hashes; determine the relevant controller/ROM revision.
2. Pin the reference research revision and compare its input/layout
   assumptions with ours.
3. Establish the processor architecture, reset entry behavior and how ROM
   starts or interacts with the downloaded RAM image.
4. Map the required RAM regions, stack, initialized data, interrupt/fault
   handling, reserved memory and any vendor heap/reclamation behavior.
5. Identify which portions are processor code, MAC microcode and data, and
   which ROM services could legitimately be reused.
6. Document any image validation or execution restrictions. If a supported
   execution route is unavailable, record that blocker rather than
   assuming the RP2350's boot/security configuration applies to the radio.

**Gate:** a reviewed, evidence-backed execution/layout contract and a
bounded plan for running a small program. Do not apply unverified
addresses or assume the RP2350 startup/linker script works on the radio.

### R1: Harmless extension of the vendor firmware

Add a narrowly scoped controller-side diagnostic while retaining the
vendor Wi-Fi implementation:

- A dedicated command accepts a fresh challenge.
- Our controller-side code computes a response and updates a diagnostic
  counter or build marker.
- The RP2350 transports the returned bytes to the host; it must not
  fabricate the expected result locally.
- Reserve extension code/data so vendor heap allocation or reclamation
  cannot overwrite it.
- Preserve normal behavior for unrelated commands and retain explicit
  error handling.

**Gate:** repeated fresh responses attributable to our controller-side
code, a negative control without the extension, and successful
reset/reload/recovery. Re-run association and UDP checks to detect
regressions; compare loss and latency rather than assuming a patch is free.

**Ownership claim:** custom code in a modified vendor image, not an
independent firmware replacement. This was the original recommended first
implementation milestone, not the current next task.

### R2: Standalone RAM-only proof of execution

Attempt a minimal source-built program without downloading the vendor
Wi-Fi RAM image:

- Provide startup, linker layout, stack and data initialization for the
  verified controller execution contract.
- Use a small fixed mailbox accessible through the proven backplane path.
- Clear the mailbox, start the program, then observe a changing counter
  or challenge-dependent response. Reading a constant already present in
  the downloaded bytes is not proof of execution.
- Keep the program bounded and diagnostic-only, with no intentional RF
  transmission and no permanent writes.
- Give this loader its own success criteria. Do not wait for vendor
  version, CLM or SDPCM responses that the tiny program does not implement.

**Gate:** a reproducibly built image executes, responds and recovers
without the downloaded vendor Wi-Fi image. State precisely which ROM boot
behavior or services it still uses.

**Not a gate:** association, packet reception or a working Wi-Fi interface.
This milestone intentionally proves none of those.

### R3: Minimal receive-only radio path

Only after execution and recovery are reliable:

- Establish the required clocks, interrupts, packet-buffer ownership,
  MAC/PHY initialization and receive path.
- Reuse understood ROM services or microcode only as declared dependencies.
- Keep transmit disabled while investigating reception.
- Use the other badge as a known, controlled source, beginning with
  identifiable frames such as beacons on one supported channel.
- Validate actual frame format, lengths and any supplied metadata.
  Raw 802.11 capture must not be misinterpreted by the existing
  Ethernet/IPv4 parser.
- Report received, rejected, truncated and dropped frames, including
  capture-buffer limits.

**Gate:** reproducible reception of identifiable test frames, with explicit
dependency and loss/coverage limits. A command acknowledgement alone is
not evidence of a receive path.

A patched-vendor monitor experiment may be pursued after R1 without
claiming R2/R3 independence. Neither route promises all-channel capture,
all frame types or raw RF/IQ samples.

### R4: Minimal interoperable Wi-Fi behavior

Expand only the functionality justified by the application:

1. Establish compliant transmit initialization and calibration.
2. Verify basic frame exchange and required real-time MAC behavior,
   including acknowledgements, retry handling and buffer ownership.
3. Add the required station/AP association behavior.
4. Add WPA2 authorization before considering the replacement suitable for
   the working badge network.
5. Reconnect the existing project-owned Ethernet/ARP/IPv4/UDP layers and
   repeat the paired echo and measurement tests.

**Gate:** real interoperable traffic and authorization, malformed-input
tests, bounded fault recovery and repeatable measurements. Keep
compatibility with an independent known-good peer in the validation plan.

A stripped-down application or removing TCP does not remove PHY setup,
MAC timing, calibration or association requirements. The real-time MAC
engine/microcode is not simply another piece of RP2350 application C.

### R5: Assess deeper independence and harden

If eliminating remaining proprietary downloadable code is still the goal:

- Enumerate every retained ROM call, binary microcode component and board
  data dependency; do not equate a renamed or repackaged component with
  source ownership.
- Establish whether vendor documentation/source access or an appropriate
  development agreement is available.
- Decide which remaining components can realistically be replaced and
  whether this silicon is suitable for that goal.
- Validate sustained traffic, link loss, reset/rejoin, multiple peers,
  firmware compatibility and regulatory behavior before considering
  adoption by ordinary badge applications.

**Gate:** an explicit go/no-go decision for each remaining dependency,
followed by evidence for any replacement. A fully independent Wi-Fi
implementation is not assumed to be a guaranteed continuation of R2.

## Evidence required for every milestone

Record source/build inputs and hashes, ownership/dependencies, selected
hardware, loader stage, request/response identity, timeouts, failures and
recovery results. Distinguish host-model tests from controller execution,
and controller execution from actual RF/network behavior.

Use existing USB reporting concepts: fresh challenges, versioned bounded
records and explicit errors. Keep research identifiers separate from
known-working tests so that a mailbox proof cannot appear as a successful
Wi-Fi version query or UDP benchmark. Preserve failed trials when they
explain a design decision.

Compile our new controller-side code freestanding and inspect its
dependency closure and undefined symbols. Any external build tool,
vendor runtime call or retained binary must be declared separately.
No-libc compilation of an extension does not prove that the surrounding
vendor image uses no library/runtime code.

## Effort and decision points

- R0 is a bounded feasibility investigation, not a commitment to a full
  replacement. Resolve the critical compatibility/boot questions first.
- R1 is plausibly weeks-scale if the existing supported target matches;
  toolchain adaptation and firmware compatibility can dominate.
- R2 cannot be estimated confidently before R0 establishes an entry and
  startup contract. A tiny program is small; discovering how to start it
  may not be.
- R3-R5 are radio-firmware research, potentially months to years for a
  genuinely independent implementation. Documentation access can change
  both feasibility and effort substantially.

These are planning ranges, not delivery promises. If the execution,
documentation, licensing or radio-initialization boundary is unsuitable,
stop or retain a clearly labeled extension approach. Continue the
working UDP/four-player track independently rather than making it wait
for controller-firmware independence.
