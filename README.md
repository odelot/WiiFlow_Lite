# WiiFlow Lite — RetroAchievements Fork

> **This is a fork of [WiiFlow Lite by Fledge68](https://github.com/Fledge68/WiiFlow_Lite)** extended
> with a RetroAchievements integration layer for Wii games via an external ESP32-S3 hardware adapter.
>
> It is one of four cooperating projects; see the
> [**wii-ra-adapter**](https://github.com/odelot/wii-ra-adapter) repository for the system overview
> and for the **pre-built binaries of all four projects** (releases are published there).

> **USB is the default link.** The adapter now plugs into a Wii **USB port** with a plain cable —
> no memory card connector, no soldering. This build looks for it on USB first and falls back to
> the original memory card Slot B link, so both kinds of adapter work with the same `boot.dol`.

---

## What changed after the fork

WiiFlow is the **front end** of the Wii-game path: it is the last code that runs with full control
of the console before the game boots, so it does everything that must happen *before* the IOS
reload — detect the adapter, identify the game, wait for the achievement set, and plant the
PPC-side hooks the `ra-module` (in the d2x cIOS) depends on afterwards.

### New files

| File | Purpose |
|---|---|
| `source/retroachievements/gc_ra_protocol.h` | Shared binary protocol between WiiFlow (PPC), the `ra-module` (ARM/Starlet), and the ESP32 adapter. Defines commands, status codes, packet layouts, and constants such as `RA_DEVICE_ID = 0x52410001`. |
| `source/retroachievements/ra_exi.h` / `ra_exi.c` | WiiFlow-side **adapter link** (the name predates USB). Called **before** the IOS reload. `RA_EXI_Probe()` looks for the adapter on **USB** first (VID:PID `303A:4052`) and only then on memory card **Slot B**; every later call goes through `ra_transaction()`, which dispatches to whichever link was found. Sends `LOAD_GAME` with the disc ID and the on-console RA hash and blocks until the adapter responds `GAME_LOADED (0x06)` or a timeout expires (90 s). Boot always proceeds — RA is best-effort. Also implements `RA_EXI_ResetCredentials()` and the `sd:/ra_exi_debug.txt` field log. |
| `source/retroachievements/ra_hash.h` / `ra_hash.c` | On-console computation of the RetroAchievements MD5 fingerprint. Faithful port of rcheevos' `rc_hash_wii_disc()` **encrypted-disc path** — the canonical form the RA hash database is built from — including its quirks, replicated bit-for-bit. Works for WBFS partitions, `.wbfs` and `.iso` images (unified through `wbfs_disc_read`, with stripped WBFS blocks presented as zeros exactly like Dolphin does) and for **physical discs** via the raw-read path the disc ripper uses. Any game the RA database knows is identified without a hardcoded ID→hash table. |

### The USB link in `ra_exi.c`

The RA protocol is unchanged — only the wire. The adapter firmware built for the cable is a
vendor-class USB device (`303A:4052`, bulk OUT `0x01`, bulk IN `0x81`):

- **Finding it** — `ra_usb_open()` scans the libogc USB device list (USBV5, retried for ~500 ms
  because a fresh `USB_Initialize()` fills the list asynchronously), and on IOSes without USBV5
  opens the device directly by VID:PID on `/dev/usb/oh0`. It selects configuration 1 if needed.
  Every step is written to `sd:/ra_exi_debug.txt`.
- **One reply per request** — each exchange posts the reply read first (`USB_ReadBlkMsgAsync`),
  then writes the request, and waits up to 500 ms. The adapter answers every request with exactly
  the bytes an EXI read would have returned, so `usb_transaction()` reproduces
  `exi_transaction()`: the first `tx_len` bytes of the reply (what the EXI write phase consumed)
  are skipped. The rest of the code is the same for both links.
- **Framing** — no message is an exact multiple of 64 bytes (a `0x00` byte is appended), so every
  message ends on a short packet. Replies arrive trimmed to a multiple of 4 bytes, so the adapter
  pads them; `ra_usb_identify()` logs the received length to make that visible.
- **Error texts** — the "adapter not detected" messages no longer mention Slot B, since the
  adapter may be on either link.

<details>
<summary><b>Memory card (EXI) link — legacy adapters</b></summary>

Used when no USB adapter is found: the ESP32 on memory card **Slot B** (`RA_EXI_CHAN = 1`,
device 0, 8 MHz). WiiFlow still owns the bus before the IOS reload. On channels 0/1 libogc requires
`EXI_Lock` before `EXI_Select`.

</details>

### Changes to `menu_game_boot.cpp`

1. **RA probe + load** — before the boot teardown, `_launchWii()` calls `RA_EXI_Probe()` (USB,
   then Slot B), then `RA_ComputeWiiHash()` + `RA_EXI_LoadGame()` (90 s budget for the ESP32's
   Wi-Fi fetch of the achievement set). The loading animation runs on a high-priority thread so
   the UI stays alive during the multi-second hash + network wait. If the hash fails (e.g. a drive
   that refuses raw reads), `LOAD_GAME` is sent with the disc ID only and the ESP32 falls back to
   its game-ID table.

2. **VBI hooktype enforcement** — when the adapter is detected (`ra_active`), the Ocarina
   hooktype is forced to `1` (VBI) even with no cheats enabled, so the codehandler hooks the
   game's `__VIRetraceHandler`.

3. **Boot-path breadcrumbs** — when RA is active, `RA_EXI_Log()` drops one-line markers through
   the boot sequence into `sd:/ra_exi_debug.txt` (while SD is still mounted) — field diagnostics
   without a USB Gecko.

### Changes to `source/loader/fst.c` — the injected VBlank hook

`ocarina_load_code()` now runs even with no user cheats and appends a synthetic **Gecko C0 code**
that the codehandler executes on every vertical retrace. The extended variant
(`RA_TROPHY_OVERLAY 1`) does two jobs:

- **Frame counter** — increments a `u32` at physical `0x2FF8` through the uncached MEM1 mirror
  (`0xC0002FF8`). This counter is the heartbeat of the whole Wii path: the `ra-module` on Starlet
  polls it to fire memory snapshots on true frame boundaries (the ARM side has no VI interrupt),
  uses it as the game-alive signal that activates the module in the first place, and ships it to
  the ESP32 as the game-frame clock for rcheevos timer accuracy.
- **Trophy badge** — reads a flag at `0xC0002FFC` (the `ra-module` raises it on an achievement
  unlock, blinking 3× in sync with the disc-slot LED) and, while set, draws a 32×32 badge (white
  box, black rounded border, gold trophy) in the top-left of the XFB — into both the current and
  previous framebuffer, so it survives double buffering. Assembly source in
  `docs/ra_trophy/ra_trophy_hook.s`.

The counter/flag live in the reserved tail of the Ocarina codelist region (`0x2FF8..0x2FFF`);
the GCT size is capped so user cheat packs can never grow into it. If a cheat pack is too big to
fit alongside the hook, the cheats win and the hook is skipped — the `ra-module` falls back to
timer pacing automatically.

### Changes to the settings menu

`menu_config_main.cpp` adds a **RetroAchievements adapter** page with *"Reset adapter WiFi & RA
login"*: sends `RA_CMD_RESET_CREDENTIALS` (over whichever link the adapter is on), making the
ESP32 wipe its stored credentials and reboot into its configuration portal (connect to the
`WII_RA_ADAPTER` Wi-Fi network to re-configure). This is the software replacement for the
physical reset button of the nes-ra-adapter.

---

## System architecture

WiiFlow is one piece of a **four-project system** (this fork covers Wii games; GameCube games go
through the [Nintendont fork](https://github.com/odelot/Nintendont) instead, which needs no
external loader help):

```
┌──────────────────────────────────────────────────────────────────────────┐
│  Wii hardware                                                            │
│                                                                          │
│  ┌─────────────────────────────────────┐   USB (bulk, 303A:4052)         │
│  │  Broadway (PPC)                     │◄──────────────────────────────┐ │
│  │  WiiFlow_Lite (this repo)           │   pre-boot handshake:         │ │
│  │                                     │  1. RA_EXI_Probe()            │ │
│  │  Before IOS reload:                 │  2. RA_ComputeWiiHash()       │ │
│  │  • Computes RA disc MD5 on-console  │  3. RA_EXI_LoadGame()         │ │
│  │  • Sends LOAD_GAME to ESP32         │     → blocks until            │ │
│  │  • Injects VBI + trophy C0 hook     │       GAME_LOADED (0x06)      │ │
│  │  • Forces VBI hooktype              │                               │ │
│  │  • Boots game via d2x cIOS          │   every VBlank:               │ │
│  └─────────────────────────────────────┘ 4. chain walk + SNAPSHOT      │ │
│                                          5. ADDR_QUERY / watchlist     │ │
│  ┌─────────────────────────────────────┐    mutations (≤12 rounds)     │ │
│  │  Starlet (ARM, d2x-v999-usb cIOS)   │ 6. ACHIEVEMENT events ────────┘ │
│  │  ra-module  (d2x-cios repo)         │                                 │
│  │                                     │                                 │
│  │  After game boots:                  │                                 │
│  │  • Polls VBI counter at 0x2FF8      │                                 │
│  │  • Walks pointer chains in RAM      │                                 │
│  │  • Reads MEM1/MEM2 watched addrs    │                                 │
│  │  • LED + trophy flag on unlock      │                                 │
│  └─────────────────────────────────────┘                                 │
└──────────────────────────────────────────────────────────────────────────┘
                  │ USB cable (power + data)
                  ▼
┌──────────────────────────────────────────────────────────────────────────┐
│  ESP32-S3 dev board (wii-ra-adapter repo)                                │
│                                                                          │
│  Core 1 (realtime):  USB device → snapshot queue → rc_client_do_frame()  │
│  Core 0 (network):   Wi-Fi / TLS / RA API — all blocking I/O             │
│                      + web dashboard at http://wii-ra.local/             │
└──────────────────────────────────────────────────────────────────────────┘
```

### Component repositories

| Repo | Role | Language |
|---|---|---|
| **WiiFlow_Lite** ← this repo | PPC loader for Wii games; hash, handshake, VBI/trophy hook | C++ (libogc) |
| [d2x-cios](https://github.com/odelot/d2x-cios) | Custom IOS; `source/ra-module/` is the ARM memory server for Wii games | C (ARM/Starlet) |
| [Nintendont](https://github.com/odelot/Nintendont) | GameCube game path (loader + kernel memory server in one) | C (ARM/Starlet) |
| [wii-ra-adapter](https://github.com/odelot/wii-ra-adapter) | ESP32-S3 firmware; runs rcheevos, talks to RA servers, hosts the dashboard. **Binaries for everything are released here.** | C++ (ESP-IDF / Arduino) |

The supported boards and firmware builds are listed in the
[wii-ra-adapter README](https://github.com/odelot/wii-ra-adapter#hardware).

### Boot sequence (Wii game with RA adapter present)

```
WiiFlow selects game
  └─► RA_EXI_Probe()            — find the ESP32 on USB (else memory card Slot B)
  └─► _showWaitMessage()        — loading animation (high-priority thread)
  └─► RA_ComputeWiiHash()       — rcheevos MD5 from WBFS/ISO/physical disc (~2-5 s)
  └─► RA_EXI_LoadGame()         — send LOAD_GAME; poll ESP32 until GAME_LOADED (≤90 s)
        (ESP32: Wi-Fi → RA API → parse set → build watchlist + chain table)
  └─► ocarina_load_code()       — inject VBI counter + trophy badge C0 hook
  └─► IOS reload (d2x cIOS)     — ra-module starts on Starlet, silent
  └─► Game boots (PPC)
        └─► C0 hook increments counter at 0x2FF8 every retrace
              └─► ra-module wakes: link up → IDENTIFY → fetch watchlist → fetch chain table
                    └─► every VBlank: chain walk + RAM sample → SNAPSHOT → ESP32
                          └─► rc_client_do_frame() → unlock → LED + trophy badge
```

---

## Releases

Pre-built binaries (this fork's `boot.dol` together with the matching d2x cIOS, Nintendont and
ESP32 firmware) are published in the
[**wii-ra-adapter releases**](https://github.com/odelot/wii-ra-adapter/releases).
Install like upstream WiiFlow Lite (see *Installing* below).

---

# Original WiiFlow Lite README

# WiiFlow Lite
My mod of the Wii USB Loader WiiFlow

## Description
WiiFlow Lite is a wii homebrew app used to display and launch your games and apps stored on a USB device or SD card plugged into a Wii or Wii U in Wii mode. The games and apps are displayed in cover flow style display.

## Installing
As of v5.2.0 WiiFlow Lite will simply be a replacement for WiiFlow. Put it in apps/wiiflow and use wiiflow forwarder's to launch it via the wii system menu. forwarders can be found on wiiflowiki4. for previous wiiflow lite users, sorry but you must uninstall your wiiflow lite forwarder and replace it with a wiiflow forwarder.

Simply download the latest release and extract it to your apps/wiiflow folder on SD or USB HDD. SD is recommended. Your device should be formatted to FAT32.

## Booting
To start WiiFlow Lite you will need the Homebrew Channel or a WiiFlow forwarder channel installed on your Wii or vWii system menu.

## Themes
Currently only Rhapsodii and Rhapsodii Shima themes are compatible with WiiFlow Lite. Other older wiiflow themes need to be updated to work properly with WFL.

Rhapsodii made by Hakaisha is a new theme designed for wiiflow lite. find it here - (https://gbatemp.net/threads/wiiflow-lite-theme-rhapsodii.511833/)

Other wiiflow lite themes can be found on the wiki linked below. but they need to be updated to properly work with wiiflow lite.

## Useful Links
[WiiFlow Lite GBATemp thread](https://gbatemp.net/threads/wiiflow-lite.422685/)

[WiiFlow Wiki](https://web.archive.org/web/20220414124727/https://sites.google.com/site/wiiflowiki4/)

[Newer Wiki WIP](https://sites.google.com/view/wiiflow-wiki/welcome)

[Github Wiki](https://github.com/Fledge68/WiiFlow_Lite/wiki)

[Old Sourceforge Project Repository](https://sourceforge.net/projects/wiiflow-lite/)
