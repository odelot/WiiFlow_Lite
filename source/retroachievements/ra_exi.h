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
 * @param md5_hex     RA hash (32 lowercase hex chars) computed on-console
 *                    by RA_ComputeWiiHash, or NULL → ESP falls back to
 *                    its game-ID table
 * @return            true on success, false on error or timeout
 */
bool RA_EXI_LoadGame(const char *game_id, u32 timeout_ms, const char *md5_hex);

/**
 * Tell the ESP32 to wipe its stored WiFi + RetroAchievements credentials
 * (saved by WiFiManager / EEPROM) and reboot into its config portal.
 *
 * This is the software replacement for the nes-ra-adapter's physical reset
 * button on the memory card: the user triggers it from WiiFlow's Settings
 * menu. After this the ESP32 reboots and re-broadcasts the "WII_RA_ADAPTER"
 * Wi-Fi AP so credentials can be re-entered at http://192.168.1.1.
 *
 * Call only while WiiFlow owns the EXI bus (i.e. from a menu, before any
 * IOS reload — same constraint as RA_EXI_Probe / RA_EXI_LoadGame).
 *
 * @return true if the adapter was detected and the reset command was sent.
 *         The per-transaction response is not checked (the ESP reboots).
 */
bool RA_EXI_ResetCredentials(void);

/**
 * Append one line to sd:/ra_exi_debug.txt — field diagnostics without a
 * USB Gecko. Must be called while the SD card is still mounted (i.e.
 * before WiiFlow_ExternalBooter / ShutdownBeforeExit).
 */
void RA_EXI_Log(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* _RA_EXI_H_ */
