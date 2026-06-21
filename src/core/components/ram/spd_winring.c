/* spd_winring.c
   Read DRAM chip manufacturer from SPD via Intel PCH SMBus (WinRing0).

   Protocol:
     DDR3: DRAM mfr at SPD page 0 bytes 0x8C-0x8D (140-141).
     DDR4: DRAM mfr at SPD page 1 bytes 0x5E-0x5F (physical 350-351).
           Page 1 selected by Write Quick to I2C address 0x37;
           page 0 restored with Write Quick to 0x36.

   Falls back to "" when:
     - WinRing0 not loaded (user-mode / no driver)
     - Non-Intel CPU
     - SMBus I/O BAR not found
     - BIOS has SMBus host-software lock set (common on modern laptops)      */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../winring/winring0.h"
#include <string.h>
#include <stdio.h>

/* ── SMBus I/O port register offsets (from I/O base) ────────────────────── */
#define SMB_HST_STS    0x00
#define SMB_HST_CNT    0x02
#define SMB_HST_CMD    0x03
#define SMB_XMIT_SLVA  0x04
#define SMB_HST_DAT0   0x05

/* HST_CNT command field (bits [4:2]) */
#define SMB_CMD_QUICK      0x00   /* 000 — Quick Command               */
#define SMB_CMD_BYTE_DATA  0x08   /* 010 — Read/Write Byte Data        */
#define SMB_START          0x40   /* bit 6 — start transaction         */

/* HST_STS bits */
#define SMB_STS_BUSY      0x01
#define SMB_STS_INTR      0x02
#define SMB_STS_DEV_ERR   0x04
#define SMB_STS_BUS_ERR   0x08
#define SMB_STS_FAILED    0x10
#define SMB_STS_ERRORS    (SMB_STS_DEV_ERR | SMB_STS_BUS_ERR | SMB_STS_FAILED)

/* ── Module state ────────────────────────────────────────────────────────── */
static WORD g_smb_base = 0;

/* ── SMBus I/O helpers ───────────────────────────────────────────────────── */
static BYTE smb_in(BYTE reg)           { return g_rdio((WORD)(g_smb_base + reg)); }
static void smb_out(BYTE reg, BYTE v)  { g_wrio((WORD)(g_smb_base + reg), v); }

/* Returns 0 on success, -1 on timeout or error. */
static int smb_wait_idle(void) {
    for (int i = 0; i < 200; i++) {
        BYTE st = smb_in(SMB_HST_STS);
        if (st & SMB_STS_ERRORS) { smb_out(SMB_HST_STS, 0xFF); return -1; }
        if (!(st & SMB_STS_BUSY)) return 0;
        Sleep(1);
    }
    return -1;
}

static int smb_wait_done(void) {
    for (int i = 0; i < 100; i++) {
        Sleep(1);
        BYTE st = smb_in(SMB_HST_STS);
        if (st & SMB_STS_ERRORS) { smb_out(SMB_HST_STS, 0xFF); return -1; }
        if (st & SMB_STS_INTR)   { smb_out(SMB_HST_STS, 0xFF); return 0;  }
    }
    smb_out(SMB_HST_STS, 0xFF);
    return -1;
}

/* Read one byte from I2C address `addr` at offset `reg`. */
static int smb_read_byte(BYTE addr, BYTE reg, BYTE *out) {
    if (smb_wait_idle() < 0) return 0;
    smb_out(SMB_HST_STS,   0xFF);
    smb_out(SMB_XMIT_SLVA, (BYTE)((addr << 1) | 1));
    smb_out(SMB_HST_CMD,   reg);
    smb_out(SMB_HST_CNT,   SMB_START | SMB_CMD_BYTE_DATA);
    if (smb_wait_done() < 0) return 0;
    *out = smb_in(SMB_HST_DAT0);
    return 1;
}

/* Send Write Quick (0-byte write) to `addr` — used for DDR4 page select.
   0x36 → page 0,  0x37 → page 1.                                           */
static void smb_write_quick(BYTE addr) {
    if (smb_wait_idle() < 0) return;
    smb_out(SMB_HST_STS,   0xFF);
    smb_out(SMB_XMIT_SLVA, (BYTE)(addr << 1));   /* write direction */
    smb_out(SMB_HST_CNT,   SMB_START | SMB_CMD_QUICK);
    smb_wait_done();
}

/* ── Intel PCH SMBus detection ───────────────────────────────────────────── */
static WORD find_intel_smb_base(void) {
    if (!g_rdpci) return 0;

    /* PCH SMBus: D31:F4 (Skylake+) or D31:F3 (older ICH).
       On Tiger Lake-H and newer mobile platforms, CF8/CFC legacy PCI config
       access to PCH devices is blocked by firmware — reads return failure.
       In that case this function returns 0 and SPD reading is skipped.       */
    static const BYTE funcs[] = {4, 3};
    for (int i = 0; i < 2; i++) {
        DWORD vid_did = 0;
        if (!g_rdpci(0, 31, funcs[i], 0x00, &vid_did)) continue;
        if ((vid_did & 0xFFFF) != 0x8086)              continue;

        DWORD bar = 0;
        if (!g_rdpci(0, 31, funcs[i], 0x20, &bar)) continue;
        if (!(bar & 1))                             continue;   /* not I/O-mapped */
        WORD base = (WORD)(bar & 0xFFE0);
        if (base < 0x100 || base == 0xFFE0)         continue;

        return base;
    }
    return 0;
}

/* ── JEDEC manufacturer lookup ───────────────────────────────────────────── */
typedef struct { BYTE bank; BYTE id; const char *name; } JedecEntry;

static const JedecEntry g_jedec[] = {
    /* Bank 0 (no continuation codes) */
    {0, 0xCE, "Samsung"},
    {0, 0xAD, "SK Hynix"},
    {0, 0x2C, "Micron"},
    {0, 0x04, "NEC"},
    {0, 0x07, "Hitachi"},
    {0, 0x89, "Intel"},
    {0, 0x01, "AMD"},
    /* Bank 1 (one continuation code 0x7F) */
    {1, 0x9E, "Kingston"},
    {1, 0x43, "Ramaxel"},
    {1, 0xF1, "G.Skill"},
};

static const char *jedec_name(BYTE bank_byte, BYTE id) {
    BYTE bank = bank_byte & 0x7F;   /* strip odd-parity bit */
    for (int i = 0; i < (int)(sizeof(g_jedec) / sizeof(g_jedec[0])); i++)
        if (g_jedec[i].bank == bank && g_jedec[i].id == id)
            return g_jedec[i].name;
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────────── */
static char g_chip_mfr[32] = "";

const char *spd_get_dram_manufacturer(void) {
    if (g_chip_mfr[0]) return g_chip_mfr;
    if (!g_ready || !g_rdio || !g_wrio) return g_chip_mfr;
    if (g_cpu_vendor != CPU_VENDOR_INTEL) return g_chip_mfr;

    g_smb_base = find_intel_smb_base();
    if (!g_smb_base) return g_chip_mfr;

    for (BYTE slot = 0; slot < 8; slot++) {
        BYTE addr = (BYTE)(0x50 + slot);
        BYTE ddr_type = 0;

        if (!smb_read_byte(addr, 0x02, &ddr_type)) continue;
        if (ddr_type == 0 || ddr_type == 0xFF)     continue;

        printf("[spd] slot %d: DRAM type byte=0x%02X\n", slot, ddr_type);

        BYTE bank = 0, id = 0;
        int  ok   = 0;

        if (ddr_type == 0x0B) {
            /* DDR3: DRAM mfr at page 0 offsets 0x8C-0x8D */
            ok = smb_read_byte(addr, 0x8C, &bank) &&
                 smb_read_byte(addr, 0x8D, &id);
        } else if (ddr_type == 0x0C || ddr_type == 0x0D) {
            /* DDR4: DRAM mfr at page 1 offsets 0x5E-0x5F (physical 350-351) */
            smb_write_quick(0x37);
            ok = smb_read_byte(addr, 0x5E, &bank) &&
                 smb_read_byte(addr, 0x5F, &id);
            smb_write_quick(0x36);   /* restore page 0 */
        } else {
            continue;   /* DDR5 or unknown — not supported */
        }

        if (!ok) continue;

        const char *name = jedec_name(bank, id);
        if (name)
            snprintf(g_chip_mfr, sizeof(g_chip_mfr), "%s", name);
        else
            snprintf(g_chip_mfr, sizeof(g_chip_mfr), "0x%02X%02X", bank, id);

        printf("[spd] slot %d: DRAM chip mfr=%s (bank=0x%02X id=0x%02X)\n",
               slot, g_chip_mfr, bank, id);
        break;
    }
    return g_chip_mfr;
}
