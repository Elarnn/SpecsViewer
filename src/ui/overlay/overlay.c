#include "overlay.h"
#include <string.h>
#include <stdio.h>
#include <wchar.h>

/* GDI overlay via UpdateLayeredWindow.
   Pixels where GDI drew (RGB != 0) get alpha=255; cleared pixels stay
   alpha=0. UpdateLayeredWindow with AC_SRC_ALPHA then composites correctly. */

/* ── layout constants ─────────────────────────────────────────────────── */
#define OVL_MARGIN   14
#define FONT_H       40   /* main text character height */
#define FSMALL_H     24   /* graph label/value character height */
#define ROW_H        54   /* text row height */
#define ROW_GAP       6
#define V_PAD        12
#define H_PAD        14
#define COL_LABEL    80
#define COL_VALUE   230
#define COL_TEMP    110
#define OVL_W       (H_PAD + COL_LABEL + COL_VALUE + COL_TEMP + H_PAD)

/* graph section */
#define GR_LABEL_W   70
#define GR_VAL_W     70
#define GR_W        (OVL_W - H_PAD - GR_LABEL_W - GR_VAL_W - H_PAD)
#define GR_H         44   /* graph row height */
#define GR_GAP        5
#define GR_PAD        8   /* top/bottom padding of graph section */

/* ── helpers ──────────────────────────────────────────────────────────── */

static COLORREF cr_load(double pct) {
    if (pct >= 80.0) return RGB(220,  60,  60);
    if (pct >= 50.0) return RGB(230, 180,  60);
    return                  RGB( 80, 210, 120);
}

static COLORREF cr_temp(int c) {
    if (c >= 85) return RGB(220,  60,  60);
    if (c >= 65) return RGB(230, 180,  60);
    return              RGB( 80, 210, 120);
}

static int text_section_h(int n) {
    return 2 * V_PAD + n * ROW_H + (n - 1) * ROW_GAP;
}

static int graph_section_h(int n) {
    return GR_PAD + n * GR_H + (n - 1) * GR_GAP + GR_PAD;
}

static int total_h(int n_text, int advanced, int n_graphs) {
    return text_section_h(n_text) + (advanced ? graph_section_h(n_graphs) : 0);
}

static void rebuild_dib(OverlayState *o, int w, int h) {
    if (o->hbm) { DeleteObject(o->hbm); o->hbm = NULL; o->pbits = NULL; }
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;  /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    o->hbm = CreateDIBSection(o->hdc_mem, &bmi, DIB_RGB_COLORS,
                              (void **)&o->pbits, NULL, 0);
    SelectObject(o->hdc_mem, o->hbm);
    o->width  = w;
    o->height = h;
}

static LRESULT CALLBACK ovl_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void graph_push(OvlGraph *g, float value) {
    double now = (double)GetTickCount64();
    if (!g->initialized) {
        for (int i = 0; i < OVL_GRAPH_COUNT; i++) g->values[i] = value;
        g->head        = 0;
        g->initialized = 1;
        g->last_t      = now;
        return;
    }
    if (now - g->last_t >= 500.0) {
        g->values[g->head] = value;
        g->head = (g->head + 1) % OVL_GRAPH_COUNT;
        g->last_t = now;
    }
}

static void wdraw(HDC hdc, int x, int y, const WCHAR *s, COLORREF c) {
    SetTextColor(hdc, c);
    TextOutW(hdc, x, y, s, (int)wcslen(s));
}

static void draw_graph(OverlayState *o, int x, int y, int w, int h,
                       const OvlGraph *g, COLORREF color) {
    if (!g->initialized || w < 2 || h < 2) return;

    HPEN pen     = CreatePen(PS_SOLID, 2, color);
    HPEN old_pen = (HPEN)SelectObject(o->hdc_mem, pen);

    int inner_top = y + 2;
    int inner_h   = h - 4;

    BOOL first = TRUE;
    for (int i = 0; i < OVL_GRAPH_COUNT; i++) {
        int   idx = (g->head + i) % OVL_GRAPH_COUNT;
        float v   = g->values[idx];
        if (v < 0.0f)   v = 0.0f;
        if (v > 100.0f) v = 100.0f;
        float t  = v / 100.0f;
        int   px = x + (int)((float)i * (w - 1) / (OVL_GRAPH_COUNT - 1));
        int   py = inner_top + inner_h - 1 - (int)(t * (inner_h - 1));
        if (first) { MoveToEx(o->hdc_mem, px, py, NULL); first = FALSE; }
        else        LineTo(o->hdc_mem, px, py);
    }

    SelectObject(o->hdc_mem, old_pen);
    DeleteObject(pen);
}

/* ── public API ───────────────────────────────────────────────────────── */

void overlay_init(OverlayState *o, struct GLFWwindow *share) {
    (void)share;
    memset(o, 0, sizeof(*o));

    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = ovl_wndproc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "SpecsOvl";
    RegisterClassExA(&wc);

    HMONITOR hmon = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoA(hmon, &mi);
    o->mon_x = mi.rcWork.left;
    o->mon_y = mi.rcWork.top;

    int init_h = total_h(3, 0, 0);
    o->hwnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        "SpecsOvl", "", WS_POPUP,
        o->mon_x + OVL_MARGIN, o->mon_y + OVL_MARGIN, OVL_W, init_h,
        NULL, NULL, GetModuleHandleA(NULL), NULL
    );
    if (!o->hwnd) return;

    o->hdc_screen = GetDC(NULL);
    o->hdc_mem    = CreateCompatibleDC(o->hdc_screen);
    rebuild_dib(o, OVL_W, init_h);

    o->hfont = CreateFontA(
        -FONT_H, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        "Segoe UI"
    );
    o->hfont_small = CreateFontA(
        -FSMALL_H, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        "Segoe UI"
    );
}

void overlay_show(OverlayState *o) {
    if (!o->hwnd) return;
    ShowWindow(o->hwnd, SW_SHOWNOACTIVATE);
    o->visible = 1;
}

void overlay_hide(OverlayState *o) {
    if (!o->hwnd) return;
    ShowWindow(o->hwnd, SW_HIDE);
    o->visible = 0;
}

void overlay_frame(OverlayState *o, const Snapshot *snap, struct GLFWwindow *main_win) {
    (void)main_win;
    if (!o->hwnd || !o->pbits || !o->visible) return;

    MSG msg;
    while (PeekMessageA(&msg, o->hwnd, 0, 0, PM_REMOVE))
        DispatchMessageA(&msg);

    int has_gpu = snap->data.gpu.name[0] != '\0';
    int n_rows  = 2 + (has_gpu ? 1 : 0);
    int n_graphs = n_rows;

    double ram_pct = 0.0;
    if (snap->data.ram.total_mb > 0)
        ram_pct = (double)snap->ram_rt.used_mb * 100.0 / snap->data.ram.total_mb;

    graph_push(&o->g_cpu_load, (float)snap->cpu_rt.load);
    if (has_gpu) graph_push(&o->g_gpu_load, (float)snap->gpu_rt.vram_load);
    graph_push(&o->g_ram, (float)ram_pct);

    int need_h = total_h(n_rows, o->advanced, n_graphs);
    if (need_h != o->height) {
        rebuild_dib(o, OVL_W, need_h);
        SetWindowPos(o->hwnd, NULL, 0, 0, OVL_W, need_h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    memset(o->pbits, 0, (size_t)o->width * o->height * 4);

    /* ── text rows ─────────────────────────────────────────────────── */
    SelectObject(o->hdc_mem, o->hfont);
    SetBkMode(o->hdc_mem, TRANSPARENT);

    TEXTMETRIC tm;
    GetTextMetricsA(o->hdc_mem, &tm);
    int ty = (ROW_H - tm.tmHeight) / 2;

    COLORREF gray = RGB(170, 170, 170);
    WCHAR buf[64];
    int row = 0;

#define RY(r)  (V_PAD + (r) * (ROW_H + ROW_GAP) + ty)
#define RX0    H_PAD
#define RX1    (H_PAD + COL_LABEL)
#define RX2    (H_PAD + COL_LABEL + COL_VALUE)

    wdraw(o->hdc_mem, RX0, RY(row), L"CPU", gray);
    swprintf(buf, 64, L"%.1f%%", snap->cpu_rt.load);
    wdraw(o->hdc_mem, RX1, RY(row), buf, cr_load(snap->cpu_rt.load));
    if (snap->cpu_rt.cpu_temp >= 0) {
        swprintf(buf, 64, L"%d°C", snap->cpu_rt.cpu_temp);
        wdraw(o->hdc_mem, RX2, RY(row), buf, cr_temp(snap->cpu_rt.cpu_temp));
    }
    row++;

    if (has_gpu) {
        wdraw(o->hdc_mem, RX0, RY(row), L"GPU", gray);
        swprintf(buf, 64, L"%.1f%%", snap->gpu_rt.vram_load);
        wdraw(o->hdc_mem, RX1, RY(row), buf, cr_load(snap->gpu_rt.vram_load));
        swprintf(buf, 64, L"%d°C", snap->gpu_rt.clock_temp);
        wdraw(o->hdc_mem, RX2, RY(row), buf, cr_temp(snap->gpu_rt.clock_temp));
        row++;
    }

    wdraw(o->hdc_mem, RX0, RY(row), L"RAM", gray);
    if (snap->data.ram.total_mb > 0) {
        swprintf(buf, 64, L"%.1f / %.0f GB",
                 snap->ram_rt.used_mb   / 1024.0,
                 snap->data.ram.total_mb / 1024.0);
        wdraw(o->hdc_mem, RX1, RY(row), buf, cr_load(ram_pct));
    } else {
        wdraw(o->hdc_mem, RX1, RY(row), L"N/A", gray);
    }

#undef RY
#undef RX0
#undef RX1
#undef RX2

    /* ── graph section ─────────────────────────────────────────────── */
    if (o->advanced) {
        SelectObject(o->hdc_mem, o->hfont_small);
        GetTextMetricsA(o->hdc_mem, &tm);
        int sty  = (GR_H - tm.tmHeight) / 2;
        int base = text_section_h(n_rows) + GR_PAD;

#define GY(r)  (base + (r) * (GR_H + GR_GAP))
#define GLX    H_PAD
#define GGX    (H_PAD + GR_LABEL_W)
#define GVX    (H_PAD + GR_LABEL_W + GR_W + 4)

        int gr = 0;

        /* CPU load */
        wdraw(o->hdc_mem, GLX, GY(gr) + sty, L"CPU", gray);
        draw_graph(o, GGX, GY(gr), GR_W, GR_H,
                   &o->g_cpu_load, cr_load(snap->cpu_rt.load));
        swprintf(buf, 64, L"%.0f%%", snap->cpu_rt.load);
        wdraw(o->hdc_mem, GVX, GY(gr) + sty, buf, cr_load(snap->cpu_rt.load));
        gr++;

        /* GPU load */
        if (has_gpu) {
            wdraw(o->hdc_mem, GLX, GY(gr) + sty, L"GPU", gray);
            draw_graph(o, GGX, GY(gr), GR_W, GR_H,
                       &o->g_gpu_load, cr_load(snap->gpu_rt.vram_load));
            swprintf(buf, 64, L"%.0f%%", snap->gpu_rt.vram_load);
            wdraw(o->hdc_mem, GVX, GY(gr) + sty, buf, cr_load(snap->gpu_rt.vram_load));
            gr++;
        }

        /* RAM */
        wdraw(o->hdc_mem, GLX, GY(gr) + sty, L"RAM", gray);
        draw_graph(o, GGX, GY(gr), GR_W, GR_H,
                   &o->g_ram, cr_load(ram_pct));
        swprintf(buf, 64, L"%.0f%%", ram_pct);
        wdraw(o->hdc_mem, GVX, GY(gr) + sty, buf, cr_load(ram_pct));

#undef GY
#undef GLX
#undef GGX
#undef GVX
    }

    /* ── alpha fix-up ──────────────────────────────────────────────── */
    int npx = o->width * o->height;
    for (int i = 0; i < npx; i++) {
        if (o->pbits[i] & 0x00FFFFFFu)
            o->pbits[i] |= 0xFF000000u;
    }

    /* ── present ───────────────────────────────────────────────────── */
    POINT          pt_src = {0, 0};
    POINT          pt_dst = {o->mon_x + OVL_MARGIN, o->mon_y + OVL_MARGIN};
    SIZE           sz     = {o->width, o->height};
    BLENDFUNCTION  blend  = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(o->hwnd, o->hdc_screen, &pt_dst, &sz,
                        o->hdc_mem, &pt_src, 0, &blend, ULW_ALPHA);
}

void overlay_shutdown(OverlayState *o) {
    if (!o->hwnd) return;
    ShowWindow(o->hwnd, SW_HIDE);
    if (o->hfont_small)  { DeleteObject(o->hfont_small);   o->hfont_small = NULL; }
    if (o->hfont)        { DeleteObject(o->hfont);         o->hfont = NULL; }
    if (o->hbm)          { DeleteObject(o->hbm);           o->hbm = NULL; }
    if (o->hdc_mem)      { DeleteDC(o->hdc_mem);           o->hdc_mem = NULL; }
    if (o->hdc_screen)   { ReleaseDC(NULL, o->hdc_screen); o->hdc_screen = NULL; }
    DestroyWindow(o->hwnd);
    o->hwnd   = NULL;
    o->pbits  = NULL;
    o->visible = 0;
}
