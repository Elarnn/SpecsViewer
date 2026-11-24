// cpu_winring0.c
// CPU temperature and core voltage (VID) via WinRing0 MSR / PCI config access.
//
// Temperature
//   Intel Nehalem+ (Core i-series, 2008+):
//     - TjMax from IA32_TEMPERATURE_TARGET (0x1A2) bits [23:16]
//     - Core temp from IA32_THERM_STATUS   (0x19C) bits [22:16], bit 31 = valid
//   AMD Family 15h/16h (Bulldozer–Carrizo, ~2012–2016):
//     - D18F3xA4 PCI config register, CurTmp bits [31:21], unit 0.125 °C
//   AMD Family 17h+ (Zen / Ryzen, 2017+):
//     - Tctl via SMN register 0x00059800, accessed through PCI host-bridge
//     - Bits [31:21], unit 0.125 °C; bit 19-18 == 3 → subtract 49 °C offset
//
// Core voltage (VID — the voltage the CPU requests from the VRM; this is the
// value CPU-Z / HWiNFO report as "Core VID" and tracks the BIOS Vcore setting)
//   Intel Sandy Bridge+:
//     - IA32_PERF_STATUS (0x198) bits [47:32], unit 1/8192 V
//   AMD Family 17h+ (Zen):
//     - current P-state from MSR 0xC0010063 bits [2:0]
//     - CpuVid from P-state MSR 0xC0010064+P bits [21:14]
//       SVI2 (Zen–Zen3): V = 1.55  − VID × 0.00625
//       SVI3 (Zen4+):    V = 0.245 + VID × 0.005

#include "../../winring/winring0.h"

/* ── Intel path ─────────────────────────────────────────────────────────── */

static int intel_read_temp(void) {
  uint64_t v = 0;
  if (!wr0_rdmsr64(MSR_THERM_STATUS, &v))
    return -1;

  if (!((v >> 31) & 1u))   /* bit 31: reading valid */
    return -1;

  int distance = (int)((v >> 16) & 0x7F);
  int temp = g_tjmax - distance;
  return (temp >= 0 && temp <= 125) ? temp : -1;
}

static double intel_read_vid(void) {
  uint64_t v = 0;
  if (!wr0_rdmsr64(MSR_PERF_STATUS, &v))
    return 0.0;

  unsigned raw = (unsigned)((v >> 32) & 0xFFFFu);
  if (!raw)
    return 0.0;

  double volts = (double)raw / 8192.0;
  return (volts > 0.3 && volts < 2.0) ? volts : 0.0;
}

/* ── AMD Family 15h/16h path (PCI config D18F3xA4) ─────────────────────── */

static int amd_legacy_read_temp(void) {
  if (!g_rdpci) return -1;

  DWORD val = 0;
  if (!g_rdpci(AMD_THERM_BUS, AMD_THERM_DEV, AMD_THERM_FUNC,
               AMD_THERM_REG, &val))
    return -1;

  /* CurTmp: bits [31:21], 11-bit value, unit = 0.125 °C */
  int raw  = (int)((val >> 21) & 0x7FF);
  int temp = raw / 8;   /* integer °C, rounds down */
  return (temp >= 0 && temp <= 150) ? temp : -1;
}

/* ── AMD Family 17h+ path (Zen, via SMN over PCI host-bridge) ───────────── */

static int amd_zen_read_temp(void) {
  if (!g_rdpci || !g_wrpci) return -1;

  /* Write SMN address into index register */
  if (!g_wrpci(AMD_SMN_BUS, AMD_SMN_DEV, AMD_SMN_FUNC,
               (BYTE)AMD_SMN_INDEX_REG, AMD_SMN_THM_CUR_TMP))
    return -1;

  /* Read result from data register */
  DWORD val = 0;
  if (!g_rdpci(AMD_SMN_BUS, AMD_SMN_DEV, AMD_SMN_FUNC,
               (BYTE)AMD_SMN_DATA_REG, &val))
    return -1;

  /* Bits [31:21]: Tctl in units of 0.125 °C.
     Bits [19:18] == 3 → Tctl includes a +49 °C offset over Tdie
     (applies to some Threadripper / EPYC SKUs). */
  int raw    = (int)((val >> 21) & 0x7FF);
  int offset = (((val >> 18) & 0x3) == 3) ? 49 : 0;
  int temp   = raw / 8 - offset;
  return (temp >= 0 && temp <= 150) ? temp : -1;
}

static double amd_zen_read_vid(void) {
  uint64_t st = 0;
  if (!wr0_rdmsr64(MSR_AMD_PSTATE_STATUS, &st))
    return 0.0;
  unsigned p = (unsigned)(st & 0x7u);

  uint64_t ps = 0;
  if (!wr0_rdmsr64(MSR_AMD_PSTATE_0 + p, &ps))
    return 0.0;
  if (!((ps >> 63) & 1u))   /* PstateEn */
    return 0.0;

  unsigned vid  = (unsigned)((ps >> 14) & 0xFFu);
  double volts  = g_cpu_svi3 ? (0.245 + 0.005   * (double)vid)
                             : (1.55  - 0.00625 * (double)vid);
  return (volts > 0.3 && volts < 2.0) ? volts : 0.0;
}

/* ── Public entry points ────────────────────────────────────────────────── */

int cpu_wr0_read_temp(void) {
  if (!g_ready) return -1;

  switch (g_cpu_vendor) {
    case CPU_VENDOR_INTEL:
      return intel_read_temp();

    case CPU_VENDOR_AMD:
      if (g_cpu_family >= 0x17)        /* Zen and newer */
        return amd_zen_read_temp();
      else if (g_cpu_family >= 0x15)   /* Bulldozer–Carrizo */
        return amd_legacy_read_temp();
      return -1;

    default:
      return -1;
  }
}

double cpu_wr0_read_voltage(void) {
  if (!g_ready) return 0.0;

  switch (g_cpu_vendor) {
    case CPU_VENDOR_INTEL:
      /* Family 6, Sandy Bridge (model 0x2A) and newer */
      if (g_cpu_family == 0x06 && g_cpu_model >= 0x2A)
        return intel_read_vid();
      return 0.0;

    case CPU_VENDOR_AMD:
      if (g_cpu_family >= 0x17)        /* Zen and newer */
        return amd_zen_read_vid();
      return 0.0;

    default:
      return 0.0;
  }
}
