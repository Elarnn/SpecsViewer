// app.c
#include "app.h"
#include "ui/ui.h"
#include "ui/nk_common.h"
#include "config.h"
#include "update/update_dialog.h"
#include "net/bench_sync.h"
#include <stdio.h>

#define FAIL(MSG) \
    do { MessageBoxA(NULL, MSG, "SpecsViewer error", MB_OK | MB_ICONERROR); return 0; } while (0)

int app_init(struct AppState *S, int w, int h, const char *title) {
    /* GLFW + window */
    if (!glfwInit())
        FAIL("glfwInit failed");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED,  GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE,    GLFW_FALSE);

    S->window = glfwCreateWindow(w, h, title, NULL, NULL);
    if (!S->window) {
        glfwTerminate();
        FAIL("glfwCreateWindow failed (OpenGL not supported)");
    }
    glfwMakeContextCurrent(S->window);
    glfwSwapInterval(1);

    snprintf(S->title, sizeof(S->title), "%s", title);

    /* Win32: icon, drag WndProc, DWM sharp corners */
    titlebar_win32_setup(S->window);

    /* OpenGL */
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        FAIL("gladLoadGLLoader failed");

    /* UI icons */
    ui_icons_load(S);

    /* App state defaults — must come before config_load so config can override */
    InitializeCriticalSection(&S->scan_cs);
    S->scan_thread     = NULL;
    S->scan_stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    S->scan_state      = SCAN_IDLE;
    S->scan_progress   = 0;
    S->drv_ready       = 0;
    S->drv_win_open    = 0;
    S->fp_win_open     = 0;
    S->fp_text[0]      = '\0';

    S->updater.state         = UPDATE_IDLE;
    S->updater.thread        = NULL;
    S->updater.latest_ver[0] = '\0';
    S->updater_asked   = 0;
    S->update_dont_ask = 0;

    /* Nuklear: init, fonts, default theme */
    nk_setup_init(S);
    config_load(S); /* override theme and dont_ask from saved config */

    bench_init(&S->bench);
    bench_sync_init(&S->bench_sync);
    S->overlay_enabled = 0;
    overlay_init(&S->overlay, S->window);

    /* Backend + updater */
    if (!backend_start(&S->backend))
        FAIL("backend_start failed");

    update_check_start(&S->updater);
    bench_sync_fetch_refs(&S->bench_sync);

    glfwShowWindow(S->window);
    return 1;
}

void app_frame(struct AppState *S) {
    if (!S->updater_asked && !S->update_dont_ask &&
        (int)S->updater.state == UPDATE_AVAILABLE) {
        S->updater_asked = 1;

        int dont_ask = 0;
        int yes = update_dialog_show(S->updater.latest_ver, &dont_ask);
        if (dont_ask) {
            S->update_dont_ask = 1;
            config_save(S);
        }
        if (yes)
            update_launch_updater(&S->updater);
    }

    glfwPollEvents();
    nk_glfw3_new_frame(&S->nkglfw);

    backend_read(&S->backend, &S->snap);
    sensors_update_graphs(S);
    ui_frame(S);

    nk_setup_render(S);

    if (S->overlay_enabled && S->overlay.hwnd)
        overlay_frame(&S->overlay, &S->snap, S->window);
}

void app_shutdown(struct AppState *S) {
    config_save(S);
    ui_icons_free(S);

    if (S->scan_stop_event) SetEvent(S->scan_stop_event);
    if (S->scan_thread) {
        WaitForSingleObject(S->scan_thread, INFINITE);
        CloseHandle(S->scan_thread);
        S->scan_thread = NULL;
    }
    if (S->scan_stop_event) {
        CloseHandle(S->scan_stop_event);
        S->scan_stop_event = NULL;
    }
    DeleteCriticalSection(&S->scan_cs);

    update_cleanup(&S->updater);
    overlay_shutdown(&S->overlay);
    bench_sync_shutdown(&S->bench_sync);
    bench_shutdown(&S->bench);
    backend_stop(&S->backend);

    nk_setup_shutdown(S);

    if (S->window) glfwDestroyWindow(S->window);
    glfwTerminate();
}
