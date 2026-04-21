#pragma once

#include <windows.h>

#define BENCH_BASELINE_SCORE 1000.0
#define BENCH_TEST_DURATION_SEC 15.0

enum BenchPhase {
  BENCH_PHASE_IDLE = 0,
  BENCH_PHASE_MULTI,
  BENCH_PHASE_SINGLE,
  BENCH_PHASE_DONE
};

enum BenchMode {
  BENCH_MODE_TIMED = 0,
  BENCH_MODE_STRESS
};

typedef struct BenchTestResult {
  double throughput;
  double score;
  double relative;
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

  double elapsed_sec;
  BenchTestResult multi;
  BenchTestResult single;
} BenchState;

typedef struct BenchUiState {
  int running;
  int cancelled;
  enum BenchPhase phase;
  enum BenchMode mode;
  double elapsed_sec;
  BenchTestResult multi;
  BenchTestResult single;
} BenchUiState;

typedef struct BenchReference {
  char cpu_name[64];
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

int bench_refs_load(void);
int bench_refs_count(void);
const BenchReference *bench_ref_get(int index);
int bench_refs_selected(void);
void bench_refs_select(int index);
