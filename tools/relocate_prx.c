/*
 * PSP PRX Relocator for Static Recompilation
 * Relocates a PSP PRX (EBOOT.ELF) by applying all SHT_PRXRELOC sections
 * with seg_base[0] = 0x00000000 and seg_base[1] = 0x002634C0.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define R_MIPS_NONE     0
#define R_MIPS_16       1
#define R_MIPS_32       2
#define R_MIPS_REL32    3
#define R_MIPS_26       4
#define R_MIPS_HI16     5
#define R_MIPS_LO16     6

#define SHT_REL         9
#define SHT_PRXRELOC    0x700000A0u

#define ELF32_R_TYPE(i)      ((i) & 0xFF)
#define ELF32_R_OFS_BASE(i)  (((i) >> 8) & 0xFF)
#define ELF32_R_ADDR_BASE(i) (((i) >> 16) & 0xFF)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

typedef struct {
    uint32_t r_offset;
    uint32_t ofs_base;
    uint32_t addr_base;
} hi16_entry;

int main(int argc, char **argv) {
    const char *in_path = "d:/project/tes/extracted/EBOOT.ELF";
    const char *out_path = "d:/project/tes/extracted/EBOOT_RELOC.ELF";
    if (argc > 1) in_path = argv[1];
    if (argc > 2) out_path = argv[2];

    printf("[RELOC] Reading input ELF: %s\n", in_path);
    FILE *f = fopen(in_path, "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", in_path); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    uint8_t *data = (uint8_t *)malloc(fsize);
    if (!data || fread(data, 1, fsize, f) != (size_t)fsize) {
        fprintf(stderr, "Cannot read file\n");
        return 1;
    }
    fclose(f);

    if (memcmp(data, "\x7F" "ELF", 4) != 0) {
        fprintf(stderr, "Not an ELF file\n");
        return 1;
    }

    uint32_t phoff = rd32(data + 28);
    uint16_t phentsize = rd16(data + 42);
    uint16_t phnum = rd16(data + 44);

    uint32_t shoff = rd32(data + 32);
    uint16_t shentsize = rd16(data + 46);
    uint16_t shnum = rd16(data + 48);

    /* Collect segment bases */
    uint32_t seg_base[8] = {0};
    uint32_t seg_offset[8] = {0};
    int nseg = 0;
    for (int i = 0; i < phnum && nseg < 8; i++) {
        uint8_t *ph = data + phoff + (i * phentsize);
        if (rd32(ph + 0) == 1) { /* PT_LOAD */
            seg_offset[nseg] = rd32(ph + 4);
            seg_base[nseg] = rd32(ph + 8);
            printf("[RELOC] Segment %d: FileOff=0x%08X VAddr=0x%08X\n", nseg, seg_offset[nseg], seg_base[nseg]);
            nseg++;
        }
    }

    /* We relocate Seg 0 with 0, and Seg 1 with seg_base[1] (0x002634C0) */
    printf("[RELOC] seg_base[0] = 0x%08X, seg_base[1] = 0x%08X\n", seg_base[0], seg_base[1]);

    hi16_entry hi_queue[1024];
    int hi_count = 0;

    int total_relocs = 0;
    int patched_32 = 0;
    int patched_26 = 0;
    int patched_hi_lo = 0;
    int patched_orphan_lo = 0;

    for (int s = 0; s < shnum; s++) {
        uint8_t *sh = data + shoff + (s * shentsize);
        uint32_t sh_type = rd32(sh + 4);
        if (sh_type != SHT_REL && sh_type != SHT_PRXRELOC) continue;

        uint32_t rel_off = rd32(sh + 16);
        uint32_t rel_size = rd32(sh + 20);
        int num_entries = rel_size / 8;
        total_relocs += num_entries;

        for (int i = 0; i < num_entries; i++) {
            uint8_t *rel = data + rel_off + (i * 8);
            uint32_t r_offset = rd32(rel + 0);
            uint32_t r_info = rd32(rel + 4);

            int type = ELF32_R_TYPE(r_info);
            int ofs_base = ELF32_R_OFS_BASE(r_info);
            int addr_base = ELF32_R_ADDR_BASE(r_info);

            /* Address delta to add */
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
                            uint32_t full_addr = ((uint32_t)hi_imm << 16) + (int32_t)lo_imm;
                            full_addr += h_delta;

                            uint16_t new_hi = (uint16_t)((full_addr >> 16) + ((full_addr & 0x8000) ? 1 : 0));
                            hi_inst = (hi_inst & 0xFFFF0000u) | new_hi;
                            wr32(data + hi_target, hi_inst);
                            patched_hi_lo++;
                        }
                    }
                    hi_count = 0;
                }

                if (delta != 0) {
                    /* Patch LO16 instruction */
                    /* Note: full_addr lower 16 bits is just (lo_imm + delta) & 0xFFFF */
                    uint32_t new_lo = (uint32_t)(lo_imm + delta) & 0xFFFFu;
                    lo_inst = (lo_inst & 0xFFFF0000u) | new_lo;
                    wr32(data + lo_target, lo_inst);
                    patched_orphan_lo++;
                }
                continue;
            }

            /* Flush any lingering HI16 without LO16 */
            hi_count = 0;

            if (type == R_MIPS_32) {
                if (delta != 0) {
                    uint32_t target = r_offset + seg_offset[ofs_base];
                    uint32_t val = rd32(data + target);
                    val += delta;
                    wr32(data + target, val);
                    patched_32++;
                }
            } else if (type == R_MIPS_26) {
                if (delta != 0) {
                    uint32_t target = r_offset + seg_offset[ofs_base];
                    uint32_t inst = rd32(data + target);
                    uint32_t j_addr = ((inst & 0x03FFFFFFu) << 2) + delta;
                    inst = (inst & 0xFC000000u) | ((j_addr >> 2) & 0x03FFFFFFu);
                    wr32(data + target, inst);
                    patched_26++;
                }
            }
        }
    }

    printf("[RELOC] Total relocations processed: %d\n", total_relocs);
    printf("[RELOC] Patched R_MIPS_32:    %d\n", patched_32);
    printf("[RELOC] Patched R_MIPS_26:    %d\n", patched_26);
    printf("[RELOC] Patched R_MIPS_HI16:  %d\n", patched_hi_lo);
    printf("[RELOC] Patched R_MIPS_LO16:  %d\n", patched_orphan_lo);

    printf("[RELOC] Writing output to: %s\n", out_path);
    FILE *out = fopen(out_path, "wb");
    if (!out) { fprintf(stderr, "Cannot open output file\n"); return 1; }
    fwrite(data, 1, fsize, out);
    fclose(out);
    free(data);

    printf("[RELOC] Success!\n");
    return 0;
}

