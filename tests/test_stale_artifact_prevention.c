#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <windows.h>
#include "iso_extractor.h"

static void create_dummy_file(const char *path, const char *content, size_t len) {
    safe_remove_file(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s': ", path);
        perror("");
    }
    assert(f != NULL);
    if (len > 0) {
        fwrite(content, 1, len, f);
    }
    fclose(f);
}

int main(void) {
    printf("=================================================================\n");
    printf("  Testing Stale Artifact Prevention & Installation Integrity     \n");
    printf("=================================================================\n");

    /* Backup existing user files if present so tests leave workspace clean */
    bool had_manifest = file_exists("install_manifest.txt");
    bool had_eboot_bin = file_exists("extracted/EBOOT.BIN");
    bool had_eboot_elf = file_exists("extracted/EBOOT.ELF");
    bool had_eboot_reloc = file_exists("extracted/EBOOT_RELOC.ELF");

    if (had_manifest) CopyFileA("install_manifest.txt", "install_manifest.txt.bak", FALSE);
    if (had_eboot_bin) CopyFileA("extracted/EBOOT.BIN", "extracted/EBOOT.BIN.bak", FALSE);
    if (had_eboot_elf) CopyFileA("extracted/EBOOT.ELF", "extracted/EBOOT.ELF.bak", FALSE);
    if (had_eboot_reloc) CopyFileA("extracted/EBOOT_RELOC.ELF", "extracted/EBOOT_RELOC.ELF.bak", FALSE);

    /* ------------------------------------------------------------- */
    /* TEST 1: validate_elf_file Integrity Logic                     */
    /* ------------------------------------------------------------- */
    printf("[TEST 1] Testing validate_elf_file...\n");
    CreateDirectoryA("test_tmp", NULL);

    assert(validate_elf_file("test_tmp/non_existent.elf", 1024) == false);
    printf("  - Non-existent file: REJECTED (PASS)\n");

    /* Truncated file (< min_size) */
    create_dummy_file("test_tmp/short.elf", "\x7F" "ELF_truncated", 14);
    assert(validate_elf_file("test_tmp/short.elf", 1024) == false);
    printf("  - Truncated file (< 1024 bytes): REJECTED (PASS)\n");

    /* Corrupted magic header */
    uint8_t dummy_1k[1024] = {0};
    memcpy(dummy_1k, "NOT_AN_ELF_BINARY!", 18);
    create_dummy_file("test_tmp/corrupt_header.elf", (char *)dummy_1k, sizeof(dummy_1k));
    assert(validate_elf_file("test_tmp/corrupt_header.elf", 1024) == false);
    printf("  - Corrupted magic header: REJECTED (PASS)\n");

    /* Valid ELF magic and size >= 1024 */
    dummy_1k[0] = 0x7F;
    dummy_1k[1] = 'E';
    dummy_1k[2] = 'L';
    dummy_1k[3] = 'F';
    create_dummy_file("test_tmp/valid_magic.elf", (char *)dummy_1k, sizeof(dummy_1k));
    assert(validate_elf_file("test_tmp/valid_magic.elf", 1024) == true);
    printf("  - Valid ELF header & size >= 1024: ACCEPTED (PASS)\n");

    /* ------------------------------------------------------------- */
    /* TEST 2: Purge Stale Artifacts on New Installation Attempt     */
    /* ------------------------------------------------------------- */
    printf("[TEST 2] Testing stale artifact purge on failed or new install...\n");
    CreateDirectoryA("extracted", NULL);

    /* Plant mock stale artifacts */
    create_dummy_file("install_manifest.txt", "STALE_OLD_MANIFEST", 18);
    create_dummy_file("extracted/EBOOT.BIN", "STALE_EBOOT_BIN", 15);
    create_dummy_file("extracted/EBOOT.ELF", "STALE_EBOOT_ELF", 15);
    create_dummy_file("extracted/EBOOT_RELOC.ELF", "STALE_EBOOT_RELOC", 17);

    assert(file_exists("install_manifest.txt") == true);
    assert(file_exists("extracted/EBOOT.BIN") == true);
    assert(file_exists("extracted/EBOOT.ELF") == true);
    assert(file_exists("extracted/EBOOT_RELOC.ELF") == true);

    /* Attempt installation with a fake/non-existent ISO */
    bool install_res = iso_run_installation("fake_corrupt_nonexistent.iso", NULL);
    assert(install_res == false);
    printf("  - Installation of invalid ISO returned failure: (PASS)\n");

    /* Ensure all stale artifacts were purged and NOT left on disk */
    assert(file_exists("install_manifest.txt") == false);
    assert(file_exists("extracted/EBOOT.BIN") == false);
    assert(file_exists("extracted/EBOOT.ELF") == false);
    assert(file_exists("extracted/EBOOT_RELOC.ELF") == false);
    printf("  - All stale artifacts purged completely: (PASS)\n");

    /* ------------------------------------------------------------- */
    /* TEST 3: iso_is_game_installed Requires Valid Manifest         */
    /* ------------------------------------------------------------- */
    printf("[TEST 3] Testing iso_is_game_installed manifest enforcement...\n");
    /* Manifest is currently deleted */
    assert(iso_is_game_installed() == false);
    printf("  - Game not marked as installed when install_manifest.txt is absent: (PASS)\n");

    /* ------------------------------------------------------------- */
    /* TEST 4: PRX Relocator Rejection on Non-ELF Input              */
    /* ------------------------------------------------------------- */
    printf("[TEST 4] Testing PRX relocator rejection of corrupt binary...\n");
    int prx_res = prx_relocate_file("test_tmp/corrupt_header.elf", "test_tmp/out.elf");
    assert(prx_res != 0);
    assert(file_exists("test_tmp/out.elf") == false);
    printf("  - PRX Relocator safely rejected invalid binary without generating output: (PASS)\n");

    /* Cleanup temporary test files */
    DeleteFileA("test_tmp/short.elf");
    DeleteFileA("test_tmp/corrupt_header.elf");
    DeleteFileA("test_tmp/valid_magic.elf");
    RemoveDirectoryA("test_tmp");

    /* Restore backed-up files */
    if (had_manifest) {
        CopyFileA("install_manifest.txt.bak", "install_manifest.txt", FALSE);
        DeleteFileA("install_manifest.txt.bak");
    }
    if (had_eboot_bin) {
        CopyFileA("extracted/EBOOT.BIN.bak", "extracted/EBOOT.BIN", FALSE);
        DeleteFileA("extracted/EBOOT.BIN.bak");
    }
    if (had_eboot_elf) {
        CopyFileA("extracted/EBOOT.ELF.bak", "extracted/EBOOT.ELF", FALSE);
        DeleteFileA("extracted/EBOOT.ELF.bak");
    }
    if (had_eboot_reloc) {
        CopyFileA("extracted/EBOOT_RELOC.ELF.bak", "extracted/EBOOT_RELOC.ELF", FALSE);
        DeleteFileA("extracted/EBOOT_RELOC.ELF.bak");
    }

    printf("\n=================================================================\n");
    printf("  ALL STALE ARTIFACT PREVENTION TESTS PASSED (100%%)              \n");
    printf("=================================================================\n");
    return 0;
}
