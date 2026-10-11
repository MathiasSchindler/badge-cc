#define _POSIX_C_SOURCE 200809L
#include "host_io.h"
#include "ir_pass.h"
#include "ir_analysis.h"

int badge_opt_main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--default-passes")) {
    for (unsigned j = 0; j < badge_ir_default_pass_count; j++)
      printf("%s%s", j ? "," : "", badge_ir_default_pipeline[j]);
    putchar('\n');
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--list-passes")) {
    for (unsigned j = 0; j < badge_ir_pass_count; j++)
      printf("%s\t%s\n", badge_ir_passes[j].name,
             badge_ir_passes[j].description);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--help")) {
    puts("usage: badge-opt [--passes=name,...|--verify-only] input.ll -o output.ll\n       "
         "badge-opt --list-passes\n       badge-opt --default-passes");
    return 0;
  }
  const char *selection = NULL;
  int verify_only = argc == 5 && !strcmp(argv[1], "--verify-only");
  if (argc == 5 && (verify_only || !strncmp(argv[1], "--passes=", 9))) {
    if (!verify_only) selection = argv[1] + 9;
    argv++;
    argc--;
  }
  if (argc != 4 || strcmp(argv[2], "-o")) {
    fputs("usage: badge-opt [--passes=name,...|--verify-only] input.ll -o output.ll\n",
          stderr);
    return 2;
  }
  ir_context *c = calloc(1, sizeof(*c));
  if (!c) {
    fputs("badge-opt: out of memory\n", stderr);
    return 1;
  }
  c->file = argv[1];
  int result = 1;
  if (!setjmp(c->failure)) {
    struct stat a, b;
    if (stat(argv[1], &a) || !S_ISREG(a.st_mode) || a.st_size < 0 ||
        a.st_size > 16 * 1024 * 1024)
      ir_error(c, "input must be a regular file within 16 MiB");
    if (!stat(argv[3], &b)) {
      if (a.st_dev == b.st_dev && a.st_ino == b.st_ino)
        ir_error(c, "output aliases input");
    } else if (errno != ENOENT)
      ir_error(c, "cannot inspect output");
    size_t size = (size_t)a.st_size;
    char *source = ir_allocate(c, size + 1);
    FILE *f = fopen(argv[1], "rb");
    if (!f)
      ir_error(c, "cannot open input");
    int ok = fread(source, 1, size, f) == size;
    if (fgetc(f) != EOF || ferror(f))
      ok = 0;
    if (fclose(f))
      ok = 0;
    if (!ok)
      ir_error(c, "input read failed");
    if (verify_only) {
      ir_module m = {0};
      ir_parse(c, &m, source, size);
      ir_verify(c, &m);
      ir_write(c, &m);
    } else ir_pipeline(c, source, size, selection);
    result = host_write_file("badge-opt", argv[3], (unsigned char *)c->output,
                             c->size)
                 ? 0
                 : 1;
  }
  ir_destroy(c);
  free(c);
  return result;
}

#ifndef BADGE_OPT_LIBRARY
int main(int argc, char **argv) { return badge_opt_main(argc, argv); }
#endif
