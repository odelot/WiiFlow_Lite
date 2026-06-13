/**
 * ra_exi.c
 *
 * WiiFlow-side EXI handshake with the Wii-RA-Adapter (ESP32-S3).
 *
 * Called from menu_game_boot.cpp BEFORE the IOS reload. At this point
 * WiiFlow owns the EXI bus; the ra-module (ARM/Starlet) is not yet running
 * so there is no bus conflict.
 *
 * All EXI transactions: channel 0 (Slot A), device 0, 8 MHz, half-duplex.
 * Write phase then read phase within the same CS-low window.
 *
 * EXI_Imm() accepts 1-4 bytes per call (libogc constraint). We loop in
 * 4-byte chunks, packing big-endian into a u32 for the hardware.
 */

#include <gccore.h>
#include <ogc/exi.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include <stdio.h>

#include "ra_exi.h"
#include "gc_ra_protocol.h"

/* Append one line to sd:/ra_exi_debug.txt for field diagnostics. */
static void ra_log(const char *msg)
{
    FILE *f = fopen("sd:/ra_exi_debug.txt", "a");
    if (f) { fputs(msg, f); fputc('\n', f); fclose(f); }
}

void RA_EXI_Log(const char *msg)
{
    ra_log(msg);
}

/* EXI wiring — ESP32 in Slot B (chan 1). Slot A (chan 0) is reserved by
 * IOS internals and our ra-module on Starlet can't drive the bus there. */
#define RA_EXI_CHAN  1   /* Slot B */
#define RA_EXI_DEV   0   /* CS0 */
#define RA_EXI_FREQ  EXI_SPEED8MHZ

/* Timeout when polling for GAME_LOADED (milliseconds). */
#define RA_DEFAULT_TIMEOUT_MS  30000u
#define RA_POLL_INTERVAL_MS    300u

/* -----------------------------------------------------------------------
 * Low-level helpers
 * ----------------------------------------------------------------------- */

/* Write `len` bytes from `buf` to the currently-selected EXI device.
 * Loops in 4-byte chunks using EXI_Imm. */
static void exi_write(const void *buf, u32 len)
{
    const u8 *p = (const u8 *)buf;
    while (len > 0) {
        u32 n = (len >= 4) ? 4 : len;
        /* Pack bytes big-endian into a u32 aligned word. */
        u32 word = 0;
        for (u32 i = 0; i < n; i++)
            word |= ((u32)p[i]) << ((3 - i) * 8);
        EXI_Imm(RA_EXI_CHAN, &word, n, EXI_WRITE, NULL);
        EXI_Sync(RA_EXI_CHAN);
        p   += n;
        len -= n;
    }
}

/* Read `len` bytes from the currently-selected EXI device into `buf`. */
static void exi_read(void *buf, u32 len)
{
    u8 *p = (u8 *)buf;
    while (len > 0) {
        u32 n = (len >= 4) ? 4 : len;
        u32 word = 0;
        EXI_Imm(RA_EXI_CHAN, &word, n, EXI_READ, NULL);
        EXI_Sync(RA_EXI_CHAN);
        /* Unpack from big-endian u32. */
        for (u32 i = 0; i < n; i++)
            p[i] = (word >> ((3 - i) * 8)) & 0xFF;
        p   += n;
        len -= n;
    }
}

/* Complete half-duplex transaction: lock → select → write → read → deselect → unlock.
 * Returns -1 on lock/select failure, 0 on success. */
static s32 exi_transaction(const void *tx, u32 tx_len, void *rx, u32 rx_len)
{
    if (!EXI_Lock(RA_EXI_CHAN, RA_EXI_DEV, NULL))
        return -1;
    if (!EXI_Select(RA_EXI_CHAN, RA_EXI_DEV, RA_EXI_FREQ)) {
        EXI_Unlock(RA_EXI_CHAN);
        return -1;
    }
    if (tx && tx_len) exi_write(tx, tx_len);
    if (rx && rx_len) exi_read(rx, rx_len);
    EXI_Deselect(RA_EXI_CHAN);
    EXI_Unlock(RA_EXI_CHAN);
    return 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

bool RA_EXI_Probe(void)
{
    ra_log("[RA_EXI] Probe start");

    ra_gc_header_t hdr;
    hdr.magic       = RA_MAGIC_GC_TO_ESP;
    hdr.command     = RA_CMD_IDENTIFY;
    hdr.payload_len = 0;

    for (int attempt = 0; attempt < 5; attempt++) {
        if (attempt > 0) {
            u64 wake = ticks_to_millisecs(gettime()) + 50;
            while (ticks_to_millisecs(gettime()) < wake) ;
        }

        char buf[64];
        snprintf(buf, sizeof(buf), "[RA_EXI] attempt %d", attempt);
        ra_log(buf);

        u8 rx[sizeof(ra_esp_header_t) + sizeof(u32)] = {0};

        s32 lck = EXI_Lock(RA_EXI_CHAN, RA_EXI_DEV, NULL);
        snprintf(buf, sizeof(buf), "[RA_EXI] EXI_Lock=%d", lck);
        ra_log(buf);
        if (!lck) continue;

        s32 sel = EXI_Select(RA_EXI_CHAN, RA_EXI_DEV, RA_EXI_FREQ);
        snprintf(buf, sizeof(buf), "[RA_EXI] EXI_Select=%d", sel);
        ra_log(buf);
        if (!sel) { EXI_Unlock(RA_EXI_CHAN); continue; }

        exi_write(&hdr, sizeof(hdr));
        exi_read(rx, sizeof(rx));
        EXI_Deselect(RA_EXI_CHAN);
        EXI_Unlock(RA_EXI_CHAN);

        snprintf(buf, sizeof(buf), "[RA_EXI] rx: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            rx[0],rx[1],rx[2],rx[3],rx[4],rx[5],rx[6],rx[7],rx[8],rx[9]);
        ra_log(buf);

        const ra_esp_header_t *resp = (const ra_esp_header_t *)rx;
        if (resp->magic != RA_MAGIC_ESP_TO_GC) {
            snprintf(buf, sizeof(buf), "[RA_EXI] bad magic: %02X (want %02X)", resp->magic, RA_MAGIC_ESP_TO_GC);
            ra_log(buf);
            continue;
        }

        u32 dev_id = ((u32)rx[sizeof(ra_esp_header_t)    ] << 24) |
                     ((u32)rx[sizeof(ra_esp_header_t) + 1] << 16) |
                     ((u32)rx[sizeof(ra_esp_header_t) + 2] <<  8) |
                     ((u32)rx[sizeof(ra_esp_header_t) + 3]      );
        snprintf(buf, sizeof(buf), "[RA_EXI] dev_id=%08lX (want %08lX)", (unsigned long)dev_id, (unsigned long)RA_DEVICE_ID);
        ra_log(buf);

        if (dev_id == RA_DEVICE_ID) {
            ra_log("[RA_EXI] Probe OK");
            return true;
        }
    }
    ra_log("[RA_EXI] Probe FAILED");
    return false;
}

bool RA_EXI_LoadGame(const char *game_id, u32 timeout_ms, const char *md5_hex)
{
    if (!game_id || game_id[0] == '\0') return false;
    if (timeout_ms == 0) timeout_ms = RA_DEFAULT_TIMEOUT_MS;

    /* ---------- 1. Send LOAD_GAME ---------- */
    /* Mirrors ra_load_game_t in gc_ra_protocol.h. md5_hex (32 lowercase
     * hex chars, computed on-console by RA_ComputeWiiHash) lets the ESP
     * identify the game exactly like Dolphin would — no game-ID table. */
    struct __attribute__((packed)) {
        ra_gc_header_t  hdr;
        char            game_id[RA_GAME_ID_LEN];
        u8              disc_number;   /* 0 for single-disc */
        u8              has_hash;
        char            md5_hash[RA_HASH_LEN];
    } tx;

    tx.hdr.magic       = RA_MAGIC_GC_TO_ESP;
    tx.hdr.command     = RA_CMD_LOAD_GAME;
    tx.hdr.payload_len = sizeof(tx) - sizeof(ra_gc_header_t);
    memcpy(tx.game_id, game_id, RA_GAME_ID_LEN);
    tx.disc_number = 0;
    memset(tx.md5_hash, 0, sizeof(tx.md5_hash));
    if (md5_hex && md5_hex[0]) {
        tx.has_hash = 1;
        memcpy(tx.md5_hash, md5_hex, RA_HASH_LEN);
        ra_log(md5_hex);
    } else {
        tx.has_hash = 0;
    }

    u8 rx6[sizeof(ra_esp_header_t)] = {0};

    /* Send LOAD_GAME. The 6-byte response in THIS transaction is unreliable:
     * with half-duplex framing, the ESP32 only sees the command after the
     * transaction completes, so the bytes it clocks out come from the
     * previously-prepared tx_buf (typically the IDENTIFY response tail).
     * Don't check magic/status here — proceed directly to polling, which
     * IS the authoritative status channel. */
    if (exi_transaction(&tx, sizeof(tx), rx6, sizeof(rx6)) < 0)
        return false;
    ra_log("[RA_EXI] LOAD_GAME sent, polling for GAME_LOADED");

    /* ---------- 2. Poll until GAME_LOADED (0x06) or error ---------- */
    ra_gc_header_t poll_hdr;
    poll_hdr.magic       = RA_MAGIC_GC_TO_ESP;
    poll_hdr.command     = RA_CMD_POLL;
    poll_hdr.payload_len = 0;

    u64 deadline_ms = ticks_to_millisecs(gettime()) + (u64)timeout_ms;
    u8 last_logged_status = 0xFF;  /* impossible value to force first log */
    u32 poll_count = 0;
    while (ticks_to_millisecs(gettime()) < deadline_ms) {
        /* ~300ms busy-wait between polls — OK pre-boot (single-threaded). */
        u64 wake = ticks_to_millisecs(gettime()) + RA_POLL_INTERVAL_MS;
        while (ticks_to_millisecs(gettime()) < wake)
            ;

        u8 prx[sizeof(ra_esp_header_t)] = {0};
        if (exi_transaction(&poll_hdr, sizeof(poll_hdr), prx, sizeof(prx)) < 0)
            continue;   /* bus glitch, keep trying */

        poll_count++;
        const ra_esp_header_t *r = (const ra_esp_header_t *)prx;
        if (r->magic != RA_MAGIC_ESP_TO_GC) {
            if ((poll_count % 10) == 1) {
                char lbuf[80];
                snprintf(lbuf, sizeof(lbuf), "[RA_EXI] poll %lu: bad magic %02X", (unsigned long)poll_count, r->magic);
                ra_log(lbuf);
            }
            continue;
        }

        u8 status = r->status;
        if (status != last_logged_status) {
            char lbuf[80];
            snprintf(lbuf, sizeof(lbuf), "[RA_EXI] poll %lu: status=%02X", (unsigned long)poll_count, status);
            ra_log(lbuf);
            last_logged_status = status;
        }
        if (status == RA_STATUS_GAME_LOADED || status == RA_STATUS_ACTIVE) {
            ra_log("[RA_EXI] GAME_LOADED — proceeding with boot");
            return true;
        }
        if (status >= RA_STATUS_ERROR_WIFI) {
            ra_log("[RA_EXI] ESP32 reported error — aborting");
            return false;
        }
        /* RA_STATUS_LOADING_GAME (0x05) or anything else: keep waiting */
    }

    ra_log("[RA_EXI] LoadGame timed out");
    return false; /* timeout */
}
