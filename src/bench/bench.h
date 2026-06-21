#pragma once

#include <windows.h>

#define BENCH_BASELINE_SCORE 1000.0
#define BENCH_WARMUP_SEC     2.0
#define BENCH_SINGLE_SEC     7.0
#define BENCH_MULTI_SEC      10.0
#define BENCH_MAX_REFS       16

enum BenchPhase {
  BENCH_PHASE_IDLE = 0,
  BENCH_PHASE_MULTI,
  BENCH_PHASE_SINGLE,
  BENCH_PHASE_DONE,
  BENCH_PHASE_WARMUP
};

enum BenchMode {
  BENCH_MODE_TIMED = 0,
  BENCH_MODE_STRESS
};

typedef struct BenchTestResult {
  double             throughput;   /* ops/sec (iters * BENCH_KERNEL_OPS / elapsed) */
  double             score;        /* BASELINE_SCORE * (throughput / ref_throughput) */
  double             relative;     /* throughput / ref_throughput */
  unsigned long long iterations;
} BenchTestResult;

typedef struct BenchState {
  HANDLE thread;
  HANDLE stop_event;
  CRITICAL_SECTION cs;

  volatile LONG running;
  volatile LONG cancelled;
  volatile LONG phase;
  volatile LONG mode;

  double        elapsed_sec;
  BenchTestResult multi;
  BenchTestResult single;
} BenchState;

typedef struct BenchUiState {
  int             running;
  int             cancelled;
  enum BenchPhase phase;
  enum BenchMode  mode;
  double          elapsed_sec;
  BenchTestResult multi;
  BenchTestResult single;
} BenchUiState;

typedef struct BenchReference {
  char   cpu_name[64];
  double single_throughput;
  double multi_throughput;
} BenchReference;

void bench_init(BenchState *state);
void bench_shutdown(BenchState *state);

void bench_start_timed(BenchState *state);
void bench_start_stress(BenchState *state);
void bench_stop(BenchState *state);
void bench_poll(BenchState *state);

void bench_read_ui(BenchState *state, BenchUiState *out_state);

double bench_calc_display_max(double relative);
double bench_calc_fill_ratio(double relative, double display_max);
double bench_calc_baseline_ratio(double display_max);

void                  bench_get_default_ref(BenchReference *out);
int                   bench_refs_load(void);
void                  bench_refs_reload(void);
int                   bench_refs_parse(const char *json_buf, BenchReference *out, int max);
void                  bench_refs_set(const BenchReference *refs, int count);
int                   bench_refs_count(void);
const BenchReference *bench_ref_get(int index);
int                   bench_refs_selected(void);
void                  bench_refs_select(int index);
