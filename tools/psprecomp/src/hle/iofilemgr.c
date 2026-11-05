/* psprecomp — IoFileMgrForUser.
 *
 * The PSP's file API, mapped onto a host directory. Games address the UMD as
 * `disc0:/` and the Memory Stick as `ms0:/`, so those prefixes are rewritten to
 * subdirectories of a root the host chooses.
 *
 * Reads go straight into guest memory, which means a game loading assets is
 * doing the real thing -- and a texture or model that arrives byte-correct is
 * strong evidence the recompiled code around it is behaving too.
 */

#include "psprecomp/hle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <io.h>
#else
#  include <dirent.h>
#endif

#define MAX_FILES 64
#define MAX_DIRS  16

/* Open flags, as the guest passes them. */
#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_RDWR   0x0003
#define PSP_O_APPEND 0x0100
#define PSP_O_CREAT  0x0200
#define PSP_O_TRUNC  0x0400

typedef struct {
    FILE *f;
    int used;
    uint64_t base_offset;
    uint64_t size_limit;
    int64_t async_res;
} io_file;

typedef struct {
    int used;
#ifdef _WIN32
    intptr_t handle;
    struct _finddata_t data;
    int first;
    int done;
#else
    DIR *dir;
#endif
} io_dir;

static io_file g_file[MAX_FILES];
static io_dir  g_dir[MAX_DIRS];
static char    g_root[512];
static char    g_iso_path[1024];
static uint64_t g_bytes_read;

void psp_io_set_root(const char *root) {
    snprintf(g_root, sizeof g_root, "%s", root ? root : ".");
}

void psp_io_set_iso(const char *iso_path) {
    if (iso_path) snprintf(g_iso_path, sizeof g_iso_path, "%s", iso_path);
}

void psp_io_reset(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (g_file[i].used && g_file[i].f) fclose(g_file[i].f);
        g_file[i].used = 0;
        g_file[i].f = NULL;
    }
    memset(g_dir, 0, sizeof g_dir);
    g_bytes_read = 0;
    if (!g_root[0]) psp_io_set_root(".");
}

void psp_io_init(void) { g_root[0] = '\0'; psp_io_reset(); }

uint64_t psp_io_bytes_read(void) { return g_bytes_read; }

/* Rewrite a PSP path into a host path. The device prefix becomes a
 * subdirectory so a disc image and a memory-stick image can coexist under one
 * root without colliding. */
static void map_path(const char *guest, char *out, size_t cap) {
    const char *p = guest;
    const char *sub = "disc";

    if      (!strncmp(p, "disc0:", 6)) { p += 6; sub = "disc"; }
    else if (!strncmp(p, "umd0:",  5)) { p += 5; sub = "disc"; }
    else if (!strncmp(p, "ms0:",   4)) { p += 4; sub = "ms";   }
    else if (!strncmp(p, "flash0:",7)) { p += 7; sub = "flash";}
    else if (!strncmp(p, "host0:", 6)) { p += 6; sub = "host"; }

    while (*p == '/' || *p == '\\') p++;
    snprintf(out, cap, "%s/%s/%s", g_root, sub, p);
}

static void hle_Open(void) {
    char guest[512], host[1024];
    psp_str(psp_arg(0), guest, sizeof guest);
    uint32_t flags = psp_arg(1);

    /* Check for UMD raw sector LBN access: sce_lbn0x<LBN>_size0x<SIZE> */
    const char *lbn_str = strstr(guest, "sce_lbn");
    if (lbn_str) {
        uint64_t lbn = 0, sz = 0;
        if (sscanf(lbn_str, "sce_lbn0x%llx_size0x%llx", &lbn, &sz) == 2 ||
            sscanf(lbn_str, "sce_lbn0x%llX_size0x%llX", &lbn, &sz) == 2 ||
            sscanf(lbn_str, "sce_lbn%llu_size%llu", &lbn, &sz) == 2) {
            const char *iso = g_iso_path[0] ? g_iso_path : "d:/project/tes/Shin Megami Tensei - Persona (USA).iso";
            FILE *f = fopen(iso, "rb");
            if (!f) f = fopen("Shin Megami Tensei - Persona (USA).iso", "rb");
            if (f) {
                uint64_t byte_off = lbn * 2048ull;
#ifdef _WIN32
                _fseeki64(f, (int64_t)byte_off, SEEK_SET);
#else
                fseeko(f, (off_t)byte_off, SEEK_SET);
#endif
                for (int i = 0; i < MAX_FILES; i++) {
                    if (g_file[i].used) continue;
                    g_file[i].f = f;
                    g_file[i].used = 1;
                    g_file[i].base_offset = byte_off;
                    g_file[i].size_limit = sz;
                    if (getenv("PSPRECOMP_TRACE_IO"))
                        fprintf(stderr, "io: opened raw UMD sector LBN 0x%llX (offset 0x%llX, size %llu) from '%s' -> fd %d\n",
                                (unsigned long long)lbn, (unsigned long long)byte_off, (unsigned long long)sz, iso, i + 3);
                    psp_ret((uint32_t)(i + 3));
                    return;
                }
                fclose(f);
                psp_ret(0x80010018);
                return;
            } else {
                if (getenv("PSPRECOMP_TRACE_IO"))
                    fprintf(stderr, "io: failed to open UMD ISO '%s' for raw sector read\n", iso);
            }
        }
    }

    map_path(guest, host, sizeof host);

    const char *mode = "rb";
    if (flags & PSP_O_TRUNC)                  mode = (flags & PSP_O_RDWR) == PSP_O_RDWR ? "w+b" : "wb";
    else if (flags & PSP_O_APPEND)            mode = "ab";
    else if ((flags & PSP_O_RDWR) == PSP_O_RDWR) mode = "r+b";
    else if (flags & PSP_O_WRONLY)            mode = (flags & PSP_O_CREAT) ? "wb" : "r+b";

    FILE *f = fopen(host, mode);
    if (!f && (flags & PSP_O_CREAT)) f = fopen(host, "w+b");
    /* PSPRECOMP_TRACE_IO=1 names every open and whether it worked. A game
     * that cannot find its assets usually does not crash; it waits. */
    if (getenv("PSPRECOMP_TRACE_IO"))
        fprintf(stderr, "io: open \"%s\" -> \"%s\" flags 0x%X: %s\n",
                guest, host, flags, f ? "ok" : "not found");
    if (!f) {
        /* A failed open is normal (a game probing for a save file) and is not
         * worth a warning, but the mapped path is worth knowing when a game
         * cannot find assets it expects. */
        psp_ret(0x80010002);          /* ENOENT */
        return;
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (g_file[i].used) continue;
        g_file[i].f = f;
        g_file[i].used = 1;
        g_file[i].base_offset = 0;
        g_file[i].size_limit = 0;
        psp_ret((uint32_t)(i + 3));   /* 0-2 are reserved for the std streams */
        return;
    }
    fclose(f);
    psp_ret(0x80010018);              /* too many open files */
}

static io_file *fd_arg(void) {
    int32_t fd = (int32_t)psp_arg(0) - 3;
    if (fd < 0 || fd >= MAX_FILES || !g_file[fd].used) return NULL;
    return &g_file[fd];
}

static void hle_Close(void) {
    io_file *h = fd_arg();
    if (!h) { psp_ret(0x80020323); return; }
    fclose(h->f);
    h->f = NULL;
    h->used = 0;
    h->base_offset = 0;
    h->size_limit = 0;
    psp_ret(0);
}

static void hle_Read(void) {
    io_file *h = fd_arg();
    uint32_t dst = psp_arg(1), size = psp_arg(2);
    if (!h) { psp_ret(0x80020323); return; }
    if (!size) { psp_ret(0); return; }

    /* Respect size_limit if set for raw sector files */
    if (h->size_limit > 0) {
#ifdef _WIN32
        int64_t cur = _ftelli64(h->f);
#else
        off_t cur = ftello(h->f);
#endif
        int64_t rel = cur - (int64_t)h->base_offset;
        if (rel >= (int64_t)h->size_limit) { psp_ret(0); return; }
        if (rel + size > h->size_limit) size = (uint32_t)(h->size_limit - rel);
    }

    /* Read through a host buffer and then place it, so a read that straddles
     * the end of a guest region is rejected by the memory layer rather than
     * writing past it. */
    uint8_t *tmp = (uint8_t *)malloc(size);
    if (!tmp) { psp_ret(0x80020190); return; }

    size_t got = fread(tmp, 1, size, h->f);
    if (got && psp_mem_write_block(dst, tmp, (uint32_t)got) != 0) {
        /* Fall back to byte-at-a-time so a partially mapped destination still
         * gets what fits, and the bad-access counter records the rest. */
        for (size_t i = 0; i < got; i++) psp_write8(dst + (uint32_t)i, tmp[i]);
    }
    free(tmp);
    g_bytes_read += got;
    if (h) h->async_res = (int64_t)got;
    if (getenv("PSPRECOMP_TRACE_IO")) {
        fprintf(stderr, "io: read fd %u size %u -> got %zu\n", psp_arg(0), size, got);
    }
    psp_ret((uint32_t)got);
}

static void hle_Write(void) {
    io_file *h = fd_arg();
    uint32_t src = psp_arg(1), size = psp_arg(2);

    /* fd 1 and 2 are stdout/stderr: a game writing there is talking to us. */
    int32_t fd = (int32_t)psp_arg(0);
    if (fd == 1 || fd == 2) {
        for (uint32_t i = 0; i < size; i++) fputc(psp_read8(src + i), stderr);
        psp_ret(size);
        return;
    }
    if (!h) { psp_ret(0x80020323); return; }

    uint8_t *tmp = (uint8_t *)malloc(size ? size : 1);
    if (!tmp) { psp_ret(0x80020190); return; }
    for (uint32_t i = 0; i < size; i++) tmp[i] = psp_read8(src + i);
    size_t put = fwrite(tmp, 1, size, h->f);
    free(tmp);
    if (h) h->async_res = (int64_t)put;
    psp_ret((uint32_t)put);
}

static void hle_Lseek(void) {
    io_file *h = fd_arg();
    uint64_t off = (uint64_t)psp_arg(2) | ((uint64_t)psp_arg(3) << 32);
    uint32_t whence = psp_arg(4);
    if (!h) { psp_ret(0x80020323); return; }

    int64_t target = 0;
    if (h->base_offset > 0) {
        if (whence == 0) { /* SEEK_SET */
            target = (int64_t)h->base_offset + (int64_t)off;
        } else if (whence == 1) { /* SEEK_CUR */
#ifdef _WIN32
            target = _ftelli64(h->f) + (int64_t)off;
#else
            target = ftello(h->f) + (int64_t)off;
#endif
        } else if (whence == 2) { /* SEEK_END */
            target = (int64_t)h->base_offset + (int64_t)h->size_limit + (int64_t)off;
        }
#ifdef _WIN32
        if (_fseeki64(h->f, target, SEEK_SET) != 0) { psp_ret(0xFFFFFFFFu); return; }
        int64_t rel = _ftelli64(h->f) - (int64_t)h->base_offset;
#else
        if (fseeko(h->f, (off_t)target, SEEK_SET) != 0) { psp_ret(0xFFFFFFFFu); return; }
        off_t rel = ftello(h->f) - (off_t)h->base_offset;
#endif
        if (h) h->async_res = rel;
        if (getenv("PSPRECOMP_TRACE_IO")) {
            fprintf(stderr, "io: lseek fd %u off %lld whence %u -> rel %lld\n", psp_arg(0), (long long)off, whence, (long long)rel);
        }
        psp_cpu.r[PSP_REG_V0] = (uint32_t)rel;
        psp_cpu.r[PSP_REG_V1] = (uint32_t)(rel >> 32);
        return;
    }

    int w = (whence == 1) ? SEEK_CUR : (whence == 2) ? SEEK_END : SEEK_SET;
    if (fseek(h->f, (long)(int64_t)off, w) != 0) { psp_ret(0xFFFFFFFFu); return; }

    long pos = ftell(h->f);
    if (h) h->async_res = (int64_t)pos;
    psp_cpu.r[PSP_REG_V0] = (uint32_t)pos;
    psp_cpu.r[PSP_REG_V1] = 0;
}

static void hle_Rename(void) {
    char a[512], b[512], ha[1024], hb[1024];
    psp_str(psp_arg(0), a, sizeof a);
    psp_str(psp_arg(1), b, sizeof b);
    map_path(a, ha, sizeof ha);
    map_path(b, hb, sizeof hb);
    psp_ret(rename(ha, hb) == 0 ? 0 : 0x80010002);
}

/* ---- directories --------------------------------------------------------- */

static void hle_Dopen(void) {
    char guest[512], host[1024];
    psp_str(psp_arg(0), guest, sizeof guest);
    map_path(guest, host, sizeof host);

    for (int i = 0; i < MAX_DIRS; i++) {
        if (g_dir[i].used) continue;
#ifdef _WIN32
        char pattern[1088];
        snprintf(pattern, sizeof pattern, "%s/*", host);
        g_dir[i].handle = _findfirst(pattern, &g_dir[i].data);
        if (g_dir[i].handle == -1) { psp_ret(0x80010002); return; }
        g_dir[i].first = 1;
        g_dir[i].done = 0;
#else
        g_dir[i].dir = opendir(host);
        if (!g_dir[i].dir) { psp_ret(0x80010002); return; }
#endif
        g_dir[i].used = 1;
        psp_ret((uint32_t)(i + 1));
        return;
    }
    psp_ret(0x80010018);
}

/* Fill a SceIoDirent. Only the name is populated: games use Dread to enumerate
 * save slots and asset directories by name, and a wrong stat block would be
 * worse than an empty one. */
static void hle_Dread(void) {
    int32_t id = (int32_t)psp_arg(0) - 1;
    uint32_t dirent = psp_arg(1);
    if (id < 0 || id >= MAX_DIRS || !g_dir[id].used) { psp_ret(0x80020323); return; }

    const char *name = NULL;
#ifdef _WIN32
    if (g_dir[id].done) { psp_ret(0); return; }
    if (g_dir[id].first) {
        g_dir[id].first = 0;
        name = g_dir[id].data.name;
    } else if (_findnext(g_dir[id].handle, &g_dir[id].data) == 0) {
        name = g_dir[id].data.name;
    } else {
        g_dir[id].done = 1;
        psp_ret(0);
        return;
    }
#else
    struct dirent *de = readdir(g_dir[id].dir);
    if (!de) { psp_ret(0); return; }
    name = de->d_name;
#endif

    /* SceIoDirent: a 52-byte SceIoStat, then char d_name[256]. */
    for (int i = 0; i < 52; i++) psp_write8(dirent + (uint32_t)i, 0);
    uint32_t at = dirent + 52;
    for (uint32_t i = 0; i < 255 && name[i]; i++) psp_write8(at + i, (uint8_t)name[i]);
    psp_write8(at + (uint32_t)strlen(name), 0);

    psp_ret(1);                       /* more entries may follow */
}

static void hle_Dclose(void) {
    int32_t id = (int32_t)psp_arg(0) - 1;
    if (id < 0 || id >= MAX_DIRS || !g_dir[id].used) { psp_ret(0x80020323); return; }
#ifdef _WIN32
    if (g_dir[id].handle != -1) _findclose(g_dir[id].handle);
#else
    if (g_dir[id].dir) closedir(g_dir[id].dir);
#endif
    g_dir[id].used = 0;
    psp_ret(0);
}

static void hle_ReadAsync(void) {
    hle_Read();
    psp_ret(0);
}

static void hle_WriteAsync(void) {
    hle_Write();
    psp_ret(0);
}

static void hle_LseekAsync(void) {
    hle_Lseek();
    psp_ret(0);
}

static void hle_WaitAsync(void) {
    io_file *h = fd_arg();
    uint32_t res = psp_arg(1);
    int64_t r = h ? h->async_res : 0;
    if (res) {
        psp_write32(res + 0, (uint32_t)(r & 0xFFFFFFFFu));
        psp_write32(res + 4, (uint32_t)((uint64_t)r >> 32));
    }
    if (getenv("PSPRECOMP_TRACE_IO")) {
        fprintf(stderr, "io: wait_async fd %u -> res=%lld\n", psp_arg(0), (long long)r);
    }
    psp_ret(0);
}

static void hle_PollAsync(void) {
    io_file *h = fd_arg();
    uint32_t res = psp_arg(1);
    int64_t r = h ? h->async_res : 0;
    if (res) {
        psp_write32(res + 0, (uint32_t)(r & 0xFFFFFFFFu));
        psp_write32(res + 4, (uint32_t)((uint64_t)r >> 32));
    }
    if (getenv("PSPRECOMP_TRACE_IO")) {
        fprintf(stderr, "io: poll_async fd %u -> res=%lld\n", psp_arg(0), (long long)r);
    }
    psp_ret(0);
}

void psp_io_register(void) {
    psp_hle_register(0x109F50BC, "IoFileMgrForUser", "sceIoOpen",        hle_Open);
    psp_hle_register(0x89AA9906, "IoFileMgrForUser", "sceIoOpenAsync",   hle_Open);
    psp_hle_register(0x810C4BC3, "IoFileMgrForUser", "sceIoClose",       hle_Close);
    psp_hle_register(0xFF5940B6, "IoFileMgrForUser", "sceIoCloseAsync",  hle_Close);
    psp_hle_register(0x6A638D83, "IoFileMgrForUser", "sceIoRead",        hle_Read);
    psp_hle_register(0xA0B5A7C2, "IoFileMgrForUser", "sceIoReadAsync",   hle_ReadAsync);
    psp_hle_register(0x42EC03AC, "IoFileMgrForUser", "sceIoWrite",       hle_Write);
    psp_hle_register(0x27EB27B8, "IoFileMgrForUser", "sceIoLseek",       hle_Lseek);
    psp_hle_register(0x71B19E77, "IoFileMgrForUser", "sceIoLseekAsync",  hle_LseekAsync);
    psp_hle_register(0x35DBD746, "IoFileMgrForUser", "sceIoWaitAsyncCB", hle_WaitAsync);
    psp_hle_register(0xE23EEC33, "IoFileMgrForUser", "sceIoWaitAsync",   hle_WaitAsync);
    psp_hle_register(0x3251EA56, "IoFileMgrForUser", "sceIoPollAsync",   hle_PollAsync);
    psp_hle_register(0x779103A0, "IoFileMgrForUser", "sceIoRename",      hle_Rename);
    psp_hle_register(0xB29DDF9C, "IoFileMgrForUser", "sceIoDopen",       hle_Dopen);
    psp_hle_register(0xE3EB004C, "IoFileMgrForUser", "sceIoDread",       hle_Dread);
    psp_hle_register(0xEB092469, "IoFileMgrForUser", "sceIoDclose",      hle_Dclose);
}
