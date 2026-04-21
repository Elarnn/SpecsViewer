// system_info.c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <intrin.h> 
#include <string.h>
#include <stdlib.h> // для atoi

typedef struct SysInfoCache {
    char os_name_full[128]; // "Windows 11 Pro x64"
    char display_ver[32];   // "22H2"
    char os_build[32];
    char kernel_ver[64];
    char patch_level[32];
    char vm_vendor[64];
    int  has_hypervisor;
    int  ready;
} SysInfoCache;

static SysInfoCache g_sys;

// --- Helpers ---
static void read_reg_sz(HKEY hKey, const char* value, char* out, size_t out_sz) {
    DWORD type = REG_SZ;
    DWORD size = (DWORD)out_sz;
    if (RegQueryValueExA(hKey, value, NULL, &type, (LPBYTE)out, &size) != ERROR_SUCCESS) {
        out[0] = '\0';
    }
}

static DWORD read_reg_dword(HKEY hKey, const char* value) {
    DWORD type = REG_DWORD;
    DWORD data = 0;
    DWORD size = sizeof(data);
    if (RegQueryValueExA(hKey, value, NULL, &type, (LPBYTE)&data, &size) == ERROR_SUCCESS) {
        return data;
    }
    return 0;
}

// --- Logic ---

static void get_arch_string(char* out, size_t sz) {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si); // Важно: Native, чтобы на x64 не показывало x86 для 32-бит процесса

    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        strncpy(out, "x64", sz);
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
        strncpy(out, "ARM64", sz);
    } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        strncpy(out, "x86", sz);
    } else {
        strncpy(out, "Unknown Arch", sz);
    }
}

static void detect_vm_hypervisor(SysInfoCache* c) {
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 1);
    if ((cpuInfo[2] >> 31) & 1) {
        c->has_hypervisor = 1;
    } else {
        c->has_hypervisor = 0;
        snprintf(c->vm_vendor, sizeof(c->vm_vendor), "None (Bare Metal)");
        return;
    }

    __cpuid(cpuInfo, 0x40000000);
    char vendor[13];
    memset(vendor, 0, sizeof(vendor));
    memcpy(vendor + 0, &cpuInfo[1], 4);
    memcpy(vendor + 4, &cpuInfo[2], 4);
    memcpy(vendor + 8, &cpuInfo[3], 4);
    vendor[12] = '\0';

    if (strcmp(vendor, "Microsoft Hv") == 0)      snprintf(c->vm_vendor, sizeof(c->vm_vendor), "Microsoft Hyper-V");
    else if (strcmp(vendor, "VMwareVMware") == 0) snprintf(c->vm_vendor, sizeof(c->vm_vendor), "VMware");
    else if (strcmp(vendor, "VBoxVBoxVBox") == 0) snprintf(c->vm_vendor, sizeof(c->vm_vendor), "VirtualBox");
    else if (strcmp(vendor, "KVMKVMKVM") == 0)    snprintf(c->vm_vendor, sizeof(c->vm_vendor), "KVM");
    else snprintf(c->vm_vendor, sizeof(c->vm_vendor), "%s", vendor);
}

static void fetch_os_info(SysInfoCache* c) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        char prod_name[128] = {0};
        char release_id[32] = {0};
        char disp_ver[32] = {0};
        char current_build[32] = {0};

        read_reg_sz(hKey, "ProductName", prod_name, sizeof(prod_name));
        read_reg_sz(hKey, "CurrentBuild", current_build, sizeof(current_build));
        
        // Получаем версию 22H2 и т.д.
        // Сначала пробуем DisplayVersion (новее), если нет - ReleaseId (старое)
        read_reg_sz(hKey, "DisplayVersion", disp_ver, sizeof(disp_ver));
        if (disp_ver[0] == 0) {
            read_reg_sz(hKey, "ReleaseId", disp_ver, sizeof(disp_ver));
        }
        snprintf(c->display_ver, sizeof(c->display_ver), "%s", disp_ver);

        DWORD ubr = read_reg_dword(hKey, "UBR");
        DWORD major = read_reg_dword(hKey, "CurrentMajorVersionNumber");
        DWORD minor = read_reg_dword(hKey, "CurrentMinorVersionNumber");
        if (major == 0) major = 10; 

        // === Fix для Windows 11 ===
        // Windows 11 в реестре часто пишет "Windows 10 Pro", но билд >= 22000
        int build_num = atoi(current_build);
        if (build_num >= 22000) {
             // Ищем подстроку "Windows 10" и меняем на "Windows 11"
             char *p = strstr(prod_name, "Windows 10");
             if (p) {
                 p[8] = '1'; 
                 p[9] = '1';
             }
        }

        // Архитектура
        char arch[16] = {0};
        get_arch_string(arch, sizeof(arch));

        // Собираем полное имя: "Windows 11 Pro x64"
        snprintf(c->os_name_full, sizeof(c->os_name_full), "%s %s", prod_name, arch);
        
        // Остальные поля
        snprintf(c->os_build, sizeof(c->os_build), "%s", current_build);
        snprintf(c->patch_level, sizeof(c->patch_level), "%u", ubr);
        snprintf(c->kernel_ver, sizeof(c->kernel_ver), "%u.%u.%s.%u", major, minor, current_build, ubr);

        RegCloseKey(hKey);
    } else {
        snprintf(c->os_name_full, sizeof(c->os_name_full), "Windows Unknown");
    }
}

static void ensure_sys_cached(void) {
    if (g_sys.ready) return;
    memset(&g_sys, 0, sizeof(g_sys));
    fetch_os_info(&g_sys);
    detect_vm_hypervisor(&g_sys);
    g_sys.ready = 1;
}

/* --- Public Getters --- */

void get_os_name_full_s(char* out, size_t outsz) {
    ensure_sys_cached();
    if (out && outsz) {
        strncpy(out, g_sys.os_name_full, outsz - 1);
        out[outsz - 1] = 0;
    }
}

void get_os_display_ver_s(char* out, size_t outsz) {
    ensure_sys_cached();
    if (out && outsz) {
        strncpy(out, g_sys.display_ver, outsz - 1);
        out[outsz - 1] = 0;
    }
}

void get_os_build_s(char* out, size_t outsz) {
    ensure_sys_cached();
    if (out && outsz) {
        strncpy(out, g_sys.os_build, outsz - 1);
        out[outsz - 1] = 0;
    }
}

void get_os_kernel_ver_s(char* out, size_t outsz) {
    ensure_sys_cached();
    if (out && outsz) {
        strncpy(out, g_sys.kernel_ver, outsz - 1);
        out[outsz - 1] = 0;
    }
}

void get_os_patch_level_s(char* out, size_t outsz) {
    ensure_sys_cached();
    if (out && outsz) {
        strncpy(out, g_sys.patch_level, outsz - 1);
        out[outsz - 1] = 0;
    }
}

void get_vm_vendor_s(char* out, size_t outsz) {
    ensure_sys_cached();
    if (out && outsz) {
        strncpy(out, g_sys.vm_vendor, outsz - 1);
        out[outsz - 1] = 0;
    }
}

int get_is_hypervisor_present(void) {
    ensure_sys_cached();
    return g_sys.has_hypervisor;
}