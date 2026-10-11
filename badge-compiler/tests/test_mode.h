#ifndef BADGE_COMPILER_TEST_MODE_H
#define BADGE_COMPILER_TEST_MODE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int badge_test_oracles_enabled(void) {
  const char *mode = getenv("BADGE_TEST_MODE");
  if (!mode || !strcmp(mode, "oracle"))
    return 1;
  if (!strcmp(mode, "owned"))
    return 0;
  fprintf(stderr, "badge-test: invalid BADGE_TEST_MODE: %s\n", mode);
  exit(2);
}

static inline const char *badge_test_tool(const char *name) {
  if (!badge_test_oracles_enabled())
    return NULL;
  const char *tool = getenv(name);
  if (!tool || !*tool) {
    fprintf(stderr, "badge-test: oracle mode requires %s\n", name);
    exit(2);
  }
  return tool;
}

#endif
