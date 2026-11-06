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

    /* Test 4: End-to-end extract_iso_dir rejection of traversal payload */
    printf("[TEST 4] Testing production extract_iso_dir() with synthetic traversal ISO...\n");
    char orig_cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, orig_cwd);
    CreateDirectoryA("test_sandbox_traversal", NULL);
    assert(SetCurrentDirectoryA("test_sandbox_traversal") != 0);

    /* Create synthetic 64KB ISO with malicious directory record pointing to ../evil.exe */
    uint8_t iso_buf[65536] = {0};
    uint8_t *dir = iso_buf + (16 * 2048);

    /* Record 0: Root "." */
    dir[0] = 34; dir[25] = 2; dir[32] = 1; dir[33] = 0;
    /* Record 1: Parent ".." */
    dir[34] = 34; dir[34+25] = 2; dir[34+32] = 1; dir[34+33] = 1;

    /* Record 2: Malicious traversal file "../evil.exe;1" */
    uint8_t *mal_rec = dir + 68;
    const char *mal_name = "../evil.exe;1";
    uint8_t name_len = (uint8_t)strlen(mal_name);
    uint8_t rec_len = (uint8_t)((33 + name_len + 1) & ~1);
    mal_rec[0] = rec_len; /* length */
    *(uint32_t *)(mal_rec + 2) = 20; /* extent_le (LBA 20) */
    *(uint32_t *)(mal_rec + 10) = 100; /* size_le (100 bytes) */
    mal_rec[25] = 0; /* regular file */
    mal_rec[32] = name_len;
    memcpy(mal_rec + 33, mal_name, name_len);

    FILE *f_syn = fopen("synth_traversal.iso", "wb");
    assert(f_syn != NULL);
    fwrite(iso_buf, 1, sizeof(iso_buf), f_syn);
    fclose(f_syn);

    f_syn = fopen("synth_traversal.iso", "rb");
    assert(f_syn != NULL);
    uint64_t bytes = 0;
    uint32_t errors = 0;
    extract_iso_dir(f_syn, 16, 2048, "out_dest", &bytes, 2048, sizeof(iso_buf), &errors, NULL);
    fclose(f_syn);

    printf("  - Traversal entries detected by extract_iso_dir: %u error(s) logged\n", errors);
    assert(errors > 0 && "extract_iso_dir must count traversal entry as error!");
    assert(file_exists("evil.exe") == false && "evil.exe must not be written outside dest_dir!");
    assert(file_exists("../evil.exe") == false && "evil.exe must not escape directory!");
    assert(file_exists("out_dest/evil.exe") == false && "evil.exe must not be written!");

    DeleteFileA("synth_traversal.iso");
    RemoveDirectoryA("out_dest");
    SetCurrentDirectoryA(orig_cwd);
    RemoveDirectoryA("test_sandbox_traversal");

    printf("[PASS] extract_iso_dir safely blocked traversal payload!\n\n");
    printf("=================================================================\n");
    printf("  ALL SECURITY TESTS PASSED SUCCESSFULLY! (100%% PROTECTED)      \n");
    printf("=================================================================\n");
    return 0;
}

