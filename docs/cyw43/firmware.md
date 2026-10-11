# Controller execution, images and memory ownership

[Guide](README.md) · [Receive path](rx-path.md) · [Provenance](references.md)

## Identity contract

The strongest available identity binds the **device, exact RAM image,
observed ROM window, build and reset path**, not just a product name.

| Item | Established value |
| --- | --- |
| Research RP2350 serial | `9BBF88DD17D66E7D` |
| Reference badge serial | `08DA26F86E57F5EA` |
| Chip-common word | `0x1545a9af` |
| Revision-info fields | Chip 43439, chip revision 5, D11 revision 39, package 4 |
| Vendor version / FWID | `7.95.49 (2271bb6 CY)` / `01-c51d9400` |
| Controller CPU | ARMv7-M, little-endian Thumb-2, Cortex-M3/ARMCM3 |
| Observed CPUID | `0x412fc230` |
| Controller RAM | **512 KiB**, `[0x00000000,0x00080000)` |
| CPU ROM map | `[0x00800000,0x008a0000)` |
| Backplane ROM read map | `[0x1e000000,0x1e0a0000)`, **655,360 bytes** |
| ROM-window SHA256 | `915edc5880a961032af35a009d842323981a74a81afbfc955206d60943f6089e` |
| ROM-window CRC32 | `298d2833` |

[R0 physical evidence](../../experimental/cyw43/R0-results.md) validates the
first 68 bytes of the 128-byte revision reply as 17 little-endian words;
the remaining zero bytes are not invented extra fields. The ROM was hashed
on RP via bounded reads, including anti-blank/repeat and window checks.
R2 subsequently matched it while ARM remained reset-held, without stock
startup. Full ROM contents were not exported.

CPU and backplane ROM addresses are **different address spaces**. The
fingerprint is not a vendor-signed semantic ROM version. CPU-visible
flash-patched ROM need not equal unpatched backplane bytes. The host's
Cortex-M33, RP2350 A4, TrustZone and boot ROM are not the radio's ABI.

## Exact stock assets

| Asset | Stored bytes | SHA256 |
| --- | ---: | --- |
| Firmware, padded | 224,192 | `204e802a87c97d691b14eb7c42c41dba6fe109c26451529f4f7a0536342a065f` |
| Firmware, original prefix | 224,190 | `dda7d2527e1cc9b1ebdd107d46932643617f6e83040f24cba722cbb0ded8c4a8` |
| NVRAM, padded | 744 | `4e85b562cdc6b3914b7bf73a2a62faf484c1ad0673a15ad607134b29e4554954` |
| CLM | 984 | `e712b3d218e8b1e2747b092e03b8b0afcb8c8c8e355d2a4a0d47b493800f3f89` |
| Expanded D11 PSM image | 50,264 | `9d30a53a63f0eb52cbca32e6ddf9f633f9312659226b6d70a46ea9e73ff04f17` |

The combined vendor array is 225,240 bytes: firmware prefix, **66 bytes
container padding**, then CLM at byte **224,256**, length **984**.
Only two padding bytes belong to the generated firmware download.
NVRAM is **743 original / 744 padded bytes**. Its embedded nulls must not
be truncated with `strlen`.

Output CRC32s are firmware `e627c344`, NVRAM `fa352d20`, CLM `29aa78fd`,
covering complete generated assets including download padding. NVRAM goes
to `[0x7fd14,0x7fffc)`, with token **`0xff4500ba`** at `0x7fffc`.
The token is not part of the NVRAM asset.

The local combined header and Nexmon's header have different **text**
hashes, but extracting the declared firmware prefix proved byte equality.
Version equality alone would not have established this.
[The asset contract](../wifi-assets.md) owns header hashes, extraction
validation, license notices and distribution restrictions.

## Stock image layout is not a free-memory map

All ranges below are half-open controller RAM/file byte offsets.

| Range | Content / ownership |
| --- | --- |
| `0..0x800` | Vector-like entries and mixed vendor state |
| `0x800..0x1000` | Reference flash-patch data area; live active end observed `0xa68`, not original `0xa60` |
| `0x1000..0x1390` | Original flash-patch configuration |
| `0x1390..0x2b87c` | Mixed ARM code, literals, data and initialization material |
| `0x2b87c..0x36449` | **43,981 packed PSM bytes**, not ARM code |
| `0x36449..0x3644c` | Alignment |
| `0x3644c..0x36a8c` | **1,600-byte** save/restore template; not established free space |
| `0x36a8c..0x36bbe` | Version/trailer |
| `0x36bbe..0x36bc0` | Two download-padding bytes |
| `0x36bc0..0x80000` | Outside downloaded file, but subject to vendor runtime ownership |

Packed microcode expands seven input bytes into eight output bytes:
**6,283 PSM instruction words**, each stored as two 32-bit words, with
arch15's 51-bit packing constraint. This is a separate instruction set.

Stock startup zeroes `[0x15fbc,0x1669c)`, copies NVRAM toward `0x2758c`,
and on the inspected path sets SP to `0x6fff0`. Later reclamation clears
initialization ranges including `[0x1669c,0x2b6fc)` and
`[0x2b6fc,0x36a8c)`. Several pointers/instructions present in the file were
zero after startup. Therefore an offline address is not necessarily a
resident hook or valid post-start pointer.

The reference patch reservation `[0x34a8c,0x36a8c)` initially overlaps
microcode/template bytes. Even nominal linker MEMORY regions in the
reference can overlap. Actual sections, reclamation, runtime writes and
mutable storage must be checked; zero padding is never allocation evidence.
[Full layout/reclamation contract](../../experimental/cyw43/reference-contract.md).

## R0 → R1 → R2 → R3: what each milestone proved

| Stage | Actual result | What it did not prove |
| --- | --- | --- |
| [R0](../../experimental/cyw43/R0-results.md) | Stock startup, DOWN, revision and ROM inventory, exact input comparison | Custom execution |
| [R1](../../experimental/cyw43/r1/R1-results.md) | Own C diagnostic in modified vendor image; fresh challenges, counter reset, negative control, preserved active patches; mixed-role UDP regression | Vendor-free firmware or standalone ROM services |
| [R2](../../experimental/cyw43/r2/R2-results.md) | Own vectors/startup/C/mailbox after full RAM clear, no downloaded vendor assets, D11 reset-held | Wi-Fi RX/TX |
| [R3a](../../experimental/cyw43/r3/R3a-results.md) | Deliberate controller fault/stall and independent recovery with D11 reset-held | Safe recovery from active PHY/PSM/DMA |
| [R3 staging](../../experimental/cyw43/r3/rx-memory/README.md) | Own empty RX ring, one staged buffer, independent RP byte readback | DMA publication or a received frame |
| [R3 components](../../experimental/cyw43/r3/README.md) | Exact-code contracts and checked PHY/table/calibration/DMA modules | A linked, complete cold-start receiver |
| [R3 guarded startup](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md) | Source PHYPLL activation, active timing/table gates and repeated full RX-focused calibration composition; success-only cleanup and independent recovery | Persistent calibrated receiver lifecycle, active DMA, fresh source frames or RF-silence proof |
| [R3 calibrated-state handoff](calibration.md#bounded-calibrated-state-handoff-verified---2026-09-21) | Real source consumer after private-stack return, original BSS-owned objects,267/268 nominal SOF frames with eight hold checks and21-register retention, explicit stop/revoke and cleanup; verified twice | Long-lived active receiver lifecycle, whole-hardware/analog preservation, RX or continuous RF silence |

<a id="current-execution-lineages---2026-09-22"></a>
### Execution lineages - reviewed 2026-10-08

The original freestanding C **cold-source** path now completes startup and
calibration in its implemented RX-focused scope. After the separately
verified quiescent handoff, later source MAC/PSM activation reaches the
bounded negative receive experiments. No fresh source-only frame or DMA
publication has been demonstrated. The [current status](README.md#current-status)
distinguishes the latest condition-36 fault from event-14 wait; a single
"no-event" explanation does not cover every image. This is not the frozen
reset-held source5840 image.

The **vendor-retained** comparison first runs pinned vendor firmware and
validates vendor frames, then stops MAC/PSM and candidate DMA and replaces
the PSM program with original source code. Its committed format4 snapshots
pass checks on six CCK PLCP bytes; those prefixes are not full frames or fresh-on-air
attribution. Vendor-established PHY/FIFO/history remains part of the lineage.
Three checked prefixes coexist with the six cold candidate-inhibit readbacks;
upstream-FIFO retention, TX silence, SafeRX and the cold cause remain unproved.

The historical inhibited-prefix image is RP `e17ad806` / controller `1223f20d`, receiver
report v3 / catalogue v2. Source PSM `e6947a2c` is unchanged. Its fixed cases
are `vendor-rxe`, `source-prefix` and `source-prefix-inhibited`; it is not an
arbitrary payload or register interpreter. The cold UF2 was **not rebuilt or
requalified** by these comparisons. Exact image hashes and historical
report/manifests are indexed in the
[central ledger](../../experimental/cyw43/r3/references.json), especially
`follow_up.safe_rx_inhibit_comparison_2026_09_22`.
The separate corrected **fresh-frame** profile is RP `f49aea13` / controller
`24da697f` / PSM `73fd0ced` (3,144 bytes, ABI7). It captured three consecutive
complete, post-ready challenge-bound 88-byte MPDUs with checked FCS and clean
no-burst/old-challenge controls. Its
[evidence index](../../experimental/cyw43/r3/rxe-observe/runs/reliable-one-shot-2026-09-22.json)
does exclude stale FIFO bytes for those accepted frames, but not vendor
initialization, general receiver limitations or unmeasured RF behavior.
Use the [current handoff](handoff.md) for explicit experimental opt-in and
identity-bound activation gates; RF qualification is an owner-deferred
separate track, not a passed safety claim.

### R1 placement and runtime

Final R1 controller tag `86e3247f` retains the 224,192-byte image length;
modified SHA256:
`20b96f0d41868537ecded43ae63099398d391195971ff5863f8ebbff090fd0d3`.
Compressed microcode occupies 29,315 bytes from `0x2b87c`;
own code/constants 1,322 bytes from `0x34a8c`; protected zero-initialized
state/history 4,124 bytes from `0x34fb8`. The original template at `0x3644c`
is retained. No new low/high flash-patch allocation is assumed.

The diagnostic forwards all five original IOCTL arguments for unrelated
commands. Stock rejects the new request (`-23`); malformed length/CRC
returns `-2` without incrementing the counter. Code/template readbacks,
expanded microcode CRC and unchanged active flash-patch bytes distinguish
execution from a fabricated host response. Vendor runtime, ROM, microcode,
NVRAM and CLM remain dependencies.

The later **pre-RC vendor-reference** variants use a distinct checked
controller start at`0x32c00`, consuming spare storage after the same
29,315-byte compressed PSM image. The original template is still preserved;
this does not enlarge the baseline R1 allocation or declare arbitrary
padding free. Exact code/data/stack bounds and patch sites are checked for
each image.

Those variants relocate pinned vendor PHY/radio/RC bodies for observation
and controlled contrasts. They deliberately stop at the RC boundary instead
of completing UP. Their successful measurements are not project-owned
calibration code running without vendor services.
[Reference lease and profiles](../../experimental/cyw43/r3/live-queue/README.md).

### R2 entry and source-owned layout

The controlled reset-entry bridge observed RAM word 0 supplying MSP:
zero gave MSP zero; `0x6fff0` gave that exact MSP. Word 1 redirected entry.
Both observed VTOR/CONTROL zero, LR `0xffffffff`, one entry and the CPUID
above. This establishes the tested **host-controlled core-reset interface**,
not every action of immutable ROM at power-on.

The standalone program supplies valid vectors and startup, with no ROM
service calls:

| Owned area | Purpose |
| --- | --- |
| `0x0000..0x00ff` | 256-byte vector table |
| Code/data from `0x1000` | Own reset/fault handlers, C and mailbox routines |
| `0x8000..0x807f` | 128-byte mailbox |
| `0xfc00..0xffff` | One-KiB stack; initial SP `0x10000` |

Maskable interrupts remain disabled. Startup clears deliberately poisoned
BSS. The loader clears/compares all 512 KiB on **each** of two cycles,
verifies a held-reset negative control, fresh responses, heartbeat,
program immutability and no unexpected nonzero bytes outside owned regions.
The mailbox commits request/response sequences last, with checksums and
barriers; it is not a cryptographic authentication scheme.

<a id="current-source-payload-is-small-because-its-function-is-small"></a>
## Frozen source5840 recovery image

These are **CYW flat binaries**, not ELF sizes or RP2350 UF2 sizes.

| Image | Bytes | Contents / status |
| --- | ---: | --- |
| R2 standalone | 4,836 | 996 vector/code/data bytes plus four BSS bytes; binary includes gap |
| R3 control | 4,840 | Reset-held diagnostic |
| RX memory-only | 5,324 | 1,484 loaded section bytes, 2,452 BSS bytes |
| Memory + counter (`CLOCK=1`) | **5,840** | 1,998 loaded section bytes, 2,472 BSS bytes |

The immutable memory/counter **recovery** image SHA256 is
`0fbd8c335e45cebd3b8a03728c17bf9f7bfff6b2d53774a14be882a00327461b`
(controller build `3ddd0935`). Its **2,448-byte staging area** contains an
empty 32-slot ring, empty packet array, one 1,716-byte buffer and a separate
unpublished descriptor. `prepared=1` is not `published=1`.

The new DMA component's proposed 64-KiB arena is **not** an ownership
exception in this image. Most research libraries are not linked into it.
Comparing 5,840 with 224,192 bytes is not a feature-equivalent size saving.
Source ownership of executable logic also does not establish independent
design or unrestricted rights in target-derived configuration fixtures.

Larger source startup images are separate artifacts with their own manifests,
owned RAM/stack and matching qualification proofs. They execute original C
and read explicitly pinned ROM configuration data; they do not call the
vendor initializer. Their existence must not change the frozen5,840-byte
recovery image or turn its reset-held proof into a startup proof.
Single-trial operation restores and independently executes the baseline
before final BOOTSEL recovery. The bounded vendor-retained batch workflow
instead performs that independent recovery once at the batch boundary or
first failure, never claiming it per trial. Batches allow at most four trials
and 120 active host seconds; each trial still has a fresh RP boot, complete
radio initialization and fresh challenge. Recovery overwrites the candidate.
[Current batch policy](workflow.md#bounded-resident-research-batches).

### Frozen recovery assets

All three files must accompany a testing handoff. These are immutable inputs,
not outputs to rebuild from today's sources. The runtime pins are owned by
[host_support.py](../../experimental/cyw43/r3/pmu-startup/host_support.py).
Missing files or a mismatch block hardware testing; do not repin a replacement.

| Asset | SHA256 |
| --- | --- |
| [CYW program.bin](../../experimental/cyw43/r3/rx-memory/build/program.bin) | `0fbd8c335e45cebd3b8a03728c17bf9f7bfff6b2d53774a14be882a00327461b` |
| [RP rx_memory.uf2](../../experimental/cyw43/r3/rx-memory/build/rx_memory.uf2) | `a5844d1782e6c807bb924e87ff63a1f7346c07ef498e47a2dc288a78e75908c8` |
| [Pinned host collector](../../experimental/cyw43/r3/rx-memory/build/host) | `680e52d31f40781b99b62c4131c7fe22f3dab14fd6d63dd7c145c5aa39913676` |

The collector is a host executable: a different OS/architecture may not run
it. That is a recovery/tooling qualification blocker, not permission to
silently rebuild it. The current tested environment is macOS.

The manufacturer architecture describes embedded ARM software/HNDRTE
and D11 microcode dividing device work. That is the **vendor runtime**
boundary, not a runtime linked into our standalone C program. The
[reviewed datasheet passages](../../experimental/cyw43/r3/init/activation-evidence.md)
are architectural evidence, not a complete vendor-runtime reconstruction.
