#ifndef BADGE_COMPILER_HOST_IO_H
#define BADGE_COMPILER_HOST_IO_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int host_write_file(const char *tool, const char *path,
                           const unsigned char *bytes, size_t length) {
    struct stat existing;
    if (lstat(path, &existing) == 0) {
        if (!S_ISREG(existing.st_mode)) {
            fprintf(stderr, "%s: existing output must be a regular file\n", tool);
            return 0;
        }
    } else if (errno != ENOENT) {
        fprintf(stderr, "%s: cannot inspect output %s: %s\n", tool, path, strerror(errno));
        return 0;
    }
    size_t path_length = strlen(path);
    char *temporary = malloc(path_length + sizeof(".tmp.XXXXXX"));
    if (temporary == NULL) {
        fprintf(stderr, "%s: out of memory creating output path\n", tool);
        return 0;
    }
    memcpy(temporary, path, path_length);
    memcpy(temporary + path_length, ".tmp.XXXXXX", sizeof(".tmp.XXXXXX"));
    int fd = mkstemp(temporary);
    if (fd < 0) {
        fprintf(stderr, "%s: cannot create output %s: %s\n", tool, path, strerror(errno));
        free(temporary);
        return 0;
    }
    FILE *file = fdopen(fd, "wb");
    int ok = 0;
    if (file == NULL) {
        fprintf(stderr, "%s: cannot open temporary output stream\n", tool);
        close(fd);
    } else {
        ok = length == 0 || fwrite(bytes, 1, length, file) == length;
        if (fclose(file) != 0) ok = 0;
        if (!ok) fprintf(stderr, "%s: failed to write or close output\n", tool);
    }
    if (ok && rename(temporary, path) != 0) {
        fprintf(stderr, "%s: cannot replace output %s: %s\n", tool, path, strerror(errno));
        ok = 0;
    }
    if (!ok && unlink(temporary) != 0) {
        fprintf(stderr, "%s: cannot remove temporary output %s: %s\n",
                tool, temporary, strerror(errno));
    }
    free(temporary);
    return ok;
}

#endif
