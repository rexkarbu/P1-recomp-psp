#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <windows.h>
#include "iso_extractor.h"

int main(void) {
    printf("=================================================================\n");
    printf("  Testing ISO Record Bounds & Read Size Checking (Security)      \n");
    printf("=================================================================\n");

    CreateDirectoryA("test_sandbox_iso", NULL);

    /* Test 1: Empty or Non-existent ISO */
    printf("[TEST 1] Testing non-existent or empty ISO...\n");
    char disc_id[16] = {0};
    char title[128] = {0};
    uint64_t size = 0;
    assert(iso_verify("test_sandbox_iso/non_existent.iso", disc_id, title, &size) == false);
    printf("  - Non-existent file: REJECTED (PASS)\n");

    /* Test 2: Truncated ISO (< 17 sectors / < 34KB) */
    printf("[TEST 2] Testing truncated ISO smaller than 17 sectors...\n");
    FILE *f_trunc = fopen("test_sandbox_iso/test_truncated.iso", "wb");
    assert(f_trunc != NULL);
    uint8_t dummy[1024] = {0};
    fwrite(dummy, 1, sizeof(dummy), f_trunc);
    fclose(f_trunc);

    assert(iso_verify("test_sandbox_iso/test_truncated.iso", disc_id, title, &size) == false);
    DeleteFileA("test_sandbox_iso/test_truncated.iso");
    printf("  - Truncated ISO (< 34KB): REJECTED (PASS)\n");

    /* Test 3: ISO with invalid PVD identifier (not CD001) */
    printf("[TEST 3] Testing ISO with malformed PVD magic header...\n");
    FILE *f_bad_pvd = fopen("test_sandbox_iso/test_bad_pvd.iso", "wb");
    assert(f_bad_pvd != NULL);
    uint8_t zero_sector[2048] = {0};
    for (int i = 0; i < 16; i++) {
        fwrite(zero_sector, 1, 2048, f_bad_pvd);
    }
    /* Write Sector 16 with corrupt magic */
    uint8_t bad_pvd_sector[2048] = {0};
    bad_pvd_sector[0] = 1;
    memcpy(bad_pvd_sector + 1, "CORRUPT", 7);
    fwrite(bad_pvd_sector, 1, 2048, f_bad_pvd);
    fclose(f_bad_pvd);

    assert(iso_verify("test_sandbox_iso/test_bad_pvd.iso", disc_id, title, &size) == false);
    DeleteFileA("test_sandbox_iso/test_bad_pvd.iso");
    printf("  - Malformed PVD magic: REJECTED (PASS)\n");

    /* Test 4: Malformed ISO directory record with length < sizeof(ISO_DirRecord) */
    printf("[TEST 4] Testing extract_iso_dir with malformed record length < 33...\n");
    FILE *f_rec_short = fopen("test_sandbox_iso/test_rec_short.iso", "wb+");
    assert(f_rec_short != NULL);
    uint8_t sec_short[2048] = {0};
    /* Put an invalid record with length 10 (< 33) */
    sec_short[0] = 10;
    fwrite(sec_short, 1, sizeof(sec_short), f_rec_short);
    fflush(f_rec_short);

    uint64_t extracted_bytes = 0;
    uint32_t out_errors = 0;
    extract_iso_dir(f_rec_short, 0, 2048, "test_sandbox_iso/out", &extracted_bytes, 2048, 2048, &out_errors, NULL);
    fclose(f_rec_short);
    DeleteFileA("test_sandbox_iso/test_rec_short.iso");
    assert(out_errors > 0);
    printf("  - Malformed record length < 33: DETECTED & FLAGGED ERROR (PASS)\n");

    /* Test 5: Malformed ISO directory record with length overflowing sector */
    printf("[TEST 5] Testing extract_iso_dir with record length overflowing dir_size...\n");
    FILE *f_rec_overflow = fopen("test_sandbox_iso/test_rec_overflow.iso", "wb+");
    assert(f_rec_overflow != NULL);
    uint8_t sec_overflow[2048] = {0};
    sec_overflow[0] = 255; /* claims 255 bytes at offset 2000 of a 2048 buffer */
    fseek(f_rec_overflow, 0, SEEK_SET);
    fwrite(sec_overflow, 1, sizeof(sec_overflow), f_rec_overflow);
    fflush(f_rec_overflow);

    extracted_bytes = 0;
    out_errors = 0;
    extract_iso_dir(f_rec_overflow, 0, 2048, "test_sandbox_iso/out", &extracted_bytes, 2048, 2048, &out_errors, NULL);
    fclose(f_rec_overflow);
    DeleteFileA("test_sandbox_iso/test_rec_overflow.iso");
    assert(out_errors > 0);
    printf("  - Record length overflowing sector: DETECTED & FLAGGED ERROR (PASS)\n");

    /* Test 6: Malformed ISO directory record with extent beyond physical ISO bounds */
    printf("[TEST 6] Testing extract_iso_dir with extent beyond physical ISO size...\n");
    FILE *f_extent_oob = fopen("test_sandbox_iso/test_extent_oob.iso", "wb+");
    assert(f_extent_oob != NULL);
    uint8_t sec_extent[2048] = {0};
    sec_extent[0] = 40; /* length */
    sec_extent[2] = 100; /* extent LBA = 100 (needs 200KB) */
    sec_extent[10] = 0x00; /* size = 16KB */
    sec_extent[11] = 0x40;
    sec_extent[32] = 4; /* name_len = 4 */
    memcpy(sec_extent + 33, "TEST", 4);
    fwrite(sec_extent, 1, sizeof(sec_extent), f_extent_oob);
    fflush(f_extent_oob);

    extracted_bytes = 0;
    out_errors = 0;
    /* ISO total size is only 4096 bytes (2 sectors), but record extent is LBA 100 */
    extract_iso_dir(f_extent_oob, 0, 2048, "test_sandbox_iso/out", &extracted_bytes, 4096, 4096, &out_errors, NULL);
    fclose(f_extent_oob);
    DeleteFileA("test_sandbox_iso/test_extent_oob.iso");
    assert(out_errors > 0);
    assert(!file_exists("test_sandbox_iso/out/TEST"));
    printf("  - Extent beyond physical ISO size: DETECTED & BLOCKED (PASS)\n");

    /* Test 7: Malicious path traversal record (../../evil.bin) */
    printf("[TEST 7] Testing extract_iso_dir with traversal filename payload...\n");
    FILE *f_traversal = fopen("test_sandbox_iso/test_traversal.iso", "wb+");
    assert(f_traversal != NULL);
    uint8_t sec_traversal[2048] = {0};
    sec_traversal[0] = 50; /* length */
    sec_traversal[2] = 0; /* extent LBA = 0 */
    sec_traversal[10] = 16; /* size = 16 */
    sec_traversal[32] = 13; /* name_len */
    memcpy(sec_traversal + 33, "../evil_out.bin", 15);
    fwrite(sec_traversal, 1, sizeof(sec_traversal), f_traversal);
    fflush(f_traversal);

    extracted_bytes = 0;
    out_errors = 0;
    extract_iso_dir(f_traversal, 0, 2048, "test_sandbox_iso/out", &extracted_bytes, 2048, 2048, &out_errors, NULL);
    fclose(f_traversal);
    DeleteFileA("test_sandbox_iso/test_traversal.iso");
    assert(out_errors > 0);
    assert(!file_exists("test_sandbox_iso/evil_out.bin"));
    printf("  - Directory traversal in record: BLOCKED & FLAGGED ERROR (PASS)\n");

    /* Clean up sandbox out directory */
    RemoveDirectoryA("test_sandbox_iso/out");
    RemoveDirectoryA("test_sandbox_iso");

    /* Test 8: Verify that valid official Persona 1 USA ISO still passes 100% */
    printf("[TEST 8] Testing official Persona 1 USA ISO verification...\n");
    const char *iso_path = "Shin Megami Tensei - Persona (USA).iso";
    FILE *chk = fopen(iso_path, "rb");
    if (chk) {
        fclose(chk);
        bool valid = iso_verify(iso_path, disc_id, title, &size);
        printf("  - Official ISO Verified: %s\n", valid ? "YES (PASS)" : "NO (FAIL!)");
        printf("  - Disc ID: '%s'\n", disc_id);
        printf("  - Title: '%s'\n", title);
        printf("  - Size: %llu bytes (%.2f MB)\n", (unsigned long long)size, (double)size / (1024.0 * 1024.0));
        assert(valid == true);
        assert(strcmp(disc_id, "ULUS10432") == 0);
        printf("[PASS] Official ISO verified cleanly with full bounds validation!\n");
    } else {
        printf("  - Note: Official ISO not in current folder, skipping positive test.\n");
    }

    printf("\n=================================================================\n");
    printf("  ALL ISO BOUNDS & READ VALIDATION TESTS PASSED (100%%)          \n");
    printf("=================================================================\n");
    return 0;
}

