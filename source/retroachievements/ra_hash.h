/**
 * ra_hash.h
 *
 * RetroAchievements Wii game hash, computed ON CONSOLE from the stored
 * game image (WBFS partition, .wbfs or .iso on USB/SD). Faithful port of
 * rcheevos' rc_hash_wii_disc() encrypted-disc path — the canonical hash
 * the RA database is built from — so any game RA knows is identified
 * without the hardcoded game-ID→hash table.
 */

#ifndef _RA_HASH_H_
#define _RA_HASH_H_

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compute the RA MD5 for the given game.
 *
 * @param discid   6-byte disc ID (used to locate the game on WBFS)
 * @param path     image path (empty/NULL = physical disc → unsupported,
 *                 caller falls back to the game-ID table)
 * @param out_hex  receives 32 lowercase hex chars + NUL
 * @return         0 on success; negative on failure (unsupported format,
 *                 read error, decrypted image, no partitions)
 */
s32 RA_ComputeWiiHash(const u8 *discid, const char *path, char out_hex[33]);

#ifdef __cplusplus
}
#endif

#endif /* _RA_HASH_H_ */
