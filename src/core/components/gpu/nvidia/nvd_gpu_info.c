// nvd_gpu_info.c
#include "nvapi_dyn.h"
#include <stdio.h>
#include <string.h>


static int nvd_init_first_gpu(NvPhysicalGpuHandle *out_gpu) {
  if (!out_gpu)
    return 0;

  if (!nvapi_dyn_load())
    return 0;

  if (pNvAPI_Initialize() != NVAPI_OK) {
    nvapi_dyn_unload();
    return 0;
  }

  NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS] = {0};
  NvU32 count = 0;

  if (pNvAPI_EnumPhysicalGPUs(gpus, &count) != NVAPI_OK || count == 0) {
    pNvAPI_Unload();
    nvapi_dyn_unload();
    return 0;
  }

  *out_gpu = gpus[0];
  return 1;
}

static void nvd_shutdown(void) {
  pNvAPI_Unload();
  nvapi_dyn_unload();
}

int get_nvd_gpu_name(char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return 0;

  NvPhysicalGpuHandle gpu;

  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NvAPI_ShortString name = {0};
  NvAPI_Status st = pNvAPI_GPU_GetFullName(gpu, name);

  if (st != NVAPI_OK) {
    NvAPI_ShortString err = {0};
    if (pNvAPI_GetErrorMessage)
      pNvAPI_GetErrorMessage(st, err);

    printf("[NVAPI] GetFullName failed: %s\n", err);
    nvd_shutdown();
    return 0;
  }

  strncpy(out, name, out_sz - 1);
  out[out_sz - 1] = '\0';

  nvd_shutdown();
  return 1;
}

int get_nvd_gpu_device_id(unsigned int *vendor_id, unsigned int *device_id,
                          unsigned int *subsystem_id,
                          unsigned int *revision_id) {
  if (!vendor_id || !device_id || !subsystem_id || !revision_id)
    return 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NvU32 dev_id = 0;
  NvU32 subsys = 0;
  NvU32 rev = 0;
  NvU32 ext_id = 0;

  NvAPI_Status st =
      pNvAPI_GPU_GetPCIIdentifiers(gpu, &dev_id, &subsys, &rev, &ext_id);
  if (st != NVAPI_OK) {
    nvd_shutdown();
    return 0;
  }

  *vendor_id = (unsigned)(dev_id & 0xFFFF);
  *device_id = (unsigned)((dev_id >> 16) & 0xFFFF);

  unsigned subven = (unsigned)(subsys & 0xFFFF);
  unsigned subdev = (unsigned)((subsys >> 16) & 0xFFFF);
  *subsystem_id = (subven << 16) | subdev;

  *revision_id = (unsigned)(rev & 0xFF);

  nvd_shutdown();
  return 1;
}

static int nvd_get_core_clock_mhz(NvPhysicalGpuHandle gpu, NvU32 clock_type,
                                  unsigned int *out_mhz) {
  if (!out_mhz)
    return 0;

  NV_GPU_CLOCK_FREQUENCIES f;
  memset(&f, 0, sizeof(f));
  f.version = NV_GPU_CLOCK_FREQUENCIES_VER;

  /* В твоём nvapi.h поле может называться ClockType или clockType.
     Оставь ОДНУ строку — ту, что компилируется. */
  f.ClockType = clock_type;       /* вариант 1 */
  /* f.clockType = clock_type; */ /* вариант 2 */

  NvAPI_Status st = pNvAPI_GPU_GetAllClockFrequencies(gpu, &f);
  if (st != NVAPI_OK)
    return 0;

  if (!f.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent)
    return 0;

  /* frequency в kHz */
  *out_mhz =
      (unsigned int)(f.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency /
                     1000);
  return 1;
}

int get_nvd_gpu_core_base_boost_mhz(unsigned int *out_base_mhz,
                                    unsigned int *out_boost_mhz) {
  if (!out_base_mhz || !out_boost_mhz)
    return 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  /* Эти enum’ы обычно определены в nvapi.h */
  int ok_base = nvd_get_core_clock_mhz(gpu, NV_GPU_CLOCK_FREQUENCIES_BASE_CLOCK,
                                       out_base_mhz);
  int ok_boost = nvd_get_core_clock_mhz(
      gpu, NV_GPU_CLOCK_FREQUENCIES_BOOST_CLOCK, out_boost_mhz);

  nvd_shutdown();
  return ok_base && ok_boost;
}

int get_nvd_gpu_mem_mhz(unsigned int *out_base_mhz) {
  if (!out_base_mhz)
    return 0;
  *out_base_mhz = 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NV_GPU_CLOCK_FREQUENCIES f;
  memset(&f, 0, sizeof(f));

#ifdef NV_GPU_CLOCK_FREQUENCIES_VER_2
  f.version = NV_GPU_CLOCK_FREQUENCIES_VER_2;
#else
  f.version = NV_GPU_CLOCK_FREQUENCIES_VER;
#endif

  f.ClockType = NV_GPU_CLOCK_FREQUENCIES_BASE_CLOCK;

  NvAPI_Status st = pNvAPI_GPU_GetAllClockFrequencies(gpu, &f);
  if (st != NVAPI_OK) {
    nvd_shutdown();
    return 0;
  }

  if (!f.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].bIsPresent) {
    nvd_shutdown();
    return 0;
  }

  *out_base_mhz =
      ((unsigned int)(f.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency /
                      1000) +
       2) /
      4;

  nvd_shutdown();
  return 1;
}

static const char *nvd_ram_type_to_str(NvU32 t) {
  switch (t) {
  case 0:
    return "UNKNOWN";
  case 1:
    return "SDRAM";
  case 2:
    return "DDR1";
  case 3:
    return "DDR2";
  case 4:
    return "GDDR2";
  case 5:
    return "GDDR3";
  case 6:
    return "GDDR4";
  case 7:
    return "DDR3";
  case 8:
    return "GDDR5";
  case 9:
    return "LPDDR2";
  case 10:
    return "GDDR5X";
  case 14:
    return "GDDR6";
  case 15:
    return "GDDR6X";
  default:
    return NULL;
  }
}

int get_nvd_gpu_vram_type(char *out, size_t out_sz) {
  if (!out || out_sz == 0)
    return 0;
  out[0] = '\0';

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  if (!pNvAPI_GPU_GetRamType) {
    nvd_shutdown();
    return 0;
  }

  NvU32 ramType = 0;
  NvAPI_Status st = pNvAPI_GPU_GetRamType(gpu, &ramType);
  if (st != NVAPI_OK) {
    nvd_shutdown();
    return 0;
  }

  const char *s = nvd_ram_type_to_str(ramType);
  if (s) {
    strncpy(out, s, out_sz - 1);
    out[out_sz - 1] = '\0';
  } else {
    /* на будущее: если придёт неизвестный код */
    snprintf(out, out_sz, "RAM(%u)", (unsigned)ramType);
  }

  nvd_shutdown();
  return 1;
}

/* Возвращает текущую ширину PCIe линка: 1/4/8/16 и т.д. */
int get_nvd_gpu_pcie_lanes(unsigned *out_lanes) {
  if (!out_lanes)
    return 0;
  *out_lanes = 0;

  if (!pNvAPI_GPU_GetCurrentPCIEDownstreamWidth)
    return 0; /* не экспортнули/не загрузили указатель */

  NvPhysicalGpuHandle gpu = NULL;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NvU32 w = 0;
  NvAPI_Status st = pNvAPI_GPU_GetCurrentPCIEDownstreamWidth(gpu, &w);
  if (st != NVAPI_OK) {
    if (pNvAPI_GetErrorMessage) {
      NvAPI_ShortString err = {0};
      pNvAPI_GetErrorMessage(st, err);
      printf("[NVAPI] GetCurrentPCIEDownstreamWidth failed: 0x%X (%s)\n",
             (unsigned)st, err);
    } else {
      printf("[NVAPI] GetCurrentPCIEDownstreamWidth failed: 0x%X\n",
             (unsigned)st);
    }
    nvd_shutdown();
    return 0;
  }

  *out_lanes = (unsigned)w;
  nvd_shutdown();
  return 1;
}

int get_nvd_gpu_core_temp(int *out_temp_c) {
  if (!out_temp_c)
    return 0;
  *out_temp_c = 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NV_GPU_THERMAL_SETTINGS ts;
  memset(&ts, 0, sizeof(ts));

#ifdef NV_GPU_THERMAL_SETTINGS_VER_2
  ts.version = NV_GPU_THERMAL_SETTINGS_VER_2;
#else
  ts.version = NV_GPU_THERMAL_SETTINGS_VER;
#endif

  NvAPI_Status st = pNvAPI_GPU_GetThermalSettings(gpu, 0, &ts); /* <-- st тут */
  if (st != NVAPI_OK) {
    NvAPI_ShortString err = {0};
    if (pNvAPI_GetErrorMessage)
      pNvAPI_GetErrorMessage(st, err);
    printf("[NVAPI] GetThermalSettings failed: 0x%X (%s)\n", (unsigned)st, err);
    nvd_shutdown();
    return 0;
  }

  int temp = -1;

#ifdef NVAPI_THERMAL_TARGET_GPU
  for (NvU32 i = 0; i < ts.count; ++i) {
    if (ts.sensor[i].target == NVAPI_THERMAL_TARGET_GPU) {
      temp = (int)ts.sensor[i].currentTemp;
      break;
    }
  }
#endif

  if (temp < 0 && ts.count > 0)
    temp = (int)ts.sensor[0].currentTemp;

  nvd_shutdown();

  if (temp < 0)
    return 0;
  *out_temp_c = temp;
  return 1;
}

int get_nvd_gpu_mem_temp(int *out_temp_c) {
  if (!out_temp_c)
    return 0;
  *out_temp_c = 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NV_GPU_THERMAL_SETTINGS ts;
  memset(&ts, 0, sizeof(ts));

#ifdef NV_GPU_THERMAL_SETTINGS_VER_2
  ts.version = NV_GPU_THERMAL_SETTINGS_VER_2;
#else
  ts.version = NV_GPU_THERMAL_SETTINGS_VER;
#endif

  NvAPI_Status st =
      pNvAPI_GPU_GetThermalSettings(gpu, NVAPI_THERMAL_TARGET_ALL, &ts);
  if (st != NVAPI_OK) {
    nvd_shutdown();
    return 0;
  }

#if defined(NVAPI_THERMAL_TARGET_MEMORY)
#define NVD_MEM_TARGET NVAPI_THERMAL_TARGET_MEMORY
#elif defined(NV_THERMAL_TARGET_MEMORY)
#define NVD_MEM_TARGET NV_THERMAL_TARGET_MEMORY
#endif

#ifdef NVD_MEM_TARGET
  for (NvU32 i = 0; i < ts.count; ++i) {
    if ((int)ts.sensor[i].target == (int)NVD_MEM_TARGET) {
      *out_temp_c = (int)ts.sensor[i].currentTemp;
      nvd_shutdown();
#undef NVD_MEM_TARGET
      return 1;
    }
  }
#undef NVD_MEM_TARGET
  nvd_shutdown();
  return 0;
#else
  /* В твоём nvapi.h нет “MEMORY” таргета — датчик VRAM через NVAPI недоступен
   */
  nvd_shutdown();
  return 0;
#endif
}

/* CURRENT clock type: разные nvapi.h могут называть по-разному */
#if defined(NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ)
#define NVD_CLOCK_CURRENT NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ
#elif defined(NV_GPU_CLOCK_FREQUENCIES_CLOCK_TYPE_CURRENT_FREQ)
#define NVD_CLOCK_CURRENT NV_GPU_CLOCK_FREQUENCIES_CLOCK_TYPE_CURRENT_FREQ
#else
#define NVD_CLOCK_CURRENT 0
#endif

int get_nvd_gpu_current_core_mem_mhz(unsigned int *out_core_mhz,
                                     unsigned int *out_mem_mhz) {
  if (!out_core_mhz || !out_mem_mhz)
    return 0;
  *out_core_mhz = 0;
  *out_mem_mhz = 0;

  if (!pNvAPI_GPU_GetAllClockFrequencies)
    return 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NV_GPU_CLOCK_FREQUENCIES f;
  memset(&f, 0, sizeof(f));
#ifdef NV_GPU_CLOCK_FREQUENCIES_VER_2
  f.version = NV_GPU_CLOCK_FREQUENCIES_VER_2;
#else
  f.version = NV_GPU_CLOCK_FREQUENCIES_VER;
#endif

  /* оставь только ту строку, которая есть в твоём nvapi.h */
  f.ClockType = NVD_CLOCK_CURRENT;
  /* f.clockType = NVD_CLOCK_CURRENT; */

  NvAPI_Status st = pNvAPI_GPU_GetAllClockFrequencies(gpu, &f);
  if (st != NVAPI_OK) {
    if (pNvAPI_GetErrorMessage) {
      NvAPI_ShortString err = {0};
      pNvAPI_GetErrorMessage(st, err);
      printf("[NVAPI] GetAllClockFrequencies failed: 0x%X (%s)\n", (unsigned)st,
             err);
    }
    nvd_shutdown();
    return 0;
  }

  int ok = 0;

  /* CORE (Graphics) */
  if (f.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent) {
    *out_core_mhz =
        (unsigned int)(f.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency /
                       1000);
    ok = 1;
  }

  /* MEMORY (VRAM) */
  if (f.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].bIsPresent) {
    unsigned int mem_effective_mhz =
        (unsigned int)(f.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency /
                       1000);

    *out_mem_mhz = (mem_effective_mhz + 2) / 4;
    ok = 1;
  }

  nvd_shutdown();
  return ok;
}

static void nvd_read_reg_strA(HKEY hKey, const char *name, char *out, DWORD sz) {
  DWORD type = 0, cb = sz;
  if (RegQueryValueExA(hKey, name, NULL, &type, (LPBYTE)out, &cb) != ERROR_SUCCESS
      || type != REG_SZ)
    out[0] = '\0';
}

static void nvd_wide_to_utf8(const wchar_t *w, char *out, int outsz) {
  WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outsz, NULL, NULL);
}

int get_nvd_gpu_driver_info(char *ver,  size_t ver_sz,
                             char *date, size_t date_sz) {
  if (ver)  ver[0]  = '\0';
  if (date) date[0] = '\0';

  /* 1. Driver version via NvAPI_SYS_GetDisplayDriverInfo */
  if (ver && ver_sz) {
    if (nvapi_dyn_load()) {
      if (pNvAPI_SYS_GetDisplayDriverInfo && pNvAPI_Initialize() == NVAPI_OK) {
        NV_DISPLAY_DRIVER_INFO info;
        memset(&info, 0, sizeof(info));
        info.version = NV_DISPLAY_DRIVER_INFO_VER;
        if (pNvAPI_SYS_GetDisplayDriverInfo(&info) == NVAPI_OK)
          snprintf(ver, ver_sz, "%u.%02u",
                   info.driverVersion / 100, info.driverVersion % 100);
        pNvAPI_Unload();
      }
      nvapi_dyn_unload();
    }
  }

  /* 2. Driver date from display-adapter class registry key */
  if (date && date_sz) {
    static const char *cls =
        "SYSTEM\\CurrentControlSet\\Control\\Class\\"
        "{4d36e968-e325-11ce-bfc1-08002be10318}";
    HKEY hClass;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, cls, 0, KEY_READ, &hClass) == ERROR_SUCCESS) {
      char subname[8];
      for (DWORD i = 0; i < 16; i++) {
        snprintf(subname, sizeof(subname), "%04lu", i);
        HKEY hSub;
        if (RegOpenKeyExA(hClass, subname, 0, KEY_READ, &hSub) != ERROR_SUCCESS)
          break;
        char provider[64] = {0};
        nvd_read_reg_strA(hSub, "ProviderName", provider, sizeof(provider));
        if (_stricmp(provider, "NVIDIA") == 0) {
          nvd_read_reg_strA(hSub, "DriverDate", date, (DWORD)date_sz);
          RegCloseKey(hSub);
          break;
        }
        RegCloseKey(hSub);
      }
      RegCloseKey(hClass);
    }
  }

  return 1;
}

static const char *nvd_arch_name(NvU32 arch_id) {
  switch (arch_id) {
  case NV_GPU_ARCHITECTURE_GF100:
  case NV_GPU_ARCHITECTURE_GF110: return "Fermi";
  case NV_GPU_ARCHITECTURE_GK100:
  case NV_GPU_ARCHITECTURE_GK110:
  case NV_GPU_ARCHITECTURE_GK200: return "Kepler";
  case NV_GPU_ARCHITECTURE_GM000:
  case NV_GPU_ARCHITECTURE_GM200: return "Maxwell";
  case NV_GPU_ARCHITECTURE_GP100: return "Pascal";
  case NV_GPU_ARCHITECTURE_GV100: return "Volta";
  case NV_GPU_ARCHITECTURE_TU100: return "Turing";
  case NV_GPU_ARCHITECTURE_GA100: return "Ampere";
  case NV_GPU_ARCHITECTURE_AD100: return "Ada Lovelace";
  case NV_GPU_ARCHITECTURE_GB200: return "Blackwell";
  default:                        return "";
  }
}

static const char *nvd_process_node(NvU32 arch_id) {
  switch (arch_id) {
  case NV_GPU_ARCHITECTURE_GF100:
  case NV_GPU_ARCHITECTURE_GF110: return "40 nm";
  case NV_GPU_ARCHITECTURE_GK100:
  case NV_GPU_ARCHITECTURE_GK110:
  case NV_GPU_ARCHITECTURE_GK200:
  case NV_GPU_ARCHITECTURE_GM000:
  case NV_GPU_ARCHITECTURE_GM200: return "28 nm";
  case NV_GPU_ARCHITECTURE_GP100: return "16 nm";
  case NV_GPU_ARCHITECTURE_GV100:
  case NV_GPU_ARCHITECTURE_TU100: return "12 nm";
  case NV_GPU_ARCHITECTURE_GA100: return "8 nm";
  case NV_GPU_ARCHITECTURE_AD100:
  case NV_GPU_ARCHITECTURE_GB200: return "4 nm";
  default:                        return "";
  }
}

static void nvd_build_die_name(NvU32 arch_id, NvU32 impl_id, char *out, size_t sz) {
  const char *prefix = "";
  int digit = -1;

  switch (arch_id) {
  case NV_GPU_ARCHITECTURE_GP100:
    prefix = "GP";
    switch (impl_id) {
    case NV_GPU_ARCH_IMPLEMENTATION_GP100: digit = 100; break;
    case NV_GPU_ARCH_IMPLEMENTATION_GP102: digit = 102; break;
    case NV_GPU_ARCH_IMPLEMENTATION_GP104: digit = 104; break;
    case NV_GPU_ARCH_IMPLEMENTATION_GP106: digit = 106; break;
    case NV_GPU_ARCH_IMPLEMENTATION_GP107: digit = 107; break;
    case NV_GPU_ARCH_IMPLEMENTATION_GP108: digit = 108; break;
    }
    break;
  case NV_GPU_ARCHITECTURE_GV100:
    prefix = "GV";
    if (impl_id == NV_GPU_ARCH_IMPLEMENTATION_GV100) digit = 100;
    break;
  case NV_GPU_ARCHITECTURE_TU100:
    prefix = "TU";
    switch (impl_id) {
    case NV_GPU_ARCH_IMPLEMENTATION_TU100: digit = 100; break;
    case NV_GPU_ARCH_IMPLEMENTATION_TU102: digit = 102; break;
    case NV_GPU_ARCH_IMPLEMENTATION_TU104: digit = 104; break;
    case NV_GPU_ARCH_IMPLEMENTATION_TU106: digit = 106; break;
    case NV_GPU_ARCH_IMPLEMENTATION_TU116: digit = 116; break;
    case NV_GPU_ARCH_IMPLEMENTATION_TU117: digit = 117; break;
    }
    break;
  case NV_GPU_ARCHITECTURE_GA100:
    prefix = "GA";
    switch (impl_id) {
    case NV_GPU_ARCH_IMPLEMENTATION_GA100: digit = 100; break;
    case NV_GPU_ARCH_IMPLEMENTATION_GA102: digit = 102; break;
    case NV_GPU_ARCH_IMPLEMENTATION_GA104: digit = 104; break;
    case 0x06: digit = 106; break;
    case 0x07: digit = 107; break;
    case 0x08: digit = 108; break;
    }
    break;
  case NV_GPU_ARCHITECTURE_AD100:
    prefix = "AD";
    switch (impl_id) {
    case NV_GPU_ARCH_IMPLEMENTATION_AD102: digit = 102; break;
    case NV_GPU_ARCH_IMPLEMENTATION_AD103: digit = 103; break;
    case NV_GPU_ARCH_IMPLEMENTATION_AD104: digit = 104; break;
    case 0x06: digit = 106; break;
    case 0x07: digit = 107; break;
    }
    break;
  case NV_GPU_ARCHITECTURE_GB200:
    prefix = "GB";
    switch (impl_id) {
    case NV_GPU_ARCH_IMPLEMENTATION_GB202: digit = 202; break;
    case 0x03: digit = 203; break;
    case 0x04: digit = 204; break;
    case 0x06: digit = 206; break;
    }
    break;
  }

  if (prefix[0] && digit >= 0)
    snprintf(out, sz, "%s%d", prefix, digit);
  else
    out[0] = '\0';
}

int get_nvd_gpu_vram_bus_bits(const char *die_name, unsigned int *out_bits) {
  if (!die_name || !out_bits) return 0;
  static const struct { const char *die; unsigned int bits; } tbl[] = {
    /* Fermi */   {"GF100",512}, {"GF104",256}, {"GF106",192}, {"GF108",128}, {"GF110",384}, {"GF114",256}, {"GF116",192},
    /* Kepler */  {"GK104",256}, {"GK106",192}, {"GK107",128}, {"GK110",384}, {"GK208",64},
    /* Maxwell */ {"GM107",128}, {"GM200",384}, {"GM204",256}, {"GM206",128},
    /* Pascal */  {"GP100",4096},{"GP102",384}, {"GP104",256}, {"GP106",192}, {"GP107",128}, {"GP108",32},
    /* Turing */  {"TU102",352}, {"TU104",256}, {"TU106",192}, {"TU116",192}, {"TU117",128},
    /* Ampere */  {"GA100",6144},{"GA102",384}, {"GA103",256}, {"GA104",256}, {"GA106",192}, {"GA107",128},
    /* Ada */     {"AD102",384}, {"AD103",256}, {"AD104",192}, {"AD106",128}, {"AD107",128},
    /* Blackwell*/{"GB202",512}, {"GB203",256}, {"GB205",192}, {"GB206",128}, {"GB207",128},
  };
  for (int i = 0; i < (int)(sizeof(tbl)/sizeof(tbl[0])); i++) {
    if (_stricmp(die_name, tbl[i].die) == 0) {
      *out_bits = tbl[i].bits;
      return 1;
    }
  }
  return 0;
}

int get_nvd_gpu_arch_info(char *arch, size_t arch_sz,
                          char *die,  size_t die_sz,
                          char *process_node, size_t pn_sz) {
  if (!arch || !die || !process_node)
    return 0;
  arch[0] = die[0] = process_node[0] = '\0';

  if (!pNvAPI_GPU_GetArchInfo)
    return 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NV_GPU_ARCH_INFO info;
  memset(&info, 0, sizeof(info));
  info.version = NV_GPU_ARCH_INFO_VER;

  NvAPI_Status st = pNvAPI_GPU_GetArchInfo(gpu, &info);
  nvd_shutdown();
  if (st != NVAPI_OK)
    return 0;

  const char *aname = nvd_arch_name(info.architecture_id);
  if (aname[0])
    strncpy(arch, aname, arch_sz - 1);

  nvd_build_die_name(info.architecture_id, info.implementation_id, die, die_sz);

  const char *pnode = nvd_process_node(info.architecture_id);
  if (pnode[0])
    strncpy(process_node, pnode, pn_sz - 1);

  return 1;
}

int get_nvd_gpu_vram_snapshot(unsigned *out_used_mb, unsigned *out_total_mb,
                              double *out_percent) {
  if (!out_used_mb || !out_total_mb || !out_percent)
    return 0;
  *out_used_mb = *out_total_mb = *out_percent = 0;

  if (!pNvAPI_GPU_GetMemoryInfo)
    return 0;

  NvPhysicalGpuHandle gpu;
  if (!nvd_init_first_gpu(&gpu))
    return 0;

  NV_DISPLAY_DRIVER_MEMORY_INFO mem;
  memset(&mem, 0, sizeof(mem));
  mem.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;

  NvAPI_Status st = pNvAPI_GPU_GetMemoryInfo(gpu, &mem);
  if (st != NVAPI_OK) {
    nvd_shutdown();
    return 0;
  }

  NvU32 total_kb = mem.dedicatedVideoMemory;
  NvU32 free_kb = mem.curAvailableDedicatedVideoMemory;
  NvU32 used_kb = (free_kb <= total_kb) ? (total_kb - free_kb) : 0;

  *out_total_mb = (unsigned)(total_kb / 1024);
  *out_used_mb = (unsigned)(used_kb / 1024);

  if (total_kb) {
    double pct = (double)((used_kb * 100u + total_kb / 2u) / total_kb);
    if (pct > 100u)
      pct = 100u;
    *out_percent = pct;
  }

  nvd_shutdown();
  return 1;
}
