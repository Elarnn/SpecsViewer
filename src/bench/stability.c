/* CPU stability test, see stability.h.

   Determinism: the build has no -ffast-math and C11 mode keeps
   -ffp-contract=off, so the scalar float kernel gives bit-identical results on
   every core. The AVX2 kernel uses explicit FMA intrinsics, which are exact
   IEEE operations as well. A result that differs from the reference therefore
   means the core computed something wrong. */

#include "stability.h"

#include <immintrin.h>
#include <math.h>
#include <process.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winevt.h>

/* ---------- parameters ---------------------------------------------------- */

#define STAB_NSEEDS        16              /* distinct work units per test       */
#define STAB_INT_ITERS     (1u << 21)      /* ~5-15 ms per unit on a modern core */
#define STAB_FLT_ITERS     (1u << 21)
#define STAB_AVX_ITERS     (1u << 21)
#define STAB_CACHE_BYTES   (4u << 20)      /* per thread: L2/L3 and, summed, RAM   */
#define STAB_WHEA_POLL_SEC 5.0

/* the compute tests that are checked against a reference table */
enum { K_INT = 0, K_FLT, K_AVX, K_NCOMPUTE };

static const char *k_test_name[STAB_TEST_COUNT] = {
  "Mixed", "Integer", "Float", "AVX2 / FMA", "Cache / RAM"
};

static const char *k_test_desc[STAB_TEST_COUNT] = {
  "All tests in turn: load and power keep changing, catches transients.",
  "64/128-bit integer multiply, rotate and divide on every thread.",
  "Double precision chaotic math and square roots (scalar / SSE).",
  "256-bit FMA: the heaviest power and heat load, checks Vcore.",
  "Writes and verifies a random pattern in cache and RAM, 4 MB/thread."
};

const char *stab_test_name(int test) {
  return (test >= 0 && test < STAB_TEST_COUNT) ? k_test_name[test] : "";
}

const char *stab_test_description(int test) {
  return (test >= 0 && test < STAB_TEST_COUNT) ? k_test_desc[test] : "";
}

int stab_avx2_supported(void) {
  static int cached = -1;
  if (cached < 0) {
    __builtin_cpu_init();
    /* libgcc also checks via XGETBV that the OS saves the YMM state */
    cached = (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma")) ? 1 : 0;
  }
  return cached;
}

/* ---------- helpers ------------------------------------------------------- */

static inline uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

static inline uint64_t rotl64(uint64_t x, unsigned r) {
  r &= 63u;
  return r ? (x << r) | (x >> (64u - r)) : x;
}

static inline uint64_t dbits(double d) {
  uint64_t u;
  memcpy(&u, &d, sizeof(u));
  return u;
}

/* a double in [0.2, 0.8) from a 64-bit value */
static inline double seed_unit(uint64_t s) {
  return 0.2 + 0.6 * (double)(s >> 11) * (1.0 / 9007199254740992.0);
}

static uint64_t unit_seed(int kind, int index) {
  return splitmix64(0x5EC0DEull * (uint64_t)(kind + 1) + (uint64_t)index * 0x1F123BB5ull);
}

/* ---------- kernels ------------------------------------------------------- */

__attribute__((noinline))
static uint64_t kernel_integer(uint64_t seed) {
  uint64_t a[8];
  for (int l = 0; l < 8; l++) a[l] = splitmix64(seed + (uint64_t)l);

  for (uint32_t it = 0; it < STAB_INT_ITERS; it++) {
    for (int l = 0; l < 8; l++) {
      unsigned __int128 m = (unsigned __int128)a[l] * 0x9E3779B97F4A7C15ull;
      uint64_t x = (uint64_t)m ^ (uint64_t)(m >> 64);
      x = rotl64(x, (unsigned)(x >> 58) + (unsigned)l) + (x >> 29) + it;
      a[l] = x;
    }
    if ((it & 255u) == 0) {   /* keep the divider busy too */
      a[0] ^= a[1] / (a[2] | 1u);
      a[3] += a[4] % ((a[5] >> 3) | 1u);
    }
  }

  uint64_t h = 0;
  for (int l = 0; l < 8; l++) h = splitmix64(h ^ a[l]);
  return h;
}

/* Logistic map x = r*x*(1-x) at r close to 4 is chaotic: one wrong bit
   anywhere grows into a completely different final state. */
__attribute__((noinline))
static uint64_t kernel_float(uint64_t seed) {
  double x[16];
  for (int l = 0; l < 16; l++) x[l] = seed_unit(splitmix64(seed + (uint64_t)l));
  const double r = 3.9999;

  for (uint32_t it = 0; it < STAB_FLT_ITERS; it++) {
    for (int l = 0; l < 16; l++) x[l] = r * x[l] * (1.0 - x[l]);
    if ((it & 255u) == 255u)
      for (int l = 0; l < 16; l++) x[l] = sqrt(x[l]) * 0.5 + 0.25;
  }

  uint64_t h = 0;
  for (int l = 0; l < 16; l++) h = splitmix64(h ^ dbits(x[l]));
  return h;
}

/* Same map on 12 x 4 doubles with FMA: x - x*x is one fused op. Twelve
   independent chains hide the FMA latency on both ports. */
__attribute__((noinline, target("avx2,fma")))
static uint64_t kernel_avx2(uint64_t seed) {
  __m256d x[12];
  for (int l = 0; l < 12; l++) {
    uint64_t s = splitmix64(seed + (uint64_t)l * 4u);
    x[l] = _mm256_set_pd(seed_unit(splitmix64(s + 3)), seed_unit(splitmix64(s + 2)),
                         seed_unit(splitmix64(s + 1)), seed_unit(s));
  }
  const __m256d r    = _mm256_set1_pd(3.9999);
  const __m256d half = _mm256_set1_pd(0.5);
  const __m256d qtr  = _mm256_set1_pd(0.25);

  for (uint32_t it = 0; it < STAB_AVX_ITERS; it++) {
    for (int l = 0; l < 12; l++) {
      __m256d t = _mm256_fnmadd_pd(x[l], x[l], x[l]);  /* x - x*x */
      x[l] = _mm256_mul_pd(t, r);
    }
    if ((it & 255u) == 255u)
      for (int l = 0; l < 12; l++)
        x[l] = _mm256_fmadd_pd(_mm256_sqrt_pd(x[l]), half, qtr);
  }

  double out[48];
  for (int l = 0; l < 12; l++) _mm256_storeu_pd(out + 4 * l, x[l]);
  uint64_t h = 0;
  for (int i = 0; i < 48; i++) h = splitmix64(h ^ dbits(out[i]));
  return h;
}

static inline uint64_t xorshift64(uint64_t *s) {
  uint64_t x = *s;
  x ^= x << 13; x ^= x >> 7; x ^= x << 17;
  return *s = x;
}

/* Returns the number of 64-bit words that did not read back as written. */
__attribute__((noinline))
static uint64_t kernel_cache(uint64_t *buf, size_t n, uint64_t seed) {
  uint64_t bad = 0, s;

  s = seed | 1u;
  for (size_t i = 0; i < n; i++) buf[i] = xorshift64(&s);
  __asm__ __volatile__("" ::: "memory");   /* force real reads below */

  s = seed | 1u;
  for (size_t i = 0; i < n; i++) {
    uint64_t v = xorshift64(&s);
    bad += (buf[i] != v);
    buf[i] = ~v ^ (uint64_t)i;               /* every bit flips on the second pass */
  }
  __asm__ __volatile__("" ::: "memory");

  s = seed | 1u;
  for (size_t i = 0; i < n; i++) {
    uint64_t v = xorshift64(&s);
    bad += (buf[i] != (~v ^ (uint64_t)i));
  }
  return bad;
}

static uint64_t run_compute(int kind, uint64_t seed) {
  switch (kind) {
  case K_INT: return kernel_integer(seed);
  case K_FLT: return kernel_float(seed);
  case K_AVX: return kernel_avx2(seed);
  default:    return 0;
  }
}

/* ---------- topology ------------------------------------------------------ */

static int stab_logical_count(void) {
  int n = 0;
  WORD ng = GetActiveProcessorGroupCount();
  for (WORD g = 0; g < ng; g++) n += (int)GetActiveProcessorCount(g);
  if (n < 1) n = 1;
  return (n > STAB_MAX_CPUS) ? STAB_MAX_CPUS : n;
}

/* logical index -> (group, bit) */
static int stab_lp_location(int index, WORD *group, int *bit) {
  WORD ng = GetActiveProcessorGroupCount();
  int base = 0;
  for (WORD g = 0; g < ng; g++) {
    int cnt = (int)GetActiveProcessorCount(g);
    if (index < base + cnt) { *group = g; *bit = index - base; return 1; }
    base += cnt;
  }
  return 0;
}

static int stab_group_base(WORD group) {
  int base = 0;
  for (WORD g = 0; g < group; g++) base += (int)GetActiveProcessorCount(g);
  return base;
}

/* Fills core / eff_class; returns 1 if the CPU is hybrid. */
static int stab_topology(StabCpu *cpus, int ncpu) {
  for (int i = 0; i < ncpu; i++) { cpus[i].core = -1; cpus[i].eff_class = 0; }

  DWORD len = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
  if (len == 0) return 0;
  BYTE *buf = (BYTE *)malloc(len);
  if (!buf) return 0;
  if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len)) {
    free(buf);
    return 0;
  }

  int core = 0, minc = 255, maxc = 0;
  for (DWORD off = 0; off < len;) {
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX e =
        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buf + off);
    if (e->Size == 0) break;
    if (e->Relationship == RelationProcessorCore) {
      int ec = e->Processor.EfficiencyClass;
      if (ec < minc) minc = ec;
      if (ec > maxc) maxc = ec;
      for (WORD g = 0; g < e->Processor.GroupCount; g++) {
        int base = stab_group_base(e->Processor.GroupMask[g].Group);
        KAFFINITY m = e->Processor.GroupMask[g].Mask;
        for (int b = 0; b < (int)(sizeof(KAFFINITY) * 8); b++) {
          if (!(m & ((KAFFINITY)1 << b))) continue;
          int idx = base + b;
          if (idx >= 0 && idx < ncpu) { cpus[idx].core = core; cpus[idx].eff_class = ec; }
        }
      }
      core++;
    }
    off += e->Size;
  }
  free(buf);
  return (maxc > minc) ? 1 : 0;
}

/* ---------- WHEA event log ------------------------------------------------ */

/* Number of WHEA-Logger events in the System log since `since` (UTC),
   -1 if the log cannot be queried. */
static int whea_count_since(const SYSTEMTIME *since) {
  wchar_t q[320];
  _snwprintf(q, sizeof(q) / sizeof(q[0]),
             L"*[System[Provider[@Name='Microsoft-Windows-WHEA-Logger'] and "
             L"TimeCreated[@SystemTime>='%04u-%02u-%02uT%02u:%02u:%02u.000Z']]]",
             since->wYear, since->wMonth, since->wDay,
             since->wHour, since->wMinute, since->wSecond);
  q[(sizeof(q) / sizeof(q[0])) - 1] = 0;

  EVT_HANDLE h = EvtQuery(NULL, L"System", q, EvtQueryChannelPath | EvtQueryForwardDirection);
  if (!h) return -1;

  int n = 0;
  EVT_HANDLE ev[32];
  DWORD got = 0;
  while (EvtNext(h, 32, ev, 0, 0, &got)) {
    for (DWORD i = 0; i < got; i++) EvtClose(ev[i]);
    n += (int)got;
    got = 0;
  }
  EvtClose(h);
  return n;
}

/* ---------- state helpers ------------------------------------------------- */

static uint64_t g_ref[K_NCOMPUTE][STAB_NSEEDS];

static double stab_now_sec(const StabilityState *st) {
  LARGE_INTEGER f, now;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&now);
  return (double)(now.QuadPart - st->start_qpc.QuadPart) / (double)f.QuadPart;
}

static void stab_log(StabilityState *st, const char *fmt, ...) {
  char msg[STAB_LOG_LEN - 12];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  unsigned t = (unsigned)stab_now_sec(st);
  EnterCriticalSection(&st->cs);
  char *line = st->log[st->log_count % STAB_LOG_LINES];
  snprintf(line, STAB_LOG_LEN, "[%02u:%02u:%02u] %s", (t / 3600u) % 100u, (t / 60u) % 60u, t % 60u, msg);
  st->log_count++;
  LeaveCriticalSection(&st->cs);
}

static void stab_set_status(StabilityState *st, enum StabStatus s) {
  InterlockedExchange(&st->status, (LONG)s);
}

/* ---------- workers ------------------------------------------------------- */

typedef struct {
  StabilityState *st;
  int             index;
  int             kinds[STAB_TEST_COUNT];  /* sequence of test kinds this run uses */
  int             nkinds;
  uint64_t       *cache;                   /* STAB_CACHE_BYTES, NULL if not used  */
} StabWorker;

enum { KIND_INT = 0, KIND_FLT, KIND_AVX, KIND_CACHE };

static const char *kind_name(int kind) {
  switch (kind) {
  case KIND_INT: return "Integer";
  case KIND_FLT: return "Float";
  case KIND_AVX: return "AVX2";
  default:       return "Cache/RAM";
  }
}

static void stab_report_error(StabilityState *st, int cpu, int kind, const char *what) {
  StabCpu *c = &st->cpus[cpu];
  InterlockedIncrement(&c->errors);
  InterlockedIncrement(&st->total_errors);
  if (c->core >= 0)
    stab_log(st, "CPU %d (core %d): %s %s", cpu, c->core, kind_name(kind), what);
  else
    stab_log(st, "CPU %d: %s %s", cpu, kind_name(kind), what);
  if (st->stop_on_error) SetEvent(st->stop_event);
}

static unsigned __stdcall stab_worker(void *arg) {
  StabWorker *w = (StabWorker *)arg;
  StabilityState *st = w->st;
  StabCpu *c = &st->cpus[w->index];
  long long units = 0;

  for (unsigned u = 0;; u++) {
    if (WaitForSingleObject(st->stop_event, 0) == WAIT_OBJECT_0) break;

    /* threads start at different points of the sequence so neighbouring
       cores do not switch load at the same moment */
    int kind = w->kinds[(u + (unsigned)w->index) % (unsigned)w->nkinds];
    int sidx = (int)((u / (unsigned)w->nkinds + (unsigned)w->index * 5u) % STAB_NSEEDS);

    if (kind == KIND_CACHE) {
      uint64_t seed = unit_seed(7, sidx) ^ ((uint64_t)u << 32);
      uint64_t bad  = kernel_cache(w->cache, STAB_CACHE_BYTES / sizeof(uint64_t), seed);
      if (bad) {
        char what[64];
        snprintf(what, sizeof(what), "read-back mismatch (%llu words)", (unsigned long long)bad);
        stab_report_error(st, w->index, kind, what);
      }
    } else {
      uint64_t got = run_compute(kind, unit_seed(kind, sidx));
      if (got != g_ref[kind][sidx])
        stab_report_error(st, w->index, kind, "result mismatch");
    }
    c->units = ++units;
  }
  return 0;
}

/* ---------- controller ---------------------------------------------------- */

static int build_kinds(int test, int *kinds) {
  int n = 0;
  int avx = stab_avx2_supported();
  switch (test) {
  case STAB_TEST_INTEGER: kinds[n++] = KIND_INT; break;
  case STAB_TEST_FLOAT:   kinds[n++] = KIND_FLT; break;
  case STAB_TEST_AVX2:    kinds[n++] = avx ? KIND_AVX : KIND_FLT; break;
  case STAB_TEST_CACHE:   kinds[n++] = KIND_CACHE; break;
  default:
    kinds[n++] = KIND_INT;
    kinds[n++] = avx ? KIND_AVX : KIND_FLT;
    kinds[n++] = KIND_CACHE;
    kinds[n++] = KIND_FLT;
    break;
  }
  return n;
}

/* Reference results, computed twice on one thread before the load starts.
   Returns 0 when the two passes disagree (CPU unstable even at light load). */
static int compute_references(StabilityState *st, const int *kinds, int nkinds) {
  int need[K_NCOMPUTE] = {0};
  for (int i = 0; i < nkinds; i++) if (kinds[i] < K_NCOMPUTE) need[kinds[i]] = 1;

  for (int k = 0; k < K_NCOMPUTE; k++) {
    if (!need[k]) continue;
    for (int s = 0; s < STAB_NSEEDS; s++) {
      if (WaitForSingleObject(st->stop_event, 0) == WAIT_OBJECT_0) return 1;
      uint64_t a = run_compute(k, unit_seed(k, s));
      uint64_t b = run_compute(k, unit_seed(k, s));
      if (a != b) {
        stab_log(st, "%s: reference passes differ, CPU is unstable at light load", kind_name(k));
        InterlockedIncrement(&st->total_errors);
        return 0;
      }
      g_ref[k][s] = a;
    }
  }
  return 1;
}

static DWORD WINAPI stab_main(LPVOID param) {
  StabilityState *st = (StabilityState *)param;
  StabWorker *workers = NULL;
  HANDLE *threads = NULL;
  int nthreads = 0;
  int start_failed = 0;
  enum StabStatus final = STAB_STATUS_ERROR;

  int kinds[STAB_TEST_COUNT];
  int nkinds = build_kinds(st->test, kinds);
  int need_cache = 0;
  for (int i = 0; i < nkinds; i++) if (kinds[i] == KIND_CACHE) need_cache = 1;

  QueryPerformanceCounter(&st->start_qpc);
  stab_set_status(st, STAB_STATUS_PREPARING);

  if (!compute_references(st, kinds, nkinds)) { final = STAB_STATUS_FAILED; goto done; }
  if (WaitForSingleObject(st->stop_event, 0) == WAIT_OBJECT_0) {
    final = STAB_STATUS_STOPPED;
    goto done;
  }

  workers = (StabWorker *)calloc((size_t)st->ncpu, sizeof(StabWorker));
  threads = (HANDLE *)calloc((size_t)st->ncpu, sizeof(HANDLE));
  if (!workers || !threads) { stab_log(st, "Out of memory"); goto done; }

  for (int i = 0; i < st->ncpu; i++) {
    StabWorker *w = &workers[i];
    w->st = st;
    w->index = i;
    w->nkinds = nkinds;
    memcpy(w->kinds, kinds, sizeof(kinds));
    if (need_cache) {
      w->cache = (uint64_t *)VirtualAlloc(NULL, STAB_CACHE_BYTES, MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
      if (!w->cache) { stab_log(st, "Out of memory for the cache test buffers"); goto done; }
    }
  }

  SYSTEMTIME whea_since;
  GetSystemTime(&whea_since);
  QueryPerformanceCounter(&st->start_qpc);   /* elapsed time counts the load only */
  stab_set_status(st, STAB_STATUS_RUNNING);
  stab_log(st, "Started: %s, %d threads", stab_test_name(st->test), st->ncpu);

  for (int i = 0; i < st->ncpu; i++) {
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, stab_worker, &workers[i], CREATE_SUSPENDED, NULL);
    if (!h) { stab_log(st, "Could not create worker thread %d", i); start_failed = 1; SetEvent(st->stop_event); break; }
    WORD g; int bit;
    if (stab_lp_location(i, &g, &bit)) {
      GROUP_AFFINITY ga;
      memset(&ga, 0, sizeof(ga));
      ga.Group = g;
      ga.Mask  = (KAFFINITY)1 << bit;
      SetThreadGroupAffinity(h, &ga, NULL);
    }
    /* below normal: the UI thread still gets the CPU when it needs it */
    SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
    threads[nthreads++] = h;
    ResumeThread(h);
  }

  double next_whea = STAB_WHEA_POLL_SEC;
  int completed = 0;
  for (;;) {
    DWORD wr = WaitForSingleObject(st->stop_event, 250);
    double t = stab_now_sec(st);
    EnterCriticalSection(&st->cs);
    st->elapsed_sec = t;
    LeaveCriticalSection(&st->cs);

    if (t >= next_whea) {
      int n = whea_count_since(&whea_since);
      EnterCriticalSection(&st->cs);
      int prev = st->whea_errors;
      st->whea_errors = n;
      LeaveCriticalSection(&st->cs);
      if (n > prev && prev >= 0) stab_log(st, "Windows logged %d new WHEA hardware error(s)", n - prev);
      next_whea = t + STAB_WHEA_POLL_SEC;
    }

    if (wr == WAIT_OBJECT_0) break;
    if (st->duration_sec > 0.0 && t >= st->duration_sec) {
      completed = 1;
      SetEvent(st->stop_event);
      break;
    }
  }

  /* threads poll the stop event between units, so this takes < ~50 ms */
  for (int i = 0; i < nthreads; i++) {
    WaitForSingleObject(threads[i], INFINITE);
    CloseHandle(threads[i]);
  }
  nthreads = 0;

  {
    int n = whea_count_since(&whea_since);
    EnterCriticalSection(&st->cs);
    int prev = st->whea_errors;
    st->whea_errors = n;
    st->elapsed_sec = stab_now_sec(st);
    LeaveCriticalSection(&st->cs);
    if (n > prev && prev >= 0) stab_log(st, "Windows logged %d new WHEA hardware error(s)", n - prev);
  }

  if (st->total_errors > 0)  final = STAB_STATUS_FAILED;
  else if (start_failed)     final = STAB_STATUS_ERROR;
  else if (completed)        final = STAB_STATUS_PASSED;
  else                       final = STAB_STATUS_STOPPED;

  if (final == STAB_STATUS_FAILED)
    stab_log(st, "Finished with %ld calculation error(s)", (long)st->total_errors);
  else if (final == STAB_STATUS_PASSED)
    stab_log(st, "Finished: no calculation errors");
  else
    stab_log(st, "Stopped by user: no calculation errors");

done:
  for (int i = 0; i < nthreads; i++) {   /* only after a failed start */
    SetEvent(st->stop_event);
    WaitForSingleObject(threads[i], INFINITE);
    CloseHandle(threads[i]);
  }
  if (workers) {
    for (int i = 0; i < st->ncpu; i++)
      if (workers[i].cache) VirtualFree(workers[i].cache, 0, MEM_RELEASE);
  }
  free(workers);
  free(threads);
  if (final == STAB_STATUS_ERROR && st->total_errors > 0) final = STAB_STATUS_FAILED;
  stab_set_status(st, final);
  InterlockedExchange(&st->running, 0);
  return 0;
}

/* ---------- public API ---------------------------------------------------- */

void stab_init(StabilityState *st) {
  memset(st, 0, sizeof(*st));
  InitializeCriticalSection(&st->cs);
  st->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
  st->status = STAB_STATUS_IDLE;
  st->ncpu = stab_logical_count();
  st->cpus = (StabCpu *)_aligned_malloc((size_t)st->ncpu * sizeof(StabCpu), 64);
  if (st->cpus) {
    memset(st->cpus, 0, (size_t)st->ncpu * sizeof(StabCpu));
    st->hybrid = stab_topology(st->cpus, st->ncpu);
  }
}

static void stab_join(StabilityState *st) {
  if (st->thread) {
    WaitForSingleObject(st->thread, INFINITE);
    CloseHandle(st->thread);
    st->thread = NULL;
  }
}

void stab_shutdown(StabilityState *st) {
  stab_stop(st);
  stab_join(st);
  if (st->stop_event) CloseHandle(st->stop_event);
  st->stop_event = NULL;
  if (st->cpus) _aligned_free(st->cpus);
  st->cpus = NULL;
  DeleteCriticalSection(&st->cs);
}

int stab_start(StabilityState *st, int test, double duration_sec, int stop_on_error) {
  if (!st->cpus || !st->stop_event || st->running) return 0;
  stab_join(st);

  if (test < 0 || test >= STAB_TEST_COUNT) test = STAB_TEST_MIXED;
  if (test == STAB_TEST_AVX2 && !stab_avx2_supported()) test = STAB_TEST_FLOAT;

  EnterCriticalSection(&st->cs);
  st->test          = test;
  st->duration_sec  = duration_sec > 0.0 ? duration_sec : 0.0;
  st->stop_on_error = stop_on_error ? 1 : 0;
  st->elapsed_sec   = 0.0;
  st->whea_errors   = 0;
  st->log_count     = 0;
  st->total_errors  = 0;
  st->user_stop     = 0;
  for (int i = 0; i < st->ncpu; i++) { st->cpus[i].units = 0; st->cpus[i].errors = 0; }
  LeaveCriticalSection(&st->cs);

  ResetEvent(st->stop_event);
  st->status  = STAB_STATUS_PREPARING;
  st->running = 1;
  st->thread  = CreateThread(NULL, 0, stab_main, st, 0, NULL);
  if (!st->thread) {
    st->running = 0;
    st->status  = STAB_STATUS_ERROR;
    return 0;
  }
  return 1;
}

void stab_stop(StabilityState *st) {
  if (!st->stop_event) return;
  InterlockedExchange(&st->user_stop, 1);
  SetEvent(st->stop_event);
}

void stab_read_ui(StabilityState *st, StabUiState *out) {
  out->running      = (int)st->running;
  out->status       = (enum StabStatus)st->status;
  out->test         = st->test;
  out->duration_sec = st->duration_sec;
  out->total_errors = (int)st->total_errors;
  out->ncpu         = st->cpus ? st->ncpu : 0;
  out->hybrid       = st->hybrid;
  out->total_units  = 0;
  for (int i = 0; i < out->ncpu; i++) {
    out->cpus[i].units     = (long long)st->cpus[i].units;
    out->cpus[i].errors    = (int)st->cpus[i].errors;
    out->cpus[i].core      = st->cpus[i].core;
    out->cpus[i].eff_class = st->cpus[i].eff_class;
    out->total_units      += out->cpus[i].units;
  }

  EnterCriticalSection(&st->cs);
  out->elapsed_sec = st->elapsed_sec;
  out->whea_errors = st->whea_errors;
  int n = st->log_count < STAB_LOG_LINES ? st->log_count : STAB_LOG_LINES;
  int first = st->log_count - n;
  for (int i = 0; i < n; i++)
    memcpy(out->log[i], st->log[(first + i) % STAB_LOG_LINES], STAB_LOG_LEN);
  out->log_n = n;
  LeaveCriticalSection(&st->cs);
}
