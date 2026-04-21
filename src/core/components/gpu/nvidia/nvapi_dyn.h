#pragma once
// clang-format off
#include <windows.h>
#include "nvapi_mingw.h"
// clang-format on

#ifdef __cplusplus
extern "C"
{
#endif

    int nvapi_dyn_load(void);
    void nvapi_dyn_unload(void);

    /* указатели на NVAPI */
    extern NvAPI_Status(__cdecl *pNvAPI_Initialize)(void);
    extern NvAPI_Status(__cdecl *pNvAPI_Unload)(void);
    extern NvAPI_Status(__cdecl *pNvAPI_EnumPhysicalGPUs)(
        NvPhysicalGpuHandle[NVAPI_MAX_PHYSICAL_GPUS],
        NvU32 *);

    extern NvAPI_Status(__cdecl *pNvAPI_GPU_GetFullName)(
        NvPhysicalGpuHandle,
        NvAPI_ShortString);
    extern NvAPI_Status(__cdecl *pNvAPI_GetErrorMessage)(NvAPI_Status nr, NvAPI_ShortString szDesc);
    extern NvAPI_Status(__cdecl *pNvAPI_GPU_GetMemoryInfo)(NvPhysicalGpuHandle, NV_DISPLAY_DRIVER_MEMORY_INFO *);
    extern NvAPI_Status(__cdecl *pNvAPI_GPU_GetPCIIdentifiers)(
        NvPhysicalGpuHandle hPhysicalGpu,
        NvU32 *pDeviceId,
        NvU32 *pSubSystemId,
        NvU32 *pRevisionId,
        NvU32 *pExtDeviceId);
    extern NvAPI_Status(__cdecl *pNvAPI_GPU_GetAllClockFrequencies)(NvPhysicalGpuHandle, NV_GPU_CLOCK_FREQUENCIES *);
    extern NvAPI_Status(__cdecl *pNvAPI_GPU_GetRamType)(NvPhysicalGpuHandle, NvU32 *);
    extern NvAPI_Status(__cdecl *pNvAPI_GPU_GetCurrentPCIEDownstreamWidth)(NvPhysicalGpuHandle hPhysicalGpu, NvU32 *pWidth);
    extern NvAPI_Status (__cdecl *pNvAPI_GPU_GetThermalSettings)(NvPhysicalGpuHandle hPhysicalGpu, NvU32 sensorIndex, NV_GPU_THERMAL_SETTINGS *pThermalSettings);


#ifdef __cplusplus
}
#endif
