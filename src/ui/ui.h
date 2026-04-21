// ui.h
#pragma once
#include "ui/nk_common.h"

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

/* верхняя панель */
void ui_topbar(struct nk_context *ctx, enum AppTab *active);

/* страницы */
void ui_page_cpu(struct nk_context *ctx, const struct AppState *S);
void ui_page_ram(struct nk_context *ctx, const struct AppState *S);
void ui_page_gpu(struct nk_context *ctx, const struct AppState *S);
void ui_page_about(struct nk_context *ctx, struct AppState *S);
void ui_page_secure(struct nk_context *ctx, const struct AppState *S);
void ui_page_sensors(struct nk_context *ctx, const struct AppState *S);
void ui_page_bench(struct nk_context *ctx, const struct AppState *S);
void ui_window_drivers(struct nk_context *ctx, struct AppState *S, int ww,
                       int wh);
void ui_window_hardware_fingerprint(struct nk_context *ctx, struct AppState *S, int ww,
                           int wh);
void ui_window_usermode_warn(struct nk_context *ctx, struct AppState *S, int ww,
                             int wh);
int load_texture_rgba(const char *path, unsigned int *out_tex);
