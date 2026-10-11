#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include "host_io.h"

static int cg_input_error(const char *filename, const char *message) {
  fprintf(stderr, "badge-codegen: %s:0: %s\n", filename, message);
  return 1;
}

int main(int argc, char **argv) {
  const char *default_frame =
      BADGE_DEFAULT_FRAME_ADDRESSING == BADGE_FRAME_DIRECT ? "direct" : "materialized";
  const char *default_registers =
      BADGE_DEFAULT_REGISTER_RESIDENCY == BADGE_RESIDENCY_GLOBAL_COMPACT ? "global-compact" :
      BADGE_DEFAULT_REGISTER_RESIDENCY == BADGE_RESIDENCY_GLOBAL_FLEX ? "global-flex" :
      BADGE_DEFAULT_REGISTER_RESIDENCY == BADGE_RESIDENCY_GLOBAL ? "global" :
      BADGE_DEFAULT_REGISTER_RESIDENCY == BADGE_RESIDENCY_LOCAL ? "local" : "stack";
  const char *default_scalar =
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_CONSTANTS ? "thumb-constants" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_WIDTHS ? "thumb-widths" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_OPERANDS ? "thumb-operands" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_CONTROL ? "thumb-control" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_FRAMES ? "thumb-frames" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_EFFECTS ? "thumb-effects" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_CALLS ? "thumb-calls" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_HOMES ? "thumb-homes" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_BRANCH ? "thumb-branch" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_RELAX ? "thumb-relax" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_SCHEDULE ? "thumb-schedule" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_COMPACT ? "thumb-compact" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_LAYOUT ? "thumb-layout" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB_REGISTERS ? "thumb-registers" :
      BADGE_DEFAULT_SCALAR_SELECTION == BADGE_SELECTION_THUMB ? "thumb" : "legacy";
  const char *default_memory =
      BADGE_DEFAULT_MEMORY_SELECTION == BADGE_MEMORY_WORD ? "word" : "byte";
  const char *usage =
      "usage: badge-codegen [--frame-addressing=materialized|direct] "
      "[--register-residency=stack|local|global|global-flex|global-compact] "
      "[--scalar-selection=legacy|thumb|thumb-registers|thumb-layout|thumb-compact|thumb-schedule|thumb-relax|thumb-branch|thumb-homes|thumb-calls|thumb-effects|thumb-frames|thumb-control|thumb-operands|thumb-widths|thumb-constants] [--memory-selection=byte|word] "
      "input.ll "
      "-o output.s";
  if (argc == 2 && !strcmp(argv[1], "--help")) {
    puts(usage);
    printf("default frame addressing: %s (query with --default-frame-addressing)\n", default_frame);
    printf("default register residency: %s (query with --default-register-residency)\n", default_registers);
    printf("default scalar selection: %s (query with --default-scalar-selection)\n", default_scalar);
    printf("default memory selection: %s (query with --default-memory-selection)\n", default_memory);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--default-frame-addressing")) {
    puts(default_frame);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--default-register-residency")) {
    puts(default_registers);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--default-scalar-selection")) {
    puts(default_scalar);
    return 0;
  }
  badge_frame_addressing frame_addressing = BADGE_DEFAULT_FRAME_ADDRESSING;
  if (argc == 2 && !strcmp(argv[1], "--default-memory-selection")) {
    puts(default_memory);
    return 0;
  }
  badge_register_residency register_residency = BADGE_DEFAULT_REGISTER_RESIDENCY;
  badge_scalar_selection selection = BADGE_DEFAULT_SCALAR_SELECTION;
  badge_memory_selection memory = BADGE_DEFAULT_MEMORY_SELECTION;
  int seen_memory = 0;
  int seen_frame = 0, seen_registers = 0, seen_selection = 0;
  while (argc > 1 && (!strncmp(argv[1], "--frame-addressing=", 19) ||
                      !strncmp(argv[1], "--register-residency=", 21) ||
                      !strncmp(argv[1], "--scalar-selection=", 19) ||
                      !strncmp(argv[1], "--memory-selection=", 19))) {
    if (!strncmp(argv[1], "--memory-selection=", 19)) {
      if (seen_memory++ ||
          (strcmp(argv[1] + 19, "byte") && strcmp(argv[1] + 19, "word"))) {
        fputs("badge-codegen: unsupported/duplicate memory selection\n",
              stderr);
        return 2;
      }
      memory =
          !strcmp(argv[1] + 19, "word") ? BADGE_MEMORY_WORD : BADGE_MEMORY_BYTE;
    } else if (!strncmp(argv[1], "--scalar-selection=", 19)) {
      if (seen_selection++ ||
          (strcmp(argv[1] + 19, "legacy") && strcmp(argv[1] + 19, "thumb") &&
           strcmp(argv[1] + 19, "thumb-registers") &&
           strcmp(argv[1] + 19, "thumb-layout") &&
           strcmp(argv[1] + 19, "thumb-compact") &&
           strcmp(argv[1] + 19, "thumb-schedule") &&
           strcmp(argv[1] + 19, "thumb-relax") &&
           strcmp(argv[1] + 19, "thumb-branch") &&
           strcmp(argv[1] + 19, "thumb-homes") &&
           strcmp(argv[1] + 19, "thumb-calls") &&
           strcmp(argv[1] + 19, "thumb-effects") &&
           strcmp(argv[1] + 19, "thumb-frames") &&
           strcmp(argv[1] + 19, "thumb-control") &&
           strcmp(argv[1] + 19, "thumb-operands") &&
           strcmp(argv[1] + 19, "thumb-widths") &&
           strcmp(argv[1] + 19, "thumb-constants"))) {
        fputs("badge-codegen: unsupported/duplicate scalar selection\n",
              stderr);
        return 2;
      }
      selection = !strcmp(argv[1] + 19, "thumb-constants") ? BADGE_SELECTION_THUMB_CONSTANTS :
                  !strcmp(argv[1] + 19, "thumb-widths") ? BADGE_SELECTION_THUMB_WIDTHS :
                  !strcmp(argv[1] + 19, "thumb-operands") ? BADGE_SELECTION_THUMB_OPERANDS :
                  !strcmp(argv[1] + 19, "thumb-control") ? BADGE_SELECTION_THUMB_CONTROL :
                  !strcmp(argv[1] + 19, "thumb-frames") ? BADGE_SELECTION_THUMB_FRAMES :
                  !strcmp(argv[1] + 19, "thumb-effects") ? BADGE_SELECTION_THUMB_EFFECTS :
                  !strcmp(argv[1] + 19, "thumb-calls") ? BADGE_SELECTION_THUMB_CALLS :
                  !strcmp(argv[1] + 19, "thumb-homes") ? BADGE_SELECTION_THUMB_HOMES :
                  !strcmp(argv[1] + 19, "thumb-branch") ? BADGE_SELECTION_THUMB_BRANCH :
                  !strcmp(argv[1] + 19, "thumb-relax") ? BADGE_SELECTION_THUMB_RELAX :
                  !strcmp(argv[1] + 19, "thumb-schedule") ? BADGE_SELECTION_THUMB_SCHEDULE :
                  !strcmp(argv[1] + 19, "thumb-compact") ? BADGE_SELECTION_THUMB_COMPACT :
                  !strcmp(argv[1] + 19, "thumb-layout") ? BADGE_SELECTION_THUMB_LAYOUT :
                  !strcmp(argv[1] + 19, "thumb-registers") ? BADGE_SELECTION_THUMB_REGISTERS :
                  !strcmp(argv[1] + 19, "thumb") ? BADGE_SELECTION_THUMB
                                                 : BADGE_SELECTION_LEGACY;
    } else if (!strncmp(argv[1], "--register-residency=", 21)) {
      if (seen_registers++ ||
          (strcmp(argv[1] + 21, "stack") && strcmp(argv[1] + 21, "local") &&
           strcmp(argv[1] + 21, "global") && strcmp(argv[1] + 21, "global-flex") &&
           strcmp(argv[1] + 21, "global-compact"))) {
        fputs("badge-codegen: unsupported/duplicate register residency "
              "selection\n",
              stderr);
        return 2;
      }
      register_residency =
          !strcmp(argv[1] + 21, "global-compact") ? BADGE_RESIDENCY_GLOBAL_COMPACT :
          !strcmp(argv[1] + 21, "global-flex") ? BADGE_RESIDENCY_GLOBAL_FLEX :
          !strcmp(argv[1] + 21, "global")  ? BADGE_RESIDENCY_GLOBAL
          : !strcmp(argv[1] + 21, "local") ? BADGE_RESIDENCY_LOCAL
                                           : BADGE_RESIDENCY_STACK;
    } else {
      if (seen_frame++) {
        fputs("badge-codegen: duplicate frame addressing selection\n", stderr);
        return 2;
      }
      if (!strcmp(argv[1] + 19, "direct"))
        frame_addressing = BADGE_FRAME_DIRECT;
      else if (strcmp(argv[1] + 19, "materialized")) {
        fputs("badge-codegen: unsupported frame addressing selection\n",
              stderr);
        return 2;
      }
    }
    argv++;
    argc--;
  }
  if (argc != 4 || strcmp(argv[2], "-o")) {
    fprintf(stderr, "%s\n", usage);
    return 2;
  }
  struct stat a, b;
  if (stat(argv[1], &a) || !S_ISREG(a.st_mode) || a.st_size < 0 ||
      a.st_size > 16 * 1024 * 1024)
    return cg_input_error(argv[1],
                          "input must be a regular file within 16 MiB");
  if (stat(argv[3], &b) == 0) {
    if (a.st_dev == b.st_dev && a.st_ino == b.st_ino)
      return cg_input_error(argv[1], "output aliases input");
  } else if (errno != ENOENT)
    return cg_input_error(argv[1], "cannot inspect output");
  size_t n = (size_t)a.st_size;
  char *source = calloc(1, n + 1);
  if (!source)
    return cg_input_error(argv[1], "out of memory");
  FILE *f = fopen(argv[1], "rb");
  if (!f) {
    free(source);
    return cg_input_error(argv[1], "cannot open input");
  }
  int ok = fread(source, 1, n, f) == n;
  if (fgetc(f) != EOF || ferror(f))
    ok = 0;
  if (fclose(f))
    ok = 0;
  if (!ok) {
    free(source);
    return cg_input_error(argv[1], "input read failed");
  }
  char *assembly = NULL;
  size_t size = 0;
  ok = badge_codegen_compile_with_selections(
      source, n, argv[1],
      (badge_codegen_options){frame_addressing, register_residency}, selection,
      memory, &assembly, &size);
  free(source);
  if (ok)
    ok = host_write_file("badge-codegen", argv[3], (unsigned char *)assembly,
                         size);
  free(assembly);
  return ok ? 0 : 1;
}
