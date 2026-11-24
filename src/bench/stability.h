#pragma once

/* CPU stability test with calculation error detection.

   Every logical processor gets its own worker thread hard-pinned to it. The
   workers run deterministic work units and compare each result with a
   reference computed before the load starts, so a single wrong bit on any
   core is reported as a calculation error, attributed to that core. The
   Cache/RAM test writes a pseudo-random pattern into a per-thread buffer and
   verifies it on read-back. During the test Windows' WHEA-Logger events
   (machine-check errors corrected by the hardware) are counted as well. */

#include <windows.h>

#define STAB_MAX_CPUS  256
#define STAB_LOG_LINES 8
#define STAB_LOG_LEN   128

enum StabTest {
  STAB_TEST_MIXED = 0,   /* round-robin of all tests: varying load and power   */
  STAB_TEST_INTEGER,     /* 64/128-bit multiply, rotate, divide                */
  STAB_TEST_FLOAT,       /* scalar/SSE double precision chaotic map + sqrt      */
  STAB_TEST_AVX2,        /* 256-bit FMA, the heaviest power load               */
  STAB_TEST_CACHE,       /* write / verify pattern in cache and RAM            */
  STAB_TEST_COUNT
};

enum StabStatus {
  STAB_STATUS_IDLE = 0,
  STAB_STATUS_PREPARING,  /* computing reference results                        */
  STAB_STATUS_RUNNING,
  STAB_STATUS_PASSED,     /* full duration without calculation errors            */
  STAB_STATUS_STOPPED,    /* stopped by the user, no calculation errors          */
  STAB_STATUS_FAILED,     /* at least one calculation error                      */
  STAB_STATUS_ERROR       /* could not start (out of memory, thread creation)    */
};

typedef struct StabCpu {
  _Alignas(64) volatile LONG64 units;  /* verified work units, written by its worker */
  volatile LONG errors;
  int core;       /* physical core index, -1 if unknown */
  int eff_class;  /* EfficiencyClass, higher = faster (P-core on hybrid CPUs) */
} StabCpu;

typedef struct StabilityState {
  HANDLE thread;
  HANDLE stop_event;
  CRITICAL_SECTION cs;

  volatile LONG running;
  volatile LONG status;        /* enum StabStatus */
  volatile LONG total_errors;
  volatile LONG user_stop;

  int    test;
  double duration_sec;         /* 0 = until Stop */
  int    stop_on_error;
  int    ncpu;
  int    hybrid;               /* 1 when cores have different efficiency classes */
  StabCpu *cpus;               /* ncpu entries, 64-byte aligned */

  LARGE_INTEGER start_qpc;
  double elapsed_sec;          /* guarded by cs */
  int    whea_errors;          /* -1 = event log unavailable; guarded by cs */

  char log[STAB_LOG_LINES][STAB_LOG_LEN]; /* ring buffer, guarded by cs */
  int  log_count;                         /* total lines ever written      */
} StabilityState;

typedef struct StabCpuView {
  long long units;
  int errors;
  int core;
  int eff_class;
} StabCpuView;

typedef struct StabUiState {
  int    running;
  enum StabStatus status;
  int    test;
  double duration_sec;
  double elapsed_sec;
  int    total_errors;
  int    whea_errors;
  long long total_units;
  int    ncpu;
  int    hybrid;
  StabCpuView cpus[STAB_MAX_CPUS];
  int    log_n;                              /* lines in log[], oldest first */
  char   log[STAB_LOG_LINES][STAB_LOG_LEN];
} StabUiState;

void stab_init(StabilityState *st);
void stab_shutdown(StabilityState *st);

/* duration_sec <= 0 runs until stab_stop(). Returns 0 if already running. */
int  stab_start(StabilityState *st, int test, double duration_sec, int stop_on_error);
void stab_stop(StabilityState *st);
void stab_read_ui(StabilityState *st, StabUiState *out);

const char *stab_test_name(int test);
const char *stab_test_description(int test);
/* 1 if the CPU and the OS support AVX2 + FMA. */
int stab_avx2_supported(void);
