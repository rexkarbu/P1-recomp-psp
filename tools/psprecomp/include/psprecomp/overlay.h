/* psprecomp — In-Game Settings Overlay (Menu F1)
 * Covers: Controls, Video, Audio, Cheats, Hotkeys (F2, F4, F5, F6, F11)
 */

#ifndef PSPRECOMP_OVERLAY_H
#define PSPRECOMP_OVERLAY_H

#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TAB_CONTROLS = 0,
    TAB_VIDEO,
    TAB_AUDIO,
    TAB_CHEATS,
    TAB_HOTKEYS,
    TAB_COUNT
} overlay_tab_t;

typedef struct {
    int open;
    int current_tab;
    int selected_row;

    /* Controls */
    int free_cam_enabled;
    int pad_r3_look;

    /* Video Display */
    int aspect_ratio;   /* 0: 16:9 (Native), 1: 4:3, 2: Stretch */
    int fullscreen;
    int target_fps;     /* 30, 60, 90, 120, 240 */
    int show_fps;

    /* Video Rendering */
    int internal_res;   /* 1: 1x (480x272), 2: 2x, 3: 3x, 4: 4x */
    int upscaler;       /* 0: Nearest, 1: Bilinear, 2: AMD FSR 1.0 */
    int anti_aliasing;  /* 0: OFF, 1: ON (FXAA) */

    /* Audio */
    int volume;         /* 0 .. 100 */

    /* Cheats (ULUS-10432) */
    int cheat_infinite_hp_sp;
    int cheat_max_money;
    int cheat_max_coins;
    int cheat_no_encounters;

    /* OSD Notification banner */
    char osd_msg[128];
    float osd_timer;
} overlay_state_t;

extern overlay_state_t g_overlay;

void psp_overlay_init(void);
int  psp_overlay_is_open(void);
void psp_overlay_toggle(void);
void psp_overlay_open(void);
void psp_overlay_close(void);

/* Input routing: returns 1 if consumed by overlay */
int  psp_overlay_handle_key(int vk);
int  psp_overlay_handle_pad(uint32_t buttons);

/* Free camera right stick processing */
void psp_overlay_process_camera(int rx, int ry);

/* Frame update (calculates real-time FPS & applies cheats) */
void psp_overlay_tick(void);

/* Render GUI overlay onto current OpenGL viewport */
void psp_overlay_render(int screen_w, int screen_h);

/* Hotkey Actions */
void psp_overlay_screenshot(HWND hwnd);
void psp_overlay_save_state(int slot);
void psp_overlay_load_state(int slot);

/* OSD message popup */
void psp_overlay_notify(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* PSPRECOMP_OVERLAY_H */

