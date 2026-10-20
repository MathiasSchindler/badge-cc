#ifndef BADGE_COMPILER_HOST_PLATFORM_H
#define BADGE_COMPILER_HOST_PLATFORM_H

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static inline const char *host_last_separator(const char *path) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
#endif
    return slash;
}

static inline int host_path_equal(const char *left, const char *right) {
#ifdef _WIN32
    return _stricmp(left, right) == 0;
#else
    return strcmp(left, right) == 0;
#endif
}

#ifdef _WIN32
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* CRT stat reports zero inodes on Windows; retain real file identities for
   include-once and alias protection, including hard links. */
struct host_windows_stat {
    uint64_t st_dev, st_ino;
    int64_t st_size;
    unsigned st_mode;
};

static inline int host_windows_error(void) {
    switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION: errno = EACCES; break;
        case ERROR_FILENAME_EXCED_RANGE: errno = ENAMETOOLONG; break;
        default: errno = EIO; break;
    }
    return -1;
}

static inline int host_windows_info(HANDLE handle, struct host_windows_stat *info,
                                    int nofollow) {
    BY_HANDLE_FILE_INFORMATION data;
    if (!GetFileInformationByHandle(handle, &data)) return host_windows_error();
    info->st_dev = data.dwVolumeSerialNumber;
    info->st_ino = (uint64_t)data.nFileIndexHigh << 32u | data.nFileIndexLow;
    info->st_size = (int64_t)((uint64_t)data.nFileSizeHigh << 32u | data.nFileSizeLow);
    info->st_mode = nofollow && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        ? 0u : (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? S_IFDIR : S_IFREG;
    return 0;
}

static inline int host_windows_path_info(const char *path, struct host_windows_stat *info,
                                         int nofollow) {
    HANDLE handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | (nofollow ? FILE_FLAG_OPEN_REPARSE_POINT : 0), NULL);
    if (handle == INVALID_HANDLE_VALUE) return host_windows_error();
    int result = host_windows_info(handle, info, nofollow);
    CloseHandle(handle);
    return result;
}

static inline int host_windows_stat(const char *path, struct host_windows_stat *info) {
    return host_windows_path_info(path, info, 0);
}

static inline int host_windows_lstat(const char *path, struct host_windows_stat *info) {
    return host_windows_path_info(path, info, 1);
}

static inline int host_windows_fstat(int fd, struct host_windows_stat *info) {
    intptr_t handle = _get_osfhandle(fd);
    if (handle == -1) { errno = EBADF; return -1; }
    return host_windows_info((HANDLE)handle, info, 0);
}

static inline char *host_windows_realpath(const char *path, char *resolved) {
    HANDLE handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) { host_windows_error(); return NULL; }
    char buffer[PATH_MAX + 8];
    DWORD count = GetFinalPathNameByHandleA(handle, buffer, sizeof(buffer), FILE_NAME_NORMALIZED);
    if (count == 0) {
        host_windows_error();
        CloseHandle(handle);
        return NULL;
    }
    CloseHandle(handle);
    if (count >= sizeof(buffer)) { errno = ENAMETOOLONG; return NULL; }
    const char *start = buffer;
    char unc[PATH_MAX + 8];
    if (strncmp(start, "\\\\?\\UNC\\", 8) == 0) {
        unc[0] = unc[1] = '\\';
        strcpy(unc + 2, start + 8);
        start = unc;
    } else if (strncmp(start, "\\\\?\\", 4) == 0) start += 4;
    size_t length = strlen(start);
    if (length >= PATH_MAX) { errno = ENAMETOOLONG; return NULL; }
    if (resolved == NULL) {
        resolved = malloc(length + 1);
        if (resolved == NULL) { errno = ENOMEM; return NULL; }
    }
    memcpy(resolved, start, length + 1);
    for (size_t i = 0; i < length; ++i)
        if (resolved[i] == '\\') resolved[i] = '/';
    return resolved;
}

/* Keep the POSIX call sites shared; these aliases are internal to host tools. */
#define stat host_windows_stat
#define lstat host_windows_lstat
#define fstat host_windows_fstat
#define realpath host_windows_realpath
#endif

#endif
