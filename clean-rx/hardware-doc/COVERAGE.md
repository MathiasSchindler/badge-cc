# Coverage and unspecified interfaces

[Manual contents](README.md)

## Device and revision scope

CYW43439 is the device family identified by ChipCommon device number
43439. The reference describes the Wi-Fi programming interfaces and
configurations explicitly given in its chapters.

It does not define a complete ordering-code, package, stepping or
revision-compatibility matrix. Equal family IDs do not imply identical
analog tuning, ROM contents or undocumented field behavior.

## Coverage by subsystem

| Subsystem | Defined here | Not specified |
| --- | --- | --- |
| Cortex-M3 | SRAM boot organization, vectors, cycle-counter address | Complete ARM instruction/system reference, debug/security lifecycle |
| SOCRAM | 512-KiB aperture and DMA/software ownership | Full bank configuration, ECC/parity modes, retention matrix |
| Backplane | Core bases, wrapper controls, indirect access | Complete interconnect enumeration, arbitration and fault semantics |
| ChipCommon/PMU | Register addresses, resource model, selected 37.4-MHz settings | Every resource name, all timer units, all low-power modes |
| SDIOD/gSPI | Command form, wire ordering, windowing, selected status/clock fields | Electrical timing, maximum rates, complete native SDIO interface |
| D11 MAC | Listed registers and receive/transmit interactions | Every MAC field, all reset values and interrupt semantics |
| PSM | Stored instruction format and useful instruction subset | Complete ISA, all pipeline hazards, instruction-memory capacity |
| RXE | Prefix/body phases, streaming, copy and completion distinctions | Every FIFO depth, all aggregate corner cases and maximum limits |
| DMA | Descriptor form and described ring/control configurations | All modes, burst combinations, error decoding and maximum capacities |
| PHY/radio | Indirect ports, table organization, channel settings and calibration controls | Complete analog field map, receiver transfer functions and RF guarantees |
| Crypto | Selected AES-CCMP programming controls | Complete key-engine architecture and every cipher/mode |
| Coexistence | Listed GCI/D11 control locations | Complete Bluetooth coexistence policy and arbitration |
| Bluetooth | No programming interface defined in this manual | Controller ISA, memory map, HCI transport and radio operation |

## RF capabilities and limits

The defined radio programming is for 2.4-GHz WLAN centers 2412..2484 MHz.
The listed channel settings do not establish that every channel/mode is
permitted in every regulatory region.

This manual defines no:

- 5-GHz or 6-GHz operating mode.
- Out-of-band receive range or sensitivity guarantee.
- Arbitrary continuous-frequency tuning specification.
- Instantaneous RF observation wider than the described WLAN modes.
- Raw IQ/ADC sample acquisition interface.
- FFT/spectrum interface.
- Calibrated RSSI, noise-floor or energy-detection conversion.

An unspecified capability is not automatically absent from the silicon.
It is also not a capability on which software may rely.

## Electrical and physical information

The following are unspecified:

- Package pinout and package dimensions.
- Supply-voltage and absolute-maximum ratings.
- Power-rail sequencing and current limits.
- Digital input/output thresholds and drive strengths.
- External crystal electrical requirements.
- RF matching, antenna layout and conducted-power calibration.
- Temperature, ESD and environmental ratings.
- Minimum reset/power-enable times and maximum serial clock.
- Production test, OTP layout and factory-programming procedures.

Register programming is not a replacement for those specifications.
Do not derive an electrical limit from a software delay or one working
bus frequency.

## Reset values and unlisted bits

Unless explicitly defined, a register's reset value and the semantics of
unlisted bits are unspecified.

- Do not read undefined locations merely to discover whether they respond.
- Do not assume a write of all zeroes is harmless.
- Do not assume a register read is free of side effects.
- Do not copy read-only status bits back into a control register without
  a defined read/modify/write operation.
- Do not use an all-ones read as a valid completion value.

The manual's complete control settings apply to the stated sequence,
not to arbitrary runtime states.

## Software-defined objects

The following have no universal hardware-assigned address or meaning:

- Host/controller command mailboxes.
- Boot-stage numbers and software error codes.
- Capture slot counts and capture-record formats.
- Frame/diagnostic buffers and test records.
- Logical transport credits and packet envelopes.
- Station addresses, response tables and PSM state variables in SHM.
- Executable/BSS/stack subdivisions of SRAM.

Such objects can be designed using the chip resources described here.
They must not be treated as fixed chip registers merely because one
program places them at a particular address.

## Completion and timing guarantees

A status indication describes its own hardware boundary. It does not
implicitly establish a later boundary:

| Indication | Does not by itself establish |
| --- | --- |
| PLL/calibration predicate | Actual RF frequency, sensitivity or clean waveform |
| PLCP decoded | Complete current MPDU |
| Copy count reached | Every retained byte has current-frame provenance |
| GoodFCS status | Correct host transport or buffer ownership |
| DMA descriptor advanced | Host consumption or packet acceptance |
| TX trigger written | Physical transmission started |
| MAC TX drained | Physical waveform ended |
| Response scheduled | Exact RF SIFS timing or peer receipt |
| No captured packet | No RF signal |

Loss-free operation, exact physical response timing and interoperability
across all modes are not blanket guarantees of this reference.
