/*
 * ISO Extractor & BYOG Pipeline for Shin Megami Tensei: Persona PC Recomp
 * Handles native ISO9660 parsing, PARAM.SFO verification, asset extraction,
 * EBOOT decryption via pspdecrypt, and PRX relocation.
 */

#include "iso_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wincrypt.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t  type;
    char     id[5];
    uint8_t  version;
    uint8_t  unused1;
    char     system_id[32];
    char     volume_id[32];
    uint8_t  unused2[8];
    uint32_t volume_space_size_le;
    uint32_t volume_space_size_be;
    uint8_t  unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_seq_number_le;
    uint16_t volume_seq_number_be;
    uint16_t logical_block_size_le;
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t type_l_path_table;
    uint32_t opt_type_l_path_table;
    uint32_t type_m_path_table;
    uint32_t opt_type_m_path_table;
    uint8_t  root_directory_record[34];
    char     volume_set_id[128];
    char     publisher_id[128];
    char     data_preparer_id[128];
    char     application_id[128];
    char     copyright_file_id[37];
    char     abstract_file_id[37];
    char     bibliographic_file_id[37];
} ISO_PVD;

typedef struct {
    uint8_t  length;
    uint8_t  ext_attr_length;
    uint32_t extent_le;
    uint32_t extent_be;
    uint32_t size_le;
    uint32_t size_be;
    uint8_t  date[7];
    uint8_t  flags;
    uint8_t  file_unit_size;
    uint8_t  interleave_gap_size;
    uint16_t volume_seq_le;
    uint16_t volume_seq_be;
    uint8_t  name_len;
} ISO_DirRecord;
#pragma pack(pop)

/* Check if directories and files exist */
bool file_exists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static void ensure_dir(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
            if (tmp[0] && !(tmp[1] == ':' && tmp[2] == '\0')) {
                CreateDirectoryA(tmp, NULL);
            }
            *p = c;
        }
    }
    CreateDirectoryA(tmp, NULL);
}

bool iso_is_game_installed(void) {
    /* Required core files for Shin Megami Tensei: Persona */
    bool has_datapack = file_exists("game_root/disc/PSP_GAME/USRDIR/datapack.bin");
    bool has_eboot    = file_exists("extracted/EBOOT_RELOC.ELF");
    bool has_exe      = file_exists("persona_pc.exe") || file_exists("build_pc/persona_pc.exe");
    return has_datapack && has_eboot && has_exe;
}

bool iso_verify(const char *iso_path, char *out_disc_id, char *out_title, uint64_t *out_size) {
    if (!iso_path || !iso_path[0]) return false;

    FILE *f = fopen(iso_path, "rb");
    if (!f) return false;

    if (out_size) {
        _fseeki64(f, 0, SEEK_END);
        *out_size = (uint64_t)_ftelli64(f);
        _fseeki64(f, 0, SEEK_SET);
    }

    /* Read PVD at Sector 16 (0x8000) */
    fseek(f, 16 * 2048, SEEK_SET);
    ISO_PVD pvd;
    if (fread(&pvd, 1, sizeof(pvd), f) != sizeof(pvd)) {
        fclose(f);
        return false;
    }

    if (memcmp(pvd.id, "CD001", 5) != 0) {
        fclose(f);
        return false;
    }

    ISO_DirRecord *root = (ISO_DirRecord *)pvd.root_directory_record;
    if (root->extent_le == 0 || root->size_le == 0) {
        fclose(f);
        return false;
    }

    uint8_t *root_buf = (uint8_t *)malloc(root->size_le);
    if (!root_buf) { fclose(f); return false; }
    fseek(f, (long)root->extent_le * 2048, SEEK_SET);
    fread(root_buf, 1, root->size_le, f);

    uint32_t psp_game_lba = 0, psp_game_size = 0;
    uint32_t off = 0;
    while (off < root->size_le) {
        ISO_DirRecord *rec = (ISO_DirRecord *)(root_buf + off);
        if (rec->length == 0) { off = (off + 2047) & ~2047; continue; }
        if (rec->name_len >= 8 && memcmp(root_buf + off + sizeof(ISO_DirRecord), "PSP_GAME", 8) == 0) {
            psp_game_lba = rec->extent_le;
            psp_game_size = rec->size_le;
            break;
        }
        off += rec->length;
    }
    free(root_buf);

    if (!psp_game_lba || !psp_game_size) {
        fclose(f);
        return false;
    }

    /* Read PSP_GAME directory */
    uint8_t *psp_buf = (uint8_t *)malloc(psp_game_size);
    if (!psp_buf) { fclose(f); return false; }
    fseek(f, (long)psp_game_lba * 2048, SEEK_SET);
    fread(psp_buf, 1, psp_game_size, f);

    uint32_t sfo_lba = 0, sfo_size = 0;
    off = 0;
    while (off < psp_game_size) {
        ISO_DirRecord *rec = (ISO_DirRecord *)(psp_buf + off);
        if (rec->length == 0) { off = (off + 2047) & ~2047; continue; }
        if (rec->name_len >= 9 && memcmp(psp_buf + off + sizeof(ISO_DirRecord), "PARAM.SFO", 9) == 0) {
            sfo_lba = rec->extent_le;
            sfo_size = rec->size_le;
            break;
        }
        off += rec->length;
    }
    free(psp_buf);

    if (!sfo_lba || !sfo_size) {
        fclose(f);
        return false;
    }

    /* Read PARAM.SFO to extract DISC ID & Title */
    uint8_t *sfo = (uint8_t *)malloc(sfo_size);
    if (!sfo) { fclose(f); return false; }
    fseek(f, (long)sfo_lba * 2048, SEEK_SET);
    fread(sfo, 1, sfo_size, f);
    fclose(f);

    bool is_persona_usa = false;
    if (out_disc_id) strcpy(out_disc_id, "Unknown");
    if (out_title) strcpy(out_title, "Shin Megami Tensei: Persona");

    for (uint32_t i = 0; i < sfo_size - 8; i++) {
        if (memcmp(sfo + i, "UL", 2) == 0 && (i + 9 <= sfo_size)) {
            char disc_id[10] = {0};
            memcpy(disc_id, sfo + i, 9);
            if (out_disc_id) strcpy(out_disc_id, disc_id);
            if (strcmp(disc_id, "ULUS10432") == 0) {
                is_persona_usa = true;
            }
        }
        if (memcmp(sfo + i, "Shin Megami Tensei", 18) == 0) {
            char tit[64] = {0};
            size_t len = 0;
            while (i + len < sfo_size && sfo[i + len] >= 32 && sfo[i + len] <= 126 && len < 63) {
                tit[len] = (char)sfo[i + len];
                len++;
            }
            if (out_title && tit[0]) strcpy(out_title, tit);
        }
    }
    free(sfo);

    return is_persona_usa;
}

bool iso_compute_hashes(const char *iso_path, char *out_sha1, char *out_md5, iso_progress_cb cb) {
    if (!iso_path || !iso_path[0]) return false;

    FILE *f = fopen(iso_path, "rb");
    if (!f) return false;

    _fseeki64(f, 0, SEEK_END);
    uint64_t total_size = (uint64_t)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHashSha1 = 0;
    HCRYPTHASH hHashMd5 = 0;

    if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        fclose(f);
        return false;
    }

    if (out_sha1) CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHashSha1);
    if (out_md5)  CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHashMd5);

    uint8_t buffer[1048576]; /* 1 MB fast streaming buffer */
    uint64_t bytes_processed = 0;

    while (bytes_processed < total_size) {
        size_t to_read = (size_t)((total_size - bytes_processed > sizeof(buffer)) ? sizeof(buffer) : (total_size - bytes_processed));
        size_t r = fread(buffer, 1, to_read, f);
        if (r == 0) break;

        if (hHashSha1) CryptHashData(hHashSha1, buffer, (DWORD)r, 0);
        if (hHashMd5)  CryptHashData(hHashMd5, buffer, (DWORD)r, 0);

        bytes_processed += r;

        if (cb && total_size > 0) {
            float pct = (float)bytes_processed / (float)total_size * 100.0f;
            char msg[128];
            snprintf(msg, sizeof(msg), "Verifying checksums: %.1f %% (%.1f / %.1f MB)",
                     pct, (double)bytes_processed / (1024.0 * 1024.0), (double)total_size / (1024.0 * 1024.0));
            cb(pct, msg);
        }
    }
    fclose(f);

    if (hHashSha1 && out_sha1) {
        BYTE hash_bytes[20];
        DWORD hash_len = 20;
        if (CryptGetHashParam(hHashSha1, HP_HASHVAL, hash_bytes, &hash_len, 0)) {
            for (int i = 0; i < 20; i++) {
                sprintf(out_sha1 + (i * 2), "%02x", hash_bytes[i]);
            }
            out_sha1[40] = '\0';
        }
        CryptDestroyHash(hHashSha1);
    }

    if (hHashMd5 && out_md5) {
        BYTE hash_bytes[16];
        DWORD hash_len = 16;
        if (CryptGetHashParam(hHashMd5, HP_HASHVAL, hash_bytes, &hash_len, 0)) {
            for (int i = 0; i < 16; i++) {
                sprintf(out_md5 + (i * 2), "%02x", hash_bytes[i]);
            }
            out_md5[32] = '\0';
        }
        CryptDestroyHash(hHashMd5);
    }

    CryptReleaseContext(hProv, 0);
    return true;
}

bool iso_verify_full(const char *iso_path, IsoVerificationResult *out_res, iso_progress_cb cb) {
    if (!out_res) return false;
    memset(out_res, 0, sizeof(*out_res));

    if (cb) cb(5.0f, "Inspecting ISO header and PARAM.SFO...");

    bool quick_ok = iso_verify(iso_path, out_res->disc_id, out_res->title, &out_res->size_bytes);

    /* Check for wrong regions (e.g., Japanese release ULJM-05461) */
    if (strncmp(out_res->disc_id, "ULJM", 4) == 0) {
        out_res->is_wrong_region = true;
        out_res->is_persona_usa = false;
        snprintf(out_res->status_message, sizeof(out_res->status_message),
                 "Japanese release detected (%s). Please provide the official USA release (ULUS-10432).",
                 out_res->disc_id);
        return false;
    }

    if (!quick_ok) {
        snprintf(out_res->status_message, sizeof(out_res->status_message),
                 "Invalid ISO or unknown title (Disc ID: %s).",
                 out_res->disc_id[0] ? out_res->disc_id : "Unknown");
        return false;
    }

    /* Compute Cryptographic Checksums (SHA-1 & MD5) */
    if (cb) cb(10.0f, "Computing SHA-1 & MD5 checksums for integrity check...");
    iso_compute_hashes(iso_path, out_res->sha1_hex, out_res->md5_hex, cb);

    out_res->is_persona_usa = (strcmp(out_res->disc_id, "ULUS10432") == 0);

    /* Check Redump 1:1 match */
    if (strcmp(out_res->sha1_hex, REDUMP_PERSONA1_USA_SHA1) == 0 ||
        strcmp(out_res->md5_hex, REDUMP_PERSONA1_USA_MD5) == 0) {
        out_res->is_redump_verified = true;
        snprintf(out_res->status_message, sizeof(out_res->status_message),
                 "Persona 1 (USA) Detected - Ready to Recompile (Official Redump 1:1 Match)");
    } else if (out_res->is_persona_usa) {
        out_res->is_redump_verified = false;
        snprintf(out_res->status_message, sizeof(out_res->status_message),
                 "Persona 1 (USA) Detected - Ready to Recompile (Custom/Trimmed ISO)");
    } else {
        snprintf(out_res->status_message, sizeof(out_res->status_message),
                 "Unknown Disc ID: %s", out_res->disc_id);
    }

    return out_res->is_persona_usa;
}

/* Recursive ISO directory extraction */
static void extract_iso_dir(FILE *f, uint32_t dir_lba, uint32_t dir_size, const char *dest_dir,
                            uint64_t *extracted_bytes, uint64_t total_est, iso_progress_cb cb) {
    if (!dir_lba || !dir_size) return;
    ensure_dir(dest_dir);

    uint8_t *buf = (uint8_t *)malloc(dir_size);
    if (!buf) return;

    _fseeki64(f, (int64_t)dir_lba * 2048ll, SEEK_SET);
    fread(buf, 1, dir_size, f);

    uint32_t off = 0;
    while (off < dir_size) {
        ISO_DirRecord *rec = (ISO_DirRecord *)(buf + off);
        if (rec->length == 0) {
            off = (off + 2047) & ~2047;
            continue;
        }

        uint8_t *raw_name = buf + off + sizeof(ISO_DirRecord);
        if (rec->name_len == 1 && (raw_name[0] == 0 || raw_name[0] == 1)) {
            off += rec->length;
            continue; /* Skip '.' and '..' */
        }

        char name[128] = {0};
        size_t nlen = rec->name_len;
        if (nlen > 127) nlen = 127;
        memcpy(name, raw_name, nlen);

        /* Strip ISO9660 version suffix (e.g. ';1') */
        char *semi = strchr(name, ';');
        if (semi) *semi = '\0';

        char sub_dest[MAX_PATH];
        snprintf(sub_dest, sizeof(sub_dest), "%s/%s", dest_dir, name);

        if (rec->flags & 0x02) {
            /* Subdirectory */
            extract_iso_dir(f, rec->extent_le, rec->size_le, sub_dest, extracted_bytes, total_est, cb);
        } else {
            /* Regular File */
            FILE *out_f = fopen(sub_dest, "wb");
            if (out_f) {
                _fseeki64(f, (int64_t)rec->extent_le * 2048ll, SEEK_SET);
                uint32_t remain = rec->size_le;
                uint8_t chunk[65536];
                while (remain > 0) {
                    uint32_t take = (remain > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : remain;
                    size_t r = fread(chunk, 1, take, f);
                    if (r == 0) break;
                    fwrite(chunk, 1, r, out_f);
                    remain -= (uint32_t)r;
                    *extracted_bytes += r;

                    if (cb && total_est > 0) {
                        float pct = (float)(*extracted_bytes) / (float)total_est * 65.0f;
                        if (pct > 65.0f) pct = 65.0f;
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Extracting: %s (%.1f MB)", name, (float)(*extracted_bytes) / (1024.0f * 1024.0f));
                        cb(pct, msg);
                    }
                }
                fclose(out_f);
            }
        }

        off += rec->length;
    }

    free(buf);
}

/* Internal PRX Relocator */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

#define SHT_REL         9
#define SHT_PRXRELOC    0x700000A0u
#define R_MIPS_NONE     0
#define R_MIPS_16       1
#define R_MIPS_32       2
#define R_MIPS_REL32    3
#define R_MIPS_26       4
#define R_MIPS_HI16     5
#define R_MIPS_LO16     6

#define ELF32_R_TYPE(i)      ((i) & 0xFF)
#define ELF32_R_OFS_BASE(i)  (((i) >> 8) & 0xFF)
#define ELF32_R_ADDR_BASE(i) (((i) >> 16) & 0xFF)

typedef struct {
    uint32_t r_offset;
    uint32_t ofs_base;
    uint32_t addr_base;
} Hi16Entry;

int prx_relocate_file(const char *in_path, const char *out_path) {
    FILE *f = fopen(in_path, "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    uint8_t *data = (uint8_t *)malloc(fsize);
    if (!data || fread(data, 1, fsize, f) != (size_t)fsize) {
        fclose(f);
        if (data) free(data);
        return 1;
    }
    fclose(f);

    if (memcmp(data, "\x7F" "ELF", 4) != 0) {
        free(data);
        return 2;
    }

    uint32_t phoff = rd32(data + 28);
    uint16_t phentsize = rd16(data + 42);
    uint16_t phnum = rd16(data + 44);

    uint32_t shoff = rd32(data + 32);
    uint16_t shentsize = rd16(data + 46);
    uint16_t shnum = rd16(data + 48);

    uint32_t seg_base[8] = {0};
    uint32_t seg_offset[8] = {0};
    int nseg = 0;
    for (int i = 0; i < phnum && nseg < 8; i++) {
        uint8_t *ph = data + phoff + (i * phentsize);
        if (rd32(ph + 0) == 1) { /* PT_LOAD */
            seg_offset[nseg] = rd32(ph + 4);
            seg_base[nseg]   = rd32(ph + 8);
            nseg++;
        }
    }

    Hi16Entry hi_queue[1024];
    int hi_count = 0;

    for (int s = 0; s < shnum; s++) {
        uint8_t *sh = data + shoff + (s * shentsize);
        uint32_t sh_type = rd32(sh + 4);
        if (sh_type != SHT_REL && sh_type != SHT_PRXRELOC) continue;

        uint32_t rel_off = rd32(sh + 16);
        uint32_t rel_size = rd32(sh + 20);
        int num_entries = rel_size / 8;

        for (int i = 0; i < num_entries; i++) {
            uint8_t *rel = data + rel_off + (i * 8);
            uint32_t r_offset = rd32(rel + 0);
            uint32_t r_info = rd32(rel + 4);

            int type = ELF32_R_TYPE(r_info);
            int ofs_base = ELF32_R_OFS_BASE(r_info);
            int addr_base = ELF32_R_ADDR_BASE(r_info);
            uint32_t delta = seg_base[addr_base];

            if (type == R_MIPS_HI16) {
                if (hi_count < 1024) {
                    hi_queue[hi_count].r_offset = r_offset;
                    hi_queue[hi_count].ofs_base = ofs_base;
                    hi_queue[hi_count].addr_base = addr_base;
                    hi_count++;
                }
                continue;
            }

            if (type == R_MIPS_LO16) {
                uint32_t lo_target = r_offset + seg_offset[ofs_base];
                uint32_t lo_inst = rd32(data + lo_target);
                int16_t lo_imm = (int16_t)(lo_inst & 0xFFFF);

                if (hi_count > 0) {
                    for (int h = 0; h < hi_count; h++) {
                        uint32_t hi_target = hi_queue[h].r_offset + seg_offset[hi_queue[h].ofs_base];
                        uint32_t hi_inst = rd32(data + hi_target);
                        uint16_t hi_imm = (uint16_t)(hi_inst & 0xFFFF);
                        uint32_t h_delta = seg_base[hi_queue[h].addr_base];

                        if (h_delta != 0) {
                            uint32_t full_addr = ((uint32_t)hi_imm << 16) + (int32_t)lo_imm + h_delta;
                            uint16_t new_hi = (uint16_t)((full_addr >> 16) + ((full_addr & 0x8000) ? 1 : 0));
                            hi_inst = (hi_inst & 0xFFFF0000u) | new_hi;
                            wr32(data + hi_target, hi_inst);
                        }
                    }
                    hi_count = 0;
                }

                if (delta != 0) {
                    uint32_t new_lo = (uint32_t)(lo_imm + delta) & 0xFFFFu;
                    lo_inst = (lo_inst & 0xFFFF0000u) | new_lo;
                    wr32(data + lo_target, lo_inst);
                }
                continue;
            }

            if (delta == 0) continue;

            uint32_t target = r_offset + seg_offset[ofs_base];
            uint32_t inst = rd32(data + target);

            if (type == R_MIPS_32) {
                wr32(data + target, inst + delta);
            } else if (type == R_MIPS_26) {
                uint32_t jump_target = (inst & 0x03FFFFFFu) << 2;
                jump_target += delta;
                inst = (inst & 0xFC000000u) | ((jump_target >> 2) & 0x03FFFFFFu);
                wr32(data + target, inst);
            }
        }
    }

    FILE *out_f = fopen(out_path, "wb");
    if (!out_f) { free(data); return 3; }
    fwrite(data, 1, fsize, out_f);
    fclose(out_f);
    free(data);
    return 0;
}

bool iso_run_installation(const char *iso_path, iso_progress_cb cb) {
    if (cb) cb(5.0f, "Verifying ISO integrity and disc identifier...");

    char disc_id[16] = {0};
    char title[128] = {0};
    uint64_t iso_size = 0;
    if (!iso_verify(iso_path, disc_id, title, &iso_size)) {
        if (cb) cb(0.0f, "Verification failed! Not a valid Persona 1 USA ISO.");
        return false;
    }

    if (cb) cb(10.0f, "Preparing destination folders...");
    ensure_dir("game_root/disc/PSP_GAME");
    ensure_dir("extracted");
    ensure_dir("savedata");
    ensure_dir("screenshots");
    ensure_dir("states");

    FILE *f = fopen(iso_path, "rb");
    if (!f) {
        if (cb) cb(0.0f, "Failed to open ISO file for extraction.");
        return false;
    }

    /* Locate PSP_GAME on ISO */
    fseek(f, 16 * 2048, SEEK_SET);
    ISO_PVD pvd;
    fread(&pvd, 1, sizeof(pvd), f);
    ISO_DirRecord *root = (ISO_DirRecord *)pvd.root_directory_record;

    uint8_t *root_buf = (uint8_t *)malloc(root->size_le);
    fseek(f, (long)root->extent_le * 2048, SEEK_SET);
    fread(root_buf, 1, root->size_le, f);

    uint32_t psp_game_lba = 0, psp_game_size = 0;
    uint32_t off = 0;
    while (off < root->size_le) {
        ISO_DirRecord *rec = (ISO_DirRecord *)(root_buf + off);
        if (rec->length == 0) { off = (off + 2047) & ~2047; continue; }
        if (rec->name_len >= 8 && memcmp(root_buf + off + sizeof(ISO_DirRecord), "PSP_GAME", 8) == 0) {
            psp_game_lba = rec->extent_le;
            psp_game_size = rec->size_le;
            break;
        }
        off += rec->length;
    }
    free(root_buf);

    if (!psp_game_lba) {
        fclose(f);
        if (cb) cb(0.0f, "Error: PSP_GAME directory missing in ISO.");
        return false;
    }

    /* Extract entire PSP_GAME directory tree to game_root/disc/PSP_GAME */
    if (cb) cb(15.0f, "Extracting game assets to game_root/disc/PSP_GAME...");
    uint64_t extracted_bytes = 0;
    extract_iso_dir(f, psp_game_lba, psp_game_size, "game_root/disc/PSP_GAME", &extracted_bytes, iso_size, cb);
    fclose(f);

    /* Also copy EBOOT.BIN to extracted/EBOOT.BIN for decryption */
    if (cb) cb(68.0f, "Copying EBOOT.BIN for Kirk decryption...");
    CopyFileA("game_root/disc/PSP_GAME/SYSDIR/EBOOT.BIN", "extracted/EBOOT.BIN", FALSE);

    /* Find pspdecrypt.exe binary */
    const char *pspdec = NULL;
    if (file_exists("tools/pspdecrypt-bin/pspdecrypt.exe")) pspdec = "tools\\pspdecrypt-bin\\pspdecrypt.exe";
    else if (file_exists("tools/pspdecrypt.exe"))          pspdec = "tools\\pspdecrypt.exe";
    else if (file_exists("pspdecrypt.exe"))                pspdec = "pspdecrypt.exe";
    else                                                   pspdec = "tools\\pspdecrypt-bin\\pspdecrypt.exe";

    /* Decrypt EBOOT.BIN using pspdecrypt engine */
    if (cb) cb(75.0f, "Decrypting PSP binary with Kirk crypto engine...");
    char cmd_buf[512];
    snprintf(cmd_buf, sizeof(cmd_buf), "%s -o extracted\\EBOOT.ELF extracted\\EBOOT.BIN", pspdec);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (CreateProcessA(NULL, cmd_buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 15000); /* wait up to 15s */
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (!file_exists("extracted/EBOOT.ELF")) {
        if (cb) cb(0.0f, "Decryption failed! extracted/EBOOT.ELF was not created.");
        return false;
    }

    /* Relocate PRX segments to create EBOOT_RELOC.ELF */
    if (cb) cb(88.0f, "Relocating PRX segments to generate EBOOT_RELOC.ELF...");
    int reloc_res = prx_relocate_file("extracted/EBOOT.ELF", "extracted/EBOOT_RELOC.ELF");
    if (reloc_res != 0) {
        if (cb) cb(0.0f, "PRX Relocation failed!");
        return false;
    }

    /* Step 4: Write installation manifest to persist state */
    if (cb) cb(96.0f, "Saving installation manifest and linking game assets...");
    FILE *f_manifest = fopen("install_manifest.txt", "w");
    if (f_manifest) {
        fprintf(f_manifest, "[INSTALL_MANIFEST]\n");
        fprintf(f_manifest, "game_title=Shin Megami Tensei: Persona (USA)\n");
        fprintf(f_manifest, "disc_id=%s\n", disc_id);
        fprintf(f_manifest, "iso_source=%s\n", iso_path);
        fprintf(f_manifest, "status=INSTALLED_READY\n");
        fclose(f_manifest);
    }

    if (cb) cb(100.0f, "Installation Complete! Persona 1 is ready to play!");
    return true;
}
