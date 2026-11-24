#ifndef BADGE_HOST_DIAGNOSTICS_H
#define BADGE_HOST_DIAGNOSTICS_H

#ifdef BADGE_ENABLE_PARALLEL_ADMISSION
extern _Thread_local int badge_defer_cost_diagnostics;
#endif

static inline int badge_diagnostics_enabled(void) {
#ifdef BADGE_ENABLE_PARALLEL_ADMISSION
  return !badge_defer_cost_diagnostics;
#else
  return 1;
#endif
}

#endif
