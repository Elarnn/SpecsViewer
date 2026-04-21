#define _WIN32_DCOM
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS

#include <windows.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <stdlib.h>
#include <stdint.h>

unsigned long long get_ram_total_mb(void)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return 0;

    return ms.ullTotalPhys / (1024ULL * 1024ULL);
}

unsigned long long get_ram_used_mb(void)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms))
        return 0;

    unsigned long long used = ms.ullTotalPhys - ms.ullAvailPhys;
    return used / (1024ULL * 1024ULL);
}

/* =========================================
   WMI: тип RAM + номинальная частота (MHz) + кол-во планок
   ========================================= */

static int  g_ram_cached = 0;
static char g_ram_type[32] = "Unknown";
static int  g_ram_mhz = 0;
static int g_ram_modules = 0;

static const char* smbios_memtype_to_str(unsigned code)
{
    switch (code) {
        case 20: return "DDR";
        case 21: return "DDR2";
        case 24: return "DDR3";
        case 26: return "DDR4";
        case 27: return "DDR5";
        case 28: return "LPDDR";
        case 29: return "LPDDR2";
        case 30: return "LPDDR3";
        case 31: return "LPDDR4";
        case 32: return "LPDDR5";
        case 0:  return "Unknown";
        default: return "Unknown";
    }
}

static int variant_to_u32(VARIANT* v, unsigned* out)
{
    if (!v || !out) return 0;
    if (v->vt == VT_NULL || v->vt == VT_EMPTY) return 0;

    switch (v->vt) {
        case VT_UI1:  *out = v->bVal;   return 1;
        case VT_I2:   *out = (unsigned)(unsigned short)v->iVal; return 1;
        case VT_UI2:  *out = (unsigned)v->uiVal; return 1;
        case VT_I4:   *out = (unsigned)v->lVal;  return 1;
        case VT_UI4:  *out = (unsigned)v->ulVal; return 1;
        case VT_I8:   *out = (unsigned)v->llVal; return 1;
        case VT_UI8:  *out = (unsigned)v->ullVal;return 1;
        default: return 0;
    }
}

static int wmi_query_ram_once(void)
{
    HRESULT hr;
    int need_uninit = 0;

    IWbemLocator* pLoc = NULL;
    IWbemServices* pSvc = NULL;
    IEnumWbemClassObject* pEnum = NULL;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) need_uninit = 1;
    else if (hr != RPC_E_CHANGED_MODE) return 0;

    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL
    );
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        if (need_uninit) CoUninitialize();
        return 0;
    }

    hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          &IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr) || !pLoc) {
        if (need_uninit) CoUninitialize();
        return 0;
    }

    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    hr = IWbemLocator_ConnectServer(pLoc, ns, NULL, NULL, 0, 0, NULL, NULL, &pSvc);
    SysFreeString(ns);

    if (FAILED(hr) || !pSvc) {
        IWbemLocator_Release(pLoc);
        if (need_uninit) CoUninitialize();
        return 0;
    }

    hr = CoSetProxyBlanket(
        (IUnknown*)pSvc,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE
    );
    if (FAILED(hr)) {
        IWbemServices_Release(pSvc);
        IWbemLocator_Release(pLoc);
        if (need_uninit) CoUninitialize();
        return 0;
    }

    BSTR wql = SysAllocString(L"WQL");
    BSTR q   = SysAllocString(L"SELECT SMBIOSMemoryType, MemoryType, Speed, ConfiguredClockSpeed FROM Win32_PhysicalMemory");
    hr = IWbemServices_ExecQuery(
        pSvc, wql, q,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnum
    );
    SysFreeString(wql);
    SysFreeString(q);

    if (FAILED(hr) || !pEnum) {
        IWbemServices_Release(pSvc);
        IWbemLocator_Release(pLoc);
        if (need_uninit) CoUninitialize();
        return 0;
    }

    const char* type_first = NULL;
    int mixed = 0;
    unsigned max_speed = 0;

    int module_count = 0;            // +++

    while (1) {
        IWbemClassObject* pObj = NULL;
        ULONG ret = 0;

        hr = IEnumWbemClassObject_Next(pEnum, 2000, 1, &pObj, &ret);
        if (FAILED(hr) || ret == 0) break;

        module_count++;              // +++ считаем модули (каждый объект = планка)

        VARIANT vSmbios, vLegacy, vSpeed, vCfg;
        VariantInit(&vSmbios);
        VariantInit(&vLegacy);
        VariantInit(&vSpeed);
        VariantInit(&vCfg);

        unsigned code = 0, speed = 0, cfg = 0;

        IWbemClassObject_Get(pObj, L"SMBIOSMemoryType", 0, &vSmbios, NULL, NULL);
        if (!variant_to_u32(&vSmbios, &code) || code == 0) {
            IWbemClassObject_Get(pObj, L"MemoryType", 0, &vLegacy, NULL, NULL);
            variant_to_u32(&vLegacy, &code);
        }

        const char* t = smbios_memtype_to_str(code);
        if (!type_first) type_first = t;
        else if (lstrcmpA(type_first, t) != 0) mixed = 1;

        IWbemClassObject_Get(pObj, L"Speed", 0, &vSpeed, NULL, NULL);
        variant_to_u32(&vSpeed, &speed);

        IWbemClassObject_Get(pObj, L"ConfiguredClockSpeed", 0, &vCfg, NULL, NULL);
        variant_to_u32(&vCfg, &cfg);

        if (speed == 0) speed = cfg;
        if (speed > max_speed) max_speed = speed;

        VariantClear(&vSmbios);
        VariantClear(&vLegacy);
        VariantClear(&vSpeed);
        VariantClear(&vCfg);

        IWbemClassObject_Release(pObj);
    }

    if (mixed) lstrcpynA(g_ram_type, "Mixed", (int)sizeof(g_ram_type));
    else if (type_first) lstrcpynA(g_ram_type, type_first, (int)sizeof(g_ram_type));
    else lstrcpynA(g_ram_type, "Unknown", (int)sizeof(g_ram_type));

    g_ram_mhz = (int)max_speed;
    g_ram_modules = module_count;    // +++

    IEnumWbemClassObject_Release(pEnum);
    IWbemServices_Release(pSvc);
    IWbemLocator_Release(pLoc);
    if (need_uninit) CoUninitialize();

    return 1;
}

static void ensure_ram_cache(void)
{
    if (g_ram_cached) return;
    if (!wmi_query_ram_once()) {
        lstrcpynA(g_ram_type, "Unknown", (int)sizeof(g_ram_type));
        g_ram_mhz = 0;
    }
    g_ram_cached = 1;
}

#pragma pack(push, 1)
typedef struct RawSMBIOSData {
    uint8_t  Used20CallingMethod;
    uint8_t  SMBIOSMajorVersion;
    uint8_t  SMBIOSMinorVersion;
    uint8_t  DmiRevision;
    uint32_t Length;
    uint8_t  SMBIOSTableData[1];
} RawSMBIOSData;
#pragma pack(pop)

static DWORD fourcc(char a, char b, char c, char d) {
    return ((DWORD)(uint8_t)a) | ((DWORD)(uint8_t)b << 8) | ((DWORD)(uint8_t)c << 16) | ((DWORD)(uint8_t)d << 24);
}

int get_ram_channel_count_smbios(void)
{
    DWORD sig = fourcc('R','S','M','B');
    DWORD sz = GetSystemFirmwareTable(sig, 0, NULL, 0);
    if (sz == 0) return 0;

    uint8_t* buf = (uint8_t*)malloc(sz);
    if (!buf) return 0;

    if (GetSystemFirmwareTable(sig, 0, buf, sz) != sz) {
        free(buf);
        return 0;
    }

    RawSMBIOSData* rs = (RawSMBIOSData*)buf;
    uint8_t* p   = rs->SMBIOSTableData;
    uint8_t* end = rs->SMBIOSTableData + rs->Length;

    int count = 0;

    while (p + 4 <= end) {
        uint8_t type = p[0];
        uint8_t len  = p[1];

        if (len < 4 || p + len > end) break;
        if (type == 127) break;                 // End-of-table

        if (type == 37) count++;                // Memory Channel

        // Skip formatted section
        uint8_t* q = p + len;

        // Skip strings area (until double NUL)
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
        q += 2;

        p = q;
    }

    free(buf);
    return count; // 0 = нет данных (BIOS мог не заполнить)
}

/* ── SMBIOS Type 17 helpers ─────────────────────────────────────────────── */

/* Return pointer to the idx-th (1-based) string of a SMBIOS structure.
   Strings begin immediately after the formatted section (at s + s[1]).
   Returns "" when idx == 0 or not found.                                    */
static const char *smbios_str(const uint8_t *s, uint8_t idx,
                              const uint8_t *tab_end) {
    if (idx == 0) return "";
    const char *p = (const char *)(s + s[1]);
    for (uint8_t cur = 1; (const uint8_t *)p < tab_end && *p != '\0'; cur++) {
        if (cur == idx) return p;
        while ((const uint8_t *)p < tab_end && *p != '\0') p++;
        p++;  /* skip NUL terminator */
    }
    return "";
}

/* Extract a memory-channel letter (1=A, 2=B, 3=C, 4=D) from a Device
   Locator string.  Recognised patterns (case-insensitive for "Channel"):
     "ChannelA-DIMM0" → 1     "DIMM_A1" → 1     "DIMM A" → 1
     "ChannelB-DIMM0" → 2     "DIMM_B1" → 2     "DIMM B" → 2
   Returns 0 when no channel letter is found.                                */
static int channel_id_from_locator(const char *loc) {
    if (!loc || !*loc) return 0;

    for (const char *p = loc; *p; p++) {
        /* "Channel" prefix followed immediately by A-D */
        if ((*p == 'C' || *p == 'c') && _strnicmp(p, "channel", 7) == 0) {
            char ch = *(p + 7);
            if (ch >= 'A' && ch <= 'D') return ch - 'A' + 1;
            if (ch >= 'a' && ch <= 'd') return ch - 'a' + 1;
        }

        /* Separator (_  -  space) + single letter A-D + (digit | end | sep) */
        if (*p == '_' || *p == '-' || *p == ' ') {
            char ch  = *(p + 1);
            char nxt = *(p + 2);
            int letter = (ch >= 'A' && ch <= 'D') || (ch >= 'a' && ch <= 'd');
            int delim  = !nxt || nxt == '_' || nxt == '-' || nxt == ' ' ||
                         (nxt >= '0' && nxt <= '9');
            if (letter && delim)
                return (ch >= 'a') ? ch - 'a' + 1 : ch - 'A' + 1;
        }
    }
    return 0;
}

/* Parse SMBIOS Type 17 (Memory Device) entries and infer channel count from
   the Device Locator strings of populated slots.
   Works on ASUS ("ChannelA-DIMM0"), Gigabyte/ASRock ("DIMM_A1"), and other
   boards that encode the channel letter in the device locator.
   Returns 1/2/3/4 on success, 0 when no channel info is present.           */
int get_ram_channel_count_smbios_t17(void) {
    DWORD sig = fourcc('R','S','M','B');
    DWORD sz  = GetSystemFirmwareTable(sig, 0, NULL, 0);
    if (!sz) return 0;

    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) return 0;

    if (GetSystemFirmwareTable(sig, 0, buf, sz) != sz) { free(buf); return 0; }

    RawSMBIOSData *rs  = (RawSMBIOSData *)buf;
    uint8_t       *p   = rs->SMBIOSTableData;
    uint8_t       *end = rs->SMBIOSTableData + rs->Length;

    int max_ch = 0;

    while (p + 4 <= end) {
        uint8_t type = p[0];
        uint8_t len  = p[1];
        if (len < 4 || p + len > end) break;
        if (type == 127) break;

        if (type == 17 && len >= 0x12) {
            /* Size at offset 0x0C (LE 16-bit): 0 = not installed,
               0xFFFF = use extended size field at offset 0x1C.              */
            uint16_t size = (uint16_t)p[0x0C] | ((uint16_t)p[0x0D] << 8);
            if (size != 0 && size != 0xFFFF) {
                int ch = channel_id_from_locator(smbios_str(p, p[0x10], end));
                if (ch > max_ch) max_ch = ch;
            }
        }

        uint8_t *q = p + len;
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
        p = q + 2;
    }

    free(buf);
    return max_ch;
}

int get_ram_module_count(void)
{
    ensure_ram_cache();
    return g_ram_modules;
}

char* get_ram_type(void)
{
    ensure_ram_cache();
    return g_ram_type; // статический буфер, НЕ освобождать
}

int get_ram_nominal_freq_mhz(void)
{
    ensure_ram_cache();
    return g_ram_mhz; // 0 если не удалось
}
