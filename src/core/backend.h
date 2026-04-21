// backend.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DRV_MAX 64

typedef struct DriverInfo {
  char class_name[32];
  char device_desc[128];
  char provider[128];
  char version[64];
  char date[32];
  char inf[128];
  char service[64];
  char driver_file[64];
  char image_path[260];
  char start_type[16];
  char service_type[32];
  char load_order_group[64];
  char sha256[65];
  int is_signed;
  char signer[128];
} DriverInfo;

typedef struct CpuInfo {
  char model[96];
  int cores, threads;
  int base_mhz;
  int family, model_id, stepping;
  char instructions[128];

  int cache_l1d_kb, cache_l1i_kb;
  int cache_l1d_x, cache_l1i_x;
  int cache_l1d_way, cache_l1i_way;
  int cache_l2_kb, cache_l2_x, cache_l2_way;
  int cache_l3_kb, cache_l3_x, cache_l3_way;
} CpuInfo;

typedef struct CpuRuntime {
  double load;
  int cpu_temp; /* CPU package temperature °C, -1 if unavailable */
} CpuRuntime;

typedef struct GpuInfo {
  int is_nvidia;
  char name[96];
  char vram_type[16];
  unsigned int vram_total;
  unsigned int ven, dev, subsys, rev;
  unsigned int clock_base_mhz, clock_boost_mhz;
  unsigned int mem_mhz;
  unsigned int pci_lanes;
} GpuInfo;

typedef struct GpuRuntime {
  unsigned int vram_used;
  double vram_load;
  int clock_temp;
  int mem_temp;
  int curr_mem_freq;
  int curr_core_freq;
  unsigned int clock_load;
} GpuRuntime;

typedef struct RamInfo {
  unsigned long long total_mb;
  char type[32];
  int mhz, module_count, channel;
} RamInfo;

typedef struct RamRuntime { unsigned long long used_mb; } RamRuntime;

typedef struct BoardInfo {
  char board_vendor[48];
  char board_product[48];
  char bios_vendor[48];
  char bios_version[32];
  char smbios_uuid[37];
} BoardInfo;

typedef struct OsInfo {
  char os_name[128];
  char os_display_ver[32];
  char os_build[32];
  char os_kernel[64];
  char os_patch_level[32];
  char vm_vendor[64];
  int is_hypervisor;
} OsInfo;

typedef struct SecurityInfo {
  int uefi_mode;
  int secure_boot;
  int tpm20;
  int bitlocker;
  int vbs;
  int hvci;
  int cred_guard;
  int ftpm;
  int ptt;
  int nx;
  int smep;
  int cet;
} SecurityInfo;

typedef struct SystemData {
  CpuInfo cpu;
  GpuInfo gpu;
  RamInfo ram;
  BoardInfo board;
  OsInfo os;
  SecurityInfo sec;
  int drivers_count;
  DriverInfo drivers[DRV_MAX];
} SystemData;

typedef struct Snapshot {
  SystemData data;
  CpuRuntime cpu_rt;
  GpuRuntime gpu_rt;
  RamRuntime ram_rt;
} Snapshot;

typedef struct Backend {
  void *thread;
  void *stop_event;
  void *mutex;
  Snapshot front;
  Snapshot back;
  double poll_hz_fast;
  double poll_hz_slow;
} Backend;

int backend_start(Backend *b);
void backend_stop(Backend *b);
void backend_read(Backend *b, Snapshot *out);

