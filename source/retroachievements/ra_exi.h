/**
 * ra_exi.h
 *
 * WiiFlow-side EXI handshake with the Wii-RA-Adapter (ESP32-S3).
 *
 * Called from menu_game_boot.cpp BEFORE the IOS reload.  At this point
 * WiiFlow owns the EXI bus; the ra-module (ARM/Starlet) is not yet running.
 *
 * Flow:
 *   1. RA_EXI_Probe()      — check the ESP32 is present on Slot A
 *   2. RA_EXI_LoadGame()   — send disc ID, block until GAME_LOADED or error
 *
 * Uses libogc EXI directly (ogc/exi.h).  All transactions are on
 * EXI channel 0 (Slot A), device 0, 8 MHz, half-duplex (write then read).
 */

#ifndef _RA_EXI_H_
#define _RA_EXI_H_

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Probe for the Wii-RA-Adapter on EXI Slot A.
 * Sends RA_CMD_IDENTIFY and checks the device ID in the response.
 *
 * @return true if the adapter is detected and responding
 */
bool RA_EXI_Probe(void);

/**
 * Send a game ID to the ESP32 and wait for it to finish loading the
 * achievement data from RetroAchievements servers.
 *
 * Blocks until:
 *   - ESP32 status == RA_STATUS_GAME_LOADED (0x06) → returns true
 *   - ESP32 status >= 0xE0 (error codes)           → returns false
 *   - timeout_ms elapsed                           → returns false
 *
 * Typical wait: 5-20 seconds (depends on Wi-Fi + RA API response time).
 *
 * @param game_id     6-byte Wii disc ID from disc header (e.g. "RSBE01")
 * @param timeout_ms  Maximum wait time in milliseconds (0 = 30 000)
 * @return            true on success, false on error or timeout
 */
bool RA_EXI_LoadGame(const char *game_id, u32 timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _RA_EXI_H_ */
