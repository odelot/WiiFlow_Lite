/*
 *  Copyright (C) 2008 Nuke (wiinuke@gmail.com)
 *
 *  this file is part of GeckoOS for USB Gecko
 *  http://www.usbgecko.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <sys/unistd.h>
#include <ogc/ipc.h>

#include "fst.h"
#include "sys.h"

#include "gecko/gecko.hpp"
#include "memory/mem2.hpp"

#define FSTDIRTYPE 1
#define FSTFILETYPE 0
#define ENTRYSIZE 0xC

#define MAX_FILENAME_LEN	128

u8 *codelistend;
void *codelist;

u32 gameconfsize = 0;
u32 *gameconf = NULL;

u8 debuggerselect = 0;

extern const u32 viwiihooks[4];
extern const u32 kpadhooks[4];
extern const u32 joypadhooks[4];
extern const u32 gxdrawhooks[4];
extern const u32 gxflushhooks[4];
extern const u32 ossleepthreadhooks[4];
extern const u32 axnextframehooks[4];
extern const u32 wpadbuttonsdownhooks[4];
extern const u32 wpadbuttonsdown2hooks[4];

int app_gameconfig_load(const char *discid, u8 *tempgameconf, u32 tempgameconfsize)
{
	if (gameconf == NULL)
	{
		gameconf = (u32*) MEM1_lo_alloc(65536);
		if (gameconf == NULL)
			return -1;
	}
	u32 ret;
	s32 gameidmatch, maxgameidmatch = -1, maxgameidmatch2 = -1;
	u32 i, numnonascii, parsebufpos;
	u32 codeaddr, codeval, codeaddr2, codeval2, codeoffset;
	u32 temp, tempoffset = 0;
	char parsebuffer[18];

	// Remove non-ASCII characters
	numnonascii = 0;
	for (i = 0; i < tempgameconfsize; i++)
	{
		if (tempgameconf[i] < 9 || tempgameconf[i] > 126)
			numnonascii++;
		else
			tempgameconf[i - numnonascii] = tempgameconf[i];
	}
	tempgameconfsize -= numnonascii;

	*(tempgameconf + tempgameconfsize) = 0;
	//gameconf = (tempgameconf + tempgameconfsize) + (4 - (((u32) (tempgameconf + tempgameconfsize)) % 4));

	for (maxgameidmatch = 0; maxgameidmatch <= 6; maxgameidmatch++)
	{
		i = 0;
		while (i < tempgameconfsize)
		{
			maxgameidmatch2 = -1;
			while (maxgameidmatch != maxgameidmatch2)
			{
				while (i != tempgameconfsize && tempgameconf[i] != ':')
					i++;
				if (i == tempgameconfsize) break;
				while ((tempgameconf[i] != 10 && tempgameconf[i] != 13) && (i != 0))
					i--;
				if (i != 0) i++;
				parsebufpos = 0;
				gameidmatch = 0;
				while (tempgameconf[i] != ':')
				{
					if (tempgameconf[i] == '?')
					{
						parsebuffer[parsebufpos] = discid[parsebufpos];
						parsebufpos++;
						gameidmatch--;
						i++;
					}
					else if (tempgameconf[i] != 0 && tempgameconf[i] != ' ')
						parsebuffer[parsebufpos++] = tempgameconf[i++];
					else if (tempgameconf[i] == ' ')
						break;
					else i++;
					if (parsebufpos == 8)
						break;
				}
				parsebuffer[parsebufpos] = 0;
				if (strncasecmp("DEFAULT", parsebuffer, strlen(parsebuffer)) == 0 && strlen(parsebuffer) == 7)
				{
					gameidmatch = 0;
					goto idmatch;
				}
				if (strncasecmp(discid, parsebuffer, strlen(parsebuffer)) == 0)
				{
					gameidmatch += strlen(parsebuffer);
					idmatch: if (gameidmatch > maxgameidmatch2)
					{
						maxgameidmatch2 = gameidmatch;
					}
				}
				while ((i != tempgameconfsize) && (tempgameconf[i] != 10 && tempgameconf[i] != 13))
					i++;
			}
			while (i != tempgameconfsize && tempgameconf[i] != ':')
			{
				parsebufpos = 0;
				while ((i != tempgameconfsize) && (tempgameconf[i] != 10 && tempgameconf[i] != 13))
				{
					if (tempgameconf[i] != 0 && tempgameconf[i] != ' ' && tempgameconf[i] != '(' && tempgameconf[i] != ':')
						parsebuffer[parsebufpos++] = tempgameconf[i++];
					else if (tempgameconf[i] == ' ' || tempgameconf[i] == '(' || tempgameconf[i] == ':')
						break;
					else i++;
					if (parsebufpos == 17) 
						break;
				}
				parsebuffer[parsebufpos] = 0;
				//if (!autobootcheck)
				{
					if (strncasecmp("codeliststart", parsebuffer, strlen(parsebuffer)) == 0 && strlen(parsebuffer) == 13)
					{
						sscanf((char *) (tempgameconf + i), " = %x", (unsigned int *) &codelist);
					}
					if (strncasecmp("codelistend", parsebuffer, strlen(parsebuffer)) == 0 && strlen(parsebuffer) == 11)
					{
						sscanf((char *) (tempgameconf + i), " = %x", (unsigned int *) &codelistend);
					}
					if (strncasecmp("poke", parsebuffer, strlen(parsebuffer)) == 0 && strlen(parsebuffer) == 4)
					{
						ret = sscanf((char *) tempgameconf + i, "( %x , %x", &codeaddr, &codeval);
						if (ret == 2)
						{
							*(gameconf + (gameconfsize / 4)) = 0;
							gameconfsize += 4;
							*(gameconf + (gameconfsize / 4)) = 0;
							gameconfsize += 8;
							*(gameconf + (gameconfsize / 4)) = codeaddr;
							gameconfsize += 4;
							*(gameconf + (gameconfsize / 4)) = codeval;
							gameconfsize += 4;
							DCFlushRange((void *) (gameconf + (gameconfsize / 4) - 5), 20);
						}
					}
					if (strncasecmp("pokeifequal", parsebuffer, strlen(parsebuffer)) == 0 && strlen(parsebuffer) == 11)
					{
						ret = sscanf((char *) (tempgameconf + i), "( %x , %x , %x , %x", &codeaddr, &codeval, &codeaddr2, &codeval2);
						if (ret == 4)
						{
							*(gameconf + (gameconfsize / 4)) = 0;
							gameconfsize += 4;
							*(gameconf + (gameconfsize / 4)) = codeaddr;
							gameconfsize += 4;
							*(gameconf + (gameconfsize / 4)) = codeval;
							gameconfsize += 4;
							*(gameconf + (gameconfsize / 4)) = codeaddr2;
							gameconfsize += 4;
							*(gameconf + (gameconfsize / 4)) = codeval2;
							gameconfsize += 4;
							DCFlushRange((void *) (gameconf + (gameconfsize / 4) - 5), 20);
						}
					}
					if (strncasecmp("searchandpoke", parsebuffer, strlen(parsebuffer)) == 0 && strlen(parsebuffer) == 13)
					{
						ret = sscanf((char *) (tempgameconf + i), "( %x%n", &codeval, &tempoffset);
						if (ret == 1)
						{
							gameconfsize += 4;
							temp = 0;
							while (ret == 1)
							{
								*(gameconf + (gameconfsize / 4)) = codeval;
								gameconfsize += 4;
								temp++;
								i += tempoffset;
								ret = sscanf((char *) (tempgameconf + i), " %x%n", &codeval, &tempoffset);
							}
							*(gameconf + (gameconfsize / 4) - temp - 1) = temp;
							ret = sscanf((char *) (tempgameconf + i), " , %x , %x , %x , %x", &codeaddr, &codeaddr2, &codeoffset, &codeval2);
							if (ret == 4)
							{
								*(gameconf + (gameconfsize / 4)) = codeaddr;
								gameconfsize += 4;
								*(gameconf + (gameconfsize / 4)) = codeaddr2;
								gameconfsize += 4;
								*(gameconf + (gameconfsize / 4)) = codeoffset;
								gameconfsize += 4;
								*(gameconf + (gameconfsize / 4)) = codeval2;
								gameconfsize += 4;
								DCFlushRange((void *) (gameconf + (gameconfsize / 4) - temp - 5), temp * 4 + 20);
							}
							else gameconfsize -= temp * 4 + 4;
						}

					}
				}
				if (tempgameconf[i] != ':')
				{
					while ((i != tempgameconfsize) && (tempgameconf[i] != 10 && tempgameconf[i] != 13))
						i++;
					if (i != tempgameconfsize)
						i++;
				}
			}
			if (i != tempgameconfsize)
				while ((tempgameconf[i] != 10 && tempgameconf[i] != 13) && (i != 0))
					i--;
		}
	}
	return 0;
}

u8 *code_buf = NULL;
u32 code_size = 0;

int ocarina_load_code(const u8 *cheat, u32 cheatSize)
{
	if (debuggerselect == 0x00)
		codelist = (u8 *) 0x800022A8;
	else
		codelist = (u8 *) 0x800028B8;
	codelistend = (u8 *) 0x80003000;

	/* RetroAchievements VBlank sync hook — a Gecko C0 code (ASM executed by
	 * the codehandler on every VBI). Increments the u32 frame counter at
	 * physical 0x2FF8; the d2x ra-module (Starlet) polls that address to
	 * fire its memory SNAPSHOT aligned with the game's vertical retrace.
	 *
	 * The store goes through the UNCACHED MEM1 mirror (0xC0002FF8), NOT the
	 * cached 0x80002FF8. A cached store would sit in Broadway's L1 until
	 * eviction, so the Starlet would see the counter frozen or advancing in
	 * bursts — useless for per-frame edge detection. Uncached stores reach
	 * MEM1 immediately.
	 *
	 *   C0000000 00000003   (C0 = execute ASM, 3 lines of 8 bytes follow)
	 *   3D60C000 818B2FF8   lis r11,0xC000 ; lwz r12,0x2FF8(r11)
	 *   398C0001 918B2FF8   addi r12,r12,1 ; stw r12,0x2FF8(r11)
	 *   4E800020 00000000   blr            ; (pad)
	 */
	const u8 ra_vblank_hook[] = {
		0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
		0x3D, 0x60, 0xC0, 0x00, 0x81, 0x8B, 0x2F, 0xF8,
		0x39, 0x8C, 0x00, 0x01, 0x91, 0x8B, 0x2F, 0xF8,
		0x4E, 0x80, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00
	};
	u32 ra_hook_size = sizeof(ra_vblank_hook);

	/* The counter lives at the tail of the codelist region. Cap the total
	 * GCT size so the copied codes can never grow into it (0x2FF8..0x2FFF
	 * reserved). If a user's cheat pack is too big to fit alongside the
	 * hook, keep the cheats and skip the hook — ra-module falls back to
	 * timer pacing automatically when the counter never moves. */
	u32 code_cap = (u32)0x80002FF8 - (u32)codelist;
	int inject_hook = 1;
	if (cheatSize + ra_hook_size > code_cap) {
		gprintf("Ocarina: GCT too big for RA hook (%u + %u > %u), skipping hook.\n",
		        cheatSize, ra_hook_size, code_cap);
		inject_hook = 0;
		if (cheatSize > code_cap) {
			gprintf("Ocarina: GCT alone exceeds codelist space, no codes loaded!\n");
			code_buf = NULL;
			code_size = 0;
			return 0;
		}
	}

	code_size = cheatSize + (inject_hook ? ra_hook_size : 0);
	if (cheatSize == 0 || cheat == NULL)
		code_size = 8 + ra_hook_size + 8;  /* standalone GCT: header + hook + footer */

	code_buf = (u8*)MEM1_lo_alloc(code_size);
	if(code_buf == NULL)
	{
		gprintf("Ocarina: Couldnt allocate buffer!\n");
		code_size = 0;
		return 0;
	}

	if (cheatSize > 0 && cheat != NULL) {
		if (inject_hook && cheatSize >= 8
		    && cheat[cheatSize-8] == 0xF0 && cheat[cheatSize-7] == 0x00) {
			/* Inject our RA hook just before the GCT footer (F0000000 00000000). */
			memcpy(code_buf, cheat, cheatSize - 8);
			memcpy(code_buf + cheatSize - 8, ra_vblank_hook, ra_hook_size);
			memcpy(code_buf + cheatSize - 8 + ra_hook_size, cheat + cheatSize - 8, 8);
		} else if (inject_hook) {
			memcpy(code_buf, cheat, cheatSize);
			memcpy(code_buf + cheatSize, ra_vblank_hook, ra_hook_size);
		} else {
			memcpy(code_buf, cheat, cheatSize);
		}
	} else {
		/* No user cheats — build a minimal valid GCT around the hook. */
		const u8 gct_header[] = { 0x00, 0xD0, 0xC0, 0xDE, 0x00, 0xD0, 0xC0, 0xDE };
		const u8 gct_footer[] = { 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		memcpy(code_buf, gct_header, 8);
		memcpy(code_buf + 8, ra_vblank_hook, ra_hook_size);
		memcpy(code_buf + 8 + ra_hook_size, gct_footer, 8);
	}

	gprintf("Ocarina: codes loaded%s.\n", inject_hook ? " + RA VBlank hook" : "");
	DCFlushRange(code_buf, code_size);
	return code_size;
}
