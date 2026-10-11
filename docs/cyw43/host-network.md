# Project-owned host driver and networking

[Guide](README.md) · [Hardware](hardware.md) · [Workflow](workflow.md)

## Dependency and processor boundary

The working no-SDK path owns the **RP2350 executable**: startup, display,
USB reporting, GPIO/PIO gSPI, loader, control protocol, Ethernet framing,
ARP, static IPv4 and UDP. It links without libc, Pico SDK, lwIP, BTstack,
an allocator or an imported host radio driver.

The CYW43439 still runs the approved **vendor RAM firmware, D11 microcode
and ROM**. NVRAM and CLM are separate board/regulatory assets. “Freestanding
host” does not mean an entirely source-owned radio. The independent
[source-only controller research](firmware.md#current-execution-lineages---2026-09-22)
is a separate, non-networking track: original-C startup/calibration and a
bounded quiescent handoff pass, but later cold MAC/PSM attempts still have
no accepted fresh frame. The vendor-retained original PSM has captured
fresh complete MPDUs as well as earlier checked CCK prefixes; those results
do not replace the vendor firmware dependency of this working host stack
or prove cold-source RX. See the [current status](README.md#current-status).
Reference implementations have been studied; no clean-room claim is made.

The target dependency boundary is unchanged. Standard-library Python research
collectors and legacy Ruby host tools are host tooling, not production target
libc/SDK dependencies; these experiments introduce no new target dependency.

| Project source | Responsibility |
| --- | --- |
| [wifi_gspi.c](../../src/radio/wifi_gspi.c), [wifi_probe.c](../../src/radio/wifi_probe.c) | Wire encoding, bus/reset/register checks |
| [wifi_firmware.c](../../src/radio/wifi_firmware.c) | Verified download, function-2 transport and firmware control |
| [wifi_control.c](../../src/radio/wifi_control.c) | Control operations and bounded responses |
| [wifi_link.c](../../src/radio/wifi_link.c) | SoftAP/station lifecycle and authorized peers |
| [wifi_udp.c](../../src/radio/wifi_udp.c) | Narrow Ethernet/ARP/IPv4/UDP application path |
| [wifi_assets.c](../../src/radio/wifi_assets.c), [asset packer](../../src/host/tools/wifi_pack_assets.c) | Explicit pinned data exception |
| [Wi-Fi roadmap/results](../wifi.md) | Full milestones, reports, errors and implementation details |

## Transport and startup contract

```text
RP2350 application
  -> CDC commands/events or BDC Ethernet data
  -> SDPCM framing, sequence/flow control, interface selection
  -> gSPI function 2
  -> CYW vendor firmware

Reset, clocks, RAM and ROM access
  -> gSPI function 0 / windowed function 1 backplane access
```

Here **CDC is the radio control framing**, not a USB communications-device
class. Lab USB is a project-owned vendor-specific reporting interface.
BDC wraps Ethernet data; SDPCM carries control/data/event traffic and
flow-control information. They are not raw over-the-air 802.11 frames.

### Bus proof before firmware

| Check | Final observed value |
| --- | --- |
| Reset-time test, then normal-order test | `0xfeedbead` |
| Repeated normal reads | 16/16 |
| Bus control | `0x000200a3`, **not** the old `0x000204a3` |
| Function-1 padding | Register `0x1d` = **16 bytes** |
| ALP CSR | `0x48` |
| Chip-common | `0x1545a9af` |

Reset-time 16-bit ordering and normal 32-bit ordering are distinct. A first
reset transaction sometimes returned `03 03 03 03`; bounded retries produced
the constant. Raw first-transaction data and final probe status remain
separate. Software RX samples before the next rising clock; PIO supplies
the corresponding edge-aligned sample. Do not replace an unexplained
response with synthetic success.

Wire ordering is not the same as backplane access width. Function-1 address
bit`0x8000` selects32-bit access; the later PHY/radio research required
unflagged halfword accesses. That correction does not change the working
host's word/bulk-transfer measurements above.
[Indexed-port width qualification](phy-radio.md#direct-and-indexed-ports).

The small-register PIO probe uses PIO0/SM0, divider 8, MSB-first 32-bit FIFO
words and no DMA or external PIO assembler dependency. Its original scope
must not be confused with a qualified arbitrary-length DMA transport.

### Verified vendor startup

The M1b loader validates asset CRCs, bounds and chip identity, holds ARM in
reset, initializes SOCSRAM, then:

1. Writes firmware in at most **64-byte function-1 blocks**, never crossing
   a **32-KiB backplane window** within a transfer.
2. Compares every padded firmware byte.
3. Writes/compares top-of-RAM NVRAM and its length/complement token.
4. Releases ARM only after verification; bounds HT/function-2 readiness.
5. Issues SDPCM/CDC `GET_VAR ver`, validating envelope, command, request ID,
   status and response length.
6. Uploads CLM in at most **512-byte data chunks**, with zeroed eight-byte
   payload alignment, then requires `clmload_status=0`.

The CLM chunk limit is not the backplane block limit. M1b powers off on
success as well as on error/cancellation; it intentionally does not join,
scan, enable an AP or perform networking. Later link/UDP images retain the
radio for their own bounded sessions. Firmware readback alone is not startup
proof; startup alone is not association proof.

## Working packet subset

The paired-link baseline uses channel-1 WPA2/AES SoftAP/station, matching
MACs and explicit association/authorization checks. Both badges have passed
in both roles. The UDP fixture adds:

| Item | Contract |
| --- | --- |
| Addressing | AP `192.168.77.1`, station `192.168.77.2`, UDP port **30526** |
| Ethernet buffering | Four-frame receive queue retains packets arriving during control requests; drops/rejections counted |
| ARP | One authorized-peer mapping, established by a real ARP exchange; bounded requests and expiry |
| IPv4 | Version 4, IHL 5, valid checksum, nonzero TTL; DF allowed, fragmentation rejected |
| UDP | Exact lengths/ports, **nonzero valid checksum** required by this fixture |
| Echo | 32-byte `U26E` payload, role/type, sequence, timestamp and checked deterministic pattern |
| Scheduling | 100 requests per direction, at most one outstanding request; no application retry after timeout |
| Not implemented here | DHCP, DNS, TCP, routing, IPv6, VLAN, IP options/fragments, socket API, ordinary ICMP ping |

Radio-layer retries can still occur. A UDP “loss” is an application
round-trip timeout, not a localization of request, response or 802.11 loss.
One side continues answering after its own requests finish.

## Physical Wi-Fi measurements versus BLE observations

The unmodified UDP build `4c6ff5b1` completed three paired runs including
reversed roles: **600/600 echo transactions**, zero observed loss, duplicates,
rejected frames or radio-queue drops. Across six directional results:

| Metric | Observed baseline |
| --- | --- |
| Mean application RTT | **8.232–9.241 ms** |
| Directional p95 range | **9.478–16.224 ms** |
| Largest observed RTT | **67.824 ms** |
| Recovery | Both badges back in BOOTSEL after each run |

Each badge calibrates its timer against at least **128 USB SOF intervals**:

```text
RTT_ms = RTT_ticks * measured_USB_frame_count / calibration_ticks
```

This includes application scheduling, gSPI/PIO, firmware queues, echo
processing and USB/display work. It is not one-way latency, RF airtime,
throughput, an optimized SDK comparison or a worst-case bound. Three short
nearby-device sessions cannot establish conference-scale reliability.
[All six results and limits](../wifi.md#physical-udp-results) remain available.

[Wireless Lab](../wireless-lab.md) separately recorded BLE application RTT
averages of approximately **14 ms central / 13 ms peripheral**, with 100/100
acknowledgements per adjacent-badge 7.5 ms/latency-0 test. The owner also
reported fast [BLE tilt Pong](../ble-tilt-pong.md) with no significant perceived
lag. That gameplay statement is **qualitative**, not a measured game-loop
latency or verification of negotiated connection parameters.

Do not declare Wi-Fi universally faster than BLE from these different
protocols, workloads, firmware stacks and sample sets. R1's later modified
vendor regression passed another 400/400 transactions, but extra diagnostic
IOCTLs and limited runs likewise do not establish a speedup.
