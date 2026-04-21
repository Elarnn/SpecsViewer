// winring_check.c
#include "winring_check.h"
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RELEASES_HOST L"api.github.com"
#define RELEASES_PATH L"/repos/Elarnn/SpecsViewer/releases"
#define ASSET_NAME    "WinRing0x64.zip"

/* ── helpers (mirrors updater_check.c, kept static/local) ───────────────── */

static char *http_get(const wchar_t *host, const wchar_t *path) {
    HINTERNET hSess = WinHttpOpen(L"SpecsViewer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return NULL;

    HINTERNET hConn = WinHttpConnect(hSess, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return NULL; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq) {
        WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return NULL;
    }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, NULL)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess); return NULL;
    }

    size_t cap = 65536, len = 0;
    char *buf = (char *)malloc(cap);
    DWORD avail;
    while (buf && WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        if (len + avail + 1 > cap) {
            cap = len + avail + 8192;
            char *tmp = (char *)realloc(buf, cap);
            if (!tmp) { free(buf); buf = NULL; break; }
            buf = tmp;
        }
        DWORD got = 0;
        WinHttpReadData(hReq, buf + len, avail, &got);
        len += got;
    }
    if (buf) buf[len] = '\0';

    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return buf;
}

static int download_file(const char *url_in, const char *local_path) {
    char url[2048];
    strncpy(url, url_in, sizeof(url) - 1);

    for (int hop = 0; hop < 8; hop++) {
        WCHAR wurl[2048];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048);

        URL_COMPONENTS uc = {0};
        uc.dwStructSize = sizeof(uc);
        WCHAR host[512] = {0}, path[1536] = {0};
        uc.lpszHostName = host; uc.dwHostNameLength = 512;
        uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 1536;
        if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) return 0;

        INTERNET_PORT port = uc.nPort ? uc.nPort :
            (uc.nScheme == INTERNET_SCHEME_HTTPS
                ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
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

        HANDLE hFile = CreateFileA(local_path, GENERIC_WRITE, 0, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
            WinHttpCloseHandle(hSess); return 0;
        }

        int ok = 1;
        DWORD avail;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            char *chunk = (char *)malloc(avail);
            if (!chunk) { ok = 0; break; }
            DWORD got = 0, written = 0;
            WinHttpReadData(hReq, chunk, avail, &got);
            WriteFile(hFile, chunk, got, &written, NULL);
            free(chunk);
            if (written != got) { ok = 0; break; }
        }

        CloseHandle(hFile);
        if (!ok) DeleteFileA(local_path);
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
        return ok;
    }
    return 0;
}

static int extract_zip(const char *zip_path, const char *dest_dir) {
    char cmd[MAX_PATH * 3];
    snprintf(cmd, sizeof(cmd),
        "powershell.exe -NoProfile -NonInteractive -Command "
        "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
        zip_path, dest_dir);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {0};
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

/* Find browser_download_url for the asset whose "name" equals asset_name. */
static int find_asset_url(const char *json, const char *asset_name,
                           char *out, int out_sz) {
    const char *p = json;
    int name_len = (int)strlen(asset_name);

    while ((p = strstr(p, "\"name\"")) != NULL) {
        const char *q = strchr(p + 6, ':');
        if (!q) break;
        while (*q == ':' || *q == ' ') q++;
        if (*q != '"') { p++; continue; }
        q++;
        if (strncmp(q, asset_name, name_len) == 0 && q[name_len] == '"') {
            /* Found the matching asset block — look forward for download URL */
            const char *u = strstr(p, "\"browser_download_url\"");
            if (!u) { p++; continue; }
            u = strchr(u, ':');
            if (!u) { p++; continue; }
            while (*u == ':' || *u == ' ') u++;
            if (*u != '"') { p++; continue; }
            u++;
            int i = 0;
            while (*u && *u != '"' && i < out_sz - 1)
                out[i++] = *u++;
            out[i] = '\0';
            return i > 0;
        }
        p++;
    }
    return 0;
}

/* ── file-presence check ─────────────────────────────────────────────────── */

static void get_exe_dir(char *dir, int dir_sz) {
    GetModuleFileNameA(NULL, dir, dir_sz);
    char *slash = strrchr(dir, '\\');
    if (slash) *(slash + 1) = '\0';
}

static int files_present(void) {
    char dir[MAX_PATH];
    get_exe_dir(dir, sizeof(dir));

    char dll[MAX_PATH], sys[MAX_PATH];
    snprintf(dll, sizeof(dll), "%sWinRing0x64.dll", dir);
    snprintf(sys, sizeof(sys), "%sWinRing0x64.sys", dir);

    return GetFileAttributesA(dll) != INVALID_FILE_ATTRIBUTES &&
           GetFileAttributesA(sys) != INVALID_FILE_ATTRIBUTES;
}

/* ── restart self ────────────────────────────────────────────────────────── */

static void restart_self(void) {
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    ShellExecuteA(NULL, "runas", exe, NULL, NULL, SW_SHOWNORMAL);
    ExitProcess(0);
}

/* ── public ──────────────────────────────────────────────────────────────── */

void winring_ensure(void) {
    if (files_present()) return;

    int choice = MessageBoxA(NULL,
        "WinRing0x64.dll and WinRing0x64.sys were not found next to the executable.\n\n"
        "These files are required for hardware sensor access (CPU temperature, etc.).\n\n"
        "Would you like to download them now from GitHub?",
        "Missing driver files",
        MB_YESNO | MB_ICONWARNING);

    if (choice != IDYES) return;

    /* Fetch releases JSON */
    char *json = http_get(RELEASES_HOST, RELEASES_PATH);
    if (!json) {
        MessageBoxA(NULL,
            "Failed to reach GitHub. Check your internet connection and try again.",
            "Download failed", MB_OK | MB_ICONERROR);
        return;
    }

    /* Find the WinRing0x64.zip asset URL */
    char dl_url[1024] = {0};
    if (!find_asset_url(json, ASSET_NAME, dl_url, sizeof(dl_url))) {
        free(json);
        MessageBoxA(NULL,
            "Could not find \"" ASSET_NAME "\" in the latest release.\n"
            "Please download it manually from the GitHub releases page.",
            "Asset not found", MB_OK | MB_ICONERROR);
        return;
    }
    free(json);

    /* Download to a temp file */
    char tmp_dir[MAX_PATH], tmp_zip[MAX_PATH];
    GetTempPathA(sizeof(tmp_dir), tmp_dir);
    snprintf(tmp_zip, sizeof(tmp_zip), "%ssv_winring0.zip", tmp_dir);

    if (!download_file(dl_url, tmp_zip)) {
        MessageBoxA(NULL,
            "Failed to download \"" ASSET_NAME "\".\n"
            "Check your internet connection and try again.",
            "Download failed", MB_OK | MB_ICONERROR);
        return;
    }

    /* Extract next to the exe */
    char exe_dir[MAX_PATH];
    get_exe_dir(exe_dir, sizeof(exe_dir));
    /* Remove trailing backslash for PowerShell */
    int dlen = (int)strlen(exe_dir);
    if (dlen > 0 && exe_dir[dlen - 1] == '\\') exe_dir[dlen - 1] = '\0';

    if (!extract_zip(tmp_zip, exe_dir)) {
        DeleteFileA(tmp_zip);
        MessageBoxA(NULL,
            "Failed to extract \"" ASSET_NAME "\".\n"
            "You can extract it manually next to SpecsViewer.exe.",
            "Extraction failed", MB_OK | MB_ICONERROR);
        return;
    }
    DeleteFileA(tmp_zip);

    MessageBoxA(NULL,
        "Driver files downloaded successfully.\n"
        "The application will now restart.",
        "Done", MB_OK | MB_ICONINFORMATION);

    restart_self(); /* does not return */
}
