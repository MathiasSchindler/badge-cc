# Badge hardware and electrical evidence

[Guide](README.md) · [Host driver](host-network.md) · [Clocks](calibration.md)

For badge serials, roles and the last verified BOOTSEL/recovery state, see
the [current testing handoff](handoff.md#2-hardware-and-last-known-state).
Recheck attachment/identity before testing; a schematic or saved role is
not live device discovery.

## Sources and final wiring

The primary board source is the five-sheet
[Pimoroni schematic](../../pimoroni/github_badge_2026_schematic.pdf),
generated 2026-09-03, with the accompanying
[RP2350 pinout](../../pimoroni/Screenshot%202026-09-03%20at%2012.24.33.png).
[The confirmed specification](../spec.md#confirmed-wireless-path) records
schematic versus physical confirmation. Sheet 2 shows the RP2350B;
sheet 3 identifies radio module **A1, RM2**.

| Board signal | RP2350 GPIO | Module connection / meaning |
| --- | ---: | --- |
| `WL_ON` | **23** | RM2 `WLON` and `BTON`; radio enable/reset control |
| `WL_D` | **24** | Shared data/IRQ path; gSPI direction ownership matters |
| `WL_CS` | **25** | Radio chip select |
| `WL_CLK` | **29** | Host serial clock |
| `CHARGE_STAT` | Not RP2350 GPIO2 | Connected to **module GPIO2** |

**GPIO41 is not the radio enable.** It controls the switched peripheral
rail used by the display/IMU/touch paths. The diagnostic display may need
that rail while the radio is off. Radio cleanup must operate GPIO23, not
switch off unrelated badge peripherals.

Sheet 3 exposes RM2 SCLK/DI/DO/CS/IRQ and WLAN/Bluetooth enables. This is
not a conventional independent MOSI/MISO/IRQ assignment to three RP pins:
the board's shared `WL_D` net must be driven only during the appropriate
startup/TX phase and released during receive/idle.

The successful software and PIO register probes, stock firmware startup,
association and UDP tests corroborate the usable **23/24/25/29** mapping.
Older badge pin guesses and exploratory failures are not alternative
supported mappings. Do not inherit non-display pins from the 2025 badge.

## Supply, antenna and oscillator: what the schematic does and does not say

| Topic | Established evidence | Do not infer |
| --- | --- | --- |
| Supply path | RM2 uses the always-on 3.3 V path via `VDDBAT`, not `3V3_SW`/GPIO41 | That `WL_ON=0` removes all module supply voltage or gives a measured leakage current |
| Module ratings | Sheet 3 labels VDDIO **1.8–3.3 V**, VDDBAT **3.0–4.8 V** | Those are operating-range annotations, not measured badge rail voltages |
| Enables | WLON and BTON share GPIO23 | Independently controllable WLAN/Bluetooth power enables |
| RF/antenna boundary | The supplied radio sheet represents RM2 as a module; it does not expose an RF port/antenna feed among the listed module pins | Antenna gain, matching network, connector, keep-out dimensions, radiation pattern or an independently verified internal antenna design |
| External clock wiring | `WL_CLK` is the **RP-to-radio bus clock** | That GPIO29 is the RF reference crystal input |
| Oscillator configuration | Pinned NVRAM has `xtalfreq=37400`; exact-image channel selection has a **37,400,000 Hz** branch | That the host bus clock or RP timer has that frequency, or that an oscillator was physically measured at its pins |

The board schematic/module abstraction is insufficient to reconstruct the
internal RF matching and reference-oscillator circuit. No extra oscillator
pin or antenna-control GPIO should be invented. Successful nearby-radio
operation demonstrates a functioning assembled RF path, not a conducted
RF characterization or antenna certification.

The 37.4 MHz configuration is supported independently by the approximate
reset-held **CYW counter rate**, but that counter measurement is not direct
crystal probing and does not establish every PLL/clock state.
See [clock evidence and units](calibration.md#three-different-clock-facts).

Radio supply, bus HT, D11/PHY clock availability and calibration completion
are separate facts. The later [resource experiments](calibration.md#current-hardware-status-and-causal-rc-dependency)
reproduce RC failure by removing an internal PMU resource group, without
changing board wiring or the six candidate inhibits. GPIO23 and correct
gSPI access do not by themselves establish PHY-PLL readiness. These findings
do not identify a new board power/PA-control pin.

The manufacturer passages reviewed in
[activation evidence](../../experimental/cyw43/r3/init/activation-evidence.md)
add chip-level context: the default crystal/TCXO reference is **37.4 MHz**;
the PA is integrated and supplied through an internal LDO from VBAT;
on-chip calibration can occur periodically during normal operation.
WL_REG_ON low resets WLAN and prevents reception; power-off loses prior
state. None establishes a separate badge PA-disable pin or a persistent
RX-compatible inhibit. The newer byte-pinned Rev. *D datasheet was obtained
from a mirror, not verified at a manufacturer-hosted endpoint.

Independent RF measurement is an owner-deferred separate track. Bounded
receive experiments may proceed only under the [current handoff](handoff.md)'s
explicit opt-in and gates; candidate-inhibit readbacks alongside checked
vendor-retained prefixes are not measured TX silence or fresh RF reception
under inhibition.

## The physical chip identity is stronger than an SDK selection

The SDK uses CYW43439 support; the project-owned bus reads chip-common
`0x1545a9af`. The low 16 bits are `0xa9af`, decimal **43439**. R0's
validated revision response reports chip revision **5**, D11 core revision
**39**, and package code **4**. The `bcm43439a0` reference-directory name
does not override those measured fields or identify a named mask-ROM release.

The host's RP2350 A4 revision is unrelated to the CYW chip/ROM revision.
Full radio identity and the observed ROM fingerprint are in
[controller execution](firmware.md#identity-contract).

## Power-up findings, not pin rediscovery

Early register reads failed with `0x5fffffff` on both known badges.
Changing receive edges or adding PIO alone did not fix startup. The final
working sequence:

1. Drives GPIO24 low during radio power-up, then releases it.
2. Asserts CS before driving shared data.
3. Releases data for reception and idle.
4. Uses the verified receive alignment and function-specific padding.

Weak pull-down alone was not equivalent in these tests. The observation
does not prove a particular undocumented internal strap mechanism.
The original traces and intermediate failures are retained in
[the register-probe investigation](../wifi.md#2026-09-15-registerbackplane-probe-passes).
Repeated radio resets are not automatically a full battery/USB cold-power
qualification over every badge revision.
