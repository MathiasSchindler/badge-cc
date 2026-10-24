#ifndef CLEAN_RX_PROFILE_CHECKS_H
#define CLEAN_RX_PROFILE_CHECKS_H

#include "firmware_profile.h"
#include "ht_contract.h"
#include "ht_observer.h"

#if !CLEAN_RX_DIAGNOSTIC && \
                (CLEAN_RX_HT_PROBE || CLEAN_RX_HT_CLEAR_SIG_CRC || \
                 CLEAN_RX_HT_STIMULUS || CLEAN_RX_HT_HANDOFF_STIMULUS || \
                 CLEAN_RX_AMPDU_PROBE || CLEAN_RX_AMPDU_BAD_FCS || \
                 CLEAN_RX_AMPDU_INTERFACE || CLEAN_RX_AMPDU_HANDOFF || \
                 CLEAN_RX_AMPDU_DMA || CLEAN_RX_AMPDU_SHM || CLEAN_RX_AMPDU_BA || \
                 CLEAN_RX_HT_ADVANCE || CLEAN_RX_HT_COPY_PROBE || \
                 CLEAN_RX_HT_NEXT_FIELD != 0x83u || CLEAN_RX_HT_NEXT_STREAM || \
                 CLEAN_RX_HT_SHM_ONLY || CLEAN_RX_HT_SHORT_STREAM || \
                 CLEAN_RX_HT_DELIMITER_GUARD || CLEAN_RX_HT_COPY_DMA_PROBE || \
                 CLEAN_RX_HT_COPY_WAIT || CLEAN_RX_HT_COPY_END_PLUS6 || \
                 CLEAN_RX_HT_COPY_QUIET || CLEAN_RX_HT_COPY_STREAM_REARM || \
                 CLEAN_RX_HT_COPY_SCOPE || CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY || \
                 CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD != 0x83u || \
                 CLEAN_RX_HT_VARIABLE || CLEAN_RX_HT_HANDOFF_GUARD || \
                 CLEAN_RX_HT_COPY_DEFER_STREAM || CLEAN_RX_HT_VARIABLE_STREAM64 || \
                 CLEAN_RX_HT_COPY_STREAM_PHASE || CLEAN_RX_HT_REARM_INJECT || \
                 CLEAN_RX_HT_REARM_PARK)
#error "Active experiments require PROFILE=diagnostic"
#endif

#ifdef CLEAN_SDIO_SELF_TEST
_Static_assert(CLEAN_RX_DIAGNOSTIC && CLEAN_SDIO_SELF_TEST == 1,
               "Synthetic SDIO traffic requires a diagnostic firmware profile");
#elif defined(CLEAN_SDIO_SELF_TEST_PACED) || defined(CLEAN_SDIO_SELF_TEST_LARGE) || \
      defined(CLEAN_SDIO_SELF_TEST_TRANSITION) || defined(CLEAN_SDIO_SELF_TEST_DMA32) || \
      defined(CLEAN_SDIO_SELF_TEST_WINDOW)
#error "SDIO test options require PROFILE=sdio-test or explicit diagnostic SDIO selection"
#endif

#endif
