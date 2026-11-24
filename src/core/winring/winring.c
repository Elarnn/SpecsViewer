#include "winring0.h"
#include <stdio.h>
#include <string.h>
#include <intrin.h>

/* ── WinRing0 export signatures (internal only) ──────────────────────────── */
typedef BOOL (__stdcall *t_InitializeOls)(void);
typedef VOID (__stdcall *t_DeinitializeOls)(void);
typedef BOOL (__stdcall *t_IsMsr)(void);

/* ── Module state ────────────────────────────────────────────────────────── */
static HMODULE           g_dll    = NULL;
static t_InitializeOls   g_init   = NULL;
static t_DeinitializeOls g_deinit = NULL;

int     g_ready      = 0;
int     g_tjmax      = 100;
int     g_cpu_vendor = CPU_VENDOR_UNKNOWN;
int     g_cpu_family = 0;
int     g_cpu_model  = 0;
int     g_cpu_svi3   = 0;
t_Rdmsr              g_rdmsr = NULL;
t_ReadPciConfigDword  g_rdpci = NULL;
t_WritePciConfigDword g_wrpci = NULL;
t_ReadMemory          g_rdmem = NULL;
t_ReadIoPortByte      g_rdio  = NULL;
t_WriteIoPortByte     g_wrio  = NULL;

/* ── Internal helpers ────────────────────────────────────────────────────── */

int wr0_rdmsr64(DWORD msr, uint64_t *out) {
  DWORD eax = 0, edx = 0;
  if (!g_rdmsr(msr, &eax, &edx)) return 0;
  *out = ((uint64_t)edx << 32) | (uint64_t)eax;
  return 1;
}

static int read_tjmax(void) {
  uint64_t v = 0;
  if (!wr0_rdmsr64(MSR_TEMPERATURE_TARGET, &v)) return 100;
  int tj = (int)((v >> 16) & 0xFF);
  return (tj >= 60 && tj <= 120) ? tj : 100;
}

/* Detect CPU vendor, full family and full model via CPUID. */
static void detect_cpu(void) {
  int regs[4] = {0};

  /* Vendor string from leaf 0 */
  __cpuid(regs, 0);
  char vendor[13];
  *(int*)&vendor[0] = regs[1]; /* EBX */
  *(int*)&vendor[4] = regs[3]; /* EDX */
  *(int*)&vendor[8] = regs[2]; /* ECX */
  vendor[12] = '\0';

  if      (strcmp(vendor, "GenuineIntel") == 0) g_cpu_vendor = CPU_VENDOR_INTEL;
  else if (strcmp(vendor, "AuthenticAMD") == 0) g_cpu_vendor = CPU_VENDOR_AMD;
  else                                          g_cpu_vendor = CPU_VENDOR_UNKNOWN;

  /* Family / model from leaf 1, EAX */
  __cpuid(regs, 1);
  int base_family = (regs[0] >> 8)  & 0xF;
  int ext_family  = (regs[0] >> 20) & 0xFF;
  int base_model  = (regs[0] >> 4)  & 0xF;
  int ext_model   = (regs[0] >> 16) & 0xF;
  g_cpu_family = (base_family == 0xF) ? (base_family + ext_family) : base_family;
  g_cpu_model  = (base_family == 0x6 || base_family == 0xF)
                   ? (base_model | (ext_model << 4)) : base_model;

  /* AMD Zen4 / Zen5 (and newer) encode the core VID with SVI3 */
  g_cpu_svi3 = 0;
  if (g_cpu_vendor == CPU_VENDOR_AMD) {
    if (g_cpu_family >= 0x1A)
      g_cpu_svi3 = 1;
    else if (g_cpu_family == 0x19 &&
             ((g_cpu_model >= 0x10 && g_cpu_model <= 0x1F) ||   /* Genoa / Storm Peak */
              (g_cpu_model >= 0x60 && g_cpu_model <= 0x7F) ||   /* Raphael / Phoenix  */
              (g_cpu_model >= 0xA0 && g_cpu_model <= 0xAF)))    /* Bergamo / Siena    */
      g_cpu_svi3 = 1;
  }

  printf("[wr0] CPU vendor=%s family=0x%X model=0x%X svi3=%d\n",
         vendor, g_cpu_family, g_cpu_model, g_cpu_svi3);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/* Unload the DLL and forget every export pointer. Used on every failure
   path of cpu_wr0_init() and on shutdown, so no pointer into an unloaded
   module is ever called later (e.g. g_deinit() from cpu_wr0_shutdown()). */
static void wr0_unload(int call_deinit) {
  if (call_deinit && g_deinit) g_deinit();
  if (g_dll) { FreeLibrary(g_dll); g_dll = NULL; }
  g_init   = NULL;
  g_deinit = NULL;
  g_rdmsr  = NULL;
  g_rdpci  = NULL;
  g_wrpci  = NULL;
  g_rdmem  = NULL;
  g_rdio   = NULL;
  g_wrio   = NULL;
  g_ready  = 0;
}

void cpu_wr0_init(void) {
  if (g_ready) return;

  g_dll = LoadLibraryA("WinRing0x64.dll");
  if (!g_dll) {
    printf("[wr0] WinRing0x64.dll not found (error %lu)\n", GetLastError());
    return;
  }

  g_init   = (t_InitializeOls)   GetProcAddress(g_dll, "InitializeOls");
  g_deinit = (t_DeinitializeOls) GetProcAddress(g_dll, "DeinitializeOls");
  g_rdmsr  = (t_Rdmsr)           GetProcAddress(g_dll, "Rdmsr");
  t_IsMsr  is_msr = (t_IsMsr)    GetProcAddress(g_dll, "IsMsr");

  if (!g_init || !g_deinit || !g_rdmsr || !is_msr) {
    printf("[wr0] Missing exports in WinRing0x64.dll\n");
    wr0_unload(0);
    return;
  }

  if (!g_init()) {
    printf("[wr0] InitializeOls() failed — driver not loaded or not admin\n");
    wr0_unload(0);
    return;
  }

  if (!is_msr()) {
    printf("[wr0] IsMsr() returned false — MSR not supported\n");
    wr0_unload(1);
    return;
  }

  /* Optional PCI config + physical memory access */
  g_rdpci = (t_ReadPciConfigDword) GetProcAddress(g_dll, "ReadPciConfigDwordEx");
  g_wrpci = (t_WritePciConfigDword)GetProcAddress(g_dll, "WritePciConfigDwordEx");
  g_rdmem = (t_ReadMemory)         GetProcAddress(g_dll, "ReadMemory");
  g_rdio  = (t_ReadIoPortByte)     GetProcAddress(g_dll, "ReadIoPortByte");
  g_wrio  = (t_WriteIoPortByte)    GetProcAddress(g_dll, "WriteIoPortByte");

  detect_cpu();

  if (g_cpu_vendor == CPU_VENDOR_INTEL)
    g_tjmax = read_tjmax();

  g_ready = 1;
  printf("[wr0] Ready — vendor=%s family=0x%X TjMax=%d C\n",
         g_cpu_vendor == CPU_VENDOR_INTEL ? "Intel" :
         g_cpu_vendor == CPU_VENDOR_AMD   ? "AMD"   : "Unknown",
         g_cpu_family, g_tjmax);
}

void cpu_wr0_shutdown(void) {
  /* DeinitializeOls only if InitializeOls succeeded (g_ready) */
  wr0_unload(g_ready);
}
