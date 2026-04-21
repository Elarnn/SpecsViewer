// ram_winring.c
// Memory channel count detection via WinRing0.
//
// Intel Sandy Bridge through Comet Lake (2nd–10th gen Core):
//   1. Read MCHBAR base from PCI host bridge B0:D0:F0 config offset 0x48.
//      Bit 0 = enable flag; base = value & 0xFFFF8000 (32 KB aligned).
//   2. Validate MCHBAR MMIO by reading MAD_INTER_CHANNEL at base+0x5000.
//      0xFFFFFFFF → bus error; 0x00000000 → wrong offsets for this gen.
//   3. Read base+0x5004 (MAD_DIMM_CH0) and base+0x5008 (MAD_DIMM_CH1).
//      Non-zero ⇒ channel has DIMMs populated.
//   4. Count non-zero channels → 1 (single) or 2 (dual).
//
// If ReadMemory is not exported by the user's WinRing0 build, or this is
// a non-Intel CPU, returns 0 — see fallback chain in backend.c.

#include "../../winring/winring0.h"
#include <stdio.h>

/* ── MCHBAR PCI locator ──────────────────────────────────────────────────── */
#define MCHBAR_BUS    0
#define MCHBAR_DEV    0
#define MCHBAR_FUNC   0
/* Offset 0x48: bits [31:15] = base addr, bit 0 = enable (32 KB aligned). */
#define MCHBAR_REG    0x48u
#define MCHBAR_REG_HI 0x4Cu   /* high half, usually 0 on consumer desktops   */

/* ── MAD offsets inside MCHBAR (Intel gen 2–10, SNB through CML) ────────── */
#define MAD_INTER_CHANNEL  0x5000u  /* channel interleave config              */
#define MAD_DIMM_CH0       0x5004u  /* channel 0 DIMM geometry; 0 = empty     */
#define MAD_DIMM_CH1       0x5008u  /* channel 1 DIMM geometry; 0 = empty     */

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static int rdpci_dw(BYTE reg, DWORD *out) {
  return (int)g_rdpci(MCHBAR_BUS, MCHBAR_DEV, MCHBAR_FUNC, reg, out);
}

static int rdmem32(DWORD64 addr, DWORD *out) {
  return (int)g_rdmem((DWORD_PTR)addr, (PBYTE)out, sizeof(DWORD));
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int get_ram_channel_count_wr0(void) {
  if (!g_ready || !g_rdpci) return 0;
  if (g_cpu_vendor != CPU_VENDOR_INTEL) return 0;

  if (!g_rdmem) {
    printf("[wr0-ram] ReadMemory not available — falling back to SMBIOS\n");
    return 0;
  }

  /* Step 1 – read MCHBAR base address from PCI config */
  DWORD lo = 0, hi = 0;
  if (!rdpci_dw((BYTE)MCHBAR_REG, &lo)) {
    printf("[wr0-ram] PCI read MCHBAR_LO failed\n");
    return 0;
  }
  printf("[wr0-ram] MCHBAR_LO=0x%08lX\n", lo);

  if (!(lo & 1u)) {
    printf("[wr0-ram] MCHBAR enable bit is clear\n");
    return 0;
  }

  rdpci_dw((BYTE)MCHBAR_REG_HI, &hi);

  DWORD64 base = ((DWORD64)hi << 32) | (DWORD64)(lo & 0xFFFF8000u);
  printf("[wr0-ram] MCHBAR base=0x%016llX\n", (unsigned long long)base);

  /* Step 2 – validate MMIO: read MAD_INTER_CHANNEL */
  DWORD inter = 0;
  if (!rdmem32(base + MAD_INTER_CHANNEL, &inter)) {
    printf("[wr0-ram] ReadMemory failed at MCHBAR+0x5000\n");
    return 0;
  }
  printf("[wr0-ram] MAD_INTER_CHANNEL=0x%08lX\n", inter);

  if (inter == 0xFFFFFFFFu) {
    printf("[wr0-ram] Bus error reading MCHBAR+0x5000 (wrong base?)\n");
    return 0;
  }

  /* Step 3 – read per-channel DIMM population registers */
  DWORD ch0 = 0, ch1 = 0;
  rdmem32(base + MAD_DIMM_CH0, &ch0);
  rdmem32(base + MAD_DIMM_CH1, &ch1);
  printf("[wr0-ram] MAD_DIMM_CH0=0x%08lX  MAD_DIMM_CH1=0x%08lX\n", ch0, ch1);

  int populated = (ch0 != 0 ? 1 : 0) + (ch1 != 0 ? 1 : 0);
  printf("[wr0-ram] channels with DIMMs: %d\n", populated);

  return (populated >= 1) ? populated : 0;
}
