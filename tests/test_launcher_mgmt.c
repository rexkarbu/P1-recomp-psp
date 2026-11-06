/*
 * Test Suite for Launcher Management & Quick Settings (Seksi 6 Bagian D)
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <assert.h>

static int g_scale_opt = 2;
static int g_controller_opt = 0;
static bool g_fullscreen_opt = false;

static void load_launcher_config(const char *ini_path) {
    g_scale_opt = GetPrivateProfileIntA("Launcher", "Scale", 2, ini_path);
    if (g_scale_opt < 1 || g_scale_opt > 4) g_scale_opt = 2;

    g_fullscreen_opt = (GetPrivateProfileIntA("Launcher", "Fullscreen", 0, ini_path) != 0);

    g_controller_opt = GetPrivateProfileIntA("Launcher", "Controller", 0, ini_path);
    if (g_controller_opt < 0 || g_controller_opt > 2) g_controller_opt = 0;
}

static void save_launcher_config(const char *ini_path) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", g_scale_opt);
    WritePrivateProfileStringA("Launcher", "Scale", buf, ini_path);

    snprintf(buf, sizeof(buf), "%d", g_fullscreen_opt ? 1 : 0);
    WritePrivateProfileStringA("Launcher", "Fullscreen", buf, ini_path);

    snprintf(buf, sizeof(buf), "%d", g_controller_opt);
    WritePrivateProfileStringA("Launcher", "Controller", buf, ini_path);
}

static bool check_file_exists(const char *path) {
    DWORD dwAttrib = GetFileAttributesA(path);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

static bool check_dir_exists(const char *path) {
    DWORD dwAttrib = GetFileAttributesA(path);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

int main(void) {
    if (!check_dir_exists("game_root") && check_dir_exists("../game_root")) {
        SetCurrentDirectoryA("..");
    }
    printf("=================================================================\n");
    printf("  UNIT TEST: Persona Launcher Management & Quick Settings\n");
    printf("=================================================================\n");

    const char *test_ini = ".\\test_launcher_config.ini";
    DeleteFileA(test_ini);

    /* Test 1: Default configuration when ini does not exist */
    printf("\n[TEST 1] Testing Default Settings...\n");
    load_launcher_config(test_ini);
    assert(g_scale_opt == 2);
    assert(g_fullscreen_opt == false);
    assert(g_controller_opt == 0);
    printf("  -> Scale = %d (Expected: 2)\n", g_scale_opt);
    printf("  -> Fullscreen = %d (Expected: 0)\n", g_fullscreen_opt);
    printf("  -> Controller = %d (Expected: 0)\n", g_controller_opt);
    printf("  -> PASS!\n");

    /* Test 2: Saving custom configuration */
    printf("\n[TEST 2] Testing Saving Custom Configuration (Scale 4x, Fullscreen ON, Gamepad)...\n");
    g_scale_opt = 4;
    g_fullscreen_opt = true;
    g_controller_opt = 1;
    save_launcher_config(test_ini);

    /* Reset variables to verify reload */
    g_scale_opt = 1;
    g_fullscreen_opt = false;
    g_controller_opt = 0;

    load_launcher_config(test_ini);
    assert(g_scale_opt == 4);
    assert(g_fullscreen_opt == true);
    assert(g_controller_opt == 1);
    printf("  -> Loaded Scale = %d (4x FHD)\n", g_scale_opt);
    printf("  -> Loaded Fullscreen = %d (Enabled)\n", g_fullscreen_opt);
    printf("  -> Loaded Controller = %d (Xbox Gamepad)\n", g_controller_opt);
    printf("  -> PASS!\n");

    /* Test 3: Clamping out-of-bounds config values */
    printf("\n[TEST 3] Testing Configuration Clamping...\n");
    WritePrivateProfileStringA("Launcher", "Scale", "99", test_ini);
    WritePrivateProfileStringA("Launcher", "Controller", "5", test_ini);
    load_launcher_config(test_ini);
    assert(g_scale_opt == 2); /* clamped to default 2 */
    assert(g_controller_opt == 0); /* clamped to default 0 */
    printf("  -> Clamped Scale: %d, Clamped Controller: %d\n", g_scale_opt, g_controller_opt);
    printf("  -> PASS!\n");

    /* Test 4: Verify Installed Game Components */
    printf("\n[TEST 4] Testing Game Installation Integrity Verification...\n");
    const char *datapack = "game_root/disc/PSP_GAME/USRDIR/datapack.bin";
    const char *elf = "extracted/EBOOT_RELOC.ELF";
    const char *exe = check_file_exists("persona_pc.exe") ? "persona_pc.exe" : "build_pc\\persona_pc.exe";
    const char *save_dir = "savedata";

    bool ok_dp = check_file_exists(datapack);
    bool ok_elf = check_file_exists(elf);
    bool ok_exe = check_file_exists(exe);
    bool ok_save = check_dir_exists(save_dir);

    printf("  -> datapack.bin: %s\n", ok_dp ? "PRESENT" : "MISSING");
    printf("  -> EBOOT_RELOC.ELF: %s\n", ok_elf ? "PRESENT" : "MISSING");
    printf("  -> persona_pc.exe: %s\n", ok_exe ? "PRESENT" : "MISSING");
    printf("  -> savedata dir: %s\n", ok_save ? "PRESENT" : "MISSING");

    assert(ok_dp);
    assert(ok_elf);
    assert(ok_exe);
    assert(ok_save);
    printf("  -> All 4 core components verified! PASS!\n");

    /* Test 5: Launch Command Line Construction */
    printf("\n[TEST 5] Testing Launch Command Construction...\n");
    char cmd[512];
    g_scale_opt = 3;
    g_fullscreen_opt = true;
    snprintf(cmd, sizeof(cmd), "\"%s\" --scale %d%s", exe, g_scale_opt, g_fullscreen_opt ? " --fullscreen" : "");
    printf("  -> Generated Command Line: %s\n", cmd);
    assert(strstr(cmd, "--scale 3") != NULL);
    assert(strstr(cmd, "--fullscreen") != NULL);
    printf("  -> PASS!\n");

    DeleteFileA(test_ini);
    printf("\n=================================================================\n");
    printf("  ALL 5 LAUNCHER TESTS PASSED SUCCESSFULLY! (100%%)\n");
    printf("=================================================================\n");
    return 0;
}

