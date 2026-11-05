#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <windows.h>
#include <assert.h>
#include "iso_extractor.h"

int main(void) {
    printf("=================================================================\n");
    printf("  Testing ISO Path Traversal & Filename Sanitization Security   \n");
    printf("=================================================================\n");

    /* Test 1: Malicious filename rejection */
    printf("[TEST 1] Testing is_safe_filename() with malicious payloads...\n");
    const char *malicious_filenames[] = {
        "..",
        ".",
        "../evil.exe",
        "..\\evil.exe",
        "folder/../../evil.exe",
        "folder\\..\\..\\evil.exe",
        "C:\\Windows\\system32\\cmd.exe",
        "D:/something",
        "evil.bin:stream",
        "bad*name.bin",
        "bad?name.bin",
        "bad\"name.bin",
        "bad<name.bin",
        "bad>name.bin",
        "bad|name.bin",
        "bad\x01name.bin",
        "bad\x1fname.bin",
        "bad\x7fname.bin",
        "..foo",
        "foo..bar",
        ""
    };

    size_t num_malicious = sizeof(malicious_filenames) / sizeof(malicious_filenames[0]);
    for (size_t i = 0; i < num_malicious; i++) {
        const char *payload = malicious_filenames[i];
        bool safe = is_safe_filename(payload);
        printf("  - Payload: '%-30s' -> %s\n", payload, safe ? "ALLOWED (FAIL!)" : "BLOCKED (PASS)");
        assert(!safe && "Malicious payload must be rejected!");
    }
    printf("[PASS] All malicious filenames successfully rejected!\n\n");

    /* Test 2: Valid filenames acceptance */
    printf("[TEST 2] Testing is_safe_filename() with legitimate ISO filenames...\n");
    const char *valid_filenames[] = {
        "EBOOT.BIN",
        "PARAM.SFO",
        "datapack.bin",
        "ICON0.PNG",
        "PIC1.PNG",
        "snd0.at3",
        "MOVIE01.PMF",
        "DATA0001.DAT",
        "A_B_C_123.TXT"
    };

    size_t num_valid = sizeof(valid_filenames) / sizeof(valid_filenames[0]);
    for (size_t i = 0; i < num_valid; i++) {
        const char *name = valid_filenames[i];
        bool safe = is_safe_filename(name);
        printf("  - Legitimate: '%-20s' -> %s\n", name, safe ? "ALLOWED (PASS)" : "BLOCKED (FAIL!)");
        assert(safe && "Legitimate filename must be accepted!");
    }
    printf("[PASS] All legitimate filenames successfully accepted!\n\n");

    /* Test 3: Path boundary enforcement (is_path_inside_dir) */
    printf("[TEST 3] Testing canonical path boundary verification...\n");
    const char *base = "game_root/disc/PSP_GAME";

    assert(is_path_inside_dir("game_root/disc/PSP_GAME/USRDIR/datapack.bin", base) == true);
    assert(is_path_inside_dir("game_root/disc/PSP_GAME/SYSDIR/EBOOT.BIN", base) == true);
    assert(is_path_inside_dir("game_root/disc/PSP_GAME/nested/deep/file.dat", base) == true);

    /* Traversal attempts escaping base dir */
    assert(is_path_inside_dir("game_root/disc/PSP_GAME/../../evil.exe", base) == false);
    assert(is_path_inside_dir("game_root/disc/PSP_GAME/../../../Windows/system32/cmd.exe", base) == false);
    assert(is_path_inside_dir("game_root/disc/PSP_GAME_EXTRA/evil.dat", base) == false);
    assert(is_path_inside_dir("C:/evil.dll", base) == false);

    printf("[PASS] Canonical path boundary verification passed!\n\n");
    printf("=================================================================\n");
    printf("  ALL SECURITY TESTS PASSED SUCCESSFULLY! (100%% PROTECTED)      \n");
    printf("=================================================================\n");
    return 0;
}

