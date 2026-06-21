#pragma once
#include <windows.h>
#include "bench/bench.h"

typedef enum {
    BENCH_SUBMIT_IDLE    = 0,
    BENCH_SUBMIT_PENDING = 1,
    BENCH_SUBMIT_OK      = 2,
    BENCH_SUBMIT_ERROR   = 3
} BenchSubmitStatus;

typedef struct {
    HANDLE submit_thread;
    HANDLE refs_thread;
    volatile LONG submit_status;
    volatile LONG refs_ready;               /* set to 1 when server_refs is populated */
    BenchReference server_refs[BENCH_MAX_REFS];
    int server_refs_count;
} BenchSyncState;

void bench_sync_init(BenchSyncState *bs);
void bench_sync_shutdown(BenchSyncState *bs);

/* POST /submit in a background thread. */
void bench_sync_submit(BenchSyncState *bs, const char *cpu_name,
                       double single_throughput, double multi_throughput,
                       int threads);

/* GET /refs, parse in-memory, signal refs_ready. Fire-and-forget at startup. */
void bench_sync_fetch_refs(BenchSyncState *bs);
