// cpu_freq.c
// Effective current CPU frequency, Task Manager style:
//   nominal frequency x "% Processor Performance" (PDH, no admin rights needed).
// The first call only primes the rate counter; values are available from the
// second call on (the backend polls every tick, so that is a one-tick delay).

#include "../components.h"

#if defined(_WIN32)
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h> /* PDH_CSTATUS_* */

static PDH_HQUERY   g_query   = NULL;
static PDH_HCOUNTER g_perf    = NULL; /* % Processor Performance (_Total)   */
static PDH_HCOUNTER g_nominal = NULL; /* Processor Frequency (_Total), MHz  */
static int          g_state   = 0;    /* 0 = not opened, 1 = ready, -1 = n/a */

static int pdh_value(PDH_HCOUNTER c, double *out) {
  PDH_FMT_COUNTERVALUE v;
  if (!c)
    return 0;
  if (PdhGetFormattedCounterValue(c, PDH_FMT_DOUBLE, NULL, &v) != ERROR_SUCCESS)
    return 0;
  if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA)
    return 0;
  *out = v.doubleValue;
  return 1;
}

static void pdh_open(void) {
  if (PdhOpenQueryW(NULL, 0, &g_query) != ERROR_SUCCESS) {
    g_query = NULL;
    g_state = -1;
    return;
  }
  /* English counter paths work regardless of the Windows UI language */
  if (PdhAddEnglishCounterW(g_query,
                            L"\\Processor Information(_Total)\\% Processor Performance",
                            0, &g_perf) != ERROR_SUCCESS) {
    PdhCloseQuery(g_query);
    g_query = NULL;
    g_perf  = NULL;
    g_state = -1;
    return;
  }
  if (PdhAddEnglishCounterW(g_query,
                            L"\\Processor Information(_Total)\\Processor Frequency",
                            0, &g_nominal) != ERROR_SUCCESS)
    g_nominal = NULL; /* optional: fall back to the detected base frequency */

  PdhCollectQueryData(g_query); /* first sample — rate counters need two */
  g_state = 1;
}

int get_cpu_current_mhz(int base_mhz) {
  if (g_state < 0)
    return 0;
  if (g_state == 0) {
    pdh_open();
    return 0;
  }
  if (PdhCollectQueryData(g_query) != ERROR_SUCCESS)
    return 0;

  double perf = 0.0;
  if (!pdh_value(g_perf, &perf) || perf <= 0.0)
    return 0;

  double nominal = 0.0;
  if (!pdh_value(g_nominal, &nominal) || nominal <= 0.0)
    nominal = (double)base_mhz;
  if (nominal <= 0.0)
    return 0;

  double mhz = nominal * perf / 100.0;
  if (mhz < 1.0 || mhz > 20000.0)
    return 0;
  return (int)(mhz + 0.5);
}

void cpu_current_mhz_shutdown(void) {
  if (g_query) {
    PdhCloseQuery(g_query);
    g_query = NULL;
  }
  g_perf    = NULL;
  g_nominal = NULL;
  g_state   = 0;
}

#else /* !_WIN32 */

int get_cpu_current_mhz(int base_mhz) {
  (void)base_mhz;
  return 0;
}
void cpu_current_mhz_shutdown(void) {}

#endif
