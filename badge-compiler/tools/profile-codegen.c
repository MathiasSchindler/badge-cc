#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "codegen.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static uint64_t now(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t)) {
    perror("profile-codegen: clock_gettime");
    exit(1);
  }
  return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static struct rusage usage(void) {
  struct rusage u;
  if (getrusage(RUSAGE_SELF, &u)) {
    perror("profile-codegen: getrusage");
    exit(1);
  }
  return u;
}
static uint64_t micros(struct timeval t) {
  return (uint64_t)t.tv_sec * UINT64_C(1000000) + (uint64_t)t.tv_usec;
}
int main(int argc, char **argv) {
  char *end;
  errno = 0;
  unsigned long repeats = argc > 1 ? strtoul(argv[1], &end, 10) : 0;
  if (argc < 3 || errno || !repeats || repeats > 10000 ||
      argv[1][0] < '0' || argv[1][0] > '9' || *end) {
    fprintf(stderr, "Usage: profile-codegen REPEATS(1..10000) INPUT.ll ...\n");
    return 2;
  }
  for (int n = 2; n < argc; n++) {
    FILE *f = fopen(argv[n], "rb");
    if (!f || fseek(f, 0, SEEK_END)) {
      fprintf(stderr, "profile-codegen: cannot open/seek %s\n", argv[n]);
      if (f) fclose(f);
      return 1;
    }
    long length = ftell(f);
    if (length < 0 || length > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
      fprintf(stderr, "profile-codegen: invalid input size: %s\n", argv[n]);
      fclose(f);
      return 1;
    }
    char *source = malloc((size_t)length + 1);
    if (!source) {
      fprintf(stderr, "profile-codegen: out of memory\n");
      fclose(f);
      return 1;
    }
    int ok = fread(source, 1, (size_t)length, f) == (size_t)length;
    if (fclose(f)) ok = 0;
    if (!ok) {
      fprintf(stderr, "profile-codegen: cannot read %s\n", argv[n]);
      free(source);
      return 1;
    }
    source[length] = 0;
    char *expected = NULL;
    size_t size = 0;
    if (!badge_codegen_compile(source, (size_t)length, argv[n], &expected, &size)) {
      free(source);
      return 1;
    }
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t k = 0; k < size; k++)
      hash = (hash ^ (unsigned char)expected[k]) * UINT64_C(1099511628211);
    uint64_t elapsed = 0, user = 0, system = 0;
    for (unsigned long r = 0; r < repeats; r++) {
      char *assembly = NULL;
      size_t actual_size = 0;
      struct rusage before = usage();
      uint64_t start = now();
      int compiled = badge_codegen_compile(source, (size_t)length, argv[n],
                                           &assembly, &actual_size);
      elapsed += now() - start;
      struct rusage after = usage();
      user += micros(after.ru_utime) - micros(before.ru_utime);
      system += micros(after.ru_stime) - micros(before.ru_stime);
      if (!compiled) {
        free(expected);
        free(source);
        return 1;
      }
      if (actual_size != size || memcmp(assembly, expected, size)) {
        fprintf(stderr, "profile-codegen: nondeterministic output: %s\n", argv[n]);
        free(assembly);
        free(expected);
        free(source);
        return 1;
      }
      free(assembly);
    }
    struct rusage after = usage();
#ifdef __APPLE__
    const unsigned scale = 1;
#else
    const unsigned scale = 1024;
#endif
    printf("%d\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
           "\t%zu\t%016" PRIx64 "\n",
           n - 2, elapsed, user, system,
           (uint64_t)after.ru_maxrss * scale, size, hash);
    free(expected);
    free(source);
  }
  return 0;
}
