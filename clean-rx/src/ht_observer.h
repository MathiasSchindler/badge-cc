#ifndef CLEAN_HT_OBSERVER_H
#define CLEAN_HT_OBSERVER_H

#ifndef CLEAN_RX_HT_OBSERVER
#define CLEAN_RX_HT_OBSERVER 0
#endif
#ifndef CLEAN_RX_HT_EARLY_SAVE
#define CLEAN_RX_HT_EARLY_SAVE 0
#endif
#ifndef CLEAN_RX_HT_ADVANCE
#define CLEAN_RX_HT_ADVANCE 0
#endif
#ifndef CLEAN_RX_HT_NEXT_FIELD
#define CLEAN_RX_HT_NEXT_FIELD 0x83u
#endif
_Static_assert(CLEAN_RX_HT_NEXT_FIELD == 0x83u || CLEAN_RX_HT_NEXT_FIELD == 0xc3u,
    "Next-MPDU command must select the reference or continuation field");
#ifndef CLEAN_RX_HT_PHY_SNAPSHOT
#define CLEAN_RX_HT_PHY_SNAPSHOT 0
#endif
#ifndef CLEAN_RX_HT_WINDOW
#define CLEAN_RX_HT_WINDOW 0
#endif
#ifndef CLEAN_RX_HT_REARM_WAIT
#define CLEAN_RX_HT_REARM_WAIT 0
#endif
#ifndef CLEAN_RX_HT_REARM_RECOVER
#define CLEAN_RX_HT_REARM_RECOVER 0
#endif
#ifndef CLEAN_RX_HT_REARM_INJECT
#define CLEAN_RX_HT_REARM_INJECT 0
#endif
#ifndef CLEAN_RX_HT_REARM_RESET
#define CLEAN_RX_HT_REARM_RESET 0
#endif
#ifndef CLEAN_RX_HT_REARM_PARK
#define CLEAN_RX_HT_REARM_PARK 0
#endif
#ifndef CLEAN_RX_HT_OBSERVER_FIFO
#define CLEAN_RX_HT_OBSERVER_FIFO 0
#endif
#ifndef CLEAN_RX_HT_EARLY_FIFO
#define CLEAN_RX_HT_EARLY_FIFO 0
#endif
_Static_assert(CLEAN_RX_HT_EARLY_FIFO == 0 || CLEAN_RX_HT_EARLY_FIFO == 1,
    "Early FIFO gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_EARLY_FIFO ||
    (CLEAN_RX_HT_EARLY_SAVE && CLEAN_RX_HT_OBSERVER_FIFO && !CLEAN_RX_HT_ADVANCE),
    "Early FIFO requires early retention and passive FIFO without continuation advance");
#ifndef CLEAN_RX_HT_COPY_PROBE
#define CLEAN_RX_HT_COPY_PROBE 0
#endif
#ifndef CLEAN_RX_HT_NEXT_STREAM
#define CLEAN_RX_HT_NEXT_STREAM 0
#endif
#ifndef CLEAN_RX_HT_SHM_ONLY
#define CLEAN_RX_HT_SHM_ONLY 0
#endif
#ifndef CLEAN_RX_HT_SHORT_STREAM
#define CLEAN_RX_HT_SHORT_STREAM 0
#endif
#ifndef CLEAN_RX_HT_DELIMITER_GUARD
#define CLEAN_RX_HT_DELIMITER_GUARD 0
#endif
_Static_assert(CLEAN_RX_HT_SHORT_STREAM == 0 || CLEAN_RX_HT_SHORT_STREAM == 1,
    "Short-first streaming gate must be zero or one");
_Static_assert(CLEAN_RX_HT_DELIMITER_GUARD == 0 || CLEAN_RX_HT_DELIMITER_GUARD == 1,
    "Delimiter status guard must be zero or one");
_Static_assert(!CLEAN_RX_HT_SHORT_STREAM ||
    (CLEAN_RX_HT_VARIABLE && CLEAN_RX_HT_ADVANCE && CLEAN_RX_HT_COPY_PROBE &&
     CLEAN_RX_HT_OBSERVER_FIFO && !CLEAN_RX_HT_SHM_ONLY &&
     !CLEAN_RX_HT_VARIABLE_STREAM64 && !CLEAN_RX_HT_COPY_DEFER_STREAM &&
     !CLEAN_RX_HT_NEXT_STREAM),
    "Short-first streaming requires the unmodified variable handoff diagnostic");
_Static_assert(!CLEAN_RX_HT_DELIMITER_GUARD ||
    (CLEAN_RX_HT_VARIABLE && CLEAN_RX_HT_ADVANCE && CLEAN_RX_HT_COPY_PROBE),
    "Delimiter status guard requires the variable handoff diagnostic");
_Static_assert(CLEAN_RX_HT_SHM_ONLY == 0 || CLEAN_RX_HT_SHM_ONLY == 1,
    "SHM-only capture diagnostic must be zero or one");
_Static_assert(!CLEAN_RX_HT_SHM_ONLY ||
    (CLEAN_RX_HT_VARIABLE && CLEAN_RX_HT_ADVANCE && CLEAN_RX_HT_COPY_PROBE &&
     CLEAN_RX_HT_OBSERVER_FIFO && !CLEAN_RX_HT_NEXT_STREAM &&
     !CLEAN_RX_HT_VARIABLE_STREAM64 && !CLEAN_RX_HT_COPY_DEFER_STREAM),
    "SHM-only capture requires the unmodified variable handoff/FIFO diagnostic");
_Static_assert(CLEAN_RX_HT_NEXT_STREAM >= 0 && CLEAN_RX_HT_NEXT_STREAM <= 2,
    "Continuation stream probe must be off, before command, or after command");
_Static_assert(CLEAN_RX_HT_NEXT_FIELD == 0x83u ||
    (CLEAN_RX_HT_ADVANCE && CLEAN_RX_HT_COPY_PROBE),
    "Modified next-MPDU command requires the bounded handoff copy diagnostic");
#ifndef CLEAN_RX_HT_COPY_DMA_PROBE
#define CLEAN_RX_HT_COPY_DMA_PROBE 0
#endif
_Static_assert(CLEAN_RX_HT_COPY_DMA_PROBE == 0 || CLEAN_RX_HT_COPY_DMA_PROBE == 1,
    "Completed DMA probe gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_COPY_DMA_PROBE ||
    (CLEAN_RX_HT_COPY_PROBE && CLEAN_RX_HT_OBSERVER_FIFO),
    "Completed DMA probe requires the FIFO-backed handoff copy probe");
#ifndef CLEAN_RX_HT_COPY_WAIT
#define CLEAN_RX_HT_COPY_WAIT 0
#endif
#ifndef CLEAN_RX_HT_COPY_END_PLUS6
#define CLEAN_RX_HT_COPY_END_PLUS6 0
#endif
#ifndef CLEAN_RX_HT_COPY_QUIET
#define CLEAN_RX_HT_COPY_QUIET 0
#endif
_Static_assert(!CLEAN_RX_HT_NEXT_STREAM ||
    (CLEAN_RX_HT_VARIABLE && CLEAN_RX_HT_ADVANCE && CLEAN_RX_HT_COPY_PROBE &&
     CLEAN_RX_HT_NEXT_FIELD == 0xc3u && !CLEAN_RX_HT_COPY_QUIET),
    "Continuation stream probe requires the variable c3 handoff diagnostic");
_Static_assert(!CLEAN_RX_HT_COPY_DEFER_STREAM ||
    (CLEAN_RX_HT_COPY_PROBE && !CLEAN_RX_HT_COPY_WAIT && !CLEAN_RX_HT_COPY_END_PLUS6 &&
     !CLEAN_RX_HT_COPY_QUIET && !CLEAN_RX_HT_HANDOFF_GUARD),
    "Deferred streaming requires the unmodified two-body non-wait copy probe");
_Static_assert(!CLEAN_RX_HT_COPY_STREAM_PHASE ||
    (CLEAN_RX_HT_COPY_DEFER_STREAM && CLEAN_RX_HT_COPY_PROBE),
    "Streaming phase control requires the deferred non-wait copy probe");
#ifndef CLEAN_RX_HT_COPY_STREAM_REARM
#define CLEAN_RX_HT_COPY_STREAM_REARM 0
#endif
#ifndef CLEAN_RX_HT_COPY_SCOPE
#define CLEAN_RX_HT_COPY_SCOPE 0
#endif
#ifndef CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY
#define CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY 0
#endif
#ifndef CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD
#define CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD 0x83u
#endif
_Static_assert(CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD == 0x83u ||
    CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD == 0x82u ||
    CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD == 0x81u ||
    CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD == 0x03u,
    "Copy scope replay field must retain or omit one existing command bit");
_Static_assert(CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD == 0x83u ||
    (CLEAN_RX_HT_COPY_SCOPE == 3 && !CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY &&
     (CLEAN_RX_HT_COPY_STREAM_REARM == 5 ||
      (CLEAN_RX_HT_COPY_STREAM_REARM == 6 && CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD == 0x81u))),
    "Modified replay field requires the epoch-held field or bit7 destination discriminator");
_Static_assert(CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY == 0 || CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY == 1,
    "Copy scope replay suppression must be zero or one");
_Static_assert(!CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY ||
    (CLEAN_RX_HT_COPY_SCOPE == 3 && CLEAN_RX_HT_COPY_STREAM_REARM == 5),
    "Replay suppression requires the epoch-held original-destination scope");
_Static_assert(CLEAN_RX_HT_COPY_SCOPE >= 0 && CLEAN_RX_HT_COPY_SCOPE <= 4,
    "Copy scope must select a supported off, stream or epoch-held profile");
_Static_assert(!CLEAN_RX_HT_COPY_SCOPE || CLEAN_RX_HT_COPY_STREAM_REARM == 6 ||
    (CLEAN_RX_HT_COPY_SCOPE == 3 && CLEAN_RX_HT_COPY_STREAM_REARM == 5),
    "Copy scope requires fixed destination-shift replay or epoch-held original destination");
_Static_assert(CLEAN_RX_HT_COPY_STREAM_REARM >= 0 && CLEAN_RX_HT_COPY_STREAM_REARM <= 8,
    "Streaming copy rearm must select a supported off, immediate, post-stall, destination or endpoint probe");
_Static_assert(!CLEAN_RX_HT_COPY_STREAM_REARM || CLEAN_RX_HT_COPY_STREAM_PHASE == 2,
    "Streaming copy rearm requires the resume128 copy probe");
#define CLEAN_RX_HT_COPY_STREAM_STALL (CLEAN_RX_HT_COPY_STREAM_REARM >= 3)
#define CLEAN_STREAM_STALL_COUNT 134u
#define CLEAN_STREAM_STALL_US 32u
#define CLEAN_STREAM_STALL_BUDGET 1023u
#define CLEAN_STREAM_DEST_WORDS 64u
#define CLEAN_STREAM_DEST_PROBE (CLEAN_RX_HT_COPY_STREAM_REARM >= 5)
#define CLEAN_STREAM_DEST_SHIFT (CLEAN_RX_HT_COPY_STREAM_REARM >= 6)
#define CLEAN_STREAM_END_PROBE (CLEAN_RX_HT_COPY_STREAM_REARM >= 7)
#define CLEAN_STREAM_END_BYTES 128u
#if CLEAN_RX_HT_COPY_STREAM_REARM || CLEAN_RX_HT_COPY_STREAM_PHASE
#include "diagnostic/psm_observer_stream.h"
#else
#define OBSERVER_STREAM_END_GUARD(X)
#define OBSERVER_STREAM_END_PROGRAM(X)
#define OBSERVER_STREAM_END_REASON(X)
#define OBSERVER_STREAM_PHASE_DISPATCH(X)
#define OBSERVER_STREAM_PHASE_PROGRAM(X)
#define OBSERVER_STREAM_PHASE_RESET(X)
#define OBSERVER_STREAM_PHASE_TIMEOUT(X)
#define OBSERVER_STREAM_REARM_AFTER(X)
#define OBSERVER_STREAM_REARM_BEFORE(X)
#define OBSERVER_STREAM_STALL_PROGRAM(X)
#endif

_Static_assert(CLEAN_RX_HT_COPY_QUIET == 0 || CLEAN_RX_HT_COPY_QUIET == 1,
    "Receive streaming boundary control must be zero or one");
_Static_assert(!CLEAN_RX_HT_COPY_QUIET ||
    (CLEAN_RX_HT_COPY_PROBE && !CLEAN_RX_HT_COPY_WAIT && !CLEAN_RX_HT_COPY_END_PLUS6),
    "Streaming boundary control requires the unmodified-end non-wait probe");
_Static_assert(CLEAN_RX_HT_COPY_END_PLUS6 == 0 || CLEAN_RX_HT_COPY_END_PLUS6 == 1,
    "Receive-copy end control must be zero or one");
_Static_assert(!CLEAN_RX_HT_COPY_END_PLUS6 || (CLEAN_RX_HT_COPY_PROBE && !CLEAN_RX_HT_COPY_WAIT),
    "Receive-copy end control requires the non-wait copy probe");
_Static_assert(CLEAN_RX_HT_COPY_WAIT == 0 || CLEAN_RX_HT_COPY_WAIT == 1,
    "Fresh receive-count wait gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_COPY_WAIT || CLEAN_RX_HT_COPY_PROBE,
    "Fresh receive-count wait requires the one-shot copy probe");
_Static_assert(CLEAN_RX_HT_COPY_PROBE == 0 || CLEAN_RX_HT_COPY_PROBE == 1,
    "Receive-copy probe gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_COPY_PROBE || CLEAN_RX_HT_ADVANCE,
    "Receive-copy probe requires the passive one-shot advance");
_Static_assert(CLEAN_RX_HT_OBSERVER_FIFO == 0 || CLEAN_RX_HT_OBSERVER_FIFO == 1,
    "Observer FIFO gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_OBSERVER_FIFO || CLEAN_RX_HT_OBSERVER,
    "Observer FIFO requires the passive observer");

#define CLEAN_OBSERVER_PHY_ADDR 0x203e0u
#define CLEAN_OBSERVER_PHY_MAGIC 0x31594850u
#define CLEAN_OBSERVER_PHY_REGS 6u
#define CLEAN_OBSERVER_PHY_BYTES (8u+4u*CLEAN_OBSERVER_PHY_REGS)
#define CLEAN_OBSERVER_PHY_INDICES {0x440u,0x44au,0x4b0u,0x43au,0x4b1u,0x439u}
_Static_assert(CLEAN_RX_HT_PHY_SNAPSHOT == 0 || CLEAN_RX_HT_PHY_SNAPSHOT == 1,
    "Stopped PHY snapshot gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_PHY_SNAPSHOT || CLEAN_RX_HT_OBSERVER,
    "Stopped PHY snapshots require the passive observer");
_Static_assert(CLEAN_RX_HT_WINDOW == 0 || CLEAN_RX_HT_WINDOW == 1,
    "Receive timeline gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_WINDOW ||
    (CLEAN_RX_HT_OBSERVER && CLEAN_RX_HT_EARLY_SAVE &&
     !CLEAN_RX_HT_ADVANCE && !CLEAN_RX_HT_PHY_SNAPSHOT),
    "Receive timeline requires the early-only observer and owns the PHY snapshot gap");
_Static_assert(CLEAN_RX_HT_REARM_WAIT == 0 || CLEAN_RX_HT_REARM_WAIT == 1,
    "Receive rearm wait gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_REARM_WAIT || CLEAN_RX_HT_WINDOW,
    "Receive rearm wait control requires the passive receive timeline");
_Static_assert(CLEAN_RX_HT_REARM_RECOVER == 0 || CLEAN_RX_HT_REARM_RECOVER == 1,
    "Rearm recovery gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_REARM_RECOVER || CLEAN_RX_HT_REARM_WAIT,
    "One-shot recovery requires the strict passive rearm control");
_Static_assert(CLEAN_RX_HT_REARM_INJECT == 0 || CLEAN_RX_HT_REARM_INJECT == 1,
    "Rearm injection gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_REARM_INJECT || CLEAN_RX_HT_REARM_RECOVER,
    "Injected recovery requires the one-shot recovery diagnostic");
_Static_assert(CLEAN_RX_HT_REARM_RESET == 0 || CLEAN_RX_HT_REARM_RESET == 1,
    "PHY reset gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_REARM_RESET || CLEAN_RX_HT_REARM_RECOVER,
    "PHY reset requires the one-shot passive recovery diagnostic");
_Static_assert(CLEAN_RX_HT_REARM_PARK == 0 || CLEAN_RX_HT_REARM_PARK == 1,
    "Park-only control gate must be zero or one");
_Static_assert(!CLEAN_RX_HT_REARM_PARK || CLEAN_RX_HT_REARM_RESET,
    "Park-only control requires the bounded reset handoff");

#define CLEAN_OBSERVER_RESET_SHM 0x7d0u
#include "rx_recovery.h"
#define CLEAN_OBSERVER_RX_RESET_OPS(X) \
    X(0xa00u,0xffffu,0u) X(0x440u,1u,0u) X(0x44au,0x44u,0x44u) \
    X(0x44au,0xffffu,0x80u) X(0xa00u,0xffffu,0xffu) X(0x440u,1u,1u)

#include "psm/observer_rearm.h"

#define CLEAN_OBSERVER_SHM 0x100u
#define CLEAN_OBSERVER_HEADER_WORDS 16u
#define CLEAN_OBSERVER_RECORD_WORDS 47u
#define CLEAN_OBSERVER_RECORDS 16u
#define CLEAN_OBSERVER_PENDING (CLEAN_OBSERVER_SHM + 7u)
#define CLEAN_OBSERVER_HELD (CLEAN_OBSERVER_SHM + 8u)
#define CLEAN_OBSERVER_EPOCH (CLEAN_OBSERVER_SHM + 9u)
#define CLEAN_OBSERVER_OUTCOME (CLEAN_OBSERVER_SHM + 10u)
#define CLEAN_OBSERVER_PROGRESS (CLEAN_OBSERVER_SHM + 12u)
#define CLEAN_OBSERVER_HINT_LENGTH (CLEAN_OBSERVER_SHM + 13u)
#define CLEAN_OBSERVER_WORDS (CLEAN_OBSERVER_HEADER_WORDS + \
    CLEAN_OBSERVER_RECORD_WORDS * CLEAN_OBSERVER_RECORDS)
#define CLEAN_OBSERVER_EARLY_SHM 0x400u
#define CLEAN_OBSERVER_EARLY_HEADER (CLEAN_RX_HT_ADVANCE ? 16u : CLEAN_RX_HT_EARLY_FIFO ? 14u : 12u)
#define CLEAN_OBSERVER_EARLY_MAX (CLEAN_RX_HT_COPY_SCOPE ? 320u : CLEAN_RX_HT_ADVANCE ? 480u : CLEAN_RX_HT_EARLY_FIFO ? 996u : 1000u)
#define CLEAN_OBSERVER_EARLY_TAG 0x4000u
#define CLEAN_OBSERVER_ADVANCE (CLEAN_OBSERVER_EARLY_SHM + 12u)
#define CLEAN_OBSERVER_ADVANCE_TAG 0x8000u
#define CLEAN_OBSERVER_EARLY_RETAIN_WORDS (CLEAN_OBSERVER_EARLY_HEADER + \
    (CLEAN_OBSERVER_EARLY_MAX+1u)/2u)
#define CLEAN_OBSERVER_EARLY_WORDS 512u
_Static_assert(CLEAN_OBSERVER_EARLY_RETAIN_WORDS <= CLEAN_OBSERVER_EARLY_WORDS,
    "Early header and complete body must fit the exported snapshot");
#define CLEAN_OBSERVER_SECOND_SHM 0x500u
#define CLEAN_OBSERVER_SECOND_HEADER 12u
#define CLEAN_OBSERVER_SECOND_MAX 300u
#define CLEAN_OBSERVER_HANDOFF_SHM (CLEAN_OBSERVER_SECOND_SHM + \
    CLEAN_OBSERVER_SECOND_HEADER + CLEAN_OBSERVER_SECOND_MAX/2u)
#define CLEAN_OBSERVER_HANDOFF_HEADER 4u
#define CLEAN_OBSERVER_HANDOFF_RECORD_WORDS 12u
#define CLEAN_OBSERVER_HANDOFF_RECORDS 3u
#define CLEAN_OBSERVER_COPY_SHM (CLEAN_OBSERVER_HANDOFF_SHM + \
    CLEAN_OBSERVER_HANDOFF_HEADER + CLEAN_OBSERVER_HANDOFF_RECORD_WORDS*CLEAN_OBSERVER_HANDOFF_RECORDS)
#define CLEAN_OBSERVER_COPY_WORDS (CLEAN_RX_HT_COPY_STREAM_REARM ? 54u : 44u)
#define CLEAN_OBSERVER_COPY_VERSION (CLEAN_STREAM_END_PROBE ? 7u : CLEAN_STREAM_DEST_PROBE ? 6u : CLEAN_RX_HT_COPY_STREAM_STALL ? 5u : CLEAN_RX_HT_COPY_STREAM_REARM ? 4u : CLEAN_RX_HT_COPY_STREAM_PHASE ? 3u : CLEAN_RX_HT_COPY_WAIT ? 2u : 1u)
_Static_assert(!CLEAN_STREAM_DEST_PROBE ||
    (CLEAN_SHM_FRAME+CLEAN_STREAM_DEST_WORDS < 0x1000u &&
     2u*CLEAN_STREAM_DEST_WORDS+CLEAN_OBSERVER_SECOND_MAX+8u <= CLEAN_RXE_BYTES),
    "Destination probe must keep the bounded continuation inside RX scratch storage");
#define CLEAN_OBSERVER_COPY_CANARY 0xa55au
#define CLEAN_OBSERVER_COPY_OFFSETS {8u,32u,64u,128u}
#define CLEAN_OBSERVER_SAVE_R2 (CLEAN_OBSERVER_SHM + 14u)
#define CLEAN_OBSERVER_SAVE_R3 (CLEAN_OBSERVER_SHM + 15u)
/* Terminal/exit entry overwrites OUTCOME before use; RX can reuse it here. */
#define CLEAN_OBSERVER_HINT_STATUS CLEAN_OBSERVER_OUTCOME

_Static_assert(!CLEAN_RX_HT_EARLY_SAVE || CLEAN_RX_HT_OBSERVER,
    "Early snapshots require the passive HT observer");
_Static_assert(!CLEAN_RX_HT_ADVANCE || CLEAN_RX_HT_EARLY_SAVE,
    "Bounded advance requires complete early retention");
_Static_assert(CLEAN_OBSERVER_SHM+CLEAN_OBSERVER_WORDS <= CLEAN_OBSERVER_EARLY_SHM &&
    CLEAN_OBSERVER_EARLY_SHM+CLEAN_OBSERVER_EARLY_WORDS <= 0x600u,
    "Early snapshots must not overlap observer records or SHM control/status");
_Static_assert(!CLEAN_RX_HT_ADVANCE ||
    (CLEAN_OBSERVER_EARLY_SHM+CLEAN_OBSERVER_EARLY_RETAIN_WORDS <= CLEAN_OBSERVER_SECOND_SHM &&
     CLEAN_OBSERVER_SECOND_SHM+CLEAN_OBSERVER_SECOND_HEADER+CLEAN_OBSERVER_SECOND_MAX/2u <= 0x600u),
    "First and continuation snapshots must have separate bounded storage");
_Static_assert(CLEAN_OBSERVER_HANDOFF_SHM + CLEAN_OBSERVER_HANDOFF_HEADER +
    CLEAN_OBSERVER_HANDOFF_RECORD_WORDS*CLEAN_OBSERVER_HANDOFF_RECORDS <= 0x600u,
    "Handoff trace must fit unused continuation storage");
_Static_assert(CLEAN_OBSERVER_COPY_SHM+CLEAN_OBSERVER_COPY_WORDS <= 0x600u,
    "Receive-copy probe must fit unused continuation storage");
#include "copy_scope.h"

#include "psm/observer_program.h"

#endif
