#include "nvapi_dyn.h"

typedef void *(__cdecl *NvAPI_QueryInterface_t)(unsigned int);

static HMODULE g_nvapi = NULL;
static NvAPI_QueryInterface_t g_qi = NULL;

static void *qi(unsigned int id);

/* exported pointers */
NvAPI_Status(__cdecl *pNvAPI_Initialize)(void) = NULL;
NvAPI_Status(__cdecl *pNvAPI_Unload)(void) = NULL;
NvAPI_Status(__cdecl *pNvAPI_EnumPhysicalGPUs)(
    NvPhysicalGpuHandle[NVAPI_MAX_PHYSICAL_GPUS], NvU32 *) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GPU_GetFullName)(NvPhysicalGpuHandle,
                                              NvAPI_ShortString) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GetErrorMessage)(NvAPI_Status,
                                              NvAPI_ShortString) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GPU_GetMemoryInfo)(
    NvPhysicalGpuHandle, NV_DISPLAY_DRIVER_MEMORY_INFO *) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GPU_GetPCIIdentifiers)(NvPhysicalGpuHandle,
                                                    NvU32 *, NvU32 *, NvU32 *,
                                                    NvU32 *) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GPU_GetAllClockFrequencies)(
    NvPhysicalGpuHandle, NV_GPU_CLOCK_FREQUENCIES *) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GPU_GetRamType)(NvPhysicalGpuHandle,
                                             NvU32 *) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GPU_GetCurrentPCIEDownstreamWidth)(
    NvPhysicalGpuHandle, NvU32 *) = NULL;
NvAPI_Status(__cdecl *pNvAPI_GPU_GetThermalSettings)(
    NvPhysicalGpuHandle, NvU32, NV_GPU_THERMAL_SETTINGS *) = NULL;

/* NVAPI interface IDs */
#define ID_NvAPI_Initialize 0x0150E828u
#define ID_NvAPI_Unload 0xD22BDD7Eu
#define ID_NvAPI_EnumPhysicalGPUs 0xE5AC921Fu
#define ID_NvAPI_GPU_GetFullName 0xCEEE8E9Fu
#define ID_NvAPI_GetErrorMessage 0x6C2D048Cu
#define ID_NvAPI_GPU_GetMemoryInfo 0x07F9B368u
#define ID_NvAPI_GPU_GetPCIIdentifiers 0x2DDFB66Eu
#define ID_NvAPI_GPU_GetAllClockFrequencies 0xDCB616C3u
#define ID_NvAPI_GPU_GetRamType 0x57F7CAACu
#define ID_NvAPI_GPU_GetCurrentPCIEDownstreamWidth 0xD048C3B1u
#define ID_NvAPI_GPU_GetThermalSettings 0xE3640A56u

int nvapi_dyn_load(void) {
  g_nvapi = LoadLibraryA("nvapi64.dll");
  if (!g_nvapi)
    return 0;

  g_qi =
      (NvAPI_QueryInterface_t)GetProcAddress(g_nvapi, "nvapi_QueryInterface");

  if (!g_qi)
    return 0;

  pNvAPI_Initialize = g_qi(ID_NvAPI_Initialize);
  pNvAPI_Unload = g_qi(ID_NvAPI_Unload);
  pNvAPI_EnumPhysicalGPUs = g_qi(ID_NvAPI_EnumPhysicalGPUs);
  pNvAPI_GPU_GetFullName = g_qi(ID_NvAPI_GPU_GetFullName);
  pNvAPI_GetErrorMessage = (NvAPI_Status(__cdecl *)(
      NvAPI_Status, NvAPI_ShortString))qi(ID_NvAPI_GetErrorMessage);
  pNvAPI_GPU_GetMemoryInfo = (NvAPI_Status(__cdecl *)(
      NvPhysicalGpuHandle,
      NV_DISPLAY_DRIVER_MEMORY_INFO *))qi(ID_NvAPI_GPU_GetMemoryInfo);
  pNvAPI_GPU_GetPCIIdentifiers =
      (NvAPI_Status(__cdecl *)(NvPhysicalGpuHandle, NvU32 *, NvU32 *, NvU32 *,
                               NvU32 *))qi(ID_NvAPI_GPU_GetPCIIdentifiers);
  pNvAPI_GPU_GetAllClockFrequencies =
      (NvAPI_Status(__cdecl *)(NvPhysicalGpuHandle, NV_GPU_CLOCK_FREQUENCIES *))
          qi(ID_NvAPI_GPU_GetAllClockFrequencies);
  pNvAPI_GPU_GetRamType = (NvAPI_Status(__cdecl *)(
      NvPhysicalGpuHandle, NvU32 *))qi(ID_NvAPI_GPU_GetRamType);
  pNvAPI_GPU_GetCurrentPCIEDownstreamWidth =
      (NvAPI_Status(__cdecl *)(NvPhysicalGpuHandle, NvU32 *))qi(
          ID_NvAPI_GPU_GetCurrentPCIEDownstreamWidth);
  pNvAPI_GPU_GetThermalSettings = (NvAPI_Status(__cdecl *)(
      NvPhysicalGpuHandle, NvU32,
      NV_GPU_THERMAL_SETTINGS *))qi(ID_NvAPI_GPU_GetThermalSettings);

  return pNvAPI_Initialize && pNvAPI_Unload && pNvAPI_EnumPhysicalGPUs &&
         pNvAPI_GPU_GetFullName && pNvAPI_GetErrorMessage &&
         pNvAPI_GPU_GetMemoryInfo && pNvAPI_GPU_GetPCIIdentifiers &&
         pNvAPI_GPU_GetAllClockFrequencies && pNvAPI_GPU_GetRamType &&
         pNvAPI_GPU_GetCurrentPCIEDownstreamWidth &&
         pNvAPI_GPU_GetThermalSettings;
}

void nvapi_dyn_unload(void) {
  if (g_nvapi)
    FreeLibrary(g_nvapi);
  g_nvapi = NULL;
}

static void *qi(unsigned int id) { return g_qi ? g_qi(id) : NULL; }
