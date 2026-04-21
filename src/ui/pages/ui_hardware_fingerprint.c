#include "app.h"
#include "ui/nk_common.h"

void ui_window_hardware_fingerprint(struct nk_context *ctx, struct AppState *A, int ww,
                           int wh) {
  if (!A->fp_win_open)
    return;

  float w = 420.0f;
  float h = 150.0f;
  struct nk_rect r = nk_rect(((float)ww - w) * 0.5f, ((float)wh - h) * 0.5f, w,
                             h);

  nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE;

  if (!nk_begin(ctx, "Hardware fingerprint", r, flags)) {
    nk_end(ctx);
    return;
  }
  nk_window_set_focus(ctx, "Hardware fingerprint");

  nk_layout_row_dynamic(ctx, 24, 1);
  nk_label_wrap(ctx, A->fp_text[0] ? A->fp_text : "No hardware fingerprint data.");

  nk_layout_row_dynamic(ctx, 28, 1);
  if (nk_button_label(ctx, "OK")) {
    A->fp_win_open = 0;
    nk_window_set_focus(ctx, "Root");
  }

  nk_end(ctx);
}
