# CYW43439 hardware and firmware architecture

The controller is a freestanding Cortex-M3 program with static storage.
The RP2350 is its gSPI host. Radio initialization, PSM execution, frame
capture and DMA run on the CYW43439; the badge application handles host
protocols, software cryptography and application traffic.

The hardware names and register map below describe the paths used by the
source code. They are implementation identifiers, not a badge-specific
ordering-code or silicon-revision identification. See the
[device-family reference scope](README.md).

## Cores and data paths

| Core | Register base | Wrapper base | Role |
| --- | --- | --- | --- |
| ChipCommon | `0x18000000` | `0x18100000` | PMU, PLL, resources and GCI |
| D11 | `0x18001000` | `0x18101000` | MAC, PSM, RXE/TXE, SHM, DMA and PHY/radio bridges |
| SDIOD | `0x18002000` | `0x18102000` | gSPI interface and host-facing DMA |
| Cortex-M3 | `0x18003000` | `0x18103000` | Controller execution |
| SOCRAM | `0x18004000` | `0x18104000` | 512-KiB SRAM |

The captured-payload path is antenna -> Radio 20691 -> LCN20 PHY ->
D11 RXE/SHM -> Cortex-M3 SHM reads -> controller frame storage ->
SDIOD TX DMA -> gSPI Function 2 -> RP2350.

Separately, the PSM publishes a receive header to the D11 RX FIFO and
D11 RX DMA transfers it to SRAM. The controller records DMA completion,
length and header bytes alongside the SHM-derived capture.

The raw transmit path is host staging -> D11 template RAM -> TXE ->
LCN20 PHY -> Radio 20691 -> antenna. Profile-3 transmission and pinned
responses use the PSM to arbitrate the medium and trigger TX.

## Memory layout

The definitions in [frame_limits.h](../src/frame_limits.h),
[cyw43_hw.h](../src/cyw43_hw.h) and the [linker script](../src/cyw43.ld)
are shared with the implementation.

| Region | Default | Extended RX |
| --- | --- | --- |
| Vectors and executable region | `0x00000..0x13fff` | Same |
| BSS region | `0x14000..0x1bfff` | Same |
| Initial stack pointer | `0x1f000` | Same |
| 480-byte mailbox | `0x20000` | Same |
| Eight-word layout manifest | `0x203c0` | Same |
| Eight capture slots plus header | `0x20d80` | Same base |
| MPDU capacity | 2,400 bytes | 3,896 bytes |
| Capture SHM base, word index | `0x100` | `0x800` |
| D11 descriptor ring | `0x26000` | `0x30000` |
| D11 buffers | `0x26200` | `0x30200` |
| D11 buffer size | 2,464 bytes | 3,968 bytes |
| SDIOD descriptor ring | `0x3a000` | `0x52000` |
| SDIOD buffers | `0x3a200` | `0x52200` |
| TX/key staging | `0x4a400` | `0x64000` |
| Maximum F2 fragments | 2 | 3 |

Both DMA rings contain 32 descriptors of 16 bytes. SDIOD buffers are
2,048 bytes. The capture structure contains eight slots; each slot has
56 metadata bytes plus `CLEAN_RAW_BYTES`. Compile-time assertions bound
the regions and enforce descriptor-ring alignment.

The layout manifest contains magic `0x3159414c`, MPDU capacity, SHM base,
D11 ring base, SDIOD ring base, TX staging base, fragment count and D11
buffer size. The host checks every field before starting reception.
Extended RX initialization tests upper SHM readback with MAC/PSM stopped.

SHM addresses in the source are 16-bit word indices. SRAM and MMIO
addresses are byte addresses.

## Reset, clocks and radio initialization

The host controls each core wrapper through `IOCTRL` at `+0x408` and
`RESETCTRL` at `+0x800`. The Cortex-M3 reset vector masks interrupts,
sets VTOR, clears BSS and enters the controller.

[pmu.c](../src/pmu.c) configures the 37.4-MHz crystal, PLL table and
resource masks. The host and controller handshake for the HT PLL resource
before D11 and PHY initialization.

[radio_phy.c](../src/radio_phy.c) releases D11/PHY reset, establishes TX
inhibit, loads the ROM calibration tables, configures Radio 20691 and runs
LPF/ADC calibration. Channel setup programs the synthesizer, waits for VCO
lock, sets receive Farrow timing, performs DC calibration and applies AGC.

PHY accesses use the indirect selector/data ports at D11 `+0x3fc/+0x3fe`;
radio accesses use `+0x3d8/+0x3da`. Table accesses preserve the table-access
clock override. GCI and D11 coexistence registers grant the antenna to
Wi-Fi.

The transmit configuration programs radio bias/filter values, PHY manual
gain, IQ table entries, IIR filters and channel-specific TX Farrow values.
`phy_tx_set_inhibit` owns the explicit TX overrides.
`phy_tx_set_automatic` releases the owned overrides for PHY-sequenced
turnaround.

## PSM receive processing

[psm_ucode.c](../src/psm_ucode.c) supplies the Arch15 instruction image;
[psm_ack.h](../src/psm_ack.h) defines the appended response and medium-access
program. The image is loaded through D11's object selector/data port.

1. Initialize the PSM mailbox, generation token, receive bounds and header
   area. Flush RXE under a bounded countdown and signal ready.
2. Wait for PLCP-valid, copy six prefix bytes and wait for their RXE count.
3. Continue the receive transfer and enable RX-buffer streaming after the
   128-byte prefix threshold.
4. Require terminal completion and GoodFCS before capture publication.
5. Construct the 24-byte receive header in SHM `0x7f4..0x7ff`, including
   RXE length, four PHY words, PLCP and channel.
6. Publish the header to the RX FIFO and hold the capture until the
   controller acknowledges it, then flush and rearm.

The PSM fault state is 15 at SHM `0x601`. One-shot idle listening has a
64 * 65,535-poll budget. Hardware DCF mode restarts that idle wait on
expiry; raw one-shot mode records reason 6. Flush and control waits have
their own bounds.

SHM instructions have one memory input. Address comparisons stage one
word in a register before comparing it to another SHM word.

## DMA and host credits

D11 RX DMA transfers the PSM header into a 32-descriptor ring. The
reclaim routine copies 38 DMA-header bytes and records completion and
length; it does not supply the captured MPDU. The controller reads the
PLCP/MPDU bytes directly from SHM into its capture area and records the
associated metadata.
The SDIOD TX ring carries host-facing packets with 32-byte DMA bursts.
Its final descriptor marks end-of-table.

The F2 transport permits one unconsumed logical frame. The host increments
the consumed counter only after validating the complete packet or fragment
assembly. Publication increments the posted counter. Descriptor completion
and host consumption are distinct events.

An occupied credit or descriptor prevents publication. Station profiles 2
and 3 stage captures in an eight-slot SRAM FIFO, releasing the single PSM
SHM capture after copying its bytes and metadata. Publication preserves FIFO
order and retains the head across credit, descriptor and fragment
backpressure. A full FIFO leaves the next capture in SHM rather than
overwriting queued data. Target-based completion drains staged frames;
an explicit stop can leave pending reception.

After NAV observation, these station profiles select own-address captures
and group-address management/data frames transmitted by the pinned peer.
Unrelated traffic does not consume F2 credit. This selection is not frame
validation: own-address captures still require host validation. Passive/raw
capture is not filtered this way.

Other receive paths retain their prepared capture until publication.
Raw TX/rearm, retuning and key commands clear prepared transport state when
discarding a capture.

### Rejected authentication evidence

The PSM bad-FCS path retains a bounded candidate snapshot for active station
profiles 2/3: RXE length 40, authentication subtype, and either local RA or
pinned-peer TA. The record contains six PLCP bytes and the 34-byte MPDU,
RXE/PHY status, a sampled TSF value and a wrapping 16-bit count. It does not
ACK or publish rejected data and does not validate the candidate's identity.

On reception shutdown the controller copies this 28-word SHM record
(`0x750`) into a 60-byte `BAF1` SRAM record at `0x204b0`. The badge copies it
to USB detail offset 600 only when no validated authentication response is
saved there; the authentication-response length at offset 596 remains zero.
The collector distinguishes this record from successful response evidence
with `auth_bad_fcs_report`. These sampled times are not RF/SIFS timestamps.

## Function 2 packet format

An ordinary packet has an 18-byte SDPCM/BDC header:

| Bytes | Meaning |
| --- | --- |
| 0..1 | Packet length, little-endian |
| 2..3 | One's complement of length |
| 4 | Logical sequence |
| 5 | Channel 2 |
| 6 | Next length |
| 7 | Data offset, 14 |
| 8..9 | Flow control and credit |
| 10..11 | Padding |
| 12..13 | RX publication-phase tag |
| 14 | BDC flags, `0x20` |
| 15 | PLCP rate, or HT MCS with HT metadata |
| 16 | Channel and PHY flags |
| 17 | BDC offset, or HT metadata version |
| 18 onward | MPDU including FCS |

Byte 16 uses `0x10` for OFDM, `0x20` for CCK short preamble, `0x40`
for unsupported legacy interpretation and `0x80` for HT metadata.
The phase word starts at `0x5258` and changes to `0x52d8` when an explicit
TX command reaches its RX-resume path; the tag then persists until reset.
A late BUSY result with zero attempts can also reach that path. The tag
therefore identifies command-processing phase, not a confirmed transmission
or received ACK. Use the separate TX/ACK result and attempt count for those
outcomes.

MPDUs up to 2,026 bytes fit a single physical packet. Larger frames use
BDC flags `0x21`: bytes 18..19 contain total MPDU length, bytes 20..21
the MPDU offset, and payload starts at byte 22. Each full fragment carries
1,536 MPDU bytes; the last carries the remainder. Every physical packet
has its own length/complement and fits the 2,044-byte transport limit.

Fragments share logical sequence and PHY/channel metadata. The host
checks ordered contiguous offsets, payload lengths and total size, then
reconstructs the ordinary wrapper. One complete assembly consumes one
credit. Transport fragmentation is separate from 802.11 fragmentation.

## Transmit processing

Raw legacy TX validates the PLCP, MPDU size, rate, parity/CRC fields and
command flags before programming TX hardware. It pauses MAC/PSM, writes
the complete template and reads it back, configures TXE/PHY controls,
triggers transmission, waits for bounded completion and restores RX.
The OFDM path includes a PPDU-tail wait using the D11 microsecond timer.

Legacy OFDM uses `PHYCTL=0x0041`; long/short DSSS/CCK use `0x0040/0x0050`.
Rate-specific PHYCTL1 values and airtime helpers are defined in
[phy_rates.h](../src/phy_rates.h). The [HT20 reference](HT20.md) describes
its template controls and descriptors.

Command 6 copies eight staged 16-bit key words to SHM `0x618..0x61f`
and sets the hardware-crypto selector at `0x615`. The raw hardware path
programs D11 key, IV-offset, MIC and frame-length controls. The badge's
WPA2 station uses its separate software-crypto implementation.

## MAC and response ownership

The raw profile uses a Cortex-driven ACK template for matching unicast
management/data frames. Station command 7 selects the pinned-peer PSM
response path instead.

Station configuration, live template preparation and policy/EDCA updates
freeze PSM register ownership by clearing only `PSM_RUN` in MACCONTROL.
They preserve MAC receive enable and the running filter/control bits.
After freezing PSM, live transmit/policy paths check receive state and
response ownership before replacing hardware state. A busy receive defers TX.
An ACK timeout does not disable station RX: a management or data reply may
still be arriving. Raw TX/rearm and explicit shutdown retain their separate
MAC-stop behavior.

The PSM checks GoodFCS, local RA, pinned TA, frame class and response policy
before scheduling an ACK or CTS. Profiles 2 and 3 consume local ACKs
internally, recording ACK acceptance only during an active ACK wait.
Response policy also selects basic response rates and short preamble.

Profile 3 samples IFS/carrier/NAV and TSF, times DIFS, counts 9/20-us
backoff slots and freezes residual backoff during busy periods. Bad FCS
and invalid length establish EIFS; valid receive processing clears it.
Cortex selects the contention window and draw and prepares the template;
PSM fires the armed request. GoodFCS cancels an armed request before
response setup can replace TXE state.

Response ownership spans scheduled start and physical tail. Cortex checks
ownership and IFS before replacing templates or policy. Invalid counters
disable contention and produce an explicit controller error.

For hardware-timed DSSS/CCK station TX, the CPU airtime guard uses the
PSM's transmit-start latch once hardware start is observed. MAC drain time
overlaps that guard rather than starting a second full-airtime wait.
Other PHYs, and a pending hardware start, retain the conservative
post-drain guard. RX and PSM remain live throughout; the guard is not an
independent PHY-tail or SIFS-timing measurement.

## Host MAC and protected reception

[mac.c](../src/mac.c) implements duplicate tracking, sequence wrap,
bounded fragmentation and receive expiry. QoS reception has 16 independent
TID contexts, each with 64 duplicate-history entries, one active MSDU,
up to 16 fragments, a 2,304-byte payload bound and a 512-ms lifetime.
The opt-in WMM station path dispatches accepted unprotected QoS frames to
these contexts and rewrites the F2 length/FCS after reassembly. Protected
QoS frames retain their original encrypted bytes for CCMP processing.
Default non-WMM station operation does not use this QoS dispatch.

[wpa2.c](../badge/wpa2.c) authenticates and decrypts software CCMP before
passing plaintext to authenticated MAC processing. It maintains per-TID
PN replay state and requires consecutive fragment PNs and consistent
header identity. Completion, expiry and terminal failure wipe plaintext
assembly state.

The A-MSDU parser validates the entire aggregate before exposing
zero-copy subframes. It checks source addresses, big-endian payload
lengths, aggregate/MSDU bounds and padding boundaries.

The WMM station with extended reception also admits unfragmented protected
QoS A-MSDUs: up to 3,839 aggregate bytes, or a 3,885-byte MPDU including
the QoS header, CCMP envelope and FCS. Ordinary CCMP MSDUs retain their
2,304-byte bound. After MIC verification, the entire decrypted aggregate
and every subframe destination are checked before committing replay state
or entering the authenticated duplicate cache. Every subframe must target
the local station. The parser rejects LLC/SNAP bytes used as a subframe
destination, as the CCMP MIC does not authenticate the A-MSDU-present bit.
Only a newly delivered aggregate increments the aggregate/subframe counters;
replays and valid-MIC Retry duplicates do not deliver again. Plaintext is
wiped on delivery or terminal failure.
