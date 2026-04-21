// WinRing0.h
// Reference header for WinRing0x64.dll dynamic API.
// Functions are loaded at runtime via LoadLibrary / GetProcAddress.
#pragma once
#include <windows.h>

/* ── Function pointer typedefs ──────────────────────────────────────────── */

// Initialize the WinRing0 kernel driver. Must be called before any MSR/IO ops.
// Returns TRUE on success.
typedef BOOL  (WINAPI *t_InitializeOls)(void);

// Shut down the WinRing0 kernel driver and release resources.
typedef void  (WINAPI *t_DeinitializeOls)(void);

// Read a 64-bit MSR by index into eax (low 32 bits) and edx (high 32 bits).
// Returns TRUE on success.
typedef BOOL  (WINAPI *t_ReadMsr)(DWORD index, PDWORD eax, PDWORD edx);

// Read MSR on a specific logical processor (affinity mask).
typedef BOOL  (WINAPI *t_ReadMsrTx)(DWORD index, PDWORD eax, PDWORD edx,
                                    DWORD_PTR affinityMask);

// Write a 64-bit MSR by index.
typedef BOOL  (WINAPI *t_WriteMsr)(DWORD index, DWORD eax, DWORD edx);

/* ── Exported symbol names in WinRing0x64.dll ──────────────────────────── */
// "InitializeOls"
// "DeinitializeOls"
// "ReadMsr"
// "ReadMsrTx"
// "WriteMsr"
