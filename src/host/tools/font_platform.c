#include "font_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *allocate(void *context, size_t size) {
    (void)context;
    return malloc(size);
}
static void *resize(void *context, void *pointer, size_t size) {
    (void)context;
    return realloc(pointer, size);
}
static void release(void *context, void *pointer) {
    (void)context;
    free(pointer);
}
static void *copy(void *context, void *destination, const void *source, size_t size) {
    (void)context;
    return memcpy(destination, source, size);
}
static void *move(void *context, void *destination, const void *source, size_t size) {
    (void)context;
    return memmove(destination, source, size);
}
static void *fill(void *context, void *destination, int value, size_t size) {
    (void)context;
    return memset(destination, value, size);
}
static int compare(void *context, const void *left, const void *right, size_t size) {
    (void)context;
    return memcmp(left, right, size);
}
static FrPlatformFileResult load(void *context, const char *name, FrPlatformFile *file) {
    (void)context;
    memset(file, 0, sizeof(*file));
    FILE *input = fopen(name, "rb");
    if (input == NULL) { perror(name); return FR_PLATFORM_FILE_ERR_IO; }
    if (fseek(input, 0, SEEK_END) != 0) { perror(name); fclose(input); return FR_PLATFORM_FILE_ERR_IO; }
    long size = ftell(input);
    if (size <= 0 || size > 16 * 1024 * 1024 || fseek(input, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: invalid font file size\n", name);
        fclose(input);
        return FR_PLATFORM_FILE_ERR_IO;
    }
    void *data = malloc((size_t)size);
    if (data == NULL) { fprintf(stderr, "font file allocation failed\n"); fclose(input); return FR_PLATFORM_FILE_ERR_NO_MEMORY; }
    int ok = fread(data, 1u, (size_t)size, input) == (size_t)size;
    if (fclose(input) != 0) ok = 0;
    if (!ok) { fprintf(stderr, "%s: font file read failed\n", name); free(data); return FR_PLATFORM_FILE_ERR_IO; }
    file->data = data;
    file->size = (size_t)size;
    file->handle = data;
    return FR_PLATFORM_FILE_OK;
}
static void unload(void *context, FrPlatformFile *file) {
    (void)context;
    free(file->handle);
    memset(file, 0, sizeof(*file));
}
static void log_message(void *context, FrPlatformLogLevel level, const char *component,
                         const char *format, va_list arguments) {
    (void)context;
    (void)level;
    fprintf(stderr, "fontrender/%s: ", component);
    vfprintf(stderr, format, arguments);
}
int host_font_platform_install(void) {
    const FrPlatform platform = {
        .alloc = allocate, .realloc = resize, .free = release,
        .memcpy = copy, .memmove = move, .memset = fill, .memcmp = compare,
        .load_file = load, .unload_file = unload, .log = log_message
    };
    return fr_platform_set(&platform);
}
