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

/* ===============
     RAM INFO
   ===============*/

unsigned long long get_ram_total_mb(void);
unsigned long long get_ram_used_mb(void);
char *get_ram_type(void);
int get_ram_nominal_freq_mhz(void);
int get_ram_module_count(void);
int get_ram_channel_count_smbios(void);
int get_ram_channel_count_smbios_t17(void); /* SMBIOS Type 17 device locator */
int get_ram_channel_count_wr0(void);        /* WinRing0: Intel MCHBAR MMIO   */

/* ===============
  GPU INFO
===============*/

int is_gpu_nvidia_by_pci_vendor(void);
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

