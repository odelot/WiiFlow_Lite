/**
 * ra_hash.c
 *
 * On-console RetroAchievements Wii hash. Faithful port of rcheevos
 * rc_hash_wii_disc() — ENCRYPTED-disc path only, which is the canonical
 * form the RA hash database is built from (retail dumps have byte 0x61
 * == 0). The exact byte ranges, in MD5 order:
 *
 *   1. main header: first 0x80 bytes of the disc
 *   2. region code: 4 bytes at 0x4E000
 *   3. per non-update partition:
 *      a. TMD: tmd_size (capped 0x7C00) bytes at part_base + tmd_offset
 *      b. up to 1024 raw ENCRYPTED clusters: 0x7C00 bytes each at
 *         part_offset + ix*0x8000 + 0x400
 *
 * IMPORTANT quirk replicated on purpose: rcheevos seeks the cluster
 * reads at the partition header's data-offset field treated as an
 * ABSOLUTE disc offset (it does NOT add the partition base). For retail
 * discs that lands in the mostly-zero region before the update
 * partition. Bug or not, the RA database is built with this code — we
 * must match it bit for bit. A happy consequence: WBFS images that
 * stripped those (zero/unused) blocks still hash identically, because
 * Dolphin's WBFS reader also presents stripped blocks as zeros — and so
 * do we (see ra_disc_read).
 *
 * No decryption anywhere: the encrypted path hashes raw disc bytes.
 * Reads go through wbfs_disc_read(), which already unifies the three
 * storage backends (WBFS partition, .wbfs file, .iso file).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ogc/lwp_watchdog.h>

#include "ra_hash.h"
#include "ra_exi.h"               /* RA_EXI_Log → sd:/ra_exi_debug.txt */
#include "libwbfs/libwbfs.h"
#include "loader/wbfs.h"
#include "channel/MD5.h"
#include "memory/mem2.hpp"

#define RA_CLUSTER_SIZE      0x7C00u
#define RA_MAX_CLUSTER_COUNT 1024u

/* Read `len` bytes at byte offset `off` (must be 4-aligned). Returns 0
 * on success.
 *
 * d != NULL → stored image via wbfs_disc_read (WBFS partition / .wbfs /
 * .iso). Its "block not allocated" result (1) is mapped to a ZERO-FILLED
 * buffer — matching how Dolphin presents stripped WBFS blocks, which is
 * what the canonical hash expects.
 *
 * d == NULL → PHYSICAL DISC via __WBFS_ReadDVD (WDVD_UnencryptedRead) —
 * the exact same raw-read path WiiFlow's disc-ripper uses, with the same
 * offset>>2 convention; the d2x DI allows it across the disc. If the
 * drive/media refuses (e.g. some retail-disc + drive combinations), the
 * read fails and the caller falls back to the game-ID table. */
static s32 ra_disc_read(wbfs_disc_t *d, u64 off, void *buf, u32 len)
{
    s32 ret;
    memset(buf, 0, len);
    if (d) {
        ret = wbfs_disc_read(d, (u32)(off >> 2), len, (u8 *)buf);
        if (ret == 0) return 0;
        if (ret == 1) return 0;   /* unallocated WBFS block = zeros */
        return -1;
    }
    ret = __WBFS_ReadDVD(NULL, (u32)(off >> 2), len, buf);
    return (ret >= 0) ? 0 : -1;
}

static u32 ra_be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

s32 RA_ComputeWiiHash(const u8 *discid, const char *path, char out_hex[33])
{
    wbfs_disc_t *d;
    auth_md5Ctx md5;
    u8 *buffer;
    u8 quad[4];
    u8 digest[16];
    u32 partition_info_table[8];
    u32 *partition_table = NULL;
    u32 total_partition_count = 0;
    u32 ix, jx, kx;
    s32 ret = -1;
    u64 t0 = gettime();

    out_hex[0] = '\0';

    if (!path || path[0] == '\0') {
        /* Physical disc: d stays NULL; ra_disc_read uses the raw
         * WDVD_UnencryptedRead path. The disc header was already read by
         * WiiFlow's browser, so the DI is initialized at this point. */
        d = NULL;
        RA_EXI_Log("[RA_HASH] physical disc — raw DVD reads");
    } else {
        d = WBFS_OpenDisc((u8 *)discid, (char *)path);
        if (!d) {
            RA_EXI_Log("[RA_HASH] WBFS_OpenDisc failed");
            return -1;
        }
    }

    buffer = (u8 *)MEM2_alloc(RA_CLUSTER_SIZE);
    if (!buffer) {
        WBFS_CloseDisc(d);
        return -1;
    }

    auth_md5InitCtx(&md5);

    do {
        /* Encryption marker: byte 0x61 == 0 means encrypted (retail).
         * Decrypted images hash a different content set in rcheevos —
         * not supported here; fall back to the ID table. */
        if (ra_disc_read(d, 0x60, quad, 4) < 0) break;
        if (quad[1] != 0) {
            RA_EXI_Log("[RA_HASH] image is decrypted-format — unsupported");
            break;
        }

        /* 1. Main header (0x80 bytes at 0). */
        if (ra_disc_read(d, 0, buffer, 0x80) < 0) break;
        auth_md5SumCtx(&md5, buffer, 0x80);

        /* 2. Region code (4 bytes at 0x4E000). */
        if (ra_disc_read(d, 0x4E000, quad, 4) < 0) break;
        auth_md5SumCtx(&md5, quad, 4);

        /* 3. Partition info table at 0x40000: 4 pairs of
         *    (partition_count, table_offset>>2). */
        if (ra_disc_read(d, 0x40000, buffer, 32) < 0) break;
        for (ix = 0; ix < 8; ix++) {
            partition_info_table[ix] = ra_be32(buffer + ix * 4);
            if ((ix % 2) == 0)
                total_partition_count += partition_info_table[ix];
        }
        if (total_partition_count == 0 || total_partition_count > 16) {
            RA_EXI_Log("[RA_HASH] no/bogus partitions");
            break;
        }

        partition_table = (u32 *)MEM2_alloc(total_partition_count * 8);
        if (!partition_table) break;

        kx = 0;
        for (jx = 0; jx < 8; jx += 2) {
            u64 tbl = ((u64)partition_info_table[jx + 1]) << 2;
            for (ix = 0; ix < partition_info_table[jx]; ix++) {
                /* pairs of (partition_offset>>2, type) */
                if (ra_disc_read(d, tbl + ix * 8, buffer, 8) < 0) goto fail;
                partition_table[kx++] = ra_be32(buffer);
                partition_table[kx++] = ra_be32(buffer + 4);
            }
        }

        /* 4. Per-partition: TMD + raw encrypted clusters. */
        for (jx = 0; jx < total_partition_count * 2; jx += 2) {
            u64 part_base;
            u64 tmd_offset, part_offset, part_size;
            u32 tmd_size, cluster_count;

            if (partition_table[jx + 1] == 1)
                continue;   /* skip update partition */

            part_base = ((u64)partition_table[jx]) << 2;

            /* TMD size @+0x2A4, offset @+0x2A8 (<<2, partition-relative). */
            if (ra_disc_read(d, part_base + 0x2A4, buffer, 8) < 0) goto fail;
            tmd_size   = ra_be32(buffer);
            tmd_offset = ((u64)ra_be32(buffer + 4)) << 2;
            if (tmd_size > RA_CLUSTER_SIZE)
                tmd_size = RA_CLUSTER_SIZE;

            if (ra_disc_read(d, part_base + tmd_offset, buffer, tmd_size) < 0)
                goto fail;
            auth_md5SumCtx(&md5, buffer, (int)tmd_size);

            /* Data offset @+0x2B8, size @+0x2BC (both <<2). NOTE: the
             * cluster seeks below use part_offset as ABSOLUTE — the
             * rcheevos quirk, replicated faithfully (see file header). */
            if (ra_disc_read(d, part_base + 0x2B8, buffer, 8) < 0) goto fail;
            part_offset = ((u64)ra_be32(buffer)) << 2;
            part_size   = ((u64)ra_be32(buffer + 4)) << 2;

            cluster_count = (u32)(part_size / 0x8000);
            if (cluster_count > RA_MAX_CLUSTER_COUNT)
                cluster_count = RA_MAX_CLUSTER_COUNT;

            for (ix = 0; ix < cluster_count; ix++) {
                u64 off = part_offset + ((u64)ix * 0x8000) + 0x400;
                if (ra_disc_read(d, off, buffer, RA_CLUSTER_SIZE) < 0)
                    goto fail;
                auth_md5SumCtx(&md5, buffer, (int)RA_CLUSTER_SIZE);
            }
        }

        auth_md5CloseCtx(&md5, digest);
        for (ix = 0; ix < 16; ix++) {
            static const char hexd[] = "0123456789abcdef";
            out_hex[ix * 2]     = hexd[digest[ix] >> 4];
            out_hex[ix * 2 + 1] = hexd[digest[ix] & 0xF];
        }
        out_hex[32] = '\0';
        ret = 0;

        {
            char msg[80];
            u32 ms = ticks_to_millisecs(gettime() - t0);
            snprintf(msg, sizeof(msg), "[RA_HASH] %s (%lums)",
                     out_hex, (unsigned long)ms);
            RA_EXI_Log(msg);
        }
    } while (0);

fail:
    if (partition_table) MEM2_free(partition_table);
    MEM2_free(buffer);
    WBFS_CloseDisc(d);
    return ret;
}
