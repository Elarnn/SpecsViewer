// gpu_check.c
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <stdio.h>
#include <string.h>
#include "../components.h"
// clang-format on

#pragma comment(lib, "setupapi.lib")

static int contains_ven_10de(const char *s) {
  if (!s)
    return 0;
  const char *p = s;
  while (*p) {
    if ((p[0] == 'V' || p[0] == 'v') && (p[1] == 'E' || p[1] == 'e') &&
        (p[2] == 'N' || p[2] == 'n') && p[3] == '_' && p[4] == '1' &&
        p[5] == '0' && p[6] == 'D' && (p[7] == 'E' || p[7] == 'e')) {
      return 1;
    }
    p++;
  }
  return 0;
}

int is_gpu_nvidia_by_pci_vendor(void) {
  HDEVINFO h =
      SetupDiGetClassDevsA(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);
  if (h == INVALID_HANDLE_VALUE)
    return 0;

  SP_DEVINFO_DATA dev;
  dev.cbSize = sizeof(dev);

  for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &dev); i++) {
    char hwids[4096];
    DWORD regType = 0, size = 0;

    if (SetupDiGetDeviceRegistryPropertyA(h, &dev, SPDRP_HARDWAREID, &regType,
                                          (PBYTE)hwids, (DWORD)sizeof(hwids),
                                          &size)) {
      // SPDRP_HARDWAREID — это MULTI_SZ (несколько строк подряд)
      for (const char *p = hwids; *p; p += (strlen(p) + 1)) {
        if (contains_ven_10de(p)) {
          SetupDiDestroyDeviceInfoList(h);
          return 1;
        }
      }
    }
  }

  SetupDiDestroyDeviceInfoList(h);
  return 0;
}

/* ── Basic (vendor-agnostic) display adapter enumeration ───────────────── */

/* Parse a "KEY_XXXX" hex field from a PnP hardware ID such as
   "PCI\VEN_10DE&DEV_2560&SUBSYS_3F8317AA&REV_A1". Returns 0 if absent. */
static unsigned hwid_hex_field(const char *id, const char *key) {
  const size_t kl = strlen(key);
  const char *p = id;
  while ((p = strstr(p, key)) != NULL) {
    if (p == id || p[-1] == '\\' || p[-1] == '&') {
      unsigned v = 0;
      if (sscanf(p + kl, "%x", &v) == 1)
        return v;
    }
    p += kl;
  }
  return 0;
}

static void reg_read_sz(HKEY hk, const char *name, char *out, DWORD sz) {
  DWORD type = 0, cb = sz;
  if (RegQueryValueExA(hk, name, NULL, &type, (LPBYTE)out, &cb) != ERROR_SUCCESS ||
      type != REG_SZ)
    out[0] = '\0';
  else
    out[sz - 1] = '\0';
}

int enum_basic_display_adapters(GpuInfo *out, int max_out, int skip_nvidia) {
  if (!out || max_out <= 0)
    return 0;

  HDEVINFO h =
      SetupDiGetClassDevsA(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);
  if (h == INVALID_HANDLE_VALUE)
    return 0;

  int n = 0;
  SP_DEVINFO_DATA dev;
  dev.cbSize = sizeof(dev);

  for (DWORD i = 0; n < max_out && SetupDiEnumDeviceInfo(h, i, &dev); i++) {
    char hwids[4096];
    DWORD regType = 0, size = 0;

    if (!SetupDiGetDeviceRegistryPropertyA(h, &dev, SPDRP_HARDWAREID, &regType,
                                           (PBYTE)hwids, (DWORD)sizeof(hwids),
                                           &size))
      continue;

    /* Only physical PCI adapters — skips virtual / root-enumerated displays
       (RDP, Hyper-V, IddCx virtual monitors, ...). First MULTI_SZ entry is
       the most specific hardware ID. */
    if (_strnicmp(hwids, "PCI\\", 4) != 0)
      continue;

    unsigned ven = hwid_hex_field(hwids, "VEN_");
    if (!ven)
      continue;
    if (skip_nvidia && ven == 0x10DE)
      continue;

    GpuInfo *g = &out[n];
    memset(g, 0, sizeof(*g));
    g->is_nvidia   = 0;
    g->nvapi_index = -1;
    g->ven = ven;
    g->dev = hwid_hex_field(hwids, "DEV_");
    g->rev = hwid_hex_field(hwids, "REV_");
    {
      /* PnP SUBSYS_ddddvvvv → stored as (subvendor << 16) | subdevice,
         the same layout the NVAPI path produces. */
      unsigned ss = hwid_hex_field(hwids, "SUBSYS_");
      g->subsys = ((ss & 0xFFFFu) << 16) | ((ss >> 16) & 0xFFFFu);
    }

    char desc[256] = {0};
    if (SetupDiGetDeviceRegistryPropertyA(h, &dev, SPDRP_DEVICEDESC, &regType,
                                          (PBYTE)desc, (DWORD)sizeof(desc),
                                          &size) && desc[0])
      snprintf(g->name, sizeof(g->name), "%.*s", (int)sizeof(g->name) - 1, desc);
    else
      snprintf(g->name, sizeof(g->name), "Display adapter %04X:%04X", ven, g->dev);

    /* Driver version / date from the device's class-driver registry key:
       SPDRP_DRIVER = "{4d36e968-...}\0001"                                */
    char drvkey[256] = {0};
    if (SetupDiGetDeviceRegistryPropertyA(h, &dev, SPDRP_DRIVER, &regType,
                                          (PBYTE)drvkey, (DWORD)sizeof(drvkey),
                                          &size) && drvkey[0]) {
      char path[384];
      snprintf(path, sizeof(path),
               "SYSTEM\\CurrentControlSet\\Control\\Class\\%s", drvkey);
      HKEY hk;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        reg_read_sz(hk, "DriverVersion", g->drv_version, (DWORD)sizeof(g->drv_version));
        reg_read_sz(hk, "DriverDate",    g->drv_date,    (DWORD)sizeof(g->drv_date));
        RegCloseKey(hk);
      }
    }

    n++;
  }

  SetupDiDestroyDeviceInfoList(h);
  return n;
}
