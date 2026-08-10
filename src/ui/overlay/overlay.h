#pragma once
#include <windows.h>
#include "core/backend.h"

struct GLFWwindow; /* forward-declare; share/main_win args are ignored */

#define OVL_GRAPH_COUNT 60  /* ~30 seconds at 0.5s update interval */

typedef struct {
    float  values[OVL_GRAPH_COUNT];
    int    head;
    int    initialized;
    double last_t;  /* ms from GetTickCount64 */
} OvlGraph;

typedef struct {
    HWND     hwnd;
    HDC      hdc_screen;
    HDC      hdc_mem;
    HBITMAP  hbm;
    UINT32  *pbits;
    int      width;
    int      height;
    HFONT    hfont;        /* main text (40 px) */
    HFONT    hfont_small;  /* graph labels/values (24 px) */
    int      visible;
    int      advanced;     /* 1 = show graph section below text rows */
    int      mon_x, mon_y;
    OvlGraph g_cpu_load;
    OvlGraph g_gpu_load;
    OvlGraph g_ram;
} OverlayState;

void overlay_init    (OverlayState *o, struct GLFWwindow *share);
void overlay_show    (OverlayState *o);
void overlay_hide    (OverlayState *o);
void overlay_frame   (OverlayState *o, const Snapshot *snap, struct GLFWwindow *main_win);
void overlay_shutdown(OverlayState *o);
