#define wmain picoctl_entry
#include "../tools/picoctl.c"
#undef wmain
#include <assert.h>

int wmain(int argc, wchar_t **argv) {
    if (argc > 1 && !wcscmp(argv[1], L"--mock-success")) {
        if (argc != 5 || wcscmp(argv[2], L"space and Unicode \x03bb") ||
            wcscmp(argv[3], L"C:\\trailing slash\\") ||
            wcscmp(argv[4], L"quotes\" & | % !")) return 19;
        return 0;
    }
    if (argc > 1 && !wcscmp(argv[1], L"--mock-failure")) return 17;
    if (argc > 1 && !wcscmp(argv[1], L"--mock-timeout")) { Sleep(10000); return 0; }
    options_t options = {0};
    wchar_t executable[32768];
    assert(GetModuleFileNameW(NULL, executable, 32768));
    options.picotool = executable;
    options.timeout = 3000;
    const wchar_t *success[] = {L"--mock-success", L"space and Unicode \x03bb",
                                L"C:\\trailing slash\\", L"quotes\" & | % !"};
    assert(picotool(&options, success, 4));
    const wchar_t *fail[] = {L"--mock-failure"};
    assert(!picotool(&options, fail, 1));
    options.timeout = 100;
    const wchar_t *timeout[] = {L"--mock-timeout"};
    ULONGLONG start = GetTickCount64();
    assert(!picotool(&options, timeout, 1));
    assert(GetTickCount64() - start < 5000);
    wchar_t *valid[] = {L"picoctl", L"status", L"--serial", L"7c4918bab9492c77",
                        L"--build-id", L"d1828acd", L"--timeout", L"1", L"--wait-ip"};
    assert(parse(9, valid, &options));
    assert(options.build == 0xd1828acdu && options.timeout == 1000 && options.wait_ip);
    valid[1] = L"wifi";
    assert(parse(8, valid, &options) && !options.wait_ip);
    assert(!parse(9, valid, &options));
    assert(!parse(4, valid, &options));
    wchar_t *invalid[] = {L"picoctl", L"upload", L"--serial", L"7C4918BAB9492C77"};
    assert(!parse(4, invalid, &options));
    wchar_t *duplicate[] = {L"picoctl", L"bootsel", L"--serial", L"7C4918BAB9492C77",
                            L"--serial", L"7C4918BAB9492C77"};
    assert(!parse(6, duplicate, &options));
    wchar_t *unknown[] = {L"picoctl", L"status", L"--unknown", L"anything"};
    assert(!parse(4, unknown, &options));

    wchar_t directory[MAX_PATH], path[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, directory));
    assert(GetTempFileNameW(directory, L"p2w", 0, path));
    options_t image_options = {0};
    image_options.image = path;
    uint8_t block[512] = {0};
    const uint32_t header[] = {0x0a324655, 0x9e5d5157, 0x2000, 0x10000000,
                              256, 0, 1, 0xe48bff59};
    for (unsigned i = 0; i < 8; ++i) usb_store_u32(block + i * 4, header[i]);
    usb_store_u32(block + 508, 0x0ab16f30);
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
    assert(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) >= 0);
    assert(BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) >= 0);
    assert(BCryptHashData(hash, block, sizeof(block), 0) >= 0);
    assert(BCryptFinishHash(hash, image_options.sha256, 32, 0) >= 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    DWORD written;
    assert(WriteFile(file, block, sizeof(block), &written, NULL) && written == sizeof(block));
    CloseHandle(file);
    file = check_image(&image_options);
    assert(file != INVALID_HANDLE_VALUE);
    HANDLE writer = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    assert(writer == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
    CloseHandle(file);
    image_options.sha256[0] ^= 1;
    assert(check_image(&image_options) == INVALID_HANDLE_VALUE);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, 0, NULL);
    assert(file != INVALID_HANDLE_VALUE);
    assert(WriteFile(file, block, 511, &written, NULL));
    CloseHandle(file);
    assert(check_image(&image_options) == INVALID_HANDLE_VALUE);
    assert(DeleteFileW(path));
    puts("picoctl Windows process, CLI and image tests passed");
    return 0;
}
