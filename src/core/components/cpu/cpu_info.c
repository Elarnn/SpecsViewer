// cpu_info.c
#include "../../backend.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>


#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#include <intrin.h>
static void cpuid_leaf(unsigned leaf, unsigned subleaf, unsigned r[4]) {
  int v[4];
  __cpuidex(v, (int)leaf, (int)subleaf);
  r[0] = v[0];
  r[1] = v[1];
  r[2] = v[2];
  r[3] = v[3];
}
static unsigned cpuid_max_basic(void) {
  int v[4];
  __cpuid(v, 0x0);
  return (unsigned)v[0];
}
static unsigned cpuid_max_ext(void) {
  int v[4];
  __cpuid(v, 0x80000000u);
  return (unsigned)v[0];
}
static unsigned long long xgetbv0(void) { return _xgetbv(0); }
#else
#include <cpuid.h>
static void cpuid_leaf(unsigned leaf, unsigned subleaf, unsigned r[4]) {
  unsigned a, b, c, d;
  __cpuid_count(leaf, subleaf, a, b, c, d);
  r[0] = a;
  r[1] = b;
  r[2] = c;
  r[3] = d;
}
static unsigned cpuid_max_basic(void) { return __get_cpuid_max(0x0, NULL); }
static unsigned cpuid_max_ext(void) {
  return __get_cpuid_max(0x80000000u, NULL);
}
static unsigned long long xgetbv0(void) {
  unsigned a, d;
  __asm__ volatile(".byte 0x0f,0x01,0xd0" : "=a"(a), "=d"(d) : "c"(0));
  return ((unsigned long long)d << 32) | a;
}
#endif

/* ---------- Brand String ---------- */
static void trim_spaces(char *s) {
  size_t n = strlen(s), i = 0;
  while (i < n && s[i] == ' ')
    i++;
  if (i)
    memmove(s, s + i, n - i + 1);
  for (n = strlen(s); n && s[n - 1] == ' '; n--)
    s[n - 1] = '\0';
}

int get_cpu_brand_string(char *out, size_t out_sz) {
  if (!out || out_sz < 2)
    return 0;
  unsigned maxe = cpuid_max_ext();
  if (maxe < 0x80000004u) {
    out[0] = '\0';
    return 0;
  }

  unsigned r[4];
  char b[49] = {0};
  for (unsigned i = 0; i < 3; ++i) {
    cpuid_leaf(0x80000002u + i, 0, r);
    memcpy(b + i * 16 + 0, &r[0], 4);
    memcpy(b + i * 16 + 4, &r[1], 4);
    memcpy(b + i * 16 + 8, &r[2], 4);
    memcpy(b + i * 16 + 12, &r[3], 4);
  }
  trim_spaces(b);
  strncpy(out, b, out_sz - 1);
  out[out_sz - 1] = '\0';
  return out[0] != '\0';
}

/* ---------- Cache sizes (KB) ---------- */
static int cpu_total_logical_threads_cpuid(void) {
  unsigned r[4];

  if (cpuid_max_basic() >= 0x0Bu) {
    for (unsigned sub = 0;; ++sub) {
      cpuid_leaf(0x0B, sub, r);
      unsigned lvl_type = (r[2] >> 8) & 0xFF;
      if (lvl_type == 0)
        break;
      if (lvl_type == 2) { // package level
        int v = (int)(r[1] & 0xFFFF);
        if (v > 0)
          return v;
      }
    }
  }

  cpuid_leaf(1, 0, r);
  return (int)((r[1] >> 16) & 0xFF);
}

static void parse_det_cache(unsigned eax, unsigned ebx, unsigned ecx,
                            int total_logical, int *level, int *type,
                            int *size_kb, int *ways, int *instances) {
  int t = (int)(eax & 0x1F);
  int lv = (int)((eax >> 5) & 0x7);

  int line = (int)((ebx & 0xFFF) + 1);
  int part = (int)(((ebx >> 12) & 0x3FF) + 1);
  int w = (int)(((ebx >> 22) & 0x3FF) + 1);
  int sets = (int)(ecx + 1);

  uint64_t bytes =
      (uint64_t)line * (uint64_t)part * (uint64_t)w * (uint64_t)sets;

  int shared = (int)(((eax >> 14) & 0xFFF) + 1);
  int inst = (total_logical > 0 && shared > 0) ? (total_logical / shared) : 1;
  if (inst <= 0)
    inst = 1;

  if (level)
    *level = lv;
  if (type)
    *type = t;
  if (size_kb)
    *size_kb = (int)(bytes / 1024u);
  if (ways)
    *ways = w;
  if (instances)
    *instances = inst;
}

int get_cpu_cache(int *l1d_kb, int *l1i_kb, int *l2_kb, int *l3_kb, int *l1d_x,
                  int *l1i_x, int *l2_x, int *l3_x) {
  if (l1d_kb)
    *l1d_kb = 0;
  if (l1i_kb)
    *l1i_kb = 0;
  if (l2_kb)
    *l2_kb = 0;
  if (l3_kb)
    *l3_kb = 0;
  if (l1d_x)
    *l1d_x = 0;
  if (l1i_x)
    *l1i_x = 0;
  if (l2_x)
    *l2_x = 0;
  if (l3_x)
    *l3_x = 0;

  unsigned r[4];
  unsigned maxb = cpuid_max_basic();
  unsigned maxe = cpuid_max_ext();

  int total_logical = cpu_total_logical_threads_cpuid();
  if (total_logical <= 0)
    total_logical = 1;

  int ok = 0;

  if (maxb >= 0x04u) {
    for (unsigned sub = 0;; ++sub) {
      cpuid_leaf(0x04, sub, r);
      unsigned eax = r[0];
      unsigned type = eax & 0x1F;
      if (type == 0)
        break;

      int lv, tp, sz_kb, ways, inst;
      parse_det_cache(r[0], r[1], r[2], total_logical, &lv, &tp, &sz_kb, &ways,
                      &inst);

      if (lv == 1 && tp == 1) {
        if (l1d_kb)
          *l1d_kb = sz_kb;
        if (l1d_x)
          *l1d_x = inst;
        ok = 1;
      }
      if (lv == 1 && tp == 2) {
        if (l1i_kb)
          *l1i_kb = sz_kb;
        if (l1i_x)
          *l1i_x = inst;
        ok = 1;
      }
      if (lv == 2) {
        if (l2_kb)
          *l2_kb = sz_kb;
        if (l2_x)
          *l2_x = inst;
        ok = 1;
      }
      if (lv == 3) {
        if (l3_kb)
          *l3_kb = sz_kb;
        if (l3_x)
          *l3_x = inst;
        ok = 1;
      }
    }
    if (ok)
      return 1;
  }

  if (maxe >= 0x8000001Du) {
    for (unsigned sub = 0;; ++sub) {
      cpuid_leaf(0x8000001D, sub, r);
      unsigned eax = r[0];
      unsigned type = eax & 0x1F;
      if (type == 0)
        break;

      int lv, tp, sz_kb, ways, inst;
      parse_det_cache(r[0], r[1], r[2], total_logical, &lv, &tp, &sz_kb, &ways,
                      &inst);

      if (lv == 1 && tp == 1) {
        if (l1d_kb)
          *l1d_kb = sz_kb;
        if (l1d_x)
          *l1d_x = inst;
        ok = 1;
      }
      if (lv == 1 && tp == 2) {
        if (l1i_kb)
          *l1i_kb = sz_kb;
        if (l1i_x)
          *l1i_x = inst;
        ok = 1;
      }
      if (lv == 2) {
        if (l2_kb)
          *l2_kb = sz_kb;
        if (l2_x)
          *l2_x = inst;
        ok = 1;
      }
      if (lv == 3) {
        if (l3_kb)
          *l3_kb = sz_kb;
        if (l3_x)
          *l3_x = inst;
        ok = 1;
      }
    }
    if (ok)
      return 1;
  }

  return 0;
}

int get_cpu_cache_ways(int *l1d_way, int *l1i_way, int *l2_way, int *l3_way) {
  if (l1d_way)
    *l1d_way = 0;
  if (l1i_way)
    *l1i_way = 0;
  if (l2_way)
    *l2_way = 0;
  if (l3_way)
    *l3_way = 0;

  unsigned r[4];
  unsigned maxb = cpuid_max_basic();
  unsigned maxe = cpuid_max_ext();

  int total_logical = cpu_total_logical_threads_cpuid();
  if (total_logical <= 0)
    total_logical = 1;

  int ok = 0;

  if (maxb >= 0x04u) {
    for (unsigned sub = 0;; ++sub) {
      cpuid_leaf(0x04, sub, r);
      unsigned eax = r[0];
      unsigned type = eax & 0x1F;
      if (type == 0)
        break;

      int lv, tp, sz_kb, ways, inst;
      parse_det_cache(r[0], r[1], r[2], total_logical, &lv, &tp, &sz_kb, &ways,
                      &inst);

      if (lv == 1 && tp == 1) {
        if (l1d_way)
          *l1d_way = ways;
        ok = 1;
      }
      if (lv == 1 && tp == 2) {
        if (l1i_way)
          *l1i_way = ways;
        ok = 1;
      }
      if (lv == 2) {
        if (l2_way)
          *l2_way = ways;
        ok = 1;
      }
      if (lv == 3) {
        if (l3_way)
          *l3_way = ways;
        ok = 1;
      }
    }
    if (ok)
      return 1;
  }

  if (maxe >= 0x8000001Du) {
    for (unsigned sub = 0;; ++sub) {
      cpuid_leaf(0x8000001D, sub, r);
      unsigned eax = r[0];
      unsigned type = eax & 0x1F;
      if (type == 0)
        break;

      int lv, tp, sz_kb, ways, inst;
      parse_det_cache(r[0], r[1], r[2], total_logical, &lv, &tp, &sz_kb, &ways,
                      &inst);

      if (lv == 1 && tp == 1) {
        if (l1d_way)
          *l1d_way = ways;
        ok = 1;
      }
      if (lv == 1 && tp == 2) {
        if (l1i_way)
          *l1i_way = ways;
        ok = 1;
      }
      if (lv == 2) {
        if (l2_way)
          *l2_way = ways;
        ok = 1;
      }
      if (lv == 3) {
        if (l3_way)
          *l3_way = ways;
        ok = 1;
      }
    }
    if (ok)
      return 1;
  }

  return 0;
}

/* === Cores & Threads (Windows) === */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int get_cpu_core_thread_count(int *out_cores, int *out_threads) {
  if (out_cores)
    *out_cores = 0;
  if (out_threads)
    *out_threads = 0;
  DWORD len = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return 0;
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buf =
      (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)malloc(len);
  if (!buf)
    return 0;
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore, buf, &len)) {
    free(buf);
    return 0;
  }

  int cores = 0;
  BYTE *p = (BYTE *)buf;
  BYTE *end = p + len;
  while (p < end) {
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *e =
        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)p;
    if (e->Relationship == RelationProcessorCore)
      cores++;
    p += e->Size;
  }
  free(buf);

  DWORD threads = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (out_cores)
    *out_cores = cores;
  if (out_threads)
    *out_threads = (int)threads;
  return cores > 0 && threads > 0;
}
#else
int get_cpu_core_thread_count(int *out_cores, int *out_threads) {
  if (out_cores)
    *out_cores = 0;
  if (out_threads)
    *out_threads = 0;
  return 0;
}
#endif

/* === Base Frequency (MHz) — CPUID 0x16, иначе powrprof MaxMhz === */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <powrprof.h>
#include <windows.h>
#pragma comment(lib, "PowrProf.lib")

typedef struct _PROCESSOR_POWER_INFORMATION {
  ULONG Number;
  ULONG MaxMhz;
  ULONG CurrentMhz;
  ULONG MhzLimit;
  ULONG MaxIdleState;
  ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION;

static int get_base_from_powrprof(int *base_mhz) {
  if (base_mhz)
    *base_mhz = 0;
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  ULONG n = si.dwNumberOfProcessors;
  if (!n)
    return 0;
  size_t bytes = sizeof(PROCESSOR_POWER_INFORMATION) * n;
  PROCESSOR_POWER_INFORMATION *buf =
      (PROCESSOR_POWER_INFORMATION *)malloc(bytes);
  if (!buf)
    return 0;
  ULONG st =
      CallNtPowerInformation(ProcessorInformation, NULL, 0, buf, (ULONG)bytes);
  int ok = 0;
  if (st == 0) {
    ULONG maxmhz = 0;
    for (ULONG i = 0; i < n; ++i)
      if (buf[i].MaxMhz > maxmhz)
        maxmhz = buf[i].MaxMhz;
    if (maxmhz > 0) {
      if (base_mhz)
        *base_mhz = (int)maxmhz;
      ok = 1;
    }
  }
  free(buf);
  return ok;
}
#else
static int get_base_from_powrprof(int *base_mhz) {
  if (base_mhz)
    *base_mhz = 0;
  return 0;
}
#endif

int get_cpu_base_mhz(int *base_mhz) {
  if (base_mhz)
    *base_mhz = 0;
  if (cpuid_max_basic() >= 0x16) {
    unsigned r[4] = {0};
    cpuid_leaf(0x16, 0, r);
    if (r[0] > 0) {
      if (base_mhz)
        *base_mhz = (int)r[0];
      return 1;
    }
  }
  return get_base_from_powrprof(base_mhz);
}

/* === CPU instructions string === */
static void app_strcat(char *out, size_t n, const char *s) {
  size_t m = strlen(out), k = strlen(s);
  if (m >= n)
    return;
  if (m + k + 1 >= n)
    k = (n - 1) - m;
  memcpy(out + m, s, k);
  out[m + k] = 0;
}

int get_cpu_instructions(char *out, size_t n) {
  if (!out || n < 8)
    return 0;
  out[0] = 0;
  unsigned maxb = cpuid_max_basic(), maxe = cpuid_max_ext();
  if (maxb < 1)
    return 0;

  unsigned r1[4];
  cpuid_leaf(1, 0, r1);
  unsigned ecx1 = r1[2], edx1 = r1[3];
  int mmx = (edx1 >> 23) & 1, sse = (edx1 >> 25) & 1, sse2 = (edx1 >> 26) & 1,
      sse3 = (ecx1 >> 0) & 1, ssse3 = (ecx1 >> 9) & 1;
  int sse41 = (ecx1 >> 19) & 1, sse42 = (ecx1 >> 20) & 1,
      popcnt = (ecx1 >> 23) & 1, aes = (ecx1 >> 25) & 1,
      xsave = (ecx1 >> 26) & 1, osx = (ecx1 >> 27) & 1, avx = (ecx1 >> 28) & 1,
      fma3 = (ecx1 >> 12) & 1;

  unsigned ebx7 = 0, ecx7 = 0, edx7 = 0;
  if (maxb >= 7) {
    unsigned r7[4];
    cpuid_leaf(7, 0, r7);
    ebx7 = r7[1];
    ecx7 = r7[2];
    edx7 = r7[3];
  }
  int bmi1 = (ebx7 >> 3) & 1, avx2 = (ebx7 >> 5) & 1, bmi2 = (ebx7 >> 8) & 1,
      sha = (ebx7 >> 29) & 1;
  int avx512f = (ebx7 >> 16) & 1, avx512dq = (ebx7 >> 17) & 1,
      avx512bw = (ebx7 >> 30) & 1, avx512vl = (ebx7 >> 31) & 1;

  int lm = 0;
  if (maxe >= 0x80000001u) {
    unsigned re[4];
    cpuid_leaf(0x80000001u, 0, re);
    lm = (re[3] >> 29) & 1;
  }

  int avx_os = 0, avx512_os = 0;
  if (xsave && osx) {
    unsigned long long xcr0 = xgetbv0();
    avx_os = ((xcr0 & 0x6) == 0x6);
    avx512_os = avx_os && ((xcr0 & 0xE0) == 0xE0);
  }

  int comma = 0;
#define ADD(s)                                                                 \
  do {                                                                         \
    if (comma)                                                                 \
      app_strcat(out, n, ", ");                                                \
    app_strcat(out, n, (s));                                                   \
    comma = 1;                                                                 \
  } while (0)

  if (mmx)
    ADD("MMX");
  if (sse || sse2 || sse3 || sse41 || sse42) {
    char s[64];
    s[0] = 0;
    int first = 1;
    app_strcat(s, sizeof(s), "SSE (");
    if (sse) {
      app_strcat(s, sizeof(s), first ? "1" : ", 1");
      first = 0;
    }
    if (sse2) {
      app_strcat(s, sizeof(s), first ? "2" : ", 2");
      first = 0;
    }
    if (sse3) {
      app_strcat(s, sizeof(s), first ? "3" : ", 3");
      first = 0;
    }
    if (sse41) {
      app_strcat(s, sizeof(s), first ? "4.1" : ", 4.1");
      first = 0;
    }
    if (sse42) {
      app_strcat(s, sizeof(s), first ? "4.2" : ", 4.2");
      first = 0;
    }
    app_strcat(s, sizeof(s), ")");
    ADD(s);
  }
  if (ssse3)
    ADD("SSSE3");
  if (lm)
    ADD("EM64T");
  if (aes)
    ADD("AES");
  if (fma3)
    ADD("FMA3");
  if (avx && avx_os)
    ADD("AVX");
  if (avx2 && avx_os)
    ADD("AVX2");
  if (avx512f && avx512_os)
    ADD("AVX512");
  if (bmi1)
    ADD("BMI1");
  if (bmi2)
    ADD("BMI2");
  if (sha)
    ADD("SHA");
  if (popcnt)
    ADD("POPCNT");

#undef ADD
  return out[0] != 0;
}

/* === Family / Model / Stepping === */
/* out[0] = family, out[1] = model, out[2] = stepping */
int get_cpu_family_model_stepping(int out[3]) {
  if (!out)
    return 0;
  out[0] = out[1] = out[2] = 0;

  if (cpuid_max_basic() < 1)
    return 0;

  unsigned r[4];
  cpuid_leaf(1, 0, r);
  unsigned eax = r[0];

  unsigned stepping = eax & 0xF;
  unsigned model = (eax >> 4) & 0xF;
  unsigned family = (eax >> 8) & 0xF;
  unsigned ext_model = (eax >> 16) & 0xF;
  unsigned ext_family = (eax >> 20) & 0xFF; /* биты 20–27 */

  /* Интел/AMD спецификация: расширенные поля */
  if (family == 0x0F)
    family += ext_family;
  if (family == 0x06 || family == 0x0F)
    model += (ext_model << 4);

  out[0] = (int)family;
  out[1] = (int)model;
  out[2] = (int)stepping;
  return 1;
}

/* разность FILETIME → 64-битное число тиков (100 нс) */
static ULONGLONG ft_to_ull(const FILETIME *ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft->dwLowDateTime;
  u.HighPart = ft->dwHighDateTime;
  return u.QuadPart;
}

void get_cpu_load(Snapshot *s) {
  static int has_prev = 0;
  static FILETIME prev_idle = {0}, prev_kernel = {0}, prev_user = {0};

  FILETIME idle, kernel, user;
  if (!GetSystemTimes(&idle, &kernel, &user)) {
    return; // не удалось прочитать — просто оставляем старое значение
  }

  if (!has_prev) {
    /* первый вызов: только запоминаем, ничего не считаем */
    prev_idle = idle;
    prev_kernel = kernel;
    prev_user = user;
    has_prev = 1;
    return;
  }

  ULONGLONG idle_now = ft_to_ull(&idle);
  ULONGLONG kernel_now = ft_to_ull(&kernel);
  ULONGLONG user_now = ft_to_ull(&user);

  ULONGLONG idle_diff = idle_now - ft_to_ull(&prev_idle);
  ULONGLONG kernel_diff = kernel_now - ft_to_ull(&prev_kernel);
  ULONGLONG user_diff = user_now - ft_to_ull(&prev_user);

  /* kernel включает idle, user — нет */
  ULONGLONG total = kernel_diff + user_diff;
  ULONGLONG busy = total - idle_diff;

  double load = s->cpu_rt.load; // по умолчанию — старое
  if (total > 0 && busy <= total) {
    load = (double)busy * 100.0 / (double)total;
    if (load < 0.0)
      load = 0.0;
    if (load > 100.0)
      load = 100.0;
  }

  s->cpu_rt.load = load;

  prev_idle = idle;
  prev_kernel = kernel;
  prev_user = user;
}
