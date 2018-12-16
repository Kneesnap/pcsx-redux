/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

/*
 * PSX memory functions.
 */

#ifndef _WIN32
#include <sys/mman.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "core/psxhw.h"
#include "core/psxmem.h"
#include "core/r3000a.h"

#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

int8_t *g_psxM = NULL;  // Kernel & User Memory (2 Meg)
int8_t *g_psxP = NULL;  // Parallel Port (64K)
int8_t *g_psxR = NULL;  // BIOS ROM (512K)
int8_t *g_psxH = NULL;  // Scratch Pad (1K) & Hardware Registers (8K)

uint8_t **g_psxMemWLUT = NULL;
uint8_t **g_psxMemRLUT = NULL;

/*  Playstation Memory Map (from Playstation doc by Joshua Walker)
0x0000_0000-0x0000_ffff		Kernel (64K)
0x0001_0000-0x001f_ffff		User Memory (1.9 Meg)

0x1f00_0000-0x1f00_ffff		Parallel Port (64K)

0x1f80_0000-0x1f80_03ff		Scratch Pad (1024 bytes)

0x1f80_1000-0x1f80_2fff		Hardware Registers (8K)

0x1fc0_0000-0x1fc7_ffff		BIOS (512K)

0x8000_0000-0x801f_ffff		Kernel and User Memory Mirror (2 Meg) Cached
0x9fc0_0000-0x9fc7_ffff		BIOS Mirror (512K) Cached

0xa000_0000-0xa01f_ffff		Kernel and User Memory Mirror (2 Meg) Uncached
0xbfc0_0000-0xbfc7_ffff		BIOS Mirror (512K) Uncached
*/

int psxMemInit() {
    int i;

    g_psxMemRLUT = (uint8_t **)malloc(0x10000 * sizeof(void *));
    g_psxMemWLUT = (uint8_t **)malloc(0x10000 * sizeof(void *));
    memset(g_psxMemRLUT, 0, 0x10000 * sizeof(void *));
    memset(g_psxMemWLUT, 0, 0x10000 * sizeof(void *));

#ifndef _WIN32
    g_psxM = mmap(0, 0x00220000, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#else
    g_psxM = ((int8_t *)VirtualAlloc(NULL, 0x00220000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#endif

    g_psxP = &g_psxM[0x200000];
    g_psxH = &g_psxM[0x210000];

    g_psxR = (int8_t *)malloc(0x00080000);

    if (g_psxMemRLUT == NULL || g_psxMemWLUT == NULL || g_psxM == NULL || g_psxP == NULL || g_psxH == NULL) {
        PCSX::system->SysMessage("%s", _("Error allocating memory!"));
        return -1;
    }

    // MemR
    for (i = 0; i < 0x80; i++) g_psxMemRLUT[i + 0x0000] = (uint8_t *)&g_psxM[(i & 0x1f) << 16];

    memcpy(g_psxMemRLUT + 0x8000, g_psxMemRLUT, 0x80 * sizeof(void *));
    memcpy(g_psxMemRLUT + 0xa000, g_psxMemRLUT, 0x80 * sizeof(void *));

    g_psxMemRLUT[0x1f00] = (uint8_t *)g_psxP;
    g_psxMemRLUT[0x1f80] = (uint8_t *)g_psxH;

    for (i = 0; i < 0x08; i++) g_psxMemRLUT[i + 0x1fc0] = (uint8_t *)&g_psxR[i << 16];

    memcpy(g_psxMemRLUT + 0x9fc0, g_psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));
    memcpy(g_psxMemRLUT + 0xbfc0, g_psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));

    // MemW
    for (i = 0; i < 0x80; i++) g_psxMemWLUT[i + 0x0000] = (uint8_t *)&g_psxM[(i & 0x1f) << 16];

    memcpy(g_psxMemWLUT + 0x8000, g_psxMemWLUT, 0x80 * sizeof(void *));
    memcpy(g_psxMemWLUT + 0xa000, g_psxMemWLUT, 0x80 * sizeof(void *));

    g_psxMemWLUT[0x1f00] = (uint8_t *)g_psxP;
    g_psxMemWLUT[0x1f80] = (uint8_t *)g_psxH;

    return 0;
}

void psxMemReset() {
    FILE *f = NULL;
    char bios[1024] = {'\0'};

    memset(g_psxM, 0, 0x00200000);
    memset(g_psxP, 0, 0x00010000);

    // Load BIOS
    if (strcmp(g_config.Bios, "HLE") != 0) {
        // AppPath's priority is high.
        const char *apppath = GetAppPath();
        if (strlen(apppath) > 0)
            strcat(strcat(strcat(bios, GetAppPath()), "bios\\"), g_config.Bios);
        else
            sprintf(bios, "%s/%s", g_config.BiosDir, g_config.Bios);

        f = fopen(bios, "rb");
        if (f == NULL) {
            PCSX::system->SysMessage(_("Could not open BIOS:\"%s\". Enabling HLE Bios!\n"), bios);
            memset(g_psxR, 0, 0x80000);
            g_config.HLE = true;
        } else {
            fread(g_psxR, 1, 0x80000, f);
            fclose(f);
            g_config.HLE = false;
            PCSX::system->SysPrintf(_("Loaded BIOS: %s\n"), bios);
        }
    } else
        g_config.HLE = true;
}

void psxMemShutdown() {
#ifndef _WIN32
    munmap(g_psxM, 0x00220000);
#else
    VirtualFree(g_psxM, 0x00220000, MEM_RELEASE);
#endif

    free(g_psxR);
    free(g_psxMemRLUT);
    free(g_psxMemWLUT);
}

static int writeok = 1;

uint8_t psxMemRead8(uint32_t mem) {
    char *p;
    uint32_t t;

    if (!g_config.MemHack) {
        g_psxRegs.cycle += 0;
    }

    t = mem >> 16;
    if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
        if ((mem & 0xffff) < 0x400)
            return psxHu8(mem);
        else
            return psxHwRead8(mem);
    } else {
        p = (char *)(g_psxMemRLUT[t]);
        if (p != NULL) {
            if (g_config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, BR1);
            return *(uint8_t *)(p + (mem & 0xffff));
        } else {
#ifdef PSXMEM_LOG
            PSXMEM_LOG("err lb %8.8lx\n", mem);
#endif
            return 0;
        }
    }
}

uint16_t psxMemRead16(uint32_t mem) {
    char *p;
    uint32_t t;

    if (!g_config.MemHack) {
        g_psxRegs.cycle += 1;
    }

    t = mem >> 16;
    if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
        if ((mem & 0xffff) < 0x400)
            return psxHu16(mem);
        else
            return psxHwRead16(mem);
    } else {
        p = (char *)(g_psxMemRLUT[t]);
        if (p != NULL) {
            if (g_config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, BR2);
            return SWAPu16(*(uint16_t *)(p + (mem & 0xffff)));
        } else {
#ifdef PSXMEM_LOG
            PSXMEM_LOG("err lh %8.8lx\n", mem);
#endif
            return 0;
        }
    }
}

uint32_t psxMemRead32(uint32_t mem) {
    char *p;
    uint32_t t;

    if (!g_config.MemHack) {
        g_psxRegs.cycle += 1;
    }

    t = mem >> 16;
    if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
        if ((mem & 0xffff) < 0x400)
            return psxHu32(mem);
        else
            return psxHwRead32(mem);
    } else {
        p = (char *)(g_psxMemRLUT[t]);
        if (p != NULL) {
            if (g_config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, BR4);
            return SWAPu32(*(uint32_t *)(p + (mem & 0xffff)));
        } else {
#ifdef PSXMEM_LOG
            if (writeok) {
                PSXMEM_LOG("err lw %8.8lx\n", mem);
            }
#endif
            return 0;
        }
    }
}

void psxMemWrite8(uint32_t mem, uint8_t value) {
    char *p;
    uint32_t t;

    if (!g_config.MemHack) {
        g_psxRegs.cycle += 1;
    }

    t = mem >> 16;
    if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
        if ((mem & 0xffff) < 0x400)
            psxHu8(mem) = value;
        else
            psxHwWrite8(mem, value);
    } else {
        p = (char *)(g_psxMemWLUT[t]);
        if (p != NULL) {
            if (g_config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, BW1);
            *(uint8_t *)(p + (mem & 0xffff)) = value;
#ifdef PSXREC
            g_psxCpu->Clear((mem & (~3)), 1);
#endif
        } else {
#ifdef PSXMEM_LOG
            PSXMEM_LOG("err sb %8.8lx\n", mem);
#endif
        }
    }
}

void psxMemWrite16(uint32_t mem, uint16_t value) {
    char *p;
    uint32_t t;

    if (!g_config.MemHack) {
        g_psxRegs.cycle += 1;
    }

    t = mem >> 16;
    if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
        if ((mem & 0xffff) < 0x400)
            psxHu16ref(mem) = SWAPu16(value);
        else
            psxHwWrite16(mem, value);
    } else {
        p = (char *)(g_psxMemWLUT[t]);
        if (p != NULL) {
            if (g_config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, BW2);
            *(uint16_t *)(p + (mem & 0xffff)) = SWAPu16(value);
#ifdef PSXREC
            g_psxCpu->Clear((mem & (~3)), 1);
#endif
        } else {
#ifdef PSXMEM_LOG
            PSXMEM_LOG("err sh %8.8lx\n", mem);
#endif
        }
    }
}

void psxMemWrite32(uint32_t mem, uint32_t value) {
    char *p;
    uint32_t t;

    if (!g_config.MemHack) {
        g_psxRegs.cycle += 1;
    }

    //	if ((mem&0x1fffff) == 0x71E18 || value == 0x48088800) PCSX::system->SysPrintf("t2fix!!\n");
    t = mem >> 16;
    if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
        if ((mem & 0xffff) < 0x400)
            psxHu32ref(mem) = SWAPu32(value);
        else
            psxHwWrite32(mem, value);
    } else {
        p = (char *)(g_psxMemWLUT[t]);
        if (p != NULL) {
            if (g_config.Debug) DebugCheckBP((mem & 0xffffff) | 0x80000000, BW4);
            *(uint32_t *)(p + (mem & 0xffff)) = SWAPu32(value);
#ifdef PSXREC
            g_psxCpu->Clear(mem, 1);
#endif
        } else {
            if (mem != 0xfffe0130) {
#ifdef PSXREC
                if (!writeok) g_psxCpu->Clear(mem, 1);
#endif

#ifdef PSXMEM_LOG
                if (writeok) {
                    PSXMEM_LOG("err sw %8.8lx\n", mem);
                }
#endif
            } else {
                int i;

                // a0-44: used for cache flushing
                switch (value) {
                    case 0x800:
                    case 0x804:
                        if (writeok == 0) break;
                        writeok = 0;
                        memset(g_psxMemWLUT + 0x0000, 0, 0x80 * sizeof(void *));
                        memset(g_psxMemWLUT + 0x8000, 0, 0x80 * sizeof(void *));
                        memset(g_psxMemWLUT + 0xa000, 0, 0x80 * sizeof(void *));

                        g_psxRegs.ICache_valid = false;
                        break;
                    case 0x00:
                    case 0x1e988:
                        if (writeok == 1) break;
                        writeok = 1;
                        for (i = 0; i < 0x80; i++) g_psxMemWLUT[i + 0x0000] = (uint8_t *)&g_psxM[(i & 0x1f) << 16];
                        memcpy(g_psxMemWLUT + 0x8000, g_psxMemWLUT, 0x80 * sizeof(void *));
                        memcpy(g_psxMemWLUT + 0xa000, g_psxMemWLUT, 0x80 * sizeof(void *));
                        break;
                    default:
#ifdef PSXMEM_LOG
                        PSXMEM_LOG("unk %8.8lx = %x\n", mem, value);
#endif
                        break;
                }
            }
        }
    }
}

void *psxMemPointer(uint32_t mem) {
    char *p;
    uint32_t t;

    t = mem >> 16;
    if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
        if ((mem & 0xffff) < 0x400)
            return (void *)&g_psxH[mem];
        else
            return NULL;
    } else {
        p = (char *)(g_psxMemWLUT[t]);
        if (p != NULL) {
            return (void *)(p + (mem & 0xffff));
        }
        return NULL;
    }
}
