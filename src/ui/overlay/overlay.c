#define GLFW_EXPOSE_NATIVE_WIN32
#include "nk_common.h"
#include <GLFW/glfw3native.h>
#include "overlay.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define OVL_W       200
#define OVL_MARGIN   12
#define ROW_H        22
#define ROW_GAP       2
#define V_PAD         6
#define COL_LABEL    40
#define COL_VALUE    76
#define COL_TEMP     60

static struct nk_color col_load(double pct) {
    if (pct >= 80.0) return nk_rgba(220, 60,  60,  255);
    if (pct >= 50.0) return nk_rgba(230, 180, 60,  255);
    return                  nk_rgba(100, 210, 130, 255);
}

static struct nk_color col_temp(int c) {
    if (c >= 85) return nk_rgba(220, 60,  60,  255);
    if (c >= 65) return nk_rgba(230, 180, 60,  255);
    return              nk_rgba(100, 210, 130, 255);
}

static int rows_height(int n) {
    return 2 * V_PAD + n * ROW_H + (n - 1) * ROW_GAP;
}

void overlay_init(OverlayState *o, GLFWwindow *share) {
    memset(o, 0, sizeof(*o));

    GLFWwindow *prev = glfwGetCurrentContext();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DECORATED,               GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING,                GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE,               GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE,                 GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,           GLFW_FALSE);

    o->window = glfwCreateWindow(OVL_W, rows_height(3), "", NULL, share);
    if (!o->window) { glfwMakeContextCurrent(prev); return; }

    int mx, my, mw, mh;
    glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &mx, &my, &mw, &mh);
    glfwSetWindowPos(o->window, mx + mw - OVL_W - OVL_MARGIN, my + OVL_MARGIN);
    o->mon_x = mx;
    o->mon_y = my;
    o->mon_w = mw;

    HWND hwnd = glfwGetWin32Window(o->window);
    LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    glfwMakeContextCurrent(o->window);
    o->ctx = nk_glfw3_init(&o->nkglfw, o->window, 0);

    struct nk_font_atlas *atlas;
    nk_glfw3_font_stash_begin(&o->nkglfw, &atlas);
    o->font = nk_font_atlas_add_from_file(atlas, "resources\\fonts\\segoeui.ttf", 14.0f, NULL);
    if (!o->font)
        o->font = nk_font_atlas_add_default(atlas, 14.0f, NULL);
    nk_glfw3_font_stash_end(&o->nkglfw);
    if (o->font) nk_style_set_font(o->ctx, &o->font->handle);

    struct nk_color bg = nk_rgba(12, 12, 12, 215);
    o->ctx->style.window.fixed_background = nk_style_item_color(bg);
    o->ctx->style.window.background       = bg;
    o->ctx->style.window.border           = 0.0f;
    o->ctx->style.window.padding          = nk_vec2(8.0f, (float)V_PAD);
    o->ctx->style.window.spacing          = nk_vec2(4.0f, (float)ROW_GAP);
    o->ctx->style.text.color              = nk_rgba(200, 200, 200, 255);

    glfwMakeContextCurrent(prev);
}

void overlay_show(OverlayState *o) {
    if (o->window) glfwShowWindow(o->window);
}

void overlay_hide(OverlayState *o) {
    if (o->window) glfwHideWindow(o->window);
}

void overlay_frame(OverlayState *o, const Snapshot *snap, GLFWwindow *main_win) {
    if (!o->window || !o->ctx) return;

    int has_gpu = snap->data.gpu.name[0] != '\0';
    int n_rows  = 2 + (has_gpu ? 1 : 0);

    if (!o->sized) {
        int h = rows_height(n_rows);
        glfwSetWindowSize(o->window, OVL_W, h);
        glfwSetWindowPos(o->window, o->mon_x + o->mon_w - OVL_W - OVL_MARGIN, o->mon_y + OVL_MARGIN);
        o->sized = 1;
    }

    int w, h;
    glfwGetWindowSize(o->window, &w, &h);

    glfwMakeContextCurrent(o->window);
    nk_glfw3_new_frame(&o->nkglfw);

    char buf[64];

    if (nk_begin_titled(o->ctx, "ovl", "",
                        nk_rect(0, 0, (float)w, (float)h),
                        NK_WINDOW_NO_SCROLLBAR)) {

        struct nk_color gray = nk_rgba(140, 140, 140, 255);

        /* ── CPU ── */
        nk_layout_row_begin(o->ctx, NK_STATIC, ROW_H, 3);
            nk_layout_row_push(o->ctx, COL_LABEL);
            nk_label_colored(o->ctx, "CPU", NK_TEXT_LEFT, gray);

            nk_layout_row_push(o->ctx, COL_VALUE);
            snprintf(buf, sizeof(buf), "%.1f%%", snap->cpu_rt.load);
            nk_label_colored(o->ctx, buf, NK_TEXT_LEFT, col_load(snap->cpu_rt.load));

            nk_layout_row_push(o->ctx, COL_TEMP);
            if (snap->cpu_rt.cpu_temp >= 0) {
                snprintf(buf, sizeof(buf), "%d\xc2\xb0""C", snap->cpu_rt.cpu_temp);
                nk_label_colored(o->ctx, buf, NK_TEXT_LEFT, col_temp(snap->cpu_rt.cpu_temp));
            } else {
                nk_spacer(o->ctx);
            }
        nk_layout_row_end(o->ctx);

        /* ── GPU ── */
        if (has_gpu) {
            nk_layout_row_begin(o->ctx, NK_STATIC, ROW_H, 3);
                nk_layout_row_push(o->ctx, COL_LABEL);
                nk_label_colored(o->ctx, "GPU", NK_TEXT_LEFT, gray);

                nk_layout_row_push(o->ctx, COL_VALUE);
                snprintf(buf, sizeof(buf), "%.1f%%", snap->gpu_rt.vram_load);
                nk_label_colored(o->ctx, buf, NK_TEXT_LEFT, col_load(snap->gpu_rt.vram_load));

                nk_layout_row_push(o->ctx, COL_TEMP);
                snprintf(buf, sizeof(buf), "%d\xc2\xb0""C", snap->gpu_rt.clock_temp);
                nk_label_colored(o->ctx, buf, NK_TEXT_LEFT, col_temp(snap->gpu_rt.clock_temp));
            nk_layout_row_end(o->ctx);
        }

        /* ── RAM ── */
        nk_layout_row_begin(o->ctx, NK_STATIC, ROW_H, 3);
            nk_layout_row_push(o->ctx, COL_LABEL);
            nk_label_colored(o->ctx, "RAM", NK_TEXT_LEFT, gray);

            nk_layout_row_push(o->ctx, COL_VALUE);
            if (snap->data.ram.total_mb > 0) {
                double pct = (double)snap->ram_rt.used_mb * 100.0 / (double)snap->data.ram.total_mb;
                snprintf(buf, sizeof(buf), "%.1f/%.0f GB",
                         snap->ram_rt.used_mb  / 1024.0,
                         snap->data.ram.total_mb / 1024.0);
                nk_label_colored(o->ctx, buf, NK_TEXT_LEFT, col_load(pct));
            } else {
                nk_label_colored(o->ctx, "N/A", NK_TEXT_LEFT, gray);
            }

            nk_layout_row_push(o->ctx, COL_TEMP);
            nk_spacer(o->ctx);
        nk_layout_row_end(o->ctx);
    }
    nk_end(o->ctx);

    int fbw, fbh;
    glfwGetFramebufferSize(o->window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    nk_glfw3_render(&o->nkglfw, NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
    glfwSwapBuffers(o->window);

    glfwMakeContextCurrent(main_win);
}

void overlay_shutdown(OverlayState *o) {
    if (!o->window) return;
    GLFWwindow *prev = glfwGetCurrentContext();
    glfwMakeContextCurrent(o->window);
    nk_glfw3_shutdown(&o->nkglfw);
    glfwMakeContextCurrent(prev);
    glfwDestroyWindow(o->window);
    o->window = NULL;
}
