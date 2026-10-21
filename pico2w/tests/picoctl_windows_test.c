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
    wchar_t *listen[] = {L"picoctl", L"listen", L"--serial", L"7C4918BAB9492C77",
                         L"--psk-file", L"keys.txt", L"--count", L"3", L"--hex"};
    assert(parse(9, listen, &options));
    assert(options.count == 3 && options.hex && !options.has_build && !options.has_timeout &&
           !wcscmp(options.psk_file, L"keys.txt"));
    assert(parse(4, listen, &options) && !options.psk_file && !options.count);
    listen[6] = L"0";
    assert(!parse(8, listen, &options));
    listen[6] = L"3";
    listen[8] = L"--wait-ip";
    assert(!parse(9, listen, &options));
    wchar_t *listen_upload[] = {L"picoctl", L"listen", L"--serial", L"7C4918BAB9492C77",
                                L"--uf2", L"x.uf2"};
    assert(!parse(6, listen_upload, &options));
    wchar_t *listen_long[] = {L"picoctl", L"listen", L"--serial", L"7C4918BAB9492C77",
                              L"--timeout", L"86400"};
    assert(parse(6, listen_long, &options) && options.has_timeout && options.timeout == 86400000u);
    listen_long[5] = L"86401";
    assert(!parse(6, listen_long, &options));
    wchar_t *survey[] = {L"picoctl", L"survey", L"--serial", L"7C4918BAB9492C77",
                         L"--from", L"791", L"--to", L"821"};
    assert(parse(4, survey, &options) && options.from_mhz == 700 && options.to_mhz == 960);
    assert(parse(8, survey, &options) && options.from_mhz == 791 && options.to_mhz == 821);
    survey[5] = L"149";
    assert(!parse(8, survey, &options));
    survey[5] = L"900";
    assert(!parse(8, survey, &options)); /* from > to */
    survey[5] = L"821";
    assert(parse(8, survey, &options) && options.from_mhz == 821 && options.to_mhz == 821);
    survey[7] = L"961";
    assert(!parse(8, survey, &options));
    survey[6] = L"--hex";
    assert(!parse(7, survey, &options));
    survey[1] = L"listen";
    survey[6] = L"--to";
    survey[7] = L"900";
    assert(!parse(8, survey, &options)); /* --from/--to only for survey */
    survey[1] = L"survey";
    assert(!parse(3, survey, &options)); /* --serial required */
    listen_long[1] = L"status";
    listen_long[5] = L"301";
    assert(!parse(6, listen_long, &options));
    wchar_t *status_hex[] = {L"picoctl", L"status", L"--serial", L"7C4918BAB9492C77",
                             L"--build-id", L"d1828acd", L"--hex"};
    assert(!parse(7, status_hex, &options));
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
