# PHY, radio and table contracts

[Guide](README.md) · [Calibration](calibration.md) · [Receive path](rx-path.md)

**These are resolved interfaces, not a qualified complete cold-start
receiver.** Separate original-C source images now complete startup and
calibration in the implemented RX-focused scope, followed by a verified
bounded quiescent handoff. Later cold MAC/PSM activation still has no accepted
fresh frame or source DMA publication. The [current status](README.md#current-status)
distinguishes the later condition-36 fault from event-14 wait; the selected
register comparison below does not establish either failure's cause.
The frozen source5840 memory/counter payload does not execute these stages.
Addresses below refer to the pinned target; related-source register names
are interpretation evidence, not independent electrical qualification.

## Direct pre-fault cold RXE state - 2026-10-09

The [explicit HOF8 source diagnostic](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#opt-in-pre-fault-rxe-diagnostic---2026-10-09)
now samples RXCTL `18001408`, CORECTL `180014f0`, length `18001412` and
status `18001418`, bracketed by zero sticky primary-fault reads.
Unarmed no-AP/AP controls both observe `0800/f000/0000/0000` before PSM
and `0800/f020/0000/0000` immediately after MAC. These are two sequential
20-I/O samples, not atomic state or RF evidence; both controls expire the
unchanged controller budget without a frame.

The post-MAC RXCTL/CORECTL pair matches two retained positives' earlier
pre-takeover observations; the third retained CORECTL was `f0a0`.
Retained length/status and sampling phases differ. Do not substitute
retained values into cold initialization or interpret previously captured
transitional fault-time zeros as the pre-fault RXE state.
Verified receive-window stimulus overlap remains missing.

## Selected cold versus vendor-retained state - 2026-09-22

A bounded snapshot after checked MAC/PSM pause and candidate DMA stopping,
before source PSM loading, compares 21 live vendor-retained PHY/radio getters
with historical cold no-event run `8az0im44`. All selected values match except:

| Register | Historical cold | Working vendor-retained | Scope |
| --- | --- | --- | --- |
| PHY `44c` | `4200` | `0200` | Candidate TX-chain override-enable `4000` |
| PHY `938` | `0800` | `0000` | Candidate internal-PA override-enable `0800` |
| Radio `a4` | `0004` | `0000` | Candidate radio-PA override-enable `0004` |
| Radio `92/93` | Captured calibration counts | Slightly different counts | No automatic causal or benign interpretation |

The corresponding value fields match: PHY `44d=0`, PHY `939=0` and radio
`79=7f80`; the selected RFPLL override bit/value also match. This is not
whole-hardware equivalence or a fresh cold qualification.

The explicit retained `source-prefix-inhibited` case applies the same six
tighten-only masked updates as the cold candidate policy, preserving unrelated
bits and never releasing them within a trial. Three committed format4
six-byte CCK prefixes passed with all six cold readbacks before MAC enable
and at terminal capture. One additional inhibited trial failed stale-event
drain fault3 before READY/MAC enable and aborted the remaining queue.
Every batch independently recovered source5840 and both badges to BOOTSEL.

This is **prefix/readback coexistence**, not fresh RF reception under
inhibition: retained upstream FIFO content is not excluded. It proves neither
full-frame reception, physical TX inhibition, SafeRX nor an isolated cold
cause. Source PSM `e6947a2c` and its acceptance predicates are unchanged;
no cold source image was modified, rebuilt or requalified. The
[comparison evidence](../../experimental/cyw43/r3/rxe-observe/runs/safe-rx-inhibit-comparison-2026-09-22.json)
and [current handoff](handoff.md) own exact run identities and remaining gates.

## Full-frame retained comparison - 2026-10-09

The [2026-10-09 full-frame replay](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#retained-full-frame-comparison-and-missing-live-rxe-sample---2026-10-09)
also validates three retained complete fresh challenge frames with the same
six actual pre-MAC/terminal candidate-inhibit readbacks as cold operation.
It strengthens the retained comparison beyond the prefix-only records above,
but still does not qualify cold RX, physical TX inhibition or live-state
equivalence. The selected 21-register comparison combines retained pre-guard
getters with six later guard readbacks; do not treat it as an atomic snapshot.

## Never mix the address spaces

| Quantity | Unit / example |
| --- | --- |
| ARM code address | CPU bytes, e.g. ROM helper `0x0086bf78` |
| D11 register address | MMIO bytes; observed base **`0x18001000`** |
| PHY or radio index | Indexed 16-bit register number, not `D11 base + index` |
| SHM byte offset | ARM object-access helper argument |
| SHM word offset | PSM/related RXE addressing in **16-bit words** |
| PSM PC | **Instruction words**, eight expanded-image bytes per word |
| Table entry address | Entry index interpreted with table ID and width, not ordinary RAM |

### Direct and indexed ports

| Interface | Address / D11 offset | Width and rule |
| --- | --- | --- |
| MACCONTROL | `0x18001120` / `+0x120` | 32-bit; shadow-state contract below |
| D11 clock/status | `0x180011e0` / `+0x1e0` | 32-bit; distinct from ChipCommon clock/status |
| MAC interrupt status | `+0x128` | PSM-start handshake uses bit 0 |
| Template selector/data | `+0x130` / `+0x134` | 32-bit |
| Object selector | `+0x160` | 32-bit write then readback |
| Object data low/high | `+0x164` / `+0x166` | Selected 16-bit SHM lane |
| Radio selector/data | **`0x180013d8` / `0x180013da`** | Separate 16-bit operations |
| PHY selector/data | **`0x180013fc` / `0x180013fe`** | Select16/read16; packed write32 supported |

On the RP-to-CYW function-1 path, address bit `0x8000` selects **32-bit
backplane access**, not an unconditional address decoration. Native
halfword requests must leave it clear. Physical lane/footprint tests
identified the earlier flagged-halfword error; corrected access passed
PHY and radio readbacks. This transport rule is separate from a native
CYW M3 MMIO access width.
[Qualification and retained failures](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#corrected-transport-phy-readbacks).

The exact SHM helpers select `0x10000 | (byte_offset >> 2)` at the object
selector, read it back, then use `+0x164` or `+0x166` according to offset
bit 1. Bit 0 does not select another byte lane. This establishes local
addressing, not all object-memory geometry, auto-increment or concurrent
ownership rules. See the [MAC/SHM contract](../../experimental/cyw43/r3/init/control-contract.md).

## PHY and radio operations

The PHY packed writer places the index in a 32-bit word's low half and
value in its high half, writing once to `0x180013fc`. Reads select with a
16-bit write then read `0x180013fe`. RMW performs read/modify/data-write.
MOD arithmetic is:

```text
new = (old & ~mask) | (value & mask)
```

Radio writes instead use **selector16, data16**. Radio RMW repeats the
selector after the read: selector16 → read16 → selector16 → write16.
Do not substitute the PHY packed writer.

Vendor PHY/radio read and RMW helpers clear the halfword cache at
`phy+0x88`; packed/direct writers do not. Our transport has no vendor
object cache and does not implement that object ABI. Vendor radio read
index **1** returns constant `0xffff` without MMIO; our explicit radio API
rejects that index for **every** operation before I/O.

The complete 36-byte helper at `0x8712ec` ORs `0x0100` into PHY `0x44a`,
then rereads and ANDs `0xfeff`. Related LCN20 names this bit `radioReset`.
It is not `resetCCA`, a saved-word restoration, a delay/poll routine or
the DC helper's later **bit-zero** reset. Native source execution and
sampled post-pulse PHY checks followed by fresh radio PA arming now pass.
Those checks do not establish continuous retention during reset, acceptable
analog pulse width or physical TX suppression.

Sources: [PHY access](../../experimental/cyw43/r3/phy-access/README.md),
[radio access](../../experimental/cyw43/r3/phy-access/calibration.md),
[checked C API](../../experimental/cyw43/r3/phy-access/access.h).

## Three different table formats

| Format | Encoding | Established use and caveat |
| --- | --- | --- |
| D11 init records | LE `{u16 byte_offset, u16 width, u32 value}`; eight-byte stride; offset `0xffff` terminates | ROM writer `0x84dc88`, register base from context `+0x88`; widths 2/4 only accepted by inspector |
| PHY/radio register scripts | u16 stream; low 13 header bits index, upper three operation; MOD takes **three u16 words**, others two | Operation-word counts are not record counts; full prevalidation rejects truncation |
| PHY hardware tables | Five 32-bit descriptor fields: pointer, count, ID, entry address, width | Separate table ports and transfer ordering below |

Init Table A is `[0x13e94,0x14f6c)`: **538 writes**, 94 halfword and
444 word writes, plus sentinel. Table B is `[0x14f6c,0x1508c)`:
**35 writes**, 20 halfword and 15 word writes, plus sentinel. Exact call/
literal bindings and a 48-byte reset-held ROM capture establish the writer.
It silently skips unsupported widths; our inspector rejects them.

Table A includes **360 object-selector/data writes**, **75 template-data
writes** and MACCOMMAND `4`. Table B includes 30 object-selector/data-halfword
writes. Reordering, collapsing duplicate offsets or replaying only “final
values” destroys an ordered protocol. The
[ordered footprint analysis](../../experimental/cyw43/r3/init/ordered-analysis.md)
is conditional, not authorization to replay.

Register-script decoding can describe radio operations, but the existing
PHY/table execution API still rejects them. The separate explicit radio
API does not silently increase the authority of script callers.

### Hardware tables: exact target interface

| Indexed PHY register | Meaning |
| --- | --- |
| `0x00d` | Table ID |
| `0x00e` | Entry address |
| `0x456` | Low data halfword |
| `0x457` | High data halfword |

This path does **not** pack ID/address into the older combined `0x455`
interface. The checked API supports:

- 8/16-bit elements through low data.
- 32-bit reads **low then high**, writes **high then low**.
- Save PHY `0x9a5`, set mask `0x0002`, transfer, restore the complete word.
- For ID **33**, also save `0x574/0x575`, set masks `0x4000/0x0400`,
  then restore those words in order before `0x9a5`.

Vendor engines have 48/60/64-bit paths; the source API deliberately rejects
them. It uses explicit-capacity **uint32_t arrays for every supported width**,
zero-extending narrow reads and prevalidating narrow writes. Count is
1–256; 16-bit ID/address bounds prevent encoding overflow but do not certify
that every such table entry physically exists.

Partial I/O or restore failure requires independent reset; no guessed
rollback. “Entries completed” can equal count even when later restoration
fails. Synthetic port-cell tests do not prove real table advancement.
[Table API and exact-code comparisons](../../experimental/cyw43/r3/phy-tables/README.md).

For the tested bound profile, table 20's 256 logical addresses behave as
**64-cell aliasing**. The source verifier preserves all 256 writes and
checks the last-write-wins image, `data[192 + (index & 63)]`, at every
logical address. All 908 common/gain comparisons and both selected defaults
then pass before RC. This is a measured profile-specific mapping, not
universal table capacity or permission to deduplicate the script. Recheck
the mapping if the clock/resource profile changes.
[Physical mapping evidence](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#physical-mapping-result).

## Clock and core control are separate interfaces

| Address | Meaning and interpretation |
| --- | --- |
| `180001e0` | ChipCommon clock/control status |
| `180011e0` | D11 clock/control status; same core-relative offset, different instance |
| `18101408` | D11 wrapper IOCTRL: PHY/reset/band/clock-control flags |
| `18101800` | D11 wrapper core-reset control |
| `18001120` | MACCONTROL, including independent MAC_ENABLE and PSM_RUN bits |

The guarded source clock/band profile changes IOCTRL`0045 -> 2055` using
MACPHY clock-control enable`0010` and GMODE`2000`. GMODE mirrors into
MACCONTROL bit`80000000`: the stopped source value becomes`84000404`,
not`04000404`. This is an observed hardware side effect, not a MAC-enable
write. Generic force-gated-clock bit`0002` is a different wrapper control.

Published clock definitions distinguish FORCEHT`00000002`, external
request mask`00000700`, and read-only external-status mask`07000000`.
They do not prove identical PMU routing for every core's external request0.
Never write a complete observed status word merely to manufacture
availability.

The working reference observes D11`070b0042`; clearing its FORCEHT bit can
leave`070b0040` while another requester retains availability. The earlier
source core-preparation probe read`050b0040`; two pre-activation CRC-valid
source snapshots confirmed the same value after guarded radio power.
Moving the source MIN30 request to that boundary still does not activate
PHY availability within the tested bound. Controlled vendor PMU resource
removal/restoration toggles`050b0040 /070b0040`, the differing`02000000`
availability indication and RC completion.
[Causal experiment and limits](calibration.md#current-hardware-status-and-causal-rc-dependency).

The later source-owned **PHYPLL request**, unlike FORCEHT, writes low
controls`0x240` to D11`180011e0`. Two identical-image runs observe
`050b0240 ->070b0240` and resources`3eca3df7 ->7efa3df7`, without changing
PMU_MIN or SR. This is a bounded activation result with all inhibits retained,
not full calibration/RX or universal resource routing.
[Source milestone](calibration.md#source-phypll-activation-observed---2026-09-21).
The later [calibration and bounded handoff](handoff.md#3-authoritative-verified-checkpoint)
passed separately; neither establishes source reception or universal routing.

## Initialization dispatch and MAC state

The stock path is not intrinsically receive-only:

```text
outer 0x84ea94 -> RAM-dispatched core init 0xcee0
  -> MAC shadow 0x04000404
  -> PSM start request 0x04020402 (PSM_RUN, not MAC_ENABLE)
  -> Table A
  -> present TX DMA queues, then RX initialization/refill
  -> optional mute later
  -> later PHY/band work
```

The PSM helper can return failure; the stock caller at `0xcf18` does not
check it. Optional mute is not a startup interlock.
[Exact order and consequences](../../experimental/cyw43/r3/init-path/README.md).

MAC shadow at `hw+0xec` is changed with `(old & ~mask) | value`:
**this setter does not mask `value`**, unlike PHY MOD. The emitter can set
WAKE `0x04000000` from `hw+0xf4`, and clear AP `0x00040000`/set INFRA
`0x00020000` from `hw+0xf8`. An unchanged shadow can skip a hardware write.
Direct MMIO clears are not persistent while these writers remain active.

The early PHY callback at `phy+0xa4c` is now bound to Thumb pointer
`0x874a11`; captured `0x874a10` is a no-op. The later dispatcher
`0x86d480` uses `phy+0xa48` → `0x874b55`, a real target initializer.
The related `+0xa50` points to `0x874a15`; the initializer also calls it
directly. Missing callbacks or diagnostic branches do not guarantee
failure propagation. These resolved bindings replace older “unknown
callback” descriptions, not independent RF qualification, which remains
owner-deferred separately from authorized bounded receive experiments.
[PHY boundary evidence](../../experimental/cyw43/r3/phy-boundaries/README.md).

The source now also loads the fixed board/TIA/gain data from its pinned ROM
span, binds the observed 37.4-MHz profile, and executes the board-conditional
four-update GCI notification with readbacks. This is original C using
explicit data, not a vendor service call. The source notification's
post-reset placement is not claimed equivalent to the entire vendor
dispatcher order, and notification alone did not fix RC completion.
[Startup binding and GCI evidence](../../experimental/cyw43/r3/phy-tables/radio-cold-start.md).

## Receive-specific channel blocks

Exact stock constructor `0x25cdc` chooses fourteen **66-byte channel rows**,
each containing channel/frequency plus 31 radio words:

| Oscillator field, **Hz** | Stock row-table address |
| ---: | --- |
| 37,400,000 | `0x2a082` |
| 26,000,000 | `0x2a428` |
| 19,200,000 | `0x2a8b8` |

Channel frequency is `2407 + 5*channel` MHz for 1–13; channel 14 is
2484 MHz. This is data interpretation, not regulatory permission.
The tuner writes all 31 radio words and performs VCO work; an allocated
row pointer is not proof of tuning.

The source RX timing planner reproduces all fourteen target cases using
integer arithmetic, with `f` in **MHz**:

```text
farrow_mu    = (f * 65536 + 2) / 5
drift_period = 1280 / gcd(1280, f)
center_ts20  = ((f * 4 / 5) + 1) / 2
center_factor = ((5242880 / f) + 1) / 2
```

It applies seven masked PHY updates: `0x9ad/0xffff` (low Farrow),
`0x9ae/0x03ff` (high Farrow), `0x9ae/0x4000` (clear fast ADC),
`0x9b1/0x1fff` (drift), `0x9ac/0x0400` (enable drift),
`0x657/0x0fff` and `0x658/0x0fff` (centers).
Channel 1 yields `31614566`, `320`, `965`, `1087`.

RX RFSEQ is table **8**, width **16**, nineteen entries at offset 3 and
three at offset 29. The latter are **60, 1, 8**; the related source's
turnaround 12 is not the target's 60. No physical time unit is established.
Static RX gain tables are **17/8-bit/26 entries**, **18/32-bit/96 entries**,
**13/32-bit/76 entries**, written in that order. These are distinct from
PAPD setup and TX gain/power-index work.

Common table IDs 2/1/20/23/0 and broader RX controls remain dependencies,
not proven unnecessary. Captured gains are explicit fixtures/caller data,
not covert vendor code linked into the baseline.
[RX configuration and target differences](../../experimental/cyw43/r3/phy-tables/rx-config.md).
