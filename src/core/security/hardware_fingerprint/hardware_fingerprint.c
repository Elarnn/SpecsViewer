#include "../secure.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <commdlg.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct NormDriver {
  char file[96], service[96], version[96], sha256[96];
  int is_signed;
} NormDriver;

typedef struct NormProfile {
  char cpu_vendor[32], cpu_model[128], cpu_instructions[256];
  int cpu_cores, cpu_threads, cpu_base_mhz, cpu_family, cpu_model_id,
      cpu_stepping;
  char bios_vendor[64], bios_version[64], board_vendor[64], board_product[64],
      smbios_uuid[64];
  char os_name[128], os_display_ver[64], os_build[64], os_kernel[64],
      os_patch_level[64], vm_vendor[64];
  int is_hypervisor;
  char gpu_name[128], vram_type[32];
  int gpu_ven, gpu_dev, gpu_subsys, gpu_rev;
  unsigned int vram_total, gpu_clock_base_mhz, gpu_clock_boost_mhz, gpu_mem_mhz,
      gpu_pci_lanes;
  char ram_type[32];
  long long ram_total_mb;
  int ram_mhz, ram_module_count, ram_channel;
  int tpm, secure_boot, uefi, bitlocker, vbs, hvci, nx, smep, cet, ftpm, ptt;
  int drivers_count;
  NormDriver drivers[DRV_MAX];
} NormProfile;

static void appendf(char *dst, size_t cap, const char *fmt, ...) {
  size_t used = strlen(dst);
  if (used >= cap - 1)
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(dst + used, cap - used, fmt, ap);
  va_end(ap);
}

static void normalize_text(char *dst, size_t dst_sz, const char *src) {
  if (!src || !src[0]) {
    snprintf(dst, dst_sz, "-");
    return;
  }
  size_t di = 0;
  int prev_space = 1;
  for (size_t i = 0; src[i] && di + 1 < dst_sz; ++i) {
    unsigned char c = (unsigned char)src[i];
    if (isspace(c)) {
      if (!prev_space)
        dst[di++] = ' ';
      prev_space = 1;
      continue;
    }
    if (c == '"' || c == '\\')
      c = '_';
    dst[di++] = (char)tolower(c);
    prev_space = 0;
  }
  while (di > 0 && dst[di - 1] == ' ')
    di--;
  dst[di] = '\0';
  if (di == 0)
    snprintf(dst, dst_sz, "-");
}

static void detect_cpu_vendor(char *dst, size_t dst_sz, const char *cpu_model) {
  if (strstr(cpu_model, "intel"))
    snprintf(dst, dst_sz, "intel");
  else if (strstr(cpu_model, "amd"))
    snprintf(dst, dst_sz, "amd");
  else
    snprintf(dst, dst_sz, "unknown");
}

static int cmp_norm_driver(const void *a, const void *b) {
  const NormDriver *x = (const NormDriver *)a, *y = (const NormDriver *)b;
  int c = strcmp(x->file, y->file);
  if (c)
    return c;
  c = strcmp(x->service, y->service);
  if (c)
    return c;
  c = strcmp(x->version, y->version);
  if (c)
    return c;
  c = strcmp(x->sha256, y->sha256);
  if (c)
    return c;
  return x->is_signed - y->is_signed;
}

static void normalize_snapshot(const Snapshot *s, NormProfile *n) {
  memset(n, 0, sizeof(*n));
  normalize_text(n->cpu_model, sizeof(n->cpu_model), s->data.cpu.model);
  detect_cpu_vendor(n->cpu_vendor, sizeof(n->cpu_vendor), n->cpu_model);
  normalize_text(n->cpu_instructions, sizeof(n->cpu_instructions),
                 s->data.cpu.instructions);
  n->cpu_cores = s->data.cpu.cores;
  n->cpu_threads = s->data.cpu.threads;
  n->cpu_base_mhz = s->data.cpu.base_mhz;
  n->cpu_family = s->data.cpu.family;
  n->cpu_model_id = s->data.cpu.model_id;
  n->cpu_stepping = s->data.cpu.stepping;

  normalize_text(n->bios_vendor, sizeof(n->bios_vendor), s->data.board.bios_vendor);
  normalize_text(n->bios_version, sizeof(n->bios_version), s->data.board.bios_version);
  normalize_text(n->board_vendor, sizeof(n->board_vendor), s->data.board.board_vendor);
  normalize_text(n->board_product, sizeof(n->board_product), s->data.board.board_product);
  normalize_text(n->smbios_uuid, sizeof(n->smbios_uuid), s->data.board.smbios_uuid);

  normalize_text(n->os_name, sizeof(n->os_name), s->data.os.os_name);
  normalize_text(n->os_display_ver, sizeof(n->os_display_ver), s->data.os.os_display_ver);
  normalize_text(n->os_build, sizeof(n->os_build), s->data.os.os_build);
  normalize_text(n->os_kernel, sizeof(n->os_kernel), s->data.os.os_kernel);
  normalize_text(n->os_patch_level, sizeof(n->os_patch_level), s->data.os.os_patch_level);
  n->is_hypervisor = s->data.os.is_hypervisor ? 1 : 0;
  normalize_text(n->vm_vendor, sizeof(n->vm_vendor), s->data.os.vm_vendor);

  normalize_text(n->gpu_name, sizeof(n->gpu_name), s->data.gpu.name);
  normalize_text(n->vram_type, sizeof(n->vram_type), s->data.gpu.vram_type);
  n->gpu_ven = (int)s->data.gpu.ven;
  n->gpu_dev = (int)s->data.gpu.dev;
  n->gpu_subsys = (int)s->data.gpu.subsys;
  n->gpu_rev = (int)s->data.gpu.rev;
  n->vram_total = s->data.gpu.vram_total;
  n->gpu_clock_base_mhz = s->data.gpu.clock_base_mhz;
  n->gpu_clock_boost_mhz = s->data.gpu.clock_boost_mhz;
  n->gpu_mem_mhz = s->data.gpu.mem_mhz;
  n->gpu_pci_lanes = s->data.gpu.pci_lanes;

  normalize_text(n->ram_type, sizeof(n->ram_type), s->data.ram.type);
  n->ram_total_mb = (long long)s->data.ram.total_mb;
  n->ram_mhz = s->data.ram.mhz;
  n->ram_module_count = s->data.ram.module_count;
  n->ram_channel = s->data.ram.channel;

  n->tpm = (s->data.sec.tpm20 == 1);
  n->secure_boot = (s->data.sec.secure_boot == 1);
  n->uefi = (s->data.sec.uefi_mode == 1);
  n->bitlocker = (s->data.sec.bitlocker == 1);
  n->vbs = (s->data.sec.vbs == 1);
  n->hvci = (s->data.sec.hvci == 1);
  n->nx = (s->data.sec.nx == 1);
  n->smep = (s->data.sec.smep == 1);
  n->cet = (s->data.sec.cet == 1);
  n->ftpm = (s->data.sec.ftpm == 1);
  n->ptt = (s->data.sec.ptt == 1);

  n->drivers_count = s->data.drivers_count > DRV_MAX ? DRV_MAX : s->data.drivers_count;
  for (int i = 0; i < n->drivers_count; ++i) {
    normalize_text(n->drivers[i].file, sizeof(n->drivers[i].file),
                   s->data.drivers[i].driver_file);
    normalize_text(n->drivers[i].service, sizeof(n->drivers[i].service),
                   s->data.drivers[i].service);
    normalize_text(n->drivers[i].version, sizeof(n->drivers[i].version),
                   s->data.drivers[i].version);
    normalize_text(n->drivers[i].sha256, sizeof(n->drivers[i].sha256),
                   s->data.drivers[i].sha256);
    n->drivers[i].is_signed = s->data.drivers[i].is_signed ? 1 : 0;
  }
  qsort(n->drivers, (size_t)n->drivers_count, sizeof(n->drivers[0]), cmp_norm_driver);
}

static void build_sections(const NormProfile *n, char *bios, size_t bios_sz,
                           char *board, size_t board_sz, char *cpu,
                           size_t cpu_sz, char *gpu, size_t gpu_sz, char *os,
                           size_t os_sz, char *ram, size_t ram_sz, char *sec,
                           size_t sec_sz, char *drivers, size_t drv_sz) {
  snprintf(bios, bios_sz, "{\"vendor\":\"%s\",\"version\":\"%s\"}",
           n->bios_vendor, n->bios_version);
  snprintf(board, board_sz,
           "{\"product\":\"%s\",\"uuid\":\"%s\",\"vendor\":\"%s\"}",
           n->board_product, n->smbios_uuid, n->board_vendor);
  snprintf(cpu, cpu_sz,
           "{\"base_mhz\":%d,\"cores\":%d,\"family\":%d,\"instructions\":\"%s\",\"model\":\"%s\",\"model_id\":%d,\"stepping\":%d,\"threads\":%d,\"vendor\":\"%s\"}",
           n->cpu_base_mhz, n->cpu_cores, n->cpu_family, n->cpu_instructions,
           n->cpu_model, n->cpu_model_id, n->cpu_stepping, n->cpu_threads,
           n->cpu_vendor);
  snprintf(gpu, gpu_sz,
           "{\"clock_base_mhz\":%u,\"clock_boost_mhz\":%u,\"device\":%d,\"mem_mhz\":%u,\"name\":\"%s\",\"pci_lanes\":%u,\"rev\":%d,\"subsys\":%d,\"vendor\":%d,\"vram_total\":%u,\"vram_type\":\"%s\"}",
           n->gpu_clock_base_mhz, n->gpu_clock_boost_mhz, n->gpu_dev,
           n->gpu_mem_mhz, n->gpu_name, n->gpu_pci_lanes, n->gpu_rev,
           n->gpu_subsys, n->gpu_ven, n->vram_total, n->vram_type);
  snprintf(os, os_sz,
           "{\"build\":\"%s\",\"display\":\"%s\",\"hypervisor\":%s,\"kernel\":\"%s\",\"name\":\"%s\",\"patch\":\"%s\",\"vm_vendor\":\"%s\"}",
           n->os_build, n->os_display_ver, n->is_hypervisor ? "true" : "false",
           n->os_kernel, n->os_name, n->os_patch_level, n->vm_vendor);
  snprintf(ram, ram_sz,
           "{\"channel\":%d,\"mhz\":%d,\"modules\":%d,\"size_mb\":%lld,\"type\":\"%s\"}",
           n->ram_channel, n->ram_mhz, n->ram_module_count, n->ram_total_mb,
           n->ram_type);
  snprintf(sec, sec_sz,
           "{\"bitlocker\":%s,\"cet\":%s,\"ftpm\":%s,\"hvci\":%s,\"nx\":%s,\"ptt\":%s,\"secure_boot\":%s,\"smep\":%s,\"tpm\":%s,\"uefi\":%s,\"vbs\":%s}",
           n->bitlocker ? "true" : "false", n->cet ? "true" : "false",
           n->ftpm ? "true" : "false", n->hvci ? "true" : "false",
           n->nx ? "true" : "false", n->ptt ? "true" : "false",
           n->secure_boot ? "true" : "false", n->smep ? "true" : "false",
           n->tpm ? "true" : "false", n->uefi ? "true" : "false",
           n->vbs ? "true" : "false");

  drivers[0] = '\0';
  appendf(drivers, drv_sz, "[");
  for (int i = 0; i < n->drivers_count; ++i) {
    if (i)
      appendf(drivers, drv_sz, ",");
    appendf(drivers, drv_sz,
            "{\"file\":\"%s\",\"service\":\"%s\",\"sha256\":\"%s\",\"signed\":%s,\"version\":\"%s\"}",
            n->drivers[i].file, n->drivers[i].service, n->drivers[i].sha256,
            n->drivers[i].is_signed ? "true" : "false", n->drivers[i].version);
  }
  appendf(drivers, drv_sz, "]");
}

static void build_profile_json(const NormProfile *n, char *out, size_t out_sz,
                               char *bios, size_t bios_sz, char *board,
                               size_t board_sz, char *cpu, size_t cpu_sz,
                               char *gpu, size_t gpu_sz, char *os,
                               size_t os_sz, char *ram, size_t ram_sz,
                               char *sec, size_t sec_sz, char *drivers,
                               size_t drv_sz) {
  build_sections(n, bios, bios_sz, board, board_sz, cpu, cpu_sz, gpu, gpu_sz,
                 os, os_sz, ram, ram_sz, sec, sec_sz, drivers, drv_sz);
  snprintf(out, out_sz,
           "{\"bios\":%s,\"board\":%s,\"cpu\":%s,\"gpu\":%s,\"os\":%s,\"ram\":%s,\"security\":%s,\"drivers\":%s}",
           bios, board, cpu, gpu, os, ram, sec, drivers);
}

static int sha256_hex_utf8(const char *text, char out_hex[65]) {
  out_hex[0] = '\0';
  BCRYPT_ALG_HANDLE hAlg = NULL;
  NTSTATUS st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM,
                                            NULL, 0);
  if (st < 0)
    return 0;
  DWORD obj_len = 0, cb = 0, hash_len = 0;
  st = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len,
                         sizeof(obj_len), &cb, 0);
  if (st < 0 || obj_len == 0)
    goto fail;
  st = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
                         sizeof(hash_len), &cb, 0);
  if (st < 0 || hash_len != 32)
    goto fail;
  BYTE *obj = (BYTE *)malloc(obj_len);
  BYTE hash[32];
  if (!obj)
    goto fail;
  BCRYPT_HASH_HANDLE hHash = NULL;
  st = BCryptCreateHash(hAlg, &hHash, obj, obj_len, NULL, 0, 0);
  if (st < 0) {
    free(obj);
    goto fail;
  }
  st = BCryptHashData(hHash, (PUCHAR)(text ? text : ""),
                      (ULONG)strlen(text ? text : ""), 0);
  if (st >= 0)
    st = BCryptFinishHash(hHash, hash, sizeof(hash), 0);
  BCryptDestroyHash(hHash);
  free(obj);
  BCryptCloseAlgorithmProvider(hAlg, 0);
  if (st < 0)
    return 0;
  for (int i = 0; i < 32; ++i)
    snprintf(out_hex + (size_t)i * 2, 3, "%02x", hash[i]);
  out_hex[64] = '\0';
  return 1;
fail:
  BCryptCloseAlgorithmProvider(hAlg, 0);
  return 0;
}

static int save_dialog_write(const char *json) {
  char path[MAX_PATH] = "hardware_fingerprint.json";
  OPENFILENAMEA ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = "json";
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = "Save hardware fingerprint as...";
  if (!GetSaveFileNameA(&ofn))
    return 0;
  FILE *f = fopen(path, "wb");
  if (!f)
    return -1;
  size_t n = strlen(json), wr = fwrite(json, 1, n, f);
  fclose(f);
  return wr == n ? 1 : -1;
}

static int open_dialog_read(char **out_buf) {
  char path[MAX_PATH] = "";
  OPENFILENAMEA ofn;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = "Open hardware fingerprint file...";
  if (!GetOpenFileNameA(&ofn))
    return 0;

  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    fclose(f);
    return -1;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = '\0';
  *out_buf = buf;
  return 1;
}

static int extract_string_field(const char *json, const char *key, char *out,
                                size_t out_sz) {
  const char *p = strstr(json, key);
  if (!p)
    return 0;
  p = strchr(p, ':');
  if (!p)
    return 0;
  p++;
  while (*p && *p != '"')
    p++;
  if (*p != '"')
    return 0;
  p++;
  size_t i = 0;
  while (p[i] && p[i] != '"' && i + 1 < out_sz) {
    out[i] = p[i];
    i++;
  }
  out[i] = '\0';
  return i > 0;
}

static int extract_section_value(const char *json, const char *key, char *out,
                                 size_t out_sz) {
  const char *p = strstr(json, key);
  if (!p)
    return 0;
  p = strchr(p, ':');
  if (!p)
    return 0;
  p++;
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
    p++;

  char open = *p;
  char close = (open == '{') ? '}' : ((open == '[') ? ']' : 0);
  if (!close)
    return 0;

  int depth = 0;
  size_t i = 0;
  for (; *p && i + 1 < out_sz; ++p) {
    out[i++] = *p;
    if (*p == open)
      depth++;
    else if (*p == close) {
      depth--;
      if (depth == 0)
        break;
    }
  }
  out[i] = '\0';
  return depth == 0 && i > 0;
}

int hardware_fingerprint_build(const Snapshot *snap, char *out, size_t out_sz) {
  if (!out || out_sz == 0 || !snap)
    return -1;
  NormProfile n;
  normalize_snapshot(snap, &n);

  char *profile = (char *)malloc(150 * 1024), *final_json = (char *)malloc(180 * 1024);
  char *bios = (char *)malloc(4096), *board = (char *)malloc(4096), *cpu = (char *)malloc(8192),
       *gpu = (char *)malloc(8192), *os = (char *)malloc(8192), *ram = (char *)malloc(4096),
       *sec = (char *)malloc(4096), *drivers = (char *)malloc(100 * 1024);
  if (!profile || !final_json || !bios || !board || !cpu || !gpu || !os || !ram || !sec || !drivers) {
    snprintf(out, out_sz, "Save failed.");
    free(profile); free(final_json); free(bios); free(board); free(cpu); free(gpu); free(os); free(ram); free(sec); free(drivers);
    return -1;
  }

  build_profile_json(&n, profile, 150 * 1024, bios, 4096, board, 4096, cpu,
                     8192, gpu, 8192, os, 8192, ram, 4096, sec, 4096, drivers,
                     100 * 1024);

  char hash[65];
  if (!sha256_hex_utf8(profile, hash)) {
    snprintf(out, out_sz, "Save failed.");
    free(profile); free(final_json); free(bios); free(board); free(cpu); free(gpu); free(os); free(ram); free(sec); free(drivers);
    return -1;
  }

  snprintf(final_json, 180 * 1024,
           "{\n  \"schema\": 1,\n  \"profile\": %s,\n  \"fingerprint\": {\n    \"algorithm\": \"sha256\",\n    \"value\": \"%s\"\n  }\n}",
           profile, hash);

  int rc = save_dialog_write(final_json);
  snprintf(out, out_sz, rc == 1 ? "Hardware fingerprint saved" : (rc == 0 ? "Save canceled" : "Save failed"));

  free(profile); free(final_json); free(bios); free(board); free(cpu); free(gpu); free(os); free(ram); free(sec); free(drivers);
  return rc == 1 ? 1 : (rc == 0 ? 0 : -1);
}

int hardware_fingerprint_compare(const Snapshot *snap, char *out, size_t out_sz) {
  if (!out || out_sz == 0 || !snap)
    return -1;

  char *file_json = NULL;
  int orc = open_dialog_read(&file_json);
  if (orc == 0) {
    snprintf(out, out_sz, "Compare canceled");
    return 0;
  }
  if (orc < 0 || !file_json) {
    snprintf(out, out_sz, "Read error");
    return -1;
  }

  NormProfile n;
  normalize_snapshot(snap, &n);
  char *profile = (char *)malloc(150 * 1024);
  char *bios = (char *)malloc(4096), *board = (char *)malloc(4096), *cpu = (char *)malloc(8192),
       *gpu = (char *)malloc(8192), *os = (char *)malloc(8192), *ram = (char *)malloc(4096),
       *sec = (char *)malloc(4096), *drivers = (char *)malloc(100 * 1024);
  if (!profile || !bios || !board || !cpu || !gpu || !os || !ram || !sec || !drivers) {
    free(file_json); free(profile); free(bios); free(board); free(cpu); free(gpu); free(os); free(ram); free(sec); free(drivers);
    snprintf(out, out_sz, "Memory error");
    return -1;
  }

  build_profile_json(&n, profile, 150 * 1024, bios, 4096, board, 4096, cpu,
                     8192, gpu, 8192, os, 8192, ram, 4096, sec, 4096, drivers,
                     100 * 1024);

  char current_hash[65], file_hash[65];
  if (!sha256_hex_utf8(profile, current_hash) ||
      !extract_string_field(file_json, "\"value\"", file_hash, sizeof(file_hash))) {
    snprintf(out, out_sz, "Invalid hardware fingerprint file");
    free(file_json); free(profile); free(bios); free(board); free(cpu); free(gpu); free(os); free(ram); free(sec); free(drivers);
    return -1;
  }

  if (strcmp(current_hash, file_hash) == 0) {
    snprintf(out, out_sz, "Hardware fingerprint matched");
    free(file_json); free(profile); free(bios); free(board); free(cpu); free(gpu); free(os); free(ram); free(sec); free(drivers);
    return 1;
  }

  char fbios[8192], fboard[8192], fcpu[16384], fgpu[16384], fos[16384],
      fram[8192], fsec[8192], fdrv[120000];
  int mbios = !extract_section_value(file_json, "\"bios\"", fbios, sizeof(fbios)) || strcmp(fbios, bios);
  int mboard = !extract_section_value(file_json, "\"board\"", fboard, sizeof(fboard)) || strcmp(fboard, board);
  int mcpu = !extract_section_value(file_json, "\"cpu\"", fcpu, sizeof(fcpu)) || strcmp(fcpu, cpu);
  int mgpu = !extract_section_value(file_json, "\"gpu\"", fgpu, sizeof(fgpu)) || strcmp(fgpu, gpu);
  int mos = !extract_section_value(file_json, "\"os\"", fos, sizeof(fos)) || strcmp(fos, os);
  int mram = !extract_section_value(file_json, "\"ram\"", fram, sizeof(fram)) || strcmp(fram, ram);
  int msec = !extract_section_value(file_json, "\"security\"", fsec, sizeof(fsec)) || strcmp(fsec, sec);
  int mdrv = !extract_section_value(file_json, "\"drivers\"", fdrv, sizeof(fdrv)) || strcmp(fdrv, drivers);

  char mismatch[512] = {0};
  if (mbios) appendf(mismatch, sizeof(mismatch), "bios ");
  if (mboard) appendf(mismatch, sizeof(mismatch), "board ");
  if (mcpu) appendf(mismatch, sizeof(mismatch), "cpu ");
  if (mgpu) appendf(mismatch, sizeof(mismatch), "gpu ");
  if (mos) appendf(mismatch, sizeof(mismatch), "os ");
  if (mram) appendf(mismatch, sizeof(mismatch), "ram ");
  if (msec) appendf(mismatch, sizeof(mismatch), "security ");
  if (mdrv) appendf(mismatch, sizeof(mismatch), "drivers ");

  if (!mismatch[0])
    snprintf(out, out_sz, "Hardware fingerprint mismatch");
  else
    snprintf(out, out_sz, "Mismatch in: %s", mismatch);

  free(file_json); free(profile); free(bios); free(board); free(cpu); free(gpu); free(os); free(ram); free(sec); free(drivers);
  return -1;
}
