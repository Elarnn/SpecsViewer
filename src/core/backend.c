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
static void collect_cpus(Backend *b);
static void get_cpu_info_into(CpuInfo *c, int pkg, int per_package);
static void get_ram_info(Backend *b);
static void get_motherboard_info(Backend *b);
static void get_system_general_info(Backend *b);
static void collect_gpus(Backend *b);
static void get_gpu_nvd_info_into(GpuInfo *g);

static void set_cpu_selection(Backend *b, int idx);
static void set_gpu_selection(Backend *b, int idx);
static void pin_to_selected_cpu(Backend *b);

static void poll_cpu(Snapshot *s);
static void poll_cpu_temp(Snapshot *s);
static void poll_cpu_voltage(Snapshot *s);
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

  /* MSR-based CPU sensors read from whichever core this thread runs on —
     keep it on the selected package (no-op on single-socket systems). */
  pin_to_selected_cpu(b);

  for (;;) {
    // Stop requested?
    if (WaitForSingleObject(b->stop_event, 0) == WAIT_OBJECT_0)
      break;

    // Device selection requests from the UI thread
    {
      long rc = InterlockedExchange(&b->req_cpu_sel, -1);
      if (rc >= 0) {
        set_cpu_selection(b, (int)rc);
        pin_to_selected_cpu(b);
      }
      long rg = InterlockedExchange(&b->req_gpu_sel, -1);
      if (rg >= 0)
        set_gpu_selection(b, (int)rg);
    }

    // Time step (includes sleep time)
    const double dt_ms = qpc_now_ms(fq, &t_prev);
    slow_accum += dt_ms;

    // Fast metrics (every tick)
    poll_cpu(&b->back);
    poll_cpu_temp(&b->back);
    poll_cpu_voltage(&b->back);
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

int backend_start(Backend *b, int cpu_sel, int gpu_sel) {
  memset(b, 0, sizeof(*b));
  b->req_cpu_sel = -1;
  b->req_gpu_sel = -1;

  b->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL); // manual reset
  b->mutex = CreateMutexA(NULL, FALSE, NULL);

  if (!b->stop_event || !b->mutex)
    return 0;

  // Static info
  collect_cpus(b);
  get_ram_info(b);
  get_motherboard_info(b);
  get_system_general_info(b);
  collect_gpus(b);

  set_cpu_selection(b, cpu_sel);
  set_gpu_selection(b, gpu_sel);

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

  /* the PDH query is only used by the backend thread — release it after join */
  cpu_current_mhz_shutdown();

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

void backend_select_cpu(Backend *b, int index) {
  if (!b || index < 0)
    return;
  InterlockedExchange(&b->req_cpu_sel, (long)index);
}

void backend_select_gpu(Backend *b, int index) {
  if (!b || index < 0)
    return;
  InterlockedExchange(&b->req_gpu_sel, (long)index);
}

/* --------------------------- Device selection ---------------------------- */

static void set_cpu_selection(Backend *b, int idx) {
  DeviceList *d = &b->back.data.devices;
  if (d->cpu_count <= 0)
    return;
  if (idx < 0 || idx >= d->cpu_count)
    idx = 0;
  d->cpu_sel = idx;
  b->back.data.cpu = d->cpus[idx];
  b->back.cpu_rt.cpu_temp = -1;
  b->back.cpu_rt.voltage  = 0.0;
}

static void pin_to_selected_cpu(Backend *b) {
  const DeviceList *d = &b->back.data.devices;
  if (d->cpu_count > 1)
    cpu_pin_to_package(d->cpu_sel, NULL);
}

static void set_gpu_selection(Backend *b, int idx) {
  DeviceList *d = &b->back.data.devices;
  memset(&b->back.gpu_rt, 0, sizeof(b->back.gpu_rt));

  if (d->gpu_count <= 0) {
    memset(&b->back.data.gpu, 0, sizeof(b->back.data.gpu));
    b->back.data.gpu.nvapi_index = -1;
    d->gpu_sel = 0;
    return;
  }
  if (idx < 0 || idx >= d->gpu_count)
    idx = 0;
  d->gpu_sel = idx;
  b->back.data.gpu = d->gpus[idx];
  if (b->back.data.gpu.is_nvidia)
    nvd_set_active_gpu(b->back.data.gpu.nvapi_index);
}

/* ------------------------------- CPU -------------------------------------- */

static void collect_cpus(Backend *b) {
  DeviceList *d = &b->back.data.devices;

  int n = get_cpu_package_count();
  if (n < 1)
    n = 1;
  if (n > MAX_CPU_DEVICES)
    n = MAX_CPU_DEVICES;
  d->cpu_count = n;

  for (int i = 0; i < n; i++) {
    /* CPUID reports the package the calling thread runs on — pin to it. */
    CpuAffinitySave sv;
    int pinned = (n > 1) ? cpu_pin_to_package(i, &sv) : 0;
    get_cpu_info_into(&d->cpus[i], i, n > 1);
    if (pinned)
      cpu_unpin(&sv);
  }
}

static void get_cpu_info_into(CpuInfo *c, int pkg, int per_package) {
  int cores = 0, threads = 0, base = 0;

  memset(c, 0, sizeof(*c));
  c->package_index = pkg;

  /* CPU name */
  if (!get_cpu_brand_string(c->model, sizeof(c->model)))
    snprintf(c->model, sizeof(c->model), "Unknown CPU");

  /* Cores/Threads: per package when several sockets, else system-wide */
  if (!(per_package && get_cpu_package_core_thread_count(pkg, &cores, &threads)) &&
      !get_cpu_core_thread_count(&cores, &threads)) {
    cores = 0;
    threads = 0;
  }
  c->cores = cores;
  c->threads = threads;

  /* Base freq */
  get_cpu_base_mhz(&base);
  c->base_mhz = base;

  /* Caches */
  if (!get_cpu_cache(&c->cache_l1d_kb, &c->cache_l1i_kb,
                     &c->cache_l2_kb, &c->cache_l3_kb,
                     &c->cache_l1d_x, &c->cache_l1i_x,
                     &c->cache_l2_x, &c->cache_l3_x)) {
    c->cache_l1d_kb = c->cache_l1i_kb = c->cache_l2_kb = c->cache_l3_kb = 0;
    c->cache_l1d_x = c->cache_l1i_x = c->cache_l2_x = c->cache_l3_x = 0;
  }

  if (!get_cpu_cache_ways(&c->cache_l1d_way, &c->cache_l1i_way,
                          &c->cache_l2_way, &c->cache_l3_way)) {
    c->cache_l1d_way = c->cache_l1i_way = c->cache_l2_way = c->cache_l3_way = 0;
  }

  /* Family / Model / Stepping */
  int fms[3];
  if (get_cpu_family_model_stepping(fms)) {
    c->family = fms[0];
    c->model_id = fms[1];
    c->stepping = fms[2];
  }

  /* INSTRUCTIONS */
  get_cpu_instructions(c->instructions, sizeof(c->instructions));
}

/* ------------------------------- RAM -------------------------------------- */

static void get_ram_info(Backend *b) {
  b->back.data.ram.total_mb = get_ram_total_mb();
  {
    const char *rt = get_ram_type();
    snprintf(b->back.data.ram.type, sizeof(b->back.data.ram.type), "%s", rt ? rt : "-");
  }
  b->back.data.ram.mhz = get_ram_nominal_freq_mhz();
  b->back.data.ram.module_count = get_ram_module_count();
  {
    const char *mfr = get_ram_manufacturer();
    snprintf(b->back.data.ram.manufacturer, sizeof(b->back.data.ram.manufacturer),
             "%s", (mfr && mfr[0]) ? mfr : "-");
  }
  {
    const char *chip = spd_get_dram_manufacturer();
    snprintf(b->back.data.ram.chip_manufacturer, sizeof(b->back.data.ram.chip_manufacturer),
             "%s", (chip && chip[0]) ? chip : "-");
  }
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

  /* DRAM voltage as configured by the firmware (SMBIOS Type 17) */
  get_ram_voltage_smbios_mv(&b->back.data.ram.voltage_mv,
                            &b->back.data.ram.min_voltage_mv,
                            &b->back.data.ram.max_voltage_mv);
}

/* --------------------------- Board / System ------------------------------- */

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

/* ------------------------------- GPU -------------------------------------- */

/* Build the GPU list: NVAPI GPUs first (full information), then any other
   physical display adapters with basic PnP information. */
static void collect_gpus(Backend *b) {
  DeviceList *d = &b->back.data.devices;
  int n = 0;

  int nv = get_nvd_gpu_count();
  if (nv > MAX_GPU_DEVICES)
    nv = MAX_GPU_DEVICES;

  for (int i = 0; i < nv; i++) {
    GpuInfo *g = &d->gpus[n];
    memset(g, 0, sizeof(*g));
    nvd_set_active_gpu(i);
    g->is_nvidia   = 1;
    g->nvapi_index = i;
    get_gpu_nvd_info_into(g);
    n++;
  }

  if (n < MAX_GPU_DEVICES)
    n += enum_basic_display_adapters(&d->gpus[n], MAX_GPU_DEVICES - n,
                                     /*skip_nvidia=*/ nv > 0);

  d->gpu_count = n;
  d->gpu_sel   = 0;
}

static void get_gpu_nvd_info_into(GpuInfo *g) {
  unsigned used = 0;
  double load = 0.0;

  get_nvd_gpu_name(g->name, sizeof(g->name));
  get_nvd_gpu_vram_snapshot(&used, &g->vram_total, &load);
  get_nvd_gpu_device_id(&g->ven, &g->dev, &g->subsys, &g->rev);
  get_nvd_gpu_core_base_boost_mhz(&g->clock_base_mhz, &g->clock_boost_mhz);
  get_nvd_gpu_mem_mhz(&g->mem_mhz);
  get_nvd_gpu_vram_type(g->vram_type, sizeof(g->vram_type));
  get_nvd_gpu_pcie_lanes(&g->pci_lanes);
  get_nvd_gpu_arch_info(g->arch,         sizeof(g->arch),
                        g->die,          sizeof(g->die),
                        g->process_node, sizeof(g->process_node));
  get_nvd_gpu_vram_bus_bits(g->die, &g->vram_bus_bits);
  get_nvd_gpu_driver_info(g->drv_version, sizeof(g->drv_version),
                          g->drv_date,    sizeof(g->drv_date));

  /* Feature support derived from GPU name — no extra NVAPI session needed.
     CUDA/Vulkan/DX12: all Maxwell+ NVIDIA GPUs support them.
     Ray Tracing / DLSS: only RTX-class cards (Turing TU102-TU106, Ampere, Ada+). */
  g->feat_cuda        = 1;
  g->feat_vulkan      = 1;
  g->feat_dx12        = 1;
  g->feat_ray_tracing = (strstr(g->name, "RTX") != NULL) ? 1 : 0;
  g->feat_dlss        = g->feat_ray_tracing;
}

/* ------------------------------ Polling ----------------------------------- */

static void poll_cpu(Snapshot *s) {
  get_cpu_load(s);
  s->cpu_rt.cur_mhz = get_cpu_current_mhz(s->data.cpu.base_mhz);
}

static void poll_cpu_temp(Snapshot *s) { s->cpu_rt.cpu_temp = cpu_wr0_read_temp(); }

static void poll_cpu_voltage(Snapshot *s) { s->cpu_rt.voltage = cpu_wr0_read_voltage(); }

static void poll_ram(Snapshot *s) { s->ram_rt.used_mb = get_ram_used_mb(); }

static void poll_gpu(Snapshot *s) {
  if (!s->data.gpu.is_nvidia) {
    /* Basic (non-NVAPI) adapter or no GPU: no runtime sensors available */
    memset(&s->gpu_rt, 0, sizeof(s->gpu_rt));
    return;
  }

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
