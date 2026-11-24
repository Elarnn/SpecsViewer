#pragma once

#include <windows.h>

#define BENCH_BASELINE_SCORE 1000.0
#define BENCH_WARMUP_SEC     2.0
#define BENCH_SINGLE_SEC     10.0  /* 5 sub-tests x 2 s */
#define BENCH_MULTI_SEC      10.0  /* 5 sub-tests x 2 s */
#define BENCH_MAX_REFS       16
#define BENCH_SUBTESTS       5     /* Integer, Float, Memory, Sort, Scalar */

enum BenchPhase {
  BENCH_PHASE_IDLE = 0,
  BENCH_PHASE_MULTI,
  BENCH_PHASE_SINGLE,
  BENCH_PHASE_DONE,
  BENCH_PHASE_WARMUP
};

typedef struct BenchTestResult {
  double             throughput;   /* composite score in points: 1000 = calibration CPU, 1 thread */
  double             score;        /* BASELINE_SCORE * relative (kept for compatibility)        */
  double             relative;     /* throughput / reference throughput                          */
  unsigned long long iterations;   /* kernel calls, all sub-tests and threads                    */
  double             sub_rate[BENCH_SUBTESTS]; /* raw work units per second, per sub-test        */
  double             sub_pts[BENCH_SUBTESTS];  /* per sub-test points (1000 = calibration CPU)   */
  int                sub_done;                 /* sub-tests completed so far                     */
} BenchTestResult;

typedef struct BenchState {
  HANDLE thread;
  HANDLE stop_event;
  CRITICAL_SECTION cs;

  volatile LONG running;
  volatile LONG cancelled;
  volatile LONG phase;
  volatile LONG errors;       /* failed result checks in this run (invalid result) */
  volatile LONG subtest;      /* index of the sub-test currently running, -1 if none */

  double        elapsed_sec;
  BenchTestResult multi;
  BenchTestResult single;
} BenchState;

typedef struct BenchUiState {
  int             running;
  int             cancelled;
  enum BenchPhase phase;
  int             errors;
  int             subtest;
  double          elapsed_sec;
  BenchTestResult multi;
  BenchTestResult single;
} BenchUiState;

typedef struct BenchReference {
  char   cpu_name[64];
  double single_throughput;   /* points */
  double multi_throughput;    /* points */
} BenchReference;

void bench_init(BenchState *state);
void bench_shutdown(BenchState *state);

void bench_start_timed(BenchState *state);
void bench_stop(BenchState *state);
void bench_poll(BenchState *state);

void bench_read_ui(BenchState *state, BenchUiState *out_state);

/* Short name of a sub-test ("Integer", "Float", ...). */
const char *bench_subtest_name(int index);
/* Logical processors in all processor groups (threads used by the multithread test). */
int bench_logical_cpu_count(void);

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
/* 0 for results recorded with the v1 kernel (raw ops/s), which are not comparable. */
int                   bench_ref_is_compatible(const BenchReference *ref);
