#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <windows.h>
#include "iso_extractor.h"

static void create_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    if (len > 0 && data) {
        fwrite(data, 1, len, f);
    }
    fclose(f);
}

static void recursive_rm(const char *dir) {
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s/*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        RemoveDirectoryA(dir);
        return;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char sub[MAX_PATH];
        snprintf(sub, sizeof(sub), "%s/%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            recursive_rm(sub);
        } else {
            SetFileAttributesA(sub, FILE_ATTRIBUTE_NORMAL);
            DeleteFileA(sub);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    RemoveDirectoryA(dir);
}

int main(void) {
    printf("=================================================================\n");
    printf("  Testing Stale Artifact Prevention & Comprehensive ELF Bounds   \n");
    printf("=================================================================\n");

    /* Save original CWD and switch to an isolated sandbox directory */
    char original_cwd[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, original_cwd);

    const char *sandbox_name = "test_sandbox_stale";
    recursive_rm(sandbox_name);
    CreateDirectoryA(sandbox_name, NULL);
    assert(SetCurrentDirectoryA(sandbox_name) != 0);

    /* ------------------------------------------------------------- */
    /* TEST 1: validate_elf_file Comprehensive Integrity Logic       */
    /* ------------------------------------------------------------- */
    printf("[TEST 1] Testing validate_elf_file against corrupted and valid ELFs...\n");

    /* Non-existent file */
    assert(validate_elf_file("non_existent.elf", 52) == false);
    printf("  - Non-existent file: REJECTED (PASS)\n");

    /* Truncated file (< 52 bytes) */
    create_file("short.elf", "\x7F" "ELF_truncated", 14);
    assert(validate_elf_file("short.elf", 52) == false);
    printf("  - Truncated file (< 52 bytes): REJECTED (PASS)\n");

    /* Corrupted magic header */
    uint8_t bad_magic[64] = {0};
    memcpy(bad_magic, "NOT_AN_ELF_BINARY!", 18);
    create_file("bad_magic.elf", bad_magic, sizeof(bad_magic));
    assert(validate_elf_file("bad_magic.elf", 52) == false);
    printf("  - Corrupted magic header: REJECTED (PASS)\n");

    /* ELF with corrupted Program Header offset (phoff beyond file size) */
    uint8_t bad_phoff[128] = {0};
    memcpy(bad_phoff, "\x7F" "ELF\x01\x01", 6); /* ELFCLASS32, ELFDATA2LSB */
    bad_phoff[40] = 52; /* ehsize */
    bad_phoff[42] = 32; /* phentsize */
    bad_phoff[44] = 2;  /* phnum */
    *(uint32_t *)(bad_phoff + 28) = 5000; /* phoff = 5000 (file is only 128 bytes) */
    create_file("bad_phoff.elf", bad_phoff, sizeof(bad_phoff));
    assert(validate_elf_file("bad_phoff.elf", 52) == false);
    printf("  - phoff beyond file size: REJECTED (PASS)\n");

    /* ELF with corrupted Section Header offset (shoff beyond file size) */
    uint8_t bad_shoff[128] = {0};
    memcpy(bad_shoff, "\x7F" "ELF\x01\x01", 6);
    bad_shoff[40] = 52;
    bad_shoff[46] = 40; /* shentsize */
    bad_shoff[48] = 2;  /* shnum */
    *(uint32_t *)(bad_shoff + 32) = 5000; /* shoff = 5000 */
    create_file("bad_shoff.elf", bad_shoff, sizeof(bad_shoff));
    assert(validate_elf_file("bad_shoff.elf", 52) == false);
    printf("  - shoff beyond file size: REJECTED (PASS)\n");

    /* Valid minimal ELF32 header with valid phoff/shoff bounds */
    uint8_t valid_elf[128] = {0};
    memcpy(valid_elf, "\x7F" "ELF\x01\x01", 6);
    valid_elf[40] = 52; /* ehsize */
    valid_elf[42] = 32; /* phentsize */
    valid_elf[44] = 1;  /* phnum = 1 */
    *(uint32_t *)(valid_elf + 28) = 52; /* phoff at byte 52 */
    /* Phdr at byte 52: p_type = 1 (PT_LOAD), p_offset = 0, p_filesz = 52 */
    *(uint32_t *)(valid_elf + 52 + 0) = 1;  /* p_type = PT_LOAD */
    *(uint32_t *)(valid_elf + 52 + 4) = 0;  /* p_offset = 0 */
    *(uint32_t *)(valid_elf + 52 + 16) = 52; /* p_filesz = 52 */
    create_file("valid.elf", valid_elf, sizeof(valid_elf));
    assert(validate_elf_file("valid.elf", 52) == true);
    printf("  - Valid ELF32 with verified table bounds: ACCEPTED (PASS)\n");

    /* ------------------------------------------------------------- */
    /* TEST 2: prx_relocate_file Relocation Bounds Checking          */
    /* ------------------------------------------------------------- */
    printf("\n[TEST 2] Testing prx_relocate_file against corrupted tables & indices...\n");

    /* Test on corrupted magic */
    assert(prx_relocate_file("bad_magic.elf", "out.elf") == 2);
    assert(!file_exists("out.elf"));
    printf("  - Rejected bad magic without creating output: (PASS)\n");

    /* Test on corrupted phoff */
    assert(prx_relocate_file("bad_phoff.elf", "out.elf") == 2);
    assert(!file_exists("out.elf"));
    printf("  - Rejected bad phoff without creating output: (PASS)\n");

    /* Test on corrupted shoff */
    assert(prx_relocate_file("bad_shoff.elf", "out.elf") == 2);
    assert(!file_exists("out.elf"));
    printf("  - Rejected bad shoff without creating output: (PASS)\n");

    /* Test on relocation with out-of-bounds segment index */
    uint8_t bad_seg_elf[256] = {0};
    memcpy(bad_seg_elf, "\x7F" "ELF\x01\x01", 6);
    bad_seg_elf[40] = 52;
    bad_seg_elf[42] = 32;
    bad_seg_elf[44] = 1;
    *(uint32_t *)(bad_seg_elf + 28) = 52; /* phoff */
    *(uint32_t *)(bad_seg_elf + 52 + 0) = 1; /* PT_LOAD */
    *(uint32_t *)(bad_seg_elf + 52 + 4) = 0;
    *(uint32_t *)(bad_seg_elf + 52 + 16) = 100;
    /* Section header at byte 84 */
    bad_seg_elf[46] = 40; /* shentsize */
    bad_seg_elf[48] = 1;  /* shnum */
    *(uint32_t *)(bad_seg_elf + 32) = 84; /* shoff */
    *(uint32_t *)(bad_seg_elf + 84 + 4) = 9; /* SHT_REL */
    *(uint32_t *)(bad_seg_elf + 84 + 16) = 124; /* rel_off */
    *(uint32_t *)(bad_seg_elf + 84 + 20) = 8;   /* rel_size (1 entry) */
    /* Relocation entry at byte 124: ofs_base = 5 (but only 1 segment exists, so nseg=1!) */
    *(uint32_t *)(bad_seg_elf + 124 + 0) = 0; /* r_offset */
    *(uint32_t *)(bad_seg_elf + 124 + 4) = (5 << 8) | 2; /* R_MIPS_32, ofs_base=5 */
    create_file("bad_seg.elf", bad_seg_elf, sizeof(bad_seg_elf));
    assert(prx_relocate_file("bad_seg.elf", "out.elf") == 2);
    assert(!file_exists("out.elf"));
    printf("  - Out-of-bounds segment index: REJECTED SAFELY (PASS)\n");

    /* ------------------------------------------------------------- */
    /* TEST 3: Staged Installation & Stale Artifact Isolation        */
    /* ------------------------------------------------------------- */
    printf("\n[TEST 3] Testing that failed installation leaves zero artifacts...\n");

    /* Run installation on a non-existent ISO in this sandbox */
    bool res = iso_run_installation("non_existent_fake.iso", NULL);
    assert(res == false);

    /* Verify that install_manifest.txt was NOT created */
    assert(file_exists("install_manifest.txt") == false);
    assert(file_exists("staging_install") == false);
    assert(iso_is_game_installed() == false);
    printf("  - Failed installation safely aborted in staging with zero residue: (PASS)\n");

    /* Switch back to original CWD and clean up the sandbox directory */
    SetCurrentDirectoryA(original_cwd);
    recursive_rm(sandbox_name);

    printf("\n=================================================================\n");
    printf("  ALL STALE ARTIFACT & ELF BOUNDS TESTS PASSED (100%%)            \n");
    printf("=================================================================\n");
    return 0;
}
