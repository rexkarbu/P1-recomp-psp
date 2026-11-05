/*
 * Persona 1 PSP Native PC Host Launcher (Static Recompilation)
 * 
 * Pipeline:
 * 1. Initialize PSP memory map (Scratchpad, VRAM, 32MB RAM) via psp_mem_init()
 * 2. Map relocatable PRX image memory via psp_mem_map_module(0x00000000, 0x00600000)
 * 3. Load ELF segments from EBOOT_RELOC.ELF into guest memory:
 *    - Segment 0 (Code/Rodata): VAddr=0x00000000, FileOff=0x000000C0, size 0x2634B0
 *    - Segment 1 (Data/Bss):    VAddr=0x002634C0, FileOff=0x00263580, file size 0x8E2D4, mem size 0x2B138C
 * 4. Setup PSP file virtual filesystem root (disc0:/ -> game_root/disc/)
 * 5. Verify / load USRDIR/datapack.bin assets
 * 6. Register recompiled functions into dispatch table
 * 7. Initialize HLE subsystems and custom firmware hooks (sceUmdUser, sceImpose, sceDisplay)
 * 8. Open Win32 Graphics Window (psp_window_start)
 * 9. Setup CPU registers ($gp, $sp, $k0, $ra)
 * 10. Jump to entry point psp_func_00000114()
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <psprecomp/mem.h>
#include <psprecomp/cpu.h>
#include <psprecomp/hle.h>
#include <psprecomp/dispatch.h>
#include <psprecomp/render.h>
#include <psprecomp/window.h>
#include "p1_funcs.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

#define MODULE_SPAN   0x00600000u /* ~6 MB to cover .text, .data, and .bss */
#define MODULE_GP     0x000962D0u /* Global Pointer from SceModuleInfo */
#define STACK_ADDR    0x08F00000u /* 16 MB stack in main RAM */
#define TCB_ADDR      0x08800000u /* Thread Control Block pointer */
#define REENT_ADDR    0x08800100u /* Libc reentrant structure pointer */

extern void persona_register_custom_hle(void);

static int load_elf_segments(const char *elf_path) {
    printf("[HOST] Loading ELF segments from: %s\n", elf_path);
    FILE *f = fopen(elf_path, "rb");
    if (!f) {
        fprintf(stderr, "[HOST] ERROR: Failed to open ELF file '%s'\n", elf_path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer || fread(buffer, 1, file_size, f) != (size_t)file_size) {
        fprintf(stderr, "[HOST] ERROR: Failed to read ELF file into host memory\n");
        if (buffer) free(buffer);
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Verify ELF header */
    if (memcmp(buffer, "\x7F" "ELF", 4) != 0) {
        fprintf(stderr, "[HOST] ERROR: Invalid ELF magic in '%s'\n", elf_path);
        free(buffer);
        return -1;
    }

    /* Read Program Header offset and number of headers */
    uint32_t phoff = *(uint32_t *)(buffer + 28);
    uint16_t phnum = *(uint16_t *)(buffer + 44);
    printf("[HOST] ELF has %u program headers at offset 0x%X\n", phnum, phoff);

    /* Map module memory space */
    if (psp_mem_map_module(0x00000000u, MODULE_SPAN) != 0) {
        fprintf(stderr, "[HOST] ERROR: Failed to map module memory space\n");
        free(buffer);
        return -1;
    }

    /* Iterate program headers and load PT_LOAD segments */
    for (int i = 0; i < phnum; i++) {
        uint8_t *ph = buffer + phoff + (i * 32);
        uint32_t type   = *(uint32_t *)(ph + 0);
        uint32_t offset = *(uint32_t *)(ph + 4);
        uint32_t vaddr  = *(uint32_t *)(ph + 8);
        uint32_t filesz = *(uint32_t *)(ph + 16);
        uint32_t memsz  = *(uint32_t *)(ph + 20);

        if (type == 1) { /* PT_LOAD */
            printf("[HOST] Segment %d (PT_LOAD): VAddr=0x%08X FileOff=0x%08X FileSz=0x%08X MemSz=0x%08X\n",
                   i, vaddr, offset, filesz, memsz);

            if (offset + filesz > (uint32_t)file_size) {
                fprintf(stderr, "[HOST] ERROR: Segment %d exceeds file bounds!\n", i);
                free(buffer);
                return -1;
            }

            /* Copy file content to guest memory */
            if (psp_mem_write_block(vaddr, buffer + offset, filesz) != 0) {
                fprintf(stderr, "[HOST] ERROR: Failed writing segment %d into guest memory\n", i);
                free(buffer);
                return -1;
            }

            /* Zero out any .bss area (memsz > filesz) */
            if (memsz > filesz) {
                uint32_t bss_size = memsz - filesz;
                uint8_t *bss_zero = (uint8_t *)calloc(1, bss_size);
                if (bss_zero) {
                    psp_mem_write_block(vaddr + filesz, bss_zero, bss_size);
                    free(bss_zero);
                }
            }
        }
    }

    free(buffer);
    printf("[HOST] All ELF segments successfully placed into PSP guest memory!\n");
    return 0;
}

static int verify_and_load_datapack(const char *asset_path) {
    printf("[HOST] Verifying asset file: %s\n", asset_path);
    FILE *f = fopen(asset_path, "rb");
    if (!f) {
        fprintf(stderr, "[HOST] WARNING: Asset file '%s' not found!\n", asset_path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    uint8_t header[16] = {0};
    fread(header, 1, sizeof(header), f);
    fclose(f);

    printf("[HOST] Asset datapack.bin verified! Size: %ld bytes\n", size);
    printf("[HOST] Header bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
           header[0], header[1], header[2], header[3],
           header[4], header[5], header[6], header[7]);
    return 0;
}

#ifdef _WIN32
static LONG WINAPI vectored_crash_handler(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode >= 0x80000000u &&
        ep->ExceptionRecord->ExceptionCode != 0xE06D7363u) {
        char buf[512];
        DWORD written;
        snprintf(buf, sizeof(buf),
            "\n[CRASH] Code: 0x%08lX Address: %p\n",
            (unsigned long)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, (DWORD)strlen(buf), &written, NULL);
        if (ep->ExceptionRecord->ExceptionCode == 0xC0000005u) {
            snprintf(buf, sizeof(buf),
                "[CRASH] Access Violation: %s at %p\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
                (void*)ep->ExceptionRecord->ExceptionInformation[1]);
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, (DWORD)strlen(buf), &written, NULL);
        }
        snprintf(buf, sizeof(buf),
            "[CRASH] Last recompiled fn: 0x%08X Guest RA: 0x%08X SP: 0x%08X\n",
            psp_trace_last(), psp_cpu.r[31], psp_cpu.r[29]);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), buf, (DWORD)strlen(buf), &written, NULL);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    timeBeginPeriod(1);
    AddVectoredExceptionHandler(1, vectored_crash_handler);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    int window_scale = 2;
    int headless = 0;
    uint64_t frame_limit = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            window_scale = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--fullscreen") == 0) {
            psp_window_set_initial_fullscreen(1);
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frame_limit = (uint64_t)strtoull(argv[++i], NULL, 10);
        }
    }

    printf("=================================================================\n");
    printf("  Shin Megami Tensei: Persona (PSP) - Native PC Recomp Host\n");
    printf("=================================================================\n");

    /* 1. Inisialisasi memory map PSP */
    printf("\n[STEP 1] Initializing PSP Memory Map...\n");
    if (psp_mem_init() != 0) {
        fprintf(stderr, "[HOST] FATAL: psp_mem_init() failed!\n");
        return 1;
    }
    printf("[HOST] PSP Memory initialized (32MB RAM, 2MB VRAM, 16KB Scratchpad)\n");

    /* 2. Load ELF segments ke guest memory */
    printf("\n[STEP 2] Placing EBOOT_RELOC.ELF segments into guest memory...\n");
    const char *elf_path = "extracted/EBOOT_RELOC.ELF";
    FILE *f_elf_chk = fopen(elf_path, "rb");
    if (f_elf_chk) {
        fclose(f_elf_chk);
    } else {
        elf_path = "d:/project/tes/extracted/EBOOT_RELOC.ELF";
    }

    if (load_elf_segments(elf_path) != 0) {
        fprintf(stderr, "[HOST] FATAL: load_elf_segments() failed!\n");
        psp_mem_free();
        return 1;
    }

    /* 4. Inisialisasi CPU & Sub-sistem HLE */
    printf("\n[STEP 4] Initializing CPU and HLE Subsystems...\n");
    psp_cpu_reset();
    psp_hle_init();

    /* 3. Setup File System Virtual (HLE IoFileMgr) */
    printf("\n[STEP 3] Setting up Virtual File System (disc0:/ mapping & ISO access)...\n");
    const char *game_root = "game_root";
    const char *iso_path  = "Shin Megami Tensei - Persona (USA).iso";
    const char *datapack_path = "game_root/disc/PSP_GAME/USRDIR/datapack.bin";

    FILE *f_dp_chk = fopen(datapack_path, "rb");
    if (f_dp_chk) {
        fclose(f_dp_chk);
    } else {
        game_root = "d:/project/tes/game_root";
        iso_path  = "d:/project/tes/Shin Megami Tensei - Persona (USA).iso";
        datapack_path = "d:/project/tes/game_root/disc/PSP_GAME/USRDIR/datapack.bin";
    }

    psp_io_set_root(game_root);
    psp_io_set_iso(iso_path);
    printf("[HOST] VFS Root set to: %s\n", game_root);
    printf("[HOST] Raw UMD ISO image set to: %s\n", iso_path);

    verify_and_load_datapack(datapack_path);

    /* Daftarkan custom HLE hooks (sceUmd, sceImpose, sceDisplay) */
    persona_register_custom_hle();

    /* 5. Daftarkan seluruh fungsi recompiled ke dispatch table */
    printf("\n[STEP 5] Registering recompiled functions into Dispatch Table...\n");
    psp_recomp_register();
    printf("[HOST] All functions registered successfully!\n");

    /* 6. Siapkan register loader CPU */
    printf("\n[STEP 6] Setting up CPU Loader ABI Registers...\n");
    psp_cpu.r[PSP_REG_GP] = MODULE_GP;   /* $gp: 0x000962D0 */
    psp_cpu.r[PSP_REG_SP] = STACK_ADDR;  /* $sp: 0x08F00000 */
    psp_cpu.r[PSP_REG_A0] = 0;           /* $a0: argc = 0 */
    psp_cpu.r[PSP_REG_A1] = 0;           /* $a1: argv = NULL */
    psp_cpu.r[PSP_REG_RA] = 0xDEADBEEFu; /* $ra: return sentinel */
    psp_cpu.r[26]         = TCB_ADDR;    /* $k0: Thread Control Block */
    psp_write32(TCB_ADDR + 4, REENT_ADDR);

    printf("[HOST] $gp = 0x%08X\n", psp_cpu.r[PSP_REG_GP]);
    printf("[HOST] $sp = 0x%08X\n", psp_cpu.r[PSP_REG_SP]);
    printf("[HOST] $k0 = 0x%08X (reent at 0x%08X)\n", psp_cpu.r[26], REENT_ADDR);

    /* 7. Buka Window Grafis */
    if (headless) {
        printf("\n[STEP 7] Graphics Window skipped (--headless specified).\n");
    } else {
        printf("\n[STEP 7] Starting Graphics Window (Scale %dx = %dx%d)...\n",
               window_scale, 480 * window_scale, 272 * window_scale);
        if (psp_window_start("Shin Megami Tensei: Persona - Native PC (Recompiled)", window_scale) != 0) {
            fprintf(stderr, "[HOST] WARNING: psp_window_start() failed! Falling back to headless mode.\n");
        } else {
            printf("[HOST] Window successfully opened! Resolution: %dx%d (%dx PSP native)\n",
                   480 * window_scale, 272 * window_scale, window_scale);
            printf("[HOST] Hardware Backend: OpenGL (WGL Hardware Double-Buffered Presentation)\n");
            printf("[HOST] Controls:\n");
            printf("       * D-Pad: Arrow Keys OR WASD | Left Analog Stick (XInput)\n");
            printf("       * Confirm (Cross): Z / Space / J / Gamepad A\n");
            printf("       * Cancel (Circle): X / Esc / K / Gamepad B\n");
            printf("       * Square: C / U / Gamepad X\n");
            printf("       * Triangle: V / I / Gamepad Y\n");
            printf("       * Triggers (L/R): Q / E / LShift / RShift / LB / RB\n");
            printf("       * Start / Select: Enter / Tab / Backspace / Gamepad Start / Back\n");
            printf("       * Hotkeys: F1 (Toggle Crisp / Smooth Filter) | F11 / Alt+Enter (Fullscreen)\n");
        }
    }

    /* 8. Panggil Entry Point! */
    printf("\n[STEP 8] Calling Entry Point: psp_func_00000114()...\n");
    printf("-----------------------------------------------------------------\n");

    psp_func_00000114();

    /* module_start returns immediately after StartThread(user_main).
     * With PSPRECOMP_REAL_THREADS the game runs on host threads — release
     * the baton so guest threads can acquire the CPU. */
#if PSPRECOMP_REAL_THREADS
    psp_threadman_boot_done();
    printf("[HOST] module_start returned, baton released to game threads.\n");
    printf("[HOST] Waiting for game to finish (close the window to quit)...\n");
    int watchdog_ticks = 0;
    while ((headless || !psp_window_closed()) && !psp_exit_requested()) {
        if (frame_limit > 0 && psp_display_vblanks() >= frame_limit) {
            printf("[HOST] Frame limit %llu reached, exiting cleanly.\n", (unsigned long long)frame_limit);
            break;
        }
#ifdef _WIN32
        Sleep(100);
#else
        { struct timespec ts = {0, 100000000}; nanosleep(&ts, NULL); }
#endif
        if (++watchdog_ticks % 30 == 0) {
            printf("[WATCHDOG] vbl=%llu px=%llu last_fn=0x%08X\n",
                   (unsigned long long)psp_display_vblanks(),
                   (unsigned long long)psp_render_pixels(),
                   psp_trace_last());
            psp_threadman_dump(stdout);
            fflush(stdout);
        }
    }
    printf("[HOST] Game loop ended. Window closed=%d, exit_requested=%d\n",
           psp_window_closed(), psp_exit_requested());
#endif

    printf("-----------------------------------------------------------------\n");
    printf("[HOST] Execution finished! Exit code ($v0): 0x%08X (%d)\n",
           psp_cpu.r[PSP_REG_V0], (int)psp_cpu.r[PSP_REG_V0]);

    /* Dump runtime statistics */
    printf("\n[HOST] === Runtime Statistics ===\n");
    printf("[HOST] Threads created: %llu, started: %llu\n",
           (unsigned long long)psp_threadman_created(),
           (unsigned long long)psp_threadman_started());
    printf("[HOST] Context switches: %llu, parks: %llu\n",
           (unsigned long long)psp_threadman_switches(),
           (unsigned long long)psp_threadman_parks());
    printf("[HOST] Virtual time: %llu us\n",
           (unsigned long long)psp_threadman_vtime_us());
    printf("[HOST] VBlank count: %llu\n",
           (unsigned long long)psp_display_vblanks());
    printf("[HOST] Rendered pixels: %llu\n",
           (unsigned long long)psp_render_pixels());
    printf("[HOST] Unimplemented firmware calls: %llu\n",
           (unsigned long long)psp_hle_unimplemented_calls());
    psp_threadman_dump(stdout);
    psp_ge_dump_stats(stdout);

#ifdef _WIN32
    timeEndPeriod(1);
    ExitProcess(0);
#else
    _Exit(0);
#endif
    return 0;
}
