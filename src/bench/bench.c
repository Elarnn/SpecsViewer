#include "bench/bench.h"

#include <math.h>
#include <limits.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Per-thread working set: 28 KB total -- fits in L1D on any modern CPU.
 * Fixed size: workload is independent of system RAM.
 *
 * Kernel mixes five CPU subsystems per iteration:
 *   1. Integer ALU  -- xorshift32 serial chain (512 ops)
 *   2. FPU          -- sqrt serial chain (64 dependent double sqrts)
 *   3. Cache        -- sequential double read, 8 KB (L1/L2 bandwidth)
 *   4. SIMD         -- 4-wide float FMA loop (auto-vectorizes to SSE/AVX)
 *   5. Branch pred  -- Collatz-like data-dependent branches (128 steps)
 *
 * throughput = iterations * BENCH_KERNEL_OPS / elapsed_sec  (ops/sec)
 * score      = BASELINE_SCORE * (throughput / ref_throughput)
 */

#define KERN_I_SZ 1024       /* 4 KB  -- integer / branch array */
#define KERN_F_SZ 1024       /* 8 KB  -- FPU / cache array (doubles) */
#define KERN_S_SZ 4096       /* 16 KB -- SIMD array (floats) */
#define BENCH_KERNEL_OPS 10000.0

#define BENCH_DEFAULT_REF_CPU    "i5-11400H"
#define BENCH_DEFAULT_REF_SINGLE 4000000000.0
#define BENCH_DEFAULT_REF_MULTI  35000000000.0

static BenchReference  g_refs[BENCH_MAX_REFS];
static int             g_ref_count    = 0;
static int             g_ref_selected = -1;
static int             g_ref_loaded   = 0;

typedef struct {
    int             iarr[KERN_I_SZ];
    double          farr[KERN_F_SZ];
    float           sarr[KERN_S_SZ];
    volatile double sink;
} BenchWorkspace;

typedef struct {
    HANDLE                     stop_event;
    LARGE_INTEGER              end_counter;
    LARGE_INTEGER              freq;
    volatile unsigned long long iterations;
} BenchThreadCtx;

/* ---------- helpers ---------------------------------------------------- */

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

/* ---------- JSON ref parsing ------------------------------------------- */

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

static void bench_get_active_ref(BenchReference *out_ref) {
    if (!out_ref) return;
    if (g_ref_count <= 0 || g_ref_selected < 0) {
        memset(out_ref, 0, sizeof(*out_ref));
        return;
    }
    *out_ref = g_refs[g_ref_selected];
}

/* ---------- mixed kernel ----------------------------------------------- */

static void ws_init(BenchWorkspace *ws, unsigned seed) {
    unsigned s = seed;
    ws->sink = 0.0;
    for (int i = 0; i < KERN_I_SZ; i++) {
        s = s * 1664525u + 1013904223u;
        ws->iarr[i] = (int)s;
    }
    for (int i = 0; i < KERN_F_SZ; i++)
        ws->farr[i] = 1.0 + (double)(i & 0xFF) * 0.00390625;
    for (int i = 0; i < KERN_S_SZ; i++)
        ws->sarr[i] = 1.0f + (float)(i & 0xFF) * 0.00390625f;
}

static void bench_kernel(BenchWorkspace *ws, unsigned *pseed) {
    unsigned s = *pseed;

    /* 1. Integer ALU: xorshift32 serial chain */
    for (int i = 0; i < 512; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
    }
    ws->iarr[s & (KERN_I_SZ - 1u)] ^= (int)s;

    /* 2. FPU: sqrt serial chain, stresses FP divider latency */
    double x = 1.5 + (double)(s & 0xFFFu) * 0.000244140625;
    for (int i = 0; i < 64; i++)
        x = sqrt(x + 0.5) + 0.01;
    ws->farr[s & (KERN_F_SZ - 1u)] = x;

    /* 3. Cache: sequential double read, L1/L2 bandwidth */
    double csum = 0.0;
    for (int i = 0; i < KERN_F_SZ; i++)
        csum += ws->farr[i];
    ws->farr[(s + 1u) & (KERN_F_SZ - 1u)] += csum * 1e-30;

    /* 4. SIMD: 4 independent float FMA chains, auto-vectorizes to SSE/AVX */
    float a = (float)(s & 0xFFFFu) * 1.52587890625e-5f;
    for (int i = 0; i < KERN_S_SZ; i += 4) {
        ws->sarr[i]   = ws->sarr[i]   * 1.0000001f + a;
        ws->sarr[i+1] = ws->sarr[i+1] * 1.0000001f + a;
        ws->sarr[i+2] = ws->sarr[i+2] * 1.0000001f + a;
        ws->sarr[i+3] = ws->sarr[i+3] * 1.0000001f + a;
    }

    /* 5. Branch predictor: Collatz-like data-dependent sequence */
    unsigned v = (unsigned)ws->iarr[(s >> 4) & (KERN_I_SZ - 1u)] | 1u;
    for (int i = 0; i < 128; i++) {
        if (v & 1u) v = v * 3u + 1u;
        else        v >>= 1;
        if (v == 0u) v = 1u;
    }
    ws->iarr[(s >> 8) & (KERN_I_SZ - 1u)] = (int)v;

    ws->sink += csum * 1e-300 + (double)v * 1e-300 + (double)ws->sarr[0] * 1e-300;
    *pseed = s;
}

/* ---------- worker thread ---------------------------------------------- */

static unsigned __stdcall bench_worker_loop(void *arg) {
    BenchThreadCtx *ctx = (BenchThreadCtx *)arg;
    BenchWorkspace *ws  = (BenchWorkspace *)malloc(sizeof(BenchWorkspace));
    if (!ws) return 0;

    unsigned seed = ((unsigned)(uintptr_t)arg ^ 0xC0FFEEu) | 1u;
    ws_init(ws, seed ^ 0xDEADBEEFu);

    unsigned long long iter = 0;
    LARGE_INTEGER now;

    for (;;) {
        if (WaitForSingleObject(ctx->stop_event, 0) == WAIT_OBJECT_0) break;
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= ctx->end_counter.QuadPart) break;
        bench_kernel(ws, &seed);
        iter++;
        ctx->iterations = iter;
    }
    ctx->iterations = iter;
    free(ws);
    return 0;
}

/* ---------- result publishing ------------------------------------------ */

static void bench_write_result(BenchTestResult *dst, unsigned long long iterations,
                               double elapsed_sec, double ref_throughput) {
    double throughput = (elapsed_sec > 0.0)
                        ? ((double)iterations * BENCH_KERNEL_OPS / elapsed_sec)
                        : 0.0;
    double score = (ref_throughput > 0.0)
                   ? (BENCH_BASELINE_SCORE * (throughput / ref_throughput))
                   : 0.0;
    dst->iterations = iterations;
    dst->throughput = throughput;
    dst->score      = score;
    dst->relative   = (ref_throughput > 0.0) ? (throughput / ref_throughput) : 0.0;
}

static void bench_publish_live(BenchState *state, enum BenchPhase phase,
                               unsigned long long iterations,
                               double elapsed_sec, double ref_throughput) {
    if (!state) return;
    EnterCriticalSection(&state->cs);
    state->elapsed_sec = elapsed_sec;
    if (phase == BENCH_PHASE_MULTI)
        bench_write_result(&state->multi, iterations, elapsed_sec, ref_throughput);
    else if (phase == BENCH_PHASE_SINGLE)
        bench_write_result(&state->single, iterations, elapsed_sec, ref_throughput);
    LeaveCriticalSection(&state->cs);
}

/* ---------- run helpers ------------------------------------------------ */

/* Runs nthreads workers for duration_sec without publishing results. */
static int bench_run_warmup(BenchState *state, double duration_sec, int nthreads) {
    LARGE_INTEGER freq, start, end;
    if (nthreads < 1) nthreads = 1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    end.QuadPart = start.QuadPart + (LONGLONG)(duration_sec * (double)freq.QuadPart);

    HANDLE        *threads = (HANDLE *)       calloc((size_t)nthreads, sizeof(HANDLE));
    BenchThreadCtx *ctxs   = (BenchThreadCtx *)calloc((size_t)nthreads, sizeof(BenchThreadCtx));
    unsigned       *tids   = (unsigned *)      calloc((size_t)nthreads, sizeof(unsigned));
    if (!threads || !ctxs || !tids) { free(threads); free(ctxs); free(tids); return 0; }

    for (int i = 0; i < nthreads; i++) {
        ctxs[i].stop_event  = state->stop_event;
        ctxs[i].end_counter = end;
        ctxs[i].freq        = freq;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, bench_worker_loop, &ctxs[i], 0, &tids[i]);
    }
    for (int i = 0; i < nthreads; i++) {
        if (threads[i]) { WaitForSingleObject(threads[i], INFINITE); CloseHandle(threads[i]); }
    }
    free(threads); free(ctxs); free(tids);
    return !bench_is_cancelled(state);
}

static unsigned long long bench_run_single(BenchState *state, double duration_sec,
                                           double ref_single, double *elapsed_out) {
    LARGE_INTEGER freq, start, end, now;
    HANDLE thread = NULL;
    unsigned tid  = 0;
    BenchThreadCtx ctx;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    end.QuadPart = start.QuadPart + (LONGLONG)(duration_sec * (double)freq.QuadPart);

    memset(&ctx, 0, sizeof(ctx));
    ctx.stop_event  = state->stop_event;
    ctx.end_counter = end;
    ctx.freq        = freq;

    thread = (HANDLE)_beginthreadex(NULL, 0, bench_worker_loop, &ctx, 0, &tid);
    if (!thread) return 0;

    for (;;) {
        DWORD w = WaitForSingleObject(thread, 50);
        QueryPerformanceCounter(&now);
        bench_publish_live(state, BENCH_PHASE_SINGLE, ctx.iterations,
                           qpc_seconds_since(start, now, freq), ref_single);
        if (w == WAIT_OBJECT_0) break;
    }
    CloseHandle(thread);

    QueryPerformanceCounter(&now);
    *elapsed_out = qpc_seconds_since(start, now, freq);
    return ctx.iterations;
}

static unsigned long long bench_run_multi(BenchState *state, double duration_sec,
                                          double ref_multi, double *elapsed_out) {
    SYSTEM_INFO sys;
    LARGE_INTEGER freq, start, end, now;
    GetSystemInfo(&sys);
    int nthreads = (int)sys.dwNumberOfProcessors;
    if (nthreads < 1) nthreads = 1;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    end.QuadPart = (duration_sec > 0.0)
                   ? start.QuadPart + (LONGLONG)(duration_sec * (double)freq.QuadPart)
                   : LLONG_MAX;

    HANDLE        *threads = (HANDLE *)       calloc((size_t)nthreads, sizeof(HANDLE));
    BenchThreadCtx *ctxs   = (BenchThreadCtx *)calloc((size_t)nthreads, sizeof(BenchThreadCtx));
    unsigned       *tids   = (unsigned *)      calloc((size_t)nthreads, sizeof(unsigned));
    if (!threads || !ctxs || !tids) { free(threads); free(ctxs); free(tids); return 0; }

    for (int i = 0; i < nthreads; i++) {
        ctxs[i].stop_event  = state->stop_event;
        ctxs[i].end_counter = end;
        ctxs[i].freq        = freq;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, bench_worker_loop, &ctxs[i], 0, &tids[i]);
    }

    for (;;) {
        int done = 1;
        unsigned long long live_total = 0;
        for (int i = 0; i < nthreads; i++) {
            if (!threads[i]) continue;
            if (WaitForSingleObject(threads[i], 0) == WAIT_TIMEOUT) done = 0;
            live_total += ctxs[i].iterations;
        }
        QueryPerformanceCounter(&now);
        bench_publish_live(state, BENCH_PHASE_MULTI, live_total,
                           qpc_seconds_since(start, now, freq), ref_multi);
        if (done) break;
        Sleep(50);
    }

    unsigned long long total_iter = 0;
    for (int i = 0; i < nthreads; i++) {
        if (!threads[i]) continue;
        WaitForSingleObject(threads[i], INFINITE);
        total_iter += ctxs[i].iterations;
        CloseHandle(threads[i]);
    }

    QueryPerformanceCounter(&now);
    *elapsed_out = qpc_seconds_since(start, now, freq);
    free(threads); free(ctxs); free(tids);
    return total_iter;
}

/* ---------- main benchmark thread -------------------------------------- */

static DWORD WINAPI bench_main_thread(LPVOID param) {
    BenchState *state = (BenchState *)param;
    LONG mode = state->mode;
    BenchReference ref;
    SYSTEM_INFO sys;
    int ncpu;
    double elapsed;
    unsigned long long single_iters, multi_iters;

    bench_get_active_ref(&ref);
    GetSystemInfo(&sys);
    ncpu = (int)sys.dwNumberOfProcessors;
    if (ncpu < 1) ncpu = 1;
    elapsed      = 0.0;
    single_iters = 0;
    multi_iters  = 0;

    EnterCriticalSection(&state->cs);
    state->phase       = BENCH_PHASE_WARMUP;
    state->elapsed_sec = 0.0;
    bench_result_clear(&state->multi);
    bench_result_clear(&state->single);
    LeaveCriticalSection(&state->cs);

    if (mode == BENCH_MODE_STRESS) {
        /* Stress: warm all cores, then run multi indefinitely until Stop.
           Results are always discarded on stop -- stress is for thermal load only. */
        bench_run_warmup(state, BENCH_WARMUP_SEC, ncpu);
        if (bench_is_cancelled(state)) goto cancelled;

        EnterCriticalSection(&state->cs);
        state->phase       = BENCH_PHASE_MULTI;
        state->elapsed_sec = 0.0;
        LeaveCriticalSection(&state->cs);

        multi_iters = bench_run_multi(state, 0.0, ref.multi_throughput, &elapsed);
        (void)multi_iters;
        goto cancelled;
    }

    /* Timed mode: warmup(1T,2s) -> single(1T,7s) -> warmup(NT,1s) -> multi(NT,10s) */

    bench_run_warmup(state, BENCH_WARMUP_SEC, 1);
    if (bench_is_cancelled(state)) goto cancelled;

    EnterCriticalSection(&state->cs);
    state->phase       = BENCH_PHASE_SINGLE;
    state->elapsed_sec = 0.0;
    LeaveCriticalSection(&state->cs);

    single_iters = bench_run_single(state, BENCH_SINGLE_SEC, ref.single_throughput, &elapsed);
    if (bench_is_cancelled(state)) goto cancelled;

    EnterCriticalSection(&state->cs);
    bench_write_result(&state->single, single_iters, elapsed, ref.single_throughput);
    state->phase       = BENCH_PHASE_WARMUP;
    state->elapsed_sec = 0.0;
    LeaveCriticalSection(&state->cs);

    bench_run_warmup(state, 1.0, ncpu);
    if (bench_is_cancelled(state)) goto cancelled;

    EnterCriticalSection(&state->cs);
    state->phase       = BENCH_PHASE_MULTI;
    state->elapsed_sec = 0.0;
    LeaveCriticalSection(&state->cs);

    multi_iters = bench_run_multi(state, BENCH_MULTI_SEC, ref.multi_throughput, &elapsed);
    if (bench_is_cancelled(state)) goto cancelled;

    EnterCriticalSection(&state->cs);
    bench_write_result(&state->multi, multi_iters, elapsed, ref.multi_throughput);
    state->phase       = BENCH_PHASE_DONE;
    state->running     = 0;
    state->cancelled   = 0;
    state->elapsed_sec = 0.0;
    LeaveCriticalSection(&state->cs);
    return 0;

cancelled:
    EnterCriticalSection(&state->cs);
    state->phase       = BENCH_PHASE_IDLE;
    state->cancelled   = 1;
    state->running     = 0;
    state->elapsed_sec = 0.0;
    bench_result_clear(&state->multi);
    bench_result_clear(&state->single);
    LeaveCriticalSection(&state->cs);
    return 0;
}

/* ---------- public API ------------------------------------------------- */

void bench_init(BenchState *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    InitializeCriticalSection(&state->cs);
    state->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    state->phase      = BENCH_PHASE_IDLE;
    state->mode       = BENCH_MODE_TIMED;
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

static void bench_start_mode(BenchState *state, enum BenchMode mode) {
    if (!state || !state->stop_event) return;
    bench_poll(state);

    EnterCriticalSection(&state->cs);
    if (state->running) { LeaveCriticalSection(&state->cs); return; }

    ResetEvent(state->stop_event);
    state->running     = 1;
    state->cancelled   = 0;
    state->phase       = BENCH_PHASE_WARMUP;
    state->mode        = mode;
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

void bench_start_timed(BenchState *state)  { bench_start_mode(state, BENCH_MODE_TIMED);  }
void bench_start_stress(BenchState *state) { bench_start_mode(state, BENCH_MODE_STRESS); }

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
    out_state->mode        = (enum BenchMode)state->mode;
    out_state->elapsed_sec = state->elapsed_sec;
    out_state->multi       = state->multi;
    out_state->single      = state->single;
    LeaveCriticalSection(&state->cs);
}

/* ---------- display helpers -------------------------------------------- */

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

/* ---------- ref management --------------------------------------------- */

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
    if (count > BENCH_MAX_REFS) count = BENCH_MAX_REFS;
    for (int i = 0; i < count; i++) g_refs[i] = refs[i];
    g_ref_count    = count;
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
