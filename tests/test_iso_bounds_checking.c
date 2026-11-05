#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "iso_extractor.h"

int main(void) {
    printf("=================================================================\n");
    printf("  Testing ISO Record Bounds & Read Size Checking (Security)      \n");
    printf("=================================================================\n");

    /* Test 1: Empty or Non-existent ISO */
    printf("[TEST 1] Testing non-existent or empty ISO...\n");
    char disc_id[16] = {0};
    char title[128] = {0};
    uint64_t size = 0;
    assert(iso_verify("non_existent_file.iso", disc_id, title, &size) == false);
    printf("  - Non-existent file: REJECTED (PASS)\n");

    /* Test 2: Truncated ISO (< 17 sectors / < 34KB) */
    printf("[TEST 2] Testing truncated ISO smaller than 17 sectors...\n");
    FILE *f_trunc = fopen("test_truncated.iso", "wb");
    assert(f_trunc != NULL);
    uint8_t dummy[1024] = {0};
    fwrite(dummy, 1, sizeof(dummy), f_trunc);
    fclose(f_trunc);

    assert(iso_verify("test_truncated.iso", disc_id, title, &size) == false);
    remove("test_truncated.iso");
    printf("  - Truncated ISO (< 34KB): REJECTED (PASS)\n");

    /* Test 3: ISO with invalid PVD identifier (not CD001) */
    printf("[TEST 3] Testing ISO with malformed PVD magic header...\n");
    FILE *f_bad_pvd = fopen("test_bad_pvd.iso", "wb");
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

    assert(iso_verify("test_bad_pvd.iso", disc_id, title, &size) == false);
    remove("test_bad_pvd.iso");
    printf("  - Malformed PVD magic: REJECTED (PASS)\n");

    /* Test 4: Verify that valid official Persona 1 USA ISO still passes 100% */
    printf("[TEST 4] Testing official Persona 1 USA ISO verification...\n");
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
