#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

/* ── WinRing0 function pointer types ────────────────────────────────────── */
typedef BOOL (__stdcall *t_Rdmsr)             (DWORD index, PDWORD eax, PDWORD edx);
typedef BOOL (__stdcall *t_ReadPciConfigDword) (BYTE bus, BYTE dev, BYTE func,
                                                BYTE reg,  PDWORD val);
typedef BOOL (__stdcall *t_WritePciConfigDword)(BYTE bus, BYTE dev, BYTE func,
                                                BYTE reg,  DWORD  val);
typedef BOOL (__stdcall *t_ReadMemory)        (DWORD_PTR address, PBYTE buffer,
                                                DWORD size);

/* ── Shared state — defined in winring.c ─────────────────────────────────── */
extern int     g_ready;   /* 1 if DLL loaded and driver active              */
extern int     g_tjmax;   /* Intel TjMax (°C), read during init             */
extern t_Rdmsr g_rdmsr;   /* Rdmsr export pointer                           */
extern t_ReadPciConfigDword  g_rdpci;  /* ReadPciConfigDwordEx  pointer     */
extern t_WritePciConfigDword g_wrpci;  /* WritePciConfigDwordEx pointer     */
extern t_ReadMemory          g_rdmem;  /* ReadMemory pointer                */

/* ── CPU identification (set during init) ───────────────────────────────── */
#define CPU_VENDOR_UNKNOWN 0
#define CPU_VENDOR_INTEL   1
#define CPU_VENDOR_AMD     2

extern int g_cpu_vendor;  /* CPU_VENDOR_*                                   */
extern int g_cpu_family;  /* full CPUID family (e.g. 0x17 for Zen)         */

/* ── Intel MSR indices ──────────────────────────────────────────────────── */
#define MSR_TEMPERATURE_TARGET 0x000001A2u  /* bits [23:16] = TjMax          */
#define MSR_THERM_STATUS       0x0000019Cu  /* bit 31 = valid, bits [22:16]  */

/* ── AMD PCI / SMN defines ──────────────────────────────────────────────── */
/* Family 15h/16h: thermtrip register in PCI config space of node 0          */
#define AMD_THERM_BUS        0
#define AMD_THERM_DEV        24   /* D18F3xA4 — node 0 only                  */
#define AMD_THERM_FUNC       3
#define AMD_THERM_REG        0xA4 /* CurTmp bits [31:21], unit 0.125 °C      */

/* Family 17h+ (Zen): SMN access via PCI host-bridge (B0:D0:F0)             */
#define AMD_SMN_BUS          0
#define AMD_SMN_DEV          0
#define AMD_SMN_FUNC         0
#define AMD_SMN_INDEX_REG    0x60
#define AMD_SMN_DATA_REG     0x64
#define AMD_SMN_THM_CUR_TMP  0x00059800u  /* Tctl bits [31:21], 0.125 °C    */

/* Read a 64-bit MSR. Returns 1 on success, 0 on failure. */
int wr0_rdmsr64(DWORD msr, uint64_t *out);

/* ── Lifecycle (implemented in winring.c) ────────────────────────────────── */
void cpu_wr0_init(void);
void cpu_wr0_shutdown(void);

/* ── CPU temperature via MSR/PCI (implemented in cpu/cpu_winring0.c) ──────── */
int cpu_wr0_read_temp(void);
