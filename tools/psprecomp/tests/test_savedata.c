#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "psprecomp/mem.h"
#include "psprecomp/cpu.h"
#include "psprecomp/hle.h"

/* Forward declare registration from hle_custom.c */
void persona_register_custom_hle(void);

#define PSP_UTILITY_SAVEDATA_AUTOLOAD   0
#define PSP_UTILITY_SAVEDATA_AUTOSAVE   1
#define PSP_UTILITY_SAVEDATA_LOAD       2
#define PSP_UTILITY_SAVEDATA_SAVE       3
#define PSP_UTILITY_SAVEDATA_DELETE     7

#define NID_SAVEDATA_INIT_START     0x50C4CD57u
#define NID_SAVEDATA_GET_STATUS     0x8874DBE0u
#define NID_SAVEDATA_SHUTDOWN_START 0x9790B33Cu

static void call_savedata(uint32_t param_addr) {
    /* InitStart */
    psp_cpu.r[PSP_REG_A0] = param_addr;
    psp_hle_call(NID_SAVEDATA_INIT_START);

    /* GetStatus until FINISHED (3) */
    uint32_t stat = 0;
    for (int i = 0; i < 5; i++) {
        psp_hle_call(NID_SAVEDATA_GET_STATUS);
        stat = psp_cpu.r[PSP_REG_V0];
        if (stat == 3) break;
    }
    assert(stat == 3);

    /* ShutdownStart */
    psp_hle_call(NID_SAVEDATA_SHUTDOWN_START);

    /* GetStatus until NONE (0) */
    psp_hle_call(NID_SAVEDATA_GET_STATUS);
    assert(psp_cpu.r[PSP_REG_V0] == 0);
}

int main(void) {
    printf("[TEST] Testing Native Save/Load System (sceUtilitySavedata)...\n");

    /* 1. Init Memory & HLE */
    assert(psp_mem_init() == 0);
    assert(psp_mem_map_module(0x00000000u, 0x00600000u) == 0);
    persona_register_custom_hle();

    uint32_t param_addr = 0x08900000u;
    uint32_t data_addr  = 0x08910000u;

    /* -------------------------------------------------------------
     * TEST 1: AUTOSAVE to slot 0001
     * ------------------------------------------------------------- */
    printf("\n--- TEST 1: AUTOSAVE ---\n");
    memset(&psp_cpu, 0, sizeof(psp_cpu));

    /* Build synthetic payload in guest memory */
    uint8_t test_payload[512];
    for (int i = 0; i < 512; i++) {
        test_payload[i] = (uint8_t)(0xAA ^ (i & 0xFF));
    }
    psp_mem_write_block(data_addr, test_payload, sizeof(test_payload));

    /* Setup param struct at 0x08900000 */
    for (uint32_t i = 0; i < 0x600; i += 4) psp_write32(param_addr + i, 0);

    psp_write32(param_addr + 0x00, 0x600); /* size */
    psp_write32(param_addr + 0x30, PSP_UTILITY_SAVEDATA_AUTOSAVE); /* mode = 1 */

    /* gameName = "ULUS10432" */
    const char *gameName = "ULUS10432";
    for (int i = 0; gameName[i]; i++) psp_write8(param_addr + 0x3C + i, (uint8_t)gameName[i]);

    /* saveName = "0001" */
    const char *saveName = "0001";
    for (int i = 0; saveName[i]; i++) psp_write8(param_addr + 0x4C + i, (uint8_t)saveName[i]);

    /* fileName = "PERSONA.SAV" */
    const char *fileName = "PERSONA.SAV";
    for (int i = 0; fileName[i]; i++) psp_write8(param_addr + 0x64 + i, (uint8_t)fileName[i]);

    psp_write32(param_addr + 0x74, data_addr);           /* dataBuf */
    psp_write32(param_addr + 0x78, sizeof(test_payload)); /* dataBufSize */
    psp_write32(param_addr + 0x7C, sizeof(test_payload)); /* dataSize */

    /* Metadata SFO */
    const char *title = "Shin Megami Tensei: Persona";
    const char *stit  = "Slot 1 - Mikage-cho";
    const char *det   = "Naoya Toudou Lv 18 - 04:32:10";
    for (int i = 0; title[i]; i++) psp_write8(param_addr + 0x80 + i, (uint8_t)title[i]);
    for (int i = 0; stit[i]; i++)  psp_write8(param_addr + 0x100 + i, (uint8_t)stit[i]);
    for (int i = 0; det[i]; i++)   psp_write8(param_addr + 0x180 + i, (uint8_t)det[i]);

    call_savedata(param_addr);

    /* Verify result */
    uint32_t res = psp_read32(param_addr + 0x1C);
    assert(res == 0);
    printf("  [PASS] Autosave returned Result=0 (OK)\n");

    /* Verify physical file on disk */
    FILE *f = fopen("savedata/ULUS104320001/PERSONA.SAV", "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    assert(fsz == 512);

    uint8_t read_disk[512];
    fread(read_disk, 1, 512, f);
    fclose(f);
    assert(memcmp(test_payload, read_disk, 512) == 0);
    printf("  [PASS] Physical file savedata/ULUS104320001/PERSONA.SAV verified (512 bytes exact match)\n");

    /* Verify PARAM.SFO created */
    FILE *fsfo = fopen("savedata/ULUS104320001/PARAM.SFO", "rb");
    assert(fsfo != NULL);
    uint8_t sfo_magic[4];
    fread(sfo_magic, 1, 4, fsfo);
    fclose(fsfo);
    assert(sfo_magic[0] == 0x00 && sfo_magic[1] == 'P' && sfo_magic[2] == 'S' && sfo_magic[3] == 'F');
    printf("  [PASS] PSP-compliant PARAM.SFO verified (magic: \\0PSF)\n");

    /* -------------------------------------------------------------
     * TEST 2: AUTOLOAD from slot 0001
     * ------------------------------------------------------------- */
    printf("\n--- TEST 2: AUTOLOAD ---\n");
    /* Corrupt / clear guest buffer */
    uint8_t zero_payload[512] = {0};
    psp_mem_write_block(data_addr, zero_payload, sizeof(zero_payload));

    /* Set mode to AUTOLOAD (0) */
    psp_write32(param_addr + 0x30, PSP_UTILITY_SAVEDATA_AUTOLOAD);
    psp_write32(param_addr + 0x1C, 0xFFFFFFFFu); /* clear result */
    psp_write32(param_addr + 0x7C, 0);           /* clear dataSize */

    call_savedata(param_addr);

    res = psp_read32(param_addr + 0x1C);
    assert(res == 0);
    uint32_t loaded_size = psp_read32(param_addr + 0x7C);
    assert(loaded_size == 512);

    /* Verify guest memory matches original payload */
    uint8_t loaded_payload[512];
    psp_mem_read_block(loaded_payload, data_addr, 512);
    assert(memcmp(test_payload, loaded_payload, 512) == 0);
    printf("  [PASS] Autoload succeeded: 512 bytes loaded into 0x%08X with 100%% integrity\n", data_addr);

    /* -------------------------------------------------------------
     * TEST 3: PSP / PPSSPP Legacy Fallback (DATA.BIN compatibility)
     * ------------------------------------------------------------- */
    printf("\n--- TEST 3: PSP/PPSSPP DATA.BIN Fallback ---\n");
    CreateDirectoryA("savedata/ULUS104320002", NULL);
    FILE *f_legacy = fopen("savedata/ULUS104320002/DATA.BIN", "wb");
    assert(f_legacy != NULL);
    uint8_t legacy_data[256];
    for (int i = 0; i < 256; i++) legacy_data[i] = (uint8_t)(i + 1);
    fwrite(legacy_data, 1, 256, f_legacy);
    fclose(f_legacy);

    /* Request PERSONA.SAV from slot 0002, which only has DATA.BIN */
    psp_write32(param_addr + 0x30, PSP_UTILITY_SAVEDATA_LOAD);
    psp_write8(param_addr + 0x4C + 3, '2'); /* saveName = "0002" */
    psp_write32(param_addr + 0x7C, 0);

    call_savedata(param_addr);

    assert(psp_read32(param_addr + 0x1C) == 0);
    assert(psp_read32(param_addr + 0x7C) == 256);

    uint8_t read_legacy[256];
    psp_mem_read_block(read_legacy, data_addr, 256);
    assert(memcmp(legacy_data, read_legacy, 256) == 0);
    printf("  [PASS] Legacy PSP/PPSSPP save compatibility verified: DATA.BIN auto-resolved and loaded (256 bytes)\n");

    /* -------------------------------------------------------------
     * TEST 4: Non-existent slot returns NO_DATA (0x80110307)
     * ------------------------------------------------------------- */
    printf("\n--- TEST 4: Non-existent slot error handling ---\n");
    psp_write32(param_addr + 0x30, PSP_UTILITY_SAVEDATA_AUTOLOAD);
    const char *slot_9999 = "9999";
    for (int i = 0; slot_9999[i]; i++) psp_write8(param_addr + 0x4C + i, (uint8_t)slot_9999[i]);
    psp_write8(param_addr + 0x4C + 4, 0);

    call_savedata(param_addr);

    res = psp_read32(param_addr + 0x1C);
    assert(res == 0x80110307u);
    printf("  [PASS] Non-existent slot correctly returned SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA (0x80110307)\n");

    /* -------------------------------------------------------------
     * TEST 5: DELETE slot 0001
     * ------------------------------------------------------------- */
    printf("\n--- TEST 5: Savedata DELETE ---\n");
    psp_write32(param_addr + 0x30, PSP_UTILITY_SAVEDATA_DELETE);
    for (int i = 0; saveName[i]; i++) psp_write8(param_addr + 0x4C + i, (uint8_t)saveName[i]);

    call_savedata(param_addr);
    assert(psp_read32(param_addr + 0x1C) == 0);

    /* Verify files deleted */
    FILE *f_check = fopen("savedata/ULUS104320001/PERSONA.SAV", "rb");
    assert(f_check == NULL);
    printf("  [PASS] Savedata DELETE successfully wiped slot 0001 from disk\n");

    /* Cleanup slot 0002 */
    DeleteFileA("savedata/ULUS104320002/DATA.BIN");
    RemoveDirectoryA("savedata/ULUS104320002");

    printf("\n=======================================================\n");
    printf("  ALL SAVEDATA & PSP COMPATIBILITY TESTS PASSED! 100%%\n");
    printf("=======================================================\n");

    psp_mem_free();
    return 0;
}
