/**
 * ra_exi.h
 *
 * WiiFlow-side EXI handshake with the Wii-RA-Adapter (ESP32-S3).
 *
 * Called from menu_game_boot.cpp BEFORE the IOS reload.  At this point
 * WiiFlow owns the EXI bus; the ra-module (ARM/Starlet) is not yet running.
 *
 * Flow:
 *   1. RA_EXI_Probe()      — check the ESP32 is present on Slot B
 *   2. RA_EXI_LoadGame()   — send disc ID, block until GAME_LOADED or error
 *
 * Uses libogc EXI directly (ogc/exi.h).  All transactions are on
 * EXI channel 1 (Slot B), device 0, 8 MHz, half-duplex (write then read).
 */

#ifndef _RA_EXI_H_
#define _RA_EXI_H_

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Probe for the Wii-RA-Adapter on EXI Slot B.
 * Sends RA_CMD_IDENTIFY and checks the device ID in the response.
 *
 * @return true if the adapter is detected and responding
 */
bool RA_EXI_Probe(void);

/**
 * Result of the pre-boot LOAD_GAME handshake. Anything but RA_LOAD_OK
 * aborts the boot when RetroAchievements is enabled — each value maps to
 * a specific user-facing message in menu_game_boot.cpp.
 */
typedef enum {
    RA_LOAD_OK = 0,
    RA_LOAD_ERR_BUS,            /* EXI bus never answered (adapter unplugged mid-way / wiring) */
    RA_LOAD_ERR_TIMEOUT,        /* adapter alive but never reached a terminal status */
    RA_LOAD_ERR_NOT_CONFIGURED, /* adapter is in its WiFi config portal (no credentials) */
    RA_LOAD_ERR_WIFI,           /* adapter can't connect to WiFi / reach the internet */
    RA_LOAD_ERR_LOGIN,          /* RetroAchievements rejected the stored credentials */
    RA_LOAD_ERR_UNKNOWN_GAME,   /* hash not in the RA database — bad dump / unsupported version */
    RA_LOAD_ERR_GAME,           /* other game-load failure (RA server/API error) */
    RA_LOAD_ERR_PROTOCOL,       /* malformed exchange */
} ra_load_result_t;

/**
 * Called whenever the adapter's reported status byte changes while
 * RA_EXI_LoadGame is waiting (values: ra_status_t in gc_ra_protocol.h),
 * so the UI can show what the adapter is doing. May be NULL.
 */
typedef void (*ra_exi_status_cb_t)(u8 esp_status);

/**
 * Send a game ID + RA hash to the ESP32 and wait for it to finish loading
 * the achievement data from the RetroAchievements servers.
 *
 * Two phases within the same deadline:
 *   1. Wait until the adapter is ready (status >= LOGGED_IN — it may still
 *      be joining WiFi or logging in right after power-on). If it reports
 *      the config portal, fail immediately with RA_LOAD_ERR_NOT_CONFIGURED.
 *   2. Send LOAD_GAME, then poll until GAME_LOADED (success) or an error
 *      status (0xE0+, mapped to the matching ra_load_result_t).
 *
 * On timeout the last observed status refines the result (stuck joining
 * WiFi → RA_LOAD_ERR_WIFI, stuck logging in → RA_LOAD_ERR_LOGIN, …).
 *
 * Typical wait: 5-20 seconds (depends on Wi-Fi + RA API response time).
 *
 * @param game_id     6-byte Wii disc ID from disc header (e.g. "RSBE01")
 * @param timeout_ms  Maximum wait time in milliseconds (0 = 30 000)
 * @param md5_hex     RA hash (32 lowercase hex chars) computed on-console
 *                    by RA_ComputeWiiHash, or NULL → ESP falls back to
 *                    its game-ID table
 * @param status_cb   Optional UI callback for status transitions
 * @return            RA_LOAD_OK on success, specific error otherwise
 */
ra_load_result_t RA_EXI_LoadGame(const char *game_id, u32 timeout_ms,
                                 const char *md5_hex, ra_exi_status_cb_t status_cb);

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
