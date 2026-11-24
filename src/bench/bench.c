#include "bench/bench.h"

#include <limits.h>
#include <malloc.h>
#include <math.h>
#include <process.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * CPU benchmark, kernel v2 (SpecsViewer 2.2)
 *
 * Five independent sub-tests, each with a fixed working set (independent of
 * the amount of system RAM) and a different performance profile:
 *
 *   1. Integer  Murmur3-style hashing of an 8 KB buffer in 4 interleaved
 *               lanes: integer multiply / rotate throughput, instruction-level
 *               parallelism.
 *   2. Float    32x32 double matrix multiply: vectorised FP multiply-add,
 *               L1 bandwidth.
 *   3. Memory   random pointer chasing over a 512 KB cyclic permutation with
 *               4 independent chains: L2/L3 latency, memory-level parallelism.
 *   4. Sort     quicksort of 2048 pseudo-random ints: data-dependent branches,
 *               branch prediction, compare/move throughput.
 *   5. Scalar   dependent sqrt and xorshift chains: pure latency, i.e. clock
 *               speed and execution latency of a single core.
 *
 * Each sub-test runs for an equal time slice (single-thread: one worker,
 * multithread: one worker per logical processor). Its rate is work units per
 * second, summed over the workers. The composite score is the geometric mean
 * of the five rates, each normalised to the calibration CPU (i5-11400H,
 * single thread = 1000 points):
 *
 *     points = 1000 * geomean_i( rate_i / cal_i )
 *
 * The geometric mean gives every sub-test the same weight, so no single code
 * pattern can dominate the result. (The v1 kernel was a sum of serial
 * dependency chains and therefore measured little more than clock frequency,
 * which is why an old desktop core at 4.5 GHz could beat a newer mobile core.)
 *
 * Fairness measures:
 *   - the single-thread worker is restricted to the fastest core class on
 *     hybrid CPUs (P-cores) through CPU sets;
 *   - multithread workers are spread over all processor groups (> 64 LPs);
 *   - per-thread counters live on their own cache lines and are published
 *     every few calls, so workers never share a cache line (no false sharing);
 *   - the same binary (SSE2 baseline, no runtime dispatch) runs on every CPU.
 *
 * Result verification: every BENCH_CHECK_EVERY-th call of a worker is a check
 * call. It resets the mutable part of the workspace to the canonical state
 * (identical on every thread), runs the normal kernel with a fixed seed and
 * compares a hash of the output with a reference computed before the run.
 * The check call does the same work as a normal call and is counted as one,
 * so verification costs only the reset and the hash (well below 1 %). A
 * mismatch means the core computed a wrong value: the result is flagged as
 * invalid and cannot be submitted.
 */

#define BENCH_POINTS_SCALE  1000.0
#define BENCH_PUBLISH_EVERY 8u
#define BENCH_CHECK_EVERY   128u          /* one check call per N kernel calls */
#define BENCH_CHECK_SEED    0x2545F491u
#define BENCH_DATA_SEED     0xC0FFEE01u   /* workspace data, same on every thread */

/* working set sizes */
#define WS_INT_WORDS   2048u      /*   8 KB uint32 buffer                 */
#define WS_INT_ROUNDS  16
#define WS_MAT_N       32         /* 3 x 8 KB doubles                     */
#define WS_CHASE_N     131072u    /* 512 KB cyclic permutation (uint32)   */
#define WS_CHASE_STEPS 16384
#define WS_SORT_N      2048       /*   8 KB int32                         */
#define WS_SCALAR_SQRT 512
#define WS_SCALAR_XOR  4096

#define BENCH_DEFAULT_REF_CPU    "i5-11400H"
#define BENCH_DEFAULT_REF_SINGLE 1000.0
#define BENCH_DEFAULT_REF_MULTI  6700.0   /* i5-11400H, 12 threads */

/* Calibration: single-thread rates (work units / s) of the reference CPU
   (i5-11400H, Release build, 2026-11). They only fix the absolute scale of
   the points; the ratio between two machines does not depend on them
   (geometric mean). */
static const double k_cal_rate[BENCH_SUBTESTS] = {
    1.547e9, /* Integer: words hashed / s     */
    2.974e9, /* Float:   multiply-adds / s    */
    9.195e8, /* Memory:  dependent loads / s  */
    2.709e7, /* Sort:    elements sorted / s  */
    4.034e8, /* Scalar:  dependent ops / s    */
};

static const char *k_sub_name[BENCH_SUBTESTS] = {
    "Integer", "Float", "Memory", "Sort", "Scalar"
};

/* work units done by one call of each sub-test kernel */
static const double k_sub_units[BENCH_SUBTESTS] = {
    (double)WS_INT_WORDS * WS_INT_ROUNDS,
    (double)WS_MAT_N * WS_MAT_N * WS_MAT_N,
    4.0 * WS_CHASE_STEPS,
    (double)WS_SORT_N,
    (double)(WS_SCALAR_SQRT + WS_SCALAR_XOR),
};

static BenchReference  g_refs[BENCH_MAX_REFS];
static int             g_ref_count    = 0;
static int             g_ref_selected = -1;
static int             g_ref_loaded   = 0;

/* ---------- workspace ---------------------------------------------------- */

typedef struct {
    uint32_t        ibuf[WS_INT_WORDS];
    double          A[WS_MAT_N * WS_MAT_N];
    double          B[WS_MAT_N * WS_MAT_N];
    double          C[WS_MAT_N * WS_MAT_N];
    int32_t         sortbuf[WS_SORT_N];
    uint32_t        chase[WS_CHASE_N];
    volatile double sink;   /* keeps the optimiser from dropping the work */
} BenchWorkspace;

static inline uint32_t lcg_next(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* Integer buffer; returns the generator state after it. */
static uint32_t ws_fill_ibuf(BenchWorkspace *ws) {
    uint32_t s = BENCH_DATA_SEED | 1u;
    for (uint32_t i = 0; i < WS_INT_WORDS; i++)
        ws->ibuf[i] = lcg_next(&s);
    return s;
}

static void ws_fill_matrix_a(BenchWorkspace *ws) {
    for (int i = 0; i < WS_MAT_N * WS_MAT_N; i++)
        ws->A[i] = 1.0 + (double)(i % 17) * 0.01;
}

/* The data is the same on every thread (the per-thread seeds only drive
   which elements are visited), so a check call can be compared across cores. */
static void ws_init(BenchWorkspace *ws) {
    ws->sink = 0.0;
    uint32_t s = ws_fill_ibuf(ws);

    ws_fill_matrix_a(ws);
    for (int i = 0; i < WS_MAT_N * WS_MAT_N; i++) {
        ws->B[i] = 1.0 - (double)(i % 13) * 0.01;
        ws->C[i] = 0.0;
    }

    for (int i = 0; i < WS_SORT_N; i++)
        ws->sortbuf[i] = 0;

    /* Sattolo's algorithm: one cycle through all WS_CHASE_N elements, so a
       chain visits the whole 512 KB working set in pseudo-random order. */
    for (uint32_t i = 0; i < WS_CHASE_N; i++)
        ws->chase[i] = i;
    for (uint32_t i = WS_CHASE_N - 1; i > 0; i--) {
        uint32_t j = (uint32_t)(((uint64_t)lcg_next(&s) * (uint64_t)i) >> 32); /* [0, i) */
        uint32_t t = ws->chase[i];
        ws->chase[i] = ws->chase[j];
        ws->chase[j] = t;
    }
}

/* ---------- sub-test kernels --------------------------------------------- */

static inline uint32_t rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

#define MURMUR_STEP(h, k)                 \
    do {                                  \
        (k) *= 0xCC9E2D51u;               \
        (k) = rotl32((k), 15);            \
        (k) *= 0x1B873593u;               \
        (h) ^= (k);                       \
        (h) = rotl32((h), 13);            \
        (h) = (h) * 5u + 0xE6546B64u;     \
    } while (0)

/* 1. Integer: 4 interleaved Murmur3 lanes over the 8 KB buffer. */
static void sub_integer(BenchWorkspace *ws, uint32_t *seed) {
    uint32_t h0 = *seed, h1 = h0 ^ 0x9E3779B9u, h2 = h0 * 0x85EBCA6Bu, h3 = ~h0;
    uint32_t *buf = ws->ibuf;

    for (int r = 0; r < WS_INT_ROUNDS; r++) {
        for (uint32_t i = 0; i < WS_INT_WORDS; i += 4) {
            uint32_t k0 = buf[i], k1 = buf[i + 1], k2 = buf[i + 2], k3 = buf[i + 3];
            MURMUR_STEP(h0, k0);
            MURMUR_STEP(h1, k1);
            MURMUR_STEP(h2, k2);
            MURMUR_STEP(h3, k3);
        }
        /* feed the round result back so rounds cannot be merged or hoisted */
        buf[(h0 + (uint32_t)r) & (WS_INT_WORDS - 1u)] = h0 ^ h1 ^ h2 ^ h3;
    }

    *seed = h0 ^ rotl32(h1, 7) ^ rotl32(h2, 14) ^ rotl32(h3, 21);
    ws->sink += (double)(*seed & 0xFFFFu) * 1e-12;
}

/* 2. Float: C = A x B, 32x32 doubles (inner loop vectorises with SSE2). */
static void sub_float(BenchWorkspace *ws, uint32_t *seed) {
    const int n = WS_MAT_N;
    double *A = ws->A, *B = ws->B, *C = ws->C;

    for (int i = 0; i < n; i++) {
        double *ci = C + i * n;
        for (int j = 0; j < n; j++)
            ci[j] = 0.0;
        for (int k = 0; k < n; k++) {
            const double  a  = A[i * n + k];
            const double *bk = B + k * n;
            for (int j = 0; j < n; j++)
                ci[j] += a * bk[j];
        }
    }

    /* perturb one input so consecutive calls are not identical */
    uint32_t s = lcg_next(seed);
    A[s & (uint32_t)(n * n - 1)] = 1.0 + (double)((s >> 8) & 0xFFu) * 0.001
                                 + C[(s >> 16) & (uint32_t)(n * n - 1)] * 1e-12;
    ws->sink += C[(s >> 4) & (uint32_t)(n * n - 1)] * 1e-12;
}

/* 3. Memory: 4 independent random walks over the 512 KB permutation. */
static void sub_memory(BenchWorkspace *ws, uint32_t *seed) {
    const uint32_t *p = ws->chase;
    const uint32_t mask = WS_CHASE_N - 1u;
    uint32_t i0 = *seed & mask;
    uint32_t i1 = (i0 + WS_CHASE_N / 4u) & mask;
    uint32_t i2 = (i0 + WS_CHASE_N / 2u) & mask;
    uint32_t i3 = (i0 + 3u * WS_CHASE_N / 4u) & mask;

    for (int k = 0; k < WS_CHASE_STEPS; k++) {
        i0 = p[i0];
        i1 = p[i1];
        i2 = p[i2];
        i3 = p[i3];
    }

    *seed = lcg_next(seed) ^ i0 ^ (i1 << 8) ^ (i2 << 16) ^ (i3 << 24);
    ws->sink += (double)(i0 + i1 + i2 + i3) * 1e-12;
}

/* 4. Sort: quicksort (median of three, insertion sort for short runs). */
static void sort_insertion(int32_t *a, int lo, int hi) {
    for (int i = lo + 1; i <= hi; i++) {
        int32_t v = a[i];
        int j = i - 1;
        while (j >= lo && a[j] > v) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = v;
    }
}

static void sort_quick(int32_t *a, int lo, int hi) {
    while (hi - lo > 16) {
        int mid = lo + ((hi - lo) >> 1);
        int32_t x = a[lo], y = a[mid], z = a[hi];
        int32_t p = (x < y) ? ((y < z) ? y : ((x < z) ? z : x))
                            : ((x < z) ? x : ((y < z) ? z : y));
        int i = lo, j = hi;
        while (i <= j) {
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) {
                int32_t t = a[i]; a[i] = a[j]; a[j] = t;
                i++; j--;
            }
        }
        /* recurse into the smaller part, iterate on the larger: depth O(log n) */
        if (j - lo < hi - i) { sort_quick(a, lo, j); lo = i; }
        else                 { sort_quick(a, i, hi); hi = j; }
    }
    sort_insertion(a, lo, hi);
}

static void sub_sort(BenchWorkspace *ws, uint32_t *seed) {
    int32_t *a = ws->sortbuf;
    for (int i = 0; i < WS_SORT_N; i++)
        a[i] = (int32_t)(lcg_next(seed) >> 1);

    sort_quick(a, 0, WS_SORT_N - 1);

    *seed ^= (uint32_t)a[WS_SORT_N / 2];
    ws->sink += (double)a[WS_SORT_N - 1] * 1e-12;
}

/* 5. Scalar: dependent chains, bounded only by execution latency. */
static void sub_scalar(BenchWorkspace *ws, uint32_t *seed) {
    uint32_t s = *seed | 1u;
    double x = 1.5 + (double)(s & 0xFFFu) * 0.000244140625;

    for (int i = 0; i < WS_SCALAR_SQRT; i++)
        x = sqrt(x + 0.5) + 0.01;

    for (int i = 0; i < WS_SCALAR_XOR; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
    }

    *seed = s;
    ws->sink += x * 1e-12;
}

typedef void (*BenchSubtestFn)(BenchWorkspace *, uint32_t *);

static const BenchSubtestFn k_sub_fn[BENCH_SUBTESTS] = {
    sub_integer, sub_float, sub_memory, sub_sort, sub_scalar
};

/* ---------- result verification ------------------------------------------ */

static uint64_t g_check_ref[BENCH_SUBTESTS];   /* written before the workers start */

static inline uint64_t check_mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    return h ^ (h >> 29);
}

static inline uint64_t check_dbits(double d) {
    uint64_t u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

/* One normal kernel call from the canonical state with a fixed seed, plus a
   hash of everything it produced. Deterministic on every core. */
static uint64_t bench_check_call(int k, BenchWorkspace *ws) {
    uint32_t seed = BENCH_CHECK_SEED;
    uint64_t h = (uint64_t)k;
    ws->sink = 0.0;

    switch (k) {
    case 0:
        ws_fill_ibuf(ws);
        sub_integer(ws, &seed);
        break;
    case 1:
        ws_fill_matrix_a(ws);
        sub_float(ws, &seed);
        for (int i = 0; i < WS_MAT_N * WS_MAT_N; i++)
            h = check_mix(h, check_dbits(ws->C[i]));
        break;
    case 2:
        sub_memory(ws, &seed);
        break;
    case 3:
        sub_sort(ws, &seed);
        for (int i = 0; i < WS_SORT_N; i += 2)
            h = check_mix(h, ((uint64_t)(uint32_t)ws->sortbuf[i] << 32) |
                             (uint32_t)ws->sortbuf[i + 1]);
        break;
    default:
        sub_scalar(ws, &seed);
        break;
    }
    h = check_mix(h, seed);
    return check_mix(h, check_dbits(ws->sink));
}

/* Reference values, each computed twice. Returns the number of sub-tests whose
   two passes disagreed (the CPU is unstable even at light load). */
static int bench_check_prepare(void) {
    BenchWorkspace *ws = (BenchWorkspace *)malloc(sizeof(BenchWorkspace));
    if (!ws) return 0;
    ws_init(ws);
    int bad = 0;
    for (int k = 0; k < BENCH_SUBTESTS; k++) {
        uint64_t a = bench_check_call(k, ws);
        uint64_t b = bench_check_call(k, ws);
        if (a != b) bad++;
        g_check_ref[k] = a;
    }
    free(ws);
    return bad;
}

/* ---------- helpers ------------------------------------------------------ */

static void bench_result_clear(BenchTestResult *res) {
    if (!res) return;
    memset(res, 0, sizeof(*res));
}

static void bench_ref_set_default(BenchReference *ref) {
    if (!ref) return;
    memset(ref, 0, sizeof(*ref));
    snprintf(ref->cpu_name, sizeof(ref->cpu_name), "%s", BENCH_DEFAULT_REF_CPU);
    ref->single_throughput = BENCH_DEFAULT_REF_SINGLE;
    ref->multi_throughput  = BENCH_DEFAULT_REF_MULTI;
}

static double qpc_seconds_since(LARGE_INTEGER start, LARGE_INTEGER now,
                                LARGE_INTEGER freq) {
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

static int bench_is_cancelled(BenchState *state) {
    return WaitForSingleObject(state->stop_event, 0) == WAIT_OBJECT_0;
}

static void bench_get_active_ref(BenchReference *out_ref) {
    if (!out_ref) return;
    if (g_ref_count <= 0 || g_ref_selected < 0) {
        memset(out_ref, 0, sizeof(*out_ref));
        return;
    }
    *out_ref = g_refs[g_ref_selected];
}

const char *bench_subtest_name(int index) {
    if (index < 0 || index >= BENCH_SUBTESTS) return "";
    return k_sub_name[index];
}

int bench_logical_cpu_count(void) {
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (n == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        n = si.dwNumberOfProcessors;
    }
    return (n < 1) ? 1 : (int)n;
}

/* ---------- scoring ------------------------------------------------------ */

/* Geometric mean of the completed sub-test rates, normalised to the
   calibration CPU and scaled to points. */
static double bench_composite(const double *rate, int n_done) {
    double acc = 0.0;
    int    n   = 0;
    for (int i = 0; i < n_done && i < BENCH_SUBTESTS; i++) {
        if (rate[i] > 0.0 && k_cal_rate[i] > 0.0) {
            acc += log(rate[i] / k_cal_rate[i]);
            n++;
        }
    }
    if (n == 0) return 0.0;
    return BENCH_POINTS_SCALE * exp(acc / (double)n);
}

static void bench_fill_result(BenchTestResult *dst, const double *rate, int n_done,
                              unsigned long long iterations, double ref_throughput) {
    memset(dst, 0, sizeof(*dst));
    for (int i = 0; i < n_done && i < BENCH_SUBTESTS; i++) {
        dst->sub_rate[i] = rate[i];
        dst->sub_pts[i]  = (k_cal_rate[i] > 0.0)
                           ? BENCH_POINTS_SCALE * rate[i] / k_cal_rate[i] : 0.0;
    }
    dst->sub_done   = n_done;
    dst->iterations = iterations;
    dst->throughput = bench_composite(rate, n_done);
    dst->relative   = (ref_throughput > 0.0) ? (dst->throughput / ref_throughput) : 0.0;
    dst->score      = BENCH_BASELINE_SCORE * dst->relative;
}

/* ---------- CPU topology helpers ----------------------------------------- */

/* Mirrors SYSTEM_CPU_SET_INFORMATION (32 bytes); the API is loaded
   dynamically so the binary still starts on Windows versions without it. */
typedef struct {
    DWORD   Size;
    DWORD   Type;                 /* 0 = CpuSetInformation */
    DWORD   Id;
    WORD    Group;
    BYTE    LogicalProcessorIndex;
    BYTE    CoreIndex;
    BYTE    LastLevelCacheIndex;
    BYTE    NumaNodeIndex;
    BYTE    EfficiencyClass;      /* higher = faster core class (P-core) */
    BYTE    AllFlags;
    DWORD   Reserved;
    DWORD64 AllocationTag;
} BenchCpuSetInfo;

typedef BOOL (WINAPI *pfn_GetSystemCpuSetInformation)(void *, ULONG, PULONG, HANDLE, ULONG);
typedef BOOL (WINAPI *pfn_SetThreadSelectedCpuSets)(HANDLE, const ULONG *, ULONG);

/* On hybrid CPUs returns the CPU-set ids of the fastest core class, so the
   single-thread test is not scheduled onto an E-core. Returns 0 when the
   system is homogeneous or the API is unavailable. */
static int bench_perf_cpusets(ULONG *ids, int max_ids) {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return 0;
    pfn_GetSystemCpuSetInformation get = (pfn_GetSystemCpuSetInformation)(void *)
        GetProcAddress(k32, "GetSystemCpuSetInformation");
    if (!get) return 0;

    ULONG len = 0;
    get(NULL, 0, &len, GetCurrentProcess(), 0);
    if (len == 0) return 0;
    BYTE *buf = (BYTE *)malloc(len);
    if (!buf) return 0;
    if (!get(buf, len, &len, GetCurrentProcess(), 0)) { free(buf); return 0; }

    int minc = 255, maxc = -1;
    for (ULONG off = 0; off + sizeof(BenchCpuSetInfo) <= len;) {
        const BenchCpuSetInfo *e = (const BenchCpuSetInfo *)(buf + off);
        if (e->Size < sizeof(BenchCpuSetInfo)) break;
        if (e->Type == 0) {
            if ((int)e->EfficiencyClass < minc) minc = e->EfficiencyClass;
            if ((int)e->EfficiencyClass > maxc) maxc = e->EfficiencyClass;
        }
        off += e->Size;
    }

    int n = 0;
    if (maxc > minc) {
        for (ULONG off = 0; off + sizeof(BenchCpuSetInfo) <= len && n < max_ids;) {
            const BenchCpuSetInfo *e = (const BenchCpuSetInfo *)(buf + off);
            if (e->Size < sizeof(BenchCpuSetInfo)) break;
            if (e->Type == 0 && (int)e->EfficiencyClass == maxc)
                ids[n++] = e->Id;
            off += e->Size;
        }
    }
    free(buf);
    return n;
}

static void bench_thread_select_cpusets(HANDLE thread, const ULONG *ids, int count) {
    if (!thread || !ids || count <= 0) return;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    pfn_SetThreadSelectedCpuSets set = (pfn_SetThreadSelectedCpuSets)(void *)
        GetProcAddress(k32, "SetThreadSelectedCpuSets");
    if (set) set(thread, ids, (ULONG)count);
}

/* Spreads worker i over the processor groups (only matters above 64 LPs). */
static void bench_thread_assign_group(HANDLE thread, int index) {
    WORD ngroups = GetActiveProcessorGroupCount();
    if (ngroups <= 1) return;
    int base = 0;
    for (WORD g = 0; g < ngroups; g++) {
        int cnt = (int)GetActiveProcessorCount(g);
        if (index < base + cnt) {
            GROUP_AFFINITY ga;
            memset(&ga, 0, sizeof(ga));
            ga.Group = g;
            ga.Mask  = (cnt >= 64) ? ~(KAFFINITY)0 : (((KAFFINITY)1 << cnt) - 1);
            SetThreadGroupAffinity(thread, &ga, NULL);
            return;
        }
        base += cnt;
    }
}

/* ---------- worker pool --------------------------------------------------- */

typedef struct {
    _Alignas(64) volatile unsigned long long calls; /* own cache line per worker */
    unsigned        errors;          /* failed check calls in this slice */
    HANDLE          stop_event;
    LARGE_INTEGER   end_counter;
    BenchWorkspace *ws;
    uint32_t        seed;
    int             subtest;
} BenchThreadCtx;

typedef struct {
    BenchState      *state;
    int              nthreads;
    BenchWorkspace **ws;
    BenchThreadCtx  *ctx;
    HANDLE          *threads;
    ULONG            perf_ids[256];
    int              perf_count;     /* > 0: pin the single worker to P-cores */
} BenchPool;

static unsigned __stdcall bench_worker(void *arg) {
    BenchThreadCtx *ctx  = (BenchThreadCtx *)arg;
    BenchSubtestFn  fn   = k_sub_fn[ctx->subtest];
    BenchWorkspace *ws   = ctx->ws;
    uint32_t        seed = ctx->seed;
    const int       k    = ctx->subtest;
    const uint64_t  ref  = g_check_ref[k];
    unsigned long long calls = 0;
    unsigned errors = 0;
    LARGE_INTEGER now;

    for (;;) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= ctx->end_counter.QuadPart) break;
        if ((calls & 15u) == 0 &&
            WaitForSingleObject(ctx->stop_event, 0) == WAIT_OBJECT_0)
            break;
        if ((calls % BENCH_CHECK_EVERY) == BENCH_CHECK_EVERY - 1u) {
            if (bench_check_call(k, ws) != ref) errors++;
        } else {
            fn(ws, &seed);
        }
        calls++;
        if ((calls % BENCH_PUBLISH_EVERY) == 0)
            ctx->calls = calls;
    }
    ctx->calls  = calls;
    ctx->seed   = seed;
    ctx->errors = errors;
    return 0;
}

static void pool_destroy(BenchPool *p) {
    if (!p) return;
    if (p->ws) {
        for (int i = 0; i < p->nthreads; i++) free(p->ws[i]);
        free(p->ws);
    }
    if (p->ctx) _aligned_free(p->ctx);
    free(p->threads);
    memset(p, 0, sizeof(*p));
}

static int pool_create(BenchPool *p, BenchState *state, int nthreads) {
    memset(p, 0, sizeof(*p));
    if (nthreads < 1) nthreads = 1;
    p->state    = state;
    p->nthreads = nthreads;
    p->ws       = (BenchWorkspace **)calloc((size_t)nthreads, sizeof(BenchWorkspace *));
    p->ctx      = (BenchThreadCtx *)_aligned_malloc((size_t)nthreads * sizeof(BenchThreadCtx), 64);
    p->threads  = (HANDLE *)calloc((size_t)nthreads, sizeof(HANDLE));
    if (!p->ws || !p->ctx || !p->threads) { pool_destroy(p); return 0; }
    memset(p->ctx, 0, (size_t)nthreads * sizeof(BenchThreadCtx));

    for (int i = 0; i < nthreads; i++) {
        p->ws[i] = (BenchWorkspace *)malloc(sizeof(BenchWorkspace));
        if (!p->ws[i]) { pool_destroy(p); return 0; }
        ws_init(p->ws[i]);
        p->ctx[i].seed = (0x9E3779B9u * (uint32_t)(i + 1)) | 1u;
    }
    if (nthreads == 1)
        p->perf_count = bench_perf_cpusets(p->perf_ids,
                                           (int)(sizeof(p->perf_ids) / sizeof(p->perf_ids[0])));
    return 1;
}

typedef void (*BenchTickFn)(void *user, unsigned long long calls, double elapsed);

/* Runs one sub-test on every pool worker for slice_sec. Returns the total
   number of kernel calls; *elapsed_out is the wall time from start to join. */
static unsigned long long pool_run_slice(BenchPool *p, int subtest, double slice_sec,
                                         double *elapsed_out, BenchTickFn on_tick,
                                         void *user) {
    LARGE_INTEGER freq, start, end, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    end.QuadPart = (slice_sec > 0.0)
                   ? start.QuadPart + (LONGLONG)(slice_sec * (double)freq.QuadPart)
                   : LLONG_MAX;

    for (int i = 0; i < p->nthreads; i++) {
        BenchThreadCtx *c = &p->ctx[i];
        c->calls       = 0;
        c->errors      = 0;
        c->stop_event  = p->state->stop_event;
        c->end_counter = end;
        c->ws          = p->ws[i];
        c->subtest     = subtest;
        unsigned tid = 0;
        p->threads[i] = (HANDLE)_beginthreadex(NULL, 0, bench_worker, c,
                                               CREATE_SUSPENDED, &tid);
        if (!p->threads[i]) continue;
        if (p->nthreads == 1 && p->perf_count > 0)
            bench_thread_select_cpusets(p->threads[i], p->perf_ids, p->perf_count);
        else if (p->nthreads > 1)
            bench_thread_assign_group(p->threads[i], i);
        ResumeThread(p->threads[i]);
    }

    for (;;) {
        int done = 1;
        unsigned long long live = 0;
        for (int i = 0; i < p->nthreads; i++) {
            if (!p->threads[i]) continue;
            if (WaitForSingleObject(p->threads[i], 0) == WAIT_TIMEOUT) done = 0;
            live += p->ctx[i].calls;
        }
        QueryPerformanceCounter(&now);
        if (on_tick) on_tick(user, live, qpc_seconds_since(start, now, freq));
        if (done) break;
        Sleep(50);
    }

    unsigned long long total = 0;
    LONG errors = 0;
    for (int i = 0; i < p->nthreads; i++) {
        if (!p->threads[i]) continue;
        WaitForSingleObject(p->threads[i], INFINITE);
        total  += p->ctx[i].calls;
        errors += (LONG)p->ctx[i].errors;
        CloseHandle(p->threads[i]);
        p->threads[i] = NULL;
    }
    if (errors) InterlockedExchangeAdd(&p->state->errors, errors);
    QueryPerformanceCounter(&now);
    *elapsed_out = qpc_seconds_since(start, now, freq);
    return total;
}

/* ---------- phases ------------------------------------------------------- */

typedef struct {
    BenchState      *state;
    enum BenchPhase  phase;
    BenchTestResult *dst;
    double           rate[BENCH_SUBTESTS];
    int              k;              /* sub-test currently running */
    double           slice_sec;
    double           elapsed_base;   /* seconds before this sub-test started */
    double           ref_tp;
    unsigned long long iterations;
} BenchLive;

static void bench_live_tick(void *user, unsigned long long calls, double elapsed) {
    BenchLive *lv = (BenchLive *)user;
    double rate[BENCH_SUBTESTS];
    memcpy(rate, lv->rate, sizeof(rate));
    int n_done = lv->k;
    if (elapsed > 0.25 && calls > 0) {       /* partial value of the running test */
        rate[lv->k] = (double)calls * k_sub_units[lv->k] / elapsed;
        n_done = lv->k + 1;
    }
    EnterCriticalSection(&lv->state->cs);
    lv->state->elapsed_sec = lv->elapsed_base + elapsed;
    if (lv->dst)
        bench_fill_result(lv->dst, rate, n_done, lv->iterations + calls, lv->ref_tp);
    LeaveCriticalSection(&lv->state->cs);
}

/* Warm-up: Float kernel on nthreads workers, nothing published. */
static int bench_run_warmup(BenchState *state, double duration_sec, int nthreads) {
    BenchPool pool;
    if (!pool_create(&pool, state, nthreads)) return 0;
    double elapsed = 0.0;
    pool_run_slice(&pool, 1, duration_sec, &elapsed, NULL, NULL);
    pool_destroy(&pool);
    return !bench_is_cancelled(state);
}

/* Timed phase: all sub-tests, equal slices. Returns 1 when completed. */
static int bench_run_phase(BenchState *state, enum BenchPhase phase, int nthreads,
                           double total_sec, double ref_tp, BenchTestResult *dst) {
    BenchPool pool;
    BenchLive lv;
    if (!pool_create(&pool, state, nthreads)) return 0;

    memset(&lv, 0, sizeof(lv));
    lv.state     = state;
    lv.phase     = phase;
    lv.dst       = dst;
    lv.slice_sec = total_sec / (double)BENCH_SUBTESTS;
    lv.ref_tp    = ref_tp;

    for (int k = 0; k < BENCH_SUBTESTS; k++) {
        lv.k            = k;
        lv.elapsed_base = (double)k * lv.slice_sec;

        EnterCriticalSection(&state->cs);
        state->subtest = k;
        LeaveCriticalSection(&state->cs);

        double elapsed = 0.0;
        unsigned long long calls =
            pool_run_slice(&pool, k, lv.slice_sec, &elapsed, bench_live_tick, &lv);
        if (bench_is_cancelled(state)) { pool_destroy(&pool); return 0; }

        lv.rate[k]     = (elapsed > 0.0) ? (double)calls * k_sub_units[k] / elapsed : 0.0;
        lv.iterations += calls;
    }

    EnterCriticalSection(&state->cs);
    bench_fill_result(dst, lv.rate, BENCH_SUBTESTS, lv.iterations, ref_tp);
    state->subtest = -1;
    LeaveCriticalSection(&state->cs);

    pool_destroy(&pool);
    return 1;
}

/* ---------- main benchmark thread ---------------------------------------- */

static void bench_set_phase(BenchState *state, enum BenchPhase phase) {
    EnterCriticalSection(&state->cs);
    state->phase       = phase;
    state->subtest     = -1;
    state->elapsed_sec = 0.0;
    LeaveCriticalSection(&state->cs);
}

static DWORD WINAPI bench_main_thread(LPVOID param) {
    BenchState *state = (BenchState *)param;
    BenchReference ref;
    int ncpu = bench_logical_cpu_count();

    bench_get_active_ref(&ref);

    EnterCriticalSection(&state->cs);
    state->phase       = BENCH_PHASE_WARMUP;
    state->subtest     = -1;
    state->elapsed_sec = 0.0;
    bench_result_clear(&state->multi);
    bench_result_clear(&state->single);
    LeaveCriticalSection(&state->cs);

    /* references for the check calls; a mismatch between two passes on one
       idle thread already means the CPU computes wrong values */
    {
        int bad = bench_check_prepare();
        if (bad) InterlockedExchangeAdd(&state->errors, (LONG)bad);
    }

    /* Timed: warm-up(1T) -> single(1T) -> warm-up(NT) -> multi(NT) */
    bench_run_warmup(state, BENCH_WARMUP_SEC, 1);
    if (bench_is_cancelled(state)) goto cancelled;

    bench_set_phase(state, BENCH_PHASE_SINGLE);
    if (!bench_run_phase(state, BENCH_PHASE_SINGLE, 1, BENCH_SINGLE_SEC,
                         ref.single_throughput, &state->single))
        goto cancelled;

    bench_set_phase(state, BENCH_PHASE_WARMUP);
    bench_run_warmup(state, BENCH_WARMUP_SEC, ncpu);
    if (bench_is_cancelled(state)) goto cancelled;

    bench_set_phase(state, BENCH_PHASE_MULTI);
    if (!bench_run_phase(state, BENCH_PHASE_MULTI, ncpu, BENCH_MULTI_SEC,
                         ref.multi_throughput, &state->multi))
        goto cancelled;

    EnterCriticalSection(&state->cs);
    state->phase       = BENCH_PHASE_DONE;
    state->subtest     = -1;
    state->running     = 0;
    state->cancelled   = 0;
    state->elapsed_sec = 0.0;
    LeaveCriticalSection(&state->cs);
    return 0;

cancelled:
    EnterCriticalSection(&state->cs);
    state->phase       = BENCH_PHASE_IDLE;
    state->subtest     = -1;
    state->cancelled   = 1;
    state->running     = 0;
    state->elapsed_sec = 0.0;
    bench_result_clear(&state->multi);
    bench_result_clear(&state->single);
    LeaveCriticalSection(&state->cs);
    return 0;
}

/* ---------- public API --------------------------------------------------- */

void bench_init(BenchState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    InitializeCriticalSection(&state->cs);
    state->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    state->phase      = BENCH_PHASE_IDLE;
    state->subtest    = -1;
}

void bench_shutdown(BenchState *state) {
    if (!state) return;
    bench_stop(state);
    if (state->thread) {
        WaitForSingleObject(state->thread, INFINITE);
        CloseHandle(state->thread);
        state->thread = NULL;
    }
    if (state->stop_event) {
        CloseHandle(state->stop_event);
        state->stop_event = NULL;
    }
    DeleteCriticalSection(&state->cs);
}

void bench_poll(BenchState *state) {
    if (!state || !state->thread) return;
    if (WaitForSingleObject(state->thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(state->thread);
        state->thread = NULL;
    }
}

void bench_start_timed(BenchState *state) {
    if (!state || !state->stop_event) return;
    bench_poll(state);

    EnterCriticalSection(&state->cs);
    if (state->running) { LeaveCriticalSection(&state->cs); return; }

    ResetEvent(state->stop_event);
    state->running     = 1;
    state->cancelled   = 0;
    state->phase       = BENCH_PHASE_WARMUP;
    state->errors      = 0;
    state->subtest     = -1;
    state->elapsed_sec = 0.0;
    bench_result_clear(&state->multi);
    bench_result_clear(&state->single);
    LeaveCriticalSection(&state->cs);

    DWORD tid = 0;
    state->thread = CreateThread(NULL, 0, bench_main_thread, state, 0, &tid);
    if (!state->thread) {
        EnterCriticalSection(&state->cs);
        state->running = 0;
        state->phase   = BENCH_PHASE_IDLE;
        LeaveCriticalSection(&state->cs);
    }
}

void bench_stop(BenchState *state) {
    if (!state || !state->stop_event) return;
    SetEvent(state->stop_event);
}

void bench_read_ui(BenchState *state, BenchUiState *out_state) {
    if (!state || !out_state) return;
    EnterCriticalSection(&state->cs);
    out_state->running     = (int)state->running;
    out_state->cancelled   = (int)state->cancelled;
    out_state->phase       = (enum BenchPhase)state->phase;
    out_state->errors      = (int)state->errors;
    out_state->subtest     = (int)state->subtest;
    out_state->elapsed_sec = state->elapsed_sec;
    out_state->multi       = state->multi;
    out_state->single      = state->single;
    LeaveCriticalSection(&state->cs);
}

/* ---------- display helpers ---------------------------------------------- */

static double bench_step_for_value(double value) {
    if (value <= 2.0)  return 0.1;
    if (value <= 5.0)  return 0.2;
    if (value <= 10.0) return 0.5;
    return 1.0;
}

double bench_calc_display_max(double relative) {
    double target = relative * 1.2;
    if (target < 1.2) target = 1.2;
    double step = bench_step_for_value(target);
    return ceil(target / step) * step;
}

double bench_calc_fill_ratio(double relative, double display_max) {
    if (display_max <= 0.0) return 0.0;
    double v = relative / display_max;
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

double bench_calc_baseline_ratio(double display_max) {
    if (display_max <= 0.0) return 0.0;
    return 1.0 / display_max;
}

/* ---------- JSON ref parsing --------------------------------------------- */

static int bench_json_extract_string(const char *src, const char *key, char *dst,
                                     size_t dst_size) {
    if (!src || !key || !dst || dst_size == 0) return 0;
    const char *p = strstr(src, key);
    if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    p = strchr(p, '\"'); if (!p) return 0;
    p++;
    const char *end = strchr(p, '\"'); if (!end) return 0;
    size_t n = (size_t)(end - p);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
    return 1;
}

static int bench_json_extract_double(const char *src, const char *key, double *out_v) {
    if (!src || !key || !out_v) return 0;
    const char *p = strstr(src, key);
    if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    char *endptr = NULL;
    double v = strtod(p, &endptr);
    if (endptr == p) return 0;
    *out_v = v;
    return 1;
}

/* ---------- ref management ----------------------------------------------- */

int bench_ref_is_compatible(const BenchReference *ref) {
    /* v1 kernel reported raw ops/s (1e9..1e11); v2 reports points (1e2..1e6) */
    if (!ref) return 0;
    if (ref->single_throughput <= 0.0 || ref->multi_throughput <= 0.0) return 0;
    if (ref->single_throughput >= 1.0e7 || ref->multi_throughput >= 1.0e8) return 0;
    return 1;
}

int bench_refs_load(void) {
    if (g_ref_loaded) return g_ref_count;
    g_ref_loaded   = 1;
    g_ref_count    = 1;
    g_ref_selected = -1;
    bench_ref_set_default(&g_refs[0]);
    return g_ref_count;
}

int bench_refs_parse(const char *buf, BenchReference *out, int max) {
    if (!buf || !out || max <= 0) return 0;
    int count       = 0;
    const char *cur = buf;
    while (count < max) {
        const char *p = strstr(cur, "\"cpu_name\"");
        if (!p) break;
        BenchReference ref;
        memset(&ref, 0, sizeof(ref));
        int ok = bench_json_extract_string(p, "\"cpu_name\"",
                                           ref.cpu_name, sizeof(ref.cpu_name))
              && bench_json_extract_double(p, "\"single_throughput\"",
                                           &ref.single_throughput)
              && bench_json_extract_double(p, "\"multi_throughput\"",
                                           &ref.multi_throughput);
        if (ok) out[count++] = ref;
        cur = p + 10;
    }
    return count;
}

void bench_refs_set(const BenchReference *refs, int count) {
    if (!refs || count <= 0) return;
    int n = 0;
    for (int i = 0; i < count && n < BENCH_MAX_REFS; i++) {
        if (!bench_ref_is_compatible(&refs[i])) continue; /* v1 results: skip */
        g_refs[n++] = refs[i];
    }
    if (n == 0) return;   /* nothing usable: keep the built-in reference */
    g_ref_count    = n;
    g_ref_selected = -1;
    g_ref_loaded   = 1;
}

int bench_refs_count(void) { bench_refs_load(); return g_ref_count; }

const BenchReference *bench_ref_get(int index) {
    bench_refs_load();
    if (index < 0 || index >= g_ref_count) return NULL;
    return &g_refs[index];
}

int bench_refs_selected(void) {
    bench_refs_load();
    return g_ref_selected;
}

void bench_refs_select(int index) {
    bench_refs_load();
    if (index == -1) { g_ref_selected = -1; return; }
    if (index < 0 || index >= g_ref_count) return;
    g_ref_selected = index;
}

void bench_refs_reload(void) { g_ref_loaded = 0; }

void bench_get_default_ref(BenchReference *out) {
    if (out) bench_ref_set_default(out);
}
