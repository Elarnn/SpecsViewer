#include "bench/bench.h"

#include <math.h>
#include <limits.h>
#include <process.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BENCH_MATRIX_N 32
#define BENCH_DEFAULT_REF_CPU "i5-11400H"
#define BENCH_DEFAULT_REF_SINGLE 3626200000.0
#define BENCH_DEFAULT_REF_MULTI 24754040000.0
#define BENCH_MAX_REFS 16

static volatile double g_bench_sink = 0.0;
static BenchReference g_refs[BENCH_MAX_REFS];
static int g_ref_count = 0;
static int g_ref_selected = 0;
static int g_ref_loaded = 0;

typedef struct BenchThreadCtx {
  HANDLE stop_event;
  LARGE_INTEGER end_counter;
  LARGE_INTEGER freq;
  volatile unsigned long long iterations;
} BenchThreadCtx;

static void bench_result_clear(BenchTestResult *res) {
  if (!res)
    return;
  memset(res, 0, sizeof(*res));
}

static void bench_ref_set_default(BenchReference *ref) {
  if (!ref)
    return;
  memset(ref, 0, sizeof(*ref));
  snprintf(ref->cpu_name, sizeof(ref->cpu_name), "%s", BENCH_DEFAULT_REF_CPU);
  ref->single_throughput = BENCH_DEFAULT_REF_SINGLE;
  ref->multi_throughput = BENCH_DEFAULT_REF_MULTI;
}

static int bench_json_extract_string(const char *src, const char *key, char *dst,
                                     size_t dst_size) {
  if (!src || !key || !dst || dst_size == 0)
    return 0;

  const char *p = strstr(src, key);
  if (!p)
    return 0;
  p = strchr(p, ':');
  if (!p)
    return 0;
  p = strchr(p, '\"');
  if (!p)
    return 0;
  p++;
  const char *end = strchr(p, '\"');
  if (!end)
    return 0;

  size_t n = (size_t)(end - p);
  if (n >= dst_size)
    n = dst_size - 1;
  memcpy(dst, p, n);
  dst[n] = '\0';
  return 1;
}

static int bench_json_extract_double(const char *src, const char *key,
                                     double *out_v) {
  if (!src || !key || !out_v)
    return 0;

  const char *p = strstr(src, key);
  if (!p)
    return 0;
  p = strchr(p, ':');
  if (!p)
    return 0;
  p++;
  while (*p == ' ' || *p == '\t')
    p++;

  char *endptr = NULL;
  double v = strtod(p, &endptr);
  if (endptr == p)
    return 0;

  *out_v = v;
  return 1;
}

static int bench_ref_from_json_file(const char *path, BenchReference *out_ref) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (sz <= 0) {
    fclose(f);
    return 0;
  }

  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return 0;
  }

  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return 0;
  }
  buf[sz] = '\0';
  fclose(f);

  BenchReference r;
  memset(&r, 0, sizeof(r));
  int ok = bench_json_extract_string(buf, "\"cpu_name\"", r.cpu_name,
                                     sizeof(r.cpu_name)) &&
           bench_json_extract_double(buf, "\"single_throughput\"",
                                     &r.single_throughput) &&
           bench_json_extract_double(buf, "\"multi_throughput\"",
                                     &r.multi_throughput);
  free(buf);

  if (!ok)
    return 0;
  *out_ref = r;
  return 1;
}

static int bench_refs_from_json_file(const char *path, BenchReference *out_refs,
                                     int max_refs) {
  if (!path || !out_refs || max_refs <= 0)
    return 0;

  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (sz <= 0) {
    fclose(f);
    return 0;
  }

  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return 0;
  }

  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return 0;
  }
  buf[sz] = '\0';
  fclose(f);

  int count = 0;
  const char *cursor = buf;
  while (count < max_refs) {
    const char *cpu_pos = strstr(cursor, "\"cpu_name\"");
    if (!cpu_pos)
      break;

    BenchReference ref;
    memset(&ref, 0, sizeof(ref));
    int ok = bench_json_extract_string(cpu_pos, "\"cpu_name\"", ref.cpu_name,
                                       sizeof(ref.cpu_name)) &&
             bench_json_extract_double(cpu_pos, "\"single_throughput\"",
                                       &ref.single_throughput) &&
             bench_json_extract_double(cpu_pos, "\"multi_throughput\"",
                                       &ref.multi_throughput);
    if (ok)
      out_refs[count++] = ref;

    cursor = cpu_pos + 10;
  }

  free(buf);

  if (count > 0)
    return count;

  BenchReference single_ref;
  if (!bench_ref_from_json_file(path, &single_ref))
    return 0;
  out_refs[0] = single_ref;
  return 1;
}

static void bench_get_active_ref(BenchReference *out_ref) {
  if (!out_ref)
    return;
  if (g_ref_count <= 0) {
    bench_ref_set_default(out_ref);
    return;
  }

  int idx = g_ref_selected;
  if (idx < 0 || idx >= g_ref_count)
    idx = 0;
  *out_ref = g_refs[idx];
}

static double qpc_seconds_since(LARGE_INTEGER start, LARGE_INTEGER now,
                                LARGE_INTEGER freq) {
  return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

static unsigned long long bench_matrix_iteration_ops(void) {
  return 2ULL * (unsigned long long)BENCH_MATRIX_N * (unsigned long long)BENCH_MATRIX_N *
         (unsigned long long)BENCH_MATRIX_N;
}

static void bench_do_matrix_iteration(double *a, double *b, double *c) {
  for (int i = 0; i < BENCH_MATRIX_N; ++i) {
    for (int j = 0; j < BENCH_MATRIX_N; ++j) {
      double sum = 0.0;
      for (int k = 0; k < BENCH_MATRIX_N; ++k)
        sum += a[i * BENCH_MATRIX_N + k] * b[k * BENCH_MATRIX_N + j];
      c[i * BENCH_MATRIX_N + j] = sum;
    }
  }

  a[0] += c[0] * 1e-12;
  b[1] += c[1] * 1e-12;
  g_bench_sink += c[2] * 1e-12;
}

static unsigned __stdcall bench_worker_loop(void *arg) {
  BenchThreadCtx *ctx = (BenchThreadCtx *)arg;
  LARGE_INTEGER now;
  unsigned long long iter = 0;

  double *a = (double *)malloc(sizeof(double) * BENCH_MATRIX_N * BENCH_MATRIX_N);
  double *b = (double *)malloc(sizeof(double) * BENCH_MATRIX_N * BENCH_MATRIX_N);
  double *c = (double *)malloc(sizeof(double) * BENCH_MATRIX_N * BENCH_MATRIX_N);

  if (!a || !b || !c) {
    free(a);
    free(b);
    free(c);
    return 0;
  }

  for (int i = 0; i < BENCH_MATRIX_N * BENCH_MATRIX_N; ++i) {
    a[i] = (double)((i % 17) + 1) * 0.125;
    b[i] = (double)((i % 11) + 1) * 0.0625;
    c[i] = 0.0;
  }

  for (;;) {
    if (WaitForSingleObject(ctx->stop_event, 0) == WAIT_OBJECT_0)
      break;

    QueryPerformanceCounter(&now);
    if (now.QuadPart >= ctx->end_counter.QuadPart)
      break;

    bench_do_matrix_iteration(a, b, c);
    iter++;
    ctx->iterations = iter;
  }

  ctx->iterations = iter;
  free(a);
  free(b);
  free(c);
  return 0;
}

static int bench_is_cancelled(BenchState *state) {
  return WaitForSingleObject(state->stop_event, 0) == WAIT_OBJECT_0;
}

static void bench_write_result(BenchTestResult *dst, unsigned long long iterations,
                               double elapsed_sec, double ref_throughput) {
  unsigned long long ops = iterations * bench_matrix_iteration_ops();
  double throughput = (elapsed_sec > 0.0) ? ((double)ops / elapsed_sec) : 0.0;
  double score = BENCH_BASELINE_SCORE * (throughput / ref_throughput);

  dst->iterations = iterations;
  dst->throughput = throughput;
  dst->score = score;
  dst->relative = score / BENCH_BASELINE_SCORE;
}

static void bench_publish_live(BenchState *state, enum BenchPhase phase,
                               unsigned long long iterations,
                               double elapsed_sec, double ref_throughput) {
  if (!state)
    return;

  EnterCriticalSection(&state->cs);
  state->elapsed_sec = elapsed_sec;
  if (phase == BENCH_PHASE_MULTI)
    bench_write_result(&state->multi, iterations, elapsed_sec, ref_throughput);
  else if (phase == BENCH_PHASE_SINGLE)
    bench_write_result(&state->single, iterations, elapsed_sec, ref_throughput);
  LeaveCriticalSection(&state->cs);
}

static unsigned long long bench_run_single(BenchState *state, double duration_sec,
                                           double ref_single, double *elapsed_out) {
  LARGE_INTEGER freq, start, end, now;
  HANDLE thread = NULL;
  unsigned tid = 0;
  BenchThreadCtx ctx;

  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&start);
  end.QuadPart = start.QuadPart + (LONGLONG)(duration_sec * (double)freq.QuadPart);

  memset(&ctx, 0, sizeof(ctx));
  ctx.stop_event = state->stop_event;
  ctx.end_counter = end;
  ctx.freq = freq;

  thread = (HANDLE)_beginthreadex(NULL, 0, bench_worker_loop, &ctx, 0, &tid);
  if (!thread)
    return 0;

  for (;;) {
    DWORD w = WaitForSingleObject(thread, 50);

    QueryPerformanceCounter(&now);
    double elapsed = qpc_seconds_since(start, now, freq);
    bench_publish_live(state, BENCH_PHASE_SINGLE, ctx.iterations, elapsed,
                       ref_single);

    if (w == WAIT_OBJECT_0)
      break;
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
  int threads_count = (int)sys.dwNumberOfProcessors;
  if (threads_count < 1)
    threads_count = 1;

  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&start);
  end.QuadPart = start.QuadPart + (LONGLONG)(duration_sec * (double)freq.QuadPart);

  HANDLE *threads = (HANDLE *)calloc((size_t)threads_count, sizeof(HANDLE));
  unsigned *tids = (unsigned *)calloc((size_t)threads_count, sizeof(unsigned));
  BenchThreadCtx *ctxs = (BenchThreadCtx *)calloc((size_t)threads_count, sizeof(BenchThreadCtx));

  if (!threads || !tids || !ctxs) {
    free(threads);
    free(tids);
    free(ctxs);
    return 0;
  }

  for (int i = 0; i < threads_count; ++i) {
    ctxs[i].stop_event = state->stop_event;
    if (duration_sec > 0.0)
      ctxs[i].end_counter = end;
    else
      ctxs[i].end_counter.QuadPart = LLONG_MAX;
    ctxs[i].freq = freq;

    threads[i] = (HANDLE)_beginthreadex(NULL, 0, bench_worker_loop, &ctxs[i], 0, &tids[i]);
  }

  for (;;) {
    int done = 1;
    unsigned long long live_total = 0;

    for (int i = 0; i < threads_count; ++i) {
      if (!threads[i])
        continue;
      if (WaitForSingleObject(threads[i], 0) == WAIT_TIMEOUT)
        done = 0;
      live_total += ctxs[i].iterations;
    }

    QueryPerformanceCounter(&now);
    double elapsed = qpc_seconds_since(start, now, freq);
    bench_publish_live(state, BENCH_PHASE_MULTI, live_total, elapsed, ref_multi);

    if (done)
      break;

    Sleep(50);
  }

  unsigned long long total_iter = 0;
  for (int i = 0; i < threads_count; ++i) {
    if (!threads[i])
      continue;
    WaitForSingleObject(threads[i], INFINITE);
    total_iter += ctxs[i].iterations;
    CloseHandle(threads[i]);
  }

  QueryPerformanceCounter(&now);
  *elapsed_out = qpc_seconds_since(start, now, freq);

  free(threads);
  free(tids);
  free(ctxs);
  return total_iter;
}

static DWORD WINAPI bench_main_thread(LPVOID param) {
  BenchState *state = (BenchState *)param;
  double elapsed = 0.0;
  LONG mode = state->mode;
  BenchReference ref;
  bench_get_active_ref(&ref);

  EnterCriticalSection(&state->cs);
  state->phase = BENCH_PHASE_MULTI;
  state->elapsed_sec = 0.0;
  bench_result_clear(&state->multi);
  bench_result_clear(&state->single);
  LeaveCriticalSection(&state->cs);

  unsigned long long multi_iters =
      bench_run_multi(state, mode == BENCH_MODE_STRESS ? 0.0 : BENCH_TEST_DURATION_SEC,
                      ref.multi_throughput, &elapsed);
  if (bench_is_cancelled(state)) {
    EnterCriticalSection(&state->cs);
    state->phase = BENCH_PHASE_IDLE;
    state->cancelled = 1;
    state->running = 0;
    state->elapsed_sec = 0.0;
    bench_result_clear(&state->multi);
    bench_result_clear(&state->single);
    LeaveCriticalSection(&state->cs);
    return 0;
  }

  if (mode == BENCH_MODE_STRESS) {
    EnterCriticalSection(&state->cs);
    bench_write_result(&state->multi, multi_iters, elapsed, ref.multi_throughput);
    state->phase = BENCH_PHASE_DONE;
    state->running = 0;
    state->cancelled = 0;
    state->elapsed_sec = elapsed;
    LeaveCriticalSection(&state->cs);
    return 0;
  }

  EnterCriticalSection(&state->cs);
  bench_write_result(&state->multi, multi_iters, elapsed, ref.multi_throughput);
  state->phase = BENCH_PHASE_SINGLE;
  state->elapsed_sec = 0.0;
  LeaveCriticalSection(&state->cs);

  unsigned long long single_iters =
      bench_run_single(state, BENCH_TEST_DURATION_SEC, ref.single_throughput, &elapsed);
  if (bench_is_cancelled(state)) {
    EnterCriticalSection(&state->cs);
    state->phase = BENCH_PHASE_IDLE;
    state->cancelled = 1;
    state->running = 0;
    state->elapsed_sec = 0.0;
    bench_result_clear(&state->multi);
    bench_result_clear(&state->single);
    LeaveCriticalSection(&state->cs);
    return 0;
  }

  EnterCriticalSection(&state->cs);
  bench_write_result(&state->single, single_iters, elapsed, ref.single_throughput);
  state->phase = BENCH_PHASE_DONE;
  state->running = 0;
  state->cancelled = 0;
  state->elapsed_sec = 0.0;
  LeaveCriticalSection(&state->cs);

  return 0;
}

void bench_init(BenchState *state) {
  if (!state)
    return;

  memset(state, 0, sizeof(*state));
  InitializeCriticalSection(&state->cs);
  state->stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
  state->phase = BENCH_PHASE_IDLE;
  state->mode = BENCH_MODE_TIMED;
}

void bench_shutdown(BenchState *state) {
  if (!state)
    return;

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
  if (!state || !state->thread)
    return;

  DWORD w = WaitForSingleObject(state->thread, 0);
  if (w == WAIT_OBJECT_0) {
    CloseHandle(state->thread);
    state->thread = NULL;
  }
}

static void bench_start_mode(BenchState *state, enum BenchMode mode) {
  if (!state || !state->stop_event)
    return;

  bench_poll(state);

  EnterCriticalSection(&state->cs);
  if (state->running) {
    LeaveCriticalSection(&state->cs);
    return;
  }

  ResetEvent(state->stop_event);
  state->running = 1;
  state->cancelled = 0;
  state->phase = BENCH_PHASE_MULTI;
  state->mode = mode;
  state->elapsed_sec = 0.0;
  bench_result_clear(&state->multi);
  bench_result_clear(&state->single);
  LeaveCriticalSection(&state->cs);

  DWORD tid = 0;
  state->thread = CreateThread(NULL, 0, bench_main_thread, state, 0, &tid);
  if (!state->thread) {
    EnterCriticalSection(&state->cs);
    state->running = 0;
    state->phase = BENCH_PHASE_IDLE;
    LeaveCriticalSection(&state->cs);
  }
}

void bench_start_timed(BenchState *state) { bench_start_mode(state, BENCH_MODE_TIMED); }

void bench_start_stress(BenchState *state) { bench_start_mode(state, BENCH_MODE_STRESS); }

void bench_stop(BenchState *state) {
  if (!state || !state->stop_event)
    return;

  SetEvent(state->stop_event);
}

void bench_read_ui(BenchState *state, BenchUiState *out_state) {
  if (!state || !out_state)
    return;

  EnterCriticalSection(&state->cs);
  out_state->running = (int)state->running;
  out_state->cancelled = (int)state->cancelled;
  out_state->phase = (enum BenchPhase)state->phase;
  out_state->mode = (enum BenchMode)state->mode;
  out_state->elapsed_sec = state->elapsed_sec;
  out_state->multi = state->multi;
  out_state->single = state->single;
  LeaveCriticalSection(&state->cs);
}

static double bench_step_for_value(double value) {
  if (value <= 2.0)
    return 0.1;
  if (value <= 5.0)
    return 0.2;
  if (value <= 10.0)
    return 0.5;
  return 1.0;
}

double bench_calc_display_max(double relative) {
  double target = relative * 1.2;
  if (target < 1.2)
    target = 1.2;

  double step = bench_step_for_value(target);
  return ceil(target / step) * step;
}

double bench_calc_fill_ratio(double relative, double display_max) {
  if (display_max <= 0.0)
    return 0.0;

  double v = relative / display_max;
  if (v < 0.0)
    return 0.0;
  if (v > 1.0)
    return 1.0;
  return v;
}

double bench_calc_baseline_ratio(double display_max) {
  if (display_max <= 0.0)
    return 0.0;
  return 1.0 / display_max;
}

int bench_refs_load(void) {
  if (g_ref_loaded)
    return g_ref_count;

  g_ref_loaded = 1;
  g_ref_count = 0;
  g_ref_selected = 0;

  const char *patterns[] = {"resources\\jsons\\*.json", "resourses\\jsons\\*.json"};
  for (int p = 0; p < 2; ++p) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(patterns[p], &fd);
    if (h == INVALID_HANDLE_VALUE)
      continue;

    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        continue;
      if (g_ref_count >= BENCH_MAX_REFS)
        break;

      char folder[64];
      snprintf(folder, sizeof(folder), "%s",
               (p == 0) ? "resources\\jsons\\" : "resourses\\jsons\\");
      char path[MAX_PATH];
      snprintf(path, sizeof(path), "%s%s", folder, fd.cFileName);

      int free_slots = BENCH_MAX_REFS - g_ref_count;
      BenchReference parsed[BENCH_MAX_REFS];
      int parsed_count = bench_refs_from_json_file(path, parsed, free_slots);
      for (int i = 0; i < parsed_count && g_ref_count < BENCH_MAX_REFS; ++i)
        g_refs[g_ref_count++] = parsed[i];
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }

  if (g_ref_count == 0) {
    bench_ref_set_default(&g_refs[0]);
    g_ref_count = 1;
  }

  return g_ref_count;
}

int bench_refs_count(void) {
  bench_refs_load();
  return g_ref_count;
}

const BenchReference *bench_ref_get(int index) {
  bench_refs_load();
  if (index < 0 || index >= g_ref_count)
    return NULL;
  return &g_refs[index];
}

int bench_refs_selected(void) {
  bench_refs_load();
  if (g_ref_selected < 0 || g_ref_selected >= g_ref_count)
    g_ref_selected = 0;
  return g_ref_selected;
}

void bench_refs_select(int index) {
  bench_refs_load();
  if (index < 0 || index >= g_ref_count)
    return;
  g_ref_selected = index;
}
