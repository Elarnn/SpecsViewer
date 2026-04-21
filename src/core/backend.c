// backend.c
#define WIN32_LEAN_AND_MEAN
#include "backend.h"
#include "components/components.h"
#include "core/winring/winring0.h"
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <stdio.h>

/* ----------------------------- Forward decls ----------------------------- */
static void get_cpu_info(Backend *b);
static void get_ram_info(Backend *b);
static void get_motherboard_info(Backend *b);
static void get_system_general_info(Backend *b);
static void gpu_init(Backend *b);
static void get_gpu_nvd_info(Backend *b);

static void poll_cpu(Snapshot *s);
static void poll_cpu_temp(Snapshot *s);
static void poll_ram(Snapshot *s);
static void poll_gpu(Snapshot *s);

/* ------------------------------ Time helpers ----------------------------- */
#define BACKEND_DEFAULT_FAST_HZ 4.0
#define BACKEND_DEFAULT_SLOW_HZ 1.0

static double qpc_now_ms(LARGE_INTEGER fq, LARGE_INTEGER *t_prev) {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);

  const double dt_ms =
      1000.0 * (double)(t.QuadPart - t_prev->QuadPart) / (double)fq.QuadPart;
  *t_prev = t;
  return dt_ms;
}

static DWORD WINAPI backend_thread(LPVOID param) {
  Backend *b = (Backend *)param;

  const double fast_hz =
      (b->poll_hz_fast > 0.0) ? b->poll_hz_fast : BACKEND_DEFAULT_FAST_HZ;
  const double slow_hz =
      (b->poll_hz_slow > 0.0) ? b->poll_hz_slow : BACKEND_DEFAULT_SLOW_HZ;

  const double fast_period_ms = 1000.0 / fast_hz;
  const double slow_period_ms = 1000.0 / slow_hz;

  LARGE_INTEGER fq;
  QueryPerformanceFrequency(&fq);

  LARGE_INTEGER t_prev;
  QueryPerformanceCounter(&t_prev);

  double slow_accum = 0.0;

  for (;;) {
    // Stop requested?
    if (WaitForSingleObject(b->stop_event, 0) == WAIT_OBJECT_0)
      break;

    // Time step (includes sleep time)
    const double dt_ms = qpc_now_ms(fq, &t_prev);
    slow_accum += dt_ms;

    // Fast metrics (every tick)
    poll_cpu(&b->back);
    poll_cpu_temp(&b->back);
    poll_ram(&b->back);
    poll_gpu(&b->back);

    // Slow metrics (rare updates)
    if (slow_accum >= slow_period_ms) {
      // put slow polling here (drivers, disks, etc.)
      slow_accum = 0.0;
    }

    // Publish snapshot (short critical section)
    WaitForSingleObject(b->mutex, INFINITE);
    b->front = b->back;
    ReleaseMutex(b->mutex);

    // Sleep until next fast tick (interruptible by stop_event)
    const DWORD sleep_ms =
        (DWORD)((fast_period_ms >= 1.0) ? fast_period_ms : 1.0);
    WaitForSingleObject(b->stop_event, sleep_ms);
  }

  return 0;
}

/* ------------------------------- Public API ------------------------------ */

int backend_start(Backend *b) {
  memset(b, 0, sizeof(*b));

  b->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL); // manual reset
  b->mutex = CreateMutexA(NULL, FALSE, NULL);

  if (!b->stop_event || !b->mutex)
    return 0;

  // Static info
  get_cpu_info(b);
  get_ram_info(b);
  get_motherboard_info(b);
  get_system_general_info(b);
  gpu_init(b);

  b->front = b->back;

  DWORD tid = 0;
  b->thread = CreateThread(NULL, 0, backend_thread, b, 0, &tid);
  if (!b->thread) {
    if (b->mutex)
      CloseHandle(b->mutex);
    if (b->stop_event)
      CloseHandle(b->stop_event);
    memset(b, 0, sizeof(*b));
    return 0;
  }

  return 1;
}

void backend_stop(Backend *b) {
  if (!b)
    return;

  if (b->stop_event)
    SetEvent(b->stop_event);

  if (b->thread)
    WaitForSingleObject(b->thread, INFINITE);

  if (b->thread)
    CloseHandle(b->thread);
  if (b->stop_event)
    CloseHandle(b->stop_event);
  if (b->mutex)
    CloseHandle(b->mutex);

  memset(b, 0, sizeof(*b));
}

void backend_read(Backend *b, Snapshot *out) {
  WaitForSingleObject(b->mutex, INFINITE); // краткий lock на копирование
  *out = b->front;
  ReleaseMutex(b->mutex);
}

static void get_cpu_info(Backend *b) {
  int l1 = 0, l2 = 0, l3 = 0, cores = 0, threads = 0, base = 0;

  /* CPU name */
  if (!get_cpu_brand_string(b->back.data.cpu.model, sizeof(b->back.data.cpu.model)))
    snprintf(b->back.data.cpu.model, sizeof(b->back.data.cpu.model), "Unknown CPU");

  /* Cores/Threads */
  if (!get_cpu_core_thread_count(&cores, &threads)) {
    cores = 0;
    threads = 0;
  }
  b->back.data.cpu.cores = cores;
  b->back.data.cpu.threads = threads;

  /* Base freq */
  get_cpu_base_mhz(&base);
  b->back.data.cpu.base_mhz = base;

  /* Caches */
  if (!get_cpu_cache(&b->back.data.cpu.cache_l1d_kb, &b->back.data.cpu.cache_l1i_kb,
                     &b->back.data.cpu.cache_l2_kb, &b->back.data.cpu.cache_l3_kb,
                     &b->back.data.cpu.cache_l1d_x, &b->back.data.cpu.cache_l1i_x,
                     &b->back.data.cpu.cache_l2_x, &b->back.data.cpu.cache_l3_x)) {
    b->back.data.cpu.cache_l1d_kb = b->back.data.cpu.cache_l1i_kb = b->back.data.cpu.cache_l2_kb =
        b->back.data.cpu.cache_l3_kb = 0;
    b->back.data.cpu.cache_l1d_x = b->back.data.cpu.cache_l1i_x = b->back.data.cpu.cache_l2_x =
        b->back.data.cpu.cache_l3_x = 0;
  }

  if (!get_cpu_cache_ways(&b->back.data.cpu.cache_l1d_way, &b->back.data.cpu.cache_l1i_way,
                          &b->back.data.cpu.cache_l2_way, &b->back.data.cpu.cache_l3_way)) {
    b->back.data.cpu.cache_l1d_way = b->back.data.cpu.cache_l1i_way = b->back.data.cpu.cache_l2_way =
        b->back.data.cpu.cache_l3_way = 0;
  }

  /* Family / Model / Stepping */
  int fms[3];
  if (get_cpu_family_model_stepping(fms)) {
    b->back.data.cpu.family = fms[0];
    b->back.data.cpu.model_id = fms[1];
    b->back.data.cpu.stepping = fms[2];
  }

  /* INSTRUCTIONS */
  get_cpu_instructions(b->back.data.cpu.instructions,
                       sizeof(b->back.data.cpu.instructions));
}
static void get_ram_info(Backend *b) {
  b->back.data.ram.total_mb = get_ram_total_mb();
  {
    const char *rt = get_ram_type();
    snprintf(b->back.data.ram.type, sizeof(b->back.data.ram.type), "%s", rt ? rt : "-");
  }
  b->back.data.ram.mhz = get_ram_nominal_freq_mhz();
  b->back.data.ram.module_count = get_ram_module_count();
  {
    /* Infer channel count from module count.
       Accurate when DIMMs are placed in the manufacturer-recommended slots
       (interleaved pairing), which covers ~95% of consumer builds.
       1 → single; 2/4 → dual; 3/6 → triple; 8 → quad.                    */
    int m = b->back.data.ram.module_count;
    int ch = 0;
    if      (m == 1)           ch = 1;
    else if (m == 2 || m == 4) ch = 2;
    else if (m == 3 || m == 6) ch = 3;
    else if (m == 8)           ch = 4;
    b->back.data.ram.channel = ch;
  }
}

static void get_motherboard_info(Backend *b) {
  get_board_vendor_s(b->back.data.board.board_vendor, sizeof b->back.data.board.board_vendor);
  get_board_product_s(b->back.data.board.board_product, sizeof b->back.data.board.board_product);
  get_bios_vendor_s(b->back.data.board.bios_vendor, sizeof b->back.data.board.bios_vendor);
  get_bios_version_s(b->back.data.board.bios_version, sizeof b->back.data.board.bios_version);
  get_smbios_uuid_s(b->back.data.board.smbios_uuid, sizeof b->back.data.board.smbios_uuid);
}
 
static void get_system_general_info(Backend *b) {
    // === Сбор данных ===
    get_os_name_full_s(b->back.data.os.os_name, sizeof(b->back.data.os.os_name));
    get_os_display_ver_s(b->back.data.os.os_display_ver, sizeof(b->back.data.os.os_display_ver));
    get_os_build_s(b->back.data.os.os_build, sizeof(b->back.data.os.os_build));
    get_os_kernel_ver_s(b->back.data.os.os_kernel, sizeof(b->back.data.os.os_kernel));
    get_os_patch_level_s(b->back.data.os.os_patch_level, sizeof(b->back.data.os.os_patch_level));
    
    b->back.data.os.is_hypervisor = get_is_hypervisor_present();
    get_vm_vendor_s(b->back.data.os.vm_vendor, sizeof(b->back.data.os.vm_vendor));
}

static void gpu_init(Backend *b) {
  b->back.data.gpu.is_nvidia = is_gpu_nvidia_by_pci_vendor();
  if (b->back.data.gpu.is_nvidia) {
    get_gpu_nvd_info(b);
  } else {
  }
}

static void get_gpu_nvd_info(Backend *b) {
  get_nvd_gpu_name(b->back.data.gpu.name, sizeof(b->back.data.gpu.name));
  get_nvd_gpu_vram_snapshot(&b->back.gpu_rt.vram_used, &b->back.data.gpu.vram_total,
                            &b->back.gpu_rt.vram_load);
  get_nvd_gpu_device_id(&b->back.data.gpu.ven, &b->back.data.gpu.dev, &b->back.data.gpu.subsys,
                        &b->back.data.gpu.rev);
  get_nvd_gpu_core_base_boost_mhz(&b->back.data.gpu.clock_base_mhz,
                                  &b->back.data.gpu.clock_boost_mhz);
  get_nvd_gpu_mem_mhz(&b->back.data.gpu.mem_mhz);
  get_nvd_gpu_vram_type(b->back.data.gpu.vram_type, sizeof(b->back.data.gpu.vram_type));
  get_nvd_gpu_pcie_lanes(&b->back.data.gpu.pci_lanes);
}

static void poll_cpu(Snapshot *s) { get_cpu_load(s); }

static void poll_cpu_temp(Snapshot *s) { s->cpu_rt.cpu_temp = cpu_wr0_read_temp(); }

static void poll_ram(Snapshot *s) { s->ram_rt.used_mb = get_ram_used_mb(); }

static void poll_gpu(Snapshot *s) {
  get_nvd_gpu_core_temp(&s->gpu_rt.clock_temp);
  get_nvd_gpu_mem_temp(&s->gpu_rt.mem_temp);
  {
    unsigned int core_mhz = 0, mem_mhz = 0;
    get_nvd_gpu_current_core_mem_mhz(&core_mhz, &mem_mhz);
    s->gpu_rt.curr_core_freq = (int)core_mhz;
    s->gpu_rt.curr_mem_freq  = (int)mem_mhz;
  }
  get_nvd_gpu_vram_snapshot(&s->gpu_rt.vram_used, &s->data.gpu.vram_total, &s->gpu_rt.vram_load);
}
