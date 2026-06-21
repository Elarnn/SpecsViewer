// ui_usermode_warn.c
// Warning popup shown when the app is not running as administrator.

#include "app.h"
#include "config.h"
#include "ui/nk_common.h"

void ui_window_usermode_warn(struct nk_context *ctx, struct AppState *S, int ww,
                             int wh) {
  if (!S->usermode_warn_open)
    return;

  float w = 460.0f;
  float h = 200.0f;
  struct nk_rect r =
      nk_rect(((float)ww - w) * 0.5f, ((float)wh - h) * 0.5f, w, h);

  nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_NO_SCROLLBAR;

  if (!nk_begin(ctx, "User mode", r, flags)) {
    nk_end(ctx);
    return;
  }
  nk_window_set_focus(ctx, "User mode");

  struct nk_rect content = nk_window_get_content_region(ctx);
  float btn_h  = 28.0f;
  float gap    = ctx->style.window.spacing.y;
  float bottom_pad = 8.0f;
  float text_h = content.h - btn_h - 2.0f * gap - bottom_pad;
  if (text_h < 0) text_h = 0;

  nk_layout_row_dynamic(ctx, text_h, 1);
  nk_label_wrap(ctx,
    "The application is running in user mode. Some hardware information may "
    "be unavailable. To access full data, please restart as Administrator.");

  nk_layout_row_dynamic(ctx, btn_h, 2);
  if (nk_button_label(ctx, "Don't show again")) {
    S->usermode_warn_dont_ask = 1;
    S->usermode_warn_open = 0;
    config_save(S);
    nk_window_set_focus(ctx, "Root");
  }
  if (nk_button_label(ctx, "OK")) {
    S->usermode_warn_open = 0;
    nk_window_set_focus(ctx, "Root");
  }

  nk_end(ctx);
}
