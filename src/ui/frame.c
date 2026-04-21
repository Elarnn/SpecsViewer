#include "app.h"
#include "ui.h"
#include "ui/nk_common.h"


void ui_frame(struct AppState *S) {
  struct nk_context *ctx = S->ctx;
  int ww, wh;
  glfwGetWindowSize(S->window, &ww,
                    &wh); // window: GLFW окно; &ww/&wh: куда записать
                          // ширину/высоту клиентской области

  if (nk_begin(
          ctx, "Root",
          nk_rect(0, 0, (float)ww,
                  (float)wh), // ctx: контекст; "Root": ID окна; nk_rect(...):
                              // позиция/размер; flags: поведение окна
          NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
    if (!S->drv_win_open && !S->fp_win_open && !S->usermode_warn_open)
      nk_window_set_focus(
          ctx,
          "Root"); // ctx: контекст; "Root": имя окна, которому отдать фокус

    nk_layout_row_dynamic(ctx, 1, 1); // height: высота строки в px; columns:
                                      // число колонок (динамичная ширина)
    ui_topbar(ctx, &S->active); // ctx: контекст; &S->active: указатель на
                                // активную вкладку для переключения

    nk_layout_row_dynamic(
        ctx, (float)wh - 60,
        1); // height: высота области страниц (высота окна минус 60); columns: 1
    if (nk_group_begin(ctx, "Information", // ctx: контекст; "page": ID группы;
                                           // flags: рамка/заголовок/без скролла
                       NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
      switch (S->active) {
      case TAB_CPU:
        ui_page_cpu(ctx, S);
        break;
      case TAB_RAM_MBOARD:
        ui_page_ram(ctx, S);
        break;
      case TAB_GPU:
        ui_page_gpu(ctx, S);
        break;
      case TAB_SECURE:
        ui_page_secure(ctx, S);
        break;
      case TAB_SENSORS:
        ui_page_sensors(ctx, S);
        break;
      case TAB_BENCH:
        ui_page_bench(ctx, S);
        break;
      case TAB_ABOUT:
        ui_page_about(ctx, S);
        break;
      default:
        nk_layout_row_dynamic(ctx, 26, 1); // height: 26 px; columns: 1
        nk_label(ctx,
                 "Page not implemented yet.", // ctx: контекст; text: строка;
                                              // align: выравнивание текста
                 NK_TEXT_LEFT);
        break;
      }
      nk_group_end(ctx); // завершить группу "page"
    }
  }
  nk_end(ctx); // завершить окно "Root"

  ui_window_drivers(ctx, S, ww, wh);
  ui_window_hardware_fingerprint(ctx, S, ww, wh);
  ui_window_usermode_warn(ctx, S, ww, wh);
}
