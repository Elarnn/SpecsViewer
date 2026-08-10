#pragma once
#include "nk_common.h"
#include "core/backend.h"

typedef struct {
    GLFWwindow        *window;
    struct nk_glfw     nkglfw;
    struct nk_context *ctx;
    struct nk_font    *font;
    int                sized;          /* 1 after first-frame auto-resize */
    int                mon_x, mon_y, mon_w;
} OverlayState;

void overlay_init    (OverlayState *o, GLFWwindow *share);
void overlay_show    (OverlayState *o);
void overlay_hide    (OverlayState *o);
void overlay_frame   (OverlayState *o, const Snapshot *snap, GLFWwindow *main_win);
void overlay_shutdown(OverlayState *o);
