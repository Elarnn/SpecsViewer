// clang-format off
#include "bench_sync.h"
#include "bench/bench.h"
#include "config.h"
#include "app_version.h"
#include <winhttp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// clang-format on

/* ── HTTP helpers ────────────────────────────────────────────────────────── */

static char *http_get(const wchar_t *host, const wchar_t *path) {
    HINTERNET hS = WinHttpOpen(L"SpecsViewer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return NULL;
    WinHttpSetTimeouts(hS, 10000, 10000, 15000, 15000);

    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return NULL; }

    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return NULL; }

    if (!WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hR, NULL)) {
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return NULL;
    }

    size_t cap = 65536, len = 0;
    char *buf = (char *)malloc(cap);
    DWORD avail;
    while (buf && WinHttpQueryDataAvailable(hR, &avail) && avail > 0) {
        if (len + avail + 1 > cap) {
            cap = len + avail + 16384;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); buf = NULL; break; }
            buf = tmp;
        }
        DWORD got = 0;
        WinHttpReadData(hR, buf + len, avail, &got);
        len += got;
    }
    if (buf) buf[len] = '\0';

    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return buf;
}

static int http_post_json(const wchar_t *host, const wchar_t *path,
                          const char *api_key, const char *body) {
    HINTERNET hS = WinHttpOpen(L"SpecsViewer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return 0;
    WinHttpSetTimeouts(hS, 10000, 10000, 15000, 15000);

    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return 0; }

    HINTERNET hR = WinHttpOpenRequest(hC, L"POST", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return 0; }

    wchar_t hdrs[512];
    wchar_t key_w[256] = L"";
    MultiByteToWideChar(CP_ACP, 0, api_key, -1, key_w, 256);
    _snwprintf(hdrs, 512, L"Content-Type: application/json\r\nX-Api-Key: %s", key_w);

    DWORD blen = (DWORD)strlen(body);
    int sent = WinHttpSendRequest(hR, hdrs, (DWORD)-1, (LPVOID)body, blen, blen, 0) &&
               WinHttpReceiveResponse(hR, NULL);

    int status = 0;
    if (sent) {
        DWORD s = 0, ssz = sizeof(s);
        WinHttpQueryHeaders(hR,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &s, &ssz, NULL);
        status = (int)s;
    }

    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    return status;
}

/* ── JSON helper ─────────────────────────────────────────────────────────── */

static void json_escape(const char *in, char *out, size_t out_sz) {
    size_t i = 0;
    while (*in && i + 3 < out_sz) {
        if (*in == '"' || *in == '\\') out[i++] = '\\';
        out[i++] = *in++;
    }
    out[i] = '\0';
}

/* ── Submit worker ───────────────────────────────────────────────────────── */

typedef struct {
    BenchSyncState *bs;
    char  cpu_name[256];
    double single_throughput;
    double multi_throughput;
    int   threads;
} SubmitCtx;

static DWORD WINAPI submit_worker(LPVOID param) {
    SubmitCtx *ctx = (SubmitCtx *)param;

    char safe_cpu[512];
    json_escape(ctx->cpu_name, safe_cpu, sizeof(safe_cpu));

    char body[1024];
    snprintf(body, sizeof(body),
        "{\"cpu_name\":\"%s\","
        "\"single_throughput\":%.2f,"
        "\"multi_throughput\":%.2f,"
        "\"threads\":%d,"
        "\"app_version\":\"%s\"}",
        safe_cpu,
        ctx->single_throughput,
        ctx->multi_throughput,
        ctx->threads,
        SPECSVIEWER_VERSION);

    int status = http_post_json(BENCH_SERVER_HOST, L"/submit", BENCH_API_KEY, body);
    InterlockedExchange(&ctx->bs->submit_status,
        (status == 201) ? (LONG)BENCH_SUBMIT_OK : (LONG)BENCH_SUBMIT_ERROR);

    free(ctx);
    return 0;
}

void bench_sync_submit(BenchSyncState *bs, const char *cpu_name,
                       double single_throughput, double multi_throughput,
                       int threads) {
    if (bs->submit_thread) {
        if (WaitForSingleObject(bs->submit_thread, 0) == WAIT_TIMEOUT)
            return; /* already in flight */
        CloseHandle(bs->submit_thread);
        bs->submit_thread = NULL;
    }

    SubmitCtx *ctx = (SubmitCtx *)calloc(1, sizeof(SubmitCtx));
    if (!ctx) { InterlockedExchange(&bs->submit_status, (LONG)BENCH_SUBMIT_ERROR); return; }

    ctx->bs               = bs;
    ctx->single_throughput = single_throughput;
    ctx->multi_throughput  = multi_throughput;
    ctx->threads           = threads;
    strncpy(ctx->cpu_name, cpu_name ? cpu_name : "Unknown", sizeof(ctx->cpu_name) - 1);

    InterlockedExchange(&bs->submit_status, (LONG)BENCH_SUBMIT_PENDING);
    bs->submit_thread = CreateThread(NULL, 0, submit_worker, ctx, 0, NULL);
    if (!bs->submit_thread) {
        InterlockedExchange(&bs->submit_status, (LONG)BENCH_SUBMIT_ERROR);
        free(ctx);
    }
}

/* ── Refs fetch worker ───────────────────────────────────────────────────── */

static DWORD WINAPI refs_worker(LPVOID param) {
    BenchSyncState *bs = (BenchSyncState *)param;

    char *body = http_get(BENCH_SERVER_HOST, L"/refs");
    if (!body) return 0;

    BenchReference tmp[BENCH_MAX_REFS];
    int n = bench_refs_parse(body, tmp, BENCH_MAX_REFS);
    free(body);

    if (n > 0) {
        bs->server_refs_count = n;
        for (int i = 0; i < n; i++)
            bs->server_refs[i] = tmp[i];
        InterlockedExchange(&bs->refs_ready, 1);
    }
    return 0;
}

void bench_sync_fetch_refs(BenchSyncState *bs) {
    if (bs->refs_thread) {
        WaitForSingleObject(bs->refs_thread, 0);
        CloseHandle(bs->refs_thread);
        bs->refs_thread = NULL;
    }
    bs->refs_thread = CreateThread(NULL, 0, refs_worker, bs, 0, NULL);
}

/* ── Init / Shutdown ─────────────────────────────────────────────────────── */

void bench_sync_init(BenchSyncState *bs) {
    memset(bs, 0, sizeof(*bs));
}

void bench_sync_shutdown(BenchSyncState *bs) {
    if (bs->submit_thread) {
        WaitForSingleObject(bs->submit_thread, 10000);
        CloseHandle(bs->submit_thread);
        bs->submit_thread = NULL;
    }
    if (bs->refs_thread) {
        WaitForSingleObject(bs->refs_thread, 10000);
        CloseHandle(bs->refs_thread);
        bs->refs_thread = NULL;
    }
}
