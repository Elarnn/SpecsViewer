#include "update_dialog.h"
#include <windows.h>
#include <stdio.h>

#define DLG_W        300
#define IDC_DONT_ASK 101

static int  s_result   = 0;
static BOOL s_dont_ask = FALSE;
static char s_ver[64]  = "";

static LRESULT CALLBACK dlg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        wchar_t text[128];
        wchar_t ver_w[64] = L"";
        MultiByteToWideChar(CP_ACP, 0, s_ver, -1, ver_w, 64);
        _snwprintf(text, 128,
                   L"Version %s is available!\n"
                   L"Would you like to update now?", ver_w);

        HWND h;
        h = CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, 12, DLG_W - 20, 44,
            hwnd, NULL, NULL, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);

        h = CreateWindowExW(0, L"BUTTON", L"Don't ask again",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            (DLG_W - 150) / 2, 66, 150, 20,
            hwnd, (HMENU)(UINT_PTR)IDC_DONT_ASK, NULL, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);

        int bw = 110, bh = 28, gap = 6;
        int bx = (DLG_W - bw * 2 - gap) / 2;

        h = CreateWindowExW(0, L"BUTTON", L"Update now",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            bx, 98, bw, bh,
            hwnd, (HMENU)(UINT_PTR)IDYES, NULL, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);

        h = CreateWindowExW(0, L"BUTTON", L"Later",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            bx + bw + gap, 98, bw, bh,
            hwnd, (HMENU)(UINT_PTR)IDNO, NULL, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);

        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDYES || id == IDNO) {
            s_dont_ask = (BOOL)SendDlgItemMessageW(hwnd, IDC_DONT_ASK,
                                                    BM_GETCHECK, 0, 0);
            s_result = (id == IDYES) ? 1 : 0;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        s_dont_ask = (BOOL)SendDlgItemMessageW(hwnd, IDC_DONT_ASK,
                                                BM_GETCHECK, 0, 0);
        s_result = 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int update_dialog_show(const char *version, int *dont_ask_out) {
    strncpy(s_ver, version ? version : "", sizeof(s_ver) - 1);
    s_result   = 0;
    s_dont_ask = FALSE;

    HINSTANCE hinst = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = dlg_proc;
    wc.hInstance     = hinst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SV_UpdateDlg";
    wc.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hIcon         = LoadIconW(hinst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

    /* compute window size from desired client area (98 + 28 + 10 = 136) */
    RECT r = {0, 0, DLG_W, 136};
    AdjustWindowRectEx(&r,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        FALSE,
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST);
    int wnd_w = r.right  - r.left;
    int wnd_h = r.bottom - r.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - wnd_w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - wnd_h) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"SV_UpdateDlg", L"Update Available",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, wnd_w, wnd_h,
        NULL, NULL, hinst, NULL);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(L"SV_UpdateDlg", hinst);

    if (dont_ask_out) *dont_ask_out = s_dont_ask ? 1 : 0;
    return s_result;
}
