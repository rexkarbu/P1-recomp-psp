/* psprecomp — the smaller firmware libraries.
 *
 * Kernel_Library, UtilsForUser, StdioForUser, sceSuspendForUser,
 * LoadExecForUser, ModuleMgrForUser, sceCtrl and sceAudio. Individually small,
 * but collectively they are what a game's C runtime needs before main() gets
 * anywhere -- newlib's reentrancy setup alone wants interrupt masking, a
 * clock, and the standard file descriptors.
 */

#include "psprecomp/hle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

/* ---- Kernel_Library ------------------------------------------------------
 * Interrupt masking. The mask belongs to the *thread* that opened the critical
 * section, not to the machine: with several guest threads on one CPU, a global
 * flag would let one thread's suspend silence interrupts inside another
 * thread's unrelated bracket. The scheduler owns that state, because it is
 * also what decides whether an interrupt may be delivered at a park. */

static void hle_CpuSuspendIntr(void) {
    psp_ret((uint32_t)psp_threadman_intr_suspend());   /* the cookie resume expects */
}

static void hle_CpuResumeIntr(void) {
    psp_threadman_intr_resume(psp_arg(0));
    psp_ret(SCE_KERNEL_ERROR_OK);
}

/* ---- UtilsForUser -------------------------------------------------------- */

/* Cache maintenance. There is no cache to write back -- the recompiled code
 * and the GE share one flat backing store -- so these are genuinely no-ops
 * rather than unimplemented. */
static void hle_CacheOp(void) { psp_ret(SCE_KERNEL_ERROR_OK); }

static void hle_LibcTime(void) {
    time_t t = time(NULL);
    uint32_t out = psp_arg(0);
    if (out) psp_write32(out, (uint32_t)t);
    psp_ret((uint32_t)t);
}

static void hle_LibcClock(void) {
    /* Microseconds since start. A game that uses this for frame pacing needs
     * it to advance, so it is derived from the host clock rather than being a
     * constant -- a frozen clock makes a game either spin or run at infinite
     * speed, both of which look like a hang. */
    psp_ret((uint32_t)((uint64_t)clock() * 1000000ull / CLOCKS_PER_SEC));
}

static void hle_LibcGettimeofday(void) {
    uint32_t tv = psp_arg(0);
    if (tv) {
        time_t t = time(NULL);
        psp_write32(tv, (uint32_t)t);
        psp_write32(tv + 4, 0);
    }
    psp_ret(SCE_KERNEL_ERROR_OK);
}

/* General-purpose I/O pins, wired to the debug board. Nothing is connected. */
static void hle_GetGPI(void) { psp_ret(0); }

/* ---- StdioForUser -------------------------------------------------------- */
/* These return the file descriptors, which sceIoWrite then recognises. */
static void hle_Stdin(void)  { psp_ret(0); }
static void hle_Stdout(void) { psp_ret(1); }
static void hle_Stderr(void) { psp_ret(2); }

/* ---- sceSuspendForUser --------------------------------------------------- */
/* Power management around suspend. Nothing suspends here. */
static void hle_ok(void) { psp_ret(SCE_KERNEL_ERROR_OK); }

/* ---- LoadExecForUser ----------------------------------------------------- */

static int g_exit_requested;
int psp_exit_requested(void) { return g_exit_requested; }

static void hle_ExitGame(void) {
    /* A game calling this is finished. Recording it rather than terminating
     * the process lets the host report what happened and dump its counters. */
    g_exit_requested = 1;
    fprintf(stderr, "psprecomp: the game called sceKernelExitGame\n");
    psp_ret(SCE_KERNEL_ERROR_OK);
}

static void hle_RegisterExitCallback(void) { psp_ret(SCE_KERNEL_ERROR_OK); }

/* ---- ModuleMgrForUser ---------------------------------------------------- */
/* A game-sharing microgame is self-contained and does not load further
 * modules, so these report a plausible identity rather than doing anything.
 * A title that genuinely loads PRXs at run time will need real ones. */
/* These report the id of the one loaded module.
 *
 * A previous note here recorded the opposite -- that returning an id was a
 * "plausible lie" and an error was the truthful answer -- on the strength of a
 * test showing byte-identical output either way. **That test was run while
 * `jal` never assigned `$ra`**, so every non-leaf function in the program was
 * returning through a stale register. A falsification obtained under broken
 * codegen is not a falsification.
 *
 * Re-run after that fix, the two answers differ clearly: returning an id makes
 * the game's own "libc:_getmodreent: no reent structure" diagnostic disappear
 * and drops bad memory accesses from 3 to 0.
 *
 * And an id is the *truthful* answer. The question is "which module owns this
 * address", a self-contained microgame is exactly one module, and the host has
 * loaded it. Reporting 1 states that; reporting UNKNOWN_MODULE denies a module
 * that demonstrably exists. */
#define SCE_KERNEL_ERROR_UNKNOWN_MODULE 0x80020139u
#define PSP_MAIN_MODULE_ID 1u

static void hle_GetModuleId(void)          { psp_ret(PSP_MAIN_MODULE_ID); }
static void hle_GetModuleIdByAddress(void) { psp_ret(PSP_MAIN_MODULE_ID); }
static void hle_ModuleOk(void)             { psp_ret(SCE_KERNEL_ERROR_OK); }

/* ---- sceCtrl ------------------------------------------------------------- */

static uint32_t g_buttons;
static uint8_t  g_analog_x = 128, g_analog_y = 128;
static uint32_t g_ctrl_frame;

void psp_ctrl_set(uint32_t buttons, uint8_t ax, uint8_t ay) {
    g_buttons = buttons;
    g_analog_x = ax;
    g_analog_y = ay;
}

uint32_t psp_ctrl_buttons(void)  { return g_buttons; }
uint8_t  psp_ctrl_analog_x(void) { return g_analog_x; }
uint8_t  psp_ctrl_analog_y(void) { return g_analog_y; }

static void hle_CtrlSet(void) { psp_ret(SCE_KERNEL_ERROR_OK); }

/* SceCtrlData: u32 timestamp, u32 buttons, u8 lx, u8 ly, then padding to 16. */
static void hle_ReadBufferPositive(void) {
    uint32_t buf = psp_arg(0), count = psp_arg(1);
    if (!count) count = 1;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t at = buf + i * 16;
        psp_write32(at, g_ctrl_frame++);
        psp_write32(at + 4, g_buttons);
        psp_write8(at + 8, g_analog_x);
        psp_write8(at + 9, g_analog_y);
        for (int k = 10; k < 16; k++) psp_write8(at + (uint32_t)k, 0);
    }
    psp_ret(count);
}

/* ---- sceAudio ------------------------------------------------------------ */

#define AUDIO_CHANNELS 8

typedef struct { int reserved; uint32_t samples; uint32_t format; } audio_ch;
static audio_ch g_audio[AUDIO_CHANNELS];
static uint64_t g_audio_blocks;

uint64_t psp_audio_blocks(void) { return g_audio_blocks; }

static float g_master_volume = 1.0f;

void psp_audio_set_master_volume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    g_master_volume = vol;
}

float psp_audio_get_master_volume(void) {
    return g_master_volume;
}

static inline int clamp16(int v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return v;
}

#ifdef _WIN32
#define HOST_AUDIO_BUFFERS 32

typedef struct {
    HWAVEOUT hWaveOut;
    WAVEHDR  headers[HOST_AUDIO_BUFFERS];
    int16_t *pcm_buffers[HOST_AUDIO_BUFFERS];
    uint32_t buf_capacity_samples[HOST_AUDIO_BUFFERS];
    int      next_buf;
    int      opened;
} host_audio_ch;

static host_audio_ch g_host_audio[AUDIO_CHANNELS];

static void host_audio_close_ch(int ch) {
    if (ch < 0 || ch >= AUDIO_CHANNELS) return;
    if (g_host_audio[ch].opened && g_host_audio[ch].hWaveOut) {
        waveOutReset(g_host_audio[ch].hWaveOut);
        for (int i = 0; i < HOST_AUDIO_BUFFERS; i++) {
            WAVEHDR *hdr = &g_host_audio[ch].headers[i];
            if (hdr->dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(g_host_audio[ch].hWaveOut, hdr, sizeof(WAVEHDR));
            }
            if (g_host_audio[ch].pcm_buffers[i]) {
                free(g_host_audio[ch].pcm_buffers[i]);
                g_host_audio[ch].pcm_buffers[i] = NULL;
            }
            g_host_audio[ch].buf_capacity_samples[i] = 0;
        }
        waveOutClose(g_host_audio[ch].hWaveOut);
        g_host_audio[ch].hWaveOut = NULL;
    }
    g_host_audio[ch].opened = 0;
    g_host_audio[ch].next_buf = 0;
}

static int host_audio_open_ch(int ch) {
    if (ch < 0 || ch >= AUDIO_CHANNELS) return -1;
    if (g_host_audio[ch].opened) return 0;

    int channels = (g_audio[ch].format == 0x10) ? 1 : 2; /* 0: Stereo, 0x10: Mono */
    WAVEFORMATEX wfx = {0};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)channels;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(wfx.nChannels * (wfx.wBitsPerSample / 8));
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    MMRESULT res = waveOutOpen(&g_host_audio[ch].hWaveOut, WAVE_MAPPER, &wfx,
                              0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) {
        fprintf(stderr, "audio: waveOutOpen failed for ch %d (err %u)\n", ch, (unsigned)res);
        return -1;
    }

    memset(g_host_audio[ch].headers, 0, sizeof(g_host_audio[ch].headers));
    for (int i = 0; i < HOST_AUDIO_BUFFERS; i++) {
        g_host_audio[ch].pcm_buffers[i] = NULL;
        g_host_audio[ch].buf_capacity_samples[i] = 0;
    }
    g_host_audio[ch].next_buf = 0;
    g_host_audio[ch].opened = 1;
    return 0;
}

static void host_audio_play(uint32_t ch, int vol_l, int vol_r, uint32_t buf_addr) {
    if (ch >= AUDIO_CHANNELS) return;
    if (!g_host_audio[ch].opened) {
        if (host_audio_open_ch((int)ch) != 0) return;
    }

    uint32_t samples = g_audio[ch].samples;
    if (!samples || !buf_addr) return;

    if (vol_l < 0) vol_l = 0;
    if (vol_l > 32768) vol_l = 32768;
    if (vol_r < 0) vol_r = 0;
    if (vol_r > 32768) vol_r = 32768;

    int channels = (g_audio[ch].format == 0x10) ? 1 : 2;
    uint32_t bytes = samples * (uint32_t)channels * sizeof(int16_t);

    /* Find an available buffer slot (not prepared or already finished playing) */
    int slot = -1;
    for (int retry = 0; retry < 5; retry++) {
        for (int i = 0; i < HOST_AUDIO_BUFFERS; i++) {
            int idx = (g_host_audio[ch].next_buf + i) % HOST_AUDIO_BUFFERS;
            WAVEHDR *h = &g_host_audio[ch].headers[idx];
            if (!(h->dwFlags & WHDR_PREPARED) || (h->dwFlags & WHDR_DONE)) {
                slot = idx;
                break;
            }
        }
        if (slot >= 0) break;
        Sleep(1);
    }

    if (slot < 0) {
        /* Sound card driver still processing all 32 buffers; drop safely to avoid memory race */
        return;
    }

    WAVEHDR *hdr = &g_host_audio[ch].headers[slot];
    if (hdr->dwFlags & WHDR_PREPARED) {
        waveOutUnprepareHeader(g_host_audio[ch].hWaveOut, hdr, sizeof(WAVEHDR));
    }

    if (g_host_audio[ch].buf_capacity_samples[slot] < samples) {
        free(g_host_audio[ch].pcm_buffers[slot]);
        g_host_audio[ch].pcm_buffers[slot] = (int16_t *)malloc(bytes);
        g_host_audio[ch].buf_capacity_samples[slot] = samples;
    }

    int16_t *dst = g_host_audio[ch].pcm_buffers[slot];
    float master = g_master_volume;
    if (channels == 2) {
        for (uint32_t i = 0; i < samples; i++) {
            int32_t sl = (int16_t)psp_read16(buf_addr + i * 4);
            int32_t sr = (int16_t)psp_read16(buf_addr + i * 4 + 2);
            /* Scale with channel volume (0..0x8000) and master volume */
            sl = (int32_t)((float)((sl * vol_l) >> 15) * master);
            sr = (int32_t)((float)((sr * vol_r) >> 15) * master);
            dst[i * 2 + 0] = (int16_t)clamp16(sl);
            dst[i * 2 + 1] = (int16_t)clamp16(sr);
        }
    } else {
        for (uint32_t i = 0; i < samples; i++) {
            int32_t s = (int16_t)psp_read16(buf_addr + i * 2);
            s = (int32_t)((float)((s * vol_l) >> 15) * master);
            dst[i] = (int16_t)clamp16(s);
        }
    }

    memset(hdr, 0, sizeof(WAVEHDR));
    hdr->lpData = (LPSTR)dst;
    hdr->dwBufferLength = bytes;
    waveOutPrepareHeader(g_host_audio[ch].hWaveOut, hdr, sizeof(WAVEHDR));
    waveOutWrite(g_host_audio[ch].hWaveOut, hdr, sizeof(WAVEHDR));

    g_host_audio[ch].next_buf = (slot + 1) % HOST_AUDIO_BUFFERS;
}
#endif

static void hle_ChReserve(void) {
    if (getenv("PSPRECOMP_TRACE_SAS"))
        fprintf(stderr, "audio: ChReserve ch %d samples %u format %u\n",
                (int)psp_arg(0), psp_arg(1), psp_arg(2));
    /* (channel, samplecount, format) -- channel -1 means "any". */
    int32_t ch = (int32_t)psp_arg(0);
    if (ch < 0) {
        for (int i = 0; i < AUDIO_CHANNELS; i++)
            if (!g_audio[i].reserved) { ch = i; break; }
    }
    if (ch < 0 || ch >= AUDIO_CHANNELS) { psp_ret(0x80000001); return; }
    g_audio[ch].reserved = 1;
    g_audio[ch].samples = psp_arg(1);
    g_audio[ch].format  = psp_arg(2);
#ifdef _WIN32
    host_audio_open_ch(ch);
#endif
    psp_ret((uint32_t)ch);
}

static void hle_ChRelease(void) {
    int32_t ch = (int32_t)psp_arg(0);
    if (ch < 0 || ch >= AUDIO_CHANNELS) { psp_ret(0x80000001); return; }
#ifdef _WIN32
    host_audio_close_ch(ch);
#endif
    g_audio[ch].reserved = 0;
    psp_ret(SCE_KERNEL_ERROR_OK);
}

static void hle_Output(void) {
    uint32_t ch = psp_arg(0);
    int vol = (int)psp_arg(1);
    uint32_t buf = psp_arg(2);
#ifdef _WIN32
    host_audio_play(ch, vol, vol, buf);
#endif
    g_audio_blocks++;
    psp_ret(ch < AUDIO_CHANNELS ? g_audio[ch].samples : 0);
}

static void hle_OutputPanned(void) {
    uint32_t ch = psp_arg(0);
    int leftvol = (int)psp_arg(1);
    int rightvol = (int)psp_arg(2);
    uint32_t buf = psp_arg(3);
#ifdef _WIN32
    host_audio_play(ch, leftvol, rightvol, buf);
#endif
    g_audio_blocks++;
    psp_ret(ch < AUDIO_CHANNELS ? g_audio[ch].samples : 0);
}

static void hle_OutputBlocking(void) {
    uint32_t ch = psp_arg(0);
    int vol = (int)psp_arg(1);
    uint32_t buf = psp_arg(2);
    uint32_t samples = ch < AUDIO_CHANNELS ? g_audio[ch].samples : 0;
    static int traced;
    if (getenv("PSPRECOMP_TRACE_SAS") && traced++ < 4)
        fprintf(stderr, "audio: OutputBlocking ch %u samples %u buf 0x%08X vol %d\n", ch, samples, buf, vol);
#ifdef _WIN32
    host_audio_play(ch, vol, vol, buf);
#endif
    g_audio_blocks++;
#if PSPRECOMP_REAL_THREADS
    psp_threadman_yield();
#else
    const uint64_t play_us = (uint64_t)samples * 1000000ull / 44100ull;
    psp_threadman_delay(play_us);
#endif
    psp_ret(samples);
}

static void hle_OutputPannedBlocking(void) {
    uint32_t ch = psp_arg(0);
    int leftvol = (int)psp_arg(1);
    int rightvol = (int)psp_arg(2);
    uint32_t buf = psp_arg(3);
    uint32_t samples = ch < AUDIO_CHANNELS ? g_audio[ch].samples : 0;
    static int traced;
    if (getenv("PSPRECOMP_TRACE_SAS") && traced++ < 4)
        fprintf(stderr, "audio: OutputPannedBlocking ch %u samples %u buf 0x%08X lvol %d rvol %d\n",
                ch, samples, buf, leftvol, rightvol);
#ifdef _WIN32
    host_audio_play(ch, leftvol, rightvol, buf);
#endif
    g_audio_blocks++;
#if PSPRECOMP_REAL_THREADS
    psp_threadman_yield();
#else
    const uint64_t play_us = (uint64_t)samples * 1000000ull / 44100ull;
    psp_threadman_delay(play_us);
#endif
    psp_ret(samples);
}

/* The sample count a channel outputs per call. It is set at reserve time and
 * changed here; ignoring the change left WTF's channel at its reserved length,
 * so each blocking output waited for 4,096 samples while the game mixed 256,
 * and its audio ran 16 times slow. */
static void hle_SetChannelDataLen(void) {
    uint32_t ch = psp_arg(0);
    if (ch >= AUDIO_CHANNELS) { psp_ret(0x80260003); return; }   /* invalid channel */
    g_audio[ch].samples = psp_arg(1);
    if (getenv("PSPRECOMP_TRACE_SAS"))
        fprintf(stderr, "audio: SetChannelDataLen ch %u samples %u\n", ch, psp_arg(1));
    psp_ret(SCE_KERNEL_ERROR_OK);
}

/* Zero remaining means "ready for more", so a game's audio loop keeps going. */
static void hle_GetChannelRestLength(void) { psp_ret(0); }

void psp_misc_reset(void) {
    /* The interrupt mask is per thread and lives in the scheduler; a reset
     * here cannot reach another thread's bracket, and should not. */
    g_exit_requested = 0;
    g_buttons = 0;
    g_analog_x = g_analog_y = 128;
    g_ctrl_frame = 0;
#ifdef _WIN32
    for (int i = 0; i < AUDIO_CHANNELS; i++) {
        host_audio_close_ch(i);
    }
#endif
    memset(g_audio, 0, sizeof g_audio);
    g_audio_blocks = 0;
}

void psp_misc_init(void) { psp_misc_reset(); }

void psp_misc_register(void) {
    psp_hle_register(0x092968F4, "Kernel_Library", "sceKernelCpuSuspendIntr", hle_CpuSuspendIntr);
    psp_hle_register(0x5F10D406, "Kernel_Library", "sceKernelCpuResumeIntr",  hle_CpuResumeIntr);

    psp_hle_register(0x79D1C3FA, "UtilsForUser", "sceKernelDcacheWritebackAll",           hle_CacheOp);
    psp_hle_register(0xB435DEC5, "UtilsForUser", "sceKernelDcacheWritebackInvalidateAll", hle_CacheOp);
    psp_hle_register(0x27CC57F0, "UtilsForUser", "sceKernelLibcTime",         hle_LibcTime);
    psp_hle_register(0x91E4F6A7, "UtilsForUser", "sceKernelLibcClock",        hle_LibcClock);
    psp_hle_register(0x71EC4271, "UtilsForUser", "sceKernelLibcGettimeofday", hle_LibcGettimeofday);
    psp_hle_register(0x37FB5C42, "UtilsForUser", "sceKernelGetGPI",           hle_GetGPI);

    psp_hle_register(0x172D316E, "StdioForUser", "sceKernelStdin",  hle_Stdin);
    psp_hle_register(0xA6BAB2E9, "StdioForUser", "sceKernelStdout", hle_Stdout);
    psp_hle_register(0xF78BA90A, "StdioForUser", "sceKernelStderr", hle_Stderr);

    psp_hle_register(0xEADB1BD7, "sceSuspendForUser", "sceKernelPowerLock",   hle_ok);
    psp_hle_register(0x3AEE7261, "sceSuspendForUser", "sceKernelPowerUnlock", hle_ok);
    psp_hle_register(0x090CCB3F, "sceSuspendForUser", "sceKernelPowerTick",   hle_ok);

    psp_hle_register(0x05572A5F, "LoadExecForUser", "sceKernelExitGame",             hle_ExitGame);
    psp_hle_register(0x4AC57943, "LoadExecForUser", "sceKernelRegisterExitCallback", hle_RegisterExitCallback);

    /* Observed but unidentified. The game calls this three times on its
     * heap-setup path, and an unimplemented call returns 0 -- which for a
     * module query means "no module" and makes heap establishment fail.
     * Eighteen plausible ModuleMgr names were tried against SHA-1 with no
     * match, so the name is genuinely unknown and is not invented here. */
    psp_hle_register_unnamed(0xF9275D98, "ModuleMgrForUser", hle_GetModuleId);
    psp_hle_register(0xF0A26395, "ModuleMgrForUser", "sceKernelGetModuleId",          hle_GetModuleId);
    psp_hle_register(0xD8B73127, "ModuleMgrForUser", "sceKernelGetModuleIdByAddress", hle_GetModuleIdByAddress);
    psp_hle_register(0x50F0C1EC, "ModuleMgrForUser", "sceKernelStartModule",          hle_ModuleOk);
    psp_hle_register(0xD1FF982A, "ModuleMgrForUser", "sceKernelStopModule",           hle_ModuleOk);
    psp_hle_register(0x2E0911AA, "ModuleMgrForUser", "sceKernelUnloadModule",         hle_ModuleOk);

    psp_hle_register(0x1F4011E6, "sceCtrl", "sceCtrlSetSamplingMode",     hle_CtrlSet);
    psp_hle_register(0x6A2774F3, "sceCtrl", "sceCtrlSetSamplingCycle",    hle_CtrlSet);
    psp_hle_register(0x1F803938, "sceCtrl", "sceCtrlReadBufferPositive",  hle_ReadBufferPositive);

    psp_hle_register(0x5EC81C55, "sceAudio", "sceAudioChReserve",            hle_ChReserve);
    psp_hle_register(0x6FC46853, "sceAudio", "sceAudioChRelease",            hle_ChRelease);
    psp_hle_register(0x136CAF51, "sceAudio", "sceAudioOutputBlocking",       hle_OutputBlocking);
    psp_hle_register(0x13F592BC, "sceAudio", "sceAudioOutputPannedBlocking", hle_OutputPannedBlocking);
    psp_hle_register(0x8C1009B2, "sceAudio", "sceAudioOutput",               hle_Output);
    psp_hle_register(0xE2D56B2D, "sceAudio", "sceAudioOutputPanned",         hle_OutputPanned);
    psp_hle_register(0xB011922F, "sceAudio", "sceAudioGetChannelRestLength", hle_GetChannelRestLength);
    psp_hle_register(0xCB2E439E, "sceAudio", "sceAudioSetChannelDataLen",    hle_SetChannelDataLen);
    psp_hle_register(0x95FD0C2D, "sceAudio", "sceAudioChangeChannelConfig",  hle_ok);
    psp_hle_register(0xB7E1D8E7, "sceAudio", "sceAudioChangeChannelVolume",  hle_ok);
}
