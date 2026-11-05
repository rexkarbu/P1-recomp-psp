#ifndef ISO_EXTRACTOR_H
#define ISO_EXTRACTOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*iso_progress_cb)(float progress, const char *message);

typedef struct {
    char disc_id[16];
    char title[128];
    uint64_t size_bytes;
    char sha1_hex[41];
    char md5_hex[33];
    bool is_persona_usa;
    bool is_redump_verified;
    bool is_wrong_region;
    char status_message[256];
} IsoVerificationResult;

/* Official Redump North America Checksums for Persona 1 (PSP) */
#define REDUMP_PERSONA1_USA_SHA1 "ec190259efdac60801e25c9f7d1ce51cda8f94aa"
#define REDUMP_PERSONA1_USA_MD5  "c75f75b0f1e92d7cca77d75585bebd82"
#define REDUMP_PERSONA1_USA_SIZE 820740096ull

/* Mengecek apakah file ada di disk */
bool file_exists(const char *path);

/* Mengecek apakah aset game dan binary sudah terpasang lengkap */
bool iso_is_game_installed(void);

/* Quick verification (Header & PARAM.SFO only) */
bool iso_verify(const char *iso_path, char *out_disc_id, char *out_title, uint64_t *out_size);

/* Full verification (Header + PARAM.SFO + SHA-1 + MD5 Checksum) */
bool iso_verify_full(const char *iso_path, IsoVerificationResult *out_res, iso_progress_cb cb);

/* Menghitung hash SHA-1 dan MD5 dari file ISO */
bool iso_compute_hashes(const char *iso_path, char *out_sha1, char *out_md5, iso_progress_cb cb);

/* Melakukan proses instalasi lengkap: ekstraksi ISO, dekripsi EBOOT, dan relokasi PRX */
bool iso_run_installation(const char *iso_path, iso_progress_cb progress_cb);

/* Relokasi PRX internal */
int prx_relocate_file(const char *in_path, const char *out_path);

#ifdef __cplusplus
}
#endif

#endif /* ISO_EXTRACTOR_H */
