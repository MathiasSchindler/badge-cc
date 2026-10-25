# Reset, clocks and power resources

[Manual contents](README.md)

## Core wrappers

| Wrapper-relative offset | Width | Function |
| --- | --- | --- |
| `0x408` | 32-bit | IOCTRL: local clock/reset-related controls |
| `0x800` | 32-bit | RESETCTRL: bit 0 asserts core reset |

Relevant wrapper bases are:

| Core | Wrapper base |
| --- | --- |
| D11 | `0x18101000` |
| Cortex-M3 | `0x18103000` |
| SOCRAM | `0x18104000` |

For Cortex-M3/SOCRAM initialization, IOCTRL value `3` is used during
reset/forced-clock transitions, and `1` is used after reset release.
The complete IOCTRL bit assignment is not specified here.

Read back wrapper writes before proceeding to the dependent operation.
Do not reset a core that owns live DMA buffers or an active radio operation
without first establishing a shutdown/recovery policy.

## Controller and SRAM startup

A host-controlled SRAM boot sequence is:

1. Assert Cortex-M3 reset.
2. Establish its reset-time wrapper clock setting.
3. Assert SOCRAM reset, establish its clock setting, release SOCRAM reset
   and return its IOCTRL to `1`.
4. Write zero to SOCRAM registers `0x18004010` and `0x18004044` for the
   bank/control configuration used by this SRAM boot mode. Their full
   field definitions are unspecified.
5. Initialize required SRAM, including vectors, executable bytes, stack
   storage, descriptors and communication data.
6. Read back executable storage before execution.
7. Set Cortex-M3 IOCTRL to `3`, release RESETCTRL, then set IOCTRL to `1`.

The entry vector and initial stack pointer are supplied by the SRAM image.
There is no mandatory software mailbox address or boot-complete value.

The Cortex-M3 cycle counter is readable at `0x18003090`.
Its increments are clock-dependent; a raw tick count is not automatically
microseconds. The PHY initialization configuration uses 180 controller
ticks per nominal microsecond in its main operating-clock phase; that
conversion must not be applied to every clock phase.

## ChipCommon and PMU register reference

All addresses in this table are backplane byte addresses; registers are
32-bit.

| Address | Register/function |
| --- | --- |
| `0x18000000` | Chip identification |
| `0x18000004` | Capabilities |
| `0x180000ac` | Extended capabilities |
| `0x180001e0` | ChipCommon clock control/status |
| `0x18000504` | Save/restore control 0 |
| `0x18000508` | Save/restore control 1 |
| `0x18000600` | PMU control |
| `0x18000604` | PMU capabilities |
| `0x18000608` | PMU status |
| `0x1800060c` | Resource state |
| `0x18000610` | Resource transitions pending |
| `0x18000614` | PMU timer |
| `0x18000618` | Minimum resource mask |
| `0x1800061c` | Maximum resource mask |
| `0x18000620` | Resource-table selector |
| `0x18000624` | Selected resource dependency mask |
| `0x18000628` | Selected resource up/down timer |
| `0x18000658` | Regulator-control bank selector |
| `0x1800065c` | Selected regulator-control data |
| `0x18000660` | PLL-control bank selector |
| `0x18000664` | Selected PLL-control data |

Save/restore controls and capability words are listed for address
identification; their complete bit definitions are unspecified.

### Indirect PMU banks

Select a bank index, read back the selector, then read/modify/write its
data register. Serialize the entire operation. A data register does not
have a permanent meaning independent of its selector.

### Resource model

Minimum and maximum resource masks constrain the PMU's resource requests.
Dependencies and up/down timing are programmed by selecting a resource
index at `0x18000620`.

The resource-state register describes active resources; the pending register
describes transitions. A brief zero pending value need not imply that
all immediately subsequent dependency transitions have finished.
Require a bounded quiet interval as well as the desired clock state.

Resource bits `0x70000000` participate in the HT-clock PLL programming
sequence. Individual resource names for all 31 bits are unspecified.

ChipCommon clock status uses `0x00020000` for the HT indication used in
PLL sequencing. The combined availability check used after the host HT
request is `(value & 0x00030000) == 0x00030000`.

## 37.4-MHz crystal configuration

The following is a configuration for a 37.4-MHz reference crystal, not
a universal configuration for other crystals.

The crystal divisor parameter is:

```text
xtal_div = ceil(37400 / 128) - 1 = 292 = 0x124
PMU_CONTROL = ((old | 0x200) & 0x0000ff83)
              | (xtal_div << 16) | 0x3c
```

PMU control bit `0x400` commits the PLL configuration.

### PLL-bank values

| Index | Value |
| --- | --- |
| 0 | `0x00000000` |
| 1 | `0x00000000` |
| 2 | `0x00000431` |
| 3 | `0x00ba2e8c` |
| 4 | `0x04040404` |
| 5 | `0x00040104` |
| 6 | `0x080004e2` |
| 7 | `0x0000000a` |
| 8 | `0x0000106a` |

Before PLL programming, preserve the existing minimum/maximum masks,
remove `0x70000000` from both, and wait for the ChipCommon HT indication
to clear. A prepare operation sets PLL bank 5 low byte to 60 and pulses
regulator bank 6 mask `0x00800000`.

After writing the PLL bank and committing, restore previously requested
HT resource-mask bits. Resource dependency/timer configuration remains a
separate requirement; the PLL words alone are not a full power-up recipe.

An active resource configuration uses minimum mask `0x0ec23df7` and maximum
mask `0x7fffffff`. These are operating settings, not reset values or a
description of low-power policy.

### Resource dependency configuration

For each resource index, write the selector at `0x18000620` and the
dependency mask at `0x18000624`. The following operating configuration
programs resources in descending order, 30 through 0:

| Resource | Dependency mask | Resource | Dependency mask |
| --- | --- | --- | --- |
| 0 | `0x00000000` | 16 | `0x00000000` |
| 1 | `0x00000001` | 17 | `0x0ec03df7` |
| 2 | `0x00000003` | 18 | `0x0ec23df7` |
| 3 | `0x00000000` | 19 | `0x0ec23df7` |
| 4 | `0x00000007` | 20 | `0x06c03df7` |
| 5 | `0x00001157` | 21 | `0x0eda3df7` |
| 6 | `0x00000117` | 22 | `0x06003df7` |
| 7 | `0x00000157` | 23 | `0x06403df7` |
| 8 | `0x00000003` | 24 | `0x00000000` |
| 9 | `0x00000000` | 25 | `0x000001d7` |
| 10 | `0x00000003` | 26 | `0x00000dd7` |
| 11 | `0x00000407` | 27 | `0x06c03df7` |
| 12 | `0x00000157` | 28 | `0x0eca3df7` |
| 13 | `0x000015d7` | 29 | `0x1eca3df7` |
| 14 | `0x0ec2bdf7` | 30 | `0x3efa3df7` |
| 15 | `0x0ec23df7` | | |

Resource indices are hardware table selectors, not software boot states.
The table does not assign a descriptive name to every resource bit.

### Resource timing programming order

Before dependency programming, write the following ordered pairs to
resource selector `0x18000620` and timer data `0x18000628`.
Repeated resource 12 entries are part of this sequence.
The timing values are encoded register words, not microsecond literals.

```text
resource  timer
12        0x00300002
30        0x00010001
29        0x00010001
26        0x00130002
25        0x00130002
24        0x00130002
22        0x00010001
21        0x00010001
20        0x00010001
19        0x00010001
18        0x00030003
17        0x00010001
16        0x00010001
15        0x00010001
14        0x00010001
12        0x00300002
11        0x00010001
 9        0x00050001
 6        0x00010001
 5        0x00010001
 4        0x00010001
 3        0x00020002
 1        0x00010001
 0        0x00020002
28        0x00070001
27        0x00010004
23        0x00040004
13        0x00070001
10        0x000d0004
 8        0x000d0004
 7        0x000a0001
 2        0x000d0004
12        0x00160002
```

## D11/PHY reset and clock sequence

With transmissions inhibited and D11 stopped:

1. Assert D11 wrapper reset.
2. Set IOCTRL to `0x0f`.
3. Release D11 reset while holding PHY reset.
4. Set IOCTRL to `0x0d`.
5. Establish stopped MAC control using `0x04000004`, then `0x04000404`.
6. Apply these IOCTRL read/modify/write steps in order:

| Mask | Masked value |
| --- | --- |
| `0x000000cc` | `0x0000004c` |
| `0x0000000e` | `0x00000002` |
| `0x00000006` | `0x00000004` |

The resulting IOCTRL is `0x45` for this starting state.
The radio-initialization phase subsequently uses `0x2055`.
These values are sequence settings, not a complete wrapper field map.

At D11 clock-control/status `0x180011e0`, value `0x240` requests the
PHY-PLL operating configuration. Readiness requires the availability bit
`0x02000000`, together with resource state satisfying mask `0x3eca3df7`.

PHY reset, radio reset, PSM stop, RX flush and core reset have different
effects. They are not interchangeable ways to stop packet processing.

## Antenna/coexistence control

GCI output register 1 is at `0x18000d64`; GCI control register 1 is at
`0x18000d74`. D11 coexistence controls are listed in
[the D11 register chapter](D11_REGISTERS.md#coexistence-controls).

Wi-Fi RF operation requires the appropriate antenna/coexistence grant,
not just a running PHY. Complete Bluetooth coexistence arbitration and
low-power transition behavior are unspecified.
