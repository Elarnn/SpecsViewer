// clang-format off
#include "updater_check.h"
#include "app_version.h"
#include <winhttp.h>
#include <shellapi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
// clang-format on

/* ── version helpers ─────────────────────────────────────────────────────── */

static int parse_ver(const char *s, int *a, int *b, int *c) {
    if (*s == 'v' || *s == 'V') s++;
    return sscanf(s, "%d.%d.%d", a, b, c) == 3;
}

static int version_newer(const char *remote, const char *local) {
    int ra=0,rb=0,rc=0,la=0,lb=0,lc=0;
    if (!parse_ver(remote,&ra,&rb,&rc)) return 0;
    if (!parse_ver(local, &la,&lb,&lc)) return 0;
    if (ra!=la) return ra>la;
    if (rb!=lb) return rb>lb;
    return rc>lc;
}

/* ── JSON helpers ────────────────────────────────────────────────────────── */

static int extract_json_string(const char *json, const char *key,
                                char *out, int out_sz) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), ':');
    if (!p) return 0;
    while (*p == ':' || *p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < out_sz - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

/* Extracts a JSON string field and unescapes \n \r \" \\ sequences */
static int extract_json_body(const char *json, const char *key,
                              char *out, int out_sz) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), ':');
    if (!p) return 0;
    while (*p == ':' || *p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && i < out_sz - 1) {
        if (*p == '"') break;
        if (*p == '\\') {
            p++;
            if (*p == 'n')       { out[i++] = '\n'; }
            else if (*p == 'r')  { /* skip \r */ }
            else if (*p == '"')  { out[i++] = '"'; }
            else if (*p == '\\') { out[i++] = '\\'; }
            else                 { out[i++] = *p; }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return i > 0;
}

/* ── WinHTTP: fetch text ─────────────────────────────────────────────────── */

static char *winhttp_get_text(const wchar_t *host, const wchar_t *path) {
    HINTERNET hSess = WinHttpOpen(L"SpecsViewer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return NULL;

    HINTERNET hConn = WinHttpConnect(hSess, host,
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return NULL; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq) {
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return NULL;
    }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, NULL)) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return NULL;
    }

    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);

    DWORD avail;
    while (buf && WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        if (len + avail + 1 > cap) {
            cap = len + avail + 4096;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); buf = NULL; break; }
            buf = tmp;
        }
        DWORD got = 0;
        WinHttpReadData(hReq, buf + len, avail, &got);
        len += got;
    }
    if (buf) buf[len] = '\0';

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return buf;
}

/* ── WinHTTP: download binary to file (follows redirects manually) ───────── */

static int winhttp_download_file(const char *url_in, const char *local_path) {
    char url[2048];
    strncpy(url, url_in, sizeof(url) - 1);

    for (int hop = 0; hop < 8; hop++) {
        WCHAR wurl[2048];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048);

        URL_COMPONENTS uc;
        memset(&uc, 0, sizeof(uc));
        uc.dwStructSize = sizeof(uc);
        WCHAR host[512] = {0};
        WCHAR path[1536] = {0};
        uc.lpszHostName    = host; uc.dwHostNameLength = 512;
        uc.lpszUrlPath     = path; uc.dwUrlPathLength  = 1536;
        if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return 0;

        INTERNET_PORT port = uc.nPort
            ? uc.nPort
            : (uc.nScheme == INTERNET_SCHEME_HTTPS
                ? INTERNET_DEFAULT_HTTPS_PORT
                : INTERNET_DEFAULT_HTTP_PORT);
        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

        HINTERNET hSess = WinHttpOpen(L"SpecsViewer/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSess) return 0;

        HINTERNET hConn = WinHttpConnect(hSess, host, port, 0);
        if (!hConn) { WinHttpCloseHandle(hSess); return 0; }

        HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hReq) {
            WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return 0;
        }

        /* Disable auto-redirect — we handle it ourselves */
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));

        if (!WinHttpSendRequest(hReq, L"Accept: application/octet-stream\r\n",
                                (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(hReq, NULL)) {
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess); return 0;
        }

        DWORD status = 0, status_sz = sizeof(status);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &status, &status_sz, NULL);

        /* Follow redirect */
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            WCHAR loc[2048] = {0};
            DWORD loc_sz = sizeof(loc);
            BOOL got_loc = WinHttpQueryHeaders(hReq, WINHTTP_QUERY_LOCATION,
                                               NULL, loc, &loc_sz, NULL);
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess);
            if (!got_loc) return 0;
            WideCharToMultiByte(CP_UTF8, 0, loc, -1, url, sizeof(url), NULL, NULL);
            continue;
        }

        if (status != 200) {
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess); return 0;
        }

        /* Write body to file */
        HANDLE hFile = CreateFileA(local_path, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess); return 0;
        }

        int ok = 1;
        DWORD avail;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            char *buf = (char *)malloc(avail);
            if (!buf) { ok = 0; break; }
            DWORD got = 0, written = 0;
            WinHttpReadData(hReq, buf, avail, &got);
            WriteFile(hFile, buf, got, &written, NULL);
            free(buf);
            if (written != got) { ok = 0; break; }
        }

        CloseHandle(hFile);
        if (!ok) DeleteFileA(local_path);
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return ok;
    }

    return 0; /* too many redirects */
}

/* ── zip extraction via PowerShell ──────────────────────────────────────── */

static int extract_zip(const char *zip_path, const char *dest_dir) {
    char cmd[MAX_PATH * 3];
    snprintf(cmd, sizeof(cmd),
        "powershell.exe -NoProfile -NonInteractive -Command "
        "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
        zip_path, dest_dir);

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 0;

    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exit_code == 0;
}

/* ── check worker ────────────────────────────────────────────────────────── */

static DWORD WINAPI update_worker(LPVOID param) {
    UpdateState *us = (UpdateState *)param;

    char *body = winhttp_get_text(L"api.github.com",
        L"/repos/Elarnn/SpecsViewer/releases");
    if (!body) { InterlockedExchange(&us->state, UPDATE_ERROR); return 0; }

    char tag[64] = {0};
    int ok = extract_json_string(body, "\"tag_name\"", tag, sizeof(tag));
    if (ok)
        extract_json_body(body, "\"body\"", us->release_body, sizeof(us->release_body));
    free(body);

    if (!ok) { InterlockedExchange(&us->state, UPDATE_ERROR); return 0; }

    strncpy(us->latest_ver, tag, sizeof(us->latest_ver) - 1);
    us->latest_ver[sizeof(us->latest_ver) - 1] = '\0';

    InterlockedExchange(&us->state,
        version_newer(tag, SPECSVIEWER_VERSION) ? UPDATE_AVAILABLE : UPDATE_LATEST);
    return 0;
}

/* Finds the browser_download_url of an asset whose "name" contains "updater"
   (case-insensitive). Walks all "name" occurrences, checks value, then grabs
   the next "browser_download_url" within 8192 bytes. */
static int find_updater_asset_url(const char *json, char *out, int out_sz) {
    const char *p = json;
    while ((p = strstr(p, "\"name\"")) != NULL) {
        const char *colon = strchr(p + 6, ':');
        if (!colon) break;
        colon++;
        while (*colon == ' ') colon++;
        if (*colon != '"') { p += 6; continue; }
        colon++;

        char name[256] = {0};
        int i = 0;
        const char *q = colon;
        while (*q && *q != '"' && i < (int)sizeof(name) - 1)
            name[i++] = *q++;
        name[i] = '\0';

        /* case-insensitive search for "updater" in name */
        char lower[256];
        for (int j = 0; name[j]; j++)
            lower[j] = (char)(name[j] >= 'A' && name[j] <= 'Z' ? name[j] + 32 : name[j]);
        lower[i] = '\0';

        if (strstr(lower, "updater")) {
            /* found the right asset — grab the next browser_download_url nearby */
            const char *url_start = strstr(q, "\"browser_download_url\"");
            if (url_start && (url_start - q) < 8192)
                return extract_json_string(url_start, "\"browser_download_url\"",
                                           out, out_sz);
        }
        p = q;
    }
    return 0;
}

/* ── download-updater worker ─────────────────────────────────────────────── */

typedef struct {
    UpdateState *us;
    char updater_dir[MAX_PATH];
    char exe_path[MAX_PATH];
} DlCtx;

static DWORD WINAPI download_updater_worker(LPVOID param) {
    DlCtx *ctx = (DlCtx *)param;
    UpdateState *us = ctx->us;

    /* 1. Fetch releases list from main repo */
    char *body = winhttp_get_text(L"api.github.com",
        L"/repos/Elarnn/SpecsViewer/releases");
    if (!body) {
        strncpy(us->dl_error, "Failed to fetch release info",
                sizeof(us->dl_error) - 1);
        InterlockedExchange(&us->state, UPDATE_DL_ERROR);
        free(ctx);
        return 0;
    }

    /* 2. Find asset whose name contains "updater" (case-insensitive) */
    char dl_url[1024] = {0};
    if (!find_updater_asset_url(body, dl_url, sizeof(dl_url))) {
        strncpy(us->dl_error, "No updater asset found in release",
                sizeof(us->dl_error) - 1);
        free(body);
        InterlockedExchange(&us->state, UPDATE_DL_ERROR);
        free(ctx);
        return 0;
    }
    free(body);

    /* 3. Build temp file path */
    char tmp_dir[MAX_PATH];
    char tmp_path[MAX_PATH];
    GetTempPathA(sizeof(tmp_dir), tmp_dir);
    const char *dot = strrchr(dl_url, '.');
    int is_zip = dot && (_stricmp(dot, ".zip") == 0);
    snprintf(tmp_path, sizeof(tmp_path), "%ssv_updater_dl%s",
             tmp_dir, is_zip ? ".zip" : ".exe");

    /* 4. Download asset */
    if (!winhttp_download_file(dl_url, tmp_path)) {
        strncpy(us->dl_error, "Failed to download updater",
                sizeof(us->dl_error) - 1);
        InterlockedExchange(&us->state, UPDATE_DL_ERROR);
        free(ctx);
        return 0;
    }

    /* 5. Create updater directory */
    CreateDirectoryA(ctx->updater_dir, NULL);

    /* 6. Extract zip or move exe */
    if (is_zip) {
        if (!extract_zip(tmp_path, ctx->updater_dir)) {
            strncpy(us->dl_error, "Failed to extract updater archive",
                    sizeof(us->dl_error) - 1);
            DeleteFileA(tmp_path);
            InterlockedExchange(&us->state, UPDATE_DL_ERROR);
            free(ctx);
            return 0;
        }
        DeleteFileA(tmp_path);
    } else {
        if (!MoveFileExA(tmp_path, ctx->exe_path, MOVEFILE_REPLACE_EXISTING)) {
            strncpy(us->dl_error, "Failed to save updater executable",
                    sizeof(us->dl_error) - 1);
            DeleteFileA(tmp_path);
            InterlockedExchange(&us->state, UPDATE_DL_ERROR);
            free(ctx);
            return 0;
        }
    }

    /* 7. Launch updater immediately */
    ShellExecuteA(NULL, "open", ctx->exe_path, NULL, NULL, SW_SHOWNORMAL);
    InterlockedExchange(&us->state, UPDATE_AVAILABLE);
    free(ctx);
    return 0;
}

/* ── public API ──────────────────────────────────────────────────────────── */

void update_check_start(UpdateState *us) {
    if (InterlockedCompareExchange(&us->state, UPDATE_CHECKING, UPDATE_IDLE) != UPDATE_IDLE)
        return;
    if (us->thread) {
        WaitForSingleObject(us->thread, 0);
        CloseHandle(us->thread);
        us->thread = NULL;
    }
    us->thread = CreateThread(NULL, 0, update_worker, us, 0, NULL);
    if (!us->thread)
        InterlockedExchange(&us->state, UPDATE_ERROR);
}

void update_cleanup(UpdateState *us) {
    if (us->thread) {
        WaitForSingleObject(us->thread, INFINITE);
        CloseHandle(us->thread);
        us->thread = NULL;
    }
}

void update_launch_updater(UpdateState *us) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    char *slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy(slash + 1, "updater\\updater.exe");

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        char msg[MAX_PATH + 128];
        snprintf(msg, sizeof(msg),
            "Updater not found:\n%s\n\nDownload the latest updater now?", path);
        if (MessageBoxA(NULL, msg, "Updater Not Found",
                        MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;

        /* Build download context */
        DlCtx *ctx = (DlCtx *)calloc(1, sizeof(DlCtx));
        if (!ctx) return;
        ctx->us = us;

        char dir[MAX_PATH];
        GetModuleFileNameA(NULL, dir, sizeof(dir));
        char *s = strrchr(dir, '\\');
        if (s) strcpy(s + 1, "updater");
        strncpy(ctx->updater_dir, dir,  sizeof(ctx->updater_dir) - 1);
        strncpy(ctx->exe_path,    path, sizeof(ctx->exe_path)    - 1);

        if (us->thread) {
            WaitForSingleObject(us->thread, 0);
            CloseHandle(us->thread);
            us->thread = NULL;
        }

        InterlockedExchange(&us->state, UPDATE_DL_UPDATER);
        us->thread = CreateThread(NULL, 0, download_updater_worker, ctx, 0, NULL);
        if (!us->thread) {
            strncpy(us->dl_error, "Failed to start download thread",
                    sizeof(us->dl_error) - 1);
            InterlockedExchange(&us->state, UPDATE_DL_ERROR);
            free(ctx);
        }
        return;
    }

    ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
}
