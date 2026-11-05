#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <windows.h>
#include "psprecomp/overlay.h"
#include "psprecomp/mem.h"
#include "psprecomp/cpu.h"
#include "psprecomp/hle.h"

int main(void) {
    printf("[TEST] Testing In-Game Settings Overlay System...\n");

    /* 1. Inisialisasi memori PSP */
    assert(psp_mem_init() == 0);
    assert(psp_mem_map_module(0x00000000u, 0x00600000u) == 0);
    psp_overlay_init();

    /* 2. Menu open / close toggle */
    assert(psp_overlay_is_open() == 0);
    assert(psp_overlay_handle_key(VK_F1) == 1);
    assert(psp_overlay_is_open() == 1);
    printf("  [PASS] F1 Toggles Menu Open (open = %d)\n", psp_overlay_is_open());

    /* 3. Tab switching */
    assert(g_overlay.current_tab == TAB_CONTROLS);
    psp_overlay_handle_key(VK_TAB);
    assert(g_overlay.current_tab == TAB_VIDEO);
    psp_overlay_handle_key(VK_TAB);
    assert(g_overlay.current_tab == TAB_AUDIO);
    printf("  [PASS] Tab switching (now on TAB_AUDIO = %d)\n", g_overlay.current_tab);

    /* 4. Audio volume adjustment */
    assert(g_overlay.volume == 100);
    /* Left arrow decreases volume by 5% */
    psp_overlay_handle_key(VK_LEFT);
    assert(g_overlay.volume == 95);
    assert(psp_audio_get_master_volume() >= 0.94f && psp_audio_get_master_volume() <= 0.96f);
    printf("  [PASS] Audio volume slider: %d%% (master volume = %.2f)\n",
           g_overlay.volume, psp_audio_get_master_volume());

    /* 5. Cheats Tab */
    psp_overlay_handle_key(VK_TAB);
    assert(g_overlay.current_tab == TAB_CHEATS);
    /* Toggle Max Money */
    g_overlay.selected_row = 1;
    psp_overlay_handle_key(VK_RETURN);
    assert(g_overlay.cheat_max_money == 1);
    psp_overlay_tick();
    /* Verifikasi memory guest di 0x004C6200 */
    uint32_t money_val = psp_read32(0x004C6200u);
    assert(money_val == 0x05F5E0FFu);
    printf("  [PASS] Cheat Max Money: Memory at 0x004C6200 = 0x%08X (99,999,999 Yen!)\n", money_val);

    /* 6. Save State and Load State */
    psp_cpu.r[PSP_REG_V0] = 0xCAFEBABE;
    psp_write32(0x08C00000u, 0x12345678);
    psp_overlay_save_state(1);
    printf("  [PASS] Save State slot 1 created\n");

    /* Modify state */
    psp_cpu.r[PSP_REG_V0] = 0x00000000;
    psp_write32(0x08C00000u, 0x00000000);
    assert(psp_cpu.r[PSP_REG_V0] == 0);
    assert(psp_read32(0x08C00000u) == 0);

    /* Load state back */
    psp_overlay_load_state(1);
    assert(psp_cpu.r[PSP_REG_V0] == 0xCAFEBABE);
    assert(psp_read32(0x08C00000u) == 0x12345678);
    printf("  [PASS] Load State restored CPU ($v0=0x%08X) and RAM (0x%08X)\n",
           psp_cpu.r[PSP_REG_V0], psp_read32(0x08C00000u));

    /* 7. Hotkey F4 Show FPS */
    assert(g_overlay.show_fps == 0);
    psp_overlay_handle_key(VK_F4);
    assert(g_overlay.show_fps == 1);
    printf("  [PASS] F4 toggles Show FPS (show_fps = %d)\n", g_overlay.show_fps);

    /* 8. Hotkey F2 Screenshot */
    psp_display_set_framebuffer(0x04000000u, 512, 3);
    psp_overlay_handle_key(VK_F2);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("screenshots/persona_screenshot_*.bmp", &fd);
    assert(hFind != INVALID_HANDLE_VALUE);
    FindClose(hFind);
    printf("  [PASS] F2 saved screenshot file: screenshots/%s\n", fd.cFileName);

    /* 9. Close Menu */
    psp_overlay_handle_key(VK_ESCAPE);
    assert(psp_overlay_is_open() == 0);
    printf("  [PASS] Esc closes menu (open = %d)\n", psp_overlay_is_open());

    psp_mem_free();
    printf("\n=== ALL OVERLAY FEATURES PASSED VERIFICATION! ===\n");
    return 0;
}
