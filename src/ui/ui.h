// ui.h
#pragma once
#include "ui/nk_common.h"

#define TITLEBAR_H          28
#define TITLEBAR_BTN_W      40
#define TITLEBAR_THEMES_W   68   /* width of the "Themes" button */
#define TITLEBAR_LEFT_ZONE 320   /* WndProc: left interactive area (title + Themes) */

enum AppTab {
  TAB_CPU = 0,
  TAB_RAM_MBOARD,
  TAB_GPU,
  TAB_SECURE,
  TAB_SENSORS,
  TAB_BENCH,
  TAB_ABOUT
};

struct AppState; // из app.h

/* top-level контейнер кадра */
void ui_frame(struct AppState *S);

/* Win32-специфичная инициализация окна: иконка, WndProc-drag, DWM-углы */
void titlebar_win32_setup(GLFWwindow *win);

/* заголовок окна (custom title bar) */
void ui_titlebar(struct nk_context *ctx, struct AppState *S);

/* выезжающая панель смены темы */
void ui_theme_panel(struct nk_context *ctx, struct AppState *S, int ww, int wh);


/* верхняя панель */
void ui_topbar(struct nk_context *ctx, enum AppTab *active);

/* страницы */
void ui_page_cpu(struct nk_context *ctx, const struct AppState *S);
void ui_page_ram(struct nk_context *ctx, const struct AppState *S);
void ui_page_gpu(struct nk_context *ctx, const struct AppState *S);
void ui_page_about(struct nk_context *ctx, struct AppState *S);
void ui_page_secure(struct nk_context *ctx, const struct AppState *S);
void ui_page_sensors(struct nk_context *ctx, const struct AppState *S);
void sensors_update_graphs(const struct AppState *S);
void ui_page_bench(struct nk_context *ctx, const struct AppState *S);
void ui_window_drivers(struct nk_context *ctx, struct AppState *S, int ww,
                       int wh);
void ui_window_hardware_fingerprint(struct nk_context *ctx, struct AppState *S, int ww,
                           int wh);
void ui_window_usermode_warn(struct nk_context *ctx, struct AppState *S, int ww,
                             int wh);
/* generic dropdown shutter */
typedef struct { const char *label; int id; } UiShutterItem;

int ui_shutter(struct nk_context   *ctx,
               const char          *panel_id,
               float                anchor_x,
               float                panel_w,
               int                 *open,
               float               *anim,
               double              *last_t,
               const char          *header,
               const UiShutterItem *items,
               int                  n_items,
               int                  active_id);

int  load_texture_rgba(const char *path, unsigned int *out_tex);
void ui_icons_load(struct AppState *S);
void ui_icons_free(struct AppState *S);

/* Nuklear lifecycle */
void nk_setup_init(struct AppState *S);
void nk_setup_render(struct AppState *S);
void nk_setup_shutdown(struct AppState *S);
