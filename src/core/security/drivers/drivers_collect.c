// drivers_collect.c
// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <bcrypt.h>
#include <winver.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>

#include "../../backend.h"

// clang-format on

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

#ifndef DRV_MAX
#define DRV_MAX 64
#endif

static void ansi_to_utf8(const char *src, char *dst, int dstsz) {
  if (!dst || dstsz <= 0)
    return;
  dst[0] = 0;
  if (!src || !src[0])
    return;

  wchar_t wbuf[1024];
  int wlen = MultiByteToWideChar(CP_ACP, 0, src, -1, wbuf, 1024);
  if (wlen <= 0)
    return;

  WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, dst, dstsz, NULL, NULL);
}

static void str_copy(char *dst, size_t dstsz, const char *src) {
  if (!dst || dstsz == 0)
    return;
  if (!src) {
    dst[0] = 0;
    return;
  }
  strncpy(dst, src, dstsz - 1);
  dst[dstsz - 1] = 0;
}

static int str_ieq(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  return _stricmp(a, b) == 0;
}

static int str_iendswith(const char *s, const char *suffix) {
  if (!s || !suffix)
    return 0;
  size_t ls = strlen(s), lsf = strlen(suffix);
  if (ls < lsf)
    return 0;
  return _stricmp(s + (ls - lsf), suffix) == 0;
}

static void basename_from_path(const char *path, char *out, size_t outsz) {
  if (!out || outsz == 0)
    return;
  out[0] = 0;
  if (!path)
    return;

  const char *p = path;
  const char *last = path;
  for (; *p; ++p)
    if (*p == '\\' || *p == '/')
      last = p + 1;

  str_copy(out, outsz, last);
}

static void cut_path_token(const char *in, char *out, size_t outsz) {
  if (!out || outsz == 0)
    return;
  out[0] = 0;
  if (!in)
    return;

  while (*in == ' ' || *in == '\t')
    in++;

  if (*in == '"') {
    in++;
    const char *q = strchr(in, '"');
    if (!q)
      q = in + strlen(in);
    size_t n = (size_t)(q - in);
    if (n >= outsz)
      n = outsz - 1;
    memcpy(out, in, n);
    out[n] = 0;
  } else {
    const char *q = in;
    while (*q && *q != ' ' && *q != '\t')
      q++;
    size_t n = (size_t)(q - in);
    if (n >= outsz)
      n = outsz - 1;
    memcpy(out, in, n);
    out[n] = 0;
  }
}

static int resolve_driver_pathA(const char *imagePathRaw, char *out,
                                size_t outsz) {
  if (!out || outsz == 0)
    return 0;
  out[0] = 0;
  if (!imagePathRaw || !imagePathRaw[0])
    return 0;

  char token[512];
  cut_path_token(imagePathRaw, token, sizeof(token));

  if (!_strnicmp(token, "\\??\\", 4))
    memmove(token, token + 4, strlen(token + 4) + 1);

  {
    char expanded[512];
    DWORD need =
        ExpandEnvironmentStringsA(token, expanded, (DWORD)sizeof(expanded));
    if (need > 0 && need <= sizeof(expanded))
      str_copy(token, sizeof(token), expanded);
  }

  char winDir[MAX_PATH];
  winDir[0] = 0;
  GetWindowsDirectoryA(winDir, (UINT)sizeof(winDir));
  if (!winDir[0])
    return 0;

  if (strlen(token) >= 3 && token[1] == ':' &&
      (token[2] == '\\' || token[2] == '/')) {
    str_copy(out, outsz, token);
    return 1;
  }

  if (!_strnicmp(token, "\\SystemRoot\\", 12)) {
    snprintf(out, outsz, "%s\\%s", winDir, token + 12);
    return 1;
  }

  if (!_strnicmp(token, "System32\\", 9) ||
      !_strnicmp(token, "system32\\", 9)) {
    snprintf(out, outsz, "%s\\%s", winDir, token);
    return 1;
  }

  if (!_strnicmp(token, "\\Windows\\", 9)) {
    char drive[3] = "C:";
    if (strlen(winDir) >= 2 && winDir[1] == ':') {
      drive[0] = winDir[0];
      drive[1] = ':';
    }
    snprintf(out, outsz, "%s%s", drive, token);
    return 1;
  }

  if (!_strnicmp(token, "\\System32\\", 10)) {
    snprintf(out, outsz, "%s%s", winDir, token);
    return 1;
  }

  if (!_strnicmp(token, "drivers\\", 8) || !_strnicmp(token, "Drivers\\", 8)) {
    snprintf(out, outsz, "%s\\System32\\%s", winDir, token);
    return 1;
  }

  snprintf(out, outsz, "%s\\System32\\%s", winDir, token);
  return 1;
}

static void filetime_to_ymd(FILETIME ft, char out[32]) {
  if (!out)
    return;
  out[0] = 0;
  SYSTEMTIME st;
  if (FileTimeToSystemTime(&ft, &st))
    snprintf(out, 32, "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
}

static int sha256_file_hexA(const char *path, char out_hex[65]) {
  if (!out_hex)
    return 0;
  out_hex[0] = 0;

  HANDLE h = CreateFileA(
      path, GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  if (h == INVALID_HANDLE_VALUE)
    return 0;

  BCRYPT_ALG_HANDLE hAlg = NULL;
  BCRYPT_HASH_HANDLE hHash = NULL;
  DWORD objLen = 0, cb = 0;
  PUCHAR obj = NULL;
  UCHAR hash[32];
  DWORD hashLen = 0;

  if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0)
    goto done;
  if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen,
                        sizeof(objLen), &cb, 0) != 0)
    goto done;
  if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen,
                        sizeof(hashLen), &cb, 0) != 0)
    goto done;
  if (hashLen != 32)
    goto done;

  obj = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, objLen);
  if (!obj)
    goto done;

  if (BCryptCreateHash(hAlg, &hHash, obj, objLen, NULL, 0, 0) != 0)
    goto done;

  BYTE buf[64 * 1024];
  DWORD rd = 0;
  while (ReadFile(h, buf, (DWORD)sizeof(buf), &rd, NULL) && rd > 0) {
    if (BCryptHashData(hHash, buf, rd, 0) != 0)
      goto done;
  }

  if (BCryptFinishHash(hHash, hash, sizeof(hash), 0) != 0)
    goto done;

  static const char *hex = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    out_hex[i * 2 + 0] = hex[(hash[i] >> 4) & 0xF];
    out_hex[i * 2 + 1] = hex[(hash[i] >> 0) & 0xF];
  }
  out_hex[64] = 0;

  CloseHandle(h);
  if (hHash)
    BCryptDestroyHash(hHash);
  if (hAlg)
    BCryptCloseAlgorithmProvider(hAlg, 0);
  if (obj)
    HeapFree(GetProcessHeap(), 0, obj);
  return 1;

done:
  CloseHandle(h);
  if (hHash)
    BCryptDestroyHash(hHash);
  if (hAlg)
    BCryptCloseAlgorithmProvider(hAlg, 0);
  if (obj)
    HeapFree(GetProcessHeap(), 0, obj);
  return 0;
}

static int file_version_infoA(const char *path, char *company, size_t companySz,
                              char *desc, size_t descSz, char *ver,
                              size_t verSz) {
  if (company)
    company[0] = 0;
  if (desc)
    desc[0] = 0;
  if (ver)
    ver[0] = 0;

  DWORD dummy = 0;
  DWORD sz = GetFileVersionInfoSizeA(path, &dummy);
  if (!sz)
    return 0;

  BYTE *data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, sz);
  if (!data)
    return 0;

  int ok = 0;
  if (!GetFileVersionInfoA(path, 0, sz, data))
    goto done;

  struct LANGANDCODEPAGE {
    WORD wLanguage;
    WORD wCodePage;
  } *tr = NULL;
  UINT trLen = 0;
  if (!VerQueryValueA(data, "\\VarFileInfo\\Translation", (LPVOID *)&tr,
                      &trLen) ||
      trLen < sizeof(*tr))
    goto done;

  char sub[128];
  char *val = NULL;
  UINT vlen = 0;

  if (company && companySz) {
    snprintf(sub, sizeof(sub), "\\StringFileInfo\\%04x%04x\\CompanyName",
             tr[0].wLanguage, tr[0].wCodePage);
    if (VerQueryValueA(data, sub, (LPVOID *)&val, &vlen) && val && vlen)
      str_copy(company, companySz, val);
  }
  if (desc && descSz) {
    snprintf(sub, sizeof(sub), "\\StringFileInfo\\%04x%04x\\FileDescription",
             tr[0].wLanguage, tr[0].wCodePage);
    if (VerQueryValueA(data, sub, (LPVOID *)&val, &vlen) && val && vlen)
      str_copy(desc, descSz, val);
  }
  if (ver && verSz) {
    snprintf(sub, sizeof(sub), "\\StringFileInfo\\%04x%04x\\FileVersion",
             tr[0].wLanguage, tr[0].wCodePage);
    if (VerQueryValueA(data, sub, (LPVOID *)&val, &vlen) && val && vlen)
      str_copy(ver, verSz, val);
  }

  ok = 1;

done:
  HeapFree(GetProcessHeap(), 0, data);
  return ok;
}

static int snapshot_has_driver_file(const Snapshot *s,
                                    const char *driver_file) {
  if (!s || !driver_file || !driver_file[0])
    return 0;
  for (int i = 0; i < s->data.drivers_count; ++i)
    if (str_ieq(s->data.drivers[i].driver_file, driver_file))
      return 1;
  return 0;
}

static int add_driver(Snapshot *s, const DriverInfo *d) {
  if (!s || !d)
    return 0;
  if (s->data.drivers_count >= DRV_MAX)
    return 0;

  if (d->driver_file[0] && snapshot_has_driver_file(s, d->driver_file))
    return 0;

  s->data.drivers[s->data.drivers_count++] = *d;
  return 1;
}

static int is_main_class(const char *cls) {
  return str_ieq(cls, "Display") || str_ieq(cls, "Net") ||
         str_ieq(cls, "MEDIA") || str_ieq(cls, "Bluetooth") ||
         str_ieq(cls, "USB") || str_ieq(cls, "System") || str_ieq(cls, "HDC") ||
         str_ieq(cls, "SCSIAdapter");
}

static int get_dev_reg_strA(HDEVINFO di, SP_DEVINFO_DATA *dev, DWORD prop,
                            char *out, DWORD outsz) {
  DWORD regType = 0, needed = 0;
  BYTE buf[1024];
  if (!out || outsz == 0)
    return 0;
  out[0] = 0;

  if (!SetupDiGetDeviceRegistryPropertyA(di, dev, prop, &regType, buf,
                                         sizeof(buf), &needed))
    return 0;

  if (regType == REG_SZ) {
    str_copy(out, outsz, (const char *)buf);
    return 1;
  }
  if (regType == REG_MULTI_SZ) {
    str_copy(out, outsz, (const char *)buf);
    return 1;
  }
  return 0;
}

static int read_reg_strA(HKEY h, const char *name, char *out, DWORD outsz) {
  if (!out || outsz == 0)
    return 0;
  out[0] = 0;
  DWORD type = 0, cb = outsz;
  LONG r = RegQueryValueExA(h, name, 0, &type, (LPBYTE)out, &cb);
  if (r != ERROR_SUCCESS)
    return 0;
  if (type != REG_SZ && type != REG_EXPAND_SZ)
    return 0;
  out[outsz - 1] = 0;
  return 1;
}

static int read_reg_dword(HKEY h, const char *name, DWORD *out) {
  if (!out)
    return 0;
  DWORD type = 0, cb = sizeof(DWORD), v = 0;
  LONG r = RegQueryValueExA(h, name, 0, &type, (LPBYTE)&v, &cb);
  if (r != ERROR_SUCCESS || type != REG_DWORD)
    return 0;
  *out = v;
  return 1;
}

static void wide_to_utf8(const wchar_t *ws, char *out, int outsz) {
  if (!out || outsz <= 0)
    return;
  out[0] = 0;
  if (!ws || !ws[0])
    return;
  WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, outsz, NULL, NULL);
}

static void start_type_to_str(DWORD start, char *out, size_t outsz) {
  if (!out || outsz == 0)
    return;
  out[0] = 0;

  switch (start) {
  case 0:
    str_copy(out, outsz, "Boot");
    break;
  case 1:
    str_copy(out, outsz, "System");
    break;
  case 2:
    str_copy(out, outsz, "Auto");
    break;
  case 3:
    str_copy(out, outsz, "Demand");
    break;
  case 4:
    str_copy(out, outsz, "Disabled");
    break;
  default: {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "Unknown(%lu)", (unsigned long)start);
    str_copy(out, outsz, tmp);
  } break;
  }
}

static void service_type_to_str(DWORD type, char *out, size_t outsz) {
  if (!out || outsz == 0)
    return;
  out[0] = 0;

  if (type == 1)
    str_copy(out, outsz, "kernel driver");
  else if (type == 2)
    str_copy(out, outsz, "file system driver");
  else if (type &
           0x30) // SERVICE_WIN32_OWN_PROCESS / SERVICE_WIN32_SHARE_PROCESS
    str_copy(out, outsz, "user-mode service");
  else {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "other(0x%lx)", (unsigned long)type);
    str_copy(out, outsz, tmp);
  }
}

static int verify_and_get_signerA(const char *fileA, int *out_signed,
                                  char *out_signer, size_t signerSz) {
  if (out_signed)
    *out_signed = 0;
  if (out_signer && signerSz)
    out_signer[0] = 0;
  if (!fileA || !fileA[0])
    return 0;

  wchar_t wpath[MAX_PATH];
  if (!MultiByteToWideChar(CP_ACP, 0, fileA, -1, wpath, MAX_PATH))
    return 0;

  GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;

  WINTRUST_FILE_INFO fi;
  ZeroMemory(&fi, sizeof(fi));
  fi.cbStruct = sizeof(fi);
  fi.pcwszFilePath = wpath;

  WINTRUST_DATA wd;
  ZeroMemory(&wd, sizeof(wd));
  wd.cbStruct = sizeof(wd);
  wd.dwUIChoice = WTD_UI_NONE;
  wd.fdwRevocationChecks = WTD_REVOKE_NONE;
  wd.dwUnionChoice = WTD_CHOICE_FILE;
  wd.pFile = &fi;
  wd.dwStateAction = WTD_STATEACTION_VERIFY;
  wd.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

  LONG st = WinVerifyTrust(NULL, &policy, &wd);

  if (st == ERROR_SUCCESS) {
    if (out_signed)
      *out_signed = 1;

    CRYPT_PROVIDER_DATA *prov = WTHelperProvDataFromStateData(wd.hWVTStateData);
    if (prov) {
      CRYPT_PROVIDER_SGNR *sgnr =
          WTHelperGetProvSignerFromChain(prov, 0, FALSE, 0);
      if (sgnr) {
        CRYPT_PROVIDER_CERT *pc = WTHelperGetProvCertFromChain(sgnr, 0);
        if (pc && pc->pCert) {
          wchar_t nameW[256];
          DWORD n = CertGetNameStringW(pc->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                       0, NULL, nameW, 256);
          if (n > 1 && out_signer && signerSz)
            wide_to_utf8(nameW, out_signer, (int)signerSz);
        }
      }
    }
  }

  wd.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(NULL, &policy, &wd);

  return (st == ERROR_SUCCESS) ? 1 : 0;
}

static void collect_pnp_main(Snapshot *s) {
  HDEVINFO di =
      SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (di == INVALID_HANDLE_VALUE)
    return;

  for (DWORD i = 0;; ++i) {
    SP_DEVINFO_DATA dev;
    dev.cbSize = sizeof(dev);
    if (!SetupDiEnumDeviceInfo(di, i, &dev))
      break;

    char cls[32] = {0};
    if (!get_dev_reg_strA(di, &dev, SPDRP_CLASS, cls, sizeof(cls)))
      continue;
    if (!is_main_class(cls))
      continue;

    DriverInfo d;
    ZeroMemory(&d, sizeof(d));
    str_copy(d.class_name, sizeof(d.class_name), cls);

    char tmpA[256];

    if (get_dev_reg_strA(di, &dev, SPDRP_DEVICEDESC, tmpA, sizeof(tmpA)))
      ansi_to_utf8(tmpA, d.device_desc, (int)sizeof(d.device_desc));
    else
      d.device_desc[0] = 0;

    if (!get_dev_reg_strA(di, &dev, SPDRP_SERVICE, d.service,
                          sizeof(d.service)))
      d.service[0] = 0;

    HKEY hDrv = SetupDiOpenDevRegKey(di, &dev, DICS_FLAG_GLOBAL, 0, DIREG_DRV,
                                     KEY_READ);
    if (hDrv && hDrv != INVALID_HANDLE_VALUE) {
      read_reg_strA(hDrv, "ProviderName", d.provider, sizeof(d.provider));
      read_reg_strA(hDrv, "DriverVersion", d.version, sizeof(d.version));
      read_reg_strA(hDrv, "DriverDate", d.date, sizeof(d.date));
      read_reg_strA(hDrv, "InfPath", d.inf, sizeof(d.inf));
      RegCloseKey(hDrv);
    }

    if (d.service[0]) {
      char keyPath[256];
      snprintf(keyPath, sizeof(keyPath),
               "SYSTEM\\CurrentControlSet\\Services\\%s", d.service);
      HKEY hSvc = NULL;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hSvc) ==
          ERROR_SUCCESS) {
        DWORD stv = 0, tyv = 0;
        if (read_reg_dword(hSvc, "Start", &stv))
          start_type_to_str(stv, d.start_type, sizeof(d.start_type));
        if (read_reg_dword(hSvc, "Type", &tyv))
          service_type_to_str(tyv, d.service_type, sizeof(d.service_type));
        read_reg_strA(hSvc, "Group", d.load_order_group,
                      sizeof(d.load_order_group));

        char imgRaw[512] = {0};
        if (read_reg_strA(hSvc, "ImagePath", imgRaw, sizeof(imgRaw))) {
          char full[MAX_PATH] = {0};
          if (resolve_driver_pathA(imgRaw, full, sizeof(full))) {
            str_copy(d.image_path, sizeof(d.image_path), full);

            if (str_iendswith(full, ".sys")) {
              basename_from_path(full, d.driver_file, sizeof(d.driver_file));

              char comp[128] = {0}, fdesc[128] = {0}, fver[64] = {0};
              if (file_version_infoA(full, comp, sizeof(comp), fdesc,
                                     sizeof(fdesc), fver, sizeof(fver))) {
                if (!d.provider[0] && comp[0])
                  str_copy(d.provider, sizeof(d.provider), comp);
                if (!d.version[0] && fver[0])
                  str_copy(d.version, sizeof(d.version), fver);
                if (fdesc[0] && d.device_desc[0] == 0)
                  str_copy(d.device_desc, sizeof(d.device_desc), fdesc);
              }

              if (!d.date[0]) {
                HANDLE hf = CreateFileA(
                    full, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hf != INVALID_HANDLE_VALUE) {
                  FILETIME ftw;
                  if (GetFileTime(hf, NULL, NULL, &ftw))
                    filetime_to_ymd(ftw, d.date);
                  CloseHandle(hf);
                }
              }

              sha256_file_hexA(full, d.sha256);
              verify_and_get_signerA(full, &d.is_signed, d.signer,
                                     sizeof(d.signer));
            }
          }
        }
        RegCloseKey(hSvc);
      }
    }

    if (d.driver_file[0] || d.service[0])
      add_driver(s, &d);

    if (s->data.drivers_count >= DRV_MAX)
      break;
  }

  SetupDiDestroyDeviceInfoList(di);
}

static void collect_services_kernel(Snapshot *s) {
  HKEY hRoot = NULL;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services",
                    0, KEY_READ, &hRoot) != ERROR_SUCCESS)
    return;

  DWORD idx = 0;
  char subName[256];
  DWORD subLen = 0;

  for (;;) {
    subLen = (DWORD)sizeof(subName);
    FILETIME ft;
    LONG r =
        RegEnumKeyExA(hRoot, idx++, subName, &subLen, NULL, NULL, NULL, &ft);
    if (r != ERROR_SUCCESS)
      break;

    HKEY hSvc = NULL;
    if (RegOpenKeyExA(hRoot, subName, 0, KEY_READ, &hSvc) != ERROR_SUCCESS)
      continue;

    DWORD type = 0;
    if (!read_reg_dword(hSvc, "Type", &type)) {
      RegCloseKey(hSvc);
      continue;
    }

    if (type != 1 && type != 2) {
      RegCloseKey(hSvc);
      continue;
    }

    char imgRaw[512] = {0};
    if (!read_reg_strA(hSvc, "ImagePath", imgRaw, sizeof(imgRaw))) {
      RegCloseKey(hSvc);
      continue;
    }

    char full[MAX_PATH] = {0};
    if (!resolve_driver_pathA(imgRaw, full, sizeof(full)) ||
        !str_iendswith(full, ".sys")) {
      RegCloseKey(hSvc);
      continue;
    }

    DriverInfo d;
    ZeroMemory(&d, sizeof(d));
    str_copy(d.class_name, sizeof(d.class_name),
             (type == 1) ? "Kernel" : "FSDriver");
    str_copy(d.service, sizeof(d.service), subName);

    service_type_to_str(type, d.service_type, sizeof(d.service_type));

    DWORD stv = 0;
    if (read_reg_dword(hSvc, "Start", &stv))
      start_type_to_str(stv, d.start_type, sizeof(d.start_type));

    read_reg_strA(hSvc, "Group", d.load_order_group,
                  sizeof(d.load_order_group));

    str_copy(d.image_path, sizeof(d.image_path), full);

    basename_from_path(full, d.driver_file, sizeof(d.driver_file));

    if (d.driver_file[0] && snapshot_has_driver_file(s, d.driver_file)) {
      RegCloseKey(hSvc);
      continue;
    }

    char disp[256] = {0};
    if (read_reg_strA(hSvc, "DisplayName", disp, sizeof(disp)))
      str_copy(d.device_desc, sizeof(d.device_desc), disp);
    else
      str_copy(d.device_desc, sizeof(d.device_desc), d.driver_file);

    file_version_infoA(full, d.provider, sizeof(d.provider), NULL, 0, d.version,
                       sizeof(d.version));

    HANDLE hf =
        CreateFileA(full, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
      FILETIME ftw;
      if (GetFileTime(hf, NULL, NULL, &ftw))
        filetime_to_ymd(ftw, d.date);
      CloseHandle(hf);
    }

    sha256_file_hexA(full, d.sha256);
    verify_and_get_signerA(full, &d.is_signed, d.signer, sizeof(d.signer));

    add_driver(s, &d);
    RegCloseKey(hSvc);

    if (s->data.drivers_count >= DRV_MAX)
      break;
  }

  RegCloseKey(hRoot);
}

static int cmp_driver(const void *a, const void *b) {
  const DriverInfo *da = (const DriverInfo *)a;
  const DriverInfo *db = (const DriverInfo *)b;

  int r = _stricmp(da->driver_file, db->driver_file);
  if (r != 0)
    return r;
  return _stricmp(da->version, db->version);
}

int drivers_collect_win(Snapshot *s) {
  if (!s)
    return 0;

  s->data.drivers_count = 0;

  collect_pnp_main(s);
  if (s->data.drivers_count < DRV_MAX)
    collect_services_kernel(s);

  if (s->data.drivers_count > 1)
    qsort(s->data.drivers, (size_t)s->data.drivers_count, sizeof(s->data.drivers[0]),
          cmp_driver);

  return 1;
}
