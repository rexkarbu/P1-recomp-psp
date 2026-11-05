/* psprecomp — In-Game Settings Overlay Implementation
 * Supports: Controls, Video, Audio, Cheats, Hotkeys (F2, F4, F5, F6, F11)
 */

#include "psprecomp/overlay.h"
#include "psprecomp/hle.h"
#include "psprecomp/mem.h"
#include "psprecomp/cpu.h"
#include "psprecomp/render.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

overlay_state_t g_overlay;

/* =========================================================================
 * 8x8 Monochrome Bitmap Font (ASCII 32 ' ' to 126 '~')
 * ========================================================================= */
static const uint8_t font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32 ' ' */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 33 '!' */
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, /* 34 '"' */
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, /* 35 '#' */
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, /* 36 '$' */
    {0x00,0x63,0x66,0x0C,0x18,0x33,0x63,0x00}, /* 37 '%' */
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, /* 38 '&' */
    {0x30,0x30,0x10,0x20,0x00,0x00,0x00,0x00}, /* 39 ''' */
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, /* 40 '(' */
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, /* 41 ')' */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 42 '*' */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, /* 43 '+' */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, /* 44 ',' */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* 45 '-' */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* 46 '.' */
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, /* 47 '/' */
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00}, /* 48 '0' */
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, /* 49 '1' */
    {0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00}, /* 50 '2' */
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, /* 51 '3' */
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00}, /* 52 '4' */
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, /* 53 '5' */
    {0x78,0x0C,0x18,0xFA,0xC6,0xC6,0x7C,0x00}, /* 54 '6' */
    {0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00}, /* 55 '7' */
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, /* 56 '8' */
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, /* 57 '9' */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, /* 58 ':' */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, /* 59 ';' */
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, /* 60 '<' */
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, /* 61 '=' */
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, /* 62 '>' */
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00}, /* 63 '?' */
    {0x7C,0xC6,0xDE,0xDE,0xDC,0xC0,0x7C,0x00}, /* 64 '@' */
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* 65 'A' */
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, /* 66 'B' */
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, /* 67 'C' */
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, /* 68 'D' */
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, /* 69 'E' */
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, /* 70 'F' */
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, /* 71 'G' */
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, /* 72 'H' */
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* 73 'I' */
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, /* 74 'J' */
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, /* 75 'K' */
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, /* 76 'L' */
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00}, /* 77 'M' */
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, /* 78 'N' */
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, /* 79 'O' */
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, /* 80 'P' */
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0E}, /* 81 'Q' */
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, /* 82 'R' */
    {0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00}, /* 83 'S' */
    {0x7E,0x7E,0x18,0x18,0x18,0x18,0x3C,0x00}, /* 84 'T' */
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, /* 85 'U' */
    {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00}, /* 86 'V' */
    {0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00}, /* 87 'W' */
    {0xC6,0x6C,0x38,0x10,0x38,0x6C,0xC6,0x00}, /* 88 'X' */
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, /* 89 'Y' */
    {0xFE,0xC6,0x0C,0x18,0x30,0x62,0xFE,0x00}, /* 90 'Z' */
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, /* 91 '[' */
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, /* 92 '\' */
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, /* 93 ']' */
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, /* 94 '^' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 95 '_' */
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, /* 96 '`' */
    {0x00,0x00,0x7C,0x06,0x7E,0xC6,0x7E,0x00}, /* 97 'a' */
    {0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00}, /* 98 'b' */
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00}, /* 99 'c' */
    {0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00}, /* 100 'd' */
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00}, /* 101 'e' */
    {0x1C,0x36,0x30,0x7C,0x30,0x30,0x78,0x00}, /* 102 'f' */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, /* 103 'g' */
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, /* 104 'h' */
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, /* 105 'i' */
    {0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C}, /* 106 'j' */
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, /* 107 'k' */
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* 108 'l' */
    {0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00}, /* 109 'm' */
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, /* 110 'n' */
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00}, /* 111 'o' */
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, /* 112 'p' */
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, /* 113 'q' */
    {0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00}, /* 114 'r' */
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00}, /* 115 's' */
    {0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00}, /* 116 't' */
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, /* 117 'u' */
    {0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, /* 118 'v' */
    {0x00,0x00,0xC6,0xD6,0xFE,0xFE,0x6C,0x00}, /* 119 'w' */
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, /* 120 'x' */
    {0x00,0x00,0xC6,0xC6,0xCE,0x76,0x06,0xFC}, /* 121 'y' */
    {0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00}, /* 122 'z' */
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, /* 123 '{' */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 124 '|' */
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, /* 125 '}' */
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  /* 126 '~' */
};

static void draw_char(float x, float y, char c, float r, float g, float b, float a, float scale) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font8x8[c - 32];
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    for (int row = 0; row < 8; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                float px = x + (float)col * scale;
                float py = y + (float)row * scale;
                glVertex2f(px, py);
                glVertex2f(px + scale, py);
                glVertex2f(px + scale, py + scale);
                glVertex2f(px, py + scale);
            }
        }
    }
    glEnd();
}

static void draw_text(float x, float y, const char *str, float r, float g, float b, float a, float scale) {
    if (!str) return;
    float cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 10.0f * scale;
            cx = x;
        } else {
            draw_char(cx, y, *str, r, g, b, a, scale);
            cx += 8.0f * scale;
        }
        str++;
    }
}

static void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void draw_border(float x, float y, float w, float h, float r, float g, float b, float a, float t) {
    draw_rect(x, y, w, t, r, g, b, a);                 /* Top */
    draw_rect(x, y + h - t, w, t, r, g, b, a);         /* Bottom */
    draw_rect(x, y + t, t, h - 2 * t, r, g, b, a);     /* Left */
    draw_rect(x + w - t, y + t, t, h - 2 * t, r, g, b, a); /* Right */
}

/* =========================================================================
 * State Initialization & Notification
 * ========================================================================= */

void psp_overlay_init(void) {
    memset(&g_overlay, 0, sizeof(g_overlay));
    g_overlay.open = 0;
    g_overlay.current_tab = TAB_CONTROLS;
    g_overlay.selected_row = 0;

    g_overlay.free_cam_enabled = 1;
    g_overlay.pad_r3_look = 1;

    g_overlay.aspect_ratio = 0; /* 16:9 native */
    g_overlay.fullscreen = 0;
    g_overlay.target_fps = 60;
    g_overlay.show_fps = 0;

    g_overlay.internal_res = 2; /* 2x default */
    g_overlay.upscaler = 1;     /* Bilinear */
    g_overlay.anti_aliasing = 1;

    g_overlay.volume = 100;
    psp_audio_set_master_volume(1.0f);

    g_overlay.osd_msg[0] = '\0';
    g_overlay.osd_timer = 0.0f;
}

int  psp_overlay_is_open(void) { return g_overlay.open; }
void psp_overlay_open(void)    { g_overlay.open = 1; }
void psp_overlay_close(void)   { g_overlay.open = 0; }
void psp_overlay_toggle(void)  { g_overlay.open = !g_overlay.open; }

void psp_overlay_notify(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_overlay.osd_msg, sizeof(g_overlay.osd_msg), fmt, ap);
    va_end(ap);
    g_overlay.osd_timer = 3.0f; /* 3 seconds display */
}

/* =========================================================================
 * Cheats & Real-time Tick
 * ========================================================================= */

static uint64_t s_last_time_perf = 0;
static float    s_fps_current = 60.0f;
static int      s_frame_counter = 0;
static float    s_time_accum = 0.0f;

void psp_overlay_tick(void) {
    /* Calculate FPS via high-resolution counter */
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);

    if (s_last_time_perf > 0) {
        float dt = (float)(now.QuadPart - s_last_time_perf) / (float)freq.QuadPart;
        s_time_accum += dt;
        s_frame_counter++;
        if (s_time_accum >= 0.5f) {
            s_fps_current = (float)s_frame_counter / s_time_accum;
            s_frame_counter = 0;
            s_time_accum = 0.0f;
        }
        if (g_overlay.osd_timer > 0.0f) {
            g_overlay.osd_timer -= dt;
            if (g_overlay.osd_timer < 0.0f) g_overlay.osd_timer = 0.0f;
        }
    }
    s_last_time_perf = now.QuadPart;

    /* Apply CWCheats directly to guest RAM */
    if (g_overlay.cheat_max_money) {
        /* Max Money (99,999,999 Yen): _L 0x204C6200 0x05F5E0FF */
        psp_write32(0x004C6200u, 0x05F5E0FFu);
    }
    if (g_overlay.cheat_max_coins) {
        /* Max Casino Coins (99,999,999): _L 0x204C6204 0x05F5E0FF */
        psp_write32(0x004C6204u, 0x05F5E0FFu);
    }
    if (g_overlay.cheat_infinite_hp_sp) {
        /* Party Infinite HP */
        psp_write32(0x00503DC8u, 999);
        psp_write32(0x00503EE0u, 999);
        psp_write32(0x00503FF8u, 999);
        psp_write32(0x00504110u, 999);
        psp_write32(0x00504228u, 999);
        /* 1P HP & SP */
        psp_write32(0x004C51FCu, 999);
        psp_write32(0x004C5204u, 999);
    }
    if (g_overlay.cheat_no_encounters) {
        /* No Random Encounters: _L 0xC005CED0 0x00000000 */
        psp_write32(0x0005CED0u, 0x00000000u);
    }
}

/* =========================================================================
 * Hotkey Action Implementations (Screenshot, Save State, Load State)
 * ========================================================================= */

void psp_overlay_screenshot(HWND hwnd) {
    (void)hwnd;
    CreateDirectoryA("screenshots", NULL);
    time_t rawtime;
    time(&rawtime);
    struct tm *ti = localtime(&rawtime);

    char filename[256];
    snprintf(filename, sizeof(filename), "screenshots/persona_screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
             ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
             ti->tm_hour, ti->tm_min, ti->tm_sec);

    uint32_t px[480 * 272];
    if (psp_display_read(px) != 0) {
        psp_overlay_notify("Failed to capture screen!");
        return;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        psp_overlay_notify("Could not create screenshot file!");
        return;
    }

    /* Write BMP Header (24-bit RGB) */
    uint32_t row_padded = (480 * 3 + 3) & ~3;
    uint32_t img_size = row_padded * 272;
    uint32_t file_size = 54 + img_size;

    uint8_t header[54] = {
        'B', 'M',
        (uint8_t)file_size, (uint8_t)(file_size >> 8), (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        (uint8_t)480, (uint8_t)(480 >> 8), 0, 0,
        (uint8_t)272, (uint8_t)(272 >> 8), 0, 0,
        1, 0,
        24, 0,
        0, 0, 0, 0,
        (uint8_t)img_size, (uint8_t)(img_size >> 8), (uint8_t)(img_size >> 16), (uint8_t)(img_size >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    fwrite(header, 1, 54, f);

    /* Write bottom-to-top BMP rows */
    uint8_t pad_bytes[4] = {0, 0, 0, 0};
    uint32_t pad_len = row_padded - (480 * 3);
    for (int y = 271; y >= 0; y--) {
        for (int x = 0; x < 480; x++) {
            uint32_t c = px[y * 480 + x];
            uint8_t bgr[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
            fwrite(bgr, 1, 3, f);
        }
        if (pad_len > 0) fwrite(pad_bytes, 1, pad_len, f);
    }
    fclose(f);

    printf("[OVERLAY] Screenshot saved: %s\n", filename);
    psp_overlay_notify("Screenshot saved to %s", filename);
}

void psp_overlay_save_state(int slot) {
    CreateDirectoryA("states", NULL);
    char path[128];
    snprintf(path, sizeof(path), "states/slot%d.state", slot);

    FILE *f = fopen(path, "wb");
    if (!f) {
        psp_overlay_notify("Failed to open save state file!");
        return;
    }

    const char magic[4] = {'P', '1', 'S', 'S'};
    fwrite(magic, 1, 4, f);

    /* Save CPU registers */
    fwrite(psp_cpu.r, 4, 32, f);

    /* Save RAM (32 MB) */
    if (psp_mem.ram) fwrite(psp_mem.ram, 1, PSP_RAM_SIZE, f);

    /* Save VRAM (2 MB) */
    if (psp_mem.vram) fwrite(psp_mem.vram, 1, PSP_VRAM_SIZE, f);

    /* Save Scratchpad (16 KB) */
    if (psp_mem.scratch) fwrite(psp_mem.scratch, 1, PSP_SCRATCH_SIZE, f);

    fclose(f);
    printf("[OVERLAY] State saved successfully: %s\n", path);
    psp_overlay_notify("State successfully saved to Slot %d", slot);
}

void psp_overlay_load_state(int slot) {
    char path[128];
    snprintf(path, sizeof(path), "states/slot%d.state", slot);

    FILE *f = fopen(path, "rb");
    if (!f) {
        psp_overlay_notify("Save state Slot %d does not exist!", slot);
        return;
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "P1SS", 4) != 0) {
        fclose(f);
        psp_overlay_notify("Corrupt save state file!");
        return;
    }

    /* Load CPU registers */
    fread(psp_cpu.r, 4, 32, f);

    /* Load RAM */
    if (psp_mem.ram) fread(psp_mem.ram, 1, PSP_RAM_SIZE, f);

    /* Load VRAM */
    if (psp_mem.vram) fread(psp_mem.vram, 1, PSP_VRAM_SIZE, f);

    /* Load Scratchpad */
    if (psp_mem.scratch) fread(psp_mem.scratch, 1, PSP_SCRATCH_SIZE, f);

    fclose(f);
    printf("[OVERLAY] State loaded successfully: %s\n", path);
    psp_overlay_notify("State successfully loaded from Slot %d", slot);
}

/* =========================================================================
 * Input Navigation Logic
 * ========================================================================= */

static int get_row_count_for_tab(int tab) {
    switch (tab) {
    case TAB_CONTROLS: return 4;
    case TAB_VIDEO:    return 7;
    case TAB_AUDIO:    return 2;
    case TAB_CHEATS:   return 4;
    case TAB_HOTKEYS:  return 5;
    default: return 1;
    }
}

static void adjust_setting(int dir) {
    int tab = g_overlay.current_tab;
    int row = g_overlay.selected_row;

    if (tab == TAB_CONTROLS) {
        if (row == 0) g_overlay.free_cam_enabled = !g_overlay.free_cam_enabled;
        else if (row == 1) g_overlay.pad_r3_look = !g_overlay.pad_r3_look;
    } else if (tab == TAB_VIDEO) {
        if (row == 0) { /* Aspect Ratio */
            g_overlay.aspect_ratio = (g_overlay.aspect_ratio + dir + 3) % 3;
        } else if (row == 1) { /* Fullscreen toggle */
            g_overlay.fullscreen = !g_overlay.fullscreen;
        } else if (row == 2) { /* Target FPS: 30, 60, 90, 120, 240 */
            const int fps_table[5] = {30, 60, 90, 120, 240};
            int idx = 1;
            for (int i = 0; i < 5; i++) if (g_overlay.target_fps == fps_table[i]) idx = i;
            idx = (idx + dir + 5) % 5;
            g_overlay.target_fps = fps_table[idx];
        } else if (row == 3) { /* Show FPS */
            g_overlay.show_fps = !g_overlay.show_fps;
        } else if (row == 4) { /* Internal Resolution 1x..4x */
            g_overlay.internal_res += dir;
            if (g_overlay.internal_res < 1) g_overlay.internal_res = 1;
            if (g_overlay.internal_res > 4) g_overlay.internal_res = 4;
        } else if (row == 5) { /* Upscaler */
            g_overlay.upscaler = (g_overlay.upscaler + dir + 3) % 3;
        } else if (row == 6) { /* Anti-Aliasing */
            g_overlay.anti_aliasing = !g_overlay.anti_aliasing;
        }
    } else if (tab == TAB_AUDIO) {
        if (row == 0) { /* Volume */
            g_overlay.volume += dir * 5;
            if (g_overlay.volume < 0) g_overlay.volume = 0;
            if (g_overlay.volume > 100) g_overlay.volume = 100;
            psp_audio_set_master_volume((float)g_overlay.volume / 100.0f);
        } else if (row == 1) { /* Mute toggle */
            if (g_overlay.volume > 0) g_overlay.volume = 0;
            else g_overlay.volume = 100;
            psp_audio_set_master_volume((float)g_overlay.volume / 100.0f);
        }
    } else if (tab == TAB_CHEATS) {
        if (row == 0) {
            g_overlay.cheat_infinite_hp_sp = !g_overlay.cheat_infinite_hp_sp;
            psp_overlay_notify(g_overlay.cheat_infinite_hp_sp ? "Infinite HP/SP: ENABLED" : "Infinite HP/SP: DISABLED");
        } else if (row == 1) {
            g_overlay.cheat_max_money = !g_overlay.cheat_max_money;
            psp_overlay_notify(g_overlay.cheat_max_money ? "Max Money (99,999,999 Yen): ENABLED" : "Max Money: DISABLED");
        } else if (row == 2) {
            g_overlay.cheat_max_coins = !g_overlay.cheat_max_coins;
            psp_overlay_notify(g_overlay.cheat_max_coins ? "Max Casino Coins: ENABLED" : "Max Casino Coins: DISABLED");
        } else if (row == 3) {
            g_overlay.cheat_no_encounters = !g_overlay.cheat_no_encounters;
            psp_overlay_notify(g_overlay.cheat_no_encounters ? "No Random Encounters: ENABLED" : "No Encounters: DISABLED");
        }
    } else if (tab == TAB_HOTKEYS) {
        if (row == 0) psp_overlay_screenshot(NULL);
        else if (row == 1) { g_overlay.show_fps = !g_overlay.show_fps; }
        else if (row == 2) psp_overlay_save_state(1);
        else if (row == 3) psp_overlay_load_state(1);
    }
}

int psp_overlay_handle_key(int vk) {
    /* Always process global quick hotkeys */
    if (vk == VK_F1) {
        psp_overlay_toggle();
        return 1;
    }
    if (vk == VK_F2) {
        psp_overlay_screenshot(NULL);
        return 1;
    }
    if (vk == VK_F4) {
        g_overlay.show_fps = !g_overlay.show_fps;
        psp_overlay_notify(g_overlay.show_fps ? "FPS Counter: ON" : "FPS Counter: OFF");
        return 1;
    }
    if (vk == VK_F5) {
        psp_overlay_save_state(1);
        return 1;
    }
    if (vk == VK_F6) {
        psp_overlay_load_state(1);
        return 1;
    }

    if (!g_overlay.open) return 0;

    /* Overlay is OPEN: route navigation keys */
    if (vk == VK_ESCAPE) {
        psp_overlay_close();
        return 1;
    }
    if (vk == VK_TAB) {
        g_overlay.current_tab = (g_overlay.current_tab + 1) % TAB_COUNT;
        g_overlay.selected_row = 0;
        return 1;
    }
    if (vk == VK_UP || vk == 'W') {
        int max_rows = get_row_count_for_tab(g_overlay.current_tab);
        g_overlay.selected_row = (g_overlay.selected_row - 1 + max_rows) % max_rows;
        return 1;
    }
    if (vk == VK_DOWN || vk == 'S') {
        int max_rows = get_row_count_for_tab(g_overlay.current_tab);
        g_overlay.selected_row = (g_overlay.selected_row + 1) % max_rows;
        return 1;
    }
    if (vk == VK_LEFT || vk == 'A') {
        adjust_setting(-1);
        return 1;
    }
    if (vk == VK_RIGHT || vk == 'D') {
        adjust_setting(1);
        return 1;
    }
    if (vk == VK_RETURN || vk == VK_SPACE || vk == 'Z' || vk == 'J') {
        adjust_setting(1);
        return 1;
    }

    return 1; /* Consume other keys when overlay is open */
}

int psp_overlay_handle_pad(uint32_t buttons) {
    static uint32_t last_buttons = 0;
    uint32_t pressed = buttons & ~last_buttons;
    last_buttons = buttons;

    /* Start + Back / Select combo to toggle menu */
    if ((buttons & 0x0001) && (buttons & 0x0008)) { /* Select + Start */
        if (pressed & (0x0001 | 0x0008)) {
            psp_overlay_toggle();
            return 1;
        }
    }

    if (!g_overlay.open) return 0;

    /* Menu Navigation via Gamepad */
    if (pressed & 0x0010) { /* D-Pad Up */
        int max_rows = get_row_count_for_tab(g_overlay.current_tab);
        g_overlay.selected_row = (g_overlay.selected_row - 1 + max_rows) % max_rows;
    }
    if (pressed & 0x0040) { /* D-Pad Down */
        int max_rows = get_row_count_for_tab(g_overlay.current_tab);
        g_overlay.selected_row = (g_overlay.selected_row + 1) % max_rows;
    }
    if (pressed & 0x0080) { /* D-Pad Left */
        adjust_setting(-1);
    }
    if (pressed & 0x0020) { /* D-Pad Right */
        adjust_setting(1);
    }
    if (pressed & 0x0100) { /* L Trigger -> Prev Tab */
        g_overlay.current_tab = (g_overlay.current_tab - 1 + TAB_COUNT) % TAB_COUNT;
        g_overlay.selected_row = 0;
    }
    if (pressed & 0x0200) { /* R Trigger -> Next Tab */
        g_overlay.current_tab = (g_overlay.current_tab + 1) % TAB_COUNT;
        g_overlay.selected_row = 0;
    }
    if (pressed & 0x4000) { /* Cross / A -> Confirm / Toggle */
        adjust_setting(1);
    }
    if (pressed & 0x2000) { /* Circle / B -> Close Menu */
        psp_overlay_close();
    }

    return 1;
}

void psp_overlay_process_camera(int rx, int ry) {
    if (!g_overlay.free_cam_enabled) return;
    const int deadzone = 8000;
    if (rx * rx + ry * ry > deadzone * deadzone) {
        /* When free camera is enabled, inject stick delta into camera view */
        /* Persona 1 3D dungeons rotate view with L/R triggers */
        if (rx < -16000) {
            psp_ctrl_set(psp_ctrl_buttons() | 0x0100, psp_ctrl_analog_x(), psp_ctrl_analog_y()); /* Turn Left (L) */
        } else if (rx > 16000) {
            psp_ctrl_set(psp_ctrl_buttons() | 0x0200, psp_ctrl_analog_x(), psp_ctrl_analog_y()); /* Turn Right (R) */
        }
    }
}

/* =========================================================================
 * GUI Rendering
 * ========================================================================= */

void psp_overlay_render(int screen_w, int screen_h) {
    if (screen_w <= 0 || screen_h <= 0) return;

    /* Setup 2D Orthographic Projection for UI */
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    /* 1. Render FPS Counter if enabled */
    if (g_overlay.show_fps) {
        char fps_buf[32];
        snprintf(fps_buf, sizeof(fps_buf), "FPS: %.1f", s_fps_current);
        float tw = (float)strlen(fps_buf) * 12.0f;
        draw_rect((float)screen_w - tw - 16.0f, 10.0f, tw + 8.0f, 22.0f, 0.0f, 0.0f, 0.0f, 0.65f);
        draw_text((float)screen_w - tw - 12.0f, 14.0f, fps_buf, 0.2f, 1.0f, 0.3f, 1.0f, 1.5f);
    }

    /* 2. Render OSD Notification Popup */
    if (g_overlay.osd_timer > 0.0f && g_overlay.osd_msg[0]) {
        float alpha = g_overlay.osd_timer < 0.5f ? (g_overlay.osd_timer / 0.5f) : 1.0f;
        float nw = (float)strlen(g_overlay.osd_msg) * 10.0f + 32.0f;
        float nx = ((float)screen_w - nw) * 0.5f;
        float ny = 40.0f;
        draw_rect(nx, ny, nw, 30.0f, 0.05f, 0.1f, 0.2f, 0.85f * alpha);
        draw_border(nx, ny, nw, 30.0f, 0.3f, 0.7f, 1.0f, alpha, 2.0f);
        draw_text(nx + 16.0f, ny + 8.0f, g_overlay.osd_msg, 1.0f, 1.0f, 0.4f, alpha, 1.4f);
    }

    /* 3. Render Settings Menu if OPEN */
    if (!g_overlay.open) {
        glDisable(GL_BLEND);
        return;
    }

    /* Dim background */
    draw_rect(0.0f, 0.0f, (float)screen_w, (float)screen_h, 0.0f, 0.0f, 0.0f, 0.65f);

    /* Main Window Box */
    float mw = 680.0f;
    float mh = 440.0f;
    if (mw > (float)screen_w - 40.0f) mw = (float)screen_w - 40.0f;
    if (mh > (float)screen_h - 40.0f) mh = (float)screen_h - 40.0f;
    float mx = ((float)screen_w - mw) * 0.5f;
    float my = ((float)screen_h - mh) * 0.5f;

    /* Window Background & Glowing Border */
    draw_rect(mx, my, mw, mh, 0.06f, 0.08f, 0.14f, 0.95f);
    draw_border(mx, my, mw, mh, 0.2f, 0.6f, 1.0f, 1.0f, 2.0f);

    /* Title Bar */
    draw_rect(mx + 2.0f, my + 2.0f, mw - 4.0f, 36.0f, 0.12f, 0.2f, 0.35f, 1.0f);
    draw_text(mx + 16.0f, my + 10.0f, "SHIN MEGAMI TENSEI: PERSONA - SETTINGS", 1.0f, 0.9f, 0.2f, 1.0f, 1.8f);

    /* Tab Bar */
    const char *tabs[TAB_COUNT] = { "CONTROLS", "VIDEO", "AUDIO", "CHEATS", "HOTKEYS" };
    float tab_w = (mw - 20.0f) / (float)TAB_COUNT;
    float tab_y = my + 44.0f;

    for (int t = 0; t < TAB_COUNT; t++) {
        float tx = mx + 10.0f + (float)t * tab_w;
        int active = (t == g_overlay.current_tab);
        if (active) {
            draw_rect(tx, tab_y, tab_w - 4.0f, 28.0f, 0.2f, 0.5f, 0.9f, 1.0f);
            draw_border(tx, tab_y, tab_w - 4.0f, 28.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.5f);
            draw_text(tx + 8.0f, tab_y + 7.0f, tabs[t], 1.0f, 1.0f, 1.0f, 1.0f, 1.4f);
        } else {
            draw_rect(tx, tab_y, tab_w - 4.0f, 28.0f, 0.1f, 0.15f, 0.22f, 0.8f);
            draw_text(tx + 8.0f, tab_y + 7.0f, tabs[t], 0.6f, 0.7f, 0.8f, 1.0f, 1.4f);
        }
    }

    /* Content Area */
    float cx = mx + 20.0f;
    float cy = my + 84.0f;
    int row_count = get_row_count_for_tab(g_overlay.current_tab);

    for (int i = 0; i < row_count; i++) {
        float ry = cy + (float)i * 36.0f;
        int is_selected = (i == g_overlay.selected_row);

        if (is_selected) {
            draw_rect(cx - 8.0f, ry - 4.0f, mw - 24.0f, 30.0f, 0.25f, 0.4f, 0.65f, 0.8f);
            draw_border(cx - 8.0f, ry - 4.0f, mw - 24.0f, 30.0f, 1.0f, 0.9f, 0.3f, 1.0f, 1.5f);
        }

        char label[64] = {0};
        char val[64] = {0};

        if (g_overlay.current_tab == TAB_CONTROLS) {
            if (i == 0) {
                snprintf(label, sizeof(label), "Free Camera Look (Right Stick R3)");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.free_cam_enabled ? "ON" : "OFF");
            } else if (i == 1) {
                snprintf(label, sizeof(label), "Analog R3 D-Pad Look Simulation");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.pad_r3_look ? "ON" : "OFF");
            } else if (i == 2) {
                snprintf(label, sizeof(label), "Controls Preset");
                snprintf(val, sizeof(val), "Dual Mode (Gamepad + Keyboard)");
            } else if (i == 3) {
                snprintf(label, sizeof(label), "Menu Open Shortcuts");
                snprintf(val, sizeof(val), "F1 / Esc / Guide / Back+Start");
            }
        } else if (g_overlay.current_tab == TAB_VIDEO) {
            if (i == 0) {
                const char *asp_str[3] = { "16:9 (Native PSP)", "4:3 (Classic)", "16:9 (Stretched)" };
                snprintf(label, sizeof(label), "Aspect Ratio");
                snprintf(val, sizeof(val), "< %s >", asp_str[g_overlay.aspect_ratio]);
            } else if (i == 1) {
                snprintf(label, sizeof(label), "Display Mode");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.fullscreen ? "Fullscreen" : "Windowed");
            } else if (i == 2) {
                snprintf(label, sizeof(label), "Target Frame Rate");
                snprintf(val, sizeof(val), "< %d FPS >", g_overlay.target_fps);
            } else if (i == 3) {
                snprintf(label, sizeof(label), "Show Real-Time FPS (F4)");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.show_fps ? "ON" : "OFF");
            } else if (i == 4) {
                snprintf(label, sizeof(label), "Internal Resolution Multiplier");
                snprintf(val, sizeof(val), "< %dx (%dx%d) >", g_overlay.internal_res,
                         480 * g_overlay.internal_res, 272 * g_overlay.internal_res);
            } else if (i == 5) {
                const char *upscalers[3] = { "Nearest (Pixel Art)", "Bilinear (Smooth)", "AMD FSR 1.0" };
                snprintf(label, sizeof(label), "Upscaler Filter");
                snprintf(val, sizeof(val), "< %s >", upscalers[g_overlay.upscaler]);
            } else if (i == 6) {
                snprintf(label, sizeof(label), "Anti-Aliasing (FXAA)");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.anti_aliasing ? "ON" : "OFF");
            }
        } else if (g_overlay.current_tab == TAB_AUDIO) {
            if (i == 0) {
                snprintf(label, sizeof(label), "Master Output Volume");
                snprintf(val, sizeof(val), "< %d%% >", g_overlay.volume);
            } else if (i == 1) {
                snprintf(label, sizeof(label), "Mute All Audio");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.volume == 0 ? "MUTED" : "UNMUTED");
            }
        } else if (g_overlay.current_tab == TAB_CHEATS) {
            if (i == 0) {
                snprintf(label, sizeof(label), "Infinite Battle HP & SP (All Party)");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.cheat_infinite_hp_sp ? "ENABLED" : "OFF");
            } else if (i == 1) {
                snprintf(label, sizeof(label), "Max Yen / Money (99,999,999)");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.cheat_max_money ? "ENABLED" : "OFF");
            } else if (i == 2) {
                snprintf(label, sizeof(label), "Max Casino Coins (99,999,999)");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.cheat_max_coins ? "ENABLED" : "OFF");
            } else if (i == 3) {
                snprintf(label, sizeof(label), "No Random Encounters");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.cheat_no_encounters ? "ENABLED" : "OFF");
            }
        } else if (g_overlay.current_tab == TAB_HOTKEYS) {
            if (i == 0) {
                snprintf(label, sizeof(label), "F2: Take Instant Screenshot");
                snprintf(val, sizeof(val), "[ Press Enter / A to Capture ]");
            } else if (i == 1) {
                snprintf(label, sizeof(label), "F4: Toggle FPS Counter");
                snprintf(val, sizeof(val), "[ %s ]", g_overlay.show_fps ? "ON" : "OFF");
            } else if (i == 2) {
                snprintf(label, sizeof(label), "F5: Save State (Quick Slot 1)");
                snprintf(val, sizeof(val), "[ Press Enter / A to Save ]");
            } else if (i == 3) {
                snprintf(label, sizeof(label), "F6: Load State (Quick Slot 1)");
                snprintf(val, sizeof(val), "[ Press Enter / A to Load ]");
            } else if (i == 4) {
                snprintf(label, sizeof(label), "F11 / Alt+Enter: Toggle Fullscreen");
                snprintf(val, sizeof(val), "[ Fullscreen Mode ]");
            }
        }

        /* Draw row label & value */
        float text_scale = 1.4f;
        if (is_selected) {
            draw_text(cx, ry + 2.0f, label, 1.0f, 1.0f, 0.4f, 1.0f, text_scale);
            draw_text(cx + 340.0f, ry + 2.0f, val, 0.4f, 1.0f, 1.0f, 1.0f, text_scale);
        } else {
            draw_text(cx, ry + 2.0f, label, 0.9f, 0.9f, 0.9f, 1.0f, text_scale);
            draw_text(cx + 340.0f, ry + 2.0f, val, 0.7f, 0.8f, 0.9f, 1.0f, text_scale);
        }
    }

    /* Footer Navigation Guide */
    float footer_y = my + mh - 34.0f;
    draw_rect(mx + 2.0f, footer_y, mw - 4.0f, 32.0f, 0.08f, 0.12f, 0.2f, 1.0f);
    draw_text(mx + 16.0f, footer_y + 8.0f,
              "[Up/Down]: Select  [Left/Right]: Change  [Enter/A]: Confirm  [Tab/Triggers]: Tab  [F1/Esc/B]: Close",
              0.8f, 0.85f, 0.95f, 1.0f, 1.25f);

    glDisable(GL_BLEND);
}

#endif /* _WIN32 */
