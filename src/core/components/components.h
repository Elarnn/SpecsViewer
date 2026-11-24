// components.h
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../backend.h"

/* ===============
     CPU INFO
   ===============*/

int get_cpu_brand_string(char *out, size_t out_sz);
int get_cpu_cache(int *l1d_kb, int *l1i_kb, int *l2_kb, int *l3_kb,
                             int *l1d_x,  int *l1i_x,  int *l2_x,  int *l3_x);
int get_cpu_cache_ways(int *l1d_way, int *l1i_way, int *l2_way, int *l3_way);
int get_cpu_core_thread_count(int *out_cores, int *out_threads);
int get_cpu_base_mhz(int *base_mhz);
int get_cpu_instructions(char *out, size_t n);
int get_cpu_family_model_stepping(int out[3]);
void get_cpu_load(Snapshot *s);
/* Effective current frequency (nominal x %performance via PDH), 0 = n/a.
   Needs two polls before it returns a value. */
int  get_cpu_current_mhz(int base_mhz);
void cpu_current_mhz_shutdown(void);

/* Multi-package (socket) support.
   CPUID describes the package the calling thread runs on, so per-package
   collection pins the thread to that package first (cpu_pin_to_package) and
   restores the previous affinity afterwards (cpu_unpin).                   */
typedef struct CpuAffinitySave {
  unsigned short     group;
  unsigned long long mask;
  int                valid;
} CpuAffinitySave;

int  get_cpu_package_count(void);
int  get_cpu_package_core_thread_count(int pkg, int *out_cores, int *out_threads);
int  cpu_pin_to_package(int pkg, CpuAffinitySave *saved);
void cpu_unpin(const CpuAffinitySave *saved);

/* ===============
     RAM INFO
   ===============*/

unsigned long long get_ram_total_mb(void);
unsigned long long get_ram_used_mb(void);
char *get_ram_type(void);
char *get_ram_manufacturer(void);
int get_ram_nominal_freq_mhz(void);
const char *spd_get_dram_manufacturer(void);
int get_ram_module_count(void);
int get_ram_channel_count_smbios(void);
int get_ram_channel_count_smbios_t17(void); /* SMBIOS Type 17 device locator */
int get_ram_channel_count_wr0(void);        /* WinRing0: Intel MCHBAR MMIO   */
int get_ram_voltage_smbios_mv(int *configured_mv, int *min_mv, int *max_mv);

/* ===============
  GPU INFO
===============*/

int is_gpu_nvidia_by_pci_vendor(void);
/* Vendor-agnostic enumeration of physical (PCI) display adapters via SetupAPI:
   name, PCI IDs, driver version/date. is_nvidia = 0, nvapi_index = -1.
   skip_nvidia = 1 leaves out VEN_10DE devices (already covered by NVAPI). */
int  enum_basic_display_adapters(GpuInfo *out, int max_out, int skip_nvidia);

/* NVAPI: number of physical GPUs, and which one get_nvd_gpu_* operate on. */
int  get_nvd_gpu_count(void);
void nvd_set_active_gpu(int index);

int get_nvd_gpu_name(char *out, size_t out_sz);
int get_nvd_gpu_vram_total_mb(unsigned int *out_mb);
int get_nvd_gpu_device_id(
    unsigned int *vendor_id,
    unsigned int *device_id,
    unsigned int *subsystem_id,
    unsigned int *revision_id);
int get_nvd_gpu_core_base_boost_mhz(unsigned int *out_base_mhz, unsigned int *out_boost_mhz);
int get_nvd_gpu_mem_mhz(unsigned int *out_base_mhz);
int get_nvd_gpu_vram_type(char *out, size_t out_sz);
int get_nvd_gpu_pcie_lanes(unsigned *out_lanes);
int get_nvd_gpu_core_temp(int *out_temp_c);
int get_nvd_gpu_mem_temp(int *out_temp_c);
int get_nvd_gpu_current_core_mem_mhz(unsigned int *out_core_mhz, unsigned int *out_mem_mhz);
int get_nvd_gpu_vram_snapshot(unsigned *out_used_mb, unsigned *out_total_mb, double *out_percent);
int get_nvd_gpu_arch_info(char *arch, size_t arch_sz, char *die, size_t die_sz, char *process_node, size_t pn_sz);
int get_nvd_gpu_vram_bus_bits(const char *die_name, unsigned int *out_bits);
int get_nvd_gpu_driver_info(char *ver, size_t ver_sz, char *date, size_t date_sz);

/* ===============
  MOTHERBOARD INFO
===============*/
void get_board_vendor_s(char *out, size_t outsz);
void get_board_product_s(char *out, size_t outsz);
void get_bios_vendor_s(char *out, size_t outsz);
void get_bios_version_s(char *out, size_t outsz);
void get_smbios_uuid_s(char *out, size_t outsz);

/* ===============
  SYSTEM INFO
===============*/
void get_os_name_full_s(char* out, size_t outsz);   // Выведет "Windows 11 Pro x64"
void get_os_display_ver_s(char* out, size_t outsz); // Выведет "22H2"
void get_os_build_s(char* out, size_t outsz);
void get_os_kernel_ver_s(char* out, size_t outsz);
void get_os_patch_level_s(char* out, size_t outsz);
void get_vm_vendor_s(char* out, size_t outsz);
int  get_is_hypervisor_present(void);

