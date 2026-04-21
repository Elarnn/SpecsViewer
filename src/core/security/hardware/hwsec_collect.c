// hwsec_collect.c
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include "../secure.h"
#include "onstart/privilege.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <oleauto.h>
#include <wbemidl.h>
#include <ctype.h> // tolower

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif

// clang-format on

#define TPM_MFG_INTC 0x494E5443u // "INTC" -> 1229870147
#define TPM_MFG_AMD 0x414D4420u  // "AMD " -> 1095582752

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")

static void cpuid_ex(int out[4], int leaf, int subleaf) {
#if defined(_MSC_VER)
  __cpuidex(out, leaf, subleaf);
#elif defined(__GNUC__) || defined(__clang__)
  unsigned int a = 0, b = 0, c = 0, d = 0;
  __cpuid_count((unsigned int)leaf, (unsigned int)subleaf, a, b, c, d);
  out[0] = (int)a;
  out[1] = (int)b;
  out[2] = (int)c;
  out[3] = (int)d;
#else
  out[0] = out[1] = out[2] = out[3] = 0;
#endif
}

// ----------------------------- helpers: registry -----------------------------

static int reg_read_dword(HKEY root, const char *subkey, const char *value,
                          DWORD *out_val) {
  if (!out_val)
    return 0;
  DWORD type = 0, cb = sizeof(DWORD), v = 0;
  LONG r = RegGetValueA(root, subkey, value, RRF_RT_REG_DWORD, &type, &v, &cb);
  if (r != ERROR_SUCCESS)
    return 0;
  *out_val = v;
  return 1;
}

static int reg_key_exists(HKEY root, const char *subkey) {
  HKEY h = NULL;
  LONG r = RegOpenKeyExA(root, subkey, 0, KEY_READ, &h);
  if (r == ERROR_SUCCESS && h) {
    RegCloseKey(h);
    return 1;
  }
  return 0;
}

static int contains_nocase(const char *s, const char *sub) {
  if (!s || !sub)
    return 0;
  size_t n = strlen(sub);
  if (n == 0)
    return 1;
  for (const char *p = s; *p; ++p) {
    size_t i = 0;
    while (sub[i] && p[i] &&
           (char)tolower((unsigned char)p[i]) ==
               (char)tolower((unsigned char)sub[i]))
      i++;
    if (i == n)
      return 1;
  }
  return 0;
}

static int bstr_to_utf8(BSTR ws, char *out, int out_sz) {
  if (!out || out_sz <= 0)
    return 0;
  out[0] = 0;
  if (!ws)
    return 0;
  int need = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
  if (need <= 0)
    return 0;
  if (need > out_sz)
    need = out_sz;
  int wrote = WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, need, NULL, NULL);
  if (wrote <= 0) {
    out[0] = 0;
    return 0;
  }
  out[out_sz - 1] = 0;
  return 1;
}

// ----------------------------- UEFI mode -----------------------------

typedef BOOL(WINAPI *PFN_GetFirmwareType)(DWORD *firmwareType);

static int query_uefi_mode(void) {
  HMODULE k32 = GetModuleHandleA("kernel32.dll");
  if (k32) {
    PFN_GetFirmwareType pGetFirmwareType =
        (PFN_GetFirmwareType)GetProcAddress(k32, "GetFirmwareType");
    if (pGetFirmwareType) {
      DWORD ft = 0;
      if (pGetFirmwareType(&ft)) {
        if (ft == 2)
          return 1;
        if (ft == 1)
          return 0;
      }
    }
  }

  if (reg_key_exists(HKEY_LOCAL_MACHINE,
                     "SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State"))
    return 1;

  return 0;
}

// ----------------------------- Secure Boot -----------------------------

static int query_secure_boot(void) {
  int uefi = query_uefi_mode();
  if (uefi == 0)
    return 0;

  DWORD v = 0;
  if (reg_read_dword(HKEY_LOCAL_MACHINE,
                     "SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
                     "UEFISecureBootEnabled", &v)) {
    return (v != 0) ? 1 : 0;
  }

  WCHAR varName[] = L"SecureBoot";
  WCHAR guid[] = L"{8BE4DF61-93CA-11D2-AA0D-00E098032B8C}";
  BYTE sb = 0;
  DWORD got = GetFirmwareEnvironmentVariableW(varName, guid, &sb, sizeof(sb));
  if (got == 1)
    return (sb != 0) ? 1 : 0;

  return -1;
}

// ----------------------------- TPM 2.0 (через TBS)
// -----------------------------

typedef uint32_t(WINAPI *PFN_Tbsi_GetDeviceInfo)(uint32_t Size, void *Info);

typedef struct TBS_DEVICE_INFO_MIN {
  uint32_t structVersion;
  uint32_t tpmVersion; // 1=1.2, 2=2.0
  uint32_t tpmInterfaceType;
  uint32_t tpmImpRevision;
} TBS_DEVICE_INFO_MIN;

#define TBS_DEVICE_INFO_VERSION 1
#define TBS_TPM_VERSION_12 1
#define TBS_TPM_VERSION_20 2

static int query_tpm20(void) {
  HMODULE tbs = LoadLibraryA("tbs.dll");
  if (!tbs)
    return -1;

  PFN_Tbsi_GetDeviceInfo pGetInfo =
      (PFN_Tbsi_GetDeviceInfo)GetProcAddress(tbs, "Tbsi_GetDeviceInfo");
  if (!pGetInfo) {
    FreeLibrary(tbs);
    return -1;
  }

  TBS_DEVICE_INFO_MIN info;
  memset(&info, 0, sizeof(info));
  info.structVersion = TBS_DEVICE_INFO_VERSION;

  uint32_t st = pGetInfo((uint32_t)sizeof(info), &info);
  FreeLibrary(tbs);

  if (st != 0)
    return 0;
  return (info.tpmVersion == TBS_TPM_VERSION_20) ? 1 : 0;
}

// ----------------------------- Device Guard (WMI): VBS/HVCI
// -----------------------------

static int safearray_contains_long(SAFEARRAY *sa, LONG needle) {
  if (!sa)
    return 0;

  LONG lb = 0, ub = -1;
  if (FAILED(SafeArrayGetLBound(sa, 1, &lb)))
    return 0;
  if (FAILED(SafeArrayGetUBound(sa, 1, &ub)))
    return 0;

  VARTYPE elem = 0;
  if (FAILED(SafeArrayGetVartype(sa, &elem)))
    elem = 0;

  if (elem == VT_VARIANT) {
    for (LONG i = lb; i <= ub; i++) {
      VARIANT e;
      VariantInit(&e);
      if (SUCCEEDED(SafeArrayGetElement(sa, &i, &e))) {
        if (e.vt == VT_I4 && e.lVal == needle) {
          VariantClear(&e);
          return 1;
        }
        if (e.vt == VT_UI4 && (LONG)e.ulVal == needle) {
          VariantClear(&e);
          return 1;
        }
      }
      VariantClear(&e);
    }
    return 0;
  }

  if (elem == VT_I4) {
    for (LONG i = lb; i <= ub; i++) {
      LONG v = 0;
      if (SUCCEEDED(SafeArrayGetElement(sa, &i, &v)))
        if (v == needle)
          return 1;
    }
    return 0;
  }

  if (elem == VT_UI4) {
    for (LONG i = lb; i <= ub; i++) {
      ULONG v = 0;
      if (SUCCEEDED(SafeArrayGetElement(sa, &i, &v)))
        if ((LONG)v == needle)
          return 1;
    }
    return 0;
  }

  return 0;
}

static int wmi_query_deviceguard(int *out_vbs, int *out_hvci) {
  if (out_vbs)
    *out_vbs = -1;
  if (out_hvci)
    *out_hvci = -1;

  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  int need_uninit = SUCCEEDED(hr) ? 1 : 0;
  if (hr == RPC_E_CHANGED_MODE)
    need_uninit = 0;
  else if (FAILED(hr))
    return 0;

  hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                            RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
  if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  IWbemLocator *pLoc = NULL;
  hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                        &IID_IWbemLocator, (LPVOID *)&pLoc);
  if (FAILED(hr) || !pLoc) {
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  IWbemServices *pSvc = NULL;
  BSTR ns = SysAllocString(L"ROOT\\Microsoft\\Windows\\DeviceGuard");
  hr = pLoc->lpVtbl->ConnectServer(pLoc, ns, NULL, NULL, 0, 0, 0, 0, &pSvc);
  SysFreeString(ns);

  if (FAILED(hr) || !pSvc) {
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  hr = CoSetProxyBlanket((IUnknown *)pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                         NULL, RPC_C_AUTHN_LEVEL_CALL,
                         RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
  if (FAILED(hr)) {
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  IEnumWbemClassObject *pEnum = NULL;
  BSTR wql = SysAllocString(L"WQL");
  BSTR q = SysAllocString(L"SELECT VirtualizationBasedSecurityStatus, "
                          L"SecurityServicesRunning FROM Win32_DeviceGuard");
  hr = pSvc->lpVtbl->ExecQuery(
      pSvc, wql, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
      &pEnum);
  SysFreeString(wql);
  SysFreeString(q);

  if (FAILED(hr) || !pEnum) {
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  IWbemClassObject *pObj = NULL;
  ULONG ret = 0;
  hr = pEnum->lpVtbl->Next(pEnum, 8000, 1, &pObj, &ret);
  if (ret == 0 || !pObj) {
    pEnum->lpVtbl->Release(pEnum);
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  if (out_vbs) {
    VARIANT vt;
    VariantInit(&vt);
    hr = pObj->lpVtbl->Get(pObj, L"VirtualizationBasedSecurityStatus", 0, &vt,
                           NULL, NULL);
    if (SUCCEEDED(hr) && (vt.vt == VT_I4 || vt.vt == VT_UI4)) {
      LONG s = (vt.vt == VT_I4) ? vt.lVal : (LONG)vt.ulVal;
      *out_vbs = (s != 0) ? 1 : 0;
    } else {
      *out_vbs = -1;
    }
    VariantClear(&vt);
  }

  if (out_hvci) {
    VARIANT vt;
    VariantInit(&vt);
    hr =
        pObj->lpVtbl->Get(pObj, L"SecurityServicesRunning", 0, &vt, NULL, NULL);
    if (SUCCEEDED(hr) && (vt.vt & VT_ARRAY) && vt.parray) {
      *out_hvci = safearray_contains_long(vt.parray, 2) ? 1 : 0;
    } else if (SUCCEEDED(hr) && (vt.vt == VT_NULL || vt.vt == VT_EMPTY)) {
      *out_hvci = 0;
    } else {
      *out_hvci = -1;
    }
    VariantClear(&vt);
  }

  pObj->lpVtbl->Release(pObj);
  pEnum->lpVtbl->Release(pEnum);
  pSvc->lpVtbl->Release(pSvc);
  pLoc->lpVtbl->Release(pLoc);
  if (need_uninit)
    CoUninitialize();

  return 1;
}

static int query_vbs(void) {
  int vbs = -1, hvci_dummy = -1;

  if (wmi_query_deviceguard(&vbs, &hvci_dummy) && vbs != -1)
    return vbs;

  DWORD v = 0;
  if (reg_read_dword(HKEY_LOCAL_MACHINE,
                     "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
                     "EnableVirtualizationBasedSecurity", &v)) {
    return (v != 0) ? 1 : 0;
  }

  return -1;
}

static int query_hvci(void) {
  int vbs_dummy = -1, hvci = -1;

  if (wmi_query_deviceguard(&vbs_dummy, &hvci) && hvci != -1)
    return hvci;

  DWORD v = 0;
  if (reg_read_dword(HKEY_LOCAL_MACHINE,
                     "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenario"
                     "s\\HypervisorEnforcedCodeIntegrity",
                     "Enabled", &v)) {
    return (v != 0) ? 1 : 0;
  }

  return -1;
}

// ----------------------------- TPM vendor (WMI): fTPM/PTT
// -----------------------------

static int wmi_get_tpm_vendor(uint32_t *out_id, char out_txt[16]) {
  if (out_id)
    *out_id = 0;
  if (out_txt)
    out_txt[0] = 0;

  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  int need_uninit = SUCCEEDED(hr) ? 1 : 0;
  if (hr == RPC_E_CHANGED_MODE)
    need_uninit = 0;
  else if (FAILED(hr))
    return -1;

  hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                            RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
  if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
    if (need_uninit)
      CoUninitialize();
    return -1;
  }

  IWbemLocator *pLoc = NULL;
  hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                        &IID_IWbemLocator, (LPVOID *)&pLoc);
  if (FAILED(hr) || !pLoc) {
    if (need_uninit)
      CoUninitialize();
    return -1;
  }

  IWbemServices *pSvc = NULL;
  BSTR ns = SysAllocString(L"ROOT\\CIMV2\\Security\\MicrosoftTpm");
  hr = pLoc->lpVtbl->ConnectServer(pLoc, ns, NULL, NULL, 0, 0, 0, 0, &pSvc);
  SysFreeString(ns);

  if (FAILED(hr) || !pSvc) {
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return -1;
  }

  hr = CoSetProxyBlanket((IUnknown *)pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                         NULL, RPC_C_AUTHN_LEVEL_CALL,
                         RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
  if (FAILED(hr)) {
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return -1;
  }

  IEnumWbemClassObject *pEnum = NULL;
  BSTR wql = SysAllocString(L"WQL");
  BSTR q = SysAllocString(
      L"SELECT ManufacturerId, ManufacturerIdTxt FROM Win32_Tpm");
  hr = pSvc->lpVtbl->ExecQuery(
      pSvc, wql, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
      &pEnum);
  SysFreeString(wql);
  SysFreeString(q);

  if (FAILED(hr) || !pEnum) {
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return -1;
  }

  IWbemClassObject *pObj = NULL;
  ULONG ret = 0;
  hr = pEnum->lpVtbl->Next(pEnum, 2000, 1, &pObj, &ret);

  if (ret == 0 || !pObj) {
    // нет инстанса -> TPM не найден (или выключен)
    pEnum->lpVtbl->Release(pEnum);
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  int got_any = 0;

  // 1) ManufacturerId (uint32)
  {
    VARIANT vt;
    VariantInit(&vt);
    hr = pObj->lpVtbl->Get(pObj, L"ManufacturerId", 0, &vt, NULL, NULL);
    if (SUCCEEDED(hr) && (vt.vt == VT_UI4 || vt.vt == VT_I4)) {
      uint32_t id = (vt.vt == VT_UI4) ? (uint32_t)vt.ulVal : (uint32_t)vt.lVal;
      if (out_id)
        *out_id = id;
      got_any = 1;
    }
    VariantClear(&vt);
  }

  // 2) ManufacturerIdTxt (BSTR) — просто как доп. инфа
  if (out_txt) {
    VARIANT vt;
    VariantInit(&vt);
    hr = pObj->lpVtbl->Get(pObj, L"ManufacturerIdTxt", 0, &vt, NULL, NULL);
    if (SUCCEEDED(hr) && vt.vt == VT_BSTR && vt.bstrVal) {
      bstr_to_utf8(vt.bstrVal, out_txt, 16);
      got_any = 1;
    }
    VariantClear(&vt);
  }

  pObj->lpVtbl->Release(pObj);
  pEnum->lpVtbl->Release(pEnum);
  pSvc->lpVtbl->Release(pSvc);
  pLoc->lpVtbl->Release(pLoc);
  if (need_uninit)
    CoUninitialize();

  return got_any ? 1 : -1;
}

static void query_ftpm_ptt(int tpm20, int *out_ftpm, int *out_ptt) {
  // по умолчанию UNKNOWN, чтобы не врать OFF при проблемах доступа
  if (out_ftpm)
    *out_ftpm = -1;
  if (out_ptt)
    *out_ptt = -1;

  // TPM2 нет -> оба OFF
  if (tpm20 == 0) {
    if (out_ftpm)
      *out_ftpm = 0;
    if (out_ptt)
      *out_ptt = 0;
    return;
  }

  // TPM2 неизвестно -> UNKNOWN
  if (tpm20 < 0) {
    return;
  }

  // TPM2 есть, но без админа WMI к Win32_Tpm часто недоступен
  if (!app_is_elevated()) {
    return; // оставляем UNKNOWN
  }

  uint32_t id = 0;
  char txt[16] = {0};
  int r = wmi_get_tpm_vendor(&id, txt);

  if (r <= 0) {
    // админ, но не удалось получить vendor -> UNKNOWN
    return;
  }

  int ptt = 0, ftpm = 0;

  // надёжные проверки по числу (из твоего PowerShell):
  if (id == TPM_MFG_INTC || id == 1229870147u /* "INTC" */)
    ptt = 1;
  else if (id == TPM_MFG_AMD || id == 1095582752u /* "AMD " */)
    ftpm = 1;
  else {
    if (txt[0]) {
      if (contains_nocase(txt, "INTC") || contains_nocase(txt, "INTEL"))
        ptt = 1;
      else if (contains_nocase(txt, "AMD"))
        ftpm = 1;
    }
  }

  // если распознали — записываем, иначе оставляем UNKNOWN
  if (ptt || ftpm) {
    if (ptt)
      ftpm = 0;
    if (ftpm)
      ptt = 0;

    if (out_ftpm)
      *out_ftpm = ftpm;
    if (out_ptt)
      *out_ptt = ptt;
  }
}

// ----------------------------- BitLocker (WMI) -----------------------------

static void get_system_drive_letter_w(WCHAR out2[3]) {
  out2[0] = L'C';
  out2[1] = L':';
  out2[2] = 0;

  WCHAR winDir[MAX_PATH];
  DWORD n = GetWindowsDirectoryW(winDir, MAX_PATH);
  if (n >= 2 && winDir[1] == L':') {
    out2[0] = winDir[0];
    out2[1] = L':';
    out2[2] = 0;
  }
}

static int query_bitlocker_system_drive(void) {
  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  int need_uninit = SUCCEEDED(hr) ? 1 : 0;
  if (hr == RPC_E_CHANGED_MODE)
    need_uninit = 0;
  else if (FAILED(hr))
    return -1;

  hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                            RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
  if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  IWbemLocator *pLoc = NULL;
  hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                        &IID_IWbemLocator, (LPVOID *)&pLoc);
  if (FAILED(hr) || !pLoc) {
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  IWbemServices *pSvc = NULL;
  BSTR ns = SysAllocString(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption");
  hr = pLoc->lpVtbl->ConnectServer(pLoc, ns, NULL, NULL, 0, 0, 0, 0, &pSvc);
  SysFreeString(ns);

  if (FAILED(hr) || !pSvc) {
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  hr = CoSetProxyBlanket((IUnknown *)pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                         NULL, RPC_C_AUTHN_LEVEL_CALL,
                         RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
  if (FAILED(hr)) {
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  WCHAR dl[3];
  get_system_drive_letter_w(dl);
  WCHAR query[256];
  swprintf(query, 256,
           L"SELECT ProtectionStatus FROM Win32_EncryptableVolume WHERE "
           L"DriveLetter='%s'",
           dl);

  IEnumWbemClassObject *pEnum = NULL;
  BSTR wql = SysAllocString(L"WQL");
  BSTR q = SysAllocString(query);
  hr = pSvc->lpVtbl->ExecQuery(
      pSvc, wql, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
      &pEnum);
  SysFreeString(wql);
  SysFreeString(q);

  if (FAILED(hr) || !pEnum) {
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  IWbemClassObject *pObj = NULL;
  ULONG ret = 0;
  hr = pEnum->lpVtbl->Next(pEnum, 2000, 1, &pObj, &ret);

  if (ret == 0 || !pObj) {
    pEnum->lpVtbl->Release(pEnum);
    pSvc->lpVtbl->Release(pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (need_uninit)
      CoUninitialize();
    return 0;
  }

  VARIANT vt;
  VariantInit(&vt);
  hr = pObj->lpVtbl->Get(pObj, L"ProtectionStatus", 0, &vt, NULL, NULL);

  int result = 0;
  if (SUCCEEDED(hr) && (vt.vt == VT_I4 || vt.vt == VT_UI4)) {
    LONG ps = (vt.vt == VT_I4) ? vt.lVal : (LONG)vt.ulVal;
    result = (ps == 1) ? 1 : 0;
  }

  VariantClear(&vt);
  pObj->lpVtbl->Release(pObj);
  pEnum->lpVtbl->Release(pEnum);
  pSvc->lpVtbl->Release(pSvc);
  pLoc->lpVtbl->Release(pLoc);

  if (need_uninit)
    CoUninitialize();
  return result;
}

// ----------------------------- NX / DEP -----------------------------
// Возвращает: 1=включено, 0=выключено, -1=неизвестно
typedef int(WINAPI *PFN_GetSystemDEPPolicy_Int)(void);

static int query_nx(void) {
  HMODULE k32 = GetModuleHandleA("kernel32.dll");
  if (k32) {
    PFN_GetSystemDEPPolicy_Int pGetSystemDEPPolicy =
        (PFN_GetSystemDEPPolicy_Int)GetProcAddress(k32, "GetSystemDEPPolicy");
    if (pGetSystemDEPPolicy) {
      int pol =
          pGetSystemDEPPolicy(); // 0=AlwaysOff, 1=AlwaysOn, 2=OptIn, 3=OptOut
      if (pol == 0)
        return 0;
      if (pol == 1 || pol == 2 || pol == 3)
        return 1;
      // иначе продолжаем fallback
    }
  }

#ifndef PF_NX_ENABLED
#define PF_NX_ENABLED 12
#endif
  return IsProcessorFeaturePresent(PF_NX_ENABLED) ? 1 : 0;
}

static void cpuidex_int(int out[4], unsigned leaf, unsigned subleaf) {
#if defined(_MSC_VER)
  __cpuidex(out, (int)leaf, (int)subleaf);
#elif defined(__GNUC__) || defined(__clang__)
  unsigned a, b, c, d;
  __cpuid_count(leaf, subleaf, a, b, c, d);
  out[0] = (int)a;
  out[1] = (int)b;
  out[2] = (int)c;
  out[3] = (int)d;
#else
  out[0] = out[1] = out[2] = out[3] = 0;
#endif
}

// ----------------------------- SMEP (CPU support)
// ----------------------------- Возвращает: 1=поддерживается, 0=не
// поддерживается, -1=неизвестно (не x86)
static int query_smep(void) {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) ||                \
    defined(__x86_64__)
  int r[4];
  cpuidex_int(r, 0, 0);
  if ((unsigned)r[0] < 7)
    return 0; // leaf 7 нет -> SMEP нет
  cpuidex_int(r, 7, 0);
  // CPUID.(EAX=7,ECX=0):EBX[7] = SMEP
  return (r[1] & (1 << 7)) ? 1 : 0;
#else
  return -1;
#endif
}

// CET = CPUID.(EAX=7, ECX=0):ECX bit7  (CET_SS)  shadow stack
//       CPUID.(EAX=7, ECX=0):ECX bit20 (CET_IBT) indirect branch tracking
static int query_cet(void) {
  int r[4] = {0};

  cpuid_ex(r, 0, 0);
  if ((unsigned)r[0] < 7)
    return 0;

  cpuid_ex(r, 7, 0);

  int cet_ss = (r[2] >> 7) & 1;
  int cet_ibt = (r[2] >> 20) & 1;

  return (cet_ss || cet_ibt) ? 1 : 0;
}

// ----------------------------- public API -----------------------------

int hwsec_query_win(HwSecInfo *out) {
  if (!out)
    return 0;

  out->uefi_mode = query_uefi_mode();
  out->secure_boot = query_secure_boot();
  out->tpm20 = query_tpm20();
  out->bitlocker = query_bitlocker_system_drive();
  out->vbs = query_vbs();
  out->hvci = query_hvci();
  out->nx = query_nx();
  out->smep = query_smep();
  out->cet = query_cet();

  query_ftpm_ptt(out->tpm20, &out->ftpm, &out->ptt);

  return 1;
}
