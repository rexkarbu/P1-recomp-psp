/*
 * Custom HLE implementations for Shin Megami Tensei: Persona
 * Covers: sceUmdUser, sceImpose, sceDisplay, ThreadMan, sceSasCore
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <psprecomp/hle.h>
#include <psprecomp/mem.h>
#include <psprecomp/render.h>

#define PSP_UMD_NOT_PRESENT 0x01
#define PSP_UMD_PRESENT     0x02
#define PSP_UMD_CHANGED     0x04
#define PSP_UMD_NOT_READY   0x08
#define PSP_UMD_READY       0x10
#define PSP_UMD_READABLE    0x20

/* =========================================================================
 * sceUmdUser HLE Implementations
 * ========================================================================= */

static void hle_UmdCheckMedium(void) {
    /* Returns: 1 if UMD is in the drive, 0 if not */
    static int s_logged = 0;
    if (!s_logged) {
        printf("[HLE] sceUmdCheckMedium() -> 1 (Disc inserted)\n");
        s_logged = 1;
    }
    psp_ret(1);
}

static void hle_UmdActivate(void) {
    /* (unit, drive) -> 0 on success */
    uint32_t unit = psp_arg(0);
    char drive[64] = {0};
    psp_str(psp_arg(1), drive, sizeof(drive));
    printf("[HLE] sceUmdActivate(unit=%u, drive='%s') -> 0 (OK)\n", unit, drive);
    psp_ret(0);
}

static void hle_UmdDeactivate(void) {
    psp_ret(0);
}

static void hle_UmdGetDriveStat(void) {
    /* Returns bitmask of drive status: PRESENT | READY | READABLE (0x32) */
    psp_ret(PSP_UMD_PRESENT | PSP_UMD_READY | PSP_UMD_READABLE);
}

static void hle_UmdWaitDriveStat(void) {
    uint32_t stat = psp_arg(0);
    printf("[HLE] sceUmdWaitDriveStat(stat=0x%02X) -> Ready\n", stat);
    psp_ret(0);
}

static void hle_UmdWaitDriveStatCB(void) {
    uint32_t stat = psp_arg(0);
    printf("[HLE] sceUmdWaitDriveStatCB(stat=0x%02X) -> Ready\n", stat);
    psp_ret(0);
}

static void hle_UmdCancelWaitDriveStat(void) {
    psp_ret(0);
}

static void hle_IoDevctl(void) {
    char dev[64] = {0};
    psp_str(psp_arg(0), dev, sizeof(dev));
    uint32_t cmd = psp_arg(1);
    static int s_logged_devctl = 0;
    if (s_logged_devctl++ < 5) {
        printf("[HLE] sceIoDevctl(dev='%s', cmd=0x%08X) -> 0 (OK)\n", dev, cmd);
    }
    psp_ret(0);
}

/* =========================================================================
 * sceImpose HLE Implementations
 * ========================================================================= */

static void hle_ImposeParam(void) {
    uint32_t ptr = psp_arg(0);
    if (ptr) {
        /* Set standard language (English = 1) and volume defaults */
        psp_write32(ptr + 0, 1);
        psp_write32(ptr + 4, 1);
    }
    psp_ret(0);
}

/* =========================================================================
 * Extended sceDisplay & Graphics HLE Implementations
 * ========================================================================= */

static uint64_t g_vblank_last_report = 0;

/* PSP double-buffer back-buffer offset: stride(512)*height(272)*bpp(4) = 0x88000 */
#define PSP_BACK_BUF_OFFSET 0x88000u

static void capture_target_buffer(uint64_t vbl, uint32_t addr, uint32_t stride, int fmt, const char *suffix) {
    if (!addr) return;
    char cap_path[128];
    snprintf(cap_path, sizeof(cap_path), "d:/project/tes/frame_%llu_%s.ppm", (unsigned long long)vbl, suffix);
    const int bpp = (fmt == 3) ? 4 : 2;
    int non_black = 0;
    uint32_t px_buf[480 * 272];

    for (int y = 0; y < 272; y++) {
        for (int x = 0; x < 480; x++) {
            uint32_t at = addr + (uint32_t)(y * (int)stride + x) * (uint32_t)bpp;
            uint32_t raw = (bpp == 4) ? psp_read32(at) : psp_read16(at);
            uint32_t c = psp_color_expand(raw, fmt);
            px_buf[y * 480 + x] = c;
            if ((c & 0x00FFFFFFu) != 0) non_black++;
        }
    }

    FILE *f = fopen(cap_path, "wb");
    if (f) {
        fprintf(f, "P6\n480 272\n255\n");
        for (int i = 0; i < 480 * 272; i++) {
            uint32_t c = px_buf[i];
            uint8_t rgb[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        printf("[HLE-GFX] Captured %s (0x%08X, stride=%u, fmt=%d, visible_px=%d): %s\n",
               suffix, addr, stride, fmt, non_black, cap_path);
    }
}

static void hle_DisplayWaitVblankStart(void) {
    psp_display_vblank_step();
    uint64_t vbl = psp_display_vblanks();

    if (vbl - g_vblank_last_report >= 60) {
        g_vblank_last_report = vbl;
        uint32_t fb = psp_display_framebuffer();
        uint32_t rt = psp_render_target_addr();
        uint32_t rt_stride = psp_render_target_stride();
        int rt_fmt = psp_render_target_fmt();

        printf("[HLE-GFX] VBlank Frame %llu | Rendered Pixels: %llu | DisplayFB: 0x%08X | RenderTarget: 0x%08X (str=%u fmt=%d)\n",
               (unsigned long long)vbl,
               (unsigned long long)psp_render_pixels(),
               fb, rt, rt_stride, rt_fmt);

        static int s_dump_frames = -1;
        if (s_dump_frames == -1) {
            s_dump_frames = (getenv("PSPRECOMP_DUMP_FRAMES") != NULL) ? 1 : 0;
        }

        if (s_dump_frames && (vbl == 120 || vbl == 300 || vbl == 600 || vbl == 720 || vbl == 840 ||
            vbl == 960 || vbl == 1200 || vbl == 1500 || vbl == 1800 || vbl == 2100 ||
            vbl == 2400 || vbl == 2700 || vbl == 3000)) {
            char cap_path[128];
            snprintf(cap_path, sizeof(cap_path), "d:/project/tes/frame_%llu.ppm", (unsigned long long)vbl);
            if (psp_display_capture(cap_path) == 0)
                printf("[HLE-GFX] Captured front: %s\n", cap_path);

            if (rt && rt != fb) {
                capture_target_buffer(vbl, rt, rt_stride, rt_fmt, "rendertarget");
            }
            uint32_t back = fb ? (fb ^ PSP_BACK_BUF_OFFSET) : 0;
            if (back && back != fb && back != rt) {
                capture_target_buffer(vbl, back, 512, 3, "backbuf");
            }
        }
    }
    psp_ret(0);
}

static void hle_DisplayGetFrameBuf(void) {
    /* int sceDisplayGetFrameBuf(void **topaddr, int *bufferwidth, int *pixelformat, int sync) */
    uint32_t topaddr     = psp_arg(0);
    uint32_t bufferwidth = psp_arg(1);
    uint32_t pixelformat = psp_arg(2);
    if (topaddr)     psp_write32(topaddr, psp_display_framebuffer());
    if (bufferwidth) psp_write32(bufferwidth, 512);
    if (pixelformat) psp_write32(pixelformat, 3); /* PSP_DISPLAY_PIXEL_FORMAT_8888 */
    psp_ret(0);
}

static void hle_DisplayGetVcount(void) {
    psp_ret((uint32_t)psp_display_vblanks());
}

/* =========================================================================
 * Thread Synchronization & System Timing
 * ========================================================================= */

static void hle_WaitSemaCB(void) {
    /* Forward directly to sceKernelWaitSema implementation */
    psp_hle_call(0x4E3A1105u);
}

static void hle_GetSystemTimeLow(void) {
    static uint32_t s_ticks = 1000;
    s_ticks += 16666;
    psp_ret(s_ticks);
}

/* Lightweight Mutexes */
static void hle_CreateLwMutex(void) {
    uint32_t workarea = psp_arg(0);
    uint32_t count    = psp_arg(3);
    if (workarea) {
        psp_write32(workarea + 0, count);
        psp_write32(workarea + 4, 0);
    }
    psp_ret(0);
}

static void hle_LockLwMutex(void) {
    uint32_t workarea = psp_arg(0);
    uint32_t count    = psp_arg(1);
    if (workarea) {
        uint32_t cur = psp_read32(workarea + 0);
        psp_write32(workarea + 0, cur + count);
    }
    psp_ret(0);
}

static void hle_UnlockLwMutex(void) {
    uint32_t workarea = psp_arg(0);
    uint32_t count    = psp_arg(1);
    if (workarea) {
        uint32_t cur = psp_read32(workarea + 0);
        if (cur >= count) psp_write32(workarea + 0, cur - count);
        else psp_write32(workarea + 0, 0);
    }
    psp_ret(0);
}

static void hle_DeleteLwMutex(void) {
    psp_ret(0);
}

static void hle_StopUnloadSelfModuleWithStatus(void) {
    psp_ret(0);
}

/* =========================================================================
 * sceSasCore Sound Envelope Hook
 * ========================================================================= */

static void hle_SasGetAllEnvelopeHeights(void) {
    uint32_t ptr = psp_arg(1);
    if (ptr) {
        for (int i = 0; i < 32; i++) {
            psp_write32(ptr + (uint32_t)(i * 4), 0);
        }
    }
    psp_ret(0);
}

/* =========================================================================
 * sceUtility & System HLE Implementations
 * ========================================================================= */

static void hle_UtilityNetModule(void) {
    printf("[HLE] sceUtilityLoadNetModule() -> OK\n");
    psp_ret(0);
}

static void hle_CompilerVersion(void) {
    psp_ret(0);
}

/* =========================================================================
 * sceRtc — Real Time Clock
 * ========================================================================= */

static void hle_RtcGetCurrentClockLocalTime(void) {
    /* pspTime* at $a0: year(u16), month(u16), day(u16), hour(u16),
     *                  minute(u16), second(u16), microsecond(u32) */
    uint32_t ptr = psp_arg(0);
    if (ptr) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        psp_write16(ptr + 0,  (uint16_t)(t->tm_year + 1900));
        psp_write16(ptr + 2,  (uint16_t)(t->tm_mon + 1));
        psp_write16(ptr + 4,  (uint16_t)t->tm_mday);
        psp_write16(ptr + 6,  (uint16_t)t->tm_hour);
        psp_write16(ptr + 8,  (uint16_t)t->tm_min);
        psp_write16(ptr + 10, (uint16_t)t->tm_sec);
        psp_write32(ptr + 12, 0);
    }
    psp_ret(0);
}

static void hle_RtcGetTick(void) {
    /* (pspTime *date, u64 *tick) — convert pspTime to tick count */
    uint32_t date_ptr = psp_arg(0);
    uint32_t tick_ptr = psp_arg(1);
    if (tick_ptr) {
        /* Return a reasonable tick value based on current time */
        uint64_t tick = (uint64_t)time(NULL) * 1000000ull;
        psp_write32(tick_ptr + 0, (uint32_t)(tick & 0xFFFFFFFFu));
        psp_write32(tick_ptr + 4, (uint32_t)(tick >> 32));
    }
    psp_ret(0);
}

/* =========================================================================
 * sceAtrac3plus HLE Implementations
 * ========================================================================= */

static void hle_AtracSetDataAndGetID(void) {
    /* (void *buf, SceSize bufsize) -> atracID (0) */
    uint32_t buf = psp_arg(0);
    uint32_t size = psp_arg(1);
    printf("[HLE] sceAtracSetDataAndGetID(buf=0x%08X, size=%u) -> ID 0\n", buf, size);
    psp_ret(0); /* atracID = 0 */
}

static void hle_AtracReleaseAtracID(void) {
    printf("[HLE] sceAtracReleaseAtracID(id=%u) -> OK\n", psp_arg(0));
    psp_ret(0);
}

static void hle_AtracAddStreamData(void) {
    /* (int atracID, unsigned int bytesToAdd) */
    psp_ret(0);
}

static void hle_AtracDecodeData(void) {
    /* (int atracID, u16 *outSamples, int *outNsamples, int *outEnd, int *outRemainFrame) */
    uint32_t outSamples = psp_arg(1);
    uint32_t outNsamples = psp_arg(2);
    uint32_t outEnd = psp_arg(3);
    uint32_t outRemain = psp_arg(4);

    if (outNsamples) psp_write32(outNsamples, 2048);
    if (outEnd)      psp_write32(outEnd, 0);
    if (outRemain)   psp_write32(outRemain, 1);

    /* Clear audio samples to silence if buffer is provided */
    if (outSamples) {
        /* 2048 stereo 16-bit samples = 8192 bytes */
        for (uint32_t i = 0; i < 2048 * 4; i += 4) {
            psp_write32(outSamples + i, 0);
        }
    }

    psp_ret(0);
}

static void hle_AtracGetRemainFrame(void) {
    uint32_t outRemain = psp_arg(1);
    if (outRemain) psp_write32(outRemain, 1);
    psp_ret(0);
}

static void hle_AtracSetSecondBuffer(void) {
    psp_ret(0);
}

static void hle_AtracGetSecondBufferInfo(void) {
    uint32_t pPos = psp_arg(1);
    uint32_t pByte = psp_arg(2);
    if (pPos)  psp_write32(pPos, 0);
    if (pByte) psp_write32(pByte, 0);
    psp_ret(0);
}

static void hle_AtracSetLoopNum(void) {
    psp_ret(0);
}

static void hle_AtracGetSoundSample(void) {
    uint32_t pEnd = psp_arg(1);
    uint32_t pLoopStart = psp_arg(2);
    uint32_t pLoopEnd = psp_arg(3);
    if (pEnd) psp_write32(pEnd, 44100 * 60); /* 1 min of samples */
    if (pLoopStart) psp_write32(pLoopStart, 0);
    if (pLoopEnd) psp_write32(pLoopEnd, 44100 * 60);
    psp_ret(0);
}

static void hle_AtracGetBufferInfoForResetting(void) {
    psp_ret(0);
}

static void hle_AtracResetPlayPosition(void) {
    psp_ret(0);
}

static void hle_AtracGetStreamDataInfo(void) {
    /* (int atracID, void **pWritePointer, u32 *pAvailableBytes, u32 *pReadOffset) */
    uint32_t pWrite = psp_arg(1);
    uint32_t pAvail = psp_arg(2);
    uint32_t pOffset = psp_arg(3);
    static uint32_t s_stream_scratch = 0x08E00000u;
    if (pWrite)  psp_write32(pWrite, s_stream_scratch);
    if (pAvail)  psp_write32(pAvail, 65536);
    if (pOffset) psp_write32(pOffset, 0);
    psp_ret(0);
}

static void hle_AtracGetNextDecodePosition(void) {
    uint32_t pSample = psp_arg(1);
    static uint32_t s_cur_sample = 0;
    s_cur_sample += 2048;
    if (pSample) psp_write32(pSample, s_cur_sample);
    psp_ret(0);
}

static void hle_AtracGetInternalErrorInfo(void) {
    uint32_t pRes = psp_arg(1);
    if (pRes) psp_write32(pRes, 0);
    psp_ret(0);
}

/* =========================================================================
 * sceUtility — MsgDialog Utility
 * ========================================================================= */

static uint32_t g_msgdialog_param = 0;
static int      g_msgdialog_status = 0;
static int      g_msgdialog_ticks = 0;

static void hle_UtilityMsgDialogInitStart(void) {
    g_msgdialog_param = psp_arg(0);
    g_msgdialog_status = 2; /* PSP_UTILITY_DIALOG_VISIBLE */
    g_msgdialog_ticks = 0;
    printf("[HLE] sceUtilityMsgDialogInitStart(param=0x%08X)\n", g_msgdialog_param);
    if (g_msgdialog_param) {
        printf("[HLE] MsgDialog param dump:\n");
        for (int i = 0; i < 64; i += 16) {
            printf("  +%02X: %08X %08X %08X %08X\n", i,
                   psp_read32(g_msgdialog_param + i + 0),
                   psp_read32(g_msgdialog_param + i + 4),
                   psp_read32(g_msgdialog_param + i + 8),
                   psp_read32(g_msgdialog_param + i + 12));
        }
    }
    psp_ret(0);
}

static void hle_UtilityMsgDialogGetStatus(void) {
    if (g_msgdialog_status == 2) {
        if (++g_msgdialog_ticks >= 3) {
            g_msgdialog_status = 3; /* PSP_UTILITY_DIALOG_QUIT */
            if (g_msgdialog_param) {
                psp_write32(g_msgdialog_param + 28, 0); /* result = 0 (OK) */
                psp_write32(g_msgdialog_param + 0x34, 1); /* button = YES */
            }
            printf("[HLE] sceUtilityMsgDialogGetStatus() -> 3 (QUIT/DONE)\n");
        }
    }
    psp_ret((uint32_t)g_msgdialog_status);
}

static void hle_UtilityMsgDialogShutdownStart(void) {
    g_msgdialog_status = 4;
    printf("[HLE] sceUtilityMsgDialogShutdownStart() -> OK\n");
    psp_ret(0);
}

static void hle_UtilityMsgDialogUpdate(void) {
    psp_ret(0);
}

/* =========================================================================
 * sceUtility — Savedata Utility Implementation
 * ========================================================================= */

#define PSP_UTILITY_SAVEDATA_AUTOLOAD           0
#define PSP_UTILITY_SAVEDATA_AUTOSAVE           1
#define PSP_UTILITY_SAVEDATA_LOAD               2
#define PSP_UTILITY_SAVEDATA_SAVE               3
#define PSP_UTILITY_SAVEDATA_LISTLOAD           4
#define PSP_UTILITY_SAVEDATA_LISTSAVE           5
#define PSP_UTILITY_SAVEDATA_LISTDELETE         6
#define PSP_UTILITY_SAVEDATA_DELETE             7
#define PSP_UTILITY_SAVEDATA_SIZES              8
#define PSP_UTILITY_SAVEDATA_AUTODELETE         9
#define PSP_UTILITY_SAVEDATA_SINGLEDELETE       10
#define PSP_UTILITY_SAVEDATA_LIST               11
#define PSP_UTILITY_SAVEDATA_FILES              12
#define PSP_UTILITY_SAVEDATA_MAKEDATASECURE     13
#define PSP_UTILITY_SAVEDATA_MAKEDATA           14
#define PSP_UTILITY_SAVEDATA_READDATASECURE     15
#define PSP_UTILITY_SAVEDATA_READDATA           16
#define PSP_UTILITY_SAVEDATA_WRITEDATASECURE    17
#define PSP_UTILITY_SAVEDATA_WRITEDATA          18
#define PSP_UTILITY_SAVEDATA_ERASEDATA          19
#define PSP_UTILITY_SAVEDATA_DELETEDATA         20
#define PSP_UTILITY_SAVEDATA_GETSIZE            21

#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_MS       0x80110301u
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN 0x80110305u
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA     0x80110307u
#define SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA       0x80110327u

static uint32_t g_savedata_param = 0;
static int      g_savedata_status = 0;
static int      g_savedata_ticks = 0;

static void ensure_directory_exists(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char orig = *p;
            *p = '\0';
            if (tmp[0] != '\0' && !(tmp[1] == ':' && tmp[2] == '\0')) {
                CreateDirectoryA(tmp, NULL);
            }
            *p = orig;
        }
    }
    CreateDirectoryA(tmp, NULL);
}

static void write_param_sfo(const char *sfo_path, const char *title, const char *savedataTitle, const char *detail) {
    if (!title || !title[0]) title = "Shin Megami Tensei: Persona";
    if (!savedataTitle || !savedataTitle[0]) savedataTitle = "Persona Save Data";
    if (!detail || !detail[0]) detail = "Savedata for Shin Megami Tensei: Persona";

    static const char *keys[] = {
        "CATEGORY",
        "PARENTAL_LEVEL",
        "SAVEDATA_DETAIL",
        "SAVEDATA_TITLE",
        "TITLE"
    };
    int num_entries = 5;

    uint16_t key_offsets[5];
    uint32_t cur_key_off = 0;
    for (int i = 0; i < 5; i++) {
        key_offsets[i] = (uint16_t)cur_key_off;
        cur_key_off += (uint32_t)(strlen(keys[i]) + 1);
    }
    uint32_t key_table_size = (cur_key_off + 3) & ~3u;

    uint32_t cat_len = (uint32_t)strlen("MS") + 1;
    uint32_t plevel_len = 4;
    uint32_t det_len = (uint32_t)strlen(detail) + 1;
    uint32_t stit_len = (uint32_t)strlen(savedataTitle) + 1;
    uint32_t tit_len = (uint32_t)strlen(title) + 1;

    uint32_t data_offsets[5];
    uint32_t cur_data_off = 0;

    data_offsets[0] = cur_data_off; cur_data_off += ((cat_len + 3) & ~3u);
    data_offsets[1] = cur_data_off; cur_data_off += 4;
    data_offsets[2] = cur_data_off; cur_data_off += ((det_len + 3) & ~3u);
    data_offsets[3] = cur_data_off; cur_data_off += ((stit_len + 3) & ~3u);
    data_offsets[4] = cur_data_off; cur_data_off += ((tit_len + 3) & ~3u);

    uint32_t key_table_start = 20 + num_entries * 16;
    uint32_t data_table_start = key_table_start + key_table_size;
    uint32_t total_sfo_size = data_table_start + cur_data_off;

    uint8_t *sfo = (uint8_t *)calloc(1, total_sfo_size);
    if (!sfo) return;

    /* SFO Header */
    sfo[0] = 0x00; sfo[1] = 'P'; sfo[2] = 'S'; sfo[3] = 'F';
    *(uint32_t *)(sfo + 4) = 0x00000101;
    *(uint32_t *)(sfo + 8) = key_table_start;
    *(uint32_t *)(sfo + 12) = data_table_start;
    *(uint32_t *)(sfo + 16) = (uint32_t)num_entries;

    /* SFO Entries */
    uint8_t *e = sfo + 20;
    *(uint16_t *)(e + 0) = key_offsets[0];
    *(uint16_t *)(e + 2) = 0x0204;
    *(uint32_t *)(e + 4) = cat_len;
    *(uint32_t *)(e + 8) = 4;
    *(uint32_t *)(e + 12) = data_offsets[0];

    e += 16;
    *(uint16_t *)(e + 0) = key_offsets[1];
    *(uint16_t *)(e + 2) = 0x0004;
    *(uint32_t *)(e + 4) = plevel_len;
    *(uint32_t *)(e + 8) = 4;
    *(uint32_t *)(e + 12) = data_offsets[1];

    e += 16;
    *(uint16_t *)(e + 0) = key_offsets[2];
    *(uint16_t *)(e + 2) = 0x0204;
    *(uint32_t *)(e + 4) = det_len;
    *(uint32_t *)(e + 8) = 1024;
    *(uint32_t *)(e + 12) = data_offsets[2];

    e += 16;
    *(uint16_t *)(e + 0) = key_offsets[3];
    *(uint16_t *)(e + 2) = 0x0204;
    *(uint32_t *)(e + 4) = stit_len;
    *(uint32_t *)(e + 8) = 128;
    *(uint32_t *)(e + 12) = data_offsets[3];

    e += 16;
    *(uint16_t *)(e + 0) = key_offsets[4];
    *(uint16_t *)(e + 2) = 0x0204;
    *(uint32_t *)(e + 4) = tit_len;
    *(uint32_t *)(e + 8) = 128;
    *(uint32_t *)(e + 12) = data_offsets[4];

    /* Copy Keys */
    uint8_t *k = sfo + key_table_start;
    for (int i = 0; i < 5; i++) {
        strcpy((char *)(k + key_offsets[i]), keys[i]);
    }

    /* Copy Data */
    uint8_t *d = sfo + data_table_start;
    strcpy((char *)(d + data_offsets[0]), "MS");
    *(uint32_t *)(d + data_offsets[1]) = 0;
    strcpy((char *)(d + data_offsets[2]), detail);
    strcpy((char *)(d + data_offsets[3]), savedataTitle);
    strcpy((char *)(d + data_offsets[4]), title);

    FILE *f = fopen(sfo_path, "wb");
    if (f) {
        fwrite(sfo, 1, total_sfo_size, f);
        fclose(f);
        printf("[HLE-SAVE] Generated PARAM.SFO (%u bytes): %s\n", total_sfo_size, sfo_path);
    }
    free(sfo);
}

static void read_param_sfo(const char *sfo_path, char *title, char *savedataTitle, char *detail) {
    FILE *f = fopen(sfo_path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 20) { fclose(f); return; }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (buf[0] != 0x00 || buf[1] != 'P' || buf[2] != 'S' || buf[3] != 'F') {
        free(buf);
        return;
    }

    uint32_t key_table_start  = *(uint32_t *)(buf + 8);
    uint32_t data_table_start = *(uint32_t *)(buf + 12);
    uint32_t num_entries     = *(uint32_t *)(buf + 16);

    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t ent_off = 20 + i * 16;
        if (ent_off + 16 > (uint32_t)sz) break;
        uint16_t key_off  = *(uint16_t *)(buf + ent_off + 0);
        uint16_t data_fmt = *(uint16_t *)(buf + ent_off + 2);
        uint32_t data_len = *(uint32_t *)(buf + ent_off + 4);
        uint32_t data_off = *(uint32_t *)(buf + ent_off + 12);

        if (key_table_start + key_off >= (uint32_t)sz) continue;
        const char *k = (const char *)(buf + key_table_start + key_off);
        if (data_table_start + data_off + data_len > (uint32_t)sz) continue;
        const char *v = (const char *)(buf + data_table_start + data_off);

        if (data_fmt == 0x0204) {
            if (title && strcmp(k, "TITLE") == 0) {
                strncpy(title, v, 127);
                title[127] = '\0';
            } else if (savedataTitle && strcmp(k, "SAVEDATA_TITLE") == 0) {
                strncpy(savedataTitle, v, 127);
                savedataTitle[127] = '\0';
            } else if (detail && strcmp(k, "SAVEDATA_DETAIL") == 0) {
                strncpy(detail, v, 1023);
                detail[1023] = '\0';
            }
        }
    }
    free(buf);
}

static void hle_savedata_execute(uint32_t param_ptr) {
    if (!param_ptr) return;

    uint32_t mode = psp_read32(param_ptr + 0x30);
    char gameName[17] = {0};
    char saveName[21] = {0};
    char fileName[17] = {0};
    for (int i = 0; i < 16; i++) gameName[i] = (char)psp_read8(param_ptr + 0x3C + i);
    for (int i = 0; i < 20; i++) saveName[i] = (char)psp_read8(param_ptr + 0x4C + i);
    for (int i = 0; i < 16; i++) fileName[i] = (char)psp_read8(param_ptr + 0x64 + i);

    /* Clean trailing whitespace or non-printable chars */
    for (int i = 0; i < 16 && gameName[i]; i++) {
        if ((unsigned char)gameName[i] < 32 || (unsigned char)gameName[i] > 126) gameName[i] = '\0';
    }
    for (int i = 0; i < 20 && saveName[i]; i++) {
        if ((unsigned char)saveName[i] < 32 || (unsigned char)saveName[i] > 126) saveName[i] = '\0';
    }
    for (int i = 0; i < 16 && fileName[i]; i++) {
        if ((unsigned char)fileName[i] < 32 || (unsigned char)fileName[i] > 126) fileName[i] = '\0';
    }

    if (!gameName[0]) strcpy(gameName, "ULUS10432");
    if (!saveName[0]) strcpy(saveName, "0000");

    uint32_t dataBuf = psp_read32(param_ptr + 0x74);
    uint32_t dataBufSize = psp_read32(param_ptr + 0x78);
    uint32_t dataSize = psp_read32(param_ptr + 0x7C);

    char slot_dir[MAX_PATH];
    snprintf(slot_dir, sizeof(slot_dir), "savedata/%s%s", gameName, saveName);

    printf("[HLE-SAVE] Mode=%u | SlotDir='%s' | File='%s'\n", mode, slot_dir, fileName[0] ? fileName : "(auto)");

    switch (mode) {
        case PSP_UTILITY_SAVEDATA_AUTOLOAD:
        case PSP_UTILITY_SAVEDATA_LOAD:
        case PSP_UTILITY_SAVEDATA_LISTLOAD:
        case PSP_UTILITY_SAVEDATA_READDATA:
        case PSP_UTILITY_SAVEDATA_READDATASECURE: {
            char target_file[MAX_PATH] = {0};
            FILE *f = NULL;

            /* 1. Try exact requested filename */
            if (fileName[0]) {
                snprintf(target_file, sizeof(target_file), "%s/%s", slot_dir, fileName);
                f = fopen(target_file, "rb");
            }

            /* 2. Fallbacks: PERSONA.SAV, DATA.BIN, SECURE.BIN */
            if (!f) {
                snprintf(target_file, sizeof(target_file), "%s/PERSONA.SAV", slot_dir);
                f = fopen(target_file, "rb");
            }
            if (!f) {
                snprintf(target_file, sizeof(target_file), "%s/DATA.BIN", slot_dir);
                f = fopen(target_file, "rb");
            }
            if (!f) {
                snprintf(target_file, sizeof(target_file), "%s/SECURE.BIN", slot_dir);
                f = fopen(target_file, "rb");
            }

            if (!f) {
                printf("[HLE-SAVE] Load failed: No save file found in '%s' -> NO_DATA (0x80110307)\n", slot_dir);
                psp_write32(param_ptr + 0x1C, SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA);
                break;
            }

            fseek(f, 0, SEEK_END);
            long fsz = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (fsz <= 0) {
                fclose(f);
                printf("[HLE-SAVE] Load failed: File '%s' is empty -> DATA_BROKEN (0x80110305)\n", target_file);
                psp_write32(param_ptr + 0x1C, SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN);
                break;
            }

            uint32_t to_read = (uint32_t)fsz;
            if (dataBufSize > 0 && to_read > dataBufSize) {
                printf("[HLE-SAVE] Note: file size (%u) clamped to dataBufSize (%u)\n", to_read, dataBufSize);
                to_read = dataBufSize;
            }

            uint8_t *temp_buf = (uint8_t *)malloc(to_read);
            if (!temp_buf) {
                fclose(f);
                psp_write32(param_ptr + 0x1C, SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN);
                break;
            }

            size_t bytes_read = fread(temp_buf, 1, to_read, f);
            fclose(f);

            /* Write into guest RAM dataBuf */
            if (dataBuf) {
                psp_mem_write_block(dataBuf, temp_buf, (uint32_t)bytes_read);
            }
            free(temp_buf);

            /* Update actual dataSize in guest struct */
            psp_write32(param_ptr + 0x7C, (uint32_t)bytes_read);

            /* Try reading SFO metadata if available */
            char sfo_path[MAX_PATH];
            snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", slot_dir);
            char sfo_title[128] = {0};
            char sfo_savedataTitle[128] = {0};
            char sfo_detail[1024] = {0};
            read_param_sfo(sfo_path, sfo_title, sfo_savedataTitle, sfo_detail);
            if (sfo_title[0]) {
                for (int i = 0; sfo_title[i] && i < 127; i++) psp_write8(param_ptr + 0x80 + i, (uint8_t)sfo_title[i]);
                psp_write8(param_ptr + 0x80 + (uint32_t)strlen(sfo_title), 0);
            }
            if (sfo_savedataTitle[0]) {
                for (int i = 0; sfo_savedataTitle[i] && i < 127; i++) psp_write8(param_ptr + 0x100 + i, (uint8_t)sfo_savedataTitle[i]);
                psp_write8(param_ptr + 0x100 + (uint32_t)strlen(sfo_savedataTitle), 0);
            }
            if (sfo_detail[0]) {
                for (int i = 0; sfo_detail[i] && i < 1023; i++) psp_write8(param_ptr + 0x180 + i, (uint8_t)sfo_detail[i]);
                psp_write8(param_ptr + 0x180 + (uint32_t)strlen(sfo_detail), 0);
            }

            /* Read ICON0.PNG if buffer requested */
            uint32_t iconBuf = psp_read32(param_ptr + 0x584);
            uint32_t iconBufSize = psp_read32(param_ptr + 0x588);
            if (iconBuf && iconBufSize > 0) {
                char icon_path[MAX_PATH];
                snprintf(icon_path, sizeof(icon_path), "%s/ICON0.PNG", slot_dir);
                FILE *fi = fopen(icon_path, "rb");
                if (fi) {
                    fseek(fi, 0, SEEK_END);
                    long isz = ftell(fi);
                    fseek(fi, 0, SEEK_SET);
                    if (isz > 0) {
                        uint32_t to_read_icon = (iconBufSize < (uint32_t)isz) ? iconBufSize : (uint32_t)isz;
                        uint8_t *ibuf = (uint8_t *)malloc(to_read_icon);
                        if (ibuf) {
                            size_t iread = fread(ibuf, 1, to_read_icon, fi);
                            psp_mem_write_block(iconBuf, ibuf, (uint32_t)iread);
                            psp_write32(param_ptr + 0x58C, (uint32_t)iread);
                            free(ibuf);
                        }
                    }
                    fclose(fi);
                }
            }

            psp_write32(param_ptr + 0x1C, 0); /* Result = 0 (OK) */
            printf("[HLE-SAVE] Successfully loaded %u bytes from '%s' into 0x%08X (Result=OK)\n",
                   (uint32_t)bytes_read, target_file, dataBuf);
            break;
        }

        case PSP_UTILITY_SAVEDATA_AUTOSAVE:
        case PSP_UTILITY_SAVEDATA_SAVE:
        case PSP_UTILITY_SAVEDATA_LISTSAVE:
        case PSP_UTILITY_SAVEDATA_MAKEDATA:
        case PSP_UTILITY_SAVEDATA_MAKEDATASECURE:
        case PSP_UTILITY_SAVEDATA_WRITEDATA:
        case PSP_UTILITY_SAVEDATA_WRITEDATASECURE: {
            ensure_directory_exists(slot_dir);

            const char *save_fn = fileName[0] ? fileName : "PERSONA.SAV";
            char target_file[MAX_PATH];
            snprintf(target_file, sizeof(target_file), "%s/%s", slot_dir, save_fn);

            uint32_t to_write = dataSize ? dataSize : dataBufSize;
            if (!to_write || !dataBuf) {
                printf("[HLE-SAVE] Save warning: 0 bytes to write (dataBuf=0x%08X, size=%u), using fallback 524\n",
                       dataBuf, to_write);
                to_write = 524;
            }

            uint8_t *temp_buf = (uint8_t *)malloc(to_write);
            if (temp_buf) {
                psp_mem_read_block(temp_buf, dataBuf, to_write);
                FILE *f = fopen(target_file, "wb");
                if (f) {
                    fwrite(temp_buf, 1, to_write, f);
                    fclose(f);
                    printf("[HLE-SAVE] Successfully saved %u bytes to '%s'\n", to_write, target_file);
                } else {
                    printf("[HLE-SAVE] Error opening file for write: '%s'\n", target_file);
                }
                free(temp_buf);
            }

            /* Read SFO metadata from guest RAM */
            char sfo_title[128] = {0};
            char sfo_savedataTitle[128] = {0};
            char sfo_detail[1024] = {0};
            for (int i = 0; i < 127; i++) sfo_title[i] = (char)psp_read8(param_ptr + 0x80 + i);
            for (int i = 0; i < 127; i++) sfo_savedataTitle[i] = (char)psp_read8(param_ptr + 0x100 + i);
            for (int i = 0; i < 1023; i++) sfo_detail[i] = (char)psp_read8(param_ptr + 0x180 + i);

            char sfo_path[MAX_PATH];
            snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", slot_dir);
            write_param_sfo(sfo_path, sfo_title, sfo_savedataTitle, sfo_detail);

            /* Save ICON0.PNG if provided */
            uint32_t iconBuf = psp_read32(param_ptr + 0x584);
            uint32_t iconSize = psp_read32(param_ptr + 0x58C);
            if (iconBuf && iconSize > 0) {
                char icon_path[MAX_PATH];
                snprintf(icon_path, sizeof(icon_path), "%s/ICON0.PNG", slot_dir);
                uint8_t *ibuf = (uint8_t *)malloc(iconSize);
                if (ibuf) {
                    psp_mem_read_block(ibuf, iconBuf, iconSize);
                    FILE *fi = fopen(icon_path, "wb");
                    if (fi) {
                        fwrite(ibuf, 1, iconSize, fi);
                        fclose(fi);
                        printf("[HLE-SAVE] Saved ICON0.PNG (%u bytes) to '%s'\n", iconSize, icon_path);
                    }
                    free(ibuf);
                }
            }

            psp_write32(param_ptr + 0x1C, 0); /* Result = 0 (OK) */
            break;
        }

        case PSP_UTILITY_SAVEDATA_LISTDELETE:
        case PSP_UTILITY_SAVEDATA_DELETE:
        case PSP_UTILITY_SAVEDATA_AUTODELETE:
        case PSP_UTILITY_SAVEDATA_SINGLEDELETE:
        case PSP_UTILITY_SAVEDATA_ERASEDATA:
        case PSP_UTILITY_SAVEDATA_DELETEDATA: {
            char target_file[MAX_PATH];
            const char *save_fn = fileName[0] ? fileName : "PERSONA.SAV";
            snprintf(target_file, sizeof(target_file), "%s/%s", slot_dir, save_fn);
            DeleteFileA(target_file);

            char sfo_path[MAX_PATH];
            snprintf(sfo_path, sizeof(sfo_path), "%s/PARAM.SFO", slot_dir);
            DeleteFileA(sfo_path);

            char icon_path[MAX_PATH];
            snprintf(icon_path, sizeof(icon_path), "%s/ICON0.PNG", slot_dir);
            DeleteFileA(icon_path);

            RemoveDirectoryA(slot_dir);
            printf("[HLE-SAVE] Deleted save directory '%s'\n", slot_dir);
            psp_write32(param_ptr + 0x1C, 0);
            break;
        }

        case PSP_UTILITY_SAVEDATA_SIZES:
        case PSP_UTILITY_SAVEDATA_GETSIZE: {
            printf("[HLE-SAVE] Query sizes/space for '%s' -> OK\n", slot_dir);
            psp_write32(param_ptr + 0x1C, 0);
            break;
        }

        default:
            printf("[HLE-SAVE] Unhandled savedata mode %u -> Default OK\n", mode);
            psp_write32(param_ptr + 0x1C, 0);
            break;
    }
}

static void hle_UtilitySavedataInitStart(void) {
    g_savedata_param = psp_arg(0);
    g_savedata_status = 2; /* PSP_UTILITY_DIALOG_VISIBLE */
    g_savedata_ticks = 0;
    printf("[HLE] sceUtilitySavedataInitStart(param=0x%08X)\n", g_savedata_param);
    psp_ret(0);
}

static void hle_UtilitySavedataGetStatus(void) {
    if (g_savedata_status == 2) { /* RUNNING */
        if (++g_savedata_ticks >= 2) {
            g_savedata_status = 3; /* FINISHED */
            if (g_savedata_param) {
                hle_savedata_execute(g_savedata_param);
            }
            printf("[HLE] sceUtilitySavedataGetStatus() -> 3 (FINISHED)\n");
        }
    } else if (g_savedata_status == 4) { /* SHUTDOWN */
        g_savedata_status = 0; /* NONE */
        printf("[HLE] sceUtilitySavedataGetStatus() -> 0 (NONE/CLOSED)\n");
    }
    psp_ret((uint32_t)g_savedata_status);
}

static void hle_UtilitySavedataShutdownStart(void) {
    g_savedata_status = 4; /* SHUTDOWN */
    printf("[HLE] sceUtilitySavedataShutdownStart() -> 4 (SHUTDOWN)\n");
    psp_ret(0);
}

static void hle_UtilitySavedataUpdate(void) {
    psp_ret(0);
}

void persona_register_custom_hle(void) {
    printf("[HLE] Registering custom firmware hooks for Persona 1...\n");

    /* sceUmdUser & IoFileMgr */
    psp_hle_register(0x46EBB729, "sceUmdUser", "sceUmdCheckMedium",         hle_UmdCheckMedium);
    psp_hle_register(0xC6183D47, "sceUmdUser", "sceUmdActivate",            hle_UmdActivate);
    psp_hle_register(0xE83742BA, "sceUmdUser", "sceUmdDeactivate",          hle_UmdDeactivate);
    psp_hle_register(0x6B4A146C, "sceUmdUser", "sceUmdGetDriveStat",         hle_UmdGetDriveStat);
    psp_hle_register(0x4A9E5E29, "sceUmdUser", "sceUmdWaitDriveStat",        hle_UmdWaitDriveStat);
    psp_hle_register(0x8EF08FCE, "sceUmdUser", "sceUmdWaitDriveStatCB",      hle_UmdWaitDriveStatCB);
    psp_hle_register(0x6AF9B50A, "sceUmdUser", "sceUmdCancelWaitDriveStat",  hle_UmdCancelWaitDriveStat);
    psp_hle_register(0x54F5FB11, "IoFileMgrForUser", "sceIoDevctl",           hle_IoDevctl);

    /* sceImpose */
    psp_hle_register(0x36AA6E91, "sceImpose", "sceImposeGetParam",          hle_ImposeParam);

    /* sceDisplay */
    psp_hle_register(0x36CDFADE, "sceDisplay", "sceDisplayWaitVblank",        hle_DisplayWaitVblankStart);
    psp_hle_register(0x9C6EAAD7, "sceDisplay", "sceDisplayWaitVblankStart",   hle_DisplayWaitVblankStart);
    psp_hle_register(0x8EB9EC49, "sceDisplay", "sceDisplayWaitVblankCB",      hle_DisplayWaitVblankStart);
    psp_hle_register(0x46F186C3, "sceDisplay", "sceDisplayWaitVblankStartCB", hle_DisplayWaitVblankStart);
    psp_hle_register(0xDBA6C4C4, "sceDisplay", "sceDisplayGetFrameBuf",       hle_DisplayGetFrameBuf);
    psp_hle_register(0x4D4E10EC, "sceDisplay", "sceDisplayGetVcount",        hle_DisplayGetVcount);

    /* Thread Synchronization & Timing */
    psp_hle_register(0x6D212BAC, "ThreadManForUser", "sceKernelWaitSemaCB",       hle_WaitSemaCB);
    psp_hle_register(0x369ED59D, "ThreadManForUser", "sceKernelGetSystemTimeLow",   hle_GetSystemTimeLow);
    psp_hle_register(0x19CFF145, "ThreadManForUser", "sceKernelCreateLwMutex",     hle_CreateLwMutex);
    psp_hle_register(0x60107536, "ThreadManForUser", "sceKernelDeleteLwMutex",     hle_DeleteLwMutex);
    psp_hle_register(0xBEA46419, "Kernel_Library",   "sceKernelLockLwMutex",       hle_LockLwMutex);
    psp_hle_register(0x15B6446B, "Kernel_Library",   "sceKernelUnlockLwMutex",     hle_UnlockLwMutex);

    /* sceSasCore */
    psp_hle_register(0x07F58C24, "sceSasCore", "__sceSasGetAllEnvelopeHeights",   hle_SasGetAllEnvelopeHeights);

    /* sceUtility & compiler version & module */
    psp_hle_register(0x2A2B3DE0, "sceUtility", "sceUtilityLoadNetModule",         hle_UtilityNetModule);
    psp_hle_register(0x91DE343C, "SysMemUserForUser", "sceKernelSetCompilerVersion", hle_CompilerVersion);
    psp_hle_register(0x8F2DF740, "ModuleMgrForUser", "sceKernelStopUnloadSelfModuleWithStatus", hle_StopUnloadSelfModuleWithStatus);

    /* sceUtilityMsgDialog */
    psp_hle_register(0x2AD8E239, "sceUtility", "sceUtilityMsgDialogInitStart",     hle_UtilityMsgDialogInitStart);
    psp_hle_register(0x67AF3428, "sceUtility", "sceUtilityMsgDialogGetStatus",     hle_UtilityMsgDialogGetStatus);
    psp_hle_register(0x95FC253B, "sceUtility", "sceUtilityMsgDialogUpdate",        hle_UtilityMsgDialogUpdate);
    psp_hle_register(0x9A1C91D7, "sceUtility", "sceUtilityMsgDialogShutdownStart", hle_UtilityMsgDialogShutdownStart);

    /* sceUtilitySavedata */
    psp_hle_register(0x50C4CD57, "sceUtility", "sceUtilitySavedataInitStart",     hle_UtilitySavedataInitStart);
    psp_hle_register(0x8874DBE0, "sceUtility", "sceUtilitySavedataGetStatus",     hle_UtilitySavedataGetStatus);
    psp_hle_register(0x9790B33C, "sceUtility", "sceUtilitySavedataShutdownStart", hle_UtilitySavedataShutdownStart);
    psp_hle_register(0xD4B95FFB, "sceUtility", "sceUtilitySavedataUpdate",        hle_UtilitySavedataUpdate);

    /* sceRtc */
    psp_hle_register(0xE7C27D1B, "sceRtc", "sceRtcGetCurrentClockLocalTime",     hle_RtcGetCurrentClockLocalTime);
    psp_hle_register(0x6FF40ACC, "sceRtc", "sceRtcGetTick",                      hle_RtcGetTick);

    /* sceAtrac3plus */
    psp_hle_register(0x7A20E7AF, "sceAtrac3plus", "sceAtracSetDataAndGetID",           hle_AtracSetDataAndGetID);
    psp_hle_register(0x61EB33F5, "sceAtrac3plus", "sceAtracReleaseAtracID",            hle_AtracReleaseAtracID);
    psp_hle_register(0x7DB31251, "sceAtrac3plus", "sceAtracAddStreamData",             hle_AtracAddStreamData);
    psp_hle_register(0x6A8C3CD5, "sceAtrac3plus", "sceAtracDecodeData",                hle_AtracDecodeData);
    psp_hle_register(0x9AE849A7, "sceAtrac3plus", "sceAtracGetRemainFrame",            hle_AtracGetRemainFrame);
    psp_hle_register(0x83BF7AFD, "sceAtrac3plus", "sceAtracSetSecondBuffer",           hle_AtracSetSecondBuffer);
    psp_hle_register(0x83E85EA0, "sceAtrac3plus", "sceAtracGetSecondBufferInfo",       hle_AtracGetSecondBufferInfo);
    psp_hle_register(0x868120B5, "sceAtrac3plus", "sceAtracSetLoopNum",                hle_AtracSetLoopNum);
    psp_hle_register(0xA2BBA8BE, "sceAtrac3plus", "sceAtracGetSoundSample",            hle_AtracGetSoundSample);
    psp_hle_register(0x2DD3E298, "sceAtrac3plus", "sceAtracGetBufferInfoForResetting", hle_AtracGetBufferInfoForResetting);
    psp_hle_register(0x644E5607, "sceAtrac3plus", "sceAtracResetPlayPosition",         hle_AtracResetPlayPosition);
    psp_hle_register(0x5D268707, "sceAtrac3plus", "sceAtracGetStreamDataInfo",         hle_AtracGetStreamDataInfo);
    psp_hle_register(0xE23E3A35, "sceAtrac3plus", "sceAtracGetNextDecodePosition",     hle_AtracGetNextDecodePosition);
    psp_hle_register(0xE88F759B, "sceAtrac3plus", "sceAtracGetInternalErrorInfo",      hle_AtracGetInternalErrorInfo);
}
