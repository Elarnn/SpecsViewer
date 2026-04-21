// gpu_check.c
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <string.h>
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
